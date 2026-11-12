// Collections plus: collections on the root in the user's order, with their
// art, and options for how collections list.
//
// The "promote" setting names the collections to put on the root, or "*"
// for all of them; they go after the consoles, as other plugins' root
// folders do, in the "order" setting's order, then promote's, then by name.
// "order" also orders the Collections folder. The "hide_folder" setting
// drops the Collections folder from the root (the quick menu still has it).
//
// Inside a collection, the "file_order" setting keeps the collection file's
// own order, which stock gives up for names when Collections/map.txt
// renames a game. The "own_map" setting names a collection's games from its
// own map, <dir>/.media/<name>.map.txt, or for a collection in a folder
// below Collections, that folder's map.txt, before stock's shared one; a
// name starting with a dot hides the game, as in any map.txt. The "art"
// setting gives a collection's thumbnail and box from
// <dir>/.media/<name>.png or .jpg, so layouts that want a box show it too.
// The "hide_games" setting hides the quick menu's Games tile.

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "collections_model.h"

#define ID "collections-plus"
#define ROOT_MAX 128

static const MosaicHost* host;
static char collections_dir[CP_PATH_LEN]; // <sd>/Collections
static CPNames order;                     // the "order" setting
static CPNames promote;                   // the "promote" setting
static int hide_folder = 0;
static int file_order = 1;
static int own_map = 1;
static int art_on = 1;
static int hide_games = 0;

// The listing in progress: filter sees each entry first, then display_name
// and sort_compare, and list_ready ends it.
static char map_dir[CP_PATH_LEN];   // the collection own_names is for
static CPMap own_names;
static char order_dir[CP_PATH_LEN]; // the collection file_lines is for
static CPOrder file_lines;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[600];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int on(const char* key, const char* fallback) {
	return !strcmp(host->cfg_get(key, fallback), "on");
}

// The card-relative form of path, or NULL if it isn't on the card.
static const char* relative(const char* path) {
	size_t len = strlen(host->sdcard_path);
	if (!path || strncmp(path, host->sdcard_path, len) != 0 || path[len] != '/') return NULL;
	return path + len;
}

static const char* fileName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void forgetListing(void) {
	CP_mapFree(&own_names);
	map_dir[0] = '\0';
	CP_orderFree(&file_lines);
	order_dir[0] = '\0';
}

// The collection's own map, read once per listing.
static void loadOwnMap(const char* dir_path) {
	if (!strcmp(map_dir, dir_path)) return;
	CP_mapFree(&own_names);
	snprintf(map_dir, sizeof(map_dir), "%s", dir_path);
	char paths[2][CP_PATH_LEN];
	int n = CP_mapPaths(collections_dir, dir_path, paths, 2);
	for (int i = 0; i < n; i++) {
		if (CP_mapLoad(&own_names, paths[i])) break;
	}
}

///////////////////////////////////////
// the root

typedef struct Promoted {
	char path[CP_PATH_LEN];
	char name[CP_NAME_LEN]; // its map.txt alias, or "" for stock's name
	char key[CP_NAME_LEN];  // what it sorts by
	int order_rank;
	int promote_rank;
} Promoted;

static int byRank(const void* x, const void* y) {
	const Promoted* a = x;
	const Promoted* b = y;
	int r = CP_compareRanks(a->order_rank, b->order_rank);
	if (!r) r = CP_compareRanks(a->promote_rank, b->promote_rank);
	if (!r) r = strcasecmp(a->key, b->key);
	return r ? r : strcmp(a->path, b->path);
}

static int inList(const MosaicList* list, const char* path) {
	int count = host->list_count(list);
	for (int i = 0; i < count; i++) {
		if (!strcmp(host->entry_path(host->list_get(list, i)), path)) return 1;
	}
	return 0;
}

static void rootEntries(MosaicList* root) {
	if (!promote.all && promote.count == 0) return;
	DIR* dh = opendir(collections_dir);
	if (!dh) return;

	char shared_path[CP_PATH_LEN + 16];
	snprintf(shared_path, sizeof(shared_path), "%s/map.txt", collections_dir);
	CPMap shared;
	CP_mapInit(&shared);
	CP_mapLoad(&shared, shared_path);

	static Promoted found[ROOT_MAX];
	int count = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL && count < ROOT_MAX) {
		const char* file = dp->d_name;
		// what stock's Collections folder hides
		if (file[0] == '.' || !strcmp(file, "map.txt")) continue;
		Promoted* p = &found[count];
		if (snprintf(p->path, sizeof(p->path), "%s/%s", collections_dir, file) >= (int)sizeof(p->path)) continue;
		char base[CP_NAME_LEN];
		if (!CP_topCollection(collections_dir, p->path, base, sizeof(base)) || !isFile(p->path)) continue;
		const char* alias = CP_mapGet(&shared, file);
		if (alias && alias[0] == '.') continue; // hidden by the map
		snprintf(p->name, sizeof(p->name), "%s", alias ? alias : "");
		snprintf(p->key, sizeof(p->key), "%s", alias ? alias : base);
		p->promote_rank = CP_rank(&promote, base, alias);
		if (!promote.all && p->promote_rank < 0) continue;
		p->order_rank = CP_rank(&order, base, alias);
		if (inList(root, p->path)) continue; // stock promoted it, with no systems to show
		count++;
	}
	closedir(dh);
	CP_mapFree(&shared);

	qsort(found, (size_t)count, sizeof(Promoted), byRank);
	for (int i = 0; i < count; i++) {
		host->list_push(root, host->entry_new(found[i].path, MOSAIC_ENTRY_DIR, found[i].name[0] ? found[i].name : NULL));
	}
}

