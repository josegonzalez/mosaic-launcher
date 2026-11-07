// Favorites: a folder on the root holding the games and paks you mark.
//
// Y on the game list adds the selected game or pak, or removes it if it's
// already a favorite. Favorites live in a regular NextUI collection,
// Collections/Favorites.txt by default (the "collection" setting changes the
// name), so stock's Collections view and other tools see the same list.
//
// A collection file can't hold art, but a Collections/Favorites/ folder can
// (some themes ship one with .media/bg.png). While the collection exists,
// that folder's background is used for Favorites, and the folder itself is
// hidden from Collections if it holds nothing but art, so Favorites shows once.
//
// A multi-disc or cue-sheet game shows as a folder that launches when opened.
// For those, and for a "Disc N" entry inside one, the favorite is the game's
// .m3u (or its .cue), the same file stock records in Recently Played.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "nextui_plugin.h"

#define ID "favorites"
#define MAX_FAVORITES 256
#define MAX_PATH_LEN 512

static const NUIHost* host;
static char folder_path[MAX_PATH_LEN];     // the virtual folder, <sd>/Favorites
static char folder_name[128];               // its name on the root
static char collections_dir[MAX_PATH_LEN];  // <sd>/Collections
static char list_path[MAX_PATH_LEN + 160];  // <sd>/Collections/Favorites.txt
static char art_dir[MAX_PATH_LEN + 160];    // <sd>/Collections/Favorites, for its .media art
static char favorites[MAX_FAVORITES][MAX_PATH_LEN]; // card-relative, eg. /Roms/GB/Tetris.gb
static int count = 0;

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

static void readList(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[MAX_PATH_LEN];
	while (count < MAX_FAVORITES && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0] || indexOf(line) >= 0) continue;
		snprintf(favorites[count++], MAX_PATH_LEN, "%s", line);
	}
	fclose(f);
}

// Re-read every time: other tools can edit the collection too.
static void load(void) {
	count = 0;
	readList(list_path);
}

