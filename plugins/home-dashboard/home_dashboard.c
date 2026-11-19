// Home dashboard: a home screen in place of the root list, owning the home
// slot.
//
// The "mode" setting picks the home:
//   dashboard          tabs, switched with L1 and R1: Home (a continue card
//                      for the last game played and a top shelf of the
//                      games played before it), Pinned (favorites' games,
//                      while favorites is on) and Stats (play time from the
//                      play history)
//   minui              a plain list of the recently played games, then Games
//   retroachievements  recent and pinned games with RetroAchievements
//                      progress on the card
//   pico8              the Pico-8 carts in the card's Pico-8 folders, as a grid
//   minimal            the continue card and the clock
//   consoles           every system with games as a tile with its game count,
//                      which A opens
//
// The dashboard's Home tab has rows under its continue card, which the
// "rows" setting names in order: continue (the other recent games with a
// save state), recent (the games played before the last one), added (the
// games put on the card last), played (activity-stats' most played set
// while it's on, else the play history's most played), random and
// shortcuts (tiles that run the actions the "shortcuts" setting names,
// with their quick menu tile's icon and label). Rows with nothing in
// them are left out, and one shows at a time, down and up moving between
// them, with its heading while there's more than one. The rows from the
// card index (added, random) and the consoles are built when the index or
// a set changes, not as the screen draws.
//
// X closes the home screen on to the game list (the launcher's root list),
// MENU opens the quick menu and SELECT the game switcher, as on the root
// list. B does nothing, as at the root. A opens what's selected: a recent
// through recent_open, resuming its save state when it has one, anything
// else through open_path. Either way the launcher comes back to the home
// screen after the game.
//
// The screen's enter runs each time it opens again (after the quick menu,
// the switcher or a game), so the tab and selections live here, and the
// recents, favorites and carts are read again there. Favorites are the
// favorites entry set (set_list), and anything a plugin's filter hides,
// such as the hidden plugin's games, is left out everywhere (is_filtered).
// Favorites get nothing else of their own.
//
// The continue card's detail line is playtime-captions' caption for the
// game, from its captions.text service, in that plugin's parts and
// format; without it, as when playtime-captions is off, it's the game's
// play time ("2h 15m played"). While playtime-limit has a daily limit
// set, its limit.left service gives the Stats tab a Time left today row,
// with the time left of today's limit; with no limit, or without
// playtime-limit, there's no row. While romm is loaded and has downloaded
// games still on the card, its romm.downloads service gives the Stats tab
// a Recently downloaded section of the last few, read as the screen
// opens. Up and down scroll the Stats tab when its rows don't fit, and a
// heading never shows as its last row.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "home_model.h"
#include "mosaic_plugin.h"

#define ID "home-dashboard"
#define SCREEN_ID "home-dashboard.main"
#define MAX_PATH_LEN 512
#define RECENTS_MAX 24
#define PINNED_MAX 64
#define CARTS_MAX 256
#define ENTRIES_MAX 512
#define MOST_PLAYED_MAX 5
#define DOWNLOADS_MAX 3
#define ROWS_MAX (RECENTS_MAX + PINNED_MAX + 8)
#define ROW_ITEMS 16     // the most a home row holds
#define SHORTCUTS_MAX 12
#define CONSOLES_MAX 96
#define PLAYED_SCAN 200  // play history games looked at for the played row

static const MosaicHost* host;

// settings
static int mode = HD_MODE_DASHBOARD;
static int pinned_on = 1;
static int stats_on = 1;
static int shelf_on = 1;
static char pico_tags[256];
static int row_kinds[HD_ROW_KINDS]; // the rows setting's, in order
static int row_kind_count = 0;
static int clock24 = 1;

static int favorites_loaded = 0; // favorites loaded before this plugin, as its optional= asks

typedef struct Item {
	char path[MAX_PATH_LEN];
	char name[256];
	int type;
	int recent; // its index in recents, or -1
	int available;
	int can_resume;
	char preview[MAX_PATH_LEN]; // its save state's preview, or ""
	const MosaicEntry* entry;   // for its art, from the pool, or NULL
} Item;

static Item recents[RECENTS_MAX];
static int recent_count = 0;
static Item pinned[PINNED_MAX];
static int pinned_count = 0;
static Item carts[CARTS_MAX];
static int cart_count = 0;
static Item downloads[DOWNLOADS_MAX]; // romm's, newest first
static long long downloaded_at[DOWNLOADS_MAX];
static int download_count = 0;

// The rows of the last list drawn, for what A opens. NULL item for Games.
typedef struct Row {
	const char* name;
	char right[96];
	int header; // a section's label, not selectable
	Item* item;
} Row;

static Row rows[ROWS_MAX];
static int row_count = 0;

// The home tab's rows with something in them, in the rows setting's order.
typedef struct HomeRow {
	int kind;                // an enum HDRow
	int count;
	Item* items[ROW_ITEMS];  // its games; none for shortcuts
} HomeRow;

static HomeRow home_rows[HD_ROW_KINDS];
static int home_row_count = 0;
static Item added_items[ROW_ITEMS]; // newest first
static int added_count = 0;
static Item played_items[ROW_ITEMS]; // most played first
static int played_count = 0;
static Item random_items[ROW_ITEMS];
static int random_count = 0;

// The shortcuts setting's actions: each name as given, the action it
// found, and what its tile shows.
typedef struct Shortcut {
	char want[64];
	char id[64];
	char label[128];
	char icon[MAX_PATH_LEN];
	int logged; // its being unknown or unavailable was logged
} Shortcut;

static Shortcut shortcuts[SHORTCUTS_MAX];
static int shortcuts_wanted = 0;
static int shortcut_shown[SHORTCUTS_MAX]; // the ones available, as indices into shortcuts
static int shortcut_count = 0;

static HDConsole consoles[CONSOLES_MAX];
static const MosaicEntry* console_entries[CONSOLES_MAX];
static int console_count = 0;

static int card_dirty = 1; // the card index or a set changed since the rows were built

// what's selected, kept while the screen is closed
static int tabs[HD_TABS_MAX];
static int tab_count = 1;
static int tab = HD_TAB_HOME;
static HDFocus focus = { HD_FOCUS_CONTINUE, 0 };
static int shelf_first = 0;
static int list_selected = 0;
static int list_first = 0;
static int stats_first = 0; // the Stats tab's first row in view
static int stats_fit = 1;   // and how many fit, from the last draw
static int cart_selected = 0;
static int cart_first_row = 0;
static int console_selected = 0;
static int console_first_row = 0;
static long shown_minute = -1; // the minimal mode's clock, at the last render
static int shelf_cells = 1; // shelf cards in view at the last render
static int grid_cols = 1;   // carts per row at the last render

static int showing = 0; // the screen is open
static int waiting = 0; // art or achievements were still loading at the last render
static unsigned long asked_at = 0;

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

///////////////////////////////////////
// entries for art

// The launcher has no call to free an entry that isn't in a list, so these
// are made once per path and kept, up to ENTRIES_MAX for the run (the
// launcher starts again after every game).
static MosaicEntry* pool[ENTRIES_MAX];
static int pool_count = 0;

static const MosaicEntry* entryFor(const char* path, int type) {
	for (int i = 0; i < pool_count; i++) {
		const char* p = host->entry_path(pool[i]);
		if (p && !strcmp(p, path)) return pool[i];
	}
	if (pool_count >= ENTRIES_MAX) return NULL;
	MosaicEntry* entry = host->entry_new(path, type, NULL);
	if (entry) pool[pool_count++] = entry;
	return entry;
}

///////////////////////////////////////
// reading the card

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

// path's file name without its extension, or without ".p8.png" for a cart.
static void baseName(const char* path, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	snprintf(out, size, "%s", slash ? slash + 1 : path);
	size_t len = strlen(out);
	if (len > 7 && !strcasecmp(out + len - 7, ".p8.png")) {
		out[len - 7] = '\0';
		return;
	}
	char* dot = strrchr(out, '.');
	if (dot && dot != out) *dot = '\0';
}

// True when a plugin's filter, such as hidden's, hides path from its
// folder, so the dashboard leaves it out too.
static int filtered(const char* path) {
	return MOSAIC_HOST_HAS(host, is_filtered) && host->is_filtered(path);
}

