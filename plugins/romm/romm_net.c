#include "romm_net.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "bearssl.h"

#define SLICE_MS 200
#define CA_FILE_MAX (4 * 1024 * 1024)
#define ANCHORS_MAX 512

static void setError(RNCall* call, const char* message) {
	if (!call->error[0]) snprintf(call->error, sizeof(call->error), "%s", message);
}

static long long nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int cancelled(const RNCall* call) {
	return call->cancel && *call->cancel;
}

///////////////////////////////////////
// trust anchors from a PEM file

typedef struct Anchors {
	br_x509_trust_anchor* list;
	size_t count;
} Anchors;

typedef struct Bytes {
	unsigned char* data;
	size_t len, cap;
	int failed;
} Bytes;

static void bytesAppend(void* ctx, const void* buf, size_t len) {
	Bytes* b = ctx;
	if (b->failed) return;
	if (b->len + len > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 1024;
		while (cap < b->len + len) cap *= 2;
		if (cap > CA_FILE_MAX) {
			b->failed = 1;
			return;
		}
		unsigned char* data = realloc(b->data, cap);
		if (!data) {
			b->failed = 1;
			return;
		}
		b->data = data;
		b->cap = cap;
	}
	memcpy(b->data + b->len, buf, len);
	b->len += len;
}

static void* copyOf(const void* data, size_t len) {
	void* out = malloc(len ? len : 1);
	if (out && len) memcpy(out, data, len);
	return out;
}

static void anchorFree(br_x509_trust_anchor* ta) {
	free(ta->dn.data);
	if (ta->pkey.key_type == BR_KEYTYPE_RSA) {
		free(ta->pkey.key.rsa.n);
		free(ta->pkey.key.rsa.e);
	} else if (ta->pkey.key_type == BR_KEYTYPE_EC) {
		free(ta->pkey.key.ec.q);
	}
}

static void anchorsFree(Anchors* a) {
	for (size_t i = 0; i < a->count; i++) anchorFree(&a->list[i]);
	free(a->list);
	memset(a, 0, sizeof(*a));
}

// Makes a trust anchor of a DER certificate. Returns 0, or -1 for one
// BearSSL can't read.
static int anchorOf(const unsigned char* der, size_t len, br_x509_trust_anchor* out) {
	Bytes dn = { 0 };
	br_x509_decoder_context dc;
	br_x509_decoder_init(&dc, bytesAppend, &dn);
	br_x509_decoder_push(&dc, der, len);
	const br_x509_pkey* pk = br_x509_decoder_get_pkey(&dc);
	if (!pk || dn.failed) {
		free(dn.data);
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->dn.data = dn.data;
	out->dn.len = dn.len;
	out->flags = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;
	out->pkey.key_type = pk->key_type;
	if (pk->key_type == BR_KEYTYPE_RSA) {
		out->pkey.key.rsa.n = copyOf(pk->key.rsa.n, pk->key.rsa.nlen);
		out->pkey.key.rsa.nlen = pk->key.rsa.nlen;
		out->pkey.key.rsa.e = copyOf(pk->key.rsa.e, pk->key.rsa.elen);
		out->pkey.key.rsa.elen = pk->key.rsa.elen;
	} else if (pk->key_type == BR_KEYTYPE_EC) {
		out->pkey.key.ec.curve = pk->key.ec.curve;
		out->pkey.key.ec.q = copyOf(pk->key.ec.q, pk->key.ec.qlen);
		out->pkey.key.ec.qlen = pk->key.ec.qlen;
	} else {
		free(dn.data);
		return -1;
	}
	return 0;
}

static int anchorsLoad(const char* path, Anchors* out) {
	memset(out, 0, sizeof(*out));
	FILE* f = fopen(path, "rb");
	if (!f) return -1;
	struct stat st;
	if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > CA_FILE_MAX) {
		fclose(f);
		return -1;
	}
	out->list = calloc(ANCHORS_MAX, sizeof(br_x509_trust_anchor));
	if (!out->list) {
		fclose(f);
		return -1;
	}
	br_pem_decoder_context pc;
	br_pem_decoder_init(&pc);
	Bytes der = { 0 };
	int in_cert = 0;
	unsigned char buf[4096];
	size_t got;
	while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
		size_t off = 0;
		while (off < got) {
			off += br_pem_decoder_push(&pc, buf + off, got - off);
			switch (br_pem_decoder_event(&pc)) {
			case BR_PEM_BEGIN_OBJ:
				in_cert = !strcmp(br_pem_decoder_name(&pc), "CERTIFICATE")
					|| !strcmp(br_pem_decoder_name(&pc), "X509 CERTIFICATE");
				der.len = 0;
				der.failed = 0;
				br_pem_decoder_setdest(&pc, in_cert ? bytesAppend : NULL, in_cert ? &der : NULL);
				break;
			case BR_PEM_END_OBJ:
				if (in_cert && !der.failed && out->count < ANCHORS_MAX
						&& anchorOf(der.data, der.len, &out->list[out->count]) == 0) {
					out->count++;
				}
				in_cert = 0;
				break;
			case BR_PEM_ERROR:
				off = got; // skip to the next object
				br_pem_decoder_init(&pc);
				in_cert = 0;
				break;
			}
		}
	}
	free(der.data);
	fclose(f);
	return out->count ? 0 : -1;
}