static int save(void) {
	mkdir(collections_dir, 0755);
	char tmp[sizeof(list_path) + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", list_path);
	FILE* f = fopen(tmp, "w");
	if (!f) {
		say("couldn't save favorites");
		return -1;
	}
	for (int i = 0; i < count; i++) fprintf(f, "%s\n", favorites[i]);
	fclose(f);
	rename(tmp, list_path); // so a power cut never leaves half a list
	return 0;
}

// Earlier builds kept favorites in .userdata; fold them into the collection.
static void migrate(void) {
	char old_path[MAX_PATH_LEN];
	snprintf(old_path, sizeof(old_path), "%s/launcher/favorites.txt", host->userdata_path);
	if (!exists(old_path)) return;
	load();
	int before = count;
	readList(old_path);
	if (count == before || save() == 0) {
		char done[MAX_PATH_LEN + 16];
		snprintf(done, sizeof(done), "%s.migrated", old_path);
		rename(old_path, done);
		say("moved the old favorites list into the collection");
	}
}

///////////////////////////////////////
// what a favorite points at

// <dir>/<dir name><ext>, if it exists.
static int fileNamedAfter(const char* dir, const char* ext, char* out, size_t size) {
	const char* name = strrchr(dir, '/');
	if (!name) return 0;
	snprintf(out, size, "%s%s%s", dir, name, ext);
	return exists(out);
}

// The file to record for an entry, or 0 if it can't be a favorite. Mirrors
// what stock records in Recently Played: a multi-disc game's .m3u, a
// cue-sheet folder's .cue, otherwise the game or pak itself.
static int favoriteFor(const NUIEntry* entry, char* out, size_t size) {
	if (!entry) return 0;
	const char* path = host->entry_path(entry);
	if (!relative(path)) return 0;

	switch (host->entry_type(entry)) {
		case NUI_ENTRY_PAK:
			snprintf(out, size, "%s", path);
			return 1;
		case NUI_ENTRY_DIR:
			// a folder that launches a game when opened
			return fileNamedAfter(path, ".m3u", out, size) || fileNamedAfter(path, ".cue", out, size);
		case NUI_ENTRY_ROM: {
			// a disc of a multi-disc game: the game's .m3u, in the disc's folder
			char dir[MAX_PATH_LEN];
			snprintf(dir, sizeof(dir), "%s", path);
			char* slash = strrchr(dir, '/');
			if (slash) {
				*slash = '\0';
				if (!endsWith(path, ".m3u") && fileNamedAfter(dir, ".m3u", out, size)) return 1;
			}
			snprintf(out, size, "%s", path);
			return 1;
		}
	}
	return 0;
}

///////////////////////////////////////
// the toggle

static int canToggle(void* data) {
	(void)data;
	char path[MAX_PATH_LEN * 2];
	return favoriteFor(host->selected_entry(), path, sizeof(path));
}

static void toggle(void* data) {
	(void)data;
	char path[MAX_PATH_LEN * 2];
	if (!favoriteFor(host->selected_entry(), path, sizeof(path))) return;
	const char* rel = relative(path);

	load();
	char message[MAX_PATH_LEN + 32];
	char shown[160];
	int i = indexOf(rel);
	if (i >= 0) {
		memmove(&favorites[i], &favorites[i + 1], (size_t)(count - i - 1) * MAX_PATH_LEN);
		count--;
		snprintf(message, sizeof(message), "removed %s", rel);
		snprintf(shown, sizeof(shown), "Removed from %s", folder_name);
	}
	else if (count < MAX_FAVORITES) {
		// at the end, like adding to any collection, so a hand-ordered list keeps its order
		snprintf(favorites[count++], MAX_PATH_LEN, "%s", rel);
		snprintf(message, sizeof(message), "added %s", rel);
		snprintf(shown, sizeof(shown), "Added to %s", folder_name);
	}
	else {
		say("favorites are full");
		snprintf(shown, sizeof(shown), "%s is full", folder_name);
		if (NUI_HOST_HAS(host, toast)) host->toast(shown);
		return;
	}
	if (save() == 0) say(message);
	else snprintf(shown, sizeof(shown), "Couldn't save %s", folder_name);
	if (NUI_HOST_HAS(host, toast)) host->toast(shown);
	host->request_redraw();
}

///////////////////////////////////////
// hooks

static void rootEntries(NUIList* root) {
	host->list_push(root, host->entry_new(folder_path, NUI_ENTRY_DIR, folder_name));
}

static NUIList* virtualDir(const char* path) {
	if (strcmp(path, folder_path) != 0) return NULL;

	load();
	NUIList* list = host->list_new();
	for (int i = 0; i < count; i++) {
		char full[MAX_PATH_LEN * 2];
		snprintf(full, sizeof(full), "%s%.511s", host->sdcard_path, favorites[i]);
		if (!exists(full)) continue; // keep it in the file in case the card comes back
		int type = endsWith(full, ".pak") ? NUI_ENTRY_PAK : NUI_ENTRY_ROM;
		host->list_push(list, host->entry_new(full, type, NULL));
	}
	return list;
}

// True when dir holds nothing but dot-entries such as .media.
static int onlyHidden(const char* dir) {
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	struct dirent* dp;
	int empty = 1;
	while ((dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] != '.') {
			empty = 0;
			break;
		}
	}
	closedir(dh);
	return empty;
}

// Hides the art-only Collections/Favorites/ folder once the collection
// file exists, so Collections doesn't list Favorites twice.
static int filter(const NUIEntry* entry, const char* dir_path) {
	if (!dir_path || strcmp(dir_path, collections_dir) != 0) return 1;
	if (host->entry_type(entry) != NUI_ENTRY_DIR) return 1;
	if (strcmp(host->entry_path(entry), art_dir) != 0) return 1;
	return !(exists(list_path) && onlyHidden(art_dir));
}

// The art folder's background for the Favorites folder on the root and for
// the collection in Collections.
static int artPath(const NUIEntry* entry, int kind, char* out, size_t size) {
	if (kind != NUI_ART_FOLDER_BG) return 0;
	const char* path = host->entry_path(entry);
	if (strcmp(path, folder_path) != 0 && strcmp(path, list_path) != 0) return 0;
	char bg[sizeof(art_dir) + 16];
	snprintf(bg, sizeof(bg), "%s/.media/bg.png", art_dir);
	if (!exists(bg)) return 0;
	snprintf(out, size, "%s", bg);
	return 1;
}

static int init(const NUIHost* h) {
	host = h;
	const char* collection = host->cfg_get(ID, "collection", "Favorites");
	if (!collection[0] || strchr(collection, '/')) collection = "Favorites";
	snprintf(folder_name, sizeof(folder_name), "%s", collection);
	snprintf(folder_path, sizeof(folder_path), "%s/Favorites", host->sdcard_path);
	snprintf(collections_dir, sizeof(collections_dir), "%s/Collections", host->sdcard_path);
	snprintf(list_path, sizeof(list_path), "%s/%s.txt", collections_dir, folder_name);
	snprintf(art_dir, sizeof(art_dir), "%s/%s", collections_dir, folder_name);
	migrate();

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
	.filter = filter,
	.art_path = artPath,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
