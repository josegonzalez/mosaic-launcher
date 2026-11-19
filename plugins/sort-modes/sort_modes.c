// Sort modes: one order for every game list, systems and collections
// alike: name (the launcher's own), most played, last played, year,
// publisher or recently added. Recently Played, the first screen and
// search's results keep their own order (see SM_sorts). The mode is the
// "mode" setting, saved whenever it changes, so it lasts across restarts.
//
// The sort-modes.next action steps to the next mode, from the game list
// button the "button" setting picks (none by default), bound on the first
// tick when no other action has it there. The sort-modes.pick action lists
// the modes with choose, from a Sort tile on the quick menu (the "tile"
// setting) or an FN button. Either lists the folders again, which keeps
// the selection on the same game. The header shows the mode while a list
// it orders is showing, and nothing in name.
//
// In each list plain folders come first, in the launcher's order, then
// games with a value for the mode, by it, then the games without one,
// each by the name the list shows (so arcade-names' titles and numeric
// prefixes taken off count). Only games in Roms have values: play time
// and last played from the play history, year and publisher from the card
// index's .info fields, and recently added from the later of the file's
// modified and changed times. A game folder (one holding a .cue or .m3u
// named after it) reads its sheet's play history as stock records it, and
// a multi-disc game whose .m3u isn't on the card yet reads its discs'.
//
// Each key is worked out once, the first time a sort asks about a path,
// and kept by path until the mode changes, the profile does or a game
// returns (for the play history) or the card index changes (for years and
// publishers), so a sort reads nothing per compare. section_key gives
// each entry its section in the mode, for jump-letter's strip and L1 and
// R1: buckets of play time, days since, the year or the publisher, and
// one for the games without a value.
//
// sort_compare decides every pair of games itself, ties by name, so it
// loads after favorites and collections-plus (their pins and file order
// win) and numeric-prefix loads after it (its numbers only order folders
// here, and every list in name).

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "sort_model.h"

#define ID "sort-modes"
#define NEXT_ID ID ".next"
#define PICK_ID ID ".pick"
#define PATH_LEN 1024
#define LISTS 8 // the lists whose games are known, more than a reload lists
#define FONT MOSAIC_FONT_SMALL

static const MosaicHost* host;
static int mode = SM_NAME;
static SMCache keys;
// this sort's keys by entry, for compares: empty but between a sort's
// first compare and list_ready, or after a sort again past list_ready
// until the next listing's filter
static SMMemo memo;
static int memo_sorting = 0;
static int bound = 0; // the button was looked at, on the first tick

// the lists a mode orders, and whether each has games, from list_ready
static struct {
	char dir[PATH_LEN];
	int games;
	unsigned long used;
} lists[LISTS];
static unsigned long uses = 0;

// the folder SM_sorts was asked about last, and its answer
static char sorts_dir[PATH_LEN];
static int sorts_answer = 0;

// the discs in one folder, for multi-disc rows not on the card yet
typedef struct Disc {
	char* name; // its file name
	char* game; // the game it's a disc of, from SM_discGame
} Disc;
static char discs_dir[PATH_LEN];
static int discs_read = 0;
static Disc* discs = NULL;
static int disc_count = 0;

// a listing's timing, from its first compare to list_ready
static struct timespec sort_started;
static char sort_dir[PATH_LEN];
static int sorting = 0;

static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void say(const char* message) {
	host->log(ID, message);
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int sorts(const char* dir) {
	if (mode == SM_NAME || !dir) return 0;
	if (strcmp(dir, sorts_dir) != 0) {
		snprintf(sorts_dir, sizeof(sorts_dir), "%s", dir);
		sorts_answer = SM_sorts(host->sdcard_path, dir);
	}
	return sorts_answer;
}

///////////////////////////////////////
// discs of multi-disc rows not on the card yet

static void discsFree(void) {
	for (int i = 0; i < disc_count; i++) {
		free(discs[i].name);
		free(discs[i].game);
	}
	free(discs);
	discs = NULL;
	disc_count = 0;
	discs_read = 0;
	discs_dir[0] = '\0';
}

// The discs in dir, read from the card once until another folder is asked
// about: multi-disc hides them from the list.
static void discsRead(const char* dir) {
	if (discs_read && !strcmp(dir, discs_dir)) return;
	discsFree();
	discs_read = 1;
	snprintf(discs_dir, sizeof(discs_dir), "%s", dir);
	DIR* dh = opendir(dir);
	if (!dh) return;
	int room = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		char game[512];
		if (dp->d_name[0] == '.' || !SM_discGame(dp->d_name, game, sizeof(game))) continue;
		if (disc_count == room) {
			int more = room ? room * 2 : 16;
			Disc* grown = realloc(discs, sizeof(Disc) * (size_t)more);
			if (!grown) break;
			discs = grown;
			room = more;
		}
		discs[disc_count].name = strdup(dp->d_name);
		discs[disc_count].game = strdup(game);
		if (!discs[disc_count].name || !discs[disc_count].game) {
			free(discs[disc_count].name);
			free(discs[disc_count].game);
			break;
		}
		disc_count++;
	}
	closedir(dh);
}

