#ifndef ENTRYSETS_H
#define ENTRYSETS_H

// The SDL-free half of plugins' entry sets (host->set_register and the
// rest): named sets of entries, such as favorites or hidden, that one
// plugin owns and others read. The registry holds only each set's name,
// its owner and the owner's callbacks; the owner answers for what's in it
// every time it's asked, so nothing is copied here. The first plugin to
// register a name owns it until it unregisters it or is unloaded.

#define SET_NAME_MAX 128  // a set's name's bytes, its terminator included
#define SET_OWNER_MAX 64  // a plugin id's bytes, as LoadedPlugin's
#define SETS_MAX 1024     // the most sets registered at once

// The owner's callbacks, as MosaicHost's set_register takes them.
typedef int (*SetHasFn)(const char* name, const char* path);
typedef void (*SetListFn)(const char* name, void (*add)(const char* path, void* ctx), void* ctx);

typedef struct EntrySet {
	char name[SET_NAME_MAX];
	char owner[SET_OWNER_MAX]; // the plugin that registered it
	SetHasFn has;
	SetListFn list;
	int busy; // one of its callbacks is running, so it isn't asked again until it returns
} EntrySet;

typedef struct EntrySets {
	EntrySet* items; // in the order they were registered
	int count;
	int capacity;
} EntrySets;

// What EntrySets_register returns.
enum EntrySetResult {
	SET_OK = 0,
	SET_BAD = -1,      // no name, owner or has and list, or a name too long
	SET_RESERVED = -2, // a name this owner can't register
	SET_TAKEN = -3,    // another plugin registered it first
	SET_FULL = -4,     // SETS_MAX sets, or memory ran out
};

void EntrySets_init(EntrySets* sets);
void EntrySets_free(EntrySets* sets);

// 1 when owner may register name: favorites, hidden, tags:<tag>,
// smart:<collection>, or <owner>.<anything>, with something after each
// prefix, shorter than SET_NAME_MAX and with no control characters.
int EntrySets_nameAllowed(const char* name, const char* owner);

// Registers name for owner with its callbacks, both needed. The owner can
// register its own set again, which replaces the callbacks. Returns an
// enum EntrySetResult.
int EntrySets_register(EntrySets* sets, const char* name, const char* owner, SetHasFn has, SetListFn list);

// Removes owner's set name. Returns 0, or -1 when owner doesn't have it.
int EntrySets_unregister(EntrySets* sets, const char* name, const char* owner);

// The set named name, matched exactly, or NULL. It's only valid until the
// next register or unregister.
EntrySet* EntrySets_find(EntrySets* sets, const char* name);

// Removes every set owner registered, as it's unloaded. Returns how many.
int EntrySets_releaseAll(EntrySets* sets, const char* owner);

#endif
