// Clear recents: empties Recently Played, after asking first.
//
// A Clear recents tile on the quick menu (the "tile" setting, on by
// default) shows while there are recents. In Recently Played, Y (the
// "button" setting picks another) removes the selected game, or clears
// them all; with both available the button opens a menu of the two.
// Elsewhere the button is left to other plugins.
//
// Clearing asks first, then lists the folders again, which closes Recently
// Played if it's open and drops the quick menu's Recents tile and this one.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "clear-recents"
#define MAX_PATH_LEN 512

static const MosaicHost* host;
static char recents_path[MAX_PATH_LEN]; // <sd>/Recently Played

static void say(const char* message) {
	host->log(ID, message);
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int inRecents(void) {
	const char* current = host->current_path ? host->current_path() : NULL;
	return current && !strcmp(current, recents_path);
}

// The selected entry's place in recents, or -1.
static int selectedRecent(void) {
	const MosaicEntry* entry = host->selected_entry();
	if (!entry) return -1;
	const char* path = host->entry_path(entry);
	if (!path) return -1;
	int count = host->recents_count();
	for (int i = 0; i < count; i++) {
		const char* recent = host->recent_path(i);
		if (recent && !strcmp(recent, path)) return i;
	}
	return -1;
}

///////////////////////////////////////
// the actions

static int hasRecents(void* data) {
	(void)data;
	return host->recents_count() > 0;
}

static void cleared(void* data) {
	(void)data;
	host->recents_clear();
	say("cleared recents");
	toast("Recents cleared");
	host->reload_lists();
}

static void clearAll(void* data) {
	(void)data;
	if (!host->recents_count()) return;
	host->confirm("Clear all recently played games?", "CLEAR", cleared, NULL);
}

static int canClearHere(void* data) {
	return inRecents() && hasRecents(data);
}

static int canRemove(void* data) {
	(void)data;
	return inRecents() && selectedRecent() >= 0;
}

static void removeSelected(void* data) {
	(void)data;
	if (!inRecents()) return;
	int at = selectedRecent();
	if (at < 0) return;
	if (host->recent_remove(at) != 0) {
		toast("Couldn't remove that");
		return;
	}
	toast("Removed from recents");
	host->reload_lists();
}

///////////////////////////////////////
// hooks

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "Y", MOSAIC_BUTTON_Y },
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
};

static int init(const MosaicHost* h) {
	host = h;
	snprintf(recents_path, sizeof(recents_path), "%s/Recently Played", host->sdcard_path);

	if (!MOSAIC_HOST_HAS(host, recents_clear) || !MOSAIC_HOST_HAS(host, reload_lists) || !MOSAIC_HOST_HAS(host, confirm)) {
		say("this launcher can't clear recents for plugins; nothing to do");
		return 0;
	}

	const char* name = host->cfg_get(ID, "button", "Y");
	int button = MOSAIC_BUTTON_Y;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (!strcmp(name, buttons[i].name)) button = buttons[i].button;
	}
	if (host->register_action("clear-recents.remove", "REMOVE", canRemove, removeSelected, NULL) != 0
			|| host->register_action("clear-recents.clear", "CLEAR ALL", canClearHere, clearAll, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, "clear-recents.remove") != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, "clear-recents.clear") != 0) {
		say("couldn't bind a button; clear recents from the quick menu");
	}

	if (strcmp(host->cfg_get(ID, "tile", "on"), "off") != 0) {
		if (host->register_action("clear-recents.tile", "Clear recents", hasRecents, clearAll, NULL) != 0
				|| !MOSAIC_HOST_HAS(host, quick_tile)
				|| host->quick_tile("clear-recents.tile", MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0) {
			say("couldn't add the Clear recents tile");
		}
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
