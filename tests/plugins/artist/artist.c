// A plugin that supplies art and loads it through the art cache. It adds a
// <sd>/Artist folder of six games to the root, opens it, and answers
// art_path for them from the folder in its dir setting:
//
//   Alpha    thumbnail, and a logo whose file is missing
//   Bravo    box
//   Charlie  3D box
//   all      screenshot and backdrop
//
// It owns the layout slot only to log the art the launcher picks for the
// selected game (view->art), which the built-in list shows too. Its tick
// then steps through art_find, art_get and art_load, logging what each
// returns: a load and then a hit, the neighbours of the selection loaded
// ahead, and loads asked for before the selection moved being dropped.
// With mode=evict in its config it instead fills a small cache (the run
// sets MOSAIC_LAUNCHER_ART_CACHE_BYTES) and logs which image went.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "artist"
#define WAIT 30 // frames to let the worker catch up

static const MosaicHost* host;
static char folder[512];
static const char* dir = "";
static const char* mode = "";
static int ticks = 0;
static int step = 0;
static int waited = 0;
static int polls = 0;
static MosaicSurface* mine = NULL;

static const char* games[] = { "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot" };
#define GAMES (int)(sizeof(games) / sizeof(games[0]))

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Artist"));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	for (int i = 0; i < GAMES; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, games[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, games[i]));
	}
	return list;
}

// One of this folder's games, by name, or NULL.
static const char* gameOf(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	size_t len = strlen(folder);
	if (!path || strncmp(path, folder, len) != 0 || path[len] != '/') return NULL;
	return host->entry_name(entry);
}

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	const char* game = gameOf(entry);
	if (!game) return 0;
	const char* suffix = NULL;
	switch (kind) {
		case MOSAIC_ART_THUMBNAIL: if (!strcmp(game, "Alpha")) suffix = "thumb"; break;
		case MOSAIC_ART_BOX: if (!strcmp(game, "Bravo")) suffix = "box"; break;
		case MOSAIC_ART_BOX_3D: if (!strcmp(game, "Charlie")) suffix = "3d"; break;
		case MOSAIC_ART_LOGO: if (!strcmp(game, "Alpha")) suffix = "logo"; break;
		case MOSAIC_ART_SCREENSHOT: suffix = "shot"; break;
		case MOSAIC_ART_BACKDROP: suffix = "back"; break;
		default: break;
	}
	if (!suffix) return 0;
	snprintf(out, size, "%s/%s-%s.png", dir, game, suffix);
	return 1;
}

static const MosaicEntry* game(int i) {
	const MosaicList* list = host->current_list();
	return list ? host->list_get(list, i) : NULL;
}

static const char* shortPath(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void find(int i, int kind, const char* kind_name) {
	char out[512];
	strcpy(out, "unset");
	int found = host->art_find(game(i), kind, out, sizeof(out));
	say("find %s %s %i \"%s\"", games[i], kind_name, found, shortPath(out));
}

static int get(int i, int kind) {
	const MosaicSurface* art = NULL;
	return host->art_get(game(i), kind, &art);
}

// "RRGGBB" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[8];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(out, sizeof(out), "%02X%02X%02X", c.r, c.g, c.b);
	return out;
}

// Asks each frame until it's in; returns 1 once it is, logging how it went.
static int until(int i, int kind, const char* what) {
	const MosaicSurface* art = NULL;
	int state = host->art_get(game(i), kind, &art);
	if (polls++ == 0) say("%s %s first %i", what, games[i], state);
	if (state == MOSAIC_ART_LOADING && polls < 600) return 0;
	say("%s %s in %i", what, games[i], state);
	polls = 0;
	return 1;
}

static int waitFrames(void) {
	if (++waited < WAIT) return 0;
	waited = 0;
	return 1;
}

