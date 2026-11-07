// Focus mode: the root lists your favorites and nothing else, plus one All
// systems folder with every console in it.
//
// The favorites are the favorites plugin's collection, Collections/
// Favorites.txt by default, found with favorites' own "collection" setting.
// Other plugins' root entries stay off this root. Until there's a favorite
// on the card, the launcher keeps its own root, so there's always a way to
// browse and add one.
//
// A Focus tile on the quick menu turns focus mode off and on again, as does
// the "on" setting. Recents, Collections and Tools stay on the quick menu.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "focus_model.h"
#include "nextui_plugin.h"

#define ID "focus-mode"
#define FAVORITES_ID "favorites"

static const NUIHost* host;
static FocusList favorites;
static FocusRow rows[FOCUS_MAX + 1];

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int isOn(void* data) {
	(void)data;
	return strcmp(host->cfg_get(ID, "on", "on"), "off") != 0;
}

static int always(void* data) {
	(void)data;
	return 1;
}

static void toggle(void* data) {
	int on = !isOn(data);
	host->cfg_set(ID, "on", on ? "on" : "off");
	if (NUI_HOST_HAS(host, toast)) host->toast(on ? "Focus mode on" : "Focus mode off");
	if (NUI_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

static NUIList* rootReplace(void) {
	if (!isOn(NULL)) return NULL;

	// read here, not in init: favorites may load after this plugin
	char path[FOCUS_PATH_LEN];
	Focus_listPath(host->sdcard_path, host->cfg_get(FAVORITES_ID, "collection", "Favorites"), path, sizeof(path));
	Focus_load(&favorites, path);
	int count = Focus_rows(&favorites, host->sdcard_path, exists, rows);
	if (count == 0) return NULL;

	NUIList* root = host->list_new();
	for (int i = 0; i < count; i++) {
		host->list_push(root, host->entry_new(rows[i].path, rows[i].type, rows[i].name));
	}
	return root;
}

static int init(const NUIHost* h) {
	host = h;
	if (host->register_action("focus-mode.toggle", "Focus", always, toggle, NULL) != 0
			|| !NUI_HOST_HAS(host, quick_tile)
			|| host->quick_tile("focus-mode.toggle", NUI_QUICK_TOP, NULL, NULL, isOn) != 0) {
		host->log(ID, "couldn't add the Focus tile; turn focus mode off from the Launcher pak");
	}
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.root_replace = rootReplace,
	.root_with_entries = 0, // other plugins' root entries stay off the focused root
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