static void loadRecents(void) {
	recent_count = 0;
	int n = host->recents_count();
	for (int i = 0; i < n && recent_count < RECENTS_MAX; i++) {
		const char* path = host->recent_path(i);
		if (!path || strlen(path) >= MAX_PATH_LEN) continue;
		char copy[MAX_PATH_LEN];
		snprintf(copy, sizeof(copy), "%s", path); // recent_path's string lasts only until the next call
		if (filtered(copy)) continue;
		path = copy;
		Item* item = &recents[recent_count];
		memset(item, 0, sizeof(*item));
		snprintf(item->path, sizeof(item->path), "%s", path);
		item->recent = i;
		item->type = endsWith(path, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
		item->available = 1;
		MosaicRecent info = { .size = sizeof(MosaicRecent) };
		if (host->recent_info(i, &info) == 0) {
			snprintf(item->name, sizeof(item->name), "%s", info.name);
			item->type = info.type;
			item->available = info.available;
			item->can_resume = info.can_resume;
			snprintf(item->preview, sizeof(item->preview), "%s", info.preview);
		}
		if (!item->name[0]) baseName(path, item->name, sizeof(item->name));
		item->entry = entryFor(item->path, item->type);
		recent_count++;
	}
}

// A game at path, named by its entry, else its file name. 0, or -1 for one
// that isn't on the card, is hidden or whose path is too long.
static int itemFor(Item* item, const char* path) {
	if (strlen(path) >= MAX_PATH_LEN || !exists(path) || filtered(path)) return -1;
	memset(item, 0, sizeof(*item));
	snprintf(item->path, sizeof(item->path), "%s", path);
	item->recent = -1;
	item->available = 1;
	item->type = endsWith(item->path, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
	item->entry = entryFor(item->path, item->type);
	const char* name = item->entry ? host->entry_name(item->entry) : NULL;
	if (name && name[0]) snprintf(item->name, sizeof(item->name), "%s", name);
	else baseName(item->path, item->name, sizeof(item->name));
	return 0;
}

static void addPinned(const char* path, void* data) {
	(void)data;
	if (pinned_count < PINNED_MAX && itemFor(&pinned[pinned_count], path) == 0) pinned_count++;
}

// the favorites entry set, in the order favorites lists it
static void loadPinned(void) {
	pinned_count = 0;
	if (!favorites_loaded || !MOSAIC_HOST_HAS(host, is_filtered)) return;
	host->set_list("favorites", addPinned, NULL);
}

static int byName(const void* a, const void* b) {
	return strcasecmp(((const Item*)a)->name, ((const Item*)b)->name);
}

// carts in Roms/<folder>/ whose tag is one of the pico8 tags
static void loadCarts(void) {
	cart_count = 0;
	char roms[MAX_PATH_LEN];
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	DIR* dh = opendir(roms);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL && cart_count < CARTS_MAX) {
		char tag[64];
		if (dp->d_name[0] == '.' || !HD_tagOf(dp->d_name, tag, sizeof(tag)) || !HD_tagIn(tag, pico_tags)) continue;
		char folder[MAX_PATH_LEN];
		if (snprintf(folder, sizeof(folder), "%s/%s", roms, dp->d_name) >= (int)sizeof(folder)) continue;
		DIR* fh = opendir(folder);
		if (!fh) continue;
		struct dirent* fp;
		while ((fp = readdir(fh)) != NULL && cart_count < CARTS_MAX) {
			if (!HD_isPicoCart(fp->d_name)) continue;
			Item* item = &carts[cart_count];
			memset(item, 0, sizeof(*item));
			if (snprintf(item->path, sizeof(item->path), "%s/%s", folder, fp->d_name) >= (int)sizeof(item->path)) continue;
			struct stat st;
			if (stat(item->path, &st) != 0 || !S_ISREG(st.st_mode) || filtered(item->path)) continue;
			item->recent = -1;
			item->available = 1;
			item->type = MOSAIC_ENTRY_ROM;
			baseName(item->path, item->name, sizeof(item->name));
			cart_count++;
		}
		closedir(fh);
	}
	closedir(dh);
	qsort(carts, (size_t)cart_count, sizeof(Item), byName);
	// entries once sorted, as each points into the pool, not the array
	for (int i = 0; i < cart_count; i++) carts[i].entry = entryFor(carts[i].path, carts[i].type);
}

static int wantsRow(int kind) {
	for (int i = 0; i < row_kind_count; i++) {
		if (row_kinds[i] == kind) return 1;
	}
	return 0;
}

static void addPlayed(const char* path, void* data) {
	(void)data;
	if (played_count < ROW_ITEMS && itemFor(&played_items[played_count], path) == 0) played_count++;
}

// The played row: activity-stats' most played set while it's on, else the
// most played of the play history still on the card.
static void loadPlayed(void) {
	played_count = 0;
	if (HD_pluginLoaded(host, "activity-stats") && MOSAIC_HOST_HAS(host, is_filtered)
			&& host->set_list("activity-stats.most-played", addPlayed, NULL) >= 0) {
		return;
	}
	if (!MOSAIC_HOST_HAS(host, play_game_info)) return;
	MosaicPlayGame game = { .size = sizeof(game) };
	for (int i = 0; i < PLAYED_SCAN && played_count < ROW_ITEMS && host->play_game_info(i, &game) == 0; i++) {
		if (!game.orphan && game.path[0] && game.seconds > 0) addPlayed(game.path, NULL);
	}
}

// A game from the card index, at its index among every system's.
static int itemFromIndex(Item* item, int index) {
	MosaicIndexEntry e = { .size = sizeof(e) };
	if (host->index_entry(NULL, index, &e) != 0 || strlen(e.path) >= MAX_PATH_LEN) return -1;
	memset(item, 0, sizeof(*item));
	snprintf(item->path, sizeof(item->path), "%s", e.path);
	snprintf(item->name, sizeof(item->name), "%s", e.name);
	if (!item->name[0]) baseName(item->path, item->name, sizeof(item->name));
	item->recent = -1;
	item->available = 1;
	item->type = e.type == MOSAIC_ENTRY_PAK ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
	item->entry = entryFor(item->path, item->type);
	return 0;
}

// When a game came onto the card: the later of its times, as a copy can
// keep the old modified time.
static long long addedAt(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0) return -1;
	return (long long)(st.st_mtime > st.st_ctime ? st.st_mtime : st.st_ctime);
}

// What comes from the card index, in one pass over it: the added and
// random rows and the consoles with their game counts, leaving out games
// whose system has no emulator and what a filter hides. Then the played
// row. Run when the index or a set changed, not as the screen draws.
static void loadCard(void) {
	card_dirty = 0;
	added_count = random_count = console_count = 0;
	if (mode == HD_MODE_DASHBOARD && wantsRow(HD_ROW_PLAYED)) loadPlayed();
	else played_count = 0;
	int want_added = mode == HD_MODE_DASHBOARD && wantsRow(HD_ROW_ADDED);
	int want_random = mode == HD_MODE_DASHBOARD && wantsRow(HD_ROW_RANDOM);
	int want_consoles = mode == HD_MODE_CONSOLES;
	if (!(want_added || want_random || want_consoles) || !MOSAIC_HOST_HAS(host, index_refresh)) return;
	int n = host->index_count(NULL);
	if (n == MOSAIC_INDEX_NOT_READY) {
		card_dirty = 1; // again on launcher.index_ready
		say("card index not ready yet");
		return;
	}
	long long keys[ROW_ITEMS];
	int newest[ROW_ITEMS], picks[ROW_ITEMS];
	int kept = 0, picked = 0, seen = 0, emulator = 0;
	unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)n * 2654435761u;
	for (int i = 0; i < n; i++) {
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_entry(NULL, i, &e) != 0) continue;
		if (e.kind == MOSAIC_INDEX_SYSTEM) {
			emulator = e.emulator;
			if (want_consoles) console_count = HD_consoleAdd(consoles, console_count, CONSOLES_MAX, e.kind, e.emulator, e.system, e.path, e.name, 1);
			continue;
		}
		if (e.kind != MOSAIC_INDEX_GAME || !emulator) continue;
		int hidden = filtered(e.path);
		if (want_consoles) console_count = HD_consoleAdd(consoles, console_count, CONSOLES_MAX, e.kind, emulator, e.system, e.path, e.name, !hidden);
		if (hidden) continue;
		if (want_random) picked = HD_reservoir(picks, ROW_ITEMS, seen++, i, &seed);
		if (want_added && !e.added) {
			long long at = addedAt(e.path);
			if (at >= 0) kept = HD_topAdd(keys, newest, kept, ROW_ITEMS, at, i);
		}
	}
	for (int k = 0; k < kept; k++) {
		if (itemFromIndex(&added_items[added_count], newest[k]) == 0) added_count++;
	}
	for (int k = 0; k < picked; k++) {
		if (itemFromIndex(&random_items[random_count], picks[k]) == 0) random_count++;
	}
	console_count = HD_consolesKeep(consoles, console_count);
	for (int k = 0; k < console_count; k++) console_entries[k] = entryFor(consoles[k].path, MOSAIC_ENTRY_DIR);
	if (console_selected >= console_count) console_selected = 0;
}