static void normal(void) {
	switch (step) {
		case 0: {
			find(0, MOSAIC_ART_THUMBNAIL, "thumbnail");
			find(1, MOSAIC_ART_BOX, "box");
			find(2, MOSAIC_ART_BOX_3D, "box3d");
			find(0, MOSAIC_ART_LOGO, "logo");
			find(3, MOSAIC_ART_SCREENSHOT, "screenshot");
			find(4, MOSAIC_ART_BACKDROP, "backdrop");
			find(3, MOSAIC_ART_BOX_3D, "box3d");
			find(0, 99, "unknown");
			const MosaicSurface* art = (const MosaicSurface*)1;
			int bad_entry = host->art_get(NULL, MOSAIC_ART_THUMBNAIL, &art);
			int bad_kind = host->art_get(game(0), 99, &art);
			say("refused get %i %i, cleared %i, missing %i, load %i %i", bad_entry, bad_kind, art == NULL,
				get(0, MOSAIC_ART_LOGO), host->art_load("", &art), host->art_load("/no/such.png", &art));
			step++;
			break;
		}
		case 1:
			// the selection, Alpha, and its neighbours either side
			if (!until(0, MOSAIC_ART_SCREENSHOT, "shot")) return;
			step++;
			break;
		case 2: {
			const MosaicSurface* art = NULL;
			const MosaicSurface* again = NULL;
			int state = host->art_get(game(0), MOSAIC_ART_SCREENSHOT, &art);
			host->art_get(game(0), MOSAIC_ART_SCREENSHOT, &again);
			int w = 0, h = 0;
			host->surface_size(art, &w, &h);
			if (!mine) mine = host->surface_new(ID, 8, 8);
			MosaicSurface* copy = host->surface_scale(ID, art, 8, 4, MOSAIC_FIT_STRETCH);
			say("hit %i same %i, %ix%i, pixel %s, blit %i, copy %i, free %i, fill %i, text %i", state, art == again, w, h,
				pixel(art, 0, 0), host->blit(mine, art, NULL, NULL), copy != NULL, host->surface_free(ID, (MosaicSurface*)art),
				host->fill_rect((MosaicSurface*)art, NULL, mosaic_color(0xFFFFFF, 255)),
				host->draw_text((MosaicSurface*)art, MOSAIC_FONT_SMALL, "x", 0, 0, mosaic_color(0xFFFFFF, 255), 0));
			say("still %s, copy free %i", pixel(art, 0, 0), host->surface_free(ID, copy));
			step++;
			break;
		}
		case 3:
			if (!waitFrames()) return;
			// loaded ahead, either side: Bravo, and Foxtrot round the end; not Delta
			say("ahead Bravo %i Foxtrot %i Delta %i", get(1, MOSAIC_ART_SCREENSHOT), get(5, MOSAIC_ART_SCREENSHOT),
				get(3, MOSAIC_ART_SCREENSHOT));
			step++;
			break;
		case 4:
			// asked for, then the selection moves before they're in
			say("before move Delta %i Echo %i Foxtrot %i", get(3, MOSAIC_ART_BACKDROP), get(4, MOSAIC_ART_BACKDROP),
				get(5, MOSAIC_ART_BACKDROP));
			say("move %i", host->select_index(2));
			say("after move Alpha %i", get(0, MOSAIC_ART_BACKDROP));
			step++;
			break;
		case 5:
			if (!waitFrames()) return;
			say("dropped Delta %i Echo %i Foxtrot %i, kept Alpha %i", get(3, MOSAIC_ART_BACKDROP), get(4, MOSAIC_ART_BACKDROP),
				get(5, MOSAIC_ART_BACKDROP), get(0, MOSAIC_ART_BACKDROP));
			host->select_index(1);
			step++;
			break;
		case 6:
			if (!waitFrames()) return;
			host->select_index(3);
			step++;
			break;
		case 7: {
			if (!waitFrames()) return;
			// an image by its path alone
			const MosaicSurface* art = NULL;
			int state = host->art_load(host->cfg_get("image", ""), &art);
			if (polls++ == 0) say("load first %i", state);
			if (state == MOSAIC_ART_LOADING && polls < 600) {
				waited = WAIT;
				return;
			}
			polls = 0;
			say("load in %i, pixel %s", state, pixel(art, 0, 0));
			say("done");
			step++;
			break;
		}
	}
}

// With a cache that holds two of these images: the least recently used goes.
static void evict(void) {
	switch (step) {
		case 0: if (until(2, MOSAIC_ART_SCREENSHOT, "fill")) step++; break;
		case 1: if (until(3, MOSAIC_ART_SCREENSHOT, "fill")) step++; break;
		case 2:
			say("touch Charlie %i", get(2, MOSAIC_ART_SCREENSHOT));
			step++;
			break;
		case 3: if (until(4, MOSAIC_ART_SCREENSHOT, "fill")) step++; break;
		case 4:
			if (!waitFrames()) return;
			// in order, so asking again for one that went doesn't push out the others first
			say("kept Charlie %i Echo %i, evicted Delta %i", get(2, MOSAIC_ART_SCREENSHOT), get(4, MOSAIC_ART_SCREENSHOT),
				get(3, MOSAIC_ART_SCREENSHOT));
			say("done");
			step++;
			break;
	}
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	const char* path = host->current_path();
	if (ticks < 3 || !path || strcmp(path, folder) != 0) return;
	if (!strcmp(mode, "evict")) evict();
	else normal();
	host->request_redraw();
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	host->fill_rect(screen, &view->area, mosaic_color(0x000000, 255));
	static int last_depth = -1, last_selected = -1;
	if (view->depth != last_depth || view->selected != last_selected) {
		last_depth = view->depth;
		last_selected = view->selected;
		if (view->depth == 2) {
			const MosaicEntry* entry = host->list_get(view->list, view->selected);
			say("list art %s \"%s\"", host->entry_name(entry), shortPath(view->art));
		}
	}
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, art_get) || !MOSAIC_HOST_HAS(host, select_index)) return 1;
	snprintf(folder, sizeof(folder), "%s/Artist", host->sdcard_path);
	dir = host->cfg_get("dir", "");
	mode = host->cfg_get("mode", "");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.art_path = artPath,
	.layout_render = layoutRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
