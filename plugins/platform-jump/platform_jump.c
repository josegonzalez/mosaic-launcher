// Platform jump: L1 and R1 jump between systems instead of letters.
//
// Inside a system, or any folder in it, R1 opens the next system and L1
// the previous one, in the root's order, as if the user had backed out to
// the root and opened it there, so B goes back to the root with that
// system selected. In a list with games from several systems, such as
// Recently Played, a collection or Favorites (the "lists" setting), they
// move to the first game of the next or previous system, taking systems
// in the order they first appear in the list. On the root (the "root"
// setting, off by default) they move between the system rows, skipping
// Recently Played, Collections, Tools and other plugins' rows. Past the
// last system they wrap around to the first (the "wrap" setting).
//
// Everywhere else, and where the setting is off or there's only one
// system to go to, L1 and R1 jump letters as stock does. Both jumps
// repeat while the shoulder is held, as stock's letter jumps do, except
// on a launcher too old to repeat an override, where they go once per
// press.
//
// The root's systems are taken from the root list as it's built. When a
// plugin replaces the root with one that has no systems, they're taken
// from the next list of system folders shown, such as a Systems folder,
// and B from a system opened with a jump goes back through the Roms
// folder.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "jump_model.h"

#define ID "platform-jump"

static const MosaicHost* host;
static char roms_path[PJ_PATH_LEN]; // <sd>/Roms
static PJSystems systems;           // the root's systems, in its order
static int root_has_systems = 0;    // the last root built listed systems

static void say(const char* message) {
	host->log(ID, message);
}

static int isOn(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(key, fallback), "off") != 0;
}

///////////////////////////////////////
// where the user is

enum {
	MODE_NONE,   // L1 and R1 jump letters
	MODE_SYSTEM, // inside a system: open the next system
	MODE_LIST,   // a list with several systems in it: move to the next one
};

// The groups for the rows of a list: each system folder and each game
// inside a system gets its system's index in seen, other rows -1. Returns
// the groups (list_count long, freed by the caller), or NULL.
static int* listGroups(const MosaicList* list, PJSystems* seen, int* count) {
	*count = list ? host->list_count(list) : 0;
	if (*count <= 0) return NULL;
	int* groups = malloc((size_t)*count * sizeof(int));
	if (!groups) return NULL;
	for (int i = 0; i < *count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		const char* path = entry ? host->entry_path(entry) : NULL;
		int type = entry ? host->entry_type(entry) : -1;
		char key[PJ_PATH_LEN];
		groups[i] = -1;
		if (!path || type == MOSAIC_ENTRY_TOGGLE || !PJ_systemKey(roms_path, path, key, sizeof(key))) continue;
		if (type == MOSAIC_ENTRY_DIR && !PJ_isSystemDir(roms_path, path)) continue; // a subfolder
		groups[i] = PJ_systemsAdd(seen, key, path);
	}
	return groups;
}

static int isRoot(const char* path) {
	return !strcmp(path, host->sdcard_path) || !strcmp(path, roms_path);
}

// What L1 and R1 do here. For MODE_SYSTEM, writes the current system's
// index in systems.
static int mode(int* current) {
	const char* path = host->current_path ? host->current_path() : NULL;
	if (!path) return MODE_NONE;

	char key[PJ_PATH_LEN];
	if (!isRoot(path) && PJ_systemKey(roms_path, path, key, sizeof(key))) {
		*current = PJ_systemsFind(&systems, key);
		return *current >= 0 && systems.count > 1 ? MODE_SYSTEM : MODE_NONE;
	}

	if (!isOn(isRoot(path) ? "root" : "lists", isRoot(path) ? "off" : "on")) return MODE_NONE;
	PJSystems seen;
	PJ_systemsInit(&seen);
	int count;
	int* groups = listGroups(host->current_list(), &seen, &count);
	int several = groups && PJ_groupCount(groups, count) > 1;
	free(groups);
	PJ_systemsClear(&seen);
	return several ? MODE_LIST : MODE_NONE;
}

///////////////////////////////////////
// jumping

