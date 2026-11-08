#ifndef ARTLOADER_H
#define ARTLOADER_H

// The art cache over SDL (artcache.c is its SDL-free half). Images decode
// on imgloader.c's art worker, one at a time; the main thread takes them
// in, evicts and hands them out. The built-in list's thumbnail and
// plugins' art_get and art_load all go through it, so they share one
// byte cap, prefetch the entries either side of the selection, and drop
// loads asked for before the selection last moved.

#include "mosaic_plugin.h"

// Cache kinds besides the MOSAIC_ART_* ones plugins ask for, which decode
// the way image_load does.
#define ART_KIND_LIST_THUMB 100 // the built-in list's thumbnail, decoded and rounded as stock does
#define ART_KIND_IMAGE 101      // art_load's: an image by its path alone

// MOSAIC_LAUNCHER_ART_CACHE_BYTES, when set, replaces ART_CACHE_BYTES (for tests).
void ArtLoader_init(void);
// After the worker has stopped: frees every image, the thumbnail on screen too.
void ArtLoader_quit(void);

// Takes in the loads the worker finished and hands it the next. With
// evict, at the start of a frame, it also drops images past the cap, so
// what plugins were given stays valid for the rest of the frame. Call it
// again without evict just before drawing, so a thumbnail that's in shows
// on this frame.
void ArtLoader_pump(int evict);

// The lists reloaded, so files may have changed: forgets all but the
// thumbnail on screen. Call it at the start of a frame.
void ArtLoader_reset(void);

// On the worker: path decoded as a cache kind, or NULL.
struct SDL_Surface* ArtLoader_decode(const char* path, int kind);

// The built-in list's thumbnail. ArtLoader_showThumb puts path's image in
// thumbbmp, or none for "", keeping the one shown until the new one is in.
// ArtLoader_prefetchThumb loads a neighbour's after it.
void ArtLoader_showThumb(const char* path);
void ArtLoader_prefetchThumb(const char* path);

// host->art_find, art_get and art_load, for a MOSAIC_ART_* kind.
int ArtLoader_find(const Entry* entry, int kind, char* out, size_t size);
int ArtLoader_get(const Entry* entry, int kind, const MosaicSurface** out);
int ArtLoader_load(const char* path, const MosaicSurface** out);

#endif
