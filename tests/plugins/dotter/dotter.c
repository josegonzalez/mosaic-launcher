// A plugin that reads back what row decorators earlier in load order drew,
// for row-badges. It opens <sd>/<folder> (from folder= in its config) and,
// once for each row of it, logs how many dots it finds in the theme's
// accent colour and how many in its list text colour, counting runs of
// those colours along one line: through the middle of row->name, from its
// end, or with no name, as under gridder, through the right half of the
// cell where row-badges puts its dots, 6 units of the screen's scale below
// the top. With open=0 in its config, it opens nothing, such as when
// gridder opens the folder.

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
static unsigned int seen = 0; // rows logged, by index

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

// Runs of rgb, opaque, along y from x to end.
static int runs(const MosaicSurface* s, int x, int end, int y, int rgb) {
	int count = 0, in = 0;
	for (; x < end; x++) {
		MosaicColor c;
		int match = host->read_pixel(s, x, y, &c) == 0 && c.a == 255 && (c.r << 16 | c.g << 8 | c.b) == rgb;
		if (match && !in) count++;
		in = match;
	}
	return count;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	if (!inFolder(entry) || row->index < 0 || row->index >= 32 || (seen & (1u << row->index))) return 0;
	seen |= 1u << row->index;
	const MosaicRect* r = &row->rect;
	int x, y;
	if (row->name.w > 0) {
		x = row->name.x + row->name.w;
		y = row->name.y + row->name.h / 2;
	} else {
		x = r->x + r->w / 2;
		y = r->y + 6 * host->screen_scale();
	}
	say("row %s unplayed %i tiers %i", host->entry_name(entry),
		runs(screen, x, r->x + r->w, y, host->theme_color(MOSAIC_COLOR_ACCENT)),
		runs(screen, x, r->x + r->w, y, host->theme_color(MOSAIC_COLOR_LIST_TEXT)));
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
	snprintf(folder, sizeof(folder), "%s/%s", host->sdcard_path, host->cfg_get(ID, "folder", "Roms"));
	open_folder = atoi(host->cfg_get(ID, "open", "1"));
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
