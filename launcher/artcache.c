#include <string.h>
#include "artcache.h"

void ArtCache_init(ArtCache* c, size_t cap) {
	memset(c, 0, sizeof(*c));
	c->cap = cap;
	c->next_seq = 1;
	c->stale_before = 1;
}

static int find(const ArtCache* c, const char* path, int kind) {
	for (int i = 0; i < c->count; i++) {
		if (c->entries[i].kind == kind && !strcmp(c->entries[i].path, path)) return i;
	}
	return -1;
}

static void drop(ArtCache* c, int i) {
	c->bytes -= c->entries[i].bytes;
	c->entries[i] = c->entries[--c->count];
}

static int pending(const ArtCacheEntry* e) {
	return e->state == ARTC_WAITING || e->state == ARTC_LOADING;
}

int ArtCache_get(ArtCache* c, const char* path, int kind, int flags, void** image) {
	if (image) *image = NULL;
	if (!c || !path || !path[0] || strlen(path) >= ART_CACHE_PATH) return ARTC_NONE;
	int i = find(c, path, kind);
	if (i < 0) {
		if (c->count == ART_CACHE_ENTRIES) return ARTC_NONE;
		i = c->count++;
		ArtCacheEntry* e = &c->entries[i];
		memset(e, 0, sizeof(*e));
		strcpy(e->path, path);
		e->kind = kind;
		e->state = ARTC_WAITING;
		e->prefetch = 1;
	}
	ArtCacheEntry* e = &c->entries[i];
	e->seq = c->next_seq++;
	if (pending(e)) {
		if (!(flags & ARTC_PREFETCH)) e->prefetch = 0;
		if (flags & ARTC_NOTIFY) e->notify = 1;
		return e->state;
	}
	e->used = ++c->clock;
	if (e->state == ARTC_READY) {
		if (!(flags & ARTC_PREFETCH)) c->hits++;
		if (image) *image = e->image;
	}
	return e->state;
}

int ArtCache_peek(const ArtCache* c, const char* path, int kind, void** image) {
	if (image) *image = NULL;
	if (!c || !path) return ARTC_NONE;
	int i = find(c, path, kind);
	if (i < 0) return ARTC_NONE;
	if (image && c->entries[i].state == ARTC_READY) *image = c->entries[i].image;
	return c->entries[i].state;
}

int ArtCache_next(ArtCache* c, char* path, size_t size, int* kind) {
	int best = -1;
	for (int i = 0; i < c->count; i++) {
		const ArtCacheEntry* e = &c->entries[i];
		if (e->state != ARTC_WAITING || e->seq < c->stale_before) continue;
		if (best < 0) {
			best = i;
			continue;
		}
		const ArtCacheEntry* b = &c->entries[best];
		if (e->prefetch != b->prefetch ? !e->prefetch : e->seq > b->seq) best = i;
	}
	if (best < 0) return 0;
	ArtCacheEntry* e = &c->entries[best];
	if (strlen(e->path) >= size) return 0;
	e->state = ARTC_LOADING;
	strcpy(path, e->path);
	*kind = e->kind;
	return 1;
}

int ArtCache_finish(ArtCache* c, const char* path, int kind, void* image, size_t bytes, int* notify) {
	if (notify) *notify = 0;
	int i = path ? find(c, path, kind) : -1;
	if (i < 0 || !pending(&c->entries[i])) return 0;
	ArtCacheEntry* e = &c->entries[i];
	e->state = image ? ARTC_READY : ARTC_FAILED;
	e->image = image;
	e->bytes = image ? bytes : 0;
	e->used = ++c->clock;
	c->bytes += e->bytes;
	c->loads++;
	if (notify) *notify = e->notify;
	e->notify = 0;
	return 1;
}

void ArtCache_mark(ArtCache* c) {
	c->stale_before = c->next_seq;
	for (int i = c->count - 1; i >= 0; i--) {
		if (pending(&c->entries[i]) && c->entries[i].seq < c->stale_before) {
			drop(c, i);
			c->dropped++;
		}
	}
}

// The least recently used entry eviction can drop, or -1.
static int oldestOf(const ArtCache* c, int failed_only) {
	int oldest = -1;
	for (int i = 0; i < c->count; i++) {
		const ArtCacheEntry* e = &c->entries[i];
		if (pending(e) || e->pins > 0 || (failed_only && e->state != ARTC_FAILED)) continue;
		if (oldest < 0 || e->used < c->entries[oldest].used) oldest = i;
	}
	return oldest;
}

int ArtCache_evict(ArtCache* c, void** out, int max) {
	int n = 0;
	while (n < max && (c->bytes > c->cap || c->count > ART_CACHE_ENTRIES - ART_CACHE_SPARE)) {
		// with only the count over, failures go before images
		int oldest = c->bytes <= c->cap ? oldestOf(c, 1) : -1;
		if (oldest < 0) oldest = oldestOf(c, 0);
		if (oldest < 0) break;
		if (c->entries[oldest].image) out[n++] = c->entries[oldest].image;
		drop(c, oldest);
		c->evicted++;
	}
	return n;
}

static int pinnable(ArtCache* c, const char* path, int kind) {
	int i = path ? find(c, path, kind) : -1;
	if (i < 0 || pending(&c->entries[i])) return -1;
	return i;
}

int ArtCache_pin(ArtCache* c, const char* path, int kind) {
	int i = pinnable(c, path, kind);
	if (i < 0) return -1;
	c->entries[i].pins++;
	return 0;
}

int ArtCache_unpin(ArtCache* c, const char* path, int kind) {
	int i = pinnable(c, path, kind);
	if (i < 0 || c->entries[i].pins == 0) return -1;
	c->entries[i].pins--;
	return 0;
}

int ArtCache_clear(ArtCache* c, int all, void** out, int max) {
	int n = 0;
	for (int i = c->count - 1; i >= 0; i--) {
		ArtCacheEntry* e = &c->entries[i];
		if (!all && e->pins > 0) continue;
		if (e->image) {
			if (n == max) continue;
			out[n++] = e->image;
		}
		drop(c, i);
	}
	return n;
}

///////////////////////////////////////
// names

static int nameAt(const ArtNames* n, const char* subject, int kind) {
	for (int i = 0; i < n->count; i++) {
		if (n->names[i].kind == kind && !strcmp(n->names[i].subject, subject)) return i;
	}
	return -1;
}

int ArtNames_get(const ArtNames* n, const char* subject, int kind, char* path, size_t size, int* found) {
	int i = subject ? nameAt(n, subject, kind) : -1;
	if (i < 0 || strlen(n->names[i].path) >= size) return 0;
	strcpy(path, n->names[i].path);
	*found = n->names[i].found;
	return 1;
}

void ArtNames_set(ArtNames* n, const char* subject, int kind, const char* path, int found) {
	if (!subject || !path || strlen(subject) >= ART_CACHE_PATH || strlen(path) >= ART_CACHE_PATH) return;
	int i = nameAt(n, subject, kind);
	if (i < 0) {
		if (n->count == ART_NAMES) n->count = 0;
		i = n->count++;
	}
	ArtName* name = &n->names[i];
	strcpy(name->subject, subject);
	name->kind = kind;
	strcpy(name->path, path);
	name->found = found;
}

void ArtNames_clear(ArtNames* n) {
	n->count = 0;
}
