#include <string.h>
#include "events.h"
#include "providers.h"

void Providers_init(Providers* providers) {
	providers->count = 0;
	providers->depth = 0;
}

static int kindChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

int Providers_nameValid(const char* name) {
	if (!name) return 0;
	size_t len = strlen(name);
	if (!len || len >= PROVIDER_NAME_MAX) return 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];
		if (c <= 0x20 || c == 0x7f) return 0;
	}
	size_t kind = 0;
	while (kindChar(name[kind])) kind++;
	return kind > 0 && name[kind] == '.' && name[kind + 1];
}

int Providers_nameReserved(const char* name) {
	if (!name) return 0;
	return !strncmp(name, "sets.", 5) || !strncmp(name, "events.", 7);
}

// 1 when the comma separated list has name, spaces around each item
// skipped, as plugin.txt's lists are read.
static int listed(const char* list, const char* name) {
	if (!list) return 0;
	size_t want = strlen(name);
	const char* p = list;
	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == ',') p++;
		const char* start = p;
		while (*p && *p != ',') p++;
		const char* end = p;
		while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
		if ((size_t)(end - start) == want && !strncmp(start, name, want)) return 1;
	}
	return 0;
}

Provider* Providers_find(Providers* providers, const char* name) {
	if (!name) return NULL;
	for (int i = 0; i < providers->count; i++) {
		if (!strcmp(providers->items[i].name, name)) return &providers->items[i];
	}
	return NULL;
}

int Providers_add(Providers* providers, const char* name, const char* owner, const char* declared, ProviderFn fn) {
	if (!owner || !owner[0] || strlen(owner) >= PROVIDER_OWNER_MAX || !fn || !Providers_nameValid(name)) return PROVIDE_BAD;
	if (Providers_nameReserved(name)) return PROVIDE_RESERVED;
	if (!listed(declared, name)) return PROVIDE_UNDECLARED;
	Provider* provider = Providers_find(providers, name);
	if (provider) {
		if (strcmp(provider->owner, owner) != 0) return PROVIDE_TAKEN;
		provider->fn = fn;
		return PROVIDE_OK;
	}
	if (providers->count >= PROVIDERS_MAX) return PROVIDE_FULL;
	provider = &providers->items[providers->count++];
	memset(provider, 0, sizeof(*provider));
	strcpy(provider->name, name);
	strcpy(provider->owner, owner);
	provider->fn = fn;
	return PROVIDE_OK;
}

int Providers_releaseAll(Providers* providers, const char* owner) {
	if (!owner) return 0;
	int released = 0;
	for (int i = providers->count - 1; i >= 0; i--) {
		if (strcmp(providers->items[i].owner, owner) != 0) continue;
		memmove(&providers->items[i], &providers->items[i + 1], (size_t)(providers->count - i - 1) * sizeof(Provider));
		providers->count--;
		released++;
	}
	return released;
}

int Providers_call(Providers* providers, const char* name, const char* args, char* out, size_t size, ProviderRun run,
		void* ctx) {
	if (out && size) out[0] = '\0';
	if (!out || !size || !run || !Events_textValid(args, CALL_TEXT_MAX)) return CALL_REFUSED;
	Provider* provider = Providers_find(providers, name);
	if (!provider) return CALL_UNKNOWN;
	if (provider->busy || providers->depth >= CALL_DEPTH_MAX) return CALL_REFUSED;
	// a copy, and the name to find it again by, as the callback can
	// provide more names, which can't move this one, but releasing can
	Provider running = *provider;
	char reply[CALL_TEXT_MAX];
	memset(reply, 0, sizeof(reply));
	provider->busy = 1;
	providers->depth++;
	int rc = run(&running, args ? args : "", reply, sizeof(reply), ctx);
	providers->depth--;
	provider = Providers_find(providers, running.name);
	if (provider) provider->busy = 0;
	reply[sizeof(reply) - 1] = '\0';
	if (rc != 0 || !Events_textValid(reply, CALL_TEXT_MAX)) return CALL_FAILED;
	size_t len = strlen(reply);
	if (len >= size) return CALL_TOO_SMALL;
	memcpy(out, reply, len + 1);
	return (int)len;
}
