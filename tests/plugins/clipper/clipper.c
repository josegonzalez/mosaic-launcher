// A plugin that walks the selection through a folder for video-previews.
// It opens <sd>/<folder> (from folder= in its config) once the lists are
// up, then moves the selection down a row every step= milliseconds (2500
// by default), logging each game it selects, and logs that it's done a
// step after the last row.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "mosaic_plugin.h"

#define ID "clipper"

static const MosaicHost* host;
static char folder[512];
static unsigned long step_ms = 2500;
static int ticks = 0;
static int row = -1; // the row selected last, or -1 before the folder opens
static unsigned long since = 0;
static int finished = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void tick(unsigned long now) {
	if (finished || !host->current_list()) return;
	ticks++;
	if (ticks == 2) {
		say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
		row = 0;
		since = now;
		return;
	}
	if (row < 0 || now - since < step_ms) return;
	since = now;
	const MosaicList* list = host->current_list();
	if (row + 1 >= host->list_count(list)) {
		say("done");
		finished = 1;
		return;
	}
	row++;
	int moved = host->select_index(row);
	say("select %s %i", host->entry_name(host->list_get(list, row)), moved);
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder, sizeof(folder), "%s/%s", host->sdcard_path, host->cfg_get(ID, "folder", "Roms"));
	step_ms = strtoul(host->cfg_get(ID, "step", "2500"), NULL, 10);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
