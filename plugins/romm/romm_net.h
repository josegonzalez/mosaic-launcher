// The romm plugin's HTTP/1.1 client: plain sockets for http://, BearSSL
// for https://, with bounded timeouts, sizes and redirects. Each call
// blocks, so the plugin runs it on a thread of its own; cancel stops it
// within a fraction of a second, apart from a name lookup in progress.

#ifndef ROMM_NET_H
#define ROMM_NET_H

#include <stddef.h>
#include "romm_model.h"

#define RN_AUTH_MAX 1600

typedef struct RNConfig {
	RMUrl url;
	char auth[RN_AUTH_MAX];   // from RM_authHeader
	int have_pin;
	unsigned char pin[32];    // the server certificate's SHA-256, when have_pin
	char ca_file[RM_PATH_MAX]; // the CA certificates to trust (PEM); "" refuses https:// without a pin
	int connect_ms;           // per address tried
	int io_ms;                // with nothing sent or received
} RNConfig;

typedef struct RNCall {
	const char* method;
	char target[RM_PATH_MAX]; // the path and query, after the server's base

	// The body to send, if any: body_len bytes of body, or else body_file
	// between body_before and body_after (a multipart upload).
	const char* content_type;
	const char* body;
	size_t body_len;
	const char* body_file;
	const char* body_before;
	const char* body_after;

	// Where the response's body goes: out_path (created, never followed
	// through a link) or else memory, up to max_body bytes either way.
	const char* out_path;
	long long max_body;

	volatile int* cancel;      // set to 1 to stop
	volatile long long* done;  // bytes of the body so far, or NULL
	volatile long long* total; // its length when the server says, or NULL

	// What came back
	int status;
	char* data; // the body in memory, NUL-terminated, freed with RN_free
	size_t len;
	char error[256];
} RNCall;

// Makes the request, following up to RM_REDIRECTS_MAX redirects on the same
// server. Returns 0 once a response is in (call->status says which), or -1
// with call->error set, eg. for a timeout, a refused certificate or cancel.
// A partly written out_path is left for the caller to remove.
int RN_run(const RNConfig* config, RNCall* call);
void RN_free(RNCall* call);

// The device's CA bundle: the first that exists of the places one is kept,
// for ca_file. Returns 0 with its path, or -1.
int RN_systemCa(const char* sdcard, const char* platform, char* out, size_t size);
// How many certificates a PEM file has that RN_run can trust, or -1.
int RN_countAnchors(const char* path);
// Hashes a file with MD5 (RomM's content_hash) into 32 hex digits, up to
// max bytes. Returns 0, or -1.
int RN_md5File(const char* path, long long max, char out[33]);

#endif
