// All games: the root is one flat list of every game on the card, across
// every system, named as each system's list names them (map.txt aliases
// included), with a Systems folder first for the consoles.
//
// The card is scanned once per launcher start, the first time the root is
// listed. After that each listing only checks the folders the scan read
// for changes, and scans again if one changed. Subfolders of a system are
// flattened, two levels deep; a game folder (one holding a .cue or .m3u
// named after itself) is listed as that file, so it launches as stock's
// folder would.
//
// Other plugins' root folders join the root and, with Systems, stay above
// the games. When games from different systems share a name, each shows
// its system's tag, eg. "Tetris (GB)" and "Tetris (GBC)"; the "tags"
// setting turns that off. Recents, Collections and Tools stay on the quick
// menu.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "games_model.h"
#include "nextui_plugin.h"

#define ID "all-games"
#define MAX_PATH_LEN 512
#define MAX_DEPTH 2 // subfolders below a system folder that are scanned

static const NUIHost* host;
static char roms_path[MAX_PATH_LEN]; // <sd>/Roms
static AGIndex index_;
static int scanned = 0;
static int replaced = 0; // the last root listing was ours
static int tags = 1;
static int placing = 0; // back from a game: the selection may need placing

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static int isRoot(const char* path) {
	return path && !strcmp(path, host->sdcard_path);
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
	AG_emuTag(system, emu, sizeof(emu));
	char path[MAX_PATH_LEN * 2];
	if (snprintf(path, sizeof(path), "%s/.system/%s/paks/Emus/%s.pak/launch.sh", host->sdcard_path, host->platform, emu) < (int)sizeof(path)
			&& exists(path)) return 1;
	return snprintf(path, sizeof(path), "%s/Emus/%s/%s.pak/launch.sh", host->sdcard_path, host->platform, emu) < (int)sizeof(path)
		&& exists(path);
}

// What opening a game folder launches: its <name>.cue, else its
// <name>.m3u, written to out. 0 for a folder that isn't a game.
static int gameFolderLaunch(const char* path, const char* name, char* out, size_t size) {
	if (snprintf(out, size, "%s/%s.cue", path, name) < (int)size && exists(out)) return 1;
	return snprintf(out, size, "%s/%s.m3u", path, name) < (int)size && exists(out);
}

