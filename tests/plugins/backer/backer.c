// A plugin that owns the background slot, and the transition slot to read
// the frames back. It adds a <sd>/Backer folder holding an Empty folder to
// the root, and from tick opens Backer and then Empty, so the launcher
// pushes twice. Its art_path answers Empty's folder background with the
// file its bg setting names, so stock's background under it changes.
//
// Its background_render fills the right half of the screen it's lent,
// green with an entry selected and yellow in an empty folder, leaving
// stock's background on the left, and logs the entry and what stock's
// background was under it. Each push logs the old and new frames' middle
// right and left pixels, and turns the transition off. With fail=empty in
// its config, background_render fails in the empty folder.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "backer"

static const MosaicHost* host;
static char folder[512];
static char empty[600];
static const char* bg = "";
static const char* fail = "";
static int ticks = 0;
static int renders = 0;
static int checked = 0;
static char last[600] = "unset";
static char under[16] = "";

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[800];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

// "RRGGBBAA" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[4][12];
	static int at = 0;
	char* text = out[at++ % 4];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 12, "%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
	return text;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Backer"));
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, folder)) {
		MosaicList* list = host->list_new();
		host->list_push(list, host->entry_new(empty, MOSAIC_ENTRY_DIR, "Empty"));
		return list;
	}
	if (!strcmp(path, empty)) return host->list_new();
	return NULL;
}

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	if (kind != MOSAIC_ART_FOLDER_BG || strcmp(host->entry_path(entry), empty) != 0) return 0;
	snprintf(out, size, "%s", bg);
	return 1;
}

static int backgroundRender(MosaicSurface* screen, const MosaicEntry* selected) {
	renders++;
	int w = 0, h = 0;
	host->surface_size(screen, &w, &h);
	const char* name = selected ? host->entry_name(selected) : "(none)";
	if (!checked) {
		checked = 1;
		say("render size %ix%i of %ix%i, select %i, free %i", w, h, host->screen_width(), host->screen_height(),
			host->select_index(0), host->surface_free(ID, screen));
	}
	if (!selected && !strcmp(fail, "empty")) {
		say("render failing");
		return -1;
	}
	// what stock shows on the left, once per entry and stock background
	const char* left = pixel(screen, 1, h / 2);
	if (strcmp(name, last) != 0 || strcmp(left, under) != 0) {
		snprintf(last, sizeof(last), "%s", name);
		snprintf(under, sizeof(under), "%s", left);
		say("render %s under %s", name, left);
	}
	MosaicRect right = { w / 2, 0, w - w / 2, h };
	return host->fill_rect(screen, &right, mosaic_color(selected ? 0x00FF00 : 0xFFFF00, 255));
}

static int transitionBegin(MosaicTransition* t) {
	int w = 0, h = 0;
	host->surface_size(t->new_frame, &w, &h);
	const char* to = host->current_path();
	say("push to %s old %s new %s left %s", to && !strcmp(to, empty) ? "empty" : to && !strcmp(to, folder) ? "backer" : "other",
		pixel(t->old_frame, w - 2, h / 2), pixel(t->new_frame, w - 2, h / 2), pixel(t->new_frame, 1, h / 2));
	return MOSAIC_STYLE_OFF;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	// long enough for Empty's background to load under it
	if (ticks == 40) say("open empty %i", host->open_path(empty, MOSAIC_ENTRY_DIR));
	if (ticks == 80) say("done at %s, renders %i", host->current_path(), renders);
	if (ticks < 80) host->request_redraw();
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, plugin_art)) return 1;
	snprintf(folder, sizeof(folder), "%s/Backer", host->sdcard_path);
	snprintf(empty, sizeof(empty), "%s/Empty", folder);
	bg = host->cfg_get(ID, "bg", "");
	fail = host->cfg_get(ID, "fail", "");
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
	.background_render = backgroundRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
