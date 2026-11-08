// artcache.c: the art cache's keys, least recently used eviction under a
// byte cap, pins, the worker's load order, and stale requests dropped by
// sequence number when the selection moves. Images are stand-in pointers.

#include <stdio.h>
#include <string.h>
#include "check.h"
#include "artcache.h"

static ArtCache cache;
static int images[64];

// Asks for path and has the worker load it straight away, taking bytes.
static void load(ArtCache* c, const char* path, int kind, void* image, size_t bytes) {
	char next[ART_CACHE_PATH];
	int next_kind;
	CHECK(ArtCache_get(c, path, kind, 0, NULL) == ARTC_WAITING);
	CHECK(ArtCache_next(c, next, sizeof(next), &next_kind) == 1);
	CHECK_STR(next, path);
	CHECK(next_kind == kind);
	CHECK(ArtCache_finish(c, path, kind, image, bytes, NULL) == 1);
}

static void testKeys(void) {
	ArtCache* c = &cache;
	ArtCache_init(c, 1000);
	void* got;
	CHECK(ArtCache_get(c, "/a.png", 0, 0, &got) == ARTC_WAITING);
	CHECK(got == NULL);
	CHECK(ArtCache_get(c, "/a.png", 0, 0, &got) == ARTC_WAITING); // asked again, still one entry
	CHECK(c->count == 1);
	char path[ART_CACHE_PATH];
	int kind;
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK(ArtCache_get(c, "/a.png", 0, 0, &got) == ARTC_LOADING);
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 0); // nothing else waits
	int notify = 1;
	CHECK(ArtCache_finish(c, "/a.png", 0, &images[0], 100, &notify) == 1);
	CHECK(notify == 0);
	CHECK(c->bytes == 100);
	CHECK(ArtCache_get(c, "/a.png", 0, 0, &got) == ARTC_READY);
	CHECK(got == &images[0]);
	CHECK(c->hits == 1);

	// the same path as another kind, and another path as the same kind, are their own
	CHECK(ArtCache_get(c, "/a.png", 1, 0, &got) == ARTC_WAITING);
	CHECK(ArtCache_get(c, "/b.png", 0, 0, &got) == ARTC_WAITING);
	CHECK(c->count == 3);
	CHECK(ArtCache_peek(c, "/a.png", 1, &got) == ARTC_WAITING);
	CHECK(ArtCache_peek(c, "/c.png", 0, &got) == ARTC_NONE);

	// a finished load nobody's waiting for, or one already in, isn't kept
	CHECK(ArtCache_finish(c, "/c.png", 0, &images[1], 10, NULL) == 0);
	CHECK(ArtCache_finish(c, "/a.png", 0, &images[1], 10, NULL) == 0);
	CHECK(c->bytes == 100);

	// a failed decode is remembered, taking nothing
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK(ArtCache_finish(c, path, kind, NULL, 500, NULL) == 1);
	CHECK(ArtCache_get(c, path, kind, 0, &got) == ARTC_FAILED);
	CHECK(c->bytes == 100);

	// refused
	CHECK(ArtCache_get(c, NULL, 0, 0, &got) == ARTC_NONE);
	CHECK(ArtCache_get(c, "", 0, 0, &got) == ARTC_NONE);
	char longer[ART_CACHE_PATH + 1];
	memset(longer, 'x', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = '\0';
	CHECK(ArtCache_get(c, longer, 0, 0, &got) == ARTC_NONE);
	CHECK(ArtCache_get(NULL, "/a.png", 0, 0, &got) == ARTC_NONE);
}

