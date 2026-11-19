// A plugin that reads which entry a layout draws selected, for
// layout-grid's Wrap setting. It adds a <sd>/Wrapper folder of seven games,
// g0 to g6, to the root and opens it, and as a row decorator logs the name
// of the selected one each time it changes.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "wrapper"
#define GAMES 7

static const MosaicHost* host;
static char folder[512];
static int ticks = 0;
static char last[16];

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Wrapper"));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	for (int i = 0; i < GAMES; i++) {
		char file[600], name[8];
		snprintf(name, sizeof(name), "g%i", i);
		snprintf(file, sizeof(file), "%s/%s.gb", folder, name);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, name));
	}
	return list;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	(void)screen;
	const char* name = host->entry_name(entry);
	if (!row->selected || strncmp(host->entry_path(entry), folder, strlen(folder)) != 0 || !strcmp(name, last)) return 0;
	snprintf(last, sizeof(last), "%s", name);
	char message[64];
	snprintf(message, sizeof(message), "selected %s", name);
	host->log(ID, message);
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) {
		char message[64];
		snprintf(message, sizeof(message), "open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
		host->log(ID, message);
	}
	if (ticks < 600) host->request_redraw();
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell)) return 1;
	snprintf(folder, sizeof(folder), "%s/Wrapper", host->sdcard_path);
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
