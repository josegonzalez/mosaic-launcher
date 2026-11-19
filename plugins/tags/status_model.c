#include "status_model.h"
#include <string.h>

static const char* keys[ST_FLAGS] = { "completed", "beaten", "broken", "backlog" };
// English, translated where it's shown
#define TR_MARK(text) (text)
static const char* labels[ST_FLAGS] = { TR_MARK("Completed"), TR_MARK("Beaten"), TR_MARK("Broken"), TR_MARK("Backlog") };

const char* ST_key(int flag) {
	return flag >= 0 && flag < ST_FLAGS ? keys[flag] : NULL;
}

const char* ST_label(int flag) {
	return flag >= 0 && flag < ST_FLAGS ? labels[flag] : NULL;
}

int ST_fromKey(const char* key) {
	if (!key) return -1;
	for (int i = 0; i < ST_FLAGS; i++) {
		if (!strcmp(keys[i], key)) return i;
	}
	return -1;
}

int ST_fromSet(const char* set) {
	if (!set || strncmp(set, "tags.status.", 12) != 0) return -1;
	return ST_fromKey(set + 12);
}

// The store's line for the flag, matched ignoring case, or -1.
static int tagOf(const TGStore* store, int flag) {
	const char* key = ST_key(flag);
	return key ? TG_find(store, key) : -1;
}

int ST_has(const TGStore* store, int flag, const char* rel) {
	int tag = tagOf(store, flag);
	return tag >= 0 && TG_has(store, tag, rel) == 1;
}

unsigned int ST_flags(const TGStore* store, const char* rel) {
	unsigned int flags = 0;
	for (int i = 0; i < ST_FLAGS; i++) {
		if (ST_has(store, i, rel)) flags |= 1u << i;
	}
	return flags;
}

int ST_set(TGStore* store, int flag, const char* rel, int on) {
	if (!ST_key(flag) || !TG_validPath(rel)) return -1;
	int tag = tagOf(store, flag);
	if (tag < 0) {
		if (!on) return 0;
		tag = TG_add(store, ST_key(flag), NULL);
		if (tag < 0) return -1;
	}
	int changed = TG_set(store, tag, rel, on);
	// a flag no game has leaves no line behind
	if (changed == 1 && !on && !store->tags[tag].count) TG_remove(store, tag);
	return changed;
}

int ST_count(const TGStore* store, int flag) {
	int tag = tagOf(store, flag);
	return tag < 0 ? 0 : store->tags[tag].count;
}
