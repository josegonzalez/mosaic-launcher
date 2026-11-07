// Favorites: a folder on the root holding the games and paks you mark.
//
// Y on the game list adds the selected game or pak, or removes it if it's
// already a favorite. Favorites are stored one per line, relative to the SD
// card, in .userdata/<platform>/launcher/favorites.txt.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "nextui_plugin.h"

#define ID "favorites"
#define MAX_FAVORITES 256
#define MAX_PATH_LEN 512

static const NUIHost* host;
static char folder_path[MAX_PATH_LEN]; // the virtual folder, <sd>/Favorites
static char list_path[MAX_PATH_LEN];
static char favorites[MAX_FAVORITES][MAX_PATH_LEN]; // card-relative, eg. /Roms/GB/Tetris.gb
static int count = 0;

static void say(const char* message) {
	host->log(ID, message);
}

static void load(void) {
	count = 0;
	FILE* f = fopen(list_path, "r");
	if (!f) return;
	char line[MAX_PATH_LEN];
	while (count < MAX_FAVORITES && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0]) continue;
		snprintf(favorites[count++], MAX_PATH_LEN, "%s", line);
	}
	fclose(f);
}

static void save(void) {
	char tmp[MAX_PATH_LEN + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", list_path);
	FILE* f = fopen(tmp, "w");
	if (!f) {
		say("couldn't save favorites");
		return;
	}
	for (int i = 0; i < count; i++) fprintf(f, "%s\n", favorites[i]);
	fclose(f);
	rename(tmp, list_path); // so a power cut never leaves half a list
}

// The card-relative form of path, or NULL if it isn't on the card.
static const char* relative(const char* path) {
	size_t len = strlen(host->sdcard_path);
	if (!path || strncmp(path, host->sdcard_path, len) != 0 || path[len] != '/') return NULL;
	return path + len;
}

static int indexOf(const char* rel) {
	for (int i = 0; i < count; i++) {
		if (!strcmp(favorites[i], rel)) return i;
	}
	return -1;
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

///////////////////////////////////////
// the toggle

static int canToggle(void* data) {
	(void)data;
	const NUIEntry* entry = host->selected_entry();
	if (!entry) return 0;
	int type = host->entry_type(entry);
	return (type == NUI_ENTRY_ROM || type == NUI_ENTRY_PAK) && relative(host->entry_path(entry));
}

static void toggle(void* data) {
	(void)data;
	const NUIEntry* entry = host->selected_entry();
	const char* rel = entry ? relative(host->entry_path(entry)) : NULL;
	if (!rel) return;

	char message[MAX_PATH_LEN + 32];
	int i = indexOf(rel);
	if (i >= 0) {
		memmove(&favorites[i], &favorites[i + 1], (size_t)(count - i - 1) * MAX_PATH_LEN);
		count--;
		snprintf(message, sizeof(message), "removed %s", rel);
	}
	else if (count < MAX_FAVORITES) {
		// newest first, like recents
		memmove(&favorites[1], &favorites[0], (size_t)count * MAX_PATH_LEN);
		snprintf(favorites[0], MAX_PATH_LEN, "%s", rel);
		count++;
		snprintf(message, sizeof(message), "added %s", rel);
	}
	else {
		say("favorites are full");
		return;
	}
	save();
	say(message);
	host->request_redraw();
}

///////////////////////////////////////
// hooks

static void rootEntries(NUIList* root) {
	host->list_push(root, host->entry_new(folder_path, NUI_ENTRY_DIR, "Favorites"));
}

static NUIList* virtualDir(const char* path) {
	if (strcmp(path, folder_path) != 0) return NULL;

	NUIList* list = host->list_new();
	for (int i = 0; i < count; i++) {
		char full[MAX_PATH_LEN * 2];
		snprintf(full, sizeof(full), "%s%s", host->sdcard_path, favorites[i]);
		if (!exists(full)) continue; // keep it listed in the file in case the card comes back
		int type = endsWith(full, ".pak") ? NUI_ENTRY_PAK : NUI_ENTRY_ROM;
		host->list_push(list, host->entry_new(full, type, NULL));
	}
	return list;
}

static int init(const NUIHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/Favorites", host->sdcard_path);
	snprintf(list_path, sizeof(list_path), "%s/launcher/favorites.txt", host->userdata_path);
	load();

	if (host->register_action("favorites.toggle", "FAVORITE", canToggle, toggle, NULL) != 0
			|| host->bind_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_Y, NUI_TRIGGER_PRESS, "favorites.toggle") != 0) {
		say("couldn't bind Y; favorites can't be changed from the game list");
	}
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
