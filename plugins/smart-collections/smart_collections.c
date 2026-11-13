// Smart collections: a folder on the root holding one collection per game
// franchise (Mario, Zelda, Sonic and so on), built from game names across
// every system.
//
// The card is scanned once per launcher start, the first time the root is
// listed. Games are matched by name, the same name the game list shows
// (map.txt aliases included), against a built-in list of franchises plus
// any the "extra" setting adds. A franchise becomes a collection once it
// has "min" games.
//
// Y on a collection dismisses it; the plugin remembers dismissed ones in
// its .cfg. Y on the Smart Collections folder brings them all back. While
// the tags plugin offers tags.copy, Y on a collection opens a menu of
// Dismiss and Save as tag instead (the launcher's menu for two actions on
// one button); Save as tag names a new tag on the keyboard, starting from
// the collection's name, and tags.copy puts it on the collection's games
// once: the tag doesn't follow the collection afterwards.
//
// Other plugins read each collection as the entry set smart:<name> (see
// set_register), by full path, registered for every franchise as the
// launcher starts and holding the games its folder lists: none while it
// has fewer than "min" games or is dismissed. Asking one scans the card if
// it hasn't been yet.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "smart_model.h"

#define ID "smart-collections"
#define FOLDER_NAME "Smart Collections"
#define MAX_PATH_LEN 512
#define MAX_DEPTH 2 // subfolders below a system folder that are scanned