// The shortcuts setting's actions that can run now. A name is an action's
// id, or a plugin's id for its action with a quick menu tile, else its
// first that doesn't need a game. One that isn't there or can't run now
// is left out, and logged once.
static void loadShortcuts(void) {
	shortcut_count = 0;
	if (!wantsRow(HD_ROW_SHORTCUTS) || !MOSAIC_HOST_HAS(host, action_info)) return;
	int actions = host->actions_count();
	int has_icon = MOSAIC_HOST_HAS(host, quick_tile_icon);
	for (int k = 0; k < shortcuts_wanted; k++) {
		Shortcut* s = &shortcuts[k];
		int found = -1, fallback = -1;
		for (int i = 0; i < actions && found < 0; i++) {
			MosaicActionInfo info = { .size = sizeof(info) };
			if (host->action_info(i, &info) != 0) continue;
			char icon[MAX_PATH_LEN];
			if (!strcmp(info.id, s->want)) found = i;
			else if (!strcmp(info.plugin_id, s->want) && !info.takes_path) {
				if (has_icon && host->quick_tile_icon(info.id, icon, sizeof(icon)) != -1) found = i;
				else if (fallback < 0) fallback = i;
			}
		}
		if (found < 0) found = fallback;
		MosaicActionInfo info = { .size = sizeof(info) };
		char message[160];
		if (found < 0 || host->action_info(found, &info) != 0) {
			if (!s->logged) {
				snprintf(message, sizeof(message), "shortcut %.63s: no such action or plugin", s->want);
				say(message);
			}
			s->logged = 1;
			continue;
		}
		if (info.takes_path || (MOSAIC_HOST_HAS(host, action_available) && host->action_available(info.id, NULL) != 1)) {
			if (!s->logged) {
				snprintf(message, sizeof(message), "shortcut %.63s: %.63s can't run now", s->want, info.id);
				say(message);
			}
			s->logged = 1;
			continue;
		}
		snprintf(s->id, sizeof(s->id), "%s", info.id);
		snprintf(s->label, sizeof(s->label), "%s", info.label);
		if (!has_icon || host->quick_tile_icon(s->id, s->icon, sizeof(s->icon)) != 0) s->icon[0] = '\0';
		shortcut_shown[shortcut_count++] = k;
	}
}

// The home tab's rows with something in them, from what's loaded.
static void loadRows(void) {
	home_row_count = 0;
	int can_resume[RECENTS_MAX], available[RECENTS_MAX], picks[ROW_ITEMS];
	for (int i = 0; i < recent_count; i++) {
		can_resume[i] = recents[i].can_resume;
		available[i] = recents[i].available;
	}
	for (int k = 0; k < row_kind_count; k++) {
		HomeRow* row = &home_rows[home_row_count];
		row->kind = row_kinds[k];
		row->count = 0;
		Item* own = NULL;
		int own_count = 0;
		switch (row->kind) {
			case HD_ROW_CONTINUE:
			case HD_ROW_RECENT: {
				int n = HD_recentPick(row->kind, can_resume, available, recent_count, picks, ROW_ITEMS);
				for (int i = 0; i < n; i++) row->items[row->count++] = &recents[picks[i]];
				break;
			}
			case HD_ROW_ADDED: own = added_items; own_count = added_count; break;
			case HD_ROW_PLAYED: own = played_items; own_count = played_count; break;
			case HD_ROW_RANDOM: own = random_items; own_count = random_count; break;
			case HD_ROW_SHORTCUTS: row->count = shortcut_count; break;
		}
		for (int i = 0; i < own_count; i++) row->items[row->count++] = &own[i];
		if (row->count) home_row_count++;
	}
}

// A service's reply, or 0 when nobody provides it or it can't answer.
static int ask(const char* name, const char* args, char* reply, size_t size) {
	reply[0] = '\0';
	return MOSAIC_HOST_HAS(host, call) && host->call(name, args, reply, size) >= 0;
}

static long long replyNumber(const char* reply, const char* key, long long fallback) {
	char text[32];
	if (host->event_value(reply, key, text, sizeof(text)) <= 0) return fallback;
	char* end;
	long long v = strtoll(text, &end, 10);
	return end == text || *end ? fallback : v;
}

// the games romm downloaded last, from its romm.downloads service
static void loadDownloads(void) {
	download_count = 0;
	char args[32], reply[MOSAIC_CALL_MAX];
	snprintf(args, sizeof(args), "count=%i", DOWNLOADS_MAX);
	if (!ask("romm.downloads", args, reply, sizeof(reply))) return;
	int n = (int)replyNumber(reply, "count", 0);
	for (int i = 0; i < n && download_count < DOWNLOADS_MAX; i++) {
		char key[32], path[MAX_PATH_LEN];
		snprintf(key, sizeof(key), "path.%i", i);
		int len = host->event_value(reply, key, path, sizeof(path));
		if (len <= 0 || len >= (int)sizeof(path) || !exists(path) || filtered(path)) continue;
		Item* item = &downloads[download_count];
		memset(item, 0, sizeof(*item));
		snprintf(item->path, sizeof(item->path), "%s", path);
		item->recent = -1;
		item->available = 1;
		item->type = MOSAIC_ENTRY_ROM;
		item->entry = entryFor(item->path, item->type);
		const char* name = item->entry ? host->entry_name(item->entry) : NULL;
		if (name && name[0]) snprintf(item->name, sizeof(item->name), "%s", name);
		else baseName(item->path, item->name, sizeof(item->name));
		snprintf(key, sizeof(key), "at.%i", i);
		downloaded_at[download_count] = replyNumber(reply, key, 0);
		download_count++;
	}
}

// days from when to now by the local calendar
static int daysSince(long long when) {
	time_t now = time(NULL), then = (time_t)when;
	struct tm a, b;
	if (when <= 0 || !localtime_r(&now, &a) || !localtime_r(&then, &b)) return 0;
	a.tm_hour = b.tm_hour = 12;
	a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
	a.tm_isdst = b.tm_isdst = -1;
	return (int)((mktime(&a) - mktime(&b) + 43200) / 86400);
}

///////////////////////////////////////
// opening things

static void openItem(Item* item) {
	if (!item) return;
	int opened = item->recent >= 0 ? host->recent_open(item->recent, 1) : host->open_path(item->path, item->type);
	char message[MAX_PATH_LEN + 32];
	snprintf(message, sizeof(message), "open %s %i", item->path, opened);
	say(message);
	if (!opened) {
		char shown[512];
		snprintf(shown, sizeof(shown), tr("Couldn't open %s"), item->name);
		host->toast(shown);
	}
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

#define C_MAIN() themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255)
#define C_TEXT() themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 255)
#define C_SELECTED() themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000, 255)
#define C_HINT() themeColor(MOSAIC_COLOR_HINT, 0x999999, 255)
#define C_ACCENT() themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, 255)

// a card that isn't selected: the list text over the background, faintly
static MosaicColor dimCard(void) {
	return themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 28);
}

// the first letter of name, centred on a tile in the accent colour
static void drawLetter(MosaicSurface* screen, const MosaicRect* box, const char* name, int radius) {
	host->fill_round_rect(screen, box, radius, C_ACCENT());
	char letter[8] = { 0 };
	if (name && name[0]) {
		// one character, however many bytes it is
		int n = 1;
		while (name[n] && ((unsigned char)name[n] & 0xC0) == 0x80 && n < 4) n++;
		memcpy(letter, name, (size_t)n);
	}
	int w = host->text_width(MOSAIC_FONT_LARGE, letter);
	int h = host->text_height(MOSAIC_FONT_LARGE);
	host->draw_text(screen, MOSAIC_FONT_LARGE, letter, box->x + (box->w - (w > 0 ? w : 0)) / 2,
		box->y + (box->h - h) / 2, C_TEXT(), box->w);
}

// An item's art fitted into box: its save state's preview first when
// preview is set, then its thumbnail, then its box, or its first letter.
static void drawArt(MosaicSurface* screen, const Item* item, const MosaicRect* box, int preview, int radius) {
	const MosaicSurface* image = NULL;
	int got = MOSAIC_ART_MISSING;
	if (preview && item->preview[0]) got = host->art_load(item->preview, &image);
	if (got != MOSAIC_ART_READY && item->entry) {
		int thumb = host->art_get(item->entry, MOSAIC_ART_THUMBNAIL, &image);
		got = thumb;
		if (thumb == MOSAIC_ART_MISSING) got = host->art_get(item->entry, MOSAIC_ART_BOX, &image);
	}
	if (got == MOSAIC_ART_LOADING) waiting = 1;
	int w = 0, h = 0;
	MosaicRect fit;
	if (got == MOSAIC_ART_READY && image && host->surface_size(image, &w, &h) == 0 && HD_fit(w, h, box, &fit) == 0) {
		host->blit(screen, image, NULL, &fit);
		return;
	}
	drawLetter(screen, box, item->name, radius);
}

