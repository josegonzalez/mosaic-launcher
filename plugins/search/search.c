// Search: type a query on the on-screen keyboard and see the games that
// match it, from every system, in a Search folder.
//
// A Search row on the root (the "root" setting) asks for a query when it's
// opened, and backs out again if the keyboard is cancelled. The
// search.open action asks from anywhere on the game list and opens the
// folder with the results; the "button" setting binds it to a button, and
// button-remap or an FN button (action:search.open) can too. With recent
// queries remembered (the "recents" setting), a list of them comes before
// the keyboard, with New search first.
//
// Games match by the name the game list shows for them (map.txt names
// included), with case, accents and punctuation folded away, and every
// word of the query in the name, in any order. The best matches come
// first: the name itself, then names starting with the query, then names
// with each word starting a word, then the rest, each by name.
//
// The card is scanned the way stock lists it the first time a search
// needs it, and again only when a folder it read changes. The query is
// kept in the plugin's cfg, so the folder lists the same results after
// the launcher restarts. Back from a game launched from the results,
// stock opens the game's own folder; the plugin backs out to the root and
// opens the results again with the game selected, from the game's path
// kept in /tmp while it runs.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "search_model.h"

#define ID "search"
#define FOLDER_NAME "Search"
#define MAX_PATH_LEN 512
#define MAX_DEPTH 3 // subfolders below a system folder that are scanned
#ifndef SE_LAST_FILE
#define SE_LAST_FILE "/tmp/mosaic-search.last" // the game launched from the results, for post_return
#endif

static const MosaicHost* host;
static char folder_path[MAX_PATH_LEN]; // <sd>/Search, the virtual folder
static char roms_path[MAX_PATH_LEN];   // <sd>/Roms
static SEIndex index_;
static int scanned = 0;
static char query[SE_QUERY_LEN];       // the query the folder lists
static SERecents recents;
static int recents_max = SE_RECENTS_MAX;

// the last search's results, for query
static SEHit* hits = NULL;
static int hit_count = 0;
static SEOrder order;
static char hits_query[SE_QUERY_LEN];
static int hits_valid = 0;

// tick's view of where the game list is
static char last_path[MAX_PATH_LEN];
static int seen = 0;          // a path was seen since the launcher started
static int opening = 0;       // open_path to the folder is ours: don't ask again
static int back_on_cancel = 0; // asked from the root row: cancelling backs out

// back from a game launched from the results
static int restoring = 0;
static char restore_path[MAX_PATH_LEN];

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

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

