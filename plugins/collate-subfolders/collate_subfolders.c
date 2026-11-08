// Collate subfolders: in a system stock lists from several folders, such
// as "Game Boy (GB)" and "Game Boy (MGB)", same-name subfolders show once,
// with the games of all of them together.
//
// Stock lists a collated system by reading every folder in Roms whose name
// matches up to its last "(", so a Hacks folder in both shows twice. Here
// filter keeps only the first of them, by system folder name order, and
// virtual_dir lists the kept one with the games and folders of every one,
// merging their same-name subfolders the same way, one level at a time.
//
// Folders are matched by the name stock shows for them, ignoring case,
// spacing and punctuation, as genre-view matches genres. In the system's
// own list that's by the map.txt of the system folder the list was opened
// from, which stock uses for every collated folder; below it, each folder
// names its own entries. Paks and game folders (a .cue or .m3u named after
// the folder) are never merged.
//
// A system that isn't collated, or a folder only one collated folder has,
// is left to stock. The folders a system list was read into are kept for
// the frame the list is built in, and everything else is read from the
// card as it's listed.
//
// Back from a game in a folder filter hid, stock can't open that folder
// again, so the plugin opens the merged folders the game is in and
// selects it, from the game's path kept in /tmp while it runs.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "collate_model.h"
#include "mosaic_plugin.h"

#define ID "collate-subfolders"
#define MAX_PATH_LEN 512
#ifndef CS_LAST_FILE
#define CS_LAST_FILE "/tmp/mosaic-collate-subfolders.last" // the game launched, for post_return
#endif

static const MosaicHost* host;
static char roms_path[MAX_PATH_LEN]; // <sd>/Roms
// the collated system list last built, whose map.txt names its folders
static char console_path[MAX_PATH_LEN];
static unsigned long frame = 0;

// what filter read for the list it's filtering
static GVGroups shown;
static char shown_path[MAX_PATH_LEN];
static unsigned long shown_frame = 0;

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
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

static void loadMap(const char* dir, GVMap* map) {
	char path[MAX_PATH_LEN + 16];
	snprintf(path, sizeof(path), "%s/map.txt", dir);
	GV_mapParse(map, readFile(path));
}

// A folder that launches a game when opened: it holds a .cue or .m3u
// named after itself.
static int isGameFolder(const char* path, const char* name) {
	char file[MAX_PATH_LEN * 2];
	if (snprintf(file, sizeof(file), "%s/%s.cue", path, name) < (int)sizeof(file) && exists(file)) return 1;
	return snprintf(file, sizeof(file), "%s/%s.m3u", path, name) < (int)sizeof(file) && exists(file);
}

typedef struct File {
	char* name;
	int dir;
} File;

static int compareFiles(const void* a, const void* b) {
	return strcmp(((const File*)a)->name, ((const File*)b)->name);
}

// A folder's files, without those stock hides, or only its folders with
// dirs_only, in name order, so the first of two same-name folders is
// always the same one. Returns how many; free them with freeFiles.
static int readFiles(const char* dir, int dirs_only, File** out) {
	*out = NULL;
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	File* files = NULL;
	int count = 0, cap = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (GV_hidden(dp->d_name)) continue;
		int is_dir = dp->d_type == DT_DIR;
		if (dp->d_type == DT_UNKNOWN) {
			char path[MAX_PATH_LEN];
			struct stat st;
			is_dir = snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) < (int)sizeof(path)
				&& stat(path, &st) == 0 && S_ISDIR(st.st_mode);
		}
		if (dirs_only && !is_dir) continue;
		if (count == cap) {
			int next = cap ? cap * 2 : 32;
			File* bigger = realloc(files, sizeof(File) * (size_t)next);
			if (!bigger) break;
			files = bigger;
			cap = next;
		}
		if ((files[count].name = strdup(dp->d_name)) != NULL) files[count++].dir = is_dir;
	}
	closedir(dh);
	if (count > 1) qsort(files, (size_t)count, sizeof(File), compareFiles);
	*out = files;
	return count;
}

static void freeFiles(File* files, int count) {
	for (int i = 0; i < count; i++) free(files[i].name);
	free(files);
}