// Draws the hints, X for the games and A labelled open (NULL for none),
// and returns the y they start at.
static int drawHints(MosaicSurface* screen, const char* open) {
	int scale = host->screen_scale();
	int top = host->screen_height() - 40 * scale;
	MosaicRect drawn;
	MosaicHint games[] = { { MOSAIC_BUTTON_X, "GAMES" } };
	if (host->draw_hints(screen, MOSAIC_HINTS_LEFT, games, 1, &drawn) == 0) top = drawn.y;
	if (open) {
		MosaicHint a[] = { { MOSAIC_BUTTON_A, open } };
		if (host->draw_hints(screen, MOSAIC_HINTS_RIGHT, a, 1, &drawn) == 0 && drawn.y < top) top = drawn.y;
	}
	return top;
}

// Where the tab bar was drawn, and where its free room right of the tabs
// starts, at the last render.
static MosaicRect tabs_bar;
static int tabs_end = 0;

// The tab bar, or the mode's title when it has no tabs.
static void drawTabs(MosaicSurface* screen, const MosaicRect* bar) {
	tabs_bar = *bar;
	tabs_end = bar->x + bar->w;
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int text_h = host->text_height(MOSAIC_FONT_LARGE);
	int y = bar->y + (bar->h - text_h) / 2;
	if (mode != HD_MODE_DASHBOARD) {
		const char* title = mode == HD_MODE_MINUI ? tr("Recently played")
			: mode == HD_MODE_RETROACHIEVEMENTS ? tr("Achievements")
			: mode == HD_MODE_CONSOLES ? tr("Consoles") : "Pico-8";
		host->draw_text(screen, MOSAIC_FONT_LARGE, title, bar->x + pad, y, C_TEXT(), bar->w - pad);
		return;
	}
	int x = bar->x;
	int right = bar->x + bar->w;
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	if (tab_count > 1) {
		int w = host->draw_text(screen, MOSAIC_FONT_SMALL, "L1", x, bar->y + (bar->h - small_h) / 2, C_HINT(), right - x);
		if (w > 0) x += w + pad / 2;
	}
	for (int i = 0; i < tab_count && x < right; i++) {
		const char* label = tr(HD_tabLabel(tabs[i]));
		int w = host->text_width(MOSAIC_FONT_LARGE, label) + 2 * pad;
		if (x + w > right) w = right - x;
		MosaicRect pill = { x, bar->y, w, bar->h };
		int selected = tabs[i] == tab;
		if (selected) host->fill_round_rect(screen, &pill, bar->h / 2, C_MAIN());
		host->draw_text(screen, MOSAIC_FONT_LARGE, label, x + pad, y, selected ? C_SELECTED() : C_TEXT(), w - 2 * pad);
		x += w;
	}
	if (tab_count > 1 && x + pad / 2 < right) {
		int w = host->draw_text(screen, MOSAIC_FONT_SMALL, "R1", x + pad / 2, bar->y + (bar->h - small_h) / 2, C_HINT(),
			right - x - pad / 2);
		if (w > 0) x += pad / 2 + w;
	}
	tabs_end = x;
}

// A line of text centred in rect, in the hint colour.
static void drawMessage(MosaicSurface* screen, const MosaicRect* rect, const char* message) {
	int w = host->text_width(MOSAIC_FONT_LARGE, message);
	int h = host->text_height(MOSAIC_FONT_LARGE);
	int x = rect->x + (rect->w - (w > 0 ? w : 0)) / 2;
	host->draw_text(screen, MOSAIC_FONT_LARGE, message, x < rect->x ? rect->x : x, rect->y + (rect->h - h) / 2,
		C_HINT(), rect->w);
}

// rows[] as list rows in rect, the selected one on a pill, scrolled to
// keep it in view; with none (-1), from *first as up and down scroll it,
// and never with a heading last.
static void drawRows(MosaicSurface* screen, const MosaicRect* rect, int selected, int* first) {
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int row_h = 30 * scale;
	int fit = rect->h / row_h;
	if (fit < 1) return;
	int shown = fit;
	if (selected >= 0) *first = HD_window(selected, row_count, fit, *first);
	else {
		stats_fit = fit;
		*first = HD_scroll(*first, 0, row_count, fit);
		int header[ROWS_MAX];
		for (int i = 0; i < row_count; i++) header[i] = rows[i].header;
		shown = HD_shown(header, row_count, *first, fit);
	}
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	for (int i = *first; i < row_count && i < *first + shown; i++) {
		Row* row = &rows[i];
		MosaicRect r = { rect->x, rect->y + (i - *first) * row_h, rect->w, row_h };
		int on = i == selected;
		if (on) host->fill_round_rect(screen, &r, row_h / 2, C_MAIN());
		int right_w = 0;
		if (row->right[0]) {
			right_w = host->text_width(MOSAIC_FONT_SMALL, row->right);
			if (right_w > r.w / 2) right_w = r.w / 2;
			host->draw_text(screen, MOSAIC_FONT_SMALL, row->right, r.x + r.w - pad - right_w, r.y + (row_h - small_h) / 2,
				on ? C_SELECTED() : C_HINT(), right_w);
		}
		int font = row->header ? MOSAIC_FONT_SMALL : MOSAIC_FONT_LARGE;
		int text_h = row->header ? small_h : large_h;
		int name_w = r.w - 2 * pad - (right_w ? right_w + pad : 0);
		host->draw_text(screen, font, row->name, r.x + pad, r.y + (row_h - text_h) / 2,
			on ? C_SELECTED() : row->header ? C_HINT() : C_TEXT(), name_w);
	}
}

static Row* addRow(const char* name, Item* item) {
	if (row_count >= ROWS_MAX) return NULL;
	Row* row = &rows[row_count++];
	memset(row, 0, sizeof(*row));
	row->name = name;
	row->item = item;
	return row;
}

static int historyKnown(void) {
	return host->play_time_today() >= 0;
}

// playtime-captions' caption for the game at path, fitted to width in the
// small font, from its captions.text service. 1 when it answered (out may
// be "" for no caption), 0 without it.
static int captionOf(const char* path, int width, char* out, size_t size) {
	char args[MAX_PATH_LEN + 32], reply[512];
	snprintf(args, sizeof(args), "path=%s\nwidth=%i", path, width > 0 ? width : 0);
	out[0] = '\0';
	if (!ask("captions.text", args, reply, sizeof(reply))) return 0;
	host->event_value(reply, "text", out, size);
	return 1;
}

// what the caption and time left row last showed, logged when it changes
static char shown_caption[160];
static char shown_left[96];

static void logShown(char* shown, size_t size, const char* what, const char* text) {
	if (!strcmp(shown, text)) return;
	snprintf(shown, size, "%s", text);
	char message[300];
	snprintf(message, sizeof(message), "%s: %s", what, text[0] ? text : "none");
	say(message);
}

// The Stats tab's time left row's text, from playtime-limit's limit.left
// while a limit is set. 1 for a row.
static int timeLeftOf(char* out, size_t size) {
	char reply[256], state[16];
	out[0] = '\0';
	if (!ask("limit.left", NULL, reply, sizeof(reply)) || host->event_value(reply, "state", state, sizeof(state)) <= 0) return 0;
	return HD_timeLeft(state, (int)replyNumber(reply, "limit", 0), (int)replyNumber(reply, "granted", 0),
		replyNumber(reply, "left", -1), out, size);
}

// The continue card for the last game played, filled while on.
static void drawCard(MosaicSurface* screen, const HDLayout* layout, int on) {
	int scale = host->screen_scale();
	int radius = 8 * scale;
	host->fill_round_rect(screen, &layout->card, radius, on ? C_MAIN() : dimCard());
	MosaicColor text = on ? C_SELECTED() : C_TEXT();
	MosaicColor muted = on ? C_SELECTED() : C_HINT();
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	const MosaicRect* t = &layout->text;
	if (!recent_count) {
		host->draw_text(screen, MOSAIC_FONT_LARGE, tr("Nothing played yet"), t->x, t->y + (t->h - large_h) / 2, muted, t->w);
		return;
	}
	Item* item = &recents[0];
	drawArt(screen, item, &layout->art, 1, radius);
	int y = t->y;
	host->draw_text(screen, MOSAIC_FONT_SMALL, tr("CONTINUE"), t->x, y, muted, t->w);
	y += small_h + 4 * scale;
	host->draw_text(screen, MOSAIC_FONT_LARGE, item->name, t->x, y, text, t->w);
	y += large_h + 4 * scale;
	char line[128] = "";
	if (!item->available) snprintf(line, sizeof(line), "%s", tr("Its emulator isn't installed"));
	else if (!captionOf(item->path, t->w, line, sizeof(line)) && historyKnown()) {
		HD_playedLine(host->play_time(item->path), line, sizeof(line));
	}
	if (line[0]) host->draw_text(screen, MOSAIC_FONT_SMALL, line, t->x, y, muted, t->w);
	logShown(shown_caption, sizeof(shown_caption), "continue line", line);
}

