#include "romm_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// English the caller's words translate, for make pot
#define TR_MARK(text) (text)

static RMWords words; // none set: English

void RM_setWords(const RMWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

///////////////////////////////////////
// URLs

static int hostChar(int c) {
	return isalnum(c) || c == '.' || c == '-';
}

int RM_urlParse(const char* text, RMUrl* out, char* error, size_t size) {
	memset(out, 0, sizeof(*out));
	if (error && size) error[0] = '\0';
#define FAIL(why) \
	do { \
		if (error) snprintf(error, size, "%s", why); \
		return -1; \
	} while (0)
	if (!text) FAIL("no server set");
	while (*text == ' ' || *text == '\t') text++;
	const char* rest;
	if (!strncasecmp(text, "http://", 7)) {
		out->https = 0;
		out->port = 80;
		rest = text + 7;
	} else if (!strncasecmp(text, "https://", 8)) {
		out->https = 1;
		out->port = 443;
		rest = text + 8;
	} else {
		FAIL("the server must start with http:// or https://");
	}
	const char* slash = strchr(rest, '/');
	size_t authority = slash ? (size_t)(slash - rest) : strlen(rest);
	// trailing spaces belong to no part
	while (authority > 0 && !slash && (rest[authority - 1] == ' ' || rest[authority - 1] == '\t')) authority--;
	if (memchr(rest, '@', authority)) FAIL("put the user in its own setting, not in the server");
	const char* host = rest;
	size_t host_len;
	const char* after;
	if (*host == '[') {
		const char* close = memchr(host, ']', authority);
		if (!close) FAIL("an IPv6 address needs its closing ]");
		host++;
		host_len = (size_t)(close - host);
		for (size_t i = 0; i < host_len; i++) {
			if (!isxdigit((unsigned char)host[i]) && host[i] != ':' && host[i] != '.') FAIL("that isn't an IPv6 address");
		}
		after = close + 1;
	} else {
		host_len = 0;
		while (host_len < authority && host[host_len] != ':') host_len++;
		for (size_t i = 0; i < host_len; i++) {
			if (!hostChar((unsigned char)host[i])) FAIL("the server's name has a character it can't have");
		}
		after = host + host_len;
	}
	if (host_len == 0) FAIL("the server has no host");
	if (host_len >= sizeof(out->host)) FAIL("the server's name is too long");
	memcpy(out->host, host, host_len);
	out->host[host_len] = '\0';
	const char* end = rest + authority;
	if (after < end) {
		if (*after != ':') FAIL("the server has something after its host");
		after++;
		if (after == end) FAIL("the server's port is missing");
		long port = 0;
		for (const char* p = after; p < end; p++) {
			if (!isdigit((unsigned char)*p)) FAIL("the server's port isn't a number");
			port = port * 10 + (*p - '0');
			if (port > 65535) FAIL("the server's port is too big");
		}
		if (port < 1) FAIL("the server's port is too small");
		out->port = (int)port;
	}
	if (slash) {
		size_t len = strlen(slash);
		while (len > 0 && (slash[len - 1] == ' ' || slash[len - 1] == '\t')) len--;
		while (len > 0 && slash[len - 1] == '/') len--;
		if (len >= sizeof(out->base)) FAIL("the server's path is too long");
		for (size_t i = 0; i < len; i++) {
			unsigned char c = (unsigned char)slash[i];
			if (c <= ' ' || c >= 0x7f || c == '?' || c == '#' || c == '\\') FAIL("the server's path has a character it can't have");
		}
		if (strstr(slash, "/../") || (len >= 3 && !strncmp(slash + len - 3, "/..", 3))) FAIL("the server's path can't have ..");
		memcpy(out->base, slash, len);
		out->base[len] = '\0';
	}
	return 0;
#undef FAIL
}

int RM_hostIsAddress(const char* host) {
	if (strchr(host, ':')) return 1;
	int dots = 0, digits = 0;
	for (const char* p = host; *p; p++) {
		if (*p == '.') {
			if (!digits) return 0;
			dots++;
			digits = 0;
		} else if (isdigit((unsigned char)*p)) {
			digits++;
		} else {
			return 0;
		}
	}
	return dots == 3 && digits > 0;
}

void RM_hostHeader(const RMUrl* url, char* out, size_t size) {
	int v6 = strchr(url->host, ':') != NULL;
	int own = url->port == (url->https ? 443 : 80);
	if (own) snprintf(out, size, v6 ? "[%s]" : "%s", url->host);
	else snprintf(out, size, v6 ? "[%s]:%d" : "%s:%d", url->host, url->port);
}

void RM_urlEncode(const char* text, char* out, size_t size) {
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;
	if (!size) return;
	for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
		int plain = isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~';
		size_t need = plain ? 1 : 3;
		if (o + need >= size) break;
		if (plain) {
			out[o++] = (char)*p;
		} else {
			out[o++] = '%';
			out[o++] = hex[*p >> 4];
			out[o++] = hex[*p & 15];
		}
	}
	out[o] = '\0';
}

static int targetChars(const char* s) {
	for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
		if (*p <= ' ' || *p >= 0x7f) return 0;
	}
	return 1;
}

int RM_redirect(const RMUrl* url, const char* location, char* target, size_t size) {
	if (!location || !location[0] || !targetChars(location)) return -1;
	const char* path;
	if (location[0] == '/') {
		if (location[1] == '/') return -1; // another host, without a scheme
		path = location;
	} else {
		RMUrl to;
		// the URL parser takes no query, so look at the part before it
		char head[RM_PATH_MAX];
		snprintf(head, sizeof(head), "%s", location);
		char* q = strpbrk(head, "?#");
		if (q) *q = '\0';
		if (RM_urlParse(head, &to, NULL, 0) != 0) return -1;
		if (to.https != url->https || to.port != url->port || strcasecmp(to.host, url->host) != 0) return -1;
		const char* scheme_end = strstr(location, "://") + 3;
		path = strchr(scheme_end, '/');
		if (!path) path = "/";
	}
	size_t len = strcspn(path, "#");
	if (len + 1 > size) return -1;
	memcpy(target, path, len);
	target[len] = '\0';
	return 0;
}

///////////////////////////////////////
// Requests and responses