static void testOrder(void) {
	ArtCache* c = &cache;
	ArtCache_init(c, 1000);
	char path[ART_CACHE_PATH];
	int kind;
	CHECK(ArtCache_get(c, "/next.png", 0, ARTC_PREFETCH, NULL) == ARTC_WAITING);
	CHECK(ArtCache_get(c, "/old.png", 0, 0, NULL) == ARTC_WAITING);
	CHECK(ArtCache_get(c, "/prev.png", 0, ARTC_PREFETCH, NULL) == ARTC_WAITING);
	CHECK(ArtCache_get(c, "/new.png", 0, 0, NULL) == ARTC_WAITING);
	// asked for now first, newest first, then the prefetches, newest first
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/new.png");
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/old.png");
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/prev.png");
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/next.png");
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 0);

	// asking again makes it the newest, and asking for a prefetch outright
	// takes it out of the prefetches
	ArtCache_init(c, 1000);
	ArtCache_get(c, "/one.png", 0, 0, NULL);
	ArtCache_get(c, "/two.png", 0, 0, NULL);
	ArtCache_get(c, "/near.png", 0, ARTC_PREFETCH, NULL);
	ArtCache_get(c, "/one.png", 0, 0, NULL);
	ArtCache_get(c, "/near.png", 0, 0, NULL);
	ArtCache_get(c, "/two.png", 0, ARTC_PREFETCH, NULL); // a prefetch doesn't demote it
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/two.png");
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/near.png");
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/one.png");

	// a path too long for the caller's buffer stays waiting
	ArtCache_init(c, 1000);
	ArtCache_get(c, "/a-long-name.png", 0, 0, NULL);
	char tiny[4];
	CHECK(ArtCache_next(c, tiny, sizeof(tiny), &kind) == 0);
	CHECK(ArtCache_peek(c, "/a-long-name.png", 0, NULL) == ARTC_WAITING);

	// a plugin's request says to redraw once it's in; a prefetch alone doesn't
	ArtCache_init(c, 1000);
	ArtCache_get(c, "/quiet.png", 0, ARTC_PREFETCH, NULL);
	ArtCache_get(c, "/loud.png", 0, ARTC_NOTIFY, NULL);
	int notify = -1;
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK(ArtCache_finish(c, path, kind, &images[0], 1, &notify) == 1);
	CHECK_STR(path, "/loud.png");
	CHECK(notify == 1);
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK(ArtCache_finish(c, path, kind, &images[1], 1, &notify) == 1);
	CHECK(notify == 0);
}

static void testStale(void) {
	ArtCache* c = &cache;
	ArtCache_init(c, 1000);
	char path[ART_CACHE_PATH];
	int kind;
	load(c, "/kept.png", 0, &images[0], 10);
	ArtCache_get(c, "/busy.png", 0, 0, NULL);
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1); // the worker has it
	ArtCache_get(c, "/waiting.png", 0, 0, NULL);
	ArtCache_get(c, "/near.png", 0, ARTC_PREFETCH, NULL);
	unsigned before = c->next_seq;

	// the selection moves: what waits is dropped, decoded images stay
	ArtCache_mark(c);
	CHECK(c->stale_before == before);
	CHECK(c->dropped == 3);
	CHECK(ArtCache_peek(c, "/waiting.png", 0, NULL) == ARTC_NONE);
	CHECK(ArtCache_peek(c, "/near.png", 0, NULL) == ARTC_NONE);
	CHECK(ArtCache_peek(c, "/kept.png", 0, NULL) == ARTC_READY);
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 0);
	// the started load is dropped when it finishes
	CHECK(ArtCache_finish(c, "/busy.png", 0, &images[1], 10, NULL) == 0);
	CHECK(c->bytes == 10);

	// unless it's asked for again first
	ArtCache_get(c, "/again.png", 0, 0, NULL);
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	ArtCache_mark(c);
	CHECK(ArtCache_get(c, "/again.png", 0, 0, NULL) == ARTC_WAITING);
	CHECK(ArtCache_finish(c, "/again.png", 0, &images[2], 10, NULL) == 1);
	CHECK(ArtCache_peek(c, "/again.png", 0, NULL) == ARTC_READY);
	// and its second load, since it was waiting again, is dropped as already in
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 0);
	CHECK(c->dropped == 4);

	// asked for after the mark, it isn't stale
	ArtCache_get(c, "/fresh.png", 0, 0, NULL);
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 1);
	CHECK_STR(path, "/fresh.png");
	CHECK(ArtCache_finish(c, path, kind, &images[3], 10, NULL) == 1);
}