static const MosaicHost* host;
static char folder_path[MAX_PATH_LEN]; // <sd>/Smart Collections
static SMCatalog catalog;
static SMIndex index_;
static SMNameSet dismissed;
static int min_games = 2;
static int scanned = 0;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static void say(const char* message) {
	host->log(ID, message);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int isDir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// Names stock leaves out of its lists.
static int hidden(const char* name) {
	return name[0] == '.' || endsWith(name, ".disabled") || !strcmp(name, "map.txt");
}

static char* readFile(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* text = size >= 0 ? malloc((size_t)size + 1) : NULL;
	if (text) text[fread(text, 1, (size_t)size, f)] = '\0';
	fclose(f);
	return text;
}

///////////////////////////////////////
// scanning

// True when a system folder has an emulator, as stock checks before
// listing it: the emulator is the tag in the folder's last parentheses.
static int hasEmulator(const char* system) {
	char emu[256];
	const char* open = strrchr(system, '(');
	if (snprintf(emu, sizeof(emu), "%s", open ? open + 1 : system) >= (int)sizeof(emu)) return 0;
	char* close = strchr(emu, ')');
	if (open && close) *close = '\0';

	char path[MAX_PATH_LEN * 2];
	if (snprintf(path, sizeof(path), "%s/.system/%s/paks/Emus/%s.pak/launch.sh", host->sdcard_path, host->platform, emu) < (int)sizeof(path)
			&& exists(path)) return 1;
	return snprintf(path, sizeof(path), "%s/Emus/%s/%s.pak/launch.sh", host->sdcard_path, host->platform, emu) < (int)sizeof(path)
		&& exists(path);
}

// A folder that launches a game when opened: it holds a .cue or .m3u
// named after itself.
static int isGameFolder(const char* path, const char* name) {
	char file[MAX_PATH_LEN * 2];
	if (snprintf(file, sizeof(file), "%s/%s.cue", path, name) < (int)sizeof(file) && exists(file)) return 1;
	return snprintf(file, sizeof(file), "%s/%s.m3u", path, name) < (int)sizeof(file) && exists(file);
}

static void scanDir(const char* dir, int depth) {
	DIR* dh = opendir(dir);
	if (!dh) return;

	char map_path[MAX_PATH_LEN + 16];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", dir);
	char* map = readFile(map_path);

	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (hidden(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;

		int type = MOSAIC_ENTRY_ROM;
		if (isDir(path)) {
			if (endsWith(dp->d_name, ".pak")) continue;
			if (!isGameFolder(path, dp->d_name)) {
				if (depth < MAX_DEPTH) scanDir(path, depth + 1);
				continue;
			}
			type = MOSAIC_ENTRY_DIR;
		}

		char name[256];
		if (!map || !SM_mapLookup(map, dp->d_name, name, sizeof(name))) SM_cleanName(dp->d_name, name, sizeof(name));
		if (hidden(name)) continue; // map.txt can hide a game
		SM_indexAdd(&index_, &catalog, path, name, type);
	}
	closedir(dh);
	free(map);
}

static void scan(void) {
	if (scanned) return;
	scanned = 1;

	char roms[MAX_PATH_LEN];
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	DIR* dh = opendir(roms);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (hidden(dp->d_name) || !hasEmulator(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", roms, dp->d_name) >= (int)sizeof(path)) continue;
		if (isDir(path)) scanDir(path, 0);
	}
	closedir(dh);
	SM_indexSort(&index_, &catalog);

	int shown[SM_MAX_FRANCHISES];
	char message[128];
	snprintf(message, sizeof(message), "%d games in %d collections",
		index_.game_count, SM_visible(&index_, &catalog, min_games, &dismissed, shown));
	say(message);
}

///////////////////////////////////////
// paths

// The collection a path under the folder names, or -1.
static int collectionAt(const char* path) {
	size_t len = strlen(folder_path);
	if (!path || strncmp(path, folder_path, len) != 0 || path[len] != '/') return -1;
	return SM_find(&catalog, path + len + 1);
}

///////////////////////////////////////
// dismissing and restoring

static void saveDismissed(void) {
	char list[SM_MAX_FRANCHISES * SM_NAME_LEN];
	SM_setFormat(&dismissed, list, sizeof(list));
	host->cfg_set("dismissed", list);
}

// Tells other plugins the set for collection changed.
static void announce(const char* collection) {
	if (!MOSAIC_HOST_HAS(host, set_names)) return;
	char name[SM_NAME_LEN + 8];
	snprintf(name, sizeof(name), "smart:%s", collection);
	host->set_changed(name);
}

static void changed(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

// The collection waiting on the dismiss dialog.
static char pending[SM_NAME_LEN];

static void dismissConfirmed(void* data) {
	(void)data;
	if (!pending[0] || !SM_setAdd(&dismissed, pending)) return;
	saveDismissed();
	char message[SM_NAME_LEN + 256];
	snprintf(message, sizeof(message), "dismissed %s", pending);
	say(message);
	announce(pending);
	snprintf(message, sizeof(message), tr("Dismissed %s"), pending);
	pending[0] = '\0';
	changed(message);
}

static int canDismiss(const char* path, void* data) {
	(void)path;
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	if (!entry || host->entry_type(entry) != MOSAIC_ENTRY_DIR) return 0;
	int i = collectionAt(host->entry_path(entry));
	return i >= 0 && !SM_setHas(&dismissed, catalog.items[i].name);
}

static void dismiss(const char* path, void* data) {
	(void)path;
	if (!canDismiss(path, data)) return;
	int i = collectionAt(host->entry_path(host->selected_entry()));
	snprintf(pending, sizeof(pending), "%s", catalog.items[i].name);
	if (!MOSAIC_HOST_HAS(host, confirm)) {
		dismissConfirmed(NULL);
		return;
	}
	char question[SM_NAME_LEN + 256];
	snprintf(question, sizeof(question), tr("Dismiss %s?"), pending);
	host->confirm(question, tr("DISMISS"), dismissConfirmed, NULL);
}

static void restoreConfirmed(void* data) {
	(void)data;
	if (!dismissed.count) return;
	for (int i = 0; i < dismissed.count; i++) {
		int c = SM_find(&catalog, dismissed.names[i]); // its set's name is the catalog's
		announce(c >= 0 ? catalog.items[c].name : dismissed.names[i]);
	}
	dismissed.count = 0;
	saveDismissed();
	say("brought back dismissed collections");
	changed(tr("Dismissed collections are back"));
}

static int canRestore(const char* path, void* data) {
	(void)path;
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	return entry && dismissed.count > 0 && !strcmp(host->entry_path(entry), folder_path);
}

static void restore(const char* path, void* data) {
	(void)path;
	if (!canRestore(path, data)) return;
	if (!MOSAIC_HOST_HAS(host, confirm)) {
		restoreConfirmed(NULL);
		return;
	}
	char question[256];
	snprintf(question, sizeof(question), trPlural("Bring back %d dismissed collection?", "Bring back %d dismissed collections?",
		(unsigned long)dismissed.count), dismissed.count);
	host->confirm(question, tr("RESTORE"), restoreConfirmed, NULL);
}

///////////////////////////////////////
// saving as a tag

// The collection waiting on the keyboard.
static char saving[SM_NAME_LEN];

static void saveTyped(const char* text, void* data) {
	(void)data;
	if (!text || !saving[0]) return; // cancelled
	char args[SM_NAME_LEN * 4 + 32], reply[MOSAIC_CALL_MAX], value[SM_NAME_LEN * 2];
	snprintf(args, sizeof(args), "tag=%s\nset=smart:%s", text, saving);
	int rc = host->call("tags.copy", args, reply, sizeof(reply));
	char message[SM_NAME_LEN * 2 + 256];
	if (rc < 0) {
		snprintf(message, sizeof(message), "couldn't save %s as a tag: %d", saving, rc);
		say(message);
		host->toast(tr("Couldn't save the tag"));
		saving[0] = '\0';
		return;
	}
	if (host->event_value(reply, "error", value, sizeof(value)) >= 0) {
		snprintf(message, sizeof(message), "couldn't save %s as a tag: %s", saving, value);
		say(message);
		if (!strcmp(value, "exists")) snprintf(message, sizeof(message), tr("A tag named %s already exists"), text);
		else if (!strcmp(value, "name")) snprintf(message, sizeof(message), "%s", tr("Not a tag name"));
		else snprintf(message, sizeof(message), "%s", tr("Couldn't save the tag"));
		host->toast(message);
		saving[0] = '\0';
		return;
	}
	char tag[SM_NAME_LEN * 2], games[16];
	host->event_value(reply, "tag", tag, sizeof(tag));
	host->event_value(reply, "games", games, sizeof(games));
	int count = atoi(games);
	snprintf(message, sizeof(message), "saved %s as the tag %s, %d games", saving, tag, count);
	say(message);
	snprintf(message, sizeof(message), trPlural("Saved %d game as %s", "Saved %d games as %s", (unsigned long)count), count, tag);
	host->toast(message);
	saving[0] = '\0';
}

static int canSave(const char* path, void* data) {
	(void)path;
	(void)data;
	if (!MOSAIC_HOST_HAS(host, call) || !MOSAIC_HOST_HAS(host, keyboard) || !MOSAIC_HOST_HAS(host, toast)
			|| !host->has_capability("tags.copy")) return 0;
	return canDismiss(NULL, NULL); // a collection that shows
}

static void saveAsTag(const char* path, void* data) {
	if (!canSave(path, data)) return;
	int i = collectionAt(host->entry_path(host->selected_entry()));
	snprintf(saving, sizeof(saving), "%s", catalog.items[i].name);
	host->keyboard(tr("Tag name"), saving, saveTyped, NULL);
}

///////////////////////////////////////
// hooks

// Shown while any franchise has enough games, even if all are dismissed,
// so they can be brought back.
static void rootEntries(MosaicList* root) {
	scan();
	int found[SM_MAX_FRANCHISES];
	if (SM_visible(&index_, &catalog, min_games, NULL, found) == 0) return;
	host->list_push(root, host->entry_new(folder_path, MOSAIC_ENTRY_DIR, tr("Smart Collections")));
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, folder_path)) {
		scan();
		MosaicList* list = host->list_new();
		int shown[SM_MAX_FRANCHISES];
		int n = SM_visible(&index_, &catalog, min_games, &dismissed, shown);
		for (int k = 0; k < n; k++) {
			const char* name = catalog.items[shown[k]].name;
			char dir[MAX_PATH_LEN + SM_NAME_LEN];
			snprintf(dir, sizeof(dir), "%s/%s", folder_path, name);
			host->list_push(list, host->entry_new(dir, MOSAIC_ENTRY_DIR, name));
		}
		return list;
	}

	size_t len = strlen(folder_path);
	if (strncmp(path, folder_path, len) != 0 || path[len] != '/') return NULL;
	scan();
	MosaicList* list = host->list_new();
	int i = collectionAt(path);
	if (i < 0) return list;
	for (int k = 0; k < index_.member_count[i]; k++) {
		const SMGame* game = &index_.games[index_.members[i][k]];
		if (!exists(game->path)) continue; // deleted since the scan
		host->list_push(list, host->entry_new(game->path, game->type, game->name));
	}
	return list;
}

///////////////////////////////////////
// the sets

// The franchise a set's name names, smart:<name>, when its collection
// shows, else -1.
static int setCollection(const char* set) {
	if (!set || strncmp(set, "smart:", 6) != 0) return -1;
	scan();
	int i = SM_find(&catalog, set + 6);
	if (i < 0 || index_.member_count[i] < min_games || SM_setHas(&dismissed, catalog.items[i].name)) return -1;
	return i;
}

static int setHas(const char* name, const char* path) {
	int i = setCollection(name);
	if (i < 0 || !path) return 0;
	for (int k = 0; k < index_.member_count[i]; k++) {
		if (!strcmp(index_.games[index_.members[i][k]].path, path)) return 1;
	}
	return 0;
}

static void setList(const char* name, void (*add)(const char* path, void* ctx), void* ctx) {
	int i = setCollection(name);
	for (int k = 0; i >= 0 && k < index_.member_count[i]; k++) add(index_.games[index_.members[i][k]].path, ctx);
}

static void registerSets(void) {
	if (!MOSAIC_HOST_HAS(host, is_filtered)) return;
	for (int i = 0; i < catalog.count; i++) {
		char name[SM_NAME_LEN + 8];
		snprintf(name, sizeof(name), "smart:%s", catalog.items[i].name);
		if (host->set_register(name, setHas, setList) != 0) {
			char message[sizeof(name) + 32];
			snprintf(message, sizeof(message), "couldn't register the set %s", name);
			say(message);
		}
	}
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/" FOLDER_NAME, host->sdcard_path);

	SM_catalogInit(&catalog);
	if (strcmp(host->cfg_get("builtin", "on"), "off") != 0) SM_catalogAddBuiltin(&catalog);
	SM_catalogAddList(&catalog, host->cfg_get("extra", ""));
	min_games = atoi(host->cfg_get("min", "2"));
	if (min_games < 1) min_games = 1;
	SM_setParse(&dismissed, host->cfg_get("dismissed", ""));
	SM_indexInit(&index_);
	scanned = 0;
	registerSets();

	if (host->register_action("smart-collections.dismiss", "DISMISS", 0, canDismiss, dismiss, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "smart-collections.dismiss") != 0) {
		say("couldn't bind Y; collections can't be dismissed");
	}
	if (host->register_action("smart-collections.restore", "RESTORE", 0, canRestore, restore, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "smart-collections.restore") != 0) {
		say("couldn't bind Y; dismissed collections can't be brought back");
	}
	if (host->register_action("smart-collections.save_tag", "SAVE AS TAG", 0, canSave, saveAsTag, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "smart-collections.save_tag") != 0) {
		say("couldn't bind Y; collections can't be saved as tags");
	}
	return 0;
}

static void quit(void) {
	SM_indexFree(&index_);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
