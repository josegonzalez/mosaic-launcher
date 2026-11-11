// Genre view: a Genres folder on the root that merges same-name genre
// folders from every system into one list per genre.
//
// A genre folder is a subfolder of a system folder (one stock lists, with
// an emulator) that isn't a game folder (one holding a .cue or .m3u named
// after itself) or a pak. Folders are matched by the name stock shows for
// them, map.txt aliases included, ignoring case, spacing and punctuation,
// so "Shoot 'em up" in one system and "shoot em up" in another are one
// genre, named as the first system in name order names it. The "min"
// setting sets how many consoles a genre needs, and "skip" lists folder
// names that aren't genres.
//
// A genre lists the games from each system's folder together, by name.
// Subfolders inside genre folders are merged the same way, one level at a
// time. Games from different systems that share a name show their
// system's tag, eg. "Tetris (GB)" and "Tetris (GBC)"; the "tags" setting
// shows it on every game instead, or leaves it to stock.
//
// Nothing is cached: each listing reads the folders it needs from the card.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "genre_model.h"
#include "mosaic_plugin.h"

#define ID "genre-view"
#define FOLDER_NAME "Genres"
#define MAX_PATH_LEN 512

static const MosaicHost* host;
static char folder_path[MAX_PATH_LEN]; // <sd>/Genres
static char roms_path[MAX_PATH_LEN];   // <sd>/Roms
static int min_systems = 1;
static char skip[1024]; // folder names that aren't genres, comma-separated
static int tags = GV_TAGS_SHARED;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

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

