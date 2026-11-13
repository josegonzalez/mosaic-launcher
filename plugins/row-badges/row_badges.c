// Row badges: small dots after a row's name, on the built-in list and in
// plugin layouts' cells. An unplayed dot, in the theme's accent colour,
// marks games that weren't played (the "unplayed" setting), and tier dots,
// in its list text colour, give the tier= value in an entry's .info
// sidecar, from 1 to 5 dots, for games and system folders alike (the
// "tiers" setting), each ringed in its background colour. A favorite mark,
// in its secondary accent colour, marks games in the favorites entry set
// (the "favorites" setting), and a tag mark, in its hint colour, games in
// any tags:<name> set (the "tagged" setting). The sets are read when the
// launcher builds a list, back from a game and when one of them changes
// (launcher.set_changed), never while drawing. All are off by default.
//
// A game counts as played when it's in recents, when stock or minarch
// left a file for it: a save in Saves/<tag>/, a state in
// .userdata/shared/<tag>-<core>/, or a resume slot, state name or preview
// in .userdata/shared/.minui/<tag>/, or when the play history has a
// session of it. Those files are read from the card at start, back from a
// game, and each time the launcher builds a list, never while drawing.
// Folders, paks and anything outside Roms/ get no unplayed dot.
//
// Other plugins ask the same question with the games.played service, the
// one definition of played, whether or not the dot is on: path=<game or
// game folder> gives played=1 or played=0. With the dot off, the card is
// read for it on the first call after each of those times instead.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "badges_model.h"
#include "mosaic_plugin.h"

#define ID "row-badges"
#define MAX_PATH_LEN 1024

static const MosaicHost* host;
static int show_unplayed = 0;
static int show_tiers = 0;
static int show_favorites = 0;
static int show_tagged = 0;
static char roms[MAX_PATH_LEN];
static RBSet played;
static RBSet favorites; // the favorites set's paths, while its mark is on
static RBSet tagged;    // every tags:<name> set's paths, while its mark is on

static int isOn(const char* key) {
	return !strcmp(host->cfg_get(key, "off"), "on");
}

// Adds what each file in dir names, as parse reads it, under tag.
static void scanFiles(const char* dir, const char* tag, int (*parse)(const char*, char*, size_t)) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	struct dirent* dp;
	char game[512];
	while ((dp = readdir(dh)) != NULL) {
		if (parse(dp->d_name, game, sizeof(game))) RB_setAdd(&played, tag, game);
	}
	closedir(dh);
}

// Each folder of dir as a tag, with its files read by parse. With
// state_dirs, only "<tag>-<core>" folders, by their tag.
static void scanTags(const char* dir, int state_dirs, int (*parse)(const char*, char*, size_t)) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		char tag[256], path[MAX_PATH_LEN];
		if (dp->d_name[0] == '.') continue;
		if (state_dirs) {
			if (!RB_stateDir(dp->d_name, tag, sizeof(tag))) continue;
		} else if (snprintf(tag, sizeof(tag), "%s", dp->d_name) >= (int)sizeof(tag)) {
			continue;
		}
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;
		scanFiles(path, tag, parse);
	}
	closedir(dh);
}

static int stale = 1; // the card has to be read again before played is asked

static void scan(void) {
	stale = 0;
	RB_setFree(&played);
	char dir[MAX_PATH_LEN];
	snprintf(dir, sizeof(dir), "%s/Saves", host->sdcard_path);
	scanTags(dir, 0, RB_saveFile);
	snprintf(dir, sizeof(dir), "%s/.userdata/shared", host->sdcard_path);
	scanTags(dir, 1, RB_stateFile);
	snprintf(dir, sizeof(dir), "%s/.userdata/shared/.minui", host->sdcard_path);
	scanTags(dir, 0, RB_slotFile);
	int count = host->recents_count();
	for (int i = 0; i < count; i++) {
		const char* path = host->recent_path(i);
		char tag[256];
		if (!path || !RB_tagOf(roms, path, tag, sizeof(tag))) continue;
		const char* file = strrchr(path, '/');
		RB_setAdd(&played, tag, file + 1);
	}
	RB_setFinish(&played);
}

static void addPath(const char* path, void* data) {
	RB_pathsAdd(data, path);
}

static void addTagged(const char* name, void* data) {
	(void)data;
	if (!strncmp(name, "tags:", 5)) host->set_list(name, addPath, &tagged);
}

// Asks the sets the marks show again. A set nobody registered, as when
// its plugin is off, has nothing in it.
static void readSets(void) {
	if (!MOSAIC_HOST_HAS(host, set_names)) return;
	if (show_favorites) {
		RB_setFree(&favorites);
		host->set_list("favorites", addPath, &favorites);
		RB_setFinish(&favorites);
	}
	if (show_tagged) {
		RB_setFree(&tagged);
		host->set_names(addTagged, NULL);
		RB_setFinish(&tagged);
	}
}

// Reads the card now while the dot is on, and otherwise the next time a
// plugin asks, and the sets while their marks are on.
static void refresh(void) {
	stale = 1;
	if (show_unplayed) scan();
	readSets();
}

