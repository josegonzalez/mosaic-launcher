// A plugin that asks for a system's art, for 989-theme-packs: once the
// root list is up, it logs what art_find gives for the system folder in
// its "system" setting (a folder in Roms), as its thumbnail and folder
// background, and for the game in its "game" setting inside it, as its
// list background, then says it's done.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "packer"

static const MosaicHost* host;
static int done;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[1200];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void find(MosaicEntry* entry, int kind, const char* name) {
	char out[1024] = "";
	int found = host->art_find(entry, kind, out, sizeof(out));
	say("%s %i %s", name, found, found ? out : "-");
}

static void tick(unsigned long now) {
	(void)now;
	if (done) return;
	const MosaicList* list = host->current_list();
	if (!list || host->list_count(list) == 0) return;
	done = 1;
	char folder[1024], game[1100];
	snprintf(folder, sizeof(folder), "%s/Roms/%s", host->sdcard_path, host->cfg_get("system", ""));
	snprintf(game, sizeof(game), "%s/%s", folder, host->cfg_get("game", ""));
	MosaicEntry* system = host->entry_new(folder, MOSAIC_ENTRY_DIR, "System");
	MosaicEntry* rom = host->entry_new(game, MOSAIC_ENTRY_ROM, "Game");
	if (system && rom) {
		find(system, MOSAIC_ART_THUMBNAIL, "icon");
		find(system, MOSAIC_ART_FOLDER_BG, "folder");
		find(rom, MOSAIC_ART_LIST_BG, "list");
	}
	if (system) host->entry_free(system);
	if (rom) host->entry_free(rom);
	say("done");
}

static int init(const MosaicHost* h) {
	host = h;
	return MOSAIC_HOST_HAS(host, entry_free) && MOSAIC_HOST_HAS(host, art_get) ? 0 : 1;
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