///////////////////////////////////////
// keys

// The later of the file's modified and changed times, which on a FAT card
// is when it was copied there, or 0.
static long long addedAt(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return (long long)(st.st_mtime > st.st_ctime ? st.st_mtime : st.st_ctime);
}

// The mode's play history value for one path, or 0.
static long long playOf(const char* path) {
	if (!MOSAIC_HOST_HAS(host, play_time)) return 0;
	long long value = mode == SM_MOST_PLAYED ? host->play_time(path) : host->last_played(path);
	return value > 0 ? value : 0;
}

static long long valueOf(const char* path) {
	return mode == SM_ADDED ? addedAt(path) : playOf(path);
}

// A game folder's sheet, named after the folder, as stock records it.
static int sheetOf(const char* path, char* out, size_t size) {
	const char* name = strrchr(path, '/');
	const char* extensions[] = { ".m3u", ".cue" };
	for (size_t i = 0; name && i < sizeof(extensions) / sizeof(extensions[0]); i++) {
		snprintf(out, size, "%s%s%s", path, name, extensions[i]);
		if (exists(out)) return 1;
	}
	return 0;
}

// A multi-disc row whose .m3u isn't on the card: its discs' total play
// time, latest session or newest file.
static long long discsValue(const char* path) {
	const char* slash = strrchr(path, '/');
	if (!slash) return 0;
	char dir[PATH_LEN];
	snprintf(dir, sizeof(dir), "%.*s", (int)(slash - path), path);
	discsRead(dir);
	char game[512];
	snprintf(game, sizeof(game), "%s", slash + 1);
	char* dot = strrchr(game, '.');
	if (dot) *dot = '\0';
	long long value = 0;
	for (int i = 0; i < disc_count; i++) {
		if (strcasecmp(discs[i].game, game) != 0) continue;
		char disc[PATH_LEN * 2];
		snprintf(disc, sizeof(disc), "%s/%s", dir, discs[i].name);
		long long v = valueOf(disc);
		if (mode == SM_MOST_PLAYED) value += v;
		else if (v > value) value = v;
	}
	return value;
}

static int isM3u(const char* path) {
	const char* dot = strrchr(path, '.');
	return dot && !strcasecmp(dot, ".m3u");
}

// The entry's key in the mode, publisher written to text.
static SMKey keyFor(const MosaicEntry* entry, const char* path, char* text, size_t size) {
	SMKey key = { SM_GAME, 0, 0, NULL };
	char sheet[PATH_LEN * 2];
	sheet[0] = '\0';
	int folder_game = 0;
	if (host->entry_type(entry) == MOSAIC_ENTRY_DIR) {
		folder_game = SM_inRoms(host->sdcard_path, path) && sheetOf(path, sheet, sizeof(sheet));
		if (!folder_game) {
			key.kind = SM_FOLDER;
			return key;
		}
	}
	if (!SM_inRoms(host->sdcard_path, path)) return key;
	if (SM_modeReadsIndex(mode)) {
		MosaicIndexEntry e;
		memset(&e, 0, sizeof(e));
		e.size = sizeof(e);
		if (!MOSAIC_HOST_HAS(host, index_find) || host->index_find(path, &e) < 0) return key;
		if (mode == SM_YEAR) {
			key.value = SM_yearOf(e.year);
			key.has = key.value > 0;
			return key;
		}
		const char* p = e.publisher ? e.publisher : "";
		while (*p == ' ') p++;
		snprintf(text, size, "%s", p);
		size_t len = strlen(text);
		while (len > 0 && text[len - 1] == ' ') text[--len] = '\0';
		key.has = len > 0;
		key.text = key.has ? text : NULL;
		return key;
	}
	key.value = valueOf(path);
	if (key.value <= 0 && folder_game && mode != SM_ADDED) key.value = valueOf(sheet);
	if (key.value <= 0 && isM3u(path) && !exists(path)) key.value = discsValue(path);
	key.has = key.value > 0;
	return key;
}