// text centred under a tile, in the small font, in width centred on it
static void drawUnder(MosaicSurface* screen, const MosaicRect* box, const char* text, int selected, int below, int width) {
	char cut[160];
	int w = host->text_truncate(MOSAIC_FONT_SMALL, text, width, cut, sizeof(cut));
	host->draw_text(screen, MOSAIC_FONT_SMALL, cut, box->x + (box->w - (w > 0 ? w : 0)) / 2, box->y + box->h + below,
		selected ? C_TEXT() : C_HINT(), width);
}

// A shortcut's tile: its quick menu tile's icon, or its label's first letter.
static void drawShortcut(MosaicSurface* screen, const Shortcut* shortcut, const MosaicRect* box, int radius) {
	const MosaicSurface* image = NULL;
	int got = shortcut->icon[0] ? host->art_load(shortcut->icon, &image) : MOSAIC_ART_MISSING;
	if (got == MOSAIC_ART_LOADING) waiting = 1;
	int w = 0, h = 0;
	MosaicRect fit;
	if (got == MOSAIC_ART_READY && image && host->surface_size(image, &w, &h) == 0 && HD_fit(w, h, box, &fit) == 0) {
		host->fill_round_rect(screen, box, radius, dimCard());
		host->blit(screen, image, NULL, &fit);
		return;
	}
	drawLetter(screen, box, shortcut->label, radius);
}

// The home tab: the continue card, and under it the row in focus (or the
// first), with its heading while there's more than one.
static void drawHome(MosaicSurface* screen, const HDLayout* given) {
	int scale = host->screen_scale();
	int radius = 8 * scale;
	if (focus.row >= home_row_count) focus = (HDFocus){ HD_FOCUS_CONTINUE, 0 };
	HDLayout layout = *given;
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	// the heading in the small font, or the tiny one where that doesn't fit;
	// the card keeps room for its three lines of text
	int card_min_h = 2 * small_h + host->text_height(MOSAIC_FONT_LARGE) + 8 * scale + 2 * layout.gap;
	static const int title_fonts[] = { MOSAIC_FONT_SMALL, MOSAIC_FONT_TINY };
	int title_font = MOSAIC_FONT_SMALL, title_h = 0;
	for (int f = 0; f < 2 && home_row_count > 1 && layout.card.h == given->card.h && layout.shelf.y == given->shelf.y; f++) {
		title_font = title_fonts[f];
		title_h = host->text_height(title_font) + 4 * scale;
		HD_layoutTitle(&layout, title_h, card_min_h, small_h + 4 * scale, scale);
	}
	int titled = layout.card.h != given->card.h || layout.shelf.y != given->shelf.y;
	drawCard(screen, &layout, focus.row == HD_FOCUS_CONTINUE);

	shelf_cells = layout.cells;
	if (!layout.cells || !home_row_count) return;
	HomeRow* row = &home_rows[focus.row < 0 ? 0 : focus.row];
	const char* heading = tr(HD_rowLabel(row->kind));
	if (titled) {
		host->draw_text(screen, title_font, heading, layout.shelf.x, layout.shelf.y - title_h, C_HINT(), layout.shelf.w);
	} else if (home_row_count > 1) {
		// no room over the shelf: at the right of the tab bar, in the
		// largest of those fonts it fits in
		int right = tabs_bar.x + tabs_bar.w;
		for (int f = 0; f < 2; f++) {
			int w = host->text_width(title_fonts[f], heading);
			if (w <= 0 || right - w < tabs_end + 10 * scale) continue;
			int h = host->text_height(title_fonts[f]);
			host->draw_text(screen, title_fonts[f], heading, right - w, tabs_bar.y + (tabs_bar.h - h) / 2, C_HINT(), w);
			break;
		}
	}
	int on = focus.row >= 0;
	shelf_first = HD_window(on ? focus.col : shelf_first, row->count, layout.cells, shelf_first);
	for (int k = 0; k < layout.cells && shelf_first + k < row->count; k++) {
		int index = shelf_first + k;
		MosaicRect cell = { layout.shelf.x + k * (layout.cell + layout.gap), layout.shelf.y + 2 * scale, layout.cell,
			layout.cell };
		int selected = on && index == focus.col;
		if (selected) {
			MosaicRect frame = { cell.x - 2 * scale, cell.y - 2 * scale, cell.w + 4 * scale, cell.h + 4 * scale };
			host->fill_round_rect(screen, &frame, radius, C_MAIN());
		}
		if (row->kind == HD_ROW_SHORTCUTS) {
			const Shortcut* shortcut = &shortcuts[shortcut_shown[index]];
			drawShortcut(screen, shortcut, &cell, radius);
			drawUnder(screen, &cell, shortcut->label, selected, 4 * scale, cell.w);
		} else {
			drawArt(screen, row->items[index], &cell, 0, radius);
			drawUnder(screen, &cell, row->items[index]->name, selected, 4 * scale, cell.w);
		}
	}
}

// The minimal mode: the clock, and the continue card under it.
static void drawMinimal(MosaicSurface* screen, const HDLayout* given) {
	int scale = host->screen_scale();
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	HDLayout layout;
	if (HD_minimalLayout(&given->content, large_h, scale, &layout) != 0) return;
	time_t now = time(NULL);
	struct tm local;
	char clock[32] = "";
	if (localtime_r(&now, &local)) HD_clock(local.tm_hour, local.tm_min, clock24, clock, sizeof(clock));
	shown_minute = (long)(now / 60);
	int w = host->text_width(MOSAIC_FONT_LARGE, clock);
	const MosaicRect* c = &layout.content;
	host->draw_text(screen, MOSAIC_FONT_LARGE, clock, c->x + (c->w - (w > 0 ? w : 0)) / 2, c->y, C_TEXT(), c->w);
	drawCard(screen, &layout, 1);
}

// count tiles in a grid in content, with the selected one framed and kept
// in view; tile draws each, with what goes under it
static void drawGrid(MosaicSurface* screen, const MosaicRect* content, int count, int* selected, int* first_row,
		void (*tile)(MosaicSurface* screen, int index, const MosaicRect* box, int selected)) {
	int scale = host->screen_scale();
	int gap = 10 * scale;
	int name_h = host->text_height(MOSAIC_FONT_SMALL) + 4 * scale;
	int cell, view_rows;
	if (HD_grid(content, scale, name_h, &cell, &grid_cols, &view_rows) != 0) return;
	if (*selected >= count) *selected = count - 1;
	int total_rows = (count + grid_cols - 1) / grid_cols;
	*first_row = HD_window(*selected / grid_cols, total_rows, view_rows, *first_row);
	int radius = 8 * scale;
	for (int r = 0; r < view_rows; r++) {
		for (int col = 0; col < grid_cols; col++) {
			int index = (*first_row + r) * grid_cols + col;
			if (index >= count) return;
			MosaicRect box = { content->x + col * (cell + gap), content->y + r * (cell + name_h + gap), cell, cell };
			int on = index == *selected;
			if (on) {
				MosaicRect frame = { box.x - 2 * scale, box.y - 2 * scale, box.w + 4 * scale, box.h + 4 * scale };
				host->fill_round_rect(screen, &frame, radius, C_MAIN());
			}
			tile(screen, index, &box, on);
		}
	}
}

static int measureFont(const char* text, void* data) {
	return host->text_width(*(const int*)data, text);
}