///////////////////////////////////////
// listing hooks

static int filter(const MosaicEntry* entry, const char* dir_path) {
	if (!dir_path) return 1;
	const char* path = host->entry_path(entry);
	if (hide_folder && !strcmp(dir_path, host->sdcard_path) && !strcmp(path, collections_dir)) return 0;
	if (!own_map || !CP_isCollection(collections_dir, dir_path)) {
		// display_name only renames inside a collection
		map_dir[0] = '\0';
		CP_mapFree(&own_names);
		return 1;
	}
	loadOwnMap(dir_path);
	const char* alias = CP_mapGet(&own_names, fileName(path));
	return !(alias && alias[0] == '.');
}

static int displayName(const MosaicEntry* entry, char* out, size_t size) {
	if (!own_map || !map_dir[0]) return 0;
	const char* alias = CP_mapGet(&own_names, fileName(host->entry_path(entry)));
	if (!alias || !alias[0] || alias[0] == '.') return 0;
	snprintf(out, size, "%s", alias);
	return 1;
}

// An entry's place in the "order" setting, by its name without ".txt" or
// the name it shows.
static int orderRank(const MosaicEntry* entry) {
	char base[CP_NAME_LEN];
	snprintf(base, sizeof(base), "%s", fileName(host->entry_path(entry)));
	size_t len = strlen(base);
	if (len > 4 && !strcmp(base + len - 4, ".txt")) base[len - 4] = '\0';
	return CP_rank(&order, base, host->entry_name(entry));
}

static int sortCompare(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	if (!dir_path) return 0;
	if (CP_isCollection(collections_dir, dir_path)) {
		if (!file_order) return 0;
		if (strcmp(order_dir, dir_path) != 0) {
			CP_orderFree(&file_lines);
			snprintf(order_dir, sizeof(order_dir), "%s", dir_path);
			CP_orderLoad(&file_lines, dir_path);
		}
		return CP_compareRanks(CP_orderIndex(&file_lines, relative(host->entry_path(a))),
			CP_orderIndex(&file_lines, relative(host->entry_path(b))));
	}
	// the Collections folder, and folders in it
	size_t len = strlen(collections_dir);
	if (order.count && !strncmp(dir_path, collections_dir, len) && (!dir_path[len] || dir_path[len] == '/')) {
		return CP_compareRanks(orderRank(a), orderRank(b));
	}
	return 0;
}

static void listReady(const MosaicList* list, const char* dir_path) {
	(void)list;
	(void)dir_path;
	forgetListing(); // read again for the next listing, in case they changed
}

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	if (!art_on || (kind != MOSAIC_ART_THUMBNAIL && kind != MOSAIC_ART_BOX)) return 0;
	if (host->entry_type(entry) != MOSAIC_ENTRY_DIR) return 0;
	const char* path = host->entry_path(entry);
	if (!CP_isCollection(collections_dir, path)) return 0;
	char paths[2][CP_PATH_LEN];
	int n = CP_artPaths(path, paths, 2);
	for (int i = 0; i < n; i++) {
		if (isFile(paths[i])) {
			snprintf(out, size, "%s", paths[i]);
			return 1;
		}
	}
	return 0;
}

///////////////////////////////////////

static void quit(void) {
	forgetListing();
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(collections_dir, sizeof(collections_dir), "%s/Collections", host->sdcard_path);
	CP_parseNames(host->cfg_get("order", ""), &order);
	order.all = 0; // "*" means nothing as an order
	CP_parseNames(host->cfg_get("promote", ""), &promote);
	hide_folder = on("hide_folder", "off");
	file_order = on("file_order", "on");
	own_map = on("own_map", "on");
	art_on = on("art", "on");
	hide_games = on("hide_games", "off");
	CP_mapInit(&own_names);
	CP_orderInit(&file_lines);
	map_dir[0] = order_dir[0] = '\0';

	if (hide_games) {
		if (!MOSAIC_HOST_HAS(host, hide_quick_tile) || host->hide_quick_tile("Games", 1) != 0) {
			say("couldn't hide the Games tile");
			hide_games = 0;
		}
	}
	say("order %i, promote %s%i, hide folder %s, file order %s, own map %s, art %s, hide games %s", order.count,
		promote.all ? "all " : "", promote.count, hide_folder ? "on" : "off", file_order ? "on" : "off",
		own_map ? "on" : "off", art_on ? "on" : "off", hide_games ? "on" : "off");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.filter = filter,
	.display_name = displayName,
	.art_path = artPath,
	.sort_compare = sortCompare,
	.list_ready = listReady,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