static int isDirEntry(const struct dirent* dp, const char* path) {
	if (dp->d_type == DT_DIR) return 1;
	if (dp->d_type != DT_UNKNOWN) return 0;
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void loadMap(const char* dir, GVMap* map) {
	char path[MAX_PATH_LEN + 16];
	snprintf(path, sizeof(path), "%s/map.txt", dir);
	GV_mapParse(map, readFile(path));
}

///////////////////////////////////////
// the card

// True when a system folder has an emulator, as stock checks before
// listing it: the emulator is the tag in the folder's last parentheses.
static int hasEmulator(const char* system) {
	char emu[256];
	GV_emuTag(system, emu, sizeof(emu));
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

// Reads a folder in a system. Its plain subfolders go into folders, by
// the name they show as; with items, its games, paks and game folders go
// there.
static void readFolder(const char* dir, const char* system, GVGroups* folders, GVItems* items) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	GVMap map;
	loadMap(dir, &map);

	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (GV_hidden(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;
		int is_dir = isDirEntry(dp, path);
		if (!is_dir && !items) continue; // only folders wanted
		char name[GV_NAME_LEN];
		if (!GV_shownName(&map, dp->d_name, name, sizeof(name))) continue;

		int type = MOSAIC_ENTRY_ROM;
		if (is_dir) {
			if (endsWith(dp->d_name, ".pak")) type = MOSAIC_ENTRY_PAK;
			else if (isGameFolder(path, dp->d_name)) type = MOSAIC_ENTRY_DIR;
			else {
				// an alias with a slash can't be part of a path
				if (GV_groupsAdd(folders, name, path, system) < 0 && strchr(name, '/')) {
					GV_cleanName(dp->d_name, name, sizeof(name));
					GV_groupsAdd(folders, name, path, system);
				}
				continue;
			}
		}
		if (items) GV_itemsAdd(items, path, name, type);
	}
	closedir(dh);
	GV_mapFree(&map);
}

static int compareNames(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

// The genres on the card: every system's plain subfolders, merged by
// name, without those the settings leave out. Systems are read in name
// order, so a genre is named as the first system names it.
static void genres(GVGroups* out) {
	GV_groupsInit(out);
	DIR* dh = opendir(roms_path);
	if (!dh) return;
	char** systems = NULL;
	int count = 0, cap = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (GV_hidden(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", roms_path, dp->d_name) >= (int)sizeof(path)) continue;
		if (!isDirEntry(dp, path) || !hasEmulator(dp->d_name)) continue;
		if (count == cap) {
			int next = cap ? cap * 2 : 32;
			char** bigger = realloc(systems, sizeof(char*) * (size_t)next);
			if (!bigger) break;
			systems = bigger;
			cap = next;
		}
		if ((systems[count] = strdup(dp->d_name)) != NULL) count++;
	}
	closedir(dh);

	if (count > 1) qsort(systems, (size_t)count, sizeof(char*), compareNames);
	for (int i = 0; i < count; i++) {
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", roms_path, systems[i]) < (int)sizeof(path)) readFolder(path, systems[i], out, NULL);
		free(systems[i]);
	}
	free(systems);
	GV_groupsKeep(out, min_systems, skip);
	GV_groupsSort(out);
}

// The merged folder a path below the Genres folder names: on success,
// level holds it at *index. Returns 0, or -1 for a path that names none.
static int resolve(const char* path, GVGroups* level, int* index) {
	char names[GV_MAX_DEPTH][GV_NAME_LEN];
	int count = GV_pathNames(folder_path, path, names, GV_MAX_DEPTH);
	if (count < 1) return -1;

	genres(level);
	int i = GV_groupsFind(level, names[0]);
	for (int depth = 1; i >= 0 && depth < count; depth++) {
		GVGroups next;
		GV_groupsInit(&next);
		const GVGroup* group = &level->items[i];
		for (int k = 0; k < group->count; k++) readFolder(group->sources[k].path, group->sources[k].system, &next, NULL);
		GV_groupsFree(level);
		*level = next;
		i = GV_groupsFind(level, names[depth]);
	}
	if (i < 0) {
		GV_groupsFree(level);
		return -1;
	}
	*index = i;
	return 0;
}

static void pushFolders(MosaicList* list, const char* path, const GVGroups* folders) {
	for (int i = 0; i < folders->count; i++) {
		char dir[MAX_PATH_LEN + GV_NAME_LEN];
		if (snprintf(dir, sizeof(dir), "%s/%s", path, folders->items[i].name) >= (int)sizeof(dir)) continue;
		host->list_push(list, host->entry_new(dir, MOSAIC_ENTRY_DIR, folders->items[i].name));
	}
}

///////////////////////////////////////
// hooks

// Shown while the card has a genre.
static void rootEntries(MosaicList* root) {
	GVGroups found;
	genres(&found);
	if (found.count > 0) host->list_push(root, host->entry_new(folder_path, MOSAIC_ENTRY_DIR, tr("Genres")));
	GV_groupsFree(&found);
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, folder_path)) {
		MosaicList* list = host->list_new();
		GVGroups found;
		genres(&found);
		pushFolders(list, folder_path, &found);
		GV_groupsFree(&found);
		return list;
	}

	size_t len = strlen(folder_path);
	if (strncmp(path, folder_path, len) != 0 || path[len] != '/') return NULL;
	MosaicList* list = host->list_new();
	GVGroups level;
	int i;
	if (resolve(path, &level, &i) != 0) return list;

	// games and subfolders together, by name, as stock lists a folder
	GVGroups folders;
	GVItems items;
	GV_groupsInit(&folders);
	GV_itemsInit(&items);
	const GVGroup* group = &level.items[i];
	for (int k = 0; k < group->count; k++) readFolder(group->sources[k].path, group->sources[k].system, &folders, &items);
	for (int k = 0; k < folders.count; k++) {
		char dir[MAX_PATH_LEN + GV_NAME_LEN];
		if (snprintf(dir, sizeof(dir), "%s/%s", path, folders.items[k].name) >= (int)sizeof(dir)) continue;
		GV_itemsAdd(&items, dir, folders.items[k].name, MOSAIC_ENTRY_DIR);
	}
	GV_itemsSort(&items);
	for (int k = 0; k < items.count; k++) {
		host->list_push(list, host->entry_new(items.items[k].path, items.items[k].type, items.items[k].name));
	}
	GV_itemsFree(&items);
	GV_groupsFree(&folders);
	GV_groupsFree(&level);
	return list;
}

// Games in a genre get their system's tag, by the tags setting. Another
// plugin that already told them apart leaves nothing to do here.
static void listReady(const MosaicList* list, const char* dir_path) {
	size_t len = strlen(folder_path);
	if (tags == GV_TAGS_OFF || !dir_path || strncmp(dir_path, folder_path, len) != 0 || dir_path[len] != '/') return;
	if (!MOSAIC_HOST_HAS(host, entry_set_name)) return;
	int count = host->list_count(list);
	if (count < 1) return;

	MosaicEntry** entries = malloc(sizeof(MosaicEntry*) * (size_t)count);
	const char** names = malloc(sizeof(char*) * (size_t)count);
	char (*systems)[GV_NAME_LEN] = malloc(sizeof(*systems) * (size_t)count);
	const char** system_ptrs = malloc(sizeof(char*) * (size_t)count);
	int* tagged = malloc(sizeof(int) * (size_t)count);
	if (entries && names && systems && system_ptrs && tagged) {
		for (int i = 0; i < count; i++) {
			entries[i] = host->list_get(list, i);
			names[i] = host->entry_name(entries[i]);
			char system[GV_NAME_LEN];
			system_ptrs[i] = NULL;
			if (GV_systemOf(roms_path, host->entry_path(entries[i]), system, sizeof(system))) {
				GV_emuTag(system, systems[i], sizeof(systems[i]));
				system_ptrs[i] = systems[i];
			}
		}
		if (GV_tags(names, system_ptrs, count, tags, tagged) > 0) {
			for (int i = 0; i < count; i++) {
				if (!tagged[i]) continue;
				char name[GV_NAME_LEN * 2 + 4];
				snprintf(name, sizeof(name), "%s (%s)", names[i], systems[i]);
				host->entry_set_name(entries[i], name);
			}
		}
	}
	free(entries);
	free(names);
	free(systems);
	free(system_ptrs);
	free(tagged);
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/" FOLDER_NAME, host->sdcard_path);
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	min_systems = atoi(host->cfg_get(ID, "min", "1"));
	if (min_systems < 1) min_systems = 1;
	snprintf(skip, sizeof(skip), "%s", host->cfg_get(ID, "skip", ""));
	tags = GV_tagsMode(host->cfg_get(ID, "tags", "shared"));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.list_ready = listReady,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