int RN_countAnchors(const char* path) {
	Anchors a;
	if (anchorsLoad(path, &a) != 0) {
		anchorsFree(&a);
		return -1;
	}
	int n = (int)a.count;
	anchorsFree(&a);
	return n;
}

int RN_systemCa(const char* sdcard, const char* platform, char* out, size_t size) {
	char own[RM_PATH_MAX];
	snprintf(own, sizeof(own), "%s/.system/%s/etc/ssl/certs/ca-certificates.crt", sdcard ? sdcard : "",
		platform ? platform : "");
	const char* env_curl = getenv("CURL_CA_BUNDLE");
	const char* env_ssl = getenv("SSL_CERT_FILE");
	const char* places[] = {
		env_ssl,
		env_curl,
		own, // NextUI ships one on h700, which curl there is pointed at
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/ssl/cert.pem",
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/ca-bundle.pem",
	};
	for (size_t i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
		struct stat st;
		if (!places[i] || !places[i][0] || stat(places[i], &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) continue;
		snprintf(out, size, "%s", places[i]);
		return 0;
	}
	return -1;
}

///////////////////////////////////////
// a certificate pinned by its SHA-256

typedef struct Pinned {
	const br_x509_class* vtable;
	const unsigned char* pin;
	int depth;
	br_sha256_context hash;
	br_x509_decoder_context decoder;
	int matched;
	br_x509_pkey pkey;
	unsigned char key_copy[1024];
} Pinned;

static void pinStartChain(const br_x509_class** ctx, const char* server_name) {
	Pinned* p = (Pinned*)ctx;
	(void)server_name; // the pin stands in for the name: it's that one certificate
	p->depth = 0;
	p->matched = 0;
}

static void pinStartCert(const br_x509_class** ctx, uint32_t length) {
	Pinned* p = (Pinned*)ctx;
	(void)length;
	if (p->depth == 0) {
		br_sha256_init(&p->hash);
		br_x509_decoder_init(&p->decoder, NULL, NULL);
	}
}

static void pinAppend(const br_x509_class** ctx, const unsigned char* buf, size_t len) {
	Pinned* p = (Pinned*)ctx;
	if (p->depth != 0) return;
	br_sha256_update(&p->hash, buf, len);
	br_x509_decoder_push(&p->decoder, buf, len);
}

static void pinEndCert(const br_x509_class** ctx) {
	Pinned* p = (Pinned*)ctx;
	if (p->depth == 0) {
		unsigned char digest[32];
		br_sha256_out(&p->hash, digest);
		const br_x509_pkey* pk = br_x509_decoder_get_pkey(&p->decoder);
		if (pk && memcmp(digest, p->pin, 32) == 0) {
			p->pkey = *pk;
			// the decoder's key is inside the decoder; keep a copy that outlives it
			if (pk->key_type == BR_KEYTYPE_RSA && pk->key.rsa.nlen + pk->key.rsa.elen <= sizeof(p->key_copy)) {
				memcpy(p->key_copy, pk->key.rsa.n, pk->key.rsa.nlen);
				memcpy(p->key_copy + pk->key.rsa.nlen, pk->key.rsa.e, pk->key.rsa.elen);
				p->pkey.key.rsa.n = p->key_copy;
				p->pkey.key.rsa.e = p->key_copy + pk->key.rsa.nlen;
				p->matched = 1;
			} else if (pk->key_type == BR_KEYTYPE_EC && pk->key.ec.qlen <= sizeof(p->key_copy)) {
				memcpy(p->key_copy, pk->key.ec.q, pk->key.ec.qlen);
				p->pkey.key.ec.q = p->key_copy;
				p->matched = 1;
			}
		}
	}
	p->depth++;
}

static unsigned pinEndChain(const br_x509_class** ctx) {
	Pinned* p = (Pinned*)ctx;
	return p->matched ? 0 : BR_ERR_X509_NOT_TRUSTED;
}

static const br_x509_pkey* pinGetPkey(const br_x509_class* const* ctx, unsigned* usages) {
	Pinned* p = (Pinned*)ctx;
	if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
	return p->matched ? &p->pkey : NULL;
}

static const br_x509_class pin_class = {
	sizeof(Pinned), pinStartChain, pinStartCert, pinAppend, pinEndCert, pinEndChain, pinGetPkey,
};

///////////////////////////////////////
// a connection

typedef struct Conn {
	int fd;
	int tls;
	const RNConfig* config;
	RNCall* call;
	br_ssl_client_context sc;
	br_x509_minimal_context xc;
	Pinned pinned;
	br_sslio_context io;
	Anchors anchors;
	unsigned char buf[BR_SSL_BUFSIZE_BIDI];
} Conn;

// waits for fd to be ready for events, in slices so cancel is seen.
// Returns 1 when ready, 0 on timeout or cancel (with the error set), -1 on error.
static int waitFor(Conn* c, short events, int timeout_ms) {
	long long until = nowMs() + timeout_ms;
	for (;;) {
		if (cancelled(c->call)) {
			setError(c->call, "cancelled");
			return 0;
		}
		long long left = until - nowMs();
		if (left <= 0) {
			setError(c->call, "the server stopped answering");
			return 0;
		}
		struct pollfd p = { c->fd, events, 0 };
		int n = poll(&p, 1, left < SLICE_MS ? (int)left : SLICE_MS);
		if (n < 0 && errno == EINTR) continue;
		if (n < 0) return -1;
		if (n > 0) return 1;
	}
}

static int rawRead(Conn* c, unsigned char* data, size_t len) {
	for (;;) {
		int ready = waitFor(c, POLLIN, c->config->io_ms);
		if (ready <= 0) return -1;
		ssize_t n = recv(c->fd, data, len, 0);
		if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
		if (n < 0) {
			setError(c->call, "the connection broke");
			return -1;
		}
		return (int)n; // 0 at the end
	}
}

static int rawWrite(Conn* c, const unsigned char* data, size_t len) {
	for (;;) {
		int ready = waitFor(c, POLLOUT, c->config->io_ms);
		if (ready <= 0) return -1;
		int flags = 0;
#ifdef MSG_NOSIGNAL
		flags = MSG_NOSIGNAL;
#endif
		ssize_t n = send(c->fd, data, len, flags);
		if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
		if (n <= 0) {
			setError(c->call, "the connection broke");
			return -1;
		}
		return (int)n;
	}
}

static int tlsLowRead(void* ctx, unsigned char* data, size_t len) {
	int n = rawRead(ctx, data, len);
	return n <= 0 ? -1 : n;
}

static int tlsLowWrite(void* ctx, const unsigned char* data, size_t len) {
	return rawWrite(ctx, data, len);
}

static void tlsError(Conn* c) {
	int err = br_ssl_engine_last_error(&c->sc.eng);
	char message[200];
	switch (err) {
	case BR_ERR_OK: return;
	case BR_ERR_X509_NOT_TRUSTED:
		snprintf(message, sizeof(message), c->config->have_pin
			? "the server's certificate doesn't match the pinned fingerprint"
			: "the server's certificate isn't from a trusted authority");
		break;
	case BR_ERR_X509_BAD_SERVER_NAME: snprintf(message, sizeof(message), "the server's certificate is for another name"); break;
	case BR_ERR_X509_EXPIRED: snprintf(message, sizeof(message), "the server's certificate has expired or isn't valid yet (check the clock)"); break;
	default: snprintf(message, sizeof(message), "the secure connection failed (BearSSL error %d)", err); break;
	}
	// a cancel or timeout already said why
	setError(c->call, message);
}

static int connRead(Conn* c, unsigned char* data, size_t len) {
	if (!c->tls) return rawRead(c, data, len);
	int n = br_sslio_read(&c->io, data, len);
	if (n < 0) {
		int err = br_ssl_engine_last_error(&c->sc.eng);
		if (err == BR_ERR_OK && !c->call->error[0]) return 0; // closed cleanly
		tlsError(c);
		return -1;
	}
	return n;
}

static int connWriteAll(Conn* c, const void* data, size_t len) {
	if (c->tls) {
		if (br_sslio_write_all(&c->io, data, len) != 0) {
			tlsError(c);
			setError(c->call, "the connection broke");
			return -1;
		}
		return 0;
	}
	const unsigned char* p = data;
	while (len) {
		int n = rawWrite(c, p, len);
		if (n <= 0) return -1;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int connFlush(Conn* c) {
	if (c->tls && br_sslio_flush(&c->io) != 0) {
		tlsError(c);
		setError(c->call, "the connection broke");
		return -1;
	}
	return 0;
}

static void connClose(Conn* c) {
	if (c->fd >= 0) close(c->fd);
	c->fd = -1;
	anchorsFree(&c->anchors);
}

static int tcpConnect(Conn* c) {
	const RMUrl* url = &c->config->url;
	char port[8];
	snprintf(port, sizeof(port), "%d", url->port);
	struct addrinfo hints, *found = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int rc = getaddrinfo(url->host, port, &hints, &found);
	if (rc != 0 || !found) {
		setError(c->call, "can't find the server (check its name and the network)");
		return -1;
	}
	for (struct addrinfo* a = found; a; a = a->ai_next) {
		if (cancelled(c->call)) break;
		int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0) continue;
		fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
		int one = 1;
		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
		if (connect(fd, a->ai_addr, a->ai_addrlen) != 0 && errno != EINPROGRESS) {
			close(fd);
			continue;
		}
		c->fd = fd;
		int ready = waitFor(c, POLLOUT, c->config->connect_ms);
		int err = 0;
		socklen_t err_len = sizeof(err);
		if (ready == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) == 0 && err == 0) {
			freeaddrinfo(found);
			if (!cancelled(c->call)) c->call->error[0] = '\0'; // an earlier address's timeout
			return 0;
		}
		close(fd);
		c->fd = -1;
	}
	freeaddrinfo(found);
	setError(c->call, cancelled(c->call) ? "cancelled" : "can't connect to the server");
	return -1;
}

static int tlsStart(Conn* c) {
	const RNConfig* cfg = c->config;
	int address = RM_hostIsAddress(cfg->url.host);
	if (cfg->have_pin) {
		br_ssl_client_init_full(&c->sc, &c->xc, NULL, 0);
		memset(&c->pinned, 0, sizeof(c->pinned));
		c->pinned.vtable = &pin_class;
		c->pinned.pin = cfg->pin;
		br_ssl_engine_set_x509(&c->sc.eng, &c->pinned.vtable);
	} else {
		if (address) {
			setError(c->call, "for https:// to an IP address, pin the server's certificate or use its name");
			return -1;
		}
		if (!cfg->ca_file[0]) {
			setError(c->call, "no CA certificates to check the server with: set a CA file or pin the certificate");
			return -1;
		}
		if (anchorsLoad(cfg->ca_file, &c->anchors) != 0) {
			setError(c->call, "can't read any certificates from the CA file");
			return -1;
		}
		br_ssl_client_init_full(&c->sc, &c->xc, c->anchors.list, c->anchors.count);
	}
	br_ssl_engine_set_buffer(&c->sc.eng, c->buf, sizeof(c->buf), 1);
	// the name is sent and checked; with a pin, an address isn't sent at all
	if (!br_ssl_client_reset(&c->sc, address ? NULL : cfg->url.host, 0)) {
		setError(c->call, "can't start the secure connection");
		return -1;
	}
	br_sslio_init(&c->io, &c->sc.eng, tlsLowRead, c, tlsLowWrite, c);
	return 0;
}

///////////////////////////////////////
// a request

typedef struct Body {
	char* data;
	size_t len, cap;
	FILE* file;
	long long written;
} Body;

static int bodyPut(RNCall* call, Body* b, const char* data, size_t len) {
	if (!len) return 0;
	if (call->max_body >= 0 && b->written + (long long)len > call->max_body) {
		setError(call, "the server sent more than expected");
		return -1;
	}
	if (b->file) {
		if (fwrite(data, 1, len, b->file) != len) {
			setError(call, "can't write the file (is the card full?)");
			return -1;
		}
	} else {
		if (b->len + len + 1 > b->cap) {
			size_t cap = b->cap ? b->cap : 16384;
			while (cap < b->len + len + 1) cap *= 2;
			char* data2 = realloc(b->data, cap);
			if (!data2) {
				setError(call, "out of memory");
				return -1;
			}
			b->data = data2;
			b->cap = cap;
		}
		memcpy(b->data + b->len, data, len);
		b->len += len;
		b->data[b->len] = '\0';
	}
	b->written += (long long)len;
	if (call->done) *call->done = b->written;
	return 0;
}

static int sendBody(Conn* c, RNCall* call) {
	if (call->body && connWriteAll(c, call->body, call->body_len) != 0) return -1;
	if (call->body_file) {
		if (call->body_before && connWriteAll(c, call->body_before, strlen(call->body_before)) != 0) return -1;
		FILE* f = fopen(call->body_file, "rb");
		if (!f) {
			setError(call, "can't read the file to send");
			return -1;
		}
		char buf[16384];
		size_t n;
		long long sent = 0;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
			if (connWriteAll(c, buf, n) != 0) {
				fclose(f);
				return -1;
			}
			sent += (long long)n;
			if (call->done) *call->done = sent;
		}
		fclose(f);
		if (call->body_after && connWriteAll(c, call->body_after, strlen(call->body_after)) != 0) return -1;
	}
	return connFlush(c);
}