void RM_base64(const unsigned char* data, size_t len, char* out, size_t size) {
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t o = 0;
	if (!size) return;
	for (size_t i = 0; i < len; i += 3) {
		if (o + 4 >= size) break;
		unsigned int v = (unsigned int)data[i] << 16;
		if (i + 1 < len) v |= (unsigned int)data[i + 1] << 8;
		if (i + 2 < len) v |= data[i + 2];
		out[o++] = table[(v >> 18) & 63];
		out[o++] = table[(v >> 12) & 63];
		out[o++] = i + 1 < len ? table[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < len ? table[v & 63] : '=';
	}
	out[o] = '\0';
}

// no CR, LF or NUL can get into a header
static int headerSafe(const char* s) {
	for (; *s; s++) {
		if (*s == '\r' || *s == '\n') return 0;
	}
	return 1;
}

void RM_authHeader(const char* user, const char* secret, char* out, size_t size) {
	if (size) out[0] = '\0';
	if (!secret || !secret[0] || !headerSafe(secret) || (user && !headerSafe(user))) return;
	if (user && user[0]) {
		char pair[1024], encoded[1400];
		snprintf(pair, sizeof(pair), "%s:%s", user, secret);
		RM_base64((const unsigned char*)pair, strlen(pair), encoded, sizeof(encoded));
		snprintf(out, size, "Authorization: Basic %s\r\n", encoded);
		memset(pair, 0, sizeof(pair));
		memset(encoded, 0, sizeof(encoded));
	} else {
		snprintf(out, size, "Authorization: Bearer %s\r\n", secret);
	}
}

int RM_requestHead(const char* method, const RMUrl* url, const char* target, const char* auth,
		const char* extra, long long body_length, char* out, size_t size) {
	if (!target || target[0] != '/' || !targetChars(target)) return -1;
	char host[RM_HOST_MAX + 16];
	RM_hostHeader(url, host, sizeof(host));
	char length[48] = "";
	if (body_length >= 0) snprintf(length, sizeof(length), "Content-Length: %lld\r\n", body_length);
	int n = snprintf(out, size,
		"%s %s%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: mosaic-launcher-romm/1\r\nAccept: */*\r\n"
		"Accept-Encoding: identity\r\nConnection: close\r\n%s%s%s\r\n",
		method, url->base, target, host, auth ? auth : "", extra ? extra : "", length);
	if (n < 0 || (size_t)n >= size) return -1;
	return n;
}

int RM_headEnd(const char* buf, size_t len) {
	for (size_t i = 0; i + 3 < len; i++) {
		if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
			return i + 4 > RM_HEAD_MAX ? -1 : (int)(i + 4);
		}
	}
	return len >= RM_HEAD_MAX ? -1 : 0;
}

int RM_headParse(const char* buf, size_t len, RMHead* out) {
	memset(out, 0, sizeof(*out));
	out->length = -1;
	if (len < 12 || strncmp(buf, "HTTP/1.", 7) != 0 || buf[8] != ' ') return -1;
	if (!isdigit((unsigned char)buf[9]) || !isdigit((unsigned char)buf[10]) || !isdigit((unsigned char)buf[11])) return -1;
	out->status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
	const char* p = memchr(buf, '\n', len);
	const char* end = buf + len;
	while (p && p + 1 < end) {
		const char* line = p + 1;
		const char* eol = memchr(line, '\n', (size_t)(end - line));
		if (!eol) break;
		size_t line_len = (size_t)(eol - line);
		if (line_len && line[line_len - 1] == '\r') line_len--;
		if (!line_len) break;
		const char* colon = memchr(line, ':', line_len);
		if (colon) {
			size_t name_len = (size_t)(colon - line);
			const char* value = colon + 1;
			const char* value_end = line + line_len;
			while (value < value_end && (*value == ' ' || *value == '\t')) value++;
			while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
			size_t value_len = (size_t)(value_end - value);
			if (name_len == 14 && !strncasecmp(line, "content-length", 14)) {
				long long n = 0;
				if (!value_len || value_len > 18) return -1;
				for (size_t i = 0; i < value_len; i++) {
					if (!isdigit((unsigned char)value[i])) return -1;
					n = n * 10 + (value[i] - '0');
				}
				if (out->length >= 0 && out->length != n) return -1;
				out->length = n;
			} else if (name_len == 17 && !strncasecmp(line, "transfer-encoding", 17)) {
				for (size_t i = 0; i + 7 <= value_len; i++) {
					if (!strncasecmp(value + i, "chunked", 7)) out->chunked = 1;
				}
			} else if (name_len == 8 && !strncasecmp(line, "location", 8)) {
				if (value_len >= sizeof(out->location)) value_len = 0;
				memcpy(out->location, value, value_len);
				out->location[value_len] = '\0';
			}
		}
		p = eol;
	}
	if (out->chunked) out->length = -1;
	return 0;
}

enum { CH_SIZE, CH_EXT, CH_SIZE_LF, CH_DATA, CH_DATA_CR, CH_DATA_LF, CH_TRAILER_LINE, CH_TRAILER_IN, CH_TRAILER_LF, CH_DONE };

int RM_chunksFeed(RMChunks* c, char* buf, size_t len, size_t* out_len) {
	size_t w = 0, i = 0;
	*out_len = 0;
	while (i < len && c->state != CH_DONE) {
		char ch = buf[i];
		switch (c->state) {
		case CH_SIZE:
			if (isxdigit((unsigned char)ch)) {
				if (++c->digits > 15) return -1;
				c->left = c->left * 16 + (unsigned long long)(isdigit((unsigned char)ch) ? ch - '0' : (tolower((unsigned char)ch) - 'a' + 10));
				i++;
				break;
			}
			if (!c->digits) return -1;
			if (ch == ';' || ch == ' ' || ch == '\t') c->state = CH_EXT;
			else if (ch == '\r') c->state = CH_SIZE_LF;
			else if (ch == '\n') c->state = c->left ? CH_DATA : CH_TRAILER_LINE;
			else return -1;
			i++;
			break;
		case CH_EXT:
			if (ch == '\n') c->state = c->left ? CH_DATA : CH_TRAILER_LINE;
			i++;
			break;
		case CH_SIZE_LF:
			if (ch != '\n') return -1;
			c->state = c->left ? CH_DATA : CH_TRAILER_LINE;
			i++;
			break;
		case CH_DATA: {
			size_t n = len - i;
			if ((unsigned long long)n > c->left) n = (size_t)c->left;
			memmove(buf + w, buf + i, n);
			w += n;
			i += n;
			c->left -= n;
			if (!c->left) c->state = CH_DATA_CR;
			break;
		}
		case CH_DATA_CR:
			if (ch == '\r') c->state = CH_DATA_LF;
			else if (ch == '\n') {
				c->state = CH_SIZE;
				c->digits = 0;
			} else return -1;
			i++;
			break;
		case CH_DATA_LF:
			if (ch != '\n') return -1;
			c->state = CH_SIZE;
			c->digits = 0;
			i++;
			break;
		case CH_TRAILER_LINE:
			if (ch == '\r') c->state = CH_TRAILER_LF;
			else if (ch == '\n') c->state = CH_DONE;
			else c->state = CH_TRAILER_IN;
			i++;
			break;
		case CH_TRAILER_IN:
			if (ch == '\n') c->state = CH_TRAILER_LINE;
			i++;
			break;
		case CH_TRAILER_LF:
			if (ch != '\n') return -1;
			c->state = CH_DONE;
			i++;
			break;
		}
	}
	*out_len = w;
	return c->state == CH_DONE ? 1 : 0;
}