// A console without art: its name on a tile in the theme's colours, in the
// largest font its words fit across in.
static void drawConsoleName(MosaicSurface* screen, const char* name, const MosaicRect* box, int radius) {
	host->fill_round_rect(screen, box, radius, C_ACCENT());
	int scale = host->screen_scale();
	int pad = 6 * scale;
	static const int fonts[] = { MOSAIC_FONT_LARGE, MOSAIC_FONT_SMALL, MOSAIC_FONT_TINY };
	char lines[4][HD_LINE_MAX];
	int n = 0, font = MOSAIC_FONT_TINY, line_h = 0;
	for (int f = 0; f < 3; f++) {
		font = fonts[f];
		line_h = host->text_height(font);
		int max_lines = line_h > 0 ? (box->h - 2 * pad) / line_h : 0;
		if (max_lines > 4) max_lines = 4;
		if (max_lines < 1) continue;
		n = HD_wrap(name, box->w - 2 * pad, measureFont, &font, lines, max_lines);
		if (HD_linesFit(lines, n, box->w - 2 * pad, measureFont, &font)) break;
	}
	int y = HD_linesTop(box, n, line_h);
	for (int i = 0; i < n; i++) {
		char cut[HD_LINE_MAX];
		int w = host->text_truncate(font, lines[i], box->w - 2 * pad, cut, sizeof(cut));
		host->draw_text(screen, font, cut, box->x + (box->w - (w > 0 ? w : 0)) / 2, y + i * line_h, C_TEXT(), box->w - 2 * pad);
	}
}

static void consoleTile(MosaicSurface* screen, int index, const MosaicRect* box, int selected) {
	int scale = host->screen_scale();
	int radius = 8 * scale;
	const HDConsole* console = &consoles[index];
	const MosaicEntry* entry = console_entries[index];
	const MosaicSurface* image = NULL;
	int got = MOSAIC_ART_MISSING;
	static const int kinds[] = { MOSAIC_ART_THUMBNAIL, MOSAIC_ART_LOGO, MOSAIC_ART_BOX };
	for (int k = 0; entry && k < 3 && got == MOSAIC_ART_MISSING; k++) got = host->art_get(entry, kinds[k], &image);
	if (got == MOSAIC_ART_LOADING) waiting = 1;
	int w = 0, h = 0;
	MosaicRect fit;
	if (got == MOSAIC_ART_READY && image && host->surface_size(image, &w, &h) == 0 && HD_fit(w, h, box, &fit) == 0) {
		host->fill_round_rect(screen, box, radius, dimCard());
		host->blit(screen, image, NULL, &fit);
	} else {
		drawConsoleName(screen, console->name, box, radius);
	}
	char games[64];
	snprintf(games, sizeof(games), trPlural("%i game", "%i games", (unsigned long)console->games), console->games);
	drawUnder(screen, box, games, selected, 2 * scale, box->w + 10 * scale); // into the gaps beside it
}

static void drawConsoles(MosaicSurface* screen, const HDLayout* layout) {
	if (!console_count) {
		drawMessage(screen, &layout->content, card_dirty ? tr("Reading the card") : tr("No games on the card"));
		return;
	}
	drawGrid(screen, &layout->content, console_count, &console_selected, &console_first_row, consoleTile);
}

static void drawPinned(MosaicSurface* screen, const HDLayout* layout) {
	row_count = 0;
	for (int i = 0; i < pinned_count; i++) addRow(pinned[i].name, &pinned[i]);
	if (!row_count) {
		drawMessage(screen, &layout->content, tr("No favorites yet"));
		return;
	}
	list_selected = HD_listMove(list_selected, row_count, 0, 0);
	drawRows(screen, &layout->content, list_selected, &list_first);
}

// most played of the games the dashboard knows: recents and favorites
static void drawStats(MosaicSurface* screen, const HDLayout* layout) {
	row_count = 0;
	long long today = host->play_time_today();
	if (today < 0) {
		drawMessage(screen, &layout->content, tr("No play history yet"));
		return;
	}
	Row* row = addRow(tr("Played today"), NULL);
	HD_duration(today, row->right, sizeof(row->right));
	if (!row->right[0]) snprintf(row->right, sizeof(row->right), "%s", "0m");
	char left[sizeof(row->right)];
	if (timeLeftOf(left, sizeof(left))) {
		row = addRow(tr("Time left today"), NULL);
		snprintf(row->right, sizeof(row->right), "%s", left);
	}
	logShown(shown_left, sizeof(shown_left), "time left today", left);

	Item* known[RECENTS_MAX + PINNED_MAX];
	long long times[RECENTS_MAX + PINNED_MAX];
	int count = 0;
	long long recent_total = 0;
	int recent_games = 0;
	for (int i = 0; i < recent_count; i++) {
		if (recents[i].type != MOSAIC_ENTRY_ROM) continue;
		known[count] = &recents[i];
		times[count] = host->play_time(recents[i].path);
		if (times[count] > 0) recent_total += times[count];
		recent_games++;
		count++;
	}
	for (int i = 0; i < pinned_count; i++) {
		int seen = 0;
		for (int k = 0; k < count && !seen; k++) seen = !strcmp(known[k]->path, pinned[i].path);
		if (seen || pinned[i].type != MOSAIC_ENTRY_ROM) continue;
		known[count] = &pinned[i];
		times[count] = host->play_time(pinned[i].path);
		count++;
	}
	row = addRow(tr("Recently played"), NULL);
	char total[32];
	HD_duration(recent_total, total, sizeof(total));
	char games[64];
	snprintf(games, sizeof(games), trPlural("%i game", "%i games", (unsigned long)recent_games), recent_games);
	snprintf(row->right, sizeof(row->right), "%.40s%s%s", games, total[0] ? " \xc2\xb7 " : "", total);

	int top[MOST_PLAYED_MAX];
	int n = HD_mostPlayed(times, count, top, MOST_PLAYED_MAX);
	if (n) addRow(tr("MOST PLAYED"), NULL)->header = 1;
	for (int i = 0; i < n; i++) {
		Item* item = known[top[i]];
		row = addRow(item->name, item);
		if (!row) break;
		char played[32];
		HD_duration(times[top[i]], played, sizeof(played));
		int sessions = host->play_count(item->path);
		if (sessions > 0) {
			char count[64];
			snprintf(count, sizeof(count), trPlural("%i session", "%i sessions", (unsigned long)sessions), sessions);
			snprintf(row->right, sizeof(row->right), "%s \xc2\xb7 %.50s", played, count);
		} else {
			snprintf(row->right, sizeof(row->right), "%s", played);
		}
	}
	// romm's, while it has downloaded games still on the card
	if (download_count) addRow(tr("RECENTLY DOWNLOADED"), NULL)->header = 1;
	for (int i = 0; i < download_count; i++) {
		row = addRow(downloads[i].name, &downloads[i]);
		if (!row) break;
		HD_daysAgo(daysSince(downloaded_at[i]), row->right, sizeof(row->right));
	}
	drawRows(screen, &layout->content, -1, &stats_first);
}

static void drawMinUI(MosaicSurface* screen, const HDLayout* layout) {
	row_count = 0;
	for (int i = 0; i < recent_count; i++) addRow(recents[i].name, &recents[i]);
	addRow(tr("Games"), NULL);
	list_selected = HD_listMove(list_selected, row_count, 0, 0);
	drawRows(screen, &layout->content, list_selected, &list_first);
}

// recent and pinned games with achievements on the card; ones still being
// hashed come in on a later frame
static int pending = 0;
static void raRows(void) {
	row_count = 0;
	pending = 0;
	for (int pass = 0; pass < 2; pass++) {
		Item* items = pass ? pinned : recents;
		int n = pass ? pinned_count : recent_count;
		for (int i = 0; i < n; i++) {
			Item* item = &items[i];
			if (item->type != MOSAIC_ENTRY_ROM) continue;
			int seen = 0;
			for (int k = 0; k < row_count && !seen; k++) seen = !strcmp(rows[k].item->path, item->path);
			if (seen) continue;
			MosaicAchievements progress = { .size = sizeof(MosaicAchievements) };
			int got = host->achievements(item->path, &progress);
			if (got == MOSAIC_ACHIEVEMENTS_UNKNOWN) pending++;
			if (got != 1 || progress.total <= 0) continue;
			Row* row = addRow(item->name, item);
			if (!row) return;
			HD_achievementLine(progress.unlocked, progress.total, progress.points_unlocked, progress.points, row->right,
				sizeof(row->right));
		}
	}
}

// rows from raRows, which render runs first
static void drawAchievements(MosaicSurface* screen, const HDLayout* layout) {
	if (pending) waiting = 1;
	if (!row_count) {
		drawMessage(screen, &layout->content, pending ? tr("Checking achievements") : tr("No achievements on the card yet"));
		return;
	}
	list_selected = HD_listMove(list_selected, row_count, 0, 0);
	drawRows(screen, &layout->content, list_selected, &list_first);
}

static void cartTile(MosaicSurface* screen, int index, const MosaicRect* box, int selected) {
	drawArt(screen, &carts[index], box, 0, 8 * host->screen_scale());
	drawUnder(screen, box, carts[index].name, selected, 2 * host->screen_scale(), box->w);
}

