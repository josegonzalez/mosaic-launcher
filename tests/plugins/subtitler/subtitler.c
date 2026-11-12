// A plugin that reads back the captions playtime-captions, earlier in load
// order, drew on rows. It opens <sd>/<folder> (from folder= in its config;
// open=0 opens nothing) and, for each row of it, finds the pixels in
// playtime-captions' colour, the theme's hint colour 40% toward its
// background, and logs, each time it changes, "none", or whether they end
// at the row's right (within 4 units of the screen's scale and a few
// pixels for the last glyph's edge), start after the row's name, and are
// about as wide as the text its config gives for the row's name (eg.
// Tetris=2h 15m · 3 days ago) in the small font.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "subtitler"

static const MosaicHost* host;
static char folder[512];
static int open_folder = 1;
static int ticks = 0;
static int muted = 0;
static char last[32][300];

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int inFolder(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	size_t n = strlen(folder);
	return path && !strncmp(path, folder, n) && path[n] == '/';
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	if (!inFolder(entry) || row->index < 0 || row->index >= 32) return 0;
	const MosaicRect* r = &row->rect;
	int left = -1, right = -1;
	for (int y = r->y; y < r->y + r->h; y++) {
		for (int x = r->x; x < r->x + r->w; x++) {
			MosaicColor c;
			if (host->read_pixel(screen, x, y, &c) != 0 || c.a != 255) continue;
			if ((c.r << 16 | c.g << 8 | c.b) != muted) continue;
			if (left < 0 || x < left) left = x;
			if (x > right) right = x;
		}
	}
	const char* name = host->entry_name(entry);
	char line[300];
	if (left < 0) {
		snprintf(line, sizeof(line), "row %s caption none", name);
	} else {
		int scale = host->screen_scale();
		int edge = r->x + r->w - 4 * scale - 1 - right;
		const char* text = host->cfg_get(name, "");
		int want = text[0] ? host->text_width(MOSAIC_FONT_SMALL, text) : 0;
		int width = right - left + 1;
		int name_end = row->name.w > 0 ? row->name.x + row->name.w : r->x;
		snprintf(line, sizeof(line), "row %s caption at right %i, clear of name %i, as wide as \"%s\" %i", name,
			edge >= 0 && edge <= 4, left >= name_end, text, want > 0 && width <= want && width >= want - 6);
	}
	if (strcmp(line, last[row->index]) != 0) {
		snprintf(last[row->index], sizeof(last[row->index]), "%s", line);
		say("%s", line);
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2 && open_folder) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks < 30) host->request_redraw();
	if (ticks == 30) say("done");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell)) return 1;
	snprintf(folder, sizeof(folder), "%s/%s", host->sdcard_path, host->cfg_get("folder", "Roms"));
	open_folder = atoi(host->cfg_get("open", "1"));
	int hint = host->theme_color(MOSAIC_COLOR_HINT), background = host->theme_color(MOSAIC_COLOR_BACKGROUND);
	for (int shift = 16; shift >= 0; shift -= 8) {
		int a = hint >> shift & 0xFF, b = background >> shift & 0xFF;
		muted |= (a + (b - a) * 40 / 100) << shift;
	}
	return 0;
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.decorate_row = decorateRow,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