int RM_multipart(const char* boundary, const char* field, const char* file_name, char* before, size_t before_size,
		char* after, size_t after_size) {
	char name[RM_NAME_MAX];
	size_t n = 0;
	for (const char* p = file_name; *p && n + 1 < sizeof(name); p++) {
		unsigned char c = (unsigned char)*p;
		name[n++] = (c < ' ' || c == '"' || c == '\\' || c == 0x7f) ? '_' : (char)c;
	}
	name[n] = '\0';
	int a = snprintf(before, before_size,
		"--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n",
		boundary, field, name);
	int b = snprintf(after, after_size, "\r\n--%s--\r\n", boundary);
	if (a < 0 || (size_t)a >= before_size || b < 0 || (size_t)b >= after_size) return -1;
	return 0;
}

///////////////////////////////////////
// JSON

typedef struct RJParser {
	RJDoc* doc;
	char* p;
	char* end;
	int max;
	int depth;
} RJParser;

static void skipSpace(RJParser* s) {
	while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')) s->p++;
}

static int newNode(RJParser* s, int type) {
	RJDoc* d = s->doc;
	if (d->count >= s->max) return -1;
	if (d->count == d->cap) {
		int cap = d->cap ? d->cap * 2 : 256;
		if (cap > s->max) cap = s->max;
		RJNode* nodes = realloc(d->nodes, (size_t)cap * sizeof(RJNode));
		if (!nodes) return -1;
		d->nodes = nodes;
		d->cap = cap;
	}
	RJNode* n = &d->nodes[d->count];
	memset(n, 0, sizeof(*n));
	n->type = type;
	n->first = n->next = -1;
	return d->count++;
}

static int hexValue(const char* p) {
	int v = 0;
	for (int i = 0; i < 4; i++) {
		int c = (unsigned char)p[i];
		if (!isxdigit(c)) return -1;
		v = v * 16 + (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
	}
	return v;
}

static char* putUtf8(char* w, unsigned long cp) {
	if (cp < 0x80) *w++ = (char)cp;
	else if (cp < 0x800) {
		*w++ = (char)(0xC0 | (cp >> 6));
		*w++ = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		*w++ = (char)(0xE0 | (cp >> 12));
		*w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
		*w++ = (char)(0x80 | (cp & 0x3F));
	} else {
		*w++ = (char)(0xF0 | (cp >> 18));
		*w++ = (char)(0x80 | ((cp >> 12) & 0x3F));
		*w++ = (char)(0x80 | ((cp >> 6) & 0x3F));
		*w++ = (char)(0x80 | (cp & 0x3F));
	}
	return w;
}

// Decodes the string at s->p (its opening quote) in place. Returns its start.
static char* parseString(RJParser* s) {
	if (s->p >= s->end || *s->p != '"') return NULL;
	char* start = ++s->p;
	char* w = start;
	while (s->p < s->end) {
		unsigned char c = (unsigned char)*s->p;
		if (c == '"') {
			s->p++;
			*w = '\0';
			return start;
		}
		if (c < 0x20) return NULL;
		if (c != '\\') {
			*w++ = (char)c;
			s->p++;
			continue;
		}
		if (s->p + 1 >= s->end) return NULL;
		char e = s->p[1];
		s->p += 2;
		switch (e) {
		case '"': *w++ = '"'; break;
		case '\\': *w++ = '\\'; break;
		case '/': *w++ = '/'; break;
		case 'b': *w++ = '\b'; break;
		case 'f': *w++ = '\f'; break;
		case 'n': *w++ = '\n'; break;
		case 'r': *w++ = '\r'; break;
		case 't': *w++ = '\t'; break;
		case 'u': {
			if (s->end - s->p < 4) return NULL;
			int v = hexValue(s->p);
			if (v < 0) return NULL;
			s->p += 4;
			unsigned long cp = (unsigned long)v;
			if (v >= 0xD800 && v <= 0xDBFF) {
				int low = (s->end - s->p >= 6 && s->p[0] == '\\' && s->p[1] == 'u') ? hexValue(s->p + 2) : -1;
				if (low >= 0xDC00 && low <= 0xDFFF) {
					cp = 0x10000 + (((unsigned long)v - 0xD800) << 10) + ((unsigned long)low - 0xDC00);
					s->p += 6;
				} else {
					cp = 0xFFFD;
				}
			} else if (v >= 0xDC00 && v <= 0xDFFF) {
				cp = 0xFFFD;
			}
			if (cp == 0) cp = 0xFFFD; // no NUL inside a string
			w = putUtf8(w, cp);
			break;
		}
		default:
			return NULL;
		}
	}
	return NULL;
}

static int parseValue(RJParser* s);

static void addChild(RJParser* s, int parent, int* last, int child) {
	RJNode* nodes = s->doc->nodes;
	if (*last < 0) nodes[parent].first = child;
	else nodes[*last].next = child;
	*last = child;
	nodes[parent].count++;
}

static int parseValue(RJParser* s) {
	skipSpace(s);
	if (s->p >= s->end) return -1;
	char c = *s->p;
	if (c == '{' || c == '[') {
		if (++s->depth > 64) return -1;
		int object = c == '{';
		int self = newNode(s, object ? RJ_OBJECT : RJ_ARRAY);
		if (self < 0) return -1;
		s->p++;
		skipSpace(s);
		int last = -1;
		char close = object ? '}' : ']';
		if (s->p < s->end && *s->p == close) {
			s->p++;
			s->depth--;
			return self;
		}
		for (;;) {
			const char* key = NULL;
			if (object) {
				skipSpace(s);
				key = parseString(s);
				if (!key) return -1;
				skipSpace(s);
				if (s->p >= s->end || *s->p != ':') return -1;
				s->p++;
			}
			int child = parseValue(s);
			if (child < 0) return -1;
			s->doc->nodes[child].key = key;
			addChild(s, self, &last, child);
			skipSpace(s);
			if (s->p >= s->end) return -1;
			if (*s->p == ',') {
				s->p++;
				continue;
			}
			if (*s->p != close) return -1;
			s->p++;
			s->depth--;
			return self;
		}
	}
	if (c == '"') {
		int self = newNode(s, RJ_STRING);
		if (self < 0) return -1;
		char* text = parseString(s);
		if (!text) return -1;
		s->doc->nodes[self].text = text;
		return self;
	}
	static const struct {
		const char* word;
		int type;
	} words[] = { { "true", RJ_TRUE }, { "false", RJ_FALSE }, { "null", RJ_NULL } };
	for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
		size_t n = strlen(words[i].word);
		if ((size_t)(s->end - s->p) >= n && !strncmp(s->p, words[i].word, n)) {
			s->p += n;
			return newNode(s, words[i].type);
		}
	}
	if (c == '-' || isdigit((unsigned char)c)) {
		int self = newNode(s, RJ_NUMBER);
		if (self < 0) return -1;
		s->doc->nodes[self].text = s->p;
		if (*s->p == '-') s->p++;
		int digits = 0;
		while (s->p < s->end && (isdigit((unsigned char)*s->p) || *s->p == '.' || *s->p == 'e' || *s->p == 'E'
				|| ((*s->p == '+' || *s->p == '-') && (s->p[-1] == 'e' || s->p[-1] == 'E')))) {
			if (isdigit((unsigned char)*s->p)) digits++;
			s->p++;
		}
		return digits ? self : -1;
	}
	return -1;
}