static double millis(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int inFolder(void) {
	const char* current = host->current_path ? host->current_path() : NULL;
	return current && !strcmp(current, folder_path);
}

// True when the game list is on screen, not a plugin screen, the game
// switcher, the quick menu or a dialog over it.
static int onGameList(void) {
	return !MOSAIC_HOST_HAS(host, current_screen) || !strcmp(host->current_screen(), "gamelist");
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

static int isDirEntry(const struct dirent* dp, const char* path) {
	if (dp->d_type == DT_DIR) return 1;
	if (dp->d_type != DT_UNKNOWN) return 0;
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void addFolder(const char* dir) {
	struct stat st;
	SE_indexAddFolder(&index_, dir, stat(dir, &st) == 0 ? st.st_mtime : 0);
}

static void scanDir(const char* dir, int depth) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	addFolder(dir);

	char map_path[MAX_PATH_LEN + 16];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", dir);
	SEMap map;
	SE_mapParse(&map, readFile(map_path));

	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (SE_hidden(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;

		int type = MOSAIC_ENTRY_ROM;
		if (isDirEntry(dp, path)) {
			if (endsWith(dp->d_name, ".pak")) type = MOSAIC_ENTRY_PAK;
			else if (isGameFolder(path, dp->d_name)) type = MOSAIC_ENTRY_DIR; // opening it launches the game
			else {
				if (depth < MAX_DEPTH) scanDir(path, depth + 1);
				continue;
			}
		}

		char name[SE_NAME_LEN];
		const char* alias = SE_mapGet(&map, dp->d_name);
		if (alias) snprintf(name, sizeof(name), "%s", alias);
		else SE_cleanName(dp->d_name, name, sizeof(name));
		if (SE_hidden(name)) continue; // map.txt can hide a game
		SE_indexAdd(&index_, path, name, type);
	}
	closedir(dh);
	SE_mapFree(&map);
}

static void scan(void) {
	double start = millis();
	SE_indexFree(&index_);
	scanned = 1;
	hits_valid = 0;

	DIR* dh = opendir(roms_path);
	if (!dh) return;
	addFolder(roms_path);
	int systems = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (SE_hidden(dp->d_name)) continue;
		char path[MAX_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", roms_path, dp->d_name) >= (int)sizeof(path)) continue;
		if (!isDirEntry(dp, path) || !hasEmulator(dp->d_name)) continue;
		scanDir(path, 0);
		systems++;
	}
	closedir(dh);

	char message[128];
	snprintf(message, sizeof(message), "%d games in %d systems, scanned in %.1f ms", index_.game_count, systems, millis() - start);
	say(message);
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
// searching

// Searches for query, unless the results for it are still good.
static void refresh(void) {
	if (!scanned || cardChanged()) scan();
	if (hits_valid && !strcmp(hits_query, query)) return;

	SE_orderFree(&order);
	free(hits);
	hits = NULL;
	hit_count = 0;
	snprintf(hits_query, sizeof(hits_query), "%s", query);
	hits_valid = 1;
	if (!query[0] || !index_.game_count) return;

	hits = malloc(sizeof(SEHit) * (size_t)index_.game_count);
	if (!hits) {
		say("out of memory searching");
		return;
	}
	double start = millis();
	hit_count = SE_search(&index_, query, hits);
	SE_orderBuild(&order, &index_, hits, hit_count);

	char message[SE_QUERY_LEN + 96];
	snprintf(message, sizeof(message), "\"%s\": %d of %d games in %.2f ms", query, hit_count, index_.game_count, millis() - start);
	say(message);
}

static void saveRecents(void) {
	for (int i = 0; i < SE_RECENTS_MAX; i++) {
		char key[16];
		snprintf(key, sizeof(key), "recent%d", i + 1);
		const char* value = i < recents.count ? recents.items[i] : "";
		if (strcmp(host->cfg_get(ID, key, ""), value) != 0) host->cfg_set(ID, key, value);
	}
}

static void loadRecents(void) {
	recents.count = 0;
	for (int i = 0; i < SE_RECENTS_MAX && recents.count < recents_max; i++) {
		char key[16];
		snprintf(key, sizeof(key), "recent%d", i + 1);
		char value[SE_QUERY_LEN];
		snprintf(value, sizeof(value), "%s", host->cfg_get(ID, key, ""));
		SE_trim(value);
		if (value[0]) snprintf(recents.items[recents.count++], SE_QUERY_LEN, "%s", value);
	}
}

// Lists the results for text in the Search folder, opening it unless the
// game list is already there.
static void run(const char* text) {
	char typed[SE_QUERY_LEN];
	snprintf(typed, sizeof(typed), "%s", text);
	SE_trim(typed);
	snprintf(query, sizeof(query), "%s", typed);
	host->cfg_set(ID, "query", query);
	if (recents_max > 0) {
		SE_recentsAdd(&recents, query, recents_max);
		saveRecents();
	}

	refresh();
	if (!hit_count && MOSAIC_HOST_HAS(host, toast)) {
		char message[SE_QUERY_LEN + 256];
		snprintf(message, sizeof(message), tr("No games match %s"), query);
		host->toast(message);
	}
	if (inFolder()) {
		if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
		return;
	}
	opening = 1;
	if (!host->open_path(folder_path, MOSAIC_ENTRY_DIR)) {
		opening = 0;
		say("couldn't open the results");
	}
}

///////////////////////////////////////
// asking

static void cancelled(void) {
	if (back_on_cancel && inFolder() && MOSAIC_HOST_HAS(host, run_action)) host->run_action("gamelist.back");
	back_on_cancel = 0;
}

static void typed(const char* text, void* data) {
	(void)data;
	char trimmed[SE_QUERY_LEN];
	snprintf(trimmed, sizeof(trimmed), "%s", text ? text : "");
	SE_trim(trimmed);
	if (!trimmed[0]) cancelled();
	else run(trimmed);
	back_on_cancel = 0;
}

static void picked(int index, void* data) {
	(void)data;
	if (index < 0) cancelled();
	else if (index == 0) host->keyboard(tr("Search"), "", typed, NULL);
	else if (index <= recents.count) {
		char again[SE_QUERY_LEN];
		snprintf(again, sizeof(again), "%s", recents.items[index - 1]);
		back_on_cancel = 0;
		run(again);
	}
}

// Asks for a query: from the recent ones, or on the keyboard.
static void ask(int from_row) {
	back_on_cancel = from_row;
	if (recents_max > 0 && recents.count > 0 && MOSAIC_HOST_HAS(host, choose)) {
		const char* choices[SE_RECENTS_MAX + 1];
		choices[0] = tr("New search");
		for (int i = 0; i < recents.count; i++) choices[i + 1] = recents.items[i];
		if (host->choose(tr("Search"), choices, recents.count + 1, 0, picked, NULL) == 0) return;
	}
	host->keyboard(tr("Search"), "", typed, NULL);
}

static int canSearch(void* data) {
	(void)data;
	return MOSAIC_HOST_HAS(host, keyboard) && MOSAIC_HOST_HAS(host, open_path);
}

static void searchAction(void* data) {
	if (canSearch(data)) ask(0);
}

///////////////////////////////////////
// hooks

static void rootEntries(MosaicList* root) {
	if (!canSearch(NULL) || !strcmp(host->cfg_get(ID, "root", "on"), "off")) return;
	host->list_push(root, host->entry_new(folder_path, MOSAIC_ENTRY_DIR, tr("Search")));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder_path) != 0) return NULL;
	refresh();
	MosaicList* list = host->list_new();
	for (int i = 0; i < hit_count; i++) {
		const SEGame* game = &index_.games[hits[i].game];
		host->list_push(list, host->entry_new(game->path, game->type, game->name));
	}
	return list;
}

// Results keep their ranked order, even when another plugin renames one
// and the launcher sorts the folder by name.
static int sortCompare(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	if (!dir_path || strcmp(dir_path, folder_path) != 0) return 0;
	int x = SE_orderOf(&order, host->entry_path(a));
	int y = SE_orderOf(&order, host->entry_path(b));
	if (x < 0 || y < 0) return 0;
	return x < y ? -1 : x > y;
}

static int preLaunch(const MosaicLaunch* launch) {
	unlink(SE_LAST_FILE);
	if (!launch || !launch->path || !inFolder() || !onGameList()) return MOSAIC_LAUNCH_CONTINUE;
	FILE* f = fopen(SE_LAST_FILE, "w");
	if (f) {
		fputs(launch->path, f);
		fclose(f);
	}
	return MOSAIC_LAUNCH_CONTINUE;
}

static void postReturn(void) {
	FILE* f = fopen(SE_LAST_FILE, "r");
	if (!f) return;
	restoring = fgets(restore_path, sizeof(restore_path), f) != NULL && restore_path[0];
	fclose(f);
	unlink(SE_LAST_FILE);
}

// Back out of the folders stock opened for the game to the root, open the
// results and select the game.
static void restore(void) {
	if (!onGameList() || !MOSAIC_HOST_HAS(host, run_action) || !MOSAIC_HOST_HAS(host, current_list)
			|| !MOSAIC_HOST_HAS(host, select_index)) return;
	for (int i = 0; i < 16; i++) {
		const char* current = host->current_path();
		if (!current || !strcmp(current, host->sdcard_path) || host->run_action("gamelist.back") != 0) break;
	}
	const char* current = host->current_path();
	if (!current || strcmp(current, host->sdcard_path) != 0) return;

	opening = 1;
	if (!host->open_path(folder_path, MOSAIC_ENTRY_DIR)) {
		opening = 0;
		return;
	}
	const MosaicList* list = host->current_list();
	int count = list ? host->list_count(list) : 0;
	for (int i = 0; i < count; i++) {
		if (!strcmp(host->entry_path(host->list_get(list, i)), restore_path)) {
			host->select_index(i);
			break;
		}
	}
}

// Opening the root row asks for a query; tick does it, since a list hook
// can't open the keyboard. Coming back into the folder from a game, or a
// reload, doesn't ask.
static void tick(unsigned long now) {
	(void)now;
	const char* current = host->current_path ? host->current_path() : NULL;
	if (!current) return; // the lists aren't there yet
	if (restoring) {
		restoring = 0;
		restore();
		current = host->current_path();
		if (!current) return;
	}
	int entered = strcmp(current, folder_path) == 0 && strcmp(last_path, folder_path) != 0;
	int ask_now = entered && seen && !opening && canSearch(NULL);
	if (entered) opening = 0;
	snprintf(last_path, sizeof(last_path), "%s", current);
	seen = 1;
	if (ask_now) ask(1);
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/" FOLDER_NAME, host->sdcard_path);
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	SE_indexInit(&index_);
	memset(&order, 0, sizeof(order));
	scanned = 0;
	hits = NULL;
	hit_count = 0;
	hits_valid = 0;
	last_path[0] = '\0';
	seen = 0;
	opening = 0;
	back_on_cancel = 0;
	restoring = 0;

	recents_max = strcmp(host->cfg_get(ID, "recents", "on"), "off") ? SE_RECENTS_MAX : 0;
	loadRecents();
	snprintf(query, sizeof(query), "%s", host->cfg_get(ID, "query", ""));
	SE_trim(query);

	if (!canSearch(NULL)) {
		say("this launcher has no keyboard or can't open folders for plugins; nothing to do");
		return 0;
	}
	if (host->register_action("search.open", "SEARCH", canSearch, searchAction, NULL) != 0) {
		say("couldn't register search.open");
		return 0;
	}
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* name = host->cfg_get(ID, "button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, "search.open") != 0) {
			say("couldn't bind the search button");
		}
	}
	return 0;
}

static void quit(void) {
	SE_indexFree(&index_);
	SE_orderFree(&order);
	free(hits);
	hits = NULL;
	hit_count = 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.sort_compare = sortCompare,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
