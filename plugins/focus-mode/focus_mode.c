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
#include "mosaic_plugin.h"

#define ID "focus-mode"
#define FAVORITES_ID "favorites"

static const MosaicHost* host;

// Another plugin's setting, when it marks it public, else fallback.
static const char* publicSetting(const char* id, const char* key, const char* fallback) {
	return MOSAIC_HOST_HAS(host, cfg_read) ? host->cfg_read(id, key, fallback) : fallback;
}
static FocusList favorites;
static FocusRow rows[FOCUS_MAX + 1];

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static int isOn(void* data) {
	(void)data;
	return strcmp(host->cfg_get("on", "on"), "off") != 0;
}

static int always(void* data) {
	(void)data;
	return 1;
}

static void toggle(void* data) {
	int on = !isOn(data);
	host->cfg_set("on", on ? "on" : "off");
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(on ? tr("Focus mode on") : tr("Focus mode off"));
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

static MosaicList* rootReplace(void) {
	if (!isOn(NULL)) return NULL;

	// read here, not in init: favorites may load after this plugin
	char path[FOCUS_PATH_LEN];
	Focus_listPath(host->sdcard_path, publicSetting(FAVORITES_ID, "collection", "Favorites"), path, sizeof(path));
	Focus_load(&favorites, path);
	int count = Focus_rows(&favorites, host->sdcard_path, exists, rows);
	if (count == 0) return NULL;

	MosaicList* root = host->list_new();
	for (int i = 0; i < count; i++) {
		// the all-systems row is the only one with a name of its own
		const char* name = rows[i].name && !strcmp(rows[i].name, FOCUS_ALL_NAME) ? tr("All systems") : rows[i].name;
		host->list_push(root, host->entry_new(rows[i].path, rows[i].type, name));
	}
	return root;
}

static int init(const MosaicHost* h) {
	host = h;
	if (host->register_action("focus-mode.toggle", "Focus", always, toggle, NULL) != 0
			|| !MOSAIC_HOST_HAS(host, quick_tile)
			|| host->quick_tile("focus-mode.toggle", MOSAIC_QUICK_TOP, NULL, NULL, isOn) != 0) {
		host->log(ID, "couldn't add the Focus tile; turn focus mode off from the Launcher pak");
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_replace = rootReplace,
	.root_with_entries = 0, // other plugins' root entries stay off the focused root
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