int RJ_parse(RJDoc* doc, const char* text, size_t len, int max_nodes) {
	memset(doc, 0, sizeof(*doc));
	doc->text = malloc(len + 1);
	if (!doc->text) return -1;
	memcpy(doc->text, text, len);
	doc->text[len] = '\0';
	RJParser s = { doc, doc->text, doc->text + len, max_nodes, 0 };
	int root = parseValue(&s);
	skipSpace(&s);
	if (root != 0 || s.p != s.end) {
		RJ_free(doc);
		return -1;
	}
	return 0;
}

void RJ_free(RJDoc* doc) {
	free(doc->nodes);
	free(doc->text);
	memset(doc, 0, sizeof(*doc));
}

const RJNode* RJ_root(const RJDoc* doc) {
	return doc->count ? &doc->nodes[0] : NULL;
}

const RJNode* RJ_first(const RJDoc* doc, const RJNode* node) {
	return node && node->first >= 0 ? &doc->nodes[node->first] : NULL;
}

const RJNode* RJ_next(const RJDoc* doc, const RJNode* node) {
	return node && node->next >= 0 ? &doc->nodes[node->next] : NULL;
}

const RJNode* RJ_get(const RJDoc* doc, const RJNode* object, const char* key) {
	if (!object || object->type != RJ_OBJECT) return NULL;
	const RJNode* found = NULL;
	for (const RJNode* n = RJ_first(doc, object); n; n = RJ_next(doc, n)) {
		if (n->key && !strcmp(n->key, key)) found = n; // the last of a repeated key, like most readers
	}
	return found;
}

const RJNode* RJ_at(const RJDoc* doc, const RJNode* array, int index) {
	if (!array || array->type != RJ_ARRAY || index < 0) return NULL;
	const RJNode* n = RJ_first(doc, array);
	while (n && index-- > 0) n = RJ_next(doc, n);
	return n;
}

const char* RJ_string(const RJDoc* doc, const RJNode* object, const char* key, const char* fallback) {
	const RJNode* n = RJ_get(doc, object, key);
	return n && n->type == RJ_STRING ? n->text : fallback;
}

long long RJ_int(const RJDoc* doc, const RJNode* object, const char* key, long long fallback) {
	const RJNode* n = RJ_get(doc, object, key);
	if (!n || n->type != RJ_NUMBER) return fallback;
	char* end = NULL;
	long long v = strtoll(n->text, &end, 10);
	if (end && (*end == '.' || *end == 'e' || *end == 'E')) return (long long)strtod(n->text, NULL);
	return v;
}

int RJ_bool(const RJDoc* doc, const RJNode* object, const char* key, int fallback) {
	const RJNode* n = RJ_get(doc, object, key);
	if (!n) return fallback;
	if (n->type == RJ_TRUE) return 1;
	if (n->type == RJ_FALSE) return 0;
	return fallback;
}

///////////////////////////////////////
// RomM

// RomM's platform slugs and the stock system each one plays on. Only
// systems with a stock or extras emulator pak are here.
static const struct {
	const char* slug;
	const char* tag;
	const char* folder;
} systems[] = {
	{ "gb", "GB", "Game Boy (GB)" },
	{ "gbc", "GBC", "Game Boy Color (GBC)" },
	{ "gba", "GBA", "Game Boy Advance (GBA)" },
	{ "nes", "FC", "Nintendo Entertainment System (FC)" },
	{ "famicom", "FC", "Nintendo Entertainment System (FC)" },
	{ "fds", "FDS", "Famicom Disk System (FDS)" },
	{ "snes", "SFC", "Super Nintendo Entertainment System (SFC)" },
	{ "sfam", "SFC", "Super Nintendo Entertainment System (SFC)" },
	{ "genesis", "MD", "Sega Genesis (MD)" },
	{ "sms", "SMS", "Sega Master System (SMS)" },
	{ "gamegear", "GG", "Sega Game Gear (GG)" },
	{ "sg1000", "SG1000", "Sega SG-1000 (SG1000)" },
	{ "segacd", "SEGACD", "Sega CD (SEGACD)" },
	{ "sega32", "32X", "Sega 32X (32X)" },
	{ "psx", "PS", "Sony PlayStation (PS)" },
	{ "tg16", "PCE", "TurboGrafx-16 (PCE)" },
	{ "neo-geo-pocket", "NGP", "Neo Geo Pocket (NGP)" },
	{ "neo-geo-pocket-color", "NGPC", "Neo Geo Pocket Color (NGPC)" },
	{ "virtualboy", "VB", "Virtual Boy (VB)" },
	{ "pokemon-mini", "PKM", "Pok\xc3\xa9mon mini (PKM)" },
	{ "lynx", "LYNX", "Atari Lynx (LYNX)" },
	{ "atari2600", "A2600", "Atari 2600 (A2600)" },
	{ "atari5200", "A5200", "Atari 5200 (A5200)" },
	{ "atari7800", "A7800", "Atari 7800 (A7800)" },
	{ "colecovision", "COLECO", "Colecovision (COLECO)" },
	{ "c64", "C64", "Commodore 64 (C64)" },
	{ "c128", "C128", "Commodore 128 (C128)" },
	{ "vic-20", "VIC", "Commodore VIC20 (VIC)" },
	{ "cpet", "PET", "Commodore PET (PET)" },
	{ "c-plus-4", "PLUS4", "Commodore Plus4 (PLUS4)" },
	{ "acpc", "CPC", "Amstrad CPC (CPC)" },
	{ "amiga", "PUAE", "Amiga (PUAE)" },
	{ "msx", "MSX", "Microsoft MSX (MSX)" },
	{ "msx2", "MSX", "Microsoft MSX (MSX)" },
	{ "pico", "P8", "Pico-8 (P8)" },
	{ "arcade", "FBN", "Arcade (FBN)" },
	{ "doom", "PRBOOM", "Doom (PRBOOM)" },
};

int RM_tagFor(const char* slug, char* tag, size_t tag_size, char* folder, size_t folder_size) {
	for (size_t i = 0; slug && i < sizeof(systems) / sizeof(systems[0]); i++) {
		if (strcmp(slug, systems[i].slug) != 0) continue;
		if (tag) snprintf(tag, tag_size, "%s", systems[i].tag);
		if (folder) snprintf(folder, folder_size, "%s", systems[i].folder);
		return 0;
	}
	if (tag && tag_size) tag[0] = '\0';
	if (folder && folder_size) folder[0] = '\0';
	return -1;
}

int RM_tagCount(void) {
	return (int)(sizeof(systems) / sizeof(systems[0]));
}

const char* RM_tagSlug(int index) {
	return index >= 0 && index < RM_tagCount() ? systems[index].slug : NULL;
}

#define RM_NODES_MAX 2000000