static void drawCarts(MosaicSurface* screen, const HDLayout* layout) {
	if (!cart_count) {
		drawMessage(screen, &layout->content, tr("No Pico-8 carts"));
		return;
	}
	drawGrid(screen, &layout->content, cart_count, &cart_selected, &cart_first_row, cartTile);
}

// What A does with what's selected now, for its hint: NULL for nothing.
static const char* openLabel(void) {
	Item* item = NULL;
	switch (mode) {
		case HD_MODE_DASHBOARD:
			if (tab == HD_TAB_HOME) {
				if (focus.row == HD_FOCUS_CONTINUE) item = recent_count ? &recents[0] : NULL;
				else if (focus.row < home_row_count && focus.col < home_rows[focus.row].count) {
					if (home_rows[focus.row].kind == HD_ROW_SHORTCUTS) return "OPEN";
					item = home_rows[focus.row].items[focus.col];
				}
			}
			else if (tab == HD_TAB_PINNED && list_selected >= 0 && list_selected < pinned_count) item = &pinned[list_selected];
			break;
		case HD_MODE_MINUI:
			if (list_selected == recent_count) return "OPEN";
			if (list_selected >= 0 && list_selected < recent_count) item = &recents[list_selected];
			break;
		case HD_MODE_RETROACHIEVEMENTS:
			if (list_selected >= 0 && list_selected < row_count) item = rows[list_selected].item;
			break;
		case HD_MODE_PICO8:
			if (cart_selected >= 0 && cart_selected < cart_count) item = &carts[cart_selected];
			break;
		case HD_MODE_MINIMAL:
			item = recent_count ? &recents[0] : NULL;
			break;
		case HD_MODE_CONSOLES:
			return console_count ? "OPEN" : NULL;
	}
	if (!item || !item->available) return NULL;
	return HD_openLabel(item->type, item->can_resume);
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	waiting = 0;
	// achievements' rows decide A's hint, so they come first
	if (mode == HD_MODE_RETROACHIEVEMENTS) raRows();
	int hints_y = drawHints(screen, openLabel());
	int scale = host->screen_scale();
	int name_h = host->text_height(MOSAIC_FONT_SMALL) + 4 * scale;
	int shelf = mode == HD_MODE_DASHBOARD && tab == HD_TAB_HOME && shelf_on;
	HDLayout layout;
	if (HD_layout(&view->area, &view->status, &view->header, view->status.h, hints_y, scale, name_h, shelf, &layout) != 0) {
		return 0; // too small to draw anything but the hints
	}
	if (mode != HD_MODE_MINIMAL) drawTabs(screen, &layout.tabs);
	switch (mode) {
		case HD_MODE_MINIMAL: drawMinimal(screen, &layout); break;
		case HD_MODE_CONSOLES: drawConsoles(screen, &layout); break;
		case HD_MODE_MINUI: drawMinUI(screen, &layout); break;
		case HD_MODE_RETROACHIEVEMENTS: drawAchievements(screen, &layout); break;
		case HD_MODE_PICO8: drawCarts(screen, &layout); break;
		default:
			if (tab == HD_TAB_PINNED) drawPinned(screen, &layout);
			else if (tab == HD_TAB_STATS) drawStats(screen, &layout);
			else drawHome(screen, &layout);
	}
	return 0;
}

///////////////////////////////////////
// input

static int pressedBit(unsigned int bits, int button) {
	return (bits & MOSAIC_BUTTON_BIT(button)) != 0;
}

// up and down through a list of count, wrapping on a single press
static int listInput(const MosaicInput* input, int count) {
	int before = list_selected;
	if (pressedBit(input->repeated, MOSAIC_BUTTON_UP)) {
		list_selected = HD_listMove(list_selected, count, -1, pressedBit(input->pressed, MOSAIC_BUTTON_UP));
	}
	if (pressedBit(input->repeated, MOSAIC_BUTTON_DOWN)) {
		list_selected = HD_listMove(list_selected, count, 1, pressedBit(input->pressed, MOSAIC_BUTTON_DOWN));
	}
	int page = 5;
	if (pressedBit(input->repeated, MOSAIC_BUTTON_LEFT)) list_selected = HD_listMove(list_selected, count, -page, 0);
	if (pressedBit(input->repeated, MOSAIC_BUTTON_RIGHT)) list_selected = HD_listMove(list_selected, count, page, 0);
	return list_selected != before;
}

// moves through a grid of count with grid_cols across
static int gridInput(const MosaicInput* input, int count, int* selected) {
	int before = *selected;
	if (count) {
		if (pressedBit(input->repeated, MOSAIC_BUTTON_LEFT)) *selected = HD_listMove(*selected, count, -1, 0);
		if (pressedBit(input->repeated, MOSAIC_BUTTON_RIGHT)) *selected = HD_listMove(*selected, count, 1, 0);
		if (pressedBit(input->repeated, MOSAIC_BUTTON_UP)) *selected = HD_listMove(*selected, count, -grid_cols, 0);
		if (pressedBit(input->repeated, MOSAIC_BUTTON_DOWN)) *selected = HD_listMove(*selected, count, grid_cols, 0);
	}
	return *selected != before;
}

// A on the home tab: the continue card's game, a row's game, or a
// shortcut's action
static void openHome(void) {
	if (focus.row == HD_FOCUS_CONTINUE) {
		if (recent_count) openItem(&recents[0]);
		return;
	}
	if (focus.row >= home_row_count || focus.col >= home_rows[focus.row].count) return;
	HomeRow* row = &home_rows[focus.row];
	if (row->kind != HD_ROW_SHORTCUTS) {
		openItem(row->items[focus.col]);
		return;
	}
	const Shortcut* shortcut = &shortcuts[shortcut_shown[focus.col]];
	int ran = host->run_action(shortcut->id, NULL);
	char message[160];
	snprintf(message, sizeof(message), "shortcut %s %i", shortcut->id, ran);
	say(message);
}

static void setTab(int index) {
	tab = tabs[index];
	list_selected = 0;
	list_first = 0;
	stats_first = 0;
	char message[64];
	snprintf(message, sizeof(message), "tab %s", HD_tabLabel(tab));
	say(message);
}