static long long fileSize(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? (long long)st.st_size : -1;
}

// One exchange. Returns 0 with the status and head, or -1.
static int exchange(const RNConfig* cfg, RNCall* call, RMHead* head, Body* body) {
	Conn* c = calloc(1, sizeof(Conn));
	if (!c) {
		setError(call, "out of memory");
		return -1;
	}
	c->fd = -1;
	c->config = cfg;
	c->call = call;
	c->tls = cfg->url.https;
	int rc = -1;
	long long length = -1;
	if (call->body) length = (long long)call->body_len;
	if (call->body_file) {
		long long size = fileSize(call->body_file);
		if (size < 0) {
			setError(call, "can't read the file to send");
			goto done;
		}
		length = size + (long long)strlen(call->body_before ? call->body_before : "")
			+ (long long)strlen(call->body_after ? call->body_after : "");
	}
	char extra[256] = "";
	if (call->content_type) snprintf(extra, sizeof(extra), "Content-Type: %s\r\n", call->content_type);
	char request[RM_PATH_MAX * 2 + RN_AUTH_MAX + 512];
	int request_len = RM_requestHead(call->method, &cfg->url, call->target, cfg->auth, extra, length, request,
		sizeof(request));
	if (request_len < 0) {
		setError(call, "the request is too long");
		goto done;
	}
	if (tcpConnect(c) != 0) goto done;
	if (c->tls && tlsStart(c) != 0) goto done;
	int sent = connWriteAll(c, request, (size_t)request_len) == 0 && sendBody(c, call) == 0;
	memset(request, 0, sizeof(request)); // it has the login in it
	if (!sent) goto done;

	char buf[RM_HEAD_MAX + 16384];
	size_t have = 0;
	int end = 0;
	while (!end) {
		if (have >= sizeof(buf)) {
			setError(call, "the server's answer is malformed");
			goto done;
		}
		int n = connRead(c, (unsigned char*)buf + have, sizeof(buf) - have);
		if (n <= 0) {
			setError(call, "the server closed the connection");
			goto done;
		}
		have += (size_t)n;
		end = RM_headEnd(buf, have);
		if (end < 0) {
			setError(call, "the server's answer is malformed");
			goto done;
		}
	}
	if (RM_headParse(buf, (size_t)end, head) != 0) {
		setError(call, "the server's answer isn't HTTP");
		goto done;
	}
	int redirect = head->status >= 300 && head->status < 400 && head->location[0];
	if (redirect) {
		rc = 0;
		goto done;
	}
	if (head->length >= 0 && call->max_body >= 0 && head->length > call->max_body) {
		setError(call, "the server's answer is bigger than expected");
		goto done;
	}
	if (call->total) *call->total = head->length;
	// the body: what came with the head, then the rest
	RMChunks chunks = { 0 };
	size_t first = have - (size_t)end;
	memmove(buf, buf + end, first);
	size_t len = first;
	int finished = 0;
	for (;;) {
		if (len) {
			size_t out = len;
			if (head->chunked) {
				int state = RM_chunksFeed(&chunks, buf, len, &out);
				if (state < 0) {
					setError(call, "the server's answer is malformed");
					goto done;
				}
				if (state == 1) finished = 1;
			} else if (head->length >= 0 && body->written + (long long)out > head->length) {
				out = (size_t)(head->length - body->written);
			}
			if (bodyPut(call, body, buf, out) != 0) goto done;
		}
		if (finished || (!head->chunked && head->length >= 0 && body->written >= head->length)) break;
		int n = connRead(c, (unsigned char*)buf, sizeof(buf));
		if (n < 0) goto done;
		if (n == 0) {
			if (head->chunked || head->length >= 0) {
				setError(call, "the download was cut short");
				goto done;
			}
			break; // the body ran to the close
		}
		len = (size_t)n;
	}
	rc = 0;
done:
	connClose(c);
	free(c);
	if (rc != 0 && cancelled(call)) {
		call->error[0] = '\0';
		setError(call, "cancelled");
	}
	return rc;
}

