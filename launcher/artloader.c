#include "launcher.h"
#include "artcache.h"
#include "drawmodel.h"
#include "plugindraw.h"

static ArtCache cache;
static ArtNames names;
static int started = 0;
static int busy = 0;    // the worker has a load
static int finding = 0; // in art_find, so plugins' art_path hooks can't ask again

// the built-in list's thumbnail: the one it wants, and the one in thumbbmp
static char thumb_wanted[ART_CACHE_PATH] = "";
static char thumb_shown[ART_CACHE_PATH] = "";

// the selection at the last look, and the kinds prefetched either side of it
static const void* seen_top = NULL;
static const void* seen_entries = NULL;
static int seen_selected = -1;
static unsigned prefetched = 0;

void ArtLoader_init(void) {
	size_t cap = ART_CACHE_BYTES;
	const char* bytes = getenv("MOSAIC_LAUNCHER_ART_CACHE_BYTES");
	if (bytes && bytes[0]) cap = (size_t)strtoul(bytes, NULL, 10);
	ArtCache_init(&cache, cap);
	ArtNames_clear(&names);
	started = 1;
}

static void freeImage(void* image) {
	DrawReg_remove(image, DRAW_HOST); // plugins can no longer use it
	SDL_FreeSurface(image);
}

// When the selection moves, what was asked for before is stale.
static void checkSelection(void) {
	const void* top = state.top;
	const void* entries = state.top ? state.top->entries : NULL;
	int selected = state.top ? state.top->selected : -1;
	if (top == seen_top && entries == seen_entries && selected == seen_selected) return;
	seen_top = top;
	seen_entries = entries;
	seen_selected = selected;
	ArtCache_mark(&cache);
	ArtNames_clear(&names);
	prefetched = 0;
}

// Gives the worker the next load, if it's free.
static void dispatch(void) {
	if (busy) return;
	char path[ART_CACHE_PATH];
	int kind;
	if (!ArtCache_next(&cache, path, sizeof(path), &kind)) return;
	if (startLoadArt(path, kind) == 0) busy = 1;
	else ArtCache_finish(&cache, path, kind, NULL, 0, NULL); // no worker
}

static void setShown(const char* path, void* image) {
	if (thumb_shown[0]) ArtCache_unpin(&cache, thumb_shown, ART_KIND_LIST_THUMB);
	snprintf(thumb_shown, sizeof(thumb_shown), "%s", image ? path : "");
	if (image) ArtCache_pin(&cache, path, ART_KIND_LIST_THUMB);
	SDL_LockMutex(thumbMutex);
	thumbbmp = image;
	thumbchanged = 1;
	SDL_UnlockMutex(thumbMutex);
	setNeedDraw(1);
}

void ArtLoader_pump(int evict) {
	if (!started) return;
	for (ArtLoad* load = takeLoadedArt(); load;) {
		ArtLoad* next = load->next;
		busy = 0;
		SDL_Surface* image = load->image;
		int notify = 0;
		if (ArtCache_finish(&cache, load->path, load->kind, image, load->bytes, &notify)) {
			// plugins can draw it and read it, but not draw on it or free it
			if (image && load->kind != ART_KIND_LIST_THUMB && DrawReg_add(image, DRAW_HOST, 0) == 0) DrawReg_lock(image);
			if (notify) state.dirty = 1;
			if (load->kind == ART_KIND_LIST_THUMB && !strcmp(load->path, thumb_wanted)) setShown(load->path, image);
		}
		else if (image) SDL_FreeSurface(image);
		free(load);
		load = next;
	}
	if (evict) {
		void* out[32];
		int n;
		do {
			n = ArtCache_evict(&cache, out, 32);
			for (int i = 0; i < n; i++) freeImage(out[i]);
		} while (n == 32);
	}
	dispatch();
}

void ArtLoader_reset(void) {
	if (!started) return;
	void* out[32];
	int n;
	while ((n = ArtCache_clear(&cache, 0, out, 32)) > 0) {
		for (int i = 0; i < n; i++) freeImage(out[i]);
	}
	ArtNames_clear(&names);
	prefetched = 0;
}