// Reads a folder in a system, naming its entries by map, or by its own
// map.txt when map is NULL. Its plain subfolders go into folders; with
// items, its games, paks and game folders go there.
static void readFolder(const char* dir, const char* system, const GVMap* map, GVGroups* folders, GVItems* items) {
	GVMap own;
	memset(&own, 0, sizeof(own));
	if (!map) {
		loadMap(dir, &own);
		map = &own;
	}

	File* files;
	int count = readFiles(dir, !items, &files);
	for (int i = 0; i < count; i++) {
		const char* file = files[i].name;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, file) >= (int)sizeof(path)) continue;
		char name[GV_NAME_LEN];
		if (!GV_shownName(map, file, name, sizeof(name))) continue;

		int type = MOSAIC_ENTRY_ROM;
		if (files[i].dir) {
			if (endsWith(file, ".pak")) type = MOSAIC_ENTRY_PAK;
			else if (isGameFolder(path, file)) type = MOSAIC_ENTRY_DIR;
			else {
				// an alias with a slash can't be a folder's name here
				if (GV_groupsAdd(folders, name, path, system) < 0 && strchr(name, '/')) {
					GV_cleanName(file, name, sizeof(name));
					GV_groupsAdd(folders, name, path, system);
				}
				continue;
			}
		}
		if (items) GV_itemsAdd(items, path, name, type);
	}
	freeFiles(files, count);
	GV_mapFree(&own);
}

// The subfolders of every system folder collated with system, named by
// the map.txt in map_dir, into groups. Returns how many system folders
// that is: 2 or more when system is collated.
static int systemFolders(const char* system, const char* map_dir, GVGroups* groups) {
	GV_groupsInit(groups);
	File* systems;
	int count = readFiles(roms_path, 1, &systems);
	int collated = 0;
	for (int i = 0; i < count; i++) {
		if (CS_collates(systems[i].name, system)) systems[collated++] = systems[i];
		else free(systems[i].name);
	}

	if (collated > 1) {
		GVMap map;
		loadMap(map_dir, &map);
		for (int i = 0; i < collated; i++) {
			char path[MAX_PATH_LEN];
			if (snprintf(path, sizeof(path), "%s/%s", roms_path, systems[i].name) < (int)sizeof(path)) {
				readFolder(path, systems[i].name, &map, groups, NULL);
			}
		}
		GV_mapFree(&map);
	}
	freeFiles(systems, collated);
	return collated;
}

///////////////////////////////////////
// hooks

// In a collated system's list, hides every folder but the first of those
// sharing a name.
static int filter(const MosaicEntry* entry, const char* dir_path) {
	if (host->entry_type(entry) != MOSAIC_ENTRY_DIR) return 1;
	char system[GV_NAME_LEN];
	if (!CS_consoleDir(roms_path, dir_path, system, sizeof(system))) return 1;

	if (shown_frame != frame || strcmp(shown_path, dir_path) != 0) {
		GV_groupsFree(&shown);
		if (systemFolders(system, dir_path, &shown) > 1) snprintf(console_path, sizeof(console_path), "%s", dir_path);
		snprintf(shown_path, sizeof(shown_path), "%s", dir_path);
		shown_frame = frame;
	}
	return CS_shown(&shown, host->entry_path(entry));
}

// Walks a folder below a collated system folder down from the system, a
// level at a time: on success, level holds the merged folder it's part of
// at *index, and with shown, each level's folder as the list above it
// shows it (the first of those merged). Returns how many levels down it
// is, or -1 for a folder that isn't below a collated system folder.
static int resolve(const char* path, GVGroups* level, int* index, char shown[][MAX_PATH_LEN]) {
	GV_groupsInit(level);
	char system[GV_NAME_LEN];
	if (!GV_systemOf(roms_path, path, system, sizeof(system))) return -1;
	char system_path[MAX_PATH_LEN];
	if (snprintf(system_path, sizeof(system_path), "%s/%s", roms_path, system) >= (int)sizeof(system_path)) return -1;
	char names[GV_MAX_DEPTH][GV_NAME_LEN];
	int depth = GV_pathNames(system_path, path, names, GV_MAX_DEPTH);
	if (depth < 1) return -1;

	// named as the system's list named them, when it's this system
	char console[GV_NAME_LEN];
	const char* map_dir = system_path;
	if (CS_consoleDir(roms_path, console_path, console, sizeof(console)) && CS_collates(console, system)) map_dir = console_path;
	if (systemFolders(system, map_dir, level) < 2) {
		GV_groupsFree(level);
		return -1;
	}

	// the folder on the card at each level of path
	char at[MAX_PATH_LEN];
	int i = snprintf(at, sizeof(at), "%s/%s", system_path, names[0]) < (int)sizeof(at) ? CS_find(level, at) : -1;
	for (int d = 1; i >= 0 && d <= depth; d++) {
		if (shown) snprintf(shown[d - 1], MAX_PATH_LEN, "%s", level->items[i].sources[0].path);
		if (d == depth) break;
		GVGroups next;
		GV_groupsInit(&next);
		const GVGroup* group = &level->items[i];
		for (int k = 0; k < group->count; k++) readFolder(group->sources[k].path, group->sources[k].system, NULL, &next, NULL);
		GV_groupsFree(level);
		*level = next;
		size_t len = strlen(at);
		i = snprintf(at + len, sizeof(at) - len, "/%s", names[d]) < (int)(sizeof(at) - len) ? CS_find(level, at) : -1;
	}
	if (i < 0) {
		GV_groupsFree(level);
		return -1;
	}
	*index = i;
	return depth;
}

