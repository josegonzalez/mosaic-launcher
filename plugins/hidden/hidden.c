// Hidden games: drops the games you hide from every list, and keeps them in
// a Hidden folder on the root so they can be brought back.
//
// Y on the game list hides the selected game or pak (the "button" setting
// picks another button). In the Hidden folder the same button unhides it.
// The folder shows on the root only while something hidden is on the card.
//
// The list is .userdata/<platform>/launcher/hidden.txt, one full path per
// line, so it can be edited by hand; it's read again whenever it changes.
//
// A multi-disc or cue-sheet game shows as a folder that launches when
// opened. Hiding it, or one of its discs, hides the folder, and anything in
// a hidden folder (its .m3u in Recently Played or a collection, say) is
// hidden with it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "nextui_plugin.h"
#include "hiddenset.h"

#define ID "hidden"
#define MAX_PATH_LEN 512

static const NUIHost* host;
static char folder_path[MAX_PATH_LEN]; // the virtual folder, <sd>/Hidden
static char state_dir[MAX_PATH_LEN];   // <userdata>/launcher
static char list_path[MAX_PATH_LEN + 16]; // <userdata>/launcher/hidden.txt
static HiddenSet hidden;
static struct stat loaded; // the list file as it was when last read or written

static void say(const char* message) {
	host->log(ID, message);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static void remember(void) {
	if (stat(list_path, &loaded) != 0) memset(&loaded, 0, sizeof(loaded));
}

// Reads the list again if it changed on disk since we last saw it.
static void refresh(void) {
	struct stat st;
	if (stat(list_path, &st) != 0) memset(&st, 0, sizeof(st));
	if (st.st_mtime == loaded.st_mtime && st.st_size == loaded.st_size && st.st_ino == loaded.st_ino) return;
	if (HiddenSet_load(&hidden, list_path) != 0) say("couldn't read the hidden list");
	loaded = st;
}

static int save(void) {
	mkdir(state_dir, 0755);
	if (HiddenSet_save(&hidden, list_path) != 0) {
		say("couldn't save the hidden list");
		return -1;
	}
	remember();
	return 0;
}

static void toast(const char* message) {
	if (NUI_HOST_HAS(host, toast)) host->toast(message);
}

// Lists change on disk, so have the launcher list them again.
static void reload(void) {
	if (NUI_HOST_HAS(host, reload_lists)) host->reload_lists();
	else host->request_redraw();
}

///////////////////////////////////////
// what hiding an entry means

// True when dir is a game folder: it holds <dir name>.m3u or .cue.
static int isGameFolder(const char* dir) {
	const char* name = strrchr(dir, '/');
	if (!name) return 0;
	char path[MAX_PATH_LEN * 2 + 8];
	snprintf(path, sizeof(path), "%s%s.m3u", dir, name);
	if (exists(path)) return 1;
	snprintf(path, sizeof(path), "%s%s.cue", dir, name);
	return exists(path);
}

// The path to hide for an entry, or 0 if it isn't a game: a game or pak
// itself, a game folder, or for a disc inside a game folder, that folder.
static int pathFor(const NUIEntry* entry, char* out, size_t size) {
	if (!entry) return 0;
	const char* path = host->entry_path(entry);
	if (!path || !path[0]) return 0;
	switch (host->entry_type(entry)) {
		case NUI_ENTRY_PAK:
			snprintf(out, size, "%s", path);
			return 1;
		case NUI_ENTRY_DIR:
			if (!isGameFolder(path)) return 0;
			snprintf(out, size, "%s", path);
			return 1;
		case NUI_ENTRY_ROM: {
			char dir[MAX_PATH_LEN];
			snprintf(dir, sizeof(dir), "%s", path);
			char* slash = strrchr(dir, '/');
			if (slash && slash != dir) {
				*slash = '\0';
				if (isGameFolder(dir)) {
					snprintf(out, size, "%s", dir);
					return 1;
				}
			}
			snprintf(out, size, "%s", path);
			return 1;
		}
	}
	return 0;
}

// True when path, or the folder it's in, is hidden.
static int isHidden(const char* path) {
	if (HiddenSet_contains(&hidden, path)) return 1;
	const char* slash = strrchr(path, '/');
	if (!slash || slash == path) return 0;
	size_t len = (size_t)(slash - path);
	if (len >= MAX_PATH_LEN) return 0;
	char dir[MAX_PATH_LEN];
	memcpy(dir, path, len);
	dir[len] = '\0';
	return HiddenSet_contains(&hidden, dir);
}

static int inFolder(void) {
	const char* current = host->current_path ? host->current_path() : NULL;
	return current && !strcmp(current, folder_path);
}

///////////////////////////////////////
// the actions

static int canHide(void* data) {
	(void)data;
	if (inFolder()) return 0;
	char path[MAX_PATH_LEN * 2];
	return pathFor(host->selected_entry(), path, sizeof(path));
}

static void hide(void* data) {
	(void)data;
	char path[MAX_PATH_LEN * 2];
	if (inFolder() || !pathFor(host->selected_entry(), path, sizeof(path))) return;
	refresh();
	int added = HiddenSet_add(&hidden, path);
	if (added < 0) {
		toast("Couldn't hide that");
		return;
	}
	if (added && save() != 0) {
		HiddenSet_remove(&hidden, path);
		toast("Couldn't save hidden games");
		return;
	}
	char message[MAX_PATH_LEN * 2 + 8];
	snprintf(message, sizeof(message), "hid %s", path);
	say(message);
	toast("Hidden");
	reload();
}

static int canUnhide(void* data) {
	(void)data;
	if (!inFolder()) return 0;
	const NUIEntry* entry = host->selected_entry();
	return entry && HiddenSet_contains(&hidden, host->entry_path(entry));
}

static void unhide(void* data) {
	(void)data;
	const NUIEntry* entry = host->selected_entry();
	if (!inFolder() || !entry) return;
	char path[MAX_PATH_LEN * 2];
	snprintf(path, sizeof(path), "%s", host->entry_path(entry));
	refresh();
	if (!HiddenSet_remove(&hidden, path)) return;
	if (save() != 0) {
		HiddenSet_add(&hidden, path);
		toast("Couldn't save hidden games");
		return;
	}
	char message[MAX_PATH_LEN * 2 + 16];
	snprintf(message, sizeof(message), "unhid %s", path);
	say(message);
	toast("Unhidden");
	reload();
}

///////////////////////////////////////
// hooks

// True when something hidden is still on the card.
static int anyOnCard(void) {
	for (int i = 0; i < hidden.count; i++) {
		if (exists(hidden.paths[i])) return 1;
	}
	return 0;
}

static void rootEntries(NUIList* root) {
	refresh();
	if (!anyOnCard()) return;
	host->list_push(root, host->entry_new(folder_path, NUI_ENTRY_DIR, "Hidden"));
}

static NUIList* virtualDir(const char* path) {
	if (strcmp(path, folder_path) != 0) return NULL;
	refresh();
	NUIList* list = host->list_new();
	for (int i = 0; i < hidden.count; i++) {
		const char* item = hidden.paths[i];
		struct stat st;
		if (stat(item, &st) != 0) continue; // keep it in the file in case the card comes back
		int type = endsWith(item, ".pak") ? NUI_ENTRY_PAK : S_ISDIR(st.st_mode) ? NUI_ENTRY_DIR : NUI_ENTRY_ROM;
		host->list_push(list, host->entry_new(item, type, NULL));
	}
	return list;
}

static int filter(const NUIEntry* entry, const char* dir_path) {
	if (!hidden.count) return 1;
	if (dir_path && !strcmp(dir_path, folder_path)) return 1;
	return !isHidden(host->entry_path(entry));
}

static void quit(void) {
	HiddenSet_clear(&hidden);
}

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "Y", NUI_BUTTON_Y },
	{ "L2", NUI_BUTTON_L2 },
	{ "R2", NUI_BUTTON_R2 },
	{ "SELECT", NUI_BUTTON_SELECT },
};

static int init(const NUIHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/Hidden", host->sdcard_path);
	snprintf(state_dir, sizeof(state_dir), "%s/launcher", host->userdata_path);
	snprintf(list_path, sizeof(list_path), "%s/hidden.txt", state_dir);
	HiddenSet_init(&hidden);
	memset(&loaded, 0, sizeof(loaded));
	loaded.st_size = -1; // so the first refresh reads the file
	refresh();

	const char* name = host->cfg_get(ID, "button", "Y");
	int button = NUI_BUTTON_Y;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (!strcmp(name, buttons[i].name)) button = buttons[i].button;
	}

	if (host->register_action("hidden.hide", "HIDE", canHide, hide, NULL) != 0
			|| host->register_action("hidden.unhide", "UNHIDE", canUnhide, unhide, NULL) != 0
			|| host->bind_button(NUI_SCREEN_GAMELIST, button, NUI_TRIGGER_PRESS, "hidden.hide") != 0
			|| host->bind_button(NUI_SCREEN_GAMELIST, button, NUI_TRIGGER_PRESS, "hidden.unhide") != 0) {
		say("couldn't bind a button; games can't be hidden from the game list");
	}
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.filter = filter,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
