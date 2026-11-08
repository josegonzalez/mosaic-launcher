// A plugin that reads the bundled layout-fancy's art panel back. It adds a
// <sd>/Roms/Glancer (GLANCE) folder of three games to the root, opens it,
// and selects each game in turn, about a second and a half each. As a row
// decorator, which a layout runs on each row it draws, it reads the screen
// down a column through the panel (three quarters of the way across) on
// the selected game's row, and logs the stretches of pure red, green and
// blue there, as "panel <game> runs <colour>:<from>-<to> ...", each time
// they change, as art comes in. It logs "done" after the last game.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "glancer"
#define GAMES 3
#define MAX_RUNS 4

static const MosaicHost* host;
static char folder[512];
static const char* names[GAMES] = { "Alpha", "Beta", "Gamma" };
static char last[GAMES][256]; // the last line logged for each game
static int step = 0;          // 0 before the folder opens, then 1 + the game selected, then GAMES + 1 when done
static long step_at = 0;
static int ticks = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static long nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Glancer"));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	for (int i = 0; i < GAMES; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
}

static unsigned int pixel(const MosaicSurface* s, int x, int y) {
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return 0x1000000;
	return (unsigned int)c.r << 16 | (unsigned int)c.g << 8 | c.b;
}

static int pure(unsigned int rgb) {
	return rgb == 0xFF0000 || rgb == 0x00FF00 || rgb == 0x0000FF;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	if (!row->selected || strncmp(host->entry_path(entry), folder, strlen(folder)) != 0) return 0;
	const char* name = host->entry_name(entry);
	int game = -1;
	for (int i = 0; i < GAMES; i++) {
		if (!strcmp(name, names[i])) game = i;
	}
	if (game < 0) return 0;

	int w = 0, h = 0;
	if (host->surface_size(screen, &w, &h) != 0) return 0;
	int x = w * 3 / 4;
	char line[256];
	int n = snprintf(line, sizeof(line), "panel %s runs", name);
	int runs = 0;
	for (int y = 0; y < h && runs < MAX_RUNS;) {
		unsigned int rgb = pixel(screen, x, y);
		int from = y;
		while (y < h && pixel(screen, x, y) == rgb) y++;
		// a stretch of a few pixels is an edge, not art
		if (!pure(rgb) || y - from < 8) continue;
		n += snprintf(line + n, sizeof(line) - (size_t)n, " %06X:%i-%i", rgb, from, y);
		runs++;
	}
	if (!runs) snprintf(line + n, sizeof(line) - (size_t)n, " none");
	if (strcmp(line, last[game]) != 0) {
		snprintf(last[game], sizeof(last[game]), "%s", line);
		say("%s", line);
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	host->request_redraw();
	if (step > GAMES) return;
	ticks++;
	if (step == 0) {
		if (ticks < 2) return;
		say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
		step = 1;
		step_at = nowMs();
		return;
	}
	if (nowMs() - step_at < 1500) return;
	step_at = nowMs();
	if (step < GAMES) host->select_index(step);
	else say("done");
	step++;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell)) return 1;
	snprintf(folder, sizeof(folder), "%s/Roms/Glancer (GLANCE)", host->sdcard_path);
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