static int input(const MosaicInput* input, void* data) {
	(void)data;
	unsigned int pressed = input->pressed;
	if (pressedBit(pressed, MOSAIC_BUTTON_MENU)) {
		host->run_action("gamelist.quick_menu", NULL);
		return MOSAIC_SCREEN_STAY;
	}
	if (pressedBit(pressed, MOSAIC_BUTTON_SELECT)) {
		host->run_action("gamelist.switcher", NULL);
		return MOSAIC_SCREEN_STAY;
	}
	if (pressedBit(pressed, MOSAIC_BUTTON_X)) {
		say("games");
		return MOSAIC_SCREEN_CLOSE;
	}

	int moved = 0;
	int open = pressedBit(pressed, MOSAIC_BUTTON_A);
	switch (mode) {
		case HD_MODE_DASHBOARD: {
			int step = pressedBit(pressed, MOSAIC_BUTTON_R1) - pressedBit(pressed, MOSAIC_BUTTON_L1);
			if (step && tab_count > 1) {
				setTab(HD_tabStep(HD_tabIndex(tabs, tab_count, tab), tab_count, step));
				host->request_redraw();
				return MOSAIC_SCREEN_STAY;
			}
			if (tab == HD_TAB_HOME) {
				int counts[HD_ROW_KINDS];
				int shown = shelf_on && shelf_cells ? home_row_count : 0;
				for (int i = 0; i < shown; i++) counts[i] = home_rows[i].count;
				HDFocus before = focus;
				static const int dpad[] = { MOSAIC_BUTTON_UP, MOSAIC_BUTTON_DOWN, MOSAIC_BUTTON_LEFT, MOSAIC_BUTTON_RIGHT };
				for (int i = 0; i < 4; i++) {
					if (pressedBit(input->repeated, dpad[i])) HD_rowsMove(&focus, counts, shown, dpad[i]);
				}
				if (focus.row != before.row) shelf_first = 0;
				moved = focus.row != before.row || focus.col != before.col;
				if (open) openHome();
			}
			else if (tab == HD_TAB_STATS) {
				int before = stats_first;
				if (pressedBit(input->repeated, MOSAIC_BUTTON_UP)) stats_first = HD_scroll(stats_first, -1, row_count, stats_fit);
				if (pressedBit(input->repeated, MOSAIC_BUTTON_DOWN)) stats_first = HD_scroll(stats_first, 1, row_count, stats_fit);
				moved = stats_first != before;
			}
			else if (tab == HD_TAB_PINNED) {
				moved = listInput(input, pinned_count);
				if (open && list_selected >= 0 && list_selected < pinned_count) openItem(&pinned[list_selected]);
			}
			break;
		}
		case HD_MODE_MINUI:
			moved = listInput(input, recent_count + 1);
			if (open) {
				if (list_selected == recent_count) {
					say("games");
					return MOSAIC_SCREEN_CLOSE;
				}
				if (list_selected >= 0 && list_selected < recent_count) openItem(&recents[list_selected]);
			}
			break;
		case HD_MODE_RETROACHIEVEMENTS:
			moved = listInput(input, row_count);
			if (open && list_selected >= 0 && list_selected < row_count) openItem(rows[list_selected].item);
			break;
		case HD_MODE_PICO8:
			moved = gridInput(input, cart_count, &cart_selected);
			if (open && cart_selected >= 0 && cart_selected < cart_count) openItem(&carts[cart_selected]);
			break;
		case HD_MODE_MINIMAL:
			if (open && recent_count) openItem(&recents[0]);
			break;
		case HD_MODE_CONSOLES:
			moved = gridInput(input, console_count, &console_selected);
			if (open && console_selected >= 0 && console_selected < console_count) {
				const HDConsole* console = &consoles[console_selected];
				int opened = host->open_path(console->path, MOSAIC_ENTRY_DIR);
				char message[MAX_PATH_LEN + 32];
				snprintf(message, sizeof(message), "open %s %i", console->path, opened);
				say(message);
				if (opened) return MOSAIC_SCREEN_CLOSE;
			}
			break;
	}
	if (moved) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

///////////////////////////////////////
// opening and closing

// what the home tab's rows hold, for the log
static void logRows(void) {
	char message[512];
	int used = snprintf(message, sizeof(message), "rows:");
	for (int i = 0; i < home_row_count && used < (int)sizeof(message); i++) {
		used += snprintf(message + used, sizeof(message) - used, " %s %i", HD_rowName(home_rows[i].kind), home_rows[i].count);
	}
	if (!home_row_count) snprintf(message + used, sizeof(message) - used, " none");
	say(message);
}

static void logConsoles(void) {
	char message[512];
	int used = snprintf(message, sizeof(message), "consoles %i:", console_count);
	for (int i = 0; i < console_count && used < (int)sizeof(message); i++) {
		used += snprintf(message + used, sizeof(message) - used, "%s %s %i", i ? "," : "", consoles[i].name, consoles[i].games);
	}
	say(message);
}

// The card index or a set changed: build what comes from them again, now
// while the screen shows, else as it next opens.
static void cardChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	card_dirty = 1;
	if (!showing) return;
	loadCard();
	if (mode == HD_MODE_DASHBOARD) {
		loadShortcuts();
		loadRows();
	}
	if (focus.row >= home_row_count) focus = (HDFocus){ HD_FOCUS_CONTINUE, 0 };
	if (mode == HD_MODE_DASHBOARD) logRows();
	else if (mode == HD_MODE_CONSOLES) logConsoles();
	host->request_redraw();
}

static int enter(void* data) {
	(void)data;
	loadRecents();
	if (mode == HD_MODE_RETROACHIEVEMENTS || (mode == HD_MODE_DASHBOARD && pinned_on)) loadPinned();
	if (mode == HD_MODE_PICO8) loadCarts();
	download_count = 0;
	if (mode == HD_MODE_DASHBOARD && stats_on) loadDownloads();
	if (card_dirty) loadCard();
	if (mode == HD_MODE_DASHBOARD) {
		loadShortcuts();
		loadRows();
	}
	tab_count = HD_tabs(pinned_on, favorites_loaded, stats_on, tabs);
	tab = tabs[HD_tabIndex(tabs, tab_count, tab)];
	if (focus.row >= home_row_count) focus = (HDFocus){ HD_FOCUS_CONTINUE, 0 };
	if (cart_selected >= cart_count) cart_selected = 0;
	showing = 1;
	stats_first = 0;
	shown_caption[0] = '\x01'; // logged again on the first frame
	shown_left[0] = '\x01';
	char message[256];
	snprintf(message, sizeof(message), "home: mode %s, tab %s of %i, recents %i, pinned %i, carts %i, history %s, downloads %i",
		HD_modeName(mode), HD_tabLabel(tab), tab_count, recent_count, pinned_count, cart_count,
		historyKnown() ? "on" : "unknown", download_count);
	say(message);
	if (mode == HD_MODE_DASHBOARD) logRows();
	else if (mode == HD_MODE_CONSOLES) logConsoles();
	return 0;
}

static void leave(void* data) {
	(void)data;
	showing = 0;
}

// art and achievements that were loading: draw again once they're in
static void tick(unsigned long now) {
	// the minimal mode's clock, as the minute changes
	if (showing && mode == HD_MODE_MINIMAL && shown_minute >= 0 && (long)(time(NULL) / 60) != shown_minute) {
		shown_minute = -1;
		host->request_redraw();
	}
	if (!showing || !waiting || now - asked_at < 250) return;
	asked_at = now;
	waiting = 0;
	host->request_redraw();
}

static int setting(const char* key, const char* fallback) {
	return !strcmp(host->cfg_get(key, fallback), "on");
}

// home-dashboard.setup: mode=<a mode> saves it, for the next start
static int provideSetup(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char wanted[64];
	int refused = 0;
	if (host->event_value(args, "mode", wanted, sizeof(wanted)) >= 0) {
		if (strcmp(HD_modeName(HD_modeOf(wanted)), wanted) != 0) refused = 1;
		else {
			host->cfg_set("mode", wanted);
			char message[sizeof(wanted) + 64];
			snprintf(message, sizeof(message), "setup: mode %s, from the next start", wanted);
			say(message);
		}
	}
	return HD_setupReply(host->cfg_get("mode", "dashboard"), refused, out, size) < 0;
}

static int init(const MosaicHost* h) {
	host = h;
	static const HDWords words = { tr, trPlural };
	HD_setWords(&words);
	if (!MOSAIC_HOST_HAS(host, draw_hints)) {
		say("needs a launcher with plugin screens, the home slot and draw_hints");
		return -1;
	}
	mode = HD_modeOf(host->cfg_get("mode", "dashboard"));
	pinned_on = setting("pinned", "on");
	stats_on = setting("stats", "on");
	shelf_on = setting("shelf", "on");
	snprintf(pico_tags, sizeof(pico_tags), "%s", host->cfg_get("pico8_tags", "P8,PICO8,PICO"));
	clock24 = strcmp(host->cfg_get("clock", "24h"), "12h") != 0;
	char unknown[256];
	row_kind_count = HD_parseRows(host->cfg_get("rows", HD_ROWS_DEFAULT), row_kinds, HD_ROW_KINDS, unknown, sizeof(unknown));
	if (unknown[0]) {
		char message[320];
		snprintf(message, sizeof(message), "rows: no such row %s", unknown);
		say(message);
	}
	shortcuts_wanted = 0;
	const char* names = host->cfg_get("shortcuts", HD_SHORTCUTS_DEFAULT);
	char name[64];
	while (shortcuts_wanted < SHORTCUTS_MAX && HD_listNext(&names, name, sizeof(name))) {
		Shortcut* shortcut = &shortcuts[shortcuts_wanted++];
		memset(shortcut, 0, sizeof(*shortcut));
		snprintf(shortcut->want, sizeof(shortcut->want), "%s", name);
	}
	card_dirty = 1;

	favorites_loaded = HD_pluginLoaded(host, "favorites");
	if (MOSAIC_HOST_HAS(host, call) && host->provide(ID ".setup", provideSetup) != 0) say("couldn't provide " ID ".setup");

	MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.flags = MOSAIC_SCREEN_HOME,
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("couldn't register the home screen");
		return -1;
	}
	// the rows and consoles from the card index, read from now so it's
	// ready as the home opens
	int index_rows = mode == HD_MODE_DASHBOARD && (wantsRow(HD_ROW_ADDED) || wantsRow(HD_ROW_RANDOM));
	if ((index_rows || mode == HD_MODE_CONSOLES) && MOSAIC_HOST_HAS(host, index_refresh)) host->index_count(NULL);
	if (MOSAIC_HOST_HAS(host, subscribe) && mode != HD_MODE_MINIMAL) {
		host->subscribe("launcher.index_ready", cardChanged, NULL);
		host->subscribe("launcher.set_changed", cardChanged, NULL);
		host->subscribe("launcher.profile_changed", cardChanged, NULL);
	}
	tab_count = HD_tabs(pinned_on, favorites_loaded, stats_on, tabs);
	char message[160];
	snprintf(message, sizeof(message), "mode %s, pinned %s, stats %s, shelf %s, favorites %s", HD_modeName(mode),
		pinned_on ? "on" : "off", stats_on ? "on" : "off", shelf_on ? "on" : "off", favorites_loaded ? "on" : "off");
	say(message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