int RM_parsePlatforms(const char* json, size_t len, RMPlatform* out, int max) {
	RJDoc doc;
	if (RJ_parse(&doc, json, len, RM_NODES_MAX) != 0) return -1;
	const RJNode* root = RJ_root(&doc);
	if (root->type != RJ_ARRAY) {
		RJ_free(&doc);
		return -1;
	}
	int n = 0;
	for (const RJNode* p = RJ_first(&doc, root); p && n < max; p = RJ_next(&doc, p)) {
		if (p->type != RJ_OBJECT) continue;
		RMPlatform* o = &out[n];
		memset(o, 0, sizeof(*o));
		o->id = (int)RJ_int(&doc, p, "id", 0);
		if (o->id <= 0) continue;
		snprintf(o->slug, sizeof(o->slug), "%s", RJ_string(&doc, p, "slug", ""));
		const char* name = RJ_string(&doc, p, "display_name", "");
		if (!name[0]) name = RJ_string(&doc, p, "name", o->slug);
		snprintf(o->name, sizeof(o->name), "%s", name);
		o->rom_count = (int)RJ_int(&doc, p, "rom_count", 0);
		RM_tagFor(o->slug, o->tag, sizeof(o->tag), NULL, 0);
		n++;
	}
	RJ_free(&doc);
	return n;
}

// A rom's files: how it's served and, for a zip, what to launch from it.
// Returns 0, or -1 to leave the rom out.
static int romFiles(const RJDoc* doc, const RJNode* r, RMRom* o) {
	const RJNode* files = RJ_get(doc, r, "files");
	int n = files && files->type == RJ_ARRAY ? files->count : 0;
	if (n == 0) {
		// no list of files to go by: only a plain rom can be fetched
		o->multi = RJ_bool(doc, r, "has_multiple_files", 0);
		return o->multi ? -1 : 0;
	}
	if (n == 1) {
		// one file, maybe in a folder of its own: the server sends just the file
		const char* name = RJ_string(doc, RJ_first(doc, files), "file_name", "");
		if (!RM_safeName(name)) return -1;
		snprintf(o->file, sizeof(o->file), "%s", name);
		return 0;
	}
	if (n > RM_ZIP_ENTRIES_MAX) return -1;
	const char** names = calloc((size_t)n, sizeof(char*));
	if (!names) return -1;
	// a file's path in the zip is its full_path inside the rom's own
	const char* top = RJ_string(doc, r, "full_path", "");
	size_t top_len = strlen(top);
	int k = 0, ok = 1;
	for (const RJNode* f = RJ_first(doc, files); f && ok; f = RJ_next(doc, f)) {
		const char* full = RJ_string(doc, f, "full_path", "");
		const char* name = top_len && !strncmp(full, top, top_len) && full[top_len] == '/' ? full + top_len + 1
			: RJ_string(doc, f, "file_name", "");
		char why[64];
		ok = RM_zipNameCheck(name, strlen(name), why, sizeof(why)) == 0;
		names[k++] = name;
	}
	o->multi = 1;
	ok = ok && RM_launchFile(o->file, names, k, o->launch, sizeof(o->launch)) > 0;
	free(names);
	return ok ? 0 : -1;
}

int RM_parseRoms(const char* json, size_t len, RMRom* out, int* count, int max, int* total) {
	RJDoc doc;
	if (RJ_parse(&doc, json, len, RM_NODES_MAX) != 0) return -1;
	const RJNode* root = RJ_root(&doc);
	const RJNode* items = RJ_get(&doc, root, "items");
	if (!items || items->type != RJ_ARRAY) {
		RJ_free(&doc);
		return -1;
	}
	if (total) *total = (int)RJ_int(&doc, root, "total", 0);
	int page = 0;
	for (const RJNode* r = RJ_first(&doc, items); r; r = RJ_next(&doc, r)) {
		page++;
		if (r->type != RJ_OBJECT || *count >= max) continue;
		RMRom* o = &out[*count];
		memset(o, 0, sizeof(*o));
		o->id = (int)RJ_int(&doc, r, "id", 0);
		const char* file = RJ_string(&doc, r, "fs_name", "");
		if (o->id <= 0 || !RM_safeName(file)) continue;
		snprintf(o->file, sizeof(o->file), "%s", file);
		o->platform_id = (int)RJ_int(&doc, r, "platform_id", 0);
		const char* name = RJ_string(&doc, r, "name", "");
		if (!name[0]) name = RJ_string(&doc, r, "fs_name_no_ext", file);
		snprintf(o->name, sizeof(o->name), "%s", name);
		o->size = RJ_int(&doc, r, "fs_size_bytes", 0);
		if (romFiles(&doc, r, o) != 0) continue;
		(*count)++;
	}
	RJ_free(&doc);
	return page;
}

int RM_parseAssets(const char* json, size_t len, int kind, RMAsset* out, int* count, int max) {
	RJDoc doc;
	if (RJ_parse(&doc, json, len, RM_NODES_MAX) != 0) return -1;
	const RJNode* root = RJ_root(&doc);
	if (root->type != RJ_ARRAY) {
		RJ_free(&doc);
		return -1;
	}
	int n = 0;
	for (const RJNode* a = RJ_first(&doc, root); a; a = RJ_next(&doc, a)) {
		n++;
		if (a->type != RJ_OBJECT || *count >= max) continue;
		RMAsset* o = &out[*count];
		memset(o, 0, sizeof(*o));
		o->kind = kind;
		o->id = (int)RJ_int(&doc, a, "id", 0);
		const char* file = RJ_string(&doc, a, "file_name", "");
		if (o->id <= 0 || !RM_safeName(file)) continue;
		snprintf(o->file, sizeof(o->file), "%s", file);
		o->rom_id = (int)RJ_int(&doc, a, "rom_id", 0);
		o->size = RJ_int(&doc, a, "file_size_bytes", 0);
		const char* when = RJ_string(&doc, a, "updated_at", "");
		if (!when[0]) when = RJ_string(&doc, a, "created_at", "");
		o->updated = RM_parseTime(when);
		snprintf(o->emulator, sizeof(o->emulator), "%s", RJ_string(&doc, a, "emulator", ""));
		snprintf(o->slot, sizeof(o->slot), "%s", RJ_string(&doc, a, "slot", ""));
		snprintf(o->hash, sizeof(o->hash), "%s", RJ_string(&doc, a, "content_hash", ""));
		(*count)++;
	}
	RJ_free(&doc);
	return n;
}

static int assetOrder(const void* pa, const void* pb) {
	const RMAsset* a = pa;
	const RMAsset* b = pb;
	if (a->updated != b->updated) return a->updated > b->updated ? -1 : 1;
	if (a->kind != b->kind) return a->kind - b->kind;
	return b->id - a->id;
}

void RM_sortAssets(RMAsset* assets, int count) {
	if (count > 1) qsort(assets, (size_t)count, sizeof(RMAsset), assetOrder);
}

// days since 1970-01-01 for a proleptic Gregorian date
static long long daysFromCivil(long long y, unsigned m, unsigned d) {
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long long)doe - 719468;
}

static int digitsAt(const char* s, int n, int* out) {
	int v = 0;
	for (int i = 0; i < n; i++) {
		if (!isdigit((unsigned char)s[i])) return -1;
		v = v * 10 + (s[i] - '0');
	}
	*out = v;
	return 0;
}