// The entry's key, worked out the first time.
static const SMKey* keyOf(const MosaicEntry* entry) {
	static SMKey spare;
	static int warned = 0;
	const char* path = host->entry_path(entry);
	if (!path) return NULL;
	const SMKey* kept = SM_cacheGet(&keys, path);
	if (kept) return kept;
	char text[256];
	spare = keyFor(entry, path, text, sizeof(text));
	kept = SM_cachePut(&keys, path, &spare);
	if (kept) return kept;
	if (!warned) say("out of memory keeping sort keys; working each out again");
	warned = 1;
	spare.text = NULL; // text is gone once this returns
	if (mode == SM_PUBLISHER) spare.has = 0;
	return &spare;
}

static void forget(void) {
	SM_memoEmpty(&memo);
	SM_cacheClear(&keys);
	discsFree();
}

// keyOf by entry, for compares within one listing.
static const SMKey* keyIn(const MosaicEntry* entry) {
	const SMKey* key = SM_memoGet(&memo, entry);
	if (key) return key;
	key = keyOf(entry);
	// the spare isn't kept, as it's reused
	if (key && SM_cacheGet(&keys, host->entry_path(entry)) == key) SM_memoPut(&memo, entry, key);
	return key;
}

///////////////////////////////////////
// hooks

// Keeps every entry. A listing filters its entries before it sorts, so
// a memo left from sorting a list again after list_ready, when a name
// changed, is emptied here: a freed entry's address can be another's now.
// is_filtered asking at other times only empties it sooner.
static int filter(const MosaicEntry* entry, const char* dir_path) {
	(void)entry;
	(void)dir_path;
	if (memo_sorting) {
		memo_sorting = 0;
		SM_memoEmpty(&memo);
	}
	return 1;
}

static int sortCompare(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	if (!sorts(dir_path)) return 0;
	if (!sorting || strcmp(dir_path, sort_dir) != 0) {
		sorting = 1;
		snprintf(sort_dir, sizeof(sort_dir), "%s", dir_path);
		clock_gettime(CLOCK_MONOTONIC, &sort_started);
	}
	memo_sorting = 1;
	const SMKey* ka = keyIn(a);
	if (!ka) return 0;
	SMKey copy = *ka; // keyOf's spare is reused for b
	const SMKey* kb = keyIn(b);
	if (!kb) return 0;
	return SM_compare(mode, &copy, kb, host->entry_name(a), host->entry_name(b));
}

static int sectionKey(const MosaicEntry* entry, const char* dir_path, char* out, size_t size) {
	if (!sorts(dir_path)) return 0;
	const SMKey* key = keyOf(entry);
	if (!key) return 0;
	const char* label = SM_section(mode, key, (long long)time(NULL), out, size);
	if (label) snprintf(out, size, "%s", tr(label));
	return out[0] != '\0';
}

static int listSlot(const char* dir) {
	int oldest = 0;
	for (int i = 0; i < LISTS; i++) {
		if (!strcmp(lists[i].dir, dir)) return i;
		if (lists[i].used < lists[oldest].used) oldest = i;
	}
	return oldest;
}

