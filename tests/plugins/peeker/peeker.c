// A plugin that reads a layout's cells back from the screen, for the
// bundled layouts. It adds a <sd>/Peeker folder of three games to the
// root and opens it, and as a row decorator, which a layout runs on each
// cell it draws, it reads each cell in that folder: the colour at the
// middle of the art (the cell above its name) and just inside its top
// edge, whether the theme's accent colour is there, and whether anything
// else is drawn across the art's middle, such as a letter. It logs a cell
// each time what it reads changes, as art comes in.
//
// With dirs=on in its config, its three entries are folders,
// <sd>/Peeker/<name>, and each cell's line ends with whether its name was
// hidden.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "peeker"

static const MosaicHost* host;
static char folder[512];
static int ticks = 0;
static int dirs = 0;
static char last[3][160]; // the last line logged for each game

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static const char* names[] = { "Apple", "banana", "Cherry" };

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Peeker"));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	for (int i = 0; i < 3; i++) {
		char file[600];
		snprintf(file, sizeof(file), dirs ? "%s/%s" : "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, dirs ? MOSAIC_ENTRY_DIR : MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
}

static unsigned int pixel(const MosaicSurface* s, int x, int y) {
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return 0x1000000;
	return (unsigned int)c.r << 16 | (unsigned int)c.g << 8 | c.b;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	const char* name = host->entry_name(entry);
	int game = -1;
	for (int i = 0; i < 3; i++) {
		if (!strcmp(name, names[i])) game = i;
	}
	if (game < 0 || strncmp(host->entry_path(entry), folder, strlen(folder)) != 0) return 0;

	const MosaicRect* r = &row->rect;
	int art_h = row->name.w > 0 ? row->name.y - r->y : r->h;
	int mid_x = r->x + r->w / 2, mid_y = r->y + art_h / 2;
	unsigned int middle = pixel(screen, mid_x, mid_y);
	unsigned int top = pixel(screen, mid_x, r->y + 1);
	int ink = 0;
	for (int x = r->x + r->w / 4; x < r->x + r->w * 3 / 4; x++) {
		if (pixel(screen, x, mid_y) != top) ink = 1;
	}
	int accent = host->theme_color(MOSAIC_COLOR_ACCENT);
	char line[160];
	int n = snprintf(line, sizeof(line), "cell %s middle %06X top %06X, accent %i, ink %i, named %i", name, middle,
		top, accent >= 0 && top == (unsigned int)accent, ink, row->name.w > 0);
	if (dirs && n > 0 && (size_t)n < sizeof(line)) {
		snprintf(line + n, sizeof(line) - (size_t)n, ", hidden %i",
			MOSAIC_ROW_HAS(row, name_hidden) ? row->name_hidden : -1);
	}
	if (strcmp(line, last[game]) != 0) {
		snprintf(last[game], sizeof(last[game]), "%s", line);
		say("%s", line);
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks < 60) host->request_redraw();
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell)) return 1;
	snprintf(folder, sizeof(folder), "%s/Peeker", host->sdcard_path);
	dirs = !strcmp(host->cfg_get(ID, "dirs", "off"), "on");
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
	.decorate_row = decorateRow,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