time_t RM_parseTime(const char* text) {
	int y, mo, d, h, mi, s;
	if (!text || strlen(text) < 19) return 0;
	if (digitsAt(text, 4, &y) || text[4] != '-' || digitsAt(text + 5, 2, &mo) || text[7] != '-'
			|| digitsAt(text + 8, 2, &d) || (text[10] != 'T' && text[10] != ' ') || digitsAt(text + 11, 2, &h)
			|| text[13] != ':' || digitsAt(text + 14, 2, &mi) || text[16] != ':' || digitsAt(text + 17, 2, &s)) {
		return 0;
	}
	if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return 0;
	const char* p = text + 19;
	if (*p == '.') {
		p++;
		while (isdigit((unsigned char)*p)) p++;
	}
	long long offset = 0;
	if (*p == '+' || *p == '-') {
		int oh, om;
		if (digitsAt(p + 1, 2, &oh) || p[3] != ':' || digitsAt(p + 4, 2, &om)) return 0;
		offset = (oh * 3600LL + om * 60LL) * (*p == '-' ? -1 : 1);
		p += 6;
	} else if (*p == 'Z') {
		p++;
	}
	if (*p) return 0;
	long long t = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400LL + h * 3600LL + mi * 60LL + s - offset;
	return t > 0 ? (time_t)t : 0;
}

void RM_formatTime(time_t when, char* out, size_t size) {
	struct tm tm;
	if (!when || !localtime_r(&when, &tm) || !strftime(out, size, "%Y-%m-%d %H:%M", &tm)) {
		snprintf(out, size, "%s", tr("unknown time"));
	}
}

void RM_formatSize(long long bytes, char* out, size_t size) {
	if (bytes < 1024) snprintf(out, size, "%lld %s", bytes < 0 ? 0 : bytes, tr(TR_MARK("B")));
	else if (bytes < 1024 * 1024) snprintf(out, size, "%.1f %s", bytes / 1024.0, tr(TR_MARK("KB")));
	else if (bytes < 1024LL * 1024 * 1024) snprintf(out, size, "%.1f %s", bytes / (1024.0 * 1024), tr(TR_MARK("MB")));
	else snprintf(out, size, "%.1f %s", bytes / (1024.0 * 1024 * 1024), tr(TR_MARK("GB")));
}

int RM_safeName(const char* name) {
	if (!name || !name[0] || name[0] == '.' || strlen(name) >= RM_NAME_MAX) return 0;
	for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
		if (*p < 0x20 || *p == 0x7f || *p == '/' || *p == '\\') return 0;
	}
	return 1;
}

// the length of name without its extension, as minarch takes it off: a
// last '.' with 1 to 4 characters after it
static size_t baseLength(const char* name) {
	const char* dot = strrchr(name, '.');
	size_t ext = dot ? strlen(dot) : 0;
	return dot && ext > 2 && ext <= 5 ? (size_t)(dot - name) : strlen(name);
}

int RM_saveNames(const char* rom_file, char names[3][RM_NAME_MAX]) {
	size_t base = baseLength(rom_file);
	snprintf(names[0], RM_NAME_MAX, "%s.sav", rom_file);
	snprintf(names[1], RM_NAME_MAX, "%.*s.srm", (int)base, rom_file);
	snprintf(names[2], RM_NAME_MAX, "%.*s.sav", (int)base, rom_file);
	return 3;
}

int RM_saveTarget(const char* rom_file, const char* server_file, int existing, char* out, size_t size) {
	char names[3][RM_NAME_MAX];
	RM_saveNames(rom_file, names);
	int pick = -1;
	for (int i = 0; i < 3; i++) {
		if (existing & (1 << i)) {
			pick = i;
			break;
		}
	}
	if (pick < 0) {
		size_t len = strlen(server_file);
		if (len >= 4 && !strcasecmp(server_file + len - 4, ".srm")) pick = 1;
		else if (!strcmp(server_file, names[2]) && strcmp(names[0], names[2]) != 0) pick = 2;
		else pick = 0;
	}
	snprintf(out, size, "%s", names[pick]);
	return pick;
}

// 1 when name ends in suffix, case ignored, with something before it
static int endsWith(const char* name, const char* suffix) {
	size_t n = strlen(name), s = strlen(suffix);
	return n > s && !strcasecmp(name + n - s, suffix);
}

int RM_stateSlot(const char* server_file) {
	size_t len = strlen(server_file);
	if (endsWith(server_file, ".state.auto")) return 9;
	if (endsWith(server_file, ".state")) return 0;
	if (len < 2 || !isdigit((unsigned char)server_file[len - 1])) return -1;
	int slot = server_file[len - 1] - '0';
	char rest[RM_NAME_MAX];
	snprintf(rest, sizeof(rest), "%.*s", (int)(len - 1), server_file);
	return endsWith(rest, ".st") || endsWith(rest, ".state") || endsWith(rest, ".state.") ? slot : -1;
}