int RN_run(const RNConfig* cfg, RNCall* call) {
	call->status = 0;
	call->data = NULL;
	call->len = 0;
	call->error[0] = '\0';
	for (int hop = 0; hop <= RM_REDIRECTS_MAX; hop++) {
		Body body = { 0 };
		if (call->out_path) {
			int fd = open(call->out_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
			body.file = fd >= 0 ? fdopen(fd, "wb") : NULL;
			if (!body.file) {
				if (fd >= 0) close(fd);
				setError(call, "can't create the file");
				return -1;
			}
		}
		if (call->done) *call->done = 0;
		RMHead head;
		int rc = exchange(cfg, call, &head, &body);
		if (body.file) {
			if (fflush(body.file) != 0 || fsync(fileno(body.file)) != 0) {
				if (rc == 0) setError(call, "can't write the file (is the card full?)");
				rc = -1;
			}
			fclose(body.file);
		}
		if (rc != 0) {
			free(body.data);
			return -1;
		}
		call->status = head.status;
		if (head.status >= 300 && head.status < 400 && head.location[0]) {
			free(body.data);
			if (strcmp(call->method, "GET") != 0) {
				setError(call, "the server redirected an upload");
				return -1;
			}
			char target[RM_PATH_MAX];
			RMUrl here = cfg->url;
			if (RM_redirect(&here, head.location, target, sizeof(target)) != 0) {
				setError(call, "the server redirected to another server, which isn't followed");
				return -1;
			}
			// a redirect's path is the whole path, base included
			size_t base = strlen(cfg->url.base);
			if (base && strncmp(target, cfg->url.base, base) == 0 && (target[base] == '/' || target[base] == '\0')) {
				memmove(target, target + base, strlen(target + base) + 1);
				if (!target[0]) snprintf(target, sizeof(target), "/");
			} else if (base) {
				setError(call, "the server redirected outside RomM");
				return -1;
			}
			snprintf(call->target, sizeof(call->target), "%s", target);
			continue;
		}
		call->data = body.data;
		call->len = body.len;
		if (!call->data) call->data = calloc(1, 1);
		return 0;
	}
	setError(call, "the server redirected too many times");
	return -1;
}

void RN_free(RNCall* call) {
	free(call->data);
	call->data = NULL;
	call->len = 0;
}

int RN_md5File(const char* path, long long max, char out[33]) {
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return -1;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > max) {
		close(fd);
		return -1;
	}
	br_md5_context md5;
	br_md5_init(&md5);
	unsigned char buf[16384];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) br_md5_update(&md5, buf, (size_t)n);
	close(fd);
	if (n < 0) return -1;
	unsigned char digest[16];
	br_md5_out(&md5, digest);
	RM_hex(digest, sizeof(digest), out, 33);
	return 0;
}