static int isDirEntry(const struct dirent* dp, const char* path) {
	if (dp->d_type == DT_DIR) return 1;
	if (dp->d_type != DT_UNKNOWN) return 0;
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void addFolder(const char* dir) {
	struct stat st;
	AG_indexAddFolder(&index_, dir, stat(dir, &st) == 0 ? st.st_mtime : 0);
}

static void scanDir(const char* dir, int depth) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	addFolder(dir);

	char map_path[MAX_PATH_LEN + 16];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", dir);
	AGMap map;
	AG_mapParse(&map, readFile(map_path));

	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (AG_hidden(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;

		char launch[MAX_PATH_LEN * 2];
		const char* game = path;
		int type = NUI_ENTRY_ROM;
		if (isDirEntry(dp, path)) {
			if (endsWith(dp->d_name, ".pak")) type = NUI_ENTRY_PAK;
			else if (gameFolderLaunch(path, dp->d_name, launch, sizeof(launch))) game = launch;
			else {
				if (depth < MAX_DEPTH) scanDir(path, depth + 1);
				continue;
			}
		}

		char name[AG_NAME_LEN];
		const char* alias = AG_mapGet(&map, dp->d_name);
		if (alias) snprintf(name, sizeof(name), "%s", alias);
		else AG_cleanName(dp->d_name, name, sizeof(name));
		if (AG_hidden(name)) continue; // map.txt can hide a game
		AG_indexAdd(&index_, game, name, type);
	}
	closedir(dh);
	AG_mapFree(&map);
}

static void scan(void) {
	AG_indexFree(&index_);
	scanned = 1;

	DIR* dh = opendir(roms_path);
	if (!dh) return;
	addFolder(roms_path);
	int systems = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (AG_hidden(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", roms_path, dp->d_name) >= (int)sizeof(path)) continue;
		if (!isDirEntry(dp, path) || !hasEmulator(dp->d_name)) continue;
		scanDir(path, 0);
		systems++;
	}
	closedir(dh);
	AG_indexSort(&index_);

	char message[96];
	snprintf(message, sizeof(message), "%d games in %d systems", index_.game_count, systems);
	host->log(ID, message);
}

// True when a folder the scan read changed since, or is gone.
static int cardChanged(void) {
	for (int i = 0; i < index_.folder_count; i++) {
		struct stat st;
		if (stat(index_.folders[i].path, &st) != 0 || st.st_mtime != index_.folders[i].mtime) return 1;
	}
	return 0;
}

///////////////////////////////////////
// hooks

static NUIList* rootReplace(void) {
	replaced = 0;
	if (!scanned || cardChanged()) scan();
	if (index_.game_count == 0) return NULL; // stock's root, so Tools and the rest stay in reach

	NUIList* root = host->list_new();
	host->list_push(root, host->entry_new(roms_path, NUI_ENTRY_DIR, AG_SYSTEMS_NAME));
	for (int i = 0; i < index_.game_count; i++) {
		const AGGame* game = &index_.games[i];
		host->list_push(root, host->entry_new(game->path, game->type, game->name));
	}
	replaced = 1;
	return root;
}

static int sortCompare(const NUIEntry* a, const NUIEntry* b, const char* dir_path) {
	if (!replaced || !isRoot(dir_path)) return 0;
	return AG_rootCompare(roms_path, host->entry_path(a), host->entry_path(b));
}

// Games from different systems that ended up with the same name get their
// system's tag. Another plugin that already told them apart leaves
// nothing to do here.
static void listReady(const NUIList* list, const char* dir_path) {
	if (!tags || !replaced || !isRoot(dir_path) || !NUI_HOST_HAS(host, entry_set_name)) return;
	int count = host->list_count(list);
	if (count < 2) return;

	NUIEntry** games = malloc(sizeof(NUIEntry*) * (size_t)count);
	const char** names = malloc(sizeof(char*) * (size_t)count);
	char (*systems)[AG_NAME_LEN] = malloc(sizeof(*systems) * (size_t)count);
	const char** system_ptrs = malloc(sizeof(char*) * (size_t)count);
	int* tagged = malloc(sizeof(int) * (size_t)count);
	int n = 0;
	if (games && names && systems && system_ptrs && tagged) {
		for (int i = 0; i < count; i++) {
			NUIEntry* entry = host->list_get(list, i);
			char system[AG_NAME_LEN];
			if (!entry || !AG_systemOf(roms_path, host->entry_path(entry), system, sizeof(system))) continue;
			games[n] = entry;
			names[n] = host->entry_name(entry);
			AG_emuTag(system, systems[n], sizeof(systems[n]));
			system_ptrs[n] = systems[n];
			n++;
		}
		if (AG_collisions(names, system_ptrs, n, tagged) > 0) {
			for (int i = 0; i < n; i++) {
				if (!tagged[i]) continue;
				char name[AG_NAME_LEN * 2 + 4];
				snprintf(name, sizeof(name), "%s (%s)", names[i], systems[i]);
				host->entry_set_name(games[i], name);
			}
		}
	}
	free(games);
	free(names);
	free(systems);
	free(system_ptrs);
	free(tagged);
}

// A game folder is listed as its .cue or .m3u; its art is the folder's.
static int artPath(const NUIEntry* entry, int kind, char* out, size_t size) {
	if (kind != NUI_ART_THUMBNAIL || !replaced || !isRoot(host->current_path())) return 0;
	const char* path = host->entry_path(entry);
	if (!AG_systemOf(roms_path, path, NULL, 0)) return 0;
	return AG_folderArt(path, out, size);
}

static void postReturn(void) {
	placing = 1;
}

// Back from a game that's a folder with only an .m3u, stock remembers the
// first disc, which isn't on the root, so the selection lands on another
// game. The game just played is the newest recent: select it instead.
static void tick(unsigned long now) {
	(void)now;
	if (!placing) return;
	if (!NUI_HOST_HAS(host, current_list) || !NUI_HOST_HAS(host, select_index) || !NUI_HOST_HAS(host, recent_path)) {
		placing = 0;
		return;
	}
	const NUIList* list = host->current_list();
	if (!list) return; // the lists aren't there yet
	placing = 0;
	if (!replaced || !isRoot(host->current_path())) return;

	const char* played = host->recent_path(0);
	if (!played || !endsWith(played, ".m3u")) return;
	int count = host->list_count(list);
	for (int i = 0; i < count; i++) {
		const char* path = host->entry_path(host->list_get(list, i));
		if (path && !strcmp(path, played)) {
			host->select_index(i);
			return;
		}
	}
}

static int init(const NUIHost* h) {
	host = h;
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	tags = strcmp(host->cfg_get(ID, "tags", "on"), "off") != 0;
	AG_indexInit(&index_);
	scanned = 0;
	replaced = 0;
	placing = 0;
	return 0;
}

static void quit(void) {
	AG_indexFree(&index_);
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.post_return = postReturn,
	.tick = tick,
	.art_path = artPath,
	.root_replace = rootReplace,
	.sort_compare = sortCompare,
	.list_ready = listReady,
	.root_with_entries = 1, // other plugins' root folders join the list
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
