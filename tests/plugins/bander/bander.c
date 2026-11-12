// A plugin that decorates the game's art, logging what it's handed and
// reading the screen back to see its drawing land under the art and
// nowhere else. It adds a <sd>/Bander folder to the root holding Apple,
// Banana and Cherry and an empty Inner folder, and opens it. Its art is
// the card's: run.sh gives Apple a square thumbnail and Cherry a wide one,
// and Banana none. Each call fills below with magenta.
//
// From tick it selects Cherry, Banana and Cherry again, then opens Inner.
// It logs the first call for each game, and at the end how many calls each
// got, in Inner too. As the transition slot's owner (slots=transition in
// its plugin.txt), it reads the push into Inner's old frame, where the
// layers are drawn together: the middle of the band and the left of the
// art, and draws the push with no transition.
//
// With open=0 and folder=<name> in its config, it adds and opens nothing
// and logs the art of <sd>/<name> instead, such as gridder's folder. With
// fail=art, decorate_art returns an error the first time it's called.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "bander"
#define BAND 0xFF00FF

static const MosaicHost* host;
static char folder[512];
static char inner[600];
static const char* fail = "";
static int open_folder = 1;
static const char* names[] = { "Apple", "Banana", "Cherry" };
static int calls[3];       // decorate_art calls for each game
static int inner_calls = 0; // and while Inner is open
static int failed = 0;
static int ticks = 0;
static int step = 0;
static int step_at = 0;
static MosaicRect last_art, last_below; // Cherry's, for the push

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
	if (open_folder) host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Bander"));
}

static MosaicList* virtualDir(const char* path) {
	if (!open_folder) return NULL;
	if (!strcmp(path, inner)) return host->list_new();
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	for (int i = 0; i < 3; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	host->list_push(list, host->entry_new(inner, MOSAIC_ENTRY_DIR, "Inner"));
	return list;
}

// "RRGGBB" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[4][8];
	static int next = 0;
	char* text = out[next++ % 4];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 8, "%02X%02X%02X", c.r, c.g, c.b);
	return text;
}

static int game(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	size_t n = strlen(folder);
	if (!path || strncmp(path, folder, n) != 0 || path[n] != '/') return -1;
	for (int i = 0; i < 3; i++) {
		if (!strcmp(host->entry_name(entry), names[i])) return i;
	}
	return -1;
}

static int decorateArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* art, const MosaicRect* below) {
	if (!strcmp(fail, "art") && !failed) {
		failed = 1;
		say("art failing");
		return -1;
	}
	const char* path = host->current_path();
	if (path && !strcmp(path, inner)) inner_calls++;
	int i = game(entry);
	if (i < 0) return 0;
	int filled = host->fill_rect(screen, below, mosaic_color(BAND, 255));
	if (i == 2) {
		last_art = *art;
		last_below = *below;
	}
	if (calls[i]++) return 0;
	// the art itself, above below, stays as it is
	int ax = art->x + art->w / 2, ay = art->y + art->h / 2;
	const char* before = pixel(screen, ax, ay);
	int outside = host->fill_rect(screen, art, mosaic_color(0xFFFF00, 255));
	say("art %s rect %i,%i %ix%i below %i,%i %ix%i, band %i %s, clipped %i (fill %i), select %i, nested %i %i",
		names[i], art->x, art->y, art->w, art->h, below->x, below->y, below->w, below->h, filled,
		pixel(screen, below->x + below->w / 2, below->y + below->h / 2), !strcmp(before, pixel(screen, ax, ay)), outside,
		host->select_index(0), host->decorate_art(screen, 0, art), host->decorate_cell(screen, 0, below, NULL));
	return 0;
}

static int transitionBegin(MosaicTransition* t) {
	static int probed = 0;
	if (t->kind != MOSAIC_TRANSITION_PUSH || step != 4 || probed || last_below.w <= 0) return MOSAIC_STYLE_OFF;
	probed = 1;
	say("push band %s art %s", pixel(t->old_frame, last_below.x + last_below.w / 2, last_below.y + last_below.h / 2),
		pixel(t->old_frame, last_art.x + last_art.w / 8, last_art.y + last_art.h / 2));
	return MOSAIC_STYLE_OFF;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2 && open_folder) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks == 3) say("decorate art outside a layout %i", host->decorate_art(NULL, 0, &(MosaicRect){ 0, 0, 10, 10 }));
	host->request_redraw();
	if (!open_folder) {
		if (ticks == 60) say("done: Apple %i, Banana %i, Cherry %i", calls[0] > 0, calls[1] > 0, calls[2] > 0);
		return;
	}
	// each step waits for the last one's art, or long enough that it would have come
	int waited = ticks - step_at;
	int next = 0;
	switch (step) {
	case 0: next = calls[0] > 0 || waited > 60; break;             // Apple's, in the folder as it opens
	case 1: next = calls[2] > 0 || waited > 60; break;             // Cherry's, straight from Apple's
	case 2: next = waited > 30; break;                             // Banana has none
	case 3: next = (calls[2] > 1 && waited > 10) || waited > 60; break; // Cherry's again
	case 4: next = waited > 30; break;                             // Inner is empty
	}
	if (!next) return;
	step++;
	step_at = ticks;
	switch (step) {
	case 1: host->select_index(2); break;
	case 2: host->select_index(1); break;
	case 3: host->select_index(2); break;
	case 4: say("open inner %i", host->open_path(inner, MOSAIC_ENTRY_DIR)); break;
	case 5: say("done: Apple %i, Banana %i, Cherry %i, inner %i", calls[0] > 0, calls[1] > 0, calls[2] > 0, inner_calls); break;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_art)) return 1;
	snprintf(folder, sizeof(folder), "%s/%s", host->sdcard_path, host->cfg_get("folder", "Bander"));
	snprintf(inner, sizeof(inner), "%s/Inner", folder);
	fail = host->cfg_get("fail", "");
	open_folder = atoi(host->cfg_get("open", "1"));
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
	.transition_begin = transitionBegin,
	.decorate_art = decorateArt,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