// A folder that's merged lists the games and folders of every folder it
// stands for. Anything else is left to stock.
static MosaicList* virtualDir(const char* path) {
	GVGroups level;
	int i;
	if (resolve(path, &level, &i, NULL) < 0) return NULL;
	if (level.items[i].count < 2) {
		GV_groupsFree(&level);
		return NULL;
	}

	// games and folders together, by name, as stock lists a folder
	GVGroups folders;
	GVItems items;
	GV_groupsInit(&folders);
	GV_itemsInit(&items);
	const GVGroup* group = &level.items[i];
	for (int k = 0; k < group->count; k++) readFolder(group->sources[k].path, group->sources[k].system, NULL, &folders, &items);
	for (int k = 0; k < folders.count; k++) {
		GV_itemsAdd(&items, folders.items[k].sources[0].path, folders.items[k].name, MOSAIC_ENTRY_DIR);
	}
	GV_itemsSort(&items);
	MosaicList* list = host->list_new();
	for (int k = 0; k < items.count; k++) {
		host->list_push(list, host->entry_new(items.items[k].path, items.items[k].type, items.items[k].name));
	}
	GV_itemsFree(&items);
	GV_groupsFree(&folders);
	GV_groupsFree(&level);
	return list;
}

///////////////////////////////////////
// coming back from a game

// The game is kept here from pre_launch, as the launcher exits while it
// runs, for post_return.
static int restoring = 0;
static char restore_path[MAX_PATH_LEN];

// After a game, stock opens the folders the game is in again, but a
// folder of it filter hid isn't there to open, so it stops in the
// system's list. This opens the merged folders the game is in from there,
// and selects it.
static void restore(void) {
	if (!MOSAIC_HOST_HAS(host, open_path) || !MOSAIC_HOST_HAS(host, current_list) || !MOSAIC_HOST_HAS(host, select_index)) return;
	const char* current = host->current_path ? host->current_path() : NULL;
	char console[GV_NAME_LEN], system[GV_NAME_LEN];
	if (!CS_consoleDir(roms_path, current, console, sizeof(console))) return;
	if (!GV_systemOf(roms_path, restore_path, system, sizeof(system)) || !CS_collates(console, system)) return;

	char folder[MAX_PATH_LEN];
	snprintf(folder, sizeof(folder), "%s", restore_path);
	char* slash = strrchr(folder, '/');
	if (!slash) return;
	*slash = '\0';
	char shown_paths[GV_MAX_DEPTH][MAX_PATH_LEN];
	GVGroups level;
	int i;
	int depth = resolve(folder, &level, &i, shown_paths);
	if (depth < 0) return;
	GV_groupsFree(&level);

	for (int d = 0; d < depth; d++) {
		if (!host->open_path(shown_paths[d], MOSAIC_ENTRY_DIR)) return;
	}
	const MosaicList* list = host->current_list();
	int count = list ? host->list_count(list) : 0;
	for (int k = 0; k < count; k++) {
		if (!strcmp(host->entry_path(host->list_get(list, k)), restore_path)) {
			host->select_index(k);
			break;
		}
	}
}

static int preLaunch(const MosaicLaunch* launch) {
	unlink(CS_LAST_FILE);
	char system[GV_NAME_LEN];
	if (!launch->path || !GV_systemOf(roms_path, launch->path, system, sizeof(system))) return MOSAIC_LAUNCH_CONTINUE;
	FILE* f = fopen(CS_LAST_FILE, "w");
	if (f) {
		fputs(launch->path, f);
		fclose(f);
	}
	return MOSAIC_LAUNCH_CONTINUE;
}

static void postReturn(void) {
	FILE* f = fopen(CS_LAST_FILE, "r");
	if (!f) return;
	restoring = fgets(restore_path, sizeof(restore_path), f) != NULL && restore_path[0];
	fclose(f);
	unlink(CS_LAST_FILE);
}

static void tick(unsigned long now) {
	(void)now;
	frame++;
	// the first frame, once stock has opened the folders it could
	if (restoring) {
		restoring = 0;
		restore();
	}
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	console_path[0] = '\0';
	shown_path[0] = '\0';
	GV_groupsInit(&shown);
	frame = 0;
	shown_frame = 0;
	return 0;
}

static void quit(void) {
	GV_groupsFree(&shown);
	shown_path[0] = '\0';
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.virtual_dir = virtualDir,
	.filter = filter,
	.tick = tick,
	.pre_launch = preLaunch,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