void ArtLoader_quit(void) {
	if (!started) return;
	LOG_info("art cache: %u loaded, %u hits, %u evicted, %u stale requests dropped\n",
		cache.loads, cache.hits, cache.evicted, cache.dropped);
	for (ArtLoad* load = takeLoadedArt(); load;) {
		ArtLoad* next = load->next;
		if (load->image) SDL_FreeSurface(load->image);
		free(load);
		load = next;
	}
	void* out[32];
	int n;
	while ((n = ArtCache_clear(&cache, 1, out, 32)) > 0) {
		for (int i = 0; i < n; i++) freeImage(out[i]);
	}
	// its mutex is gone with the worker
	thumbbmp = NULL;
	thumb_shown[0] = thumb_wanted[0] = '\0';
	busy = 0;
	started = 0;
}

///////////////////////////////////////
// decoding, on the worker

// As stock's thumbnail worker loads it, with the rounded corners it gives it.
static SDL_Surface* decodeThumb(const char* path) {
	if (access(path, F_OK) != 0) return NULL;
	SDL_Surface* image = IMG_Load(path);
	if (!image) return NULL;
	SDL_Surface* thumb = SDL_ConvertSurfaceFormat(image, screen->format->format, 0);
	SDL_FreeSurface(image);
	if (!thumb) return NULL;

	int img_w = thumb->w;
	int img_h = thumb->h;
	double aspect_ratio = (double)img_h / img_w;
	int max_w = (int)(screen->w * CFG_getGameArtWidth());
	int max_h = (int)(screen->h * 0.6);
	int new_w = max_w;
	int new_h = (int)(new_w * aspect_ratio);
	if (new_h > max_h) {
		new_h = max_h;
		new_w = (int)(new_h / aspect_ratio);
	}
	GFX_ApplyRoundedCorners_8888(
		thumb,
		&(SDL_Rect){0, 0, thumb->w, thumb->h},
		SCALE1((float)CFG_getThumbnailRadius() * ((float)img_w / (float)new_w))
	);
	return thumb;
}

// As image_load reads it: only a regular file, opened without blocking.
static SDL_Surface* decodeImage(const char* path) {
	size_t size = 0;
	void* data = PluginDraw_readImageFile(path, &size);
	if (!data) {
		LOG_warn("art cache: can't load %s: not a readable file up to 16 MiB\n", path);
		return NULL;
	}
	SDL_Surface* decoded = IMG_Load_RW(SDL_RWFromConstMem(data, (int)size), 1);
	free(data);
	if (!decoded) {
		LOG_warn("art cache: can't load %s: %s\n", path, IMG_GetError());
		return NULL;
	}
	SDL_Surface* image = NULL;
	if (decoded->w <= DRAW_MAX_SIDE && decoded->h <= DRAW_MAX_SIDE) {
		image = SDL_ConvertSurfaceFormat(decoded, SDL_PIXELFORMAT_ARGB8888, 0);
		if (image) SDL_SetSurfaceBlendMode(image, SDL_BLENDMODE_BLEND);
	}
	else {
		LOG_warn("art cache: can't load %s: %ix%i is over %i a side\n", path, decoded->w, decoded->h, DRAW_MAX_SIDE);
	}
	SDL_FreeSurface(decoded);
	return image;
}

SDL_Surface* ArtLoader_decode(const char* path, int kind) {
	return kind == ART_KIND_LIST_THUMB ? decodeThumb(path) : decodeImage(path);
}

///////////////////////////////////////
// the built-in list

void ArtLoader_showThumb(const char* path) {
	if (!started) return;
	checkSelection();
	snprintf(thumb_wanted, sizeof(thumb_wanted), "%s", path);
	if (!path[0]) {
		setShown("", NULL);
		return;
	}
	void* image;
	int st = ArtCache_get(&cache, path, ART_KIND_LIST_THUMB, 0, &image);
	if (st == ARTC_READY) setShown(path, image);
	else if (st == ARTC_FAILED || st == ARTC_NONE) setShown("", NULL);
	dispatch();
}

void ArtLoader_prefetchThumb(const char* path) {
	if (!started || !path[0]) return;
	checkSelection();
	ArtCache_get(&cache, path, ART_KIND_LIST_THUMB, ARTC_PREFETCH, NULL);
	dispatch();
}

