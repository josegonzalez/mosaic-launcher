// A plugin that reads back what row decorators earlier in load order drew,
// for row-badges. It opens <sd>/<folder> (from folder= in its config) and,
// for each row of it, logs how many dots it finds in the theme's accent
// colour and how many in its list text colour, each time that changes. A
// dot is a run of one of those colours as wide as row-badges' dots, 4 units
// of the screen's scale give or take a pixel, with the theme's background
// colour, row-badges' ring, on both sides, so a selection or art in the
// same colour doesn't count,
// along the two lines row-badges puts its dots on: through the middle of
// row->name from its end, when there's a name, and through the right half
// of the row 6 units of the screen's scale below its top, where they go
// without a name or room after it. With open=0 in its config, it opens
// nothing, such as when gridder opens the folder.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "dotter"

static const MosaicHost* host;
static char folder[512];
static int open_folder = 1;
static int ticks = 0;
static char last[32][160]; // the last line logged for each row, by index

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

// Dots of rgb along y from x to end: runs of it, opaque and within a pixel
// of width long, with ring (row-badges' background ring) on both sides.
static int runs(const MosaicSurface* s, int x, int end, int y, int rgb, int ring, int width) {
	int count = 0, run = 0, before = -1;
	for (; x < end; x++) {
		MosaicColor c;
		int v = host->read_pixel(s, x, y, &c) == 0 && c.a == 255 ? (c.r << 16 | c.g << 8 | c.b) : -1;
		if (v == rgb) {
			run++;
			continue;
		}
		if (run >= width - 1 && run <= width + 1 && before == ring && v == ring) count++;
		run = 0;
		before = v;
	}
	return count;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	if (!inFolder(entry) || row->index < 0 || row->index >= 32) return 0;
	const MosaicRect* r = &row->rect;
	int scale = host->screen_scale(), dot = 4 * scale, end = r->x + r->w;
	int accent = host->theme_color(MOSAIC_COLOR_ACCENT), text = host->theme_color(MOSAIC_COLOR_LIST_TEXT);
	int ring = host->theme_color(MOSAIC_COLOR_BACKGROUND);
	int corner_y = r->y + 6 * scale;
	int unplayed = runs(screen, r->x + r->w / 2, end, corner_y, accent, ring, dot);
	int tiers = runs(screen, r->x + r->w / 2, end, corner_y, text, ring, dot);
	if (row->name.w > 0) {
		int x = row->name.x + row->name.w, y = row->name.y + row->name.h / 2;
		unplayed += runs(screen, x, end, y, accent, ring, dot);
		tiers += runs(screen, x, end, y, text, ring, dot);
	}
	char line[160];
	snprintf(line, sizeof(line), "row %s unplayed %i tiers %i", host->entry_name(entry), unplayed, tiers);
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
