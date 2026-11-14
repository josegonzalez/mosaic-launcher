// All games: the root is one flat list of every game on the card, across
// every system, named as each system's list names them (map.txt aliases
// included), with a Systems folder first for the consoles.
//
// The games come from the launcher's card index (index_count and the
// rest), only systems with an emulator, as stock lists them. Subfolders of
// a system are flattened, two levels deep; a game folder (one holding a
// .cue or .m3u named after itself) is listed as that file, so it launches
// as stock's folder would. The list is made again, and the lists reloaded,
// each time the index changes (launcher.index_ready). Until the index is
// first ready, as on the first start with a card the launcher hasn't read,
// the root is stock's.
//
// Other plugins' root folders join the root and, with Systems, stay above
// the games. When games from different systems share a name, each shows
// its system's tag, eg. "Tetris (GB)" and "Tetris (GBC)"; the "tags"
// setting turns that off. Recents, Collections and Tools stay on the quick
// menu.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "games_model.h"
#include "mosaic_plugin.h"

#define ID "all-games"
#define MAX_PATH_LEN 512
#define MAX_DEPTH 2 // subfolders below a system folder that are listed

static const MosaicHost* host;
static char roms_path[MAX_PATH_LEN]; // <sd>/Roms
static AGIndex index_;
static int listed = 0; // index_ holds the index's games
static int replaced = 0; // the last root listing was ours
static int tags = 1;
static int placing = 0; // back from a game: the selection may need placing

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static int isRoot(const char* path) {
	return path && !strcmp(path, host->sdcard_path);
}

///////////////////////////////////////
// the games, from the card index

// Lists the games the index has. 0, or -1 while it isn't ready.
static int listGames(void) {
	AG_indexFree(&index_);
	if (!MOSAIC_HOST_HAS(host, index_refresh) || host->index_count(NULL) == MOSAIC_INDEX_NOT_READY) return -1;
	int count = host->index_count(NULL);
	int systems = 0;
	const char* last_system = "";
	for (int i = 0; i < count; i++) {
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_entry(NULL, i, &e) != 0) break;
		if (e.kind != MOSAIC_INDEX_GAME || !e.emulator || e.depth > MAX_DEPTH) continue;
		// a game folder launches as its .cue or .m3u
		if (AG_indexAdd(&index_, e.launch, e.name, e.type == MOSAIC_ENTRY_PAK ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM) != 0) break;
		if (strcmp(e.system, last_system) != 0) systems++;
		last_system = e.system;
	}
	AG_indexSort(&index_);
	listed = 1;

	char message[96];
	snprintf(message, sizeof(message), "%d games in %d systems", index_.game_count, systems);
	host->log(ID, message);
	return 0;
}

// The index changed: list the games again, on the root if it shows.
static void indexReady(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	listed = 0;
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
}

///////////////////////////////////////
// hooks

static MosaicList* rootReplace(void) {
	replaced = 0;
	if (!listed && listGames() != 0) return NULL; // stock's root until the index is ready
	if (index_.game_count == 0) return NULL; // stock's root, so Tools and the rest stay in reach

	MosaicList* root = host->list_new();
	host->list_push(root, host->entry_new(roms_path, MOSAIC_ENTRY_DIR, tr("Systems")));
	for (int i = 0; i < index_.game_count; i++) {
		const AGGame* game = &index_.games[i];
		host->list_push(root, host->entry_new(game->path, game->type, game->name));
	}
	replaced = 1;
	return root;
}

static int sortCompare(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	if (!replaced || !isRoot(dir_path)) return 0;
	return AG_rootCompare(roms_path, host->entry_path(a), host->entry_path(b));
}

// Games from different systems that ended up with the same name get their
// system's tag. Another plugin that already told them apart leaves
// nothing to do here.
static void listReady(const MosaicList* list, const char* dir_path) {
	if (!tags || !replaced || !isRoot(dir_path) || !MOSAIC_HOST_HAS(host, entry_set_name)) return;
	int count = host->list_count(list);
	if (count < 2) return;

	MosaicEntry** games = malloc(sizeof(MosaicEntry*) * (size_t)count);
	const char** names = malloc(sizeof(char*) * (size_t)count);
	char (*systems)[AG_NAME_LEN] = malloc(sizeof(*systems) * (size_t)count);
	const char** system_ptrs = malloc(sizeof(char*) * (size_t)count);
	int* tagged = malloc(sizeof(int) * (size_t)count);
	int n = 0;
	if (games && names && systems && system_ptrs && tagged) {
		for (int i = 0; i < count; i++) {
			MosaicEntry* entry = host->list_get(list, i);
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
static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	if (kind != MOSAIC_ART_THUMBNAIL || !replaced || !isRoot(host->current_path())) return 0;
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
	if (!MOSAIC_HOST_HAS(host, current_list) || !MOSAIC_HOST_HAS(host, select_index) || !MOSAIC_HOST_HAS(host, recent_path)) {
		placing = 0;
		return;
	}
	const MosaicList* list = host->current_list();
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

static int init(const MosaicHost* h) {
	host = h;
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	tags = strcmp(host->cfg_get("tags", "on"), "off") != 0;
	AG_indexInit(&index_);
	listed = 0;
	if (!MOSAIC_HOST_HAS(host, index_refresh)) host->log(ID, "this launcher has no card index; the root stays stock's");
	else {
		host->subscribe("launcher.index_ready", indexReady, NULL);
		host->index_count(NULL); // starts reading the card, so it's ready by the first listing
	}
	replaced = 0;
	placing = 0;
	return 0;
}

static void quit(void) {
	AG_indexFree(&index_);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
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

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
