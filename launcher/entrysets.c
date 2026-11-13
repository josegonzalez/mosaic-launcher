#include <stdlib.h>
#include <string.h>
#include "entrysets.h"

void EntrySets_init(EntrySets* sets) {
	sets->items = NULL;
	sets->count = 0;
	sets->capacity = 0;
}

void EntrySets_free(EntrySets* sets) {
	free(sets->items);
	EntrySets_init(sets);
}

// 1 when name starts with prefix and has more after it.
static int prefixed(const char* name, const char* prefix, size_t len) {
	return !strncmp(name, prefix, len) && name[len];
}

int EntrySets_nameAllowed(const char* name, const char* owner) {
	if (!name || !owner || !owner[0]) return 0;
	size_t len = strlen(name);
	if (!len || len >= SET_NAME_MAX) return 0;
	for (size_t i = 0; i < len; i++) {
		if ((unsigned char)name[i] < 0x20 || name[i] == 0x7f) return 0;
	}
	if (!strcmp(name, "favorites") || !strcmp(name, "hidden")) return 1;
	if (prefixed(name, "tags:", 5) || prefixed(name, "smart:", 6)) return 1;
	size_t own = strlen(owner);
	return !strncmp(name, owner, own) && name[own] == '.' && name[own + 1];
}

EntrySet* EntrySets_find(EntrySets* sets, const char* name) {
	if (!name) return NULL;
	for (int i = 0; i < sets->count; i++) {
		if (!strcmp(sets->items[i].name, name)) return &sets->items[i];
	}
	return NULL;
}

int EntrySets_register(EntrySets* sets, const char* name, const char* owner, SetHasFn has, SetListFn list) {
	if (!name || !owner || !owner[0] || strlen(owner) >= SET_OWNER_MAX || !has || !list) return SET_BAD;
	if (!name[0] || strlen(name) >= SET_NAME_MAX) return SET_BAD;
	if (!EntrySets_nameAllowed(name, owner)) return SET_RESERVED;
	EntrySet* set = EntrySets_find(sets, name);
	if (set) {
		if (strcmp(set->owner, owner) != 0) return SET_TAKEN;
		set->has = has;
		set->list = list;
		return SET_OK;
	}
	if (sets->count >= SETS_MAX) return SET_FULL;
	if (sets->count == sets->capacity) {
		int capacity = sets->capacity ? sets->capacity * 2 : 16;
		EntrySet* items = realloc(sets->items, (size_t)capacity * sizeof(EntrySet));
		if (!items) return SET_FULL;
		sets->items = items;
		sets->capacity = capacity;
	}
	set = &sets->items[sets->count++];
	memset(set, 0, sizeof(*set));
	strcpy(set->name, name);
	strcpy(set->owner, owner);
	set->has = has;
	set->list = list;
	return SET_OK;
}

static void removeAt(EntrySets* sets, int i) {
	memmove(&sets->items[i], &sets->items[i + 1], (size_t)(sets->count - i - 1) * sizeof(EntrySet));
	sets->count--;
}

int EntrySets_unregister(EntrySets* sets, const char* name, const char* owner) {
	if (!owner) return -1;
	EntrySet* set = EntrySets_find(sets, name);
	if (!set || strcmp(set->owner, owner) != 0) return -1;
	removeAt(sets, (int)(set - sets->items));
	return 0;
}

int EntrySets_releaseAll(EntrySets* sets, const char* owner) {
	if (!owner) return 0;
	int released = 0;
	for (int i = sets->count - 1; i >= 0; i--) {
		if (strcmp(sets->items[i].owner, owner) != 0) continue;
		removeAt(sets, i);
		released++;
	}
	return released;
}
