#ifndef FAKEINDEX_H
#define FAKEINDEX_H

// A fake host's card index, for testing a plugin that reads it: the
// launcher's own indexmodel.c over the test's card, built at once the
// first time the plugin asks (or not ready, while fake_index_ready is 0),
// and built again by fakeIndexRebuild, which then calls the plugin's
// launcher.index_ready subscriber as the launcher would. Link
// $(LAUNCHER)/indexmodel.c and $(LAUNCHER)/infomodel.c with -I$(LAUNCHER).

#include <string.h>
#include "indexmodel.h"
#include "mosaic_plugin.h"

#ifndef FAKE_INDEX_CARD
#define FAKE_INDEX_CARD TEST_SDCARD_PATH
#endif
#ifndef FAKE_INDEX_PLATFORM
#define FAKE_INDEX_PLATFORM "test"
#endif

static CardIndex fake_index;
static int fake_index_built = 0;
static int fake_index_ready = 1; // 0: index_count says it isn't ready yet
static int fake_index_asks = 0;
static int fake_index_refreshes = 0;
static void (*fake_index_on_ready)(const char* name, const char* payload, void* data) = NULL;
static void* fake_index_on_ready_data = NULL;

static __attribute__((unused)) void fakeIndexDrop(void) {
	if (fake_index_built) Index_free(&fake_index);
	fake_index_built = 0;
}

static const CardIndex* fakeIndexGet(void) {
	fake_index_asks++;
	if (!fake_index_ready) return NULL;
	if (!fake_index_built) {
		IndexCard card = { FAKE_INDEX_CARD, FAKE_INDEX_PLATFORM };
		fake_index_built = Index_build(&fake_index, &card, NULL, NULL, NULL) == 0;
	}
	return fake_index_built ? &fake_index : NULL;
}

// The card changed: build it again and tell the plugin, as the launcher
// does after a check.
static __attribute__((unused)) void fakeIndexRebuild(void) {
	fakeIndexDrop();
	fake_index_ready = 1;
	fakeIndexGet();
	if (fake_index_on_ready) fake_index_on_ready("launcher.index_ready", "count=0", fake_index_on_ready_data);
}

static void fakeIndexFill(const IndexEntry* e, const IndexSystem* sys, MosaicIndexEntry* out) {
	size_t size = out->size;
	memset(out, 0, size);
	out->size = size;
	out->kind = e->kind;
	out->type = e->type;
	out->depth = e->depth;
	out->emulator = sys->emulator;
	out->path = Index_str(sys, e->path);
	out->launch = Index_str(sys, e->launch);
	out->system = sys->folder;
	out->tag = sys->tag;
	out->name = Index_str(sys, e->name);
	out->year = Index_str(sys, e->year);
	out->publisher = Index_str(sys, e->publisher);
	out->genre = Index_str(sys, e->genre);
	out->players = Index_str(sys, e->players);
}

static __attribute__((unused)) int fakeIndexCount(const char* system) {
	const CardIndex* index = fakeIndexGet();
	return index ? Index_count(index, system) : MOSAIC_INDEX_NOT_READY;
}

static __attribute__((unused)) int fakeIndexEntry(const char* system, int i, MosaicIndexEntry* out) {
	const IndexSystem* sys = NULL;
	const IndexEntry* e = out ? Index_at(fakeIndexGet(), system, i, &sys) : NULL;
	if (!e) return -1;
	fakeIndexFill(e, sys, out);
	return 0;
}

static __attribute__((unused)) int fakeIndexFind(const char* path, MosaicIndexEntry* out) {
	const IndexEntry* e = NULL;
	const IndexSystem* sys = NULL;
	int at = Index_find(fakeIndexGet(), path, &e, &sys);
	if (at >= 0 && out) fakeIndexFill(e, sys, out);
	return at;
}

static __attribute__((unused)) int fakeIndexRefresh(void) {
	fake_index_refreshes++;
	return 0;
}

// For a fake subscribe: keeps the launcher.index_ready subscriber.
static __attribute__((unused)) int fakeIndexSubscribe(const char* name,
		void (*on_event)(const char* name, const char* payload, void* data), void* data) {
	if (!name || strcmp(name, "launcher.index_ready") != 0) return 0;
	fake_index_on_ready = on_event;
	fake_index_on_ready_data = data;
	return 0;
}

#define FAKE_INDEX_HOST \
	.index_count = fakeIndexCount, \
	.index_entry = fakeIndexEntry, \
	.index_find = fakeIndexFind, \
	.index_refresh = fakeIndexRefresh

#endif