static void testEvict(void) {
	ArtCache* c = &cache;
	ArtCache_init(c, 250);
	void* out[8];
	load(c, "/a.png", 0, &images[0], 100);
	load(c, "/b.png", 0, &images[1], 100);
	CHECK(ArtCache_evict(c, out, 8) == 0); // under the cap
	ArtCache_get(c, "/a.png", 0, 0, NULL); // a is now used after b
	load(c, "/c.png", 0, &images[2], 100);
	CHECK(c->bytes == 300);
	// the least recently used goes, not the first in
	CHECK(ArtCache_evict(c, out, 8) == 1);
	CHECK(out[0] == &images[1]);
	CHECK(c->bytes == 200);
	CHECK(c->evicted == 1);
	CHECK(ArtCache_peek(c, "/b.png", 0, NULL) == ARTC_NONE);
	CHECK(ArtCache_peek(c, "/a.png", 0, NULL) == ARTC_READY);
	CHECK(ArtCache_peek(c, "/c.png", 0, NULL) == ARTC_READY);
	// a peek doesn't count as a use
	ArtCache_peek(c, "/a.png", 0, NULL);
	ArtCache_get(c, "/c.png", 0, 0, NULL);

	// pinned stays, even when it's the oldest, and loads in progress aren't touched
	CHECK(ArtCache_pin(c, "/a.png", 0) == 0);
	CHECK(ArtCache_pin(c, "/b.png", 0) == -1);
	ArtCache_get(c, "/d.png", 0, 0, NULL);
	CHECK(ArtCache_pin(c, "/d.png", 0) == -1); // not in yet
	load(c, "/e.png", 0, &images[4], 100);
	CHECK(ArtCache_evict(c, out, 8) == 1);
	CHECK(out[0] == &images[2]);
	CHECK(ArtCache_peek(c, "/a.png", 0, NULL) == ARTC_READY);
	CHECK(ArtCache_peek(c, "/d.png", 0, NULL) == ARTC_WAITING);

	// one image over the cap by itself stays while pinned, and is the next to go once unpinned
	ArtCache_init(c, 250);
	load(c, "/huge.png", 0, &images[5], 1000);
	CHECK(ArtCache_pin(c, "/huge.png", 0) == 0);
	CHECK(ArtCache_evict(c, out, 8) == 0);
	CHECK(ArtCache_unpin(c, "/huge.png", 0) == 0);
	CHECK(ArtCache_unpin(c, "/huge.png", 0) == -1);
	CHECK(ArtCache_evict(c, out, 8) == 1);
	CHECK(c->bytes == 0 && c->count == 0);

	// out's size bounds one call
	ArtCache_init(c, 0);
	load(c, "/1.png", 0, &images[6], 1);
	load(c, "/2.png", 0, &images[7], 1);
	CHECK(ArtCache_evict(c, out, 1) == 1);
	CHECK(ArtCache_evict(c, out, 1) == 1);
	CHECK(ArtCache_evict(c, out, 1) == 0);
}

static void testFull(void) {
	ArtCache* c = &cache;
	ArtCache_init(c, 1 << 20);
	char path[64];
	for (int i = 0; i < ART_CACHE_ENTRIES; i++) {
		snprintf(path, sizeof(path), "/%i.png", i);
		CHECK(ArtCache_get(c, path, 0, 0, NULL) == ARTC_WAITING);
	}
	CHECK(ArtCache_get(c, "/more.png", 0, 0, NULL) == ARTC_NONE);
	// waiting requests can't be evicted, only dropped as stale
	void* out[ART_CACHE_ENTRIES];
	CHECK(ArtCache_evict(c, out, ART_CACHE_ENTRIES) == 0);
	ArtCache_mark(c);
	CHECK(c->count == 0);

	// with only the count over, failures go first, then the oldest images
	for (int i = 0; i < ART_CACHE_ENTRIES; i++) {
		snprintf(path, sizeof(path), "/%i.png", i);
		ArtCache_get(c, path, 0, 0, NULL);
		char next[ART_CACHE_PATH];
		int kind;
		ArtCache_next(c, next, sizeof(next), &kind);
		ArtCache_finish(c, next, kind, i % 2 ? &images[i % 64] : NULL, 1, NULL);
	}
	int n = ArtCache_evict(c, out, ART_CACHE_ENTRIES);
	CHECK(n == 0); // every one evicted was a failure
	CHECK(c->count == ART_CACHE_ENTRIES - ART_CACHE_SPARE);
	CHECK(c->evicted == ART_CACHE_SPARE);
	for (int i = 0; i < c->count; i++) {
		if (c->entries[i].state == ARTC_READY) continue;
		CHECK(c->entries[i].state == ARTC_FAILED);
	}
}

static void testClear(void) {
	ArtCache* c = &cache;
	ArtCache_init(c, 1000);
	void* out[8];
	load(c, "/shown.png", 0, &images[0], 10);
	load(c, "/other.png", 0, &images[1], 10);
	ArtCache_get(c, "/waiting.png", 0, 0, NULL);
	ArtCache_pin(c, "/shown.png", 0);
	CHECK(ArtCache_clear(c, 0, out, 8) == 1);
	CHECK(out[0] == &images[1]);
	CHECK(c->count == 1 && c->bytes == 10);
	CHECK(ArtCache_peek(c, "/shown.png", 0, NULL) == ARTC_READY);
	CHECK(ArtCache_clear(c, 1, out, 8) == 1);
	CHECK(out[0] == &images[0]);
	CHECK(c->count == 0 && c->bytes == 0);
}

