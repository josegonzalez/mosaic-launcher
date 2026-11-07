// A plugin that uses the quick menu, shared button, choice list, recent
// rewrite and root flag host calls, logging what each returned.
//
// It binds two actions to Y, so the game list's Y opens a menu; adds a
// tile to each quick menu row and hides Games; owns the root, taking other
// plugins' root entries unless the "keep" setting is 0; and once the
// launcher is up, points the first recent at the "moved" setting's path,
// clears recents and reloads, which rebuilds the quick menu without its
// Recents tile, then opens a choice list.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "menus"

static const MosaicHost* host;
static int ticks = 0;
static int light = 1;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static const char* shown(const char* s) {
	return s ? s : "(null)";
}

static int always(void* data) { (void)data; return 1; }
static void runOne(void* data) { (void)data; say("one ran"); }
static void runTwo(void* data) { (void)data; say("two ran"); }
static void runTile(void* data) { (void)data; say("tile ran"); }
static int lightOn(void* data) { (void)data; return light; }
static void runLight(void* data) { (void)data; light = !light; }

static void picked(int index, void* data) {
	(void)data;
	say("picked %i", index);
}

static MosaicList* rootReplace(void) {
	char path[512];
	snprintf(path, sizeof(path), "%s/Menus", host->sdcard_path);
	MosaicList* root = host->list_new();
	host->list_push(root, host->entry_new(path, MOSAIC_ENTRY_DIR, "Menus Root"));
	return root;
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	if (ticks != 1) return;

	const MosaicList* list = host->current_list();
	char names[512] = "";
	for (int i = 0; list && i < host->list_count(list); i++) {
		size_t len = strlen(names);
		snprintf(names + len, sizeof(names) - len, "%s%s", i ? "," : "", host->entry_name(host->list_get(list, i)));
	}
	say("root %s", names);

	const char* moved = host->cfg_get(ID, "moved", "");
	int set = host->recent_set(0, moved);
	say("recent_set %i, first %s", set, shown(host->recent_path(0)));
	say("recent_set bad index %i, off the card %i", host->recent_set(9, moved), host->recent_set(0, "/elsewhere/Game.gb"));
	host->recents_clear();
	host->reload_lists();

	const char* choices[] = { "First", "Second" };
	say("choose none %i", host->choose("Pick", choices, 0, 0, picked, NULL));
	say("choose %i", host->choose("Pick", choices, 2, 1, picked, NULL));
}

static int init(const MosaicHost* h);

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.root_replace = rootReplace,
};

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, quick_tile) || !MOSAIC_HOST_HAS(host, hide_quick_tile)
			|| !MOSAIC_HOST_HAS(host, choose) || !MOSAIC_HOST_HAS(host, recent_set)) {
		host->log(ID, "the launcher is missing a host call");
		return 1;
	}
	plugin.root_with_entries = atoi(host->cfg_get(ID, "keep", "1"));

	host->register_action("menus.one", "ONE", always, runOne, NULL);
	host->register_action("menus.two", "TWO", always, runTwo, NULL);
	int one = host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "menus.one");
	int two = host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "menus.two");
	say("bound %i %i", one, two);

	host->register_action("menus.tile", "Menus", always, runTile, NULL);
	host->register_action("menus.light", "Light", always, runLight, NULL);
	say("tiles %i %i, unknown %i, bad row %i",
		host->quick_tile("menus.tile", MOSAIC_QUICK_TOP, NULL, NULL, NULL),
		host->quick_tile("menus.light", MOSAIC_QUICK_BOTTOM, NULL, NULL, lightOn),
		host->quick_tile("menus.missing", MOSAIC_QUICK_TOP, NULL, NULL, NULL),
		host->quick_tile("menus.tile", 9, NULL, NULL, NULL));
	say("hide %i, empty %i", host->hide_quick_tile("Games", 1), host->hide_quick_tile("", 1));
	return 0;
}

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
