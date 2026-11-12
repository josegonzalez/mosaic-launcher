// A plugin that looks at how collections list, for the bundled
// collections-plus plugin. Once the lists are up it opens the root, then
// <sd>/Collections, then the collection its "collection" setting names
// (card-relative), logging each list's names in order, and what art_find
// answers for the thumbnail and box of the root's collections.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "collector"

static const MosaicHost* host;
static char collections[512];
static char collection[1024];
static int ticks = 0;

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
	snprintf(collections, sizeof(collections), "%s/Collections", host->sdcard_path);
	snprintf(collection, sizeof(collection), "%s/%s", host->sdcard_path, host->cfg_get("collection", ""));
	return 0;
}

static void logList(const char* what) {
	const MosaicList* list = host->current_list();
	int count = host->list_count(list);
	char names[1000] = "";
	for (int i = 0; i < count; i++) {
		size_t used = strlen(names);
		snprintf(names + used, sizeof(names) - used, "%s%s", i ? "," : "", host->entry_name(host->list_get(list, i)));
	}
	say("%s %s", what, names);
}

static void logArt(void) {
	const MosaicList* list = host->current_list();
	int count = host->list_count(list);
	size_t n = strlen(collections);
	for (int i = 0; i < count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		const char* path = host->entry_path(entry);
		if (strncmp(path, collections, n) != 0 || path[n] != '/') continue;
		char thumb[512], box[512];
		int has_thumb = host->art_find(entry, MOSAIC_ART_THUMBNAIL, thumb, sizeof(thumb));
		int has_box = host->art_find(entry, MOSAIC_ART_BOX, box, sizeof(box));
		say("art %s thumbnail %i box %i", host->entry_name(entry), has_thumb, has_box);
	}
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open root %i", host->open_path(host->sdcard_path, MOSAIC_ENTRY_DIR));
	if (ticks == 4) {
		logList("root");
		logArt();
		say("open folder %i", host->open_path(collections, MOSAIC_ENTRY_DIR));
	}
	if (ticks == 6) {
		logList("folder");
		say("open collection %i", host->open_path(collection, MOSAIC_ENTRY_DIR));
	}
	if (ticks == 8) {
		logList("collection");
		say("done");
	}
}

static MosaicPlugin plugin = {
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