///////////////////////////////////////
// plugins

static int stockKind(int kind) {
	switch (kind) {
		case MOSAIC_ART_THUMBNAIL: return ART_THUMBNAIL;
		case MOSAIC_ART_FOLDER_BG: return ART_FOLDER_BG;
		case MOSAIC_ART_LIST_BG: return ART_LIST_BG;
		case MOSAIC_ART_BOX: return ART_BOX;
		case MOSAIC_ART_BOX_3D: return ART_BOX_3D;
		case MOSAIC_ART_LOGO: return ART_LOGO;
		case MOSAIC_ART_SCREENSHOT: return ART_SCREENSHOT;
		case MOSAIC_ART_BACKDROP: return ART_BACKDROP;
		default: return -1;
	}
}

int ArtLoader_find(const Entry* entry, int kind, char* out, size_t size) {
	if (!out || size == 0) return 0;
	out[0] = '\0';
	int art = stockKind(kind);
	if (!entry || art < 0 || finding) return 0;
	finding = 1;
	int answered = Plugins_artPath(entry, art, out, size);
	finding = 0;
	if (!answered) {
		if (art == ART_THUMBNAIL || art == ART_FOLDER_BG) resolveArt(art, entry->path, out, size);
		else if (art == ART_LIST_BG) {
			// the game's folder, as the game list resolves it
			char folder[MAX_PATH];
			snprintf(folder, sizeof(folder), "%s", entry->path);
			char* slash = strrchr(folder, '/');
			if (slash) *slash = '\0';
			resolveArt(art, folder, out, size);
		}
		else out[0] = '\0';
	}
	return out[0] && exists(out);
}

// What entry's kind resolves to, asking plugins and the card once per selection.
static int resolve(const Entry* entry, int kind, char* path, size_t size) {
	int found;
	if (ArtNames_get(&names, entry->path, kind, path, size, &found)) return found;
	found = ArtLoader_find(entry, kind, path, size);
	ArtNames_set(&names, entry->path, kind, path, found);
	return found;
}

static int want(const char* path, int kind, int flags, const MosaicSurface** out) {
	void* image;
	int st = ArtCache_get(&cache, path, kind, flags, &image);
	dispatch();
	if (st == ARTC_READY) {
		*out = image;
		return MOSAIC_ART_READY;
	}
	return st == ARTC_FAILED ? MOSAIC_ART_MISSING : MOSAIC_ART_LOADING;
}

static int isSelected(const Entry* entry) {
	return state.top && state.top->entries->count > 0 && state.top->entries->items[state.top->selected] == entry;
}

// The entries either side of the selection, wrapping as up and down do.
static void prefetchAround(int kind) {
	Array* entries = state.top->entries;
	int count = entries->count;
	if (count < 2) return;
	int sel = state.top->selected;
	int sides[2] = { (sel + count - 1) % count, (sel + 1) % count };
	for (int i = 0; i < 2; i++) {
		if (i == 1 && sides[1] == sides[0]) break;
		char path[ART_CACHE_PATH];
		if (resolve(entries->items[sides[i]], kind, path, sizeof(path))) {
			ArtCache_get(&cache, path, kind, ARTC_PREFETCH, NULL);
		}
	}
}

int ArtLoader_get(const Entry* entry, int kind, const MosaicSurface** out) {
	if (out) *out = NULL;
	if (!started || !entry || !out || stockKind(kind) < 0) return MOSAIC_ART_MISSING;
	checkSelection();
	char path[ART_CACHE_PATH];
	if (!resolve(entry, kind, path, sizeof(path))) return MOSAIC_ART_MISSING;
	int result = want(path, kind, ARTC_NOTIFY, out);
	if (isSelected(entry) && !(prefetched & (1u << kind))) {
		prefetched |= 1u << kind;
		prefetchAround(kind);
	}
	return result;
}

int ArtLoader_load(const char* path, const MosaicSurface** out) {
	if (out) *out = NULL;
	if (!started || !path || !out || !path[0] || strlen(path) >= ART_CACHE_PATH || !exists((char*)path)) return MOSAIC_ART_MISSING;
	checkSelection();
	return want(path, ART_KIND_IMAGE, ARTC_NOTIFY, out);
}
