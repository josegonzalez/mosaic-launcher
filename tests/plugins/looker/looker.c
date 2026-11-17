// A plugin that looks at the art a folder's entries have, for the bundled
// art-boxart plugin. Once the lists are up it opens <sd>/Boxart (or the
// full path its folder setting names) and logs
// the entries the list kept, then what art_find answers for each entry's
// thumbnail, box, 3D box, logo and screenshot, with paths below the folder,
// and, as a row decorator, which of its rows had their names hidden. With
// its index setting, a comma-separated list of names in the folder, it
// then waits for the card index to have the folder and logs what
// index_find answers for each, and whether is_filtered hides it, before
// "done".

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "looker"

static const MosaicHost* host;
static char folder[512];
static int ticks = 0;
static char hidden[1000] = ""; // the folder's rows decorated with name_hidden, below it
static char index_names[1000] = ""; // the index setting
static int index_logged = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[1200];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int init(const MosaicHost* h) {
	host = h;
	const char* named = host->cfg_get("folder", "");
	if (named[0]) snprintf(folder, sizeof(folder), "%s", named);
	else snprintf(folder, sizeof(folder), "%s/Boxart", host->sdcard_path);
	snprintf(index_names, sizeof(index_names), "%s", host->cfg_get("index", ""));
	index_logged = !index_names[0];
	if (!index_logged) host->index_count(NULL); // starts reading the card
	return 0;
}

static const char* below(const char* path) {
	size_t n = strlen(folder);
	return !strncmp(path, folder, n) && path[n] == '/' ? path + n + 1 : path;
}

static void logList(void) {
	const MosaicList* list = host->current_list();
	int count = host->list_count(list);
	char names[1000] = "";
	for (int i = 0; i < count; i++) {
		size_t used = strlen(names);
		snprintf(names + used, sizeof(names) - used, "%s%s", i ? "," : "", below(host->entry_path(host->list_get(list, i))));
	}
	say("entries %s", names);

	static const int kinds[] = { MOSAIC_ART_THUMBNAIL, MOSAIC_ART_BOX, MOSAIC_ART_BOX_3D, MOSAIC_ART_LOGO,
		MOSAIC_ART_SCREENSHOT };
	static const char* kind_names[] = { "thumbnail", "box", "box3d", "logo", "screenshot" };
	for (int i = 0; i < count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		for (int k = 0; k < 5; k++) {
			char out[512];
			int found = host->art_find(entry, kinds[k], out, sizeof(out));
			say("art %s %s %i \"%s\"", below(host->entry_path(entry)), kind_names[k], found, below(out));
		}
	}
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	(void)screen;
	const char* path = host->entry_path(entry);
	size_t n = strlen(folder);
	if (!MOSAIC_ROW_HAS(row, name_hidden) || !row->name_hidden || strncmp(path, folder, n) != 0 || path[n] != '/') return 0;
	char item[600];
	snprintf(item, sizeof(item), ",%s,", below(path));
	char list[1002];
	snprintf(list, sizeof(list), ",%s,", hidden);
	if (strstr(list, item)) return 0;
	size_t used = strlen(hidden);
	snprintf(hidden + used, sizeof(hidden) - used, "%s%s", used ? "," : "", below(path));
	return 0;
}

// What the index has for each name in the index setting, once it has the folder.
static void logIndex(void) {
	if (index_logged || host->index_find(folder, NULL) < 0) return;
	index_logged = 1;
	char names[1000];
	snprintf(names, sizeof(names), "%s", index_names);
	for (char* name = strtok(names, ","); name; name = strtok(NULL, ",")) {
		char path[1600];
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		MosaicIndexEntry e = { .size = sizeof(e) };
		int found = host->index_find(path, &e) >= 0;
		say("index %s %i \"%s\" %s added %i filtered %i", name, found, found ? e.name : "", found ? e.year : "", found ? e.added : 0,
			host->is_filtered(path));
	}
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	if (ticks == 4) {
		logList();
		say("hidden %s", hidden[0] ? hidden : "none");
	}
	if (ticks < 4 || index_logged > 1) return;
	logIndex();
	if (index_logged) {
		index_logged = 2;
		say("done");
	}
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