// Opens a system the way backing out to the root and opening it there
// would. Opening the root first keeps the folders behind it to just the
// root, with the system selected there.
static void openSystem(int index) {
	// opening the root lists it again, which refreshes systems
	char key[PJ_PATH_LEN], path[PJ_PATH_LEN];
	snprintf(key, sizeof(key), "%s", systems.keys[index]);
	snprintf(path, sizeof(path), "%s", systems.paths[index]);

	if (root_has_systems && host->open_path(host->sdcard_path, MOSAIC_ENTRY_DIR)) {
		const MosaicList* root = host->current_list();
		int count = root ? host->list_count(root) : 0;
		for (int i = 0; i < count; i++) {
			const MosaicEntry* entry = host->list_get(root, i);
			const char* row = host->entry_path(entry);
			char row_key[PJ_PATH_LEN];
			if (host->entry_type(entry) != MOSAIC_ENTRY_DIR || !PJ_isSystemDir(roms_path, row)) continue;
			if (!PJ_systemKey(roms_path, row, row_key, sizeof(row_key)) || strcmp(row_key, key) != 0) continue;
			snprintf(path, sizeof(path), "%s", row);
			host->select_index(i);
			break;
		}
	}
	host->open_path(path, MOSAIC_ENTRY_DIR);
}

static void jump(int dir) {
	int current = -1;
	int wrap = isOn("wrap", "on");
	int m = mode(&current);
	if (m == MODE_SYSTEM) {
		int* order = malloc((size_t)systems.count * sizeof(int));
		if (!order) return;
		for (int i = 0; i < systems.count; i++) order[i] = i;
		int target = PJ_jump(order, systems.count, current, dir, wrap);
		free(order);
		if (target >= 0) openSystem(target);
	}
	else if (m == MODE_LIST) {
		PJSystems seen;
		PJ_systemsInit(&seen);
		int count;
		int* groups = listGroups(host->current_list(), &seen, &count);
		int row = groups ? PJ_jump(groups, count, host->selected_index(), dir, wrap) : -1;
		free(groups);
		PJ_systemsClear(&seen);
		if (row >= 0) host->select_index(row);
	}
}

static int canJump(const char* path, void* data) {
	(void)path;
	(void)data;
	int current;
	return mode(&current) != MODE_NONE;
}

static void prevSystem(const char* path, void* data) {
	(void)path;
	(void)data;
	jump(-1);
}

static void nextSystem(const char* path, void* data) {
	(void)path;
	(void)data;
	jump(1);
}

///////////////////////////////////////
// hooks

// Keeps the root's systems in its order. A root with none, as a plugin's
// replaced root may be, leaves it to the next list of system folders.
static void listReady(const MosaicList* list, const char* dir_path) {
	int root = !strcmp(dir_path, host->sdcard_path);
	if (!root && root_has_systems) return;

	PJSystems found;
	PJ_systemsInit(&found);
	int count = host->list_count(list);
	for (int i = 0; i < count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		const char* path = host->entry_path(entry);
		char key[PJ_PATH_LEN];
		if (host->entry_type(entry) != MOSAIC_ENTRY_DIR || !PJ_isSystemDir(roms_path, path)) continue;
		if (PJ_systemKey(roms_path, path, key, sizeof(key))) PJ_systemsAdd(&found, key, path);
	}
	if (root) root_has_systems = found.count > 0;
	if (found.count) {
		PJ_systemsClear(&systems);
		systems = found;
	}
	else PJ_systemsClear(&found);
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	PJ_systemsInit(&systems);
	root_has_systems = 0;

	if (!MOSAIC_HOST_HAS(host, override_button) || !MOSAIC_HOST_HAS(host, open_path)
			|| !MOSAIC_HOST_HAS(host, current_list) || !MOSAIC_HOST_HAS(host, select_index)) {
		say("this launcher can't hand L1 and R1 to plugins; not loading");
		return 1;
	}
	// the first available action on a button runs, so letters are the
	// fallback wherever there's no system to jump to. Both go on the same
	// trigger, so a held shoulder keeps doing one or the other.
	int trigger = MOSAIC_HOST_HAS(host, override_repeat) ? MOSAIC_TRIGGER_REPEAT : MOSAIC_TRIGGER_PRESS;
	if (host->register_action("platform-jump.prev", "SYSTEM", 0, canJump, prevSystem, NULL) != 0
			|| host->register_action("platform-jump.next", "SYSTEM", 0, canJump, nextSystem, NULL) != 0
			|| host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L1, trigger, "platform-jump.prev") != 0
			|| host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L1, trigger, "gamelist.prev_letter") != 0
			|| host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_R1, trigger, "platform-jump.next") != 0
			|| host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_R1, trigger, "gamelist.next_letter") != 0) {
		say("couldn't take L1 and R1, another plugin may have them; not loading");
		return 1;
	}
	return 0;
}

static void quit(void) {
	PJ_systemsClear(&systems);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.list_ready = listReady,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