static void listReady(const MosaicList* list, const char* dir_path) {
	int was_sorting = sorting && dir_path && !strcmp(dir_path, sort_dir);
	sorting = 0;
	memo_sorting = 0;
	SM_memoEmpty(&memo);
	if (!sorts(dir_path)) return;
	int count = host->list_count(list), games = 0;
	const char* spelling = NULL; // the publisher before, as its first game spells it
	for (int i = 0; i < count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		const SMKey* key = keyOf(entry);
		if (key && key->kind == SM_GAME && SM_inRoms(host->sdcard_path, host->entry_path(entry))) games++;
		if (mode != SM_PUBLISHER || !key || !key->text) continue;
		// one section for a publisher spelled in more than one case,
		// as the sort put its games together
		if (spelling && !strcasecmp(spelling, key->text) && strcmp(spelling, key->text) != 0) {
			SMKey same = *key;
			same.text = (char*)spelling;
			key = SM_cachePut(&keys, host->entry_path(entry), &same);
		}
		if (key && key->text) spelling = key->text;
	}
	int slot = listSlot(dir_path);
	snprintf(lists[slot].dir, sizeof(lists[slot].dir), "%s", dir_path);
	lists[slot].games = games;
	lists[slot].used = ++uses;
	if (!was_sorting) return;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	double ms = (double)(now.tv_sec - sort_started.tv_sec) * 1000.0 + (double)(now.tv_nsec - sort_started.tv_nsec) / 1e6;
	char message[PATH_LEN + 128];
	snprintf(message, sizeof(message), "sorted %s by %s: %d entries, %d games, in %.1f ms", dir_path, SM_modeId(mode), count,
		games, ms);
	say(message);
}

///////////////////////////////////////
// the mode

// The card index answers years and publishers. It's read as soon as a
// mode other than name is on, so it's ready when one that needs it is.
static void startIndex(void) {
	if (mode != SM_NAME && MOSAIC_HOST_HAS(host, index_refresh)) host->index_count(NULL);
}

static void setMode(int to) {
	if (to < 0 || to >= SM_MODES) return;
	mode = to;
	forget();
	sorts_dir[0] = '\0';
	host->cfg_set("mode", SM_modeId(mode));
	startIndex();
	char message[128];
	snprintf(message, sizeof(message), "mode %s", SM_modeId(mode));
	say(message);
	snprintf(message, sizeof(message), tr("Sort: %s"), tr(SM_modeLabel(mode)));
	toast(message);
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

// The list showing, when the mode orders it and it has games.
static int showingSorted(void) {
	const char* current = host->current_path ? host->current_path() : NULL;
	if (!current || !sorts(current)) return 0;
	for (int i = 0; i < LISTS; i++) {
		if (!strcmp(lists[i].dir, current)) return lists[i].games > 0;
	}
	return 0;
}

static int canStep(const char* path, void* data) {
	(void)path;
	(void)data;
	const char* current = host->current_path ? host->current_path() : NULL;
	return current && SM_sorts(host->sdcard_path, current);
}

static void step(const char* path, void* data) {
	(void)path;
	(void)data;
	setMode(SM_modeNext(mode));
}

static void picked(int index, void* data) {
	(void)data;
	if (index >= 0 && index != mode) setMode(index);
}

static int canPick(const char* path, void* data) {
	(void)path;
	(void)data;
	return MOSAIC_HOST_HAS(host, choose);
}

static void pick(const char* path, void* data) {
	(void)path;
	(void)data;
	const char* choices[SM_MODES];
	for (int i = 0; i < SM_MODES; i++) choices[i] = tr(SM_modeLabel(i));
	if (host->choose(tr("Sort by"), choices, SM_MODES, mode, picked, NULL) != 0) say("couldn't list the modes");
}

// The play history changed: another profile's, or a game's new session.
static void playChanged(const char* name, const char* payload, void* data) {
	(void)payload;
	(void)data;
	if (!SM_modeReadsPlay(mode)) return;
	char message[128];
	snprintf(message, sizeof(message), "%s: sorting again", name);
	say(message);
	forget();
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
}

// The index changed: years and publishers may have.
static void indexReady(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	if (!SM_modeReadsIndex(mode)) return;
	forget();
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
}

///////////////////////////////////////
// the button

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "Y", MOSAIC_BUTTON_Y },
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
	{ "SELECT", MOSAIC_BUTTON_SELECT },
};

// The action on button on the game list, into who, or 0 when it's free.
static int taken(int button, char* who, size_t size) {
	if (!MOSAIC_HOST_HAS(host, action_info)) return 0;
	int count = host->actions_count();
	for (int i = 0; i < count; i++) {
		MosaicActionInfo info;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (host->action_info(i, &info) != 0) continue;
		int n = info.binding_count < MOSAIC_ACTION_BINDINGS ? info.binding_count : MOSAIC_ACTION_BINDINGS;
		for (int k = 0; k < n; k++) {
			if (info.bindings[k].screen == MOSAIC_SCREEN_GAMELIST && info.bindings[k].button == button) {
				snprintf(who, size, "%s", info.id);
				return 1;
			}
		}
	}
	return 0;
}

