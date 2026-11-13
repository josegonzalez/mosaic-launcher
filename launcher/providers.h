#ifndef PROVIDERS_H
#define PROVIDERS_H

// The SDL-free half of plugins' providers (host->provide and call): named
// services, such as captions.text, that one plugin answers for and others
// call with key=value text, getting key=value text back. The registry
// holds each name, its owner and the owner's callback; a call runs the
// callback through the runner plugins.c passes, which runs it as the
// owner. The first plugin to provide a name keeps it until it's unloaded.

#include <stddef.h>

#define PROVIDER_NAME_MAX 64  // a service's name's bytes, its terminator included
#define PROVIDER_OWNER_MAX 64 // a plugin id's bytes, as LoadedPlugin's
#define PROVIDERS_MAX 256     // the most services provided at once
#define CALL_TEXT_MAX 4096    // a call's arguments' or reply's bytes, the terminator included
#define CALL_DEPTH_MAX 8      // calls running inside each other, at most

// The owner's callback, as MosaicHost's provide takes it.
typedef int (*ProviderFn)(const char* name, const char* args, char* out, size_t size);

typedef struct Provider {
	char name[PROVIDER_NAME_MAX];
	char owner[PROVIDER_OWNER_MAX]; // the plugin that provided it
	ProviderFn fn;
	int busy; // its callback is running, so it isn't called again until it returns
} Provider;

typedef struct Providers {
	Provider items[PROVIDERS_MAX]; // in the order they were provided
	int count;
	int depth; // calls running now
} Providers;

// What Providers_add returns.
enum ProvideResult {
	PROVIDE_OK = 0,
	PROVIDE_BAD = -1,        // no owner or callback, or a name that isn't <kind>.<name>
	PROVIDE_RESERVED = -2,   // a sets. or events. name, which are other features' capabilities
	PROVIDE_UNDECLARED = -3, // a name the owner's provides= doesn't list
	PROVIDE_TAKEN = -4,      // another plugin provided it first
	PROVIDE_FULL = -5,       // PROVIDERS_MAX services
};

// What Providers_call returns, below 0; at or above 0 it's the reply's
// length. The same as MOSAIC_CALL_*.
enum CallResult {
	CALL_UNKNOWN = -1,   // nobody provides the name
	CALL_TOO_SMALL = -2, // the reply doesn't fit out
	CALL_REFUSED = -3,   // bad arguments, the name's callback already running, or calls nested too deep
	CALL_FAILED = -4,    // the provider returned non-zero, or a reply that isn't key=value text
};

void Providers_init(Providers* providers);

// 1 for a name of the form <kind>.<name>: a kind of letters, digits, '_'
// or '-', a dot, then at least one more byte, shorter than
// PROVIDER_NAME_MAX, with no spaces or control characters.
int Providers_nameValid(const char* name);

// 1 for a name whose kind is sets or events: those are entry sets' and
// events' capabilities, which no service has.
int Providers_nameReserved(const char* name);

// Provides name for owner, whose plugin.txt's provides= is declared (a
// comma separated list, matched exactly), with fn. The owner can provide
// its own name again, which replaces fn. Returns an enum ProvideResult.
int Providers_add(Providers* providers, const char* name, const char* owner, const char* declared, ProviderFn fn);

// The service named name, matched exactly, or NULL. Only valid until the
// next add or release.
Provider* Providers_find(Providers* providers, const char* name);

// Removes every service owner provides, as it's unloaded. Returns how many.
int Providers_releaseAll(Providers* providers, const char* owner);

// Runs the owner's callback for one call: run(provider, args, reply,
// reply_size, ctx) returns what the callback did, run as the owner.
typedef int (*ProviderRun)(const Provider* provider, const char* args, char* reply, size_t reply_size, void* ctx);

// Calls name with args (NULL for none, else key=value lines as events'
// payloads, under CALL_TEXT_MAX bytes) and copies its reply into out,
// writing "" whenever it doesn't return a length. The callback gets an
// empty reply buffer CALL_TEXT_MAX bytes long, and isn't run again for
// name until it returns. Returns the reply's length, or an enum
// CallResult.
int Providers_call(Providers* providers, const char* name, const char* args, char* out, size_t size, ProviderRun run,
	void* ctx);

#endif
