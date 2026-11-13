#ifndef FAKEPROVIDERS_H
#define FAKEPROVIDERS_H

// A fake host's providers, for testing a plugin that provides services:
// the names it provided with their callbacks, a call that runs one into
// a MOSAIC_CALL_MAX buffer as the launcher does, and event_value to read
// the reply's keys with.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define FAKE_PROVIDERS_MAX 16

typedef int (*FakeProviderFn)(const char* name, const char* args, char* out, size_t size);
static char fake_provider_names[FAKE_PROVIDERS_MAX][64];
static FakeProviderFn fake_provider_fns[FAKE_PROVIDERS_MAX];
static int fake_provider_count = 0;

static int fakeProvide(const char* name, FakeProviderFn fn) {
	for (int i = 0; i < fake_provider_count; i++) {
		if (strcmp(fake_provider_names[i], name) != 0) continue;
		fake_provider_fns[i] = fn;
		return 0;
	}
	if (!fn || fake_provider_count >= FAKE_PROVIDERS_MAX) return -1;
	snprintf(fake_provider_names[fake_provider_count], sizeof(fake_provider_names[0]), "%s", name);
	fake_provider_fns[fake_provider_count++] = fn;
	return 0;
}

static int fakeCall(const char* name, const char* args, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	for (int i = 0; i < fake_provider_count; i++) {
		if (strcmp(fake_provider_names[i], name) != 0) continue;
		static char reply[MOSAIC_CALL_MAX];
		memset(reply, 0, sizeof(reply));
		if (fake_provider_fns[i](name, args ? args : "", reply, sizeof(reply)) != 0) return MOSAIC_CALL_FAILED;
		size_t len = strlen(reply);
		if (len >= size) return MOSAIC_CALL_TOO_SMALL;
		memcpy(out, reply, len + 1);
		return (int)len;
	}
	return MOSAIC_CALL_UNKNOWN;
}

// As the launcher's: the first line with key, cut to fit, or -1.
static int fakeEventValue(const char* payload, const char* key, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	if (!payload || !key || !key[0]) return -1;
	size_t want = strlen(key);
	const char* line = payload;
	while (*line) {
		const char* end = strchr(line, '\n');
		if (!end) end = line + strlen(line);
		if ((size_t)(end - line) > want && !strncmp(line, key, want) && line[want] == '=') {
			const char* value = line + want + 1;
			size_t len = (size_t)(end - value);
			if (out && size) {
				size_t copy = len < size - 1 ? len : size - 1;
				memcpy(out, value, copy);
				out[copy] = '\0';
			}
			return (int)len;
		}
		if (!*end) break;
		line = end + 1;
	}
	return -1;
}

#endif