// A set changed: read the ones the marks show again.
static void setChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)data;
	char set[256];
	if (host->event_value(payload, "name", set, sizeof(set)) < 0) return;
	if ((show_favorites && !strcmp(set, "favorites")) || (show_tagged && !strncmp(set, "tags:", 5))) {
		readSets();
		host->request_redraw();
	}
}

// The play history has a session of the game at path.
static int inHistory(const char* path, void* ctx) {
	(void)ctx;
	return MOSAIC_HOST_HAS(host, play_time) && host->play_count(path) > 0;
}

// games.played: path=<game or game folder>, played=1 or played=0.
static int providePlayed(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char path[MAX_PATH_LEN];
	int len = host->event_value(args, "path", path, sizeof(path));
	if (len <= 0 || len >= (int)sizeof(path)) return -1;
	if (stale) scan();
	struct stat st;
	int is_dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
	return RB_reply(RB_isPlayed(&played, roms, path, is_dir, inHistory, NULL), out, size);
}

static void listReady(const MosaicList* list, const char* dir_path) {
	(void)list;
	(void)dir_path;
	refresh();
}

static void postReturn(void) {
	refresh();
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	int type = host->entry_type(entry);
	const char* path = host->entry_path(entry);
	int is_dir = type == MOSAIC_ENTRY_DIR;
	int colors[RB_MAX_MARKS];
	int marks = 0;
	if (show_unplayed && type == MOSAIC_ENTRY_ROM && RB_isPlayed(&played, roms, path, 0, inHistory, NULL) == 0) {
		colors[marks++] = MOSAIC_COLOR_ACCENT;
	}
	if (show_favorites && RB_hasGame(&favorites, path, is_dir)) colors[marks++] = MOSAIC_COLOR_ACCENT2;
	if (show_tagged && RB_hasGame(&tagged, path, is_dir)) colors[marks++] = MOSAIC_COLOR_HINT;
	int tier = 0;
	if (show_tiers) {
		char value[16];
		if (host->entry_info(entry, "tier", value, sizeof(value)) == 1) tier = RB_tier(value);
	}
	if (!marks && !tier) return 0;

	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	MosaicRect dots[RB_MAX_MARKS + RB_MAX_TIER];
	int count = RB_place(&row->rect, &row->name, 4 * scale, 2 * scale, marks, tier, dots);
	int text = host->theme_color(MOSAIC_COLOR_LIST_TEXT);
	int background = host->theme_color(MOSAIC_COLOR_BACKGROUND);
	if (background < 0) background = 0x000000;
	for (int i = 0; i < count; i++) {
		int rgb = i < marks ? host->theme_color(colors[i]) : text;
		if (rgb < 0) rgb = 0xFFFFFF;
		// a ring in the background colour keeps the dot apart from a
		// layout's selection or art drawn in the same colour
		MosaicRect ring = { dots[i].x - scale, dots[i].y - scale, dots[i].w + 2 * scale, dots[i].h + 2 * scale };
		host->fill_round_rect(screen, &ring, ring.w / 2, mosaic_color((unsigned int)background, 255));
		host->fill_round_rect(screen, &dots[i], dots[i].w / 2, mosaic_color((unsigned int)rgb, 255));
	}
	return 0;
}

static void quit(void) {
	RB_setFree(&played);
	RB_setFree(&favorites);
	RB_setFree(&tagged);
	stale = 1;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell) || !MOSAIC_HOST_HAS(host, theme_color)) {
		host->log(ID, "this launcher can't decorate rows");
		return 1;
	}
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	show_unplayed = isOn("unplayed");
	show_tiers = isOn("tiers");
	if (show_tiers && !MOSAIC_HOST_HAS(host, entry_info)) {
		host->log(ID, "this launcher can't read .info sidecars, so tier dots are off");
		show_tiers = 0;
	}
	show_favorites = isOn("favorites");
	show_tagged = isOn("tagged");
	if ((show_favorites || show_tagged) && !MOSAIC_HOST_HAS(host, set_names)) {
		host->log(ID, "this launcher has no entry sets, so the favorite and tag marks are off");
		show_favorites = show_tagged = 0;
	}
	RB_setInit(&played);
	RB_setInit(&favorites);
	RB_setInit(&tagged);
	if (show_favorites || show_tagged) host->subscribe("launcher.set_changed", setChanged, NULL);
	refresh();
	if (MOSAIC_HOST_HAS(host, call) && host->provide("games.played", providePlayed) != 0) host->log(ID, "couldn't provide games.played");
	char message[128];
	snprintf(message, sizeof(message), "unplayed %s, tiers %s, favorites %s, tagged %s", show_unplayed ? "on" : "off",
		show_tiers ? "on" : "off", show_favorites ? "on" : "off", show_tagged ? "on" : "off");
	host->log(ID, message);
	return 0;
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.post_return = postReturn,
	.list_ready = listReady,
	.decorate_row = decorateRow,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
