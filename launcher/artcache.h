#ifndef ARTCACHE_H
#define ARTCACHE_H

// The SDL-free half of the art cache: decoded images kept by path and kind,
// the least recently used dropped first once they take more than a byte
// cap, and the loads waiting for the decode worker, where requests made
// before the selection last changed are dropped as stale. artloader.c
// decodes and hands the images out; here they're opaque pointers.

#include <stddef.h>

#define ART_CACHE_BYTES (32 * 1024 * 1024) // what decoded art can take together, apart from plugins' own surfaces
#define ART_CACHE_ENTRIES 256              // the most images, loads and failures it keeps track of
#define ART_CACHE_SPARE 32                 // entries eviction leaves free, for the next frame's requests
#define ART_CACHE_PATH 512

enum ArtCacheState {
	ARTC_NONE,    // not in the cache, and no room to ask for it
	ARTC_WAITING, // asked for, waiting for the worker
	ARTC_LOADING, // with the worker
	ARTC_READY,   // decoded
	ARTC_FAILED,  // couldn't be decoded
};

// ArtCache_get's flags.
#define ARTC_PREFETCH 1 // a neighbour of the selection: loads after what's asked for now
#define ARTC_NOTIFY 2   // a plugin asked, so the caller redraws once it's in

typedef struct ArtCacheEntry {
	char path[ART_CACHE_PATH];
	int kind;
	int state;
	void* image;
	size_t bytes;
	unsigned seq;  // the latest request's sequence number
	unsigned used; // when it was last asked for or loaded, for least recently used
	int prefetch;  // only prefetches have asked for it
	int notify;
	int pins;
} ArtCacheEntry;

typedef struct ArtCache {
	ArtCacheEntry entries[ART_CACHE_ENTRIES];
	int count;
	size_t cap;
	size_t bytes;           // what its images take now
	unsigned next_seq;
	unsigned stale_before;  // requests numbered below this came before the selection last changed
	unsigned clock;
	unsigned hits, loads, evicted, dropped;
} ArtCache;

void ArtCache_init(ArtCache* c, size_t cap);

// Asks for path's image of kind, with ARTC_* flags. Returns its state:
// READY writes the image to *image and counts as a use, and a new request
// is WAITING. Returns NONE for a bad argument, or when every entry is in
// use, until ArtCache_evict makes room.
int ArtCache_get(ArtCache* c, const char* path, int kind, int flags, void** image);

// Its state, without asking for it or counting a use.
int ArtCache_peek(const ArtCache* c, const char* path, int kind, void** image);

// The next load for the worker: what's asked for now before prefetches,
// the newest request first. Marks it LOADING. Returns 1, or 0 when nothing
// waits.
int ArtCache_next(ArtCache* c, char* path, size_t size, int* kind);

// A load finished with image, NULL when it couldn't be decoded, taking
// bytes. Returns 1 when it's kept, writing to *notify whether a plugin
// asked for it, or 0 when it's no longer wanted (dropped as stale or
// cleared, or already in), and the caller frees image.
int ArtCache_finish(ArtCache* c, const char* path, int kind, void* image, size_t bytes, int* notify);

// The selection changed: every request made before now is stale and
// dropped, so only what's asked for again loads. A load the worker has
// already started is dropped when it finishes, unless asked for again.
void ArtCache_mark(ArtCache* c);

// Drops the least recently used images and failures, never pinned ones or
// loads, until the images take at most the cap and ART_CACHE_SPARE entries
// are free. Writes the dropped images to out (up to max) to be freed, and
// returns how many.
int ArtCache_evict(ArtCache* c, void** out, int max);

// A pinned image is never evicted or cleared, eg. the thumbnail on screen.
// Pins count; both return 0, or -1 when it isn't READY or FAILED.
int ArtCache_pin(ArtCache* c, const char* path, int kind);
int ArtCache_unpin(ArtCache* c, const char* path, int kind);

// Forgets every entry but pinned ones (all of them with all set), and the
// loads in progress, which are dropped when they finish. Writes the
// forgotten images to out (up to max), and returns how many.
int ArtCache_clear(ArtCache* c, int all, void** out, int max);

// Which image each entry and kind resolved to, so asking every frame
// doesn't ask plugins and the card every frame. Forgotten whenever the
// selection changes.
#define ART_NAMES 64

typedef struct ArtName {
	char subject[ART_CACHE_PATH];
	int kind;
	char path[ART_CACHE_PATH];
	int found;
} ArtName;

typedef struct ArtNames {
	ArtName names[ART_NAMES];
	int count;
} ArtNames;

// Returns 1 and writes what subject's kind resolved to, or 0 when it isn't known.
int ArtNames_get(const ArtNames* n, const char* subject, int kind, char* path, size_t size, int* found);
// Remembers it, starting over when full. Paths too long to keep aren't.
void ArtNames_set(ArtNames* n, const char* subject, int kind, const char* path, int found);
void ArtNames_clear(ArtNames* n);

#endif
