// Focus mode: the root lists the games in one entry set and nothing else,
// plus one All systems folder with every console in it.
//
// The set is the "set" setting's, favorites by default (the favorites
// plugin's collection), or any other registered set, such as tags:<name>
// or smart:<name>, read with set_list each time the root is listed. Other
// plugins' root entries stay off this root. Until a game in the set is on
// the card, as when the set's plugin is off and nobody registered it, the
// launcher keeps its own root, so there's always a way to browse and add
// one. The root is listed again when the set changes
// (launcher.set_changed).
//
// A Focus tile on the quick menu turns focus mode off and on again, as does
// the "on" setting. The focus-mode.pick action lists every set but hidden
// (set_names) to focus on with choose, from a Focus on tile on the quick
// menu (the "pick_tile" setting), a game list button (the "pick_button"
// setting) or an FN button; picking one turns focus mode on. Recents,
// Collections and Tools stay on the quick menu.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "focus_model.h"
#include "mosaic_plugin.h"

#define ID "focus-mode"
#define PICK_ID "focus-mode.pick"
#define SETS_MAX 64 // what choose takes

static const MosaicHost* host;
static FocusList members;
static FocusRow rows[FOCUS_MAX + 1];

// the list of sets the pick action shows
static char set_names[SETS_MAX][80];
static char set_labels[SETS_MAX][160];
static int set_count = 0;

static int typeOf(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0) return -1;
	if (!S_ISDIR(st.st_mode)) return MOSAIC_ENTRY_ROM;
	size_t len = strlen(path);
	return len > 4 && !strcmp(path + len - 4, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_DIR;
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static void reload(void) {
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

static int isOn(void* data) {
	(void)data;
	return strcmp(host->cfg_get("on", "on"), "off") != 0;
}

static const char* focusedSet(void) {
	return host->cfg_get("set", "favorites");
}

static int always(const char* path, void* data) {
	(void)path;
	(void)data;
	return 1;
}

static void toggle(const char* path, void* data) {
	(void)path;
	int on = !isOn(data);
	host->cfg_set("on", on ? "on" : "off");
	toast(on ? tr("Focus mode on") : tr("Focus mode off"));
	reload();
}

static void addMember(const char* path, void* data) {
	(void)data;
	Focus_add(&members, path);
}

static MosaicList* rootReplace(void) {
	if (!isOn(NULL) || !MOSAIC_HOST_HAS(host, is_filtered)) return NULL;

	// asked here, not in init: the set's owner may load after this plugin
	members.count = 0;
	host->set_list(focusedSet(), addMember, NULL);
	int count = Focus_rows(&members, host->sdcard_path, typeOf, rows);
	if (count == 0) return NULL;

	MosaicList* root = host->list_new();
	for (int i = 0; i < count; i++) {
		// the all-systems row is the only one with a name of its own
		const char* name = rows[i].name && !strcmp(rows[i].name, FOCUS_ALL_NAME) ? tr("All systems") : rows[i].name;
		host->list_push(root, host->entry_new(rows[i].path, rows[i].type, name));
	}
	return root;
}

// The focused set changed: list the root again.
static void setChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)data;
	char set[sizeof(set_names[0])];
	if (!isOn(NULL) || host->event_value(payload, "name", set, sizeof(set)) < 0 || strcmp(set, focusedSet()) != 0) return;
	reload();
}

///////////////////////////////////////
// picking the set

static void addSet(const char* name, void* data) {
	(void)data;
	if (!Focus_focusable(name) || set_count >= SETS_MAX || strlen(name) >= sizeof(set_names[0])) return;
	snprintf(set_names[set_count], sizeof(set_names[0]), "%s", name);
	Focus_setLabel(name, tr("Favorites"), tr("Tag: %s"), tr("Collection: %s"), set_labels[set_count], sizeof(set_labels[0]));
	set_count++;
}

static void picked(int index, void* data) {
	(void)data;
	if (index < 0 || index >= set_count) return;
	host->cfg_set("set", set_names[index]);
	host->cfg_set("on", "on");
	char message[256];
	snprintf(message, sizeof(message), tr("Focusing on %s"), set_labels[index]);
	toast(message);
	snprintf(message, sizeof(message), "focusing on %s", set_names[index]);
	host->log(ID, message);
	reload();
}

static int canPick(const char* path, void* data) {
	(void)path;
	(void)data;
	return MOSAIC_HOST_HAS(host, choose) && MOSAIC_HOST_HAS(host, set_names) && host->set_names(NULL, NULL) > 0;
}

static void pick(const char* path, void* data) {
	if (!canPick(path, data)) return;
	set_count = 0;
	host->set_names(addSet, NULL);
	if (!set_count) {
		toast(tr("No sets to focus on"));
		return;
	}
	const char* labels[SETS_MAX];
	int current = 0;
	for (int i = 0; i < set_count; i++) {
		labels[i] = set_labels[i];
		if (!strcmp(set_names[i], focusedSet())) current = i;
	}
	if (host->choose(tr("Focus on"), labels, set_count, current, picked, NULL) != 0) host->log(ID, "couldn't list the sets");
}

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
	{ "SELECT", MOSAIC_BUTTON_SELECT },
};

static int init(const MosaicHost* h) {
	host = h;
	if (host->register_action("focus-mode.toggle", "Focus", 0, always, toggle, NULL) != 0
			|| !MOSAIC_HOST_HAS(host, quick_tile)
			|| host->quick_tile("focus-mode.toggle", MOSAIC_QUICK_TOP, NULL, NULL, isOn) != 0) {
		host->log(ID, "couldn't add the Focus tile; turn focus mode off from the Launcher pak");
	}
	if (!MOSAIC_HOST_HAS(host, set_names)) {
		host->log(ID, "this launcher has no entry sets, so the root stays stock's");
		return 0;
	}
	host->subscribe("launcher.set_changed", setChanged, NULL);
	if (host->register_action(PICK_ID, "Focus on", 0, canPick, pick, NULL) != 0) {
		host->log(ID, "couldn't register " PICK_ID);
		return 0;
	}
	const char* name = host->cfg_get("pick_button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, PICK_ID) != 0) {
			host->log(ID, "couldn't bind the focus on button");
		}
	}
	if (strcmp(host->cfg_get("pick_tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(PICK_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		host->log(ID, "couldn't add the Focus on tile");
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