// mirrors minarch's State_getPath for each State format
int RM_stateName(const char* rom_file, int format, int slot, char* out, size_t size) {
	if (slot < 0 || slot > 9 || format < 0 || format > 4) return -1;
	int base = (int)baseLength(rom_file), n;
	if (format == 0) n = snprintf(out, size, "%s.st%d", rom_file, slot);
	else if (slot == 9) n = snprintf(out, size, "%.*s.state.auto", base, rom_file);
	else if (format <= 2) n = snprintf(out, size, "%.*s.state.%d", base, rom_file, slot);
	else if (slot == 0) n = snprintf(out, size, "%.*s.state", base, rom_file);
	else n = snprintf(out, size, "%.*s.state%d", base, rom_file, slot);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int RM_stateFormat(const char* settings) {
	for (const char* line = settings; line && *line;) {
		int v;
		if (!strncmp(line, "stateFormat=", 12) && sscanf(line + 12, "%d", &v) == 1) return v >= 0 && v <= 4 ? v : 0;
		line = strchr(line, '\n');
		if (line) line++;
	}
	return 0;
}

int RM_emuCore(const char* launch_sh, char* out, size_t size) {
	for (const char* line = launch_sh; line && *line;) {
		while (*line == ' ' || *line == '\t') line++;
		if (!strncmp(line, "EMU_EXE=", 8)) {
			const char* v = line + 8;
			int quote = *v == '"' || *v == '\'' ? *v++ : 0;
			size_t n = 0;
			while (isalnum((unsigned char)v[n]) || v[n] == '_' || v[n] == '-' || v[n] == '.') n++;
			if (quote && v[n] != quote) return -1;
			if (n == 0 || n >= size || v[0] == '.') return -1;
			snprintf(out, size, "%.*s", (int)n, v);
			return 0;
		}
		line = strchr(line, '\n');
		if (line) line++;
	}
	return -1;
}

int RM_stateFolder(const char* tag, const char* const* folders, int count, const char* core, const char* emulator,
	char* out, size_t size) {
	char want[RM_NAME_MAX];
	if (core && core[0]) {
		snprintf(want, sizeof(want), "%s-%s", tag, core);
		if (!RM_safeName(want)) return -1;
		snprintf(out, size, "%s", want);
		return 0;
	}
	if (count == 1) {
		snprintf(out, size, "%s", folders[0]);
		return 0;
	}
	if (emulator && emulator[0]) {
		snprintf(want, sizeof(want), "%s-%s", tag, emulator);
		for (int i = 0; i < count; i++) {
			if (!strcmp(folders[i], want)) {
				snprintf(out, size, "%s", want);
				return 0;
			}
		}
	}
	return -1;
}

///////////////////////////////////////
// Zips

int RM_zipIsDir(const RMZipEntry* e) {
	return e->name_len > 0 && e->name[e->name_len - 1] == '/';
}

int RM_zipNameCheck(const char* name, size_t len, char* why, size_t size) {
#define REFUSE(text) \
	do { \
		if (why) snprintf(why, size, "%s", text); \
		return -1; \
	} while (0)
	if (len && name[len - 1] == '/') len--; // a folder's
	if (len == 0) REFUSE("an entry has no name");
	if (name[len - 1] == '/') REFUSE("an entry's path has an empty part");
	if (len > RM_ZIP_NAME_MAX) REFUSE("an entry's name is too long");
	if (memchr(name, '\0', len)) REFUSE("an entry's name holds a NUL");
	if (name[0] == '/' || (len >= 2 && isalpha((unsigned char)name[0]) && name[1] == ':')) {
		REFUSE("an entry's path is absolute");
	}
	int depth = 0;
	for (size_t start = 0; start < len;) {
		size_t end = start;
		while (end < len && name[end] != '/') end++;
		size_t part = end - start;
		const char* p = name + start;
		if (part == 0) REFUSE("an entry's path has an empty part");
		if ((part == 1 && p[0] == '.') || (part == 2 && p[0] == '.' && p[1] == '.')) REFUSE("an entry's path has . or ..");
		if (part >= RM_NAME_MAX) REFUSE("an entry's name is too long");
		if (part >= 6 && !strncmp(p, ".romm-", 6)) REFUSE("an entry has the plugin's own temporary name");
		for (size_t i = 0; i < part; i++) {
			unsigned char c = (unsigned char)p[i];
			if (c == '\\') REFUSE("an entry's path has a backslash");
			if (c < 0x20 || c == 0x7f) REFUSE("an entry's name has a control character");
			if (strchr(":*?\"<>|", c)) REFUSE("an entry's name has a character the card can't hold");
		}
		if (p[part - 1] == '.' || p[part - 1] == ' ') REFUSE("an entry's name ends in a dot or a space");
		if (++depth > RM_ZIP_DEPTH_MAX) REFUSE("an entry is too many folders deep");
		start = end + 1;
	}
	return 0;
}

int RM_zipEntryCheck(const RMZipEntry* e, char* why, size_t size) {
	if (RM_zipNameCheck(e->name, e->name_len, why, size) != 0) return -1;
	int dir = RM_zipIsDir(e);
	unsigned long type = (e->external >> 16) & 0170000;
	if (type == 0120000) REFUSE("an entry is a link");
	if (type != 0 && type != (dir ? 0040000UL : 0100000UL)) REFUSE("an entry isn't a plain file or folder");
	if (e->flags & (0x0001 | 0x0020 | 0x0040)) REFUSE("an entry is encrypted");
	if (e->method != 0 && e->method != 8) REFUSE("an entry is packed in a way it can't unpack");
	if (dir && (e->size || e->comp)) REFUSE("a folder entry has data");
	if (e->size > (unsigned long long)RM_ZIP_FILE_MAX) REFUSE("an entry is too big for the card");
	if (e->method == 0 && e->comp != e->size) REFUSE("an entry's sizes don't match");
	if (e->size > 0 && e->comp == 0) REFUSE("an entry's sizes look like a zip bomb");
	if (e->method == 8 && e->size / RM_ZIP_RATIO_MAX > e->comp) REFUSE("an entry's sizes look like a zip bomb");
	return 0;
}

// fold for the card's case-insensitive names
static int foldCompare(const char* a, size_t an, const char* b, size_t bn) {
	size_t n = an < bn ? an : bn;
	for (size_t i = 0; i < n; i++) {
		int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
		if (x != y) return x - y;
	}
	return an == bn ? 0 : an < bn ? -1 : 1;
}

typedef struct PathNode {
	const char* name;
	size_t len;
	int dir;
} PathNode;

static int nodeOrder(const void* pa, const void* pb) {
	const PathNode* a = pa;
	const PathNode* b = pb;
	return foldCompare(a->name, a->len, b->name, b->len);
}

int RM_zipCheck(const RMZipEntry* entries, int count, long long expect, long long* total, char* why, size_t size) {
	if (total) *total = 0;
	if (count < 0 || count > RM_ZIP_ENTRIES_MAX) REFUSE("the zip has too many entries");
	unsigned long long sum = 0;
	for (int i = 0; i < count; i++) {
		if (RM_zipEntryCheck(&entries[i], why, size) != 0) return -1;
		sum += entries[i].size;
		if (sum > (unsigned long long)RM_ZIP_TOTAL_MAX) REFUSE("the zip unpacks to more than the plugin takes");
	}
	if (expect >= 0 && sum > (unsigned long long)expect + RM_ZIP_SLACK) REFUSE("the zip holds more than the server said");
	// every path and the folders it's in, which must not collide once case is folded
	PathNode* nodes = malloc((size_t)(count ? count : 1) * (RM_ZIP_DEPTH_MAX + 2) * sizeof(PathNode));
	if (!nodes) REFUSE("out of memory");
	int n = 0;
	for (int i = 0; i < count; i++) {
		const RMZipEntry* e = &entries[i];
		size_t len = e->name_len;
		int dir = RM_zipIsDir(e);
		if (dir) len--;
		for (size_t k = 0; k < len; k++) {
			if (e->name[k] == '/') nodes[n++] = (PathNode){ e->name, k, 1 };
		}
		nodes[n++] = (PathNode){ e->name, len, dir };
	}
	qsort(nodes, (size_t)n, sizeof(PathNode), nodeOrder);
	for (int i = 1; i < n; i++) {
		const PathNode* a = &nodes[i - 1];
		const PathNode* b = &nodes[i];
		if (nodeOrder(a, b) != 0) continue;
		if (a->dir && b->dir && !memcmp(a->name, b->name, a->len)) continue; // the same folder again
		free(nodes);
		REFUSE("two of the zip's entries have the same name on the card");
	}
	free(nodes);
	if (total) *total = (long long)sum;
	return 0;
#undef REFUSE
}

// the extension of a path's last part, without the dot, or ""
static const char* extOf(const char* name) {
	const char* slash = strrchr(name, '/');
	const char* dot = strrchr(slash ? slash + 1 : name, '.');
	return dot ? dot + 1 : "";
}

static int extIn(const char* ext, const char* const* list) {
	for (; *list; list++) {
		if (!strcasecmp(ext, *list)) return 1;
	}
	return 0;
}

int RM_isGameM3u(const char* game, const char* name) {
	size_t g = strlen(game);
	return !strchr(name, '/') && strlen(name) == g + 4 && !strncasecmp(name, game, g) && !strcasecmp(name + g, ".m3u");
}

int RM_discNumber(const char* name) {
	static const char* const words[] = { "disc", "disk", "cd", "disque", NULL };
	const char* slash = strrchr(name, '/');
	for (const char* p = strchr(slash ? slash + 1 : name, '('); p; p = strchr(p + 1, '(')) {
		for (int w = 0; words[w]; w++) {
			size_t wl = strlen(words[w]);
			if (strncasecmp(p + 1, words[w], wl) != 0) continue;
			const char* q = p + 1 + wl;
			int spaces = 0;
			while (isspace((unsigned char)*q)) q++, spaces++;
			int number = 0;
			if (isdigit((unsigned char)*q)) {
				for (; isdigit((unsigned char)*q); q++) {
					if (number < 100000) number = number * 10 + (*q - '0');
				}
				if (isalpha((unsigned char)*q)) q++; // "(Disc 2A)"
			} else if (spaces && isalpha((unsigned char)*q) && !isalpha((unsigned char)q[1])) {
				number = tolower((unsigned char)*q++) - 'a' + 1; // "(Disc B)"
			} else {
				continue;
			}
			if (!strncasecmp(q, " of ", 4) && isdigit((unsigned char)q[4])) {
				q += 4;
				while (isdigit((unsigned char)*q)) q++;
			}
			if (*q == ')' && number > 0) return number;
		}
	}
	return 0;
}

static int discOrder(const char* a, const char* b) {
	int na = RM_discNumber(a), nb = RM_discNumber(b);
	if ((na == 0) != (nb == 0)) return na ? -1 : 1;
	if (na != nb) return na - nb;
	return strcmp(a, b);
}

int RM_discs(const char* const* names, int count, int* order) {
	static const char* const descriptors[] = { "cue", "gdi", "ccd", "mds", NULL };
	static const char* const tracks[] = { "bin", "raw", "img", "sub", "mdf", "wav", "ogg", "flac", "mp3", NULL };
	int sheets = 0;
	for (int i = 0; i < count; i++) sheets |= extIn(extOf(names[i]), descriptors);
	int n = 0;
	for (int i = 0; i < count; i++) {
		const char* ext = extOf(names[i]);
		if (!strcasecmp(ext, "m3u") || (sheets && extIn(ext, tracks))) continue;
		order[n++] = i;
	}
	// an insertion sort, as qsort has no way to pass names along without a global
	for (int i = 1; i < n; i++) {
		int v = order[i], k = i;
		while (k > 0 && discOrder(names[order[k - 1]], names[v]) > 0) {
			order[k] = order[k - 1];
			k--;
		}
		order[k] = v;
	}
	return n;
}

int RM_launchFile(const char* game, const char* const* names, int count, char* out, size_t size) {
	if (size) out[0] = '\0';
	int* order = malloc((size_t)(count ? count : 1) * sizeof(int));
	if (!order) return 0;
	int discs = RM_discs(names, count, order);
	if (discs >= 2) snprintf(out, size, "%s.m3u", game);
	else if (discs == 1) snprintf(out, size, "%s", names[order[0]]);
	free(order);
	return discs;
}

int RM_m3uText(const char* const* names, const int* order, int discs, char* out, size_t size) {
	size_t used = 0;
	if (size) out[0] = '\0';
	for (int i = 0; i < discs; i++) {
		int n = snprintf(out + used, size - used, "%s\n", names[order[i]]);
		if (n < 0 || (size_t)n >= size - used) return -1;
		used += (size_t)n;
	}
	return (int)used;
}

int RM_m3uFor(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path) return -1;
	const char* dir_start = slash - 1;
	while (dir_start > path && *dir_start != '/') dir_start--;
	if (*dir_start == '/') dir_start++;
	int n = snprintf(out, size, "%.*s/%.*s.m3u", (int)(slash - path), path, (int)(slash - dir_start), dir_start);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int RM_parsePin(const char* text, unsigned char out[32]) {
	int n = 0, half = -1;
	if (!text) return -1;
	for (const char* p = text; *p; p++) {
		if (*p == ':' || *p == ' ' || *p == '\t') continue;
		if (!isxdigit((unsigned char)*p)) return -1;
		int v = isdigit((unsigned char)*p) ? *p - '0' : tolower((unsigned char)*p) - 'a' + 10;
		if (half < 0) {
			half = v;
			continue;
		}
		if (n >= 32) return -1;
		out[n++] = (unsigned char)(half << 4 | v);
		half = -1;
	}
	return n == 32 && half < 0 ? 0 : -1;
}

void RM_hex(const unsigned char* data, size_t len, char* out, size_t size) {
	static const char hex[] = "0123456789abcdef";
	size_t o = 0;
	if (!size) return;
	for (size_t i = 0; i < len && o + 2 < size; i++) {
		out[o++] = hex[data[i] >> 4];
		out[o++] = hex[data[i] & 15];
	}
	out[o] = '\0';
}

int RM_statusText(const char* state, long long done, long long total, const char* label, char* out, size_t size) {
	char clean[33];
	size_t n = 0;
	for (const unsigned char* p = (const unsigned char*)(label ? label : ""); *p && n < 32; p++) {
		clean[n++] = (*p >= 0x20 && *p < 0x7f) ? (char)*p : '?';
	}
	clean[n] = '\0';
	if (done < 0) done = 0;
	if (total < 0) total = 0;
	if (done > 999999999) done = 999999999;
	if (total > 999999999) total = 999999999;
	int len = snprintf(out, size, "state=%s\ndone=%lld\ntotal=%lld\nlabel=%s\n", state, done, total, clean);
	return len < 0 || (size_t)len >= size ? -1 : len;
}

///////////////////////////////////////
// Records

int RM_recordParse(const char* line, RMRecord* out) {
	memset(out, 0, sizeof(*out));
	const char* t1 = strchr(line, '\t');
	const char* t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
	const char* t3 = t2 ? strchr(t2 + 1, '\t') : NULL;
	if (!t3) return -1;
	char* end = NULL;
	long id = strtol(line, &end, 10);
	if (end != t1 || id <= 0 || id > 0x7fffffff) return -1;
	out->rom_id = (int)id;
	size_t tag = (size_t)(t2 - t1 - 1), value = (size_t)(t3 - t2 - 1);
	if (!tag || tag >= sizeof(out->tag) || !value || value >= sizeof(out->value)) return -1;
	memcpy(out->tag, t1 + 1, tag);
	memcpy(out->value, t2 + 1, value);
	size_t path = strcspn(t3 + 1, "\r\n");
	if (!path || path >= sizeof(out->path)) return -1;
	memcpy(out->path, t3 + 1, path);
	return 0;
}

int RM_recordFormat(const RMRecord* r, char* out, size_t size) {
	if (strpbrk(r->path, "\t\r\n") || strpbrk(r->tag, "\t\r\n") || strpbrk(r->value, "\t\r\n")) return -1;
	int n = snprintf(out, size, "%d\t%s\t%s\t%s\n", r->rom_id, r->tag, r->value[0] ? r->value : "-", r->path);
	return n < 0 || (size_t)n >= size ? -1 : n;
}
