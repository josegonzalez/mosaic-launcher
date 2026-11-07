#include "netplay_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int NP_port(const char* text, int fallback) {
	if (!text) return fallback;
	while (*text == ' ') text++;
	if (!isdigit((unsigned char)*text)) return fallback;
	char* end;
	long port = strtol(text, &end, 10);
	while (*end == ' ') end++;
	if (*end || port < 1 || port > 65535) return fallback;
	return (int)port;
}

static int hostChar(char c) {
	return isalnum((unsigned char)c) || c == '.' || c == '-';
}

int NP_parseAddress(const char* text, int port_fallback, char* host, size_t size, int* port) {
	if (!text || !host || !size || !port) return -1;
	while (*text == ' ') text++;
	size_t len = strlen(text);
	while (len && text[len - 1] == ' ') len--;

	const char* colon = memchr(text, ':', len);
	size_t name_len = colon ? (size_t)(colon - text) : len;
	if (!name_len || name_len >= NP_HOST_LEN || name_len >= size) return -1;
	for (size_t i = 0; i < name_len; i++) {
		if (!hostChar(text[i])) return -1;
	}
	if (text[0] == '-' || text[0] == '.' || text[name_len - 1] == '-') return -1;

	int at = port_fallback;
	if (colon) {
		char digits[8];
		size_t n = len - name_len - 1;
		if (!n || n >= sizeof(digits)) return -1;
		memcpy(digits, colon + 1, n);
		digits[n] = '\0';
		for (size_t i = 0; i < n; i++) {
			if (!isdigit((unsigned char)digits[i])) return -1;
		}
		at = NP_port(digits, -1);
		if (at < 0) return -1;
	}

	memcpy(host, text, name_len);
	host[name_len] = '\0';
	*port = at;
	return 0;
}

int NP_setPending(NPPending* pending, int mode, const char* path, const char* host, int port) {
	if (mode != NP_HOST && mode != NP_JOIN) return -1;
	if (!path || !path[0] || strlen(path) >= sizeof(pending->path)) return -1;
	if (mode == NP_JOIN && (!host || !host[0] || strlen(host) >= sizeof(pending->host))) return -1;
	if (port < 1 || port > 65535) return -1;
	memset(pending, 0, sizeof(*pending));
	pending->mode = mode;
	snprintf(pending->path, sizeof(pending->path), "%s", path);
	if (mode == NP_JOIN) snprintf(pending->host, sizeof(pending->host), "%s", host);
	pending->port = port;
	return 0;
}

int NP_take(NPPending* pending, const char* path, int is_rom, NPPending* out) {
	int mode = pending->mode;
	if (!is_rom || !path || strcmp(path, pending->path) != 0) mode = NP_NONE;
	if (mode != NP_NONE) *out = *pending;
	memset(pending, 0, sizeof(*pending));
	return mode;
}

int NP_args(const NPPending* pending, char args[][NP_ARG_LEN], int max) {
	const char* parts[NP_ARGS_MAX];
	char port[8];
	int n = 0;
	snprintf(port, sizeof(port), "%d", pending->port);
	if (pending->mode == NP_HOST) {
		parts[n++] = "--host";
	}
	else if (pending->mode == NP_JOIN) {
		parts[n++] = "--connect";
		parts[n++] = pending->host;
	}
	else return -1;
	parts[n++] = "--port";
	parts[n++] = port;
	if (n > max) return -1;
	for (int i = 0; i < n; i++) {
		if (strlen(parts[i]) >= NP_ARG_LEN) return -1;
		snprintf(args[i], NP_ARG_LEN, "%s", parts[i]);
	}
	return n;
}

int NP_emuTag(const char* roms, const char* path, char* out, size_t size) {
	size_t roms_len = strlen(roms);
	if (strncmp(path, roms, roms_len) != 0 || path[roms_len] != '/') return -1;
	const char* folder = path + roms_len + 1;
	const char* slash = strchr(folder, '/');
	if (!slash || slash == folder || !slash[1]) return -1; // a game inside a system folder

	const char* start = folder;
	const char* end = slash;
	for (const char* c = folder; c < slash; c++) {
		if (*c == '(') start = c + 1;
	}
	if (start != folder) {
		const char* close = memchr(start, ')', (size_t)(slash - start));
		if (close) end = close;
	}
	size_t len = (size_t)(end - start);
	if (!len || len >= size) return -1;
	memcpy(out, start, len);
	out[len] = '\0';
	return 0;
}
