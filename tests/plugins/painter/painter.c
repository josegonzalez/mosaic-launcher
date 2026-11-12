// A plugin that generates art with art_surface. It adds a <sd>/Painter
// folder to the root, opens it from tick, and logs what it's asked for and
// what art_get and art_find give back:
//
//   Blank  no art file: a green thumbnail and a magenta box, generated
//   Next   a folder, opened last so the push's old frame shows the list
//   Real   a red thumbnail file (art_path, from its dir setting), which wins
//   Shy    no art file, and art_surface makes none
//
// It owns the transition slot only to look for Blank's generated
// thumbnail beside the built-in list in the push's old frame, and turns
// the transition off. With mode=again in its config, its second
// art_surface returns the surface it already handed over, an error.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "painter"

static const MosaicHost* host;
static char folder[512];
static char next[600];
static const char* dir = "";
static const char* mode = "";
static int ticks = 0;
static int step = 0;
static int polls = 0;
static int asks = 0;
static MosaicSurface* handed = NULL; // the last surface handed over

static const char* games[] = { "Blank", "Real", "Shy" };
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

// "RRGGBB" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[4][8];
	static int at = 0;
	char* text = out[at++ % 4];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 8, "%02X%02X%02X", c.r, c.g, c.b);
	return text;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Painter"));
}

static MosaicList* virtualDir(const char* path) {
	MosaicList* list = NULL;
	if (!strcmp(path, folder)) {
		list = host->list_new();
		for (int i = 0; i < GAMES; i++) {
			char file[600];
			snprintf(file, sizeof(file), "%s/%s.gb", folder, games[i]);
			host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, games[i]));
		}
		host->list_push(list, host->entry_new(next, MOSAIC_ENTRY_DIR, "Next"));
	}
	else if (!strcmp(path, next)) {
		list = host->list_new();
		char file[700];
		snprintf(file, sizeof(file), "%s/Game.gb", next);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, "Game"));
	}
	return list;
}

// One of this folder's games, by name, or NULL.
static const char* gameOf(const MosaicEntry* entry) {
	const char* path = entry ? host->entry_path(entry) : NULL;
	size_t len = strlen(folder);
	if (!path || strncmp(path, folder, len) != 0 || path[len] != '/') return NULL;
	return host->entry_name(entry);
}

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	const char* game = gameOf(entry);
	if (!game || kind != MOSAIC_ART_THUMBNAIL || strcmp(game, "Real") != 0) return 0;
	snprintf(out, size, "%s/Real-thumb.png", dir);
	return 1;
}

static const MosaicEntry* game(int i) {
	const MosaicList* list = host->current_list();
	if (!list) return NULL;
	for (int j = 0; j < host->list_count(list); j++) {
		const MosaicEntry* entry = host->list_get(list, j);
		if (!strcmp(host->entry_name(entry), games[i])) return entry;
	}
	return NULL;
}

static MosaicSurface* artSurface(const MosaicEntry* entry, int kind) {
	// only this folder's games: the root's entries are asked about too
	const char* name = gameOf(entry);
	if (!name) return NULL;
	say("asked %s %i", name, kind);
	asks++;
	if (asks == 1) {
		// nothing more is generated from in here, and it only draws
		const MosaicSurface* art = NULL;
		say("inside get %i select %i", host->art_get(game(2), MOSAIC_ART_BOX, &art), host->select_index(0));
	}
	if (!strcmp(name, "Shy")) return NULL;
	if (!strcmp(mode, "again") && handed) return handed;
	unsigned int rgb;
	if (kind == MOSAIC_ART_THUMBNAIL) rgb = 0x00FF00;
	else if (kind == MOSAIC_ART_BOX) rgb = 0xFF00FF;
	else return NULL;
	MosaicSurface* s = host->surface_new(ID, 64, 64);
	host->fill_rect(s, NULL, mosaic_color(rgb, 255));
	handed = s;
	return s;
}

static int get(int i, int kind, const MosaicSurface** art) {
	*art = NULL;
	return host->art_get(game(i), kind, art);
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	const char* path = host->current_path();
	if (ticks < 4 || !path) return;
	host->request_redraw();
	if (strcmp(path, folder) != 0) {
		if (step == 4) {
			say("done");
			step++;
		}
		return;
	}
	switch (step) {
		case 0: {
			const MosaicSurface* art;
			const MosaicSurface* again;
			int box = get(0, MOSAIC_ART_BOX, &art);
			int box_again = get(0, MOSAIC_ART_BOX, &again);
			int w = 0, h = 0;
			host->surface_size(art, &w, &h);
			say("blank box %i %i same %i, %ix%i %s", box, box_again, art == again && art, w, h, pixel(art, 0, 0));
			say("blank box read-only: fill %i, free %i", host->fill_rect((MosaicSurface*)art, NULL, mosaic_color(0xFFFFFF, 255)),
				host->surface_free(ID, (MosaicSurface*)art));
			const MosaicSurface* thumb;
			int got = get(0, MOSAIC_ART_THUMBNAIL, &thumb);
			say("blank thumbnail %i %s", got, pixel(thumb, 0, 0));
			char out[512];
			strcpy(out, "unset");
			say("blank find %i \"%s\"", host->art_find(game(0), MOSAIC_ART_THUMBNAIL, out, sizeof(out)), out);
			const MosaicSurface* shy;
			int first = get(2, MOSAIC_ART_BOX, &shy);
			int second = get(2, MOSAIC_ART_BOX, &shy);
			say("shy box %i %i", first, second);
			// what it handed over isn't on its budget any more
			MosaicSurface* big = host->surface_new(ID, 4096, 2048);
			say("budget %i", big != NULL);
			if (big) host->surface_free(ID, big);
			step++;
			break;
		}
		case 1: {
			// a real file wins, and art_surface is never asked for it
			const MosaicSurface* art;
			int state = get(1, MOSAIC_ART_THUMBNAIL, &art);
			if (state == MOSAIC_ART_LOADING && polls++ < 600) return;
			say("real thumbnail %i %s", state, pixel(art, 0, 0));
			step++;
			break;
		}
		case 2:
			say("asks %i", asks);
			step++;
			break;
		case 3:
			say("open next %i", host->open_path(next, MOSAIC_ENTRY_DIR));
			step++;
			break;
	}
}

// Counts green pixels across the middle of the old frame, where the
// built-in list's thumbnail is.
static int transitionBegin(MosaicTransition* t) {
	if (t->kind != MOSAIC_TRANSITION_PUSH) return MOSAIC_STYLE_OFF;
	int w = 0, h = 0;
	host->surface_size(t->old_frame, &w, &h);
	int green = 0;
	for (int x = w / 2; x < w; x++) {
		MosaicColor c;
		if (host->read_pixel(t->old_frame, x, h / 2, &c) == 0 && c.r < 16 && c.g > 240 && c.b < 16) green++;
	}
	const char* to = host->current_path();
	say("push to %s old thumbnail green %i", to && !strcmp(to, next) ? "next" : "other", green > 0);
	return MOSAIC_STYLE_OFF;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, plugin_art)) return 1;
	snprintf(folder, sizeof(folder), "%s/Painter", host->sdcard_path);
	snprintf(next, sizeof(next), "%s/Next", folder);
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
	.transition_begin = transitionBegin,
	.art_surface = artSurface,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