static void testNames(void) {
	static ArtNames names;
	ArtNames_clear(&names);
	char path[ART_CACHE_PATH];
	int found = -1;
	CHECK(ArtNames_get(&names, "/Roms/a.gb", 0, path, sizeof(path), &found) == 0);
	ArtNames_set(&names, "/Roms/a.gb", 0, "/Roms/.media/a.png", 1);
	ArtNames_set(&names, "/Roms/a.gb", 3, "", 0);
	CHECK(ArtNames_get(&names, "/Roms/a.gb", 0, path, sizeof(path), &found) == 1);
	CHECK_STR(path, "/Roms/.media/a.png");
	CHECK(found == 1);
	CHECK(ArtNames_get(&names, "/Roms/a.gb", 3, path, sizeof(path), &found) == 1);
	CHECK_STR(path, "");
	CHECK(found == 0);
	ArtNames_set(&names, "/Roms/a.gb", 0, "/elsewhere.png", 0); // replaced, not added
	CHECK(names.count == 2);
	char tiny[4];
	CHECK(ArtNames_get(&names, "/Roms/a.gb", 0, tiny, sizeof(tiny), &found) == 0);

	// full, it starts over
	for (int i = 0; i < ART_NAMES; i++) {
		char subject[32];
		snprintf(subject, sizeof(subject), "/%i", i);
		ArtNames_set(&names, subject, 0, subject, 1);
	}
	CHECK(names.count <= ART_NAMES);
	ArtNames_clear(&names);
	CHECK(ArtNames_get(&names, "/0", 0, path, sizeof(path), &found) == 0);
}

// generated art goes straight in, under a key no file has, and out like loaded art
static void testPut(void) {
	ArtCache* c = &cache;
	ArtCache_init(c, 100);
	char key[ART_CACHE_PATH];
	CHECK(ArtCache_generatedKey("/Roms/a.gb", key, sizeof(key)) == 1);
	CHECK_STR(key, "generated:/Roms/a.gb");
	char small[8];
	CHECK(ArtCache_generatedKey("/Roms/a.gb", small, sizeof(small)) == 0);
	CHECK(ArtCache_generatedKey("", key, sizeof(key)) == 0);
	CHECK(ArtCache_generatedKey(NULL, key, sizeof(key)) == 0);
	char longer[ART_CACHE_PATH];
	memset(longer, 'x', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = '\0';
	char big[ART_CACHE_PATH * 2];
	CHECK(ArtCache_generatedKey(longer, big, sizeof(big)) == 0);

	CHECK(ArtCache_generatedKey("/Roms/a.gb", key, sizeof(key)) == 1);
	CHECK(ArtCache_put(c, key, 3, &images[0], 60) == 1);
	CHECK(c->bytes == 60);
	void* got;
	CHECK(ArtCache_peek(c, key, 3, &got) == ARTC_READY);
	CHECK(got == &images[0]);
	CHECK(ArtCache_get(c, key, 3, 0, &got) == ARTC_READY);
	CHECK(got == &images[0]);
	// once only, and never handed to the worker
	CHECK(ArtCache_put(c, key, 3, &images[1], 60) == 0);
	char path[ART_CACHE_PATH];
	int kind;
	CHECK(ArtCache_next(c, path, sizeof(path), &kind) == 0);
	// the same key as another kind is another image
	CHECK(ArtCache_put(c, key, 4, NULL, 60) == 1);
	CHECK(ArtCache_peek(c, key, 4, &got) == ARTC_FAILED);
	CHECK(c->bytes == 60);
	CHECK(ArtCache_put(c, "", 3, &images[1], 1) == 0);
	CHECK(ArtCache_put(NULL, key, 5, &images[1], 1) == 0);

	// the selection moving doesn't drop it, the cap does
	ArtCache_mark(c);
	CHECK(ArtCache_peek(c, key, 3, NULL) == ARTC_READY);
	CHECK(ArtCache_generatedKey("/Roms/b.gb", key, sizeof(key)) == 1);
	CHECK(ArtCache_put(c, key, 3, &images[2], 60) == 1);
	CHECK(c->bytes == 120);
	void* out[4];
	CHECK(ArtCache_evict(c, out, 4) == 1);
	CHECK(out[0] == &images[0]);
	CHECK(c->bytes == 60);
	// and a pinned one stays
	CHECK(ArtCache_pin(c, key, 3) == 0);
	c->cap = 0;
	CHECK(ArtCache_evict(c, out, 4) == 0);
	CHECK(ArtCache_unpin(c, key, 3) == 0);
	CHECK(ArtCache_clear(c, 1, out, 4) == 1);
	CHECK(out[0] == &images[2]);

	// no entry free
	ArtCache_init(c, 1000);
	for (int i = 0; i < ART_CACHE_ENTRIES; i++) {
		char name[32];
		snprintf(name, sizeof(name), "generated:/%i", i);
		CHECK(ArtCache_put(c, name, 0, NULL, 0) == 1);
	}
	CHECK(ArtCache_put(c, "generated:/full", 0, &images[3], 1) == 0);
}

int main(void) {
	testKeys();
	testOrder();
	testStale();
	testEvict();
	testFull();
	testClear();
	testNames();
	testPut();
	CHECK_DONE();
}
