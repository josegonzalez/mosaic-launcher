// Title disambiguation: when two entries in a list have the same name, each
// gets what tells them apart added to it, and only then. Games from
// different systems, as in Recently Played, collections and Favorites, get
// the system's tag, eg. "Tetris (GB)" and "Tetris (GBC)". Games from one
// system get the tags or extension the name leaves out, eg. "Tetris (USA)"
// and "Tetris (Europe)", or "Tetris.gb" and "Tetris.gbc", and the folder
// they're in when nothing else differs. See titles.h for how it picks.
//
// Stock already marks neighbours with the same name by showing their file
// name dimmed past the end of the name; renamed entries no longer match, so
// it leaves them alone. Turning the same_system setting off renames only
// groups that mix systems and leaves the rest to stock.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mosaic_plugin.h"
#include "titles.h"

#define ID "title-disambiguation"

static const MosaicHost* host;
static int same_system = 1;
static char roms[512];

static void listReady(const MosaicList* list, const char* dir_path) {
	(void)dir_path;
	int count = host->list_count(list);
	if (count < 2) return;
	const char** names = malloc((size_t)count * sizeof(char*));
	const char** paths = malloc((size_t)count * sizeof(char*));
	char** labels = malloc((size_t)count * sizeof(char*));
	if (names && paths && labels) {
		for (int i = 0; i < count; i++) {
			const MosaicEntry* entry = host->list_get(list, i);
			names[i] = host->entry_name(entry);
			paths[i] = host->entry_path(entry);
		}
		if (Titles_list(roms, same_system, count, names, paths, labels) > 0) {
			for (int i = 0; i < count; i++) {
				if (!labels[i]) continue;
				host->entry_set_name(host->list_get(list, i), labels[i]);
				free(labels[i]);
			}
		}
	}
	free(names);
	free(paths);
	free(labels);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, entry_set_name)) {
		host->log(ID, "this launcher can't rename entries");
		return 1;
	}
	same_system = strcmp(host->cfg_get("same_system", "on"), "off") != 0;
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.list_ready = listReady,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