static void bindButton(void) {
	const char* name = host->cfg_get("button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		char who[64], message[192];
		if (taken(buttons[i].button, who, sizeof(who))) {
			snprintf(message, sizeof(message), "%s is %s's on the game list, so " NEXT_ID " isn't on it", name, who);
		}
		else if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, NEXT_ID) != 0) {
			snprintf(message, sizeof(message), "couldn't put " NEXT_ID " on %s", name);
		}
		else snprintf(message, sizeof(message), NEXT_ID " on %s", name);
		say(message);
	}
}

static void tick(unsigned long now) {
	(void)now;
	if (bound) return;
	// once every plugin has bound its own buttons
	bound = 1;
	bindButton();
}

///////////////////////////////////////
// the header

static int pad(void) {
	int scale = host->screen_scale();
	return 12 * (scale > 0 ? scale : 1); // stock's BUTTON_PADDING, as its pills use
}

static int headerWidth(int max_width, int height) {
	(void)height;
	if (!showingSorted()) return 0;
	int width = host->text_width(FONT, tr(SM_modeLabel(mode)));
	if (width < 0) return -1;
	width += pad() * 2;
	return width <= max_width ? width : max_width;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	static int logged = -1;
	if (!showingSorted()) return 0;
	const char* text = tr(SM_modeLabel(mode));
	int pill = host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	host->fill_round_rect(screen, rect, rect->h / 2, mosaic_color((unsigned int)pill, 255));
	int text_y = rect->y + (rect->h - host->text_height(FONT)) / 2;
	int drawn = host->draw_text(screen, FONT, text, rect->x + pad(), text_y, mosaic_color((unsigned int)ink, 255), rect->w - pad() * 2);
	if (logged != mode) {
		logged = mode;
		char message[160];
		int full = host->text_width(FONT, text);
		if (drawn >= 0 && full > drawn) snprintf(message, sizeof(message), "header: %s, cut to fit %d pixels", text, rect->w);
		else snprintf(message, sizeof(message), "header: %s", text);
		say(message);
	}
	return 0;
}

///////////////////////////////////////

static int init(const MosaicHost* h) {
	host = h;
	SM_cacheInit(&keys);
	SM_memoInit(&memo);
	memo_sorting = 0;
	discsFree();
	memset(lists, 0, sizeof(lists));
	uses = 0;
	sorts_dir[0] = '\0';
	sorting = 0;
	bound = 0;
	const char* saved = host->cfg_get("mode", "name");
	mode = SM_modeFind(saved);
	if (mode < 0) {
		char message[160];
		snprintf(message, sizeof(message), "unknown mode \"%.64s\"; sorting by name", saved);
		say(message);
		mode = SM_NAME;
	}
	if (!MOSAIC_HOST_HAS(host, decorate_cell) || !MOSAIC_HOST_HAS(host, reload_lists)) {
		say("unsupported: this launcher has no header widgets or can't list folders again");
		return 1;
	}
	char message[128];
	snprintf(message, sizeof(message), "mode %s", SM_modeId(mode));
	say(message);
	if (MOSAIC_HOST_HAS(host, set_names)) {
		host->subscribe("launcher.return", playChanged, NULL);
		if (MOSAIC_HOST_HAS(host, profile_path)) host->subscribe("launcher.profile_changed", playChanged, NULL);
		if (MOSAIC_HOST_HAS(host, index_refresh)) host->subscribe("launcher.index_ready", indexReady, NULL);
	}
	startIndex();
	if (host->register_action(NEXT_ID, "SORT", MOSAIC_ACTION_EDITS, canStep, step, NULL) != 0) say("couldn't register " NEXT_ID);
	if (host->register_action(PICK_ID, "Sort", MOSAIC_ACTION_EDITS, canPick, pick, NULL) != 0) say("couldn't register " PICK_ID);
	else if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(PICK_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the Sort tile");
	}
	return 0;
}

static void quit(void) {
	forget();
	SM_memoFree(&memo);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.filter = filter,
	.sort_compare = sortCompare,
	.list_ready = listReady,
	.header_width = headerWidth,
	.header_widget = headerWidget,
	.section_key = sectionKey,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
