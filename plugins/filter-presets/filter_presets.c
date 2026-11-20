// Filter presets: L2 and R2 step the game list you're in, a system or a
// collection, through saved filters, and the header says which one is on
// and how many games it shows.
//
// The presets are Unplayed, Favorites, Has save, one for each genre the
// folder's games have and one for each player count they reach (at
// least 2, 3 and so on), then the user's own, up to FP_USER_MAX of them,
// each written as a search query (fav:, tag:<name>, unplayed,
// system:<name>, genre:<name>, year:<year>, decade:<decade>,
// played:<days>, hours:<hours> and added:<days>, each negated by a
// leading '-', and words a name has to have), parsed and matched by
// search's own model. A preset that keeps
// none of the folder's games is stepped past, and past the last one the
// folder shows all its games again. The filter-presets.pick action lists
// them with how many games each keeps, from a Filter tile on the quick
// menu (the "tile" setting) or an FN button.
//
// Favorites and tags come from their entry sets (set_has), unplayed from
// row-badges' games.played (without it, as search does, a game is played
// when it's in recents), Has save from save-state-manager's states.last,
// genre, year and players from the card index's .info fields, and
// played: and hours: from the play history, so nothing is read from the
// card here but added:'s file times. A preset whose plugin is off keeps nothing.
//
// The folder's games are the ones its list showed with no preset on, as
// list_ready saw them, so other plugins' filters, such as hidden's, still
// apply. A preset's games are worked out when it's picked, and again when
// a set changes (launcher.set_changed), the index does or the profile does
// (launcher.profile_changed); the filter hook then keeps only them, in
// that folder alone, so plain folders inside it are left out too. Leaving
// the folder, by backing out, opening something else or launching a game,
// puts it back to all games.
//
// The "prev_button" and "next_button" settings put the two actions on L2
// and R2 by default, but only once every plugin has loaded (the first
// tick) and only where no other action has the button on the game list,
// so a button another plugin was given keeps doing what it does there.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "presets_model.h"

#define ID "filter-presets"
#define PREV_ID ID ".prev"
#define NEXT_ID ID ".next"
#define PICK_ID ID ".pick"
#define FOLDERS_MAX 4 // the folders kept, more than a reload lists before the top one
#define LABEL_LEN 200
#define FONT MOSAIC_FONT_SMALL

static const MosaicHost* host;
static char roms_path[FP_PATH_LEN];
static char user_queries[FP_USER_MAX][SE_QUERY_LEN];
static const char* user[FP_USER_MAX];

// the folders whose lists were shown last, with no preset on
static FPFolder folders[FOLDERS_MAX];
static unsigned long folder_used[FOLDERS_MAX];
static unsigned long uses = 0;

// the preset on, in active_dir
static int active = 0;
static char active_dir[FP_PATH_LEN];
static char active_key[sizeof(((FPPreset*)0)->key)];
static char active_label[LABEL_LEN];
static SEPaths shown;  // the games it keeps
static int shown_count = 0; // what the list showed with it

static FPPreset presets[FP_PRESETS_MAX];
static int preset_count = 0;

static int bound = 0; // the buttons were looked at, on the first tick

// what the pick action listed
static char pick_keys[FP_PRESETS_MAX + 1][sizeof(active_key)];
static char pick_labels[FP_PRESETS_MAX + 1][LABEL_LEN];
static char pick_dir[FP_PATH_LEN];

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void say(const char* message) {
	host->log(ID, message);
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static void reload(void) {
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

///////////////////////////////////////
// the folders

static FPFolder* folderFor(const char* dir) {
	for (int i = 0; dir && i < FOLDERS_MAX; i++) {
		if (folders[i].dir[0] && !strcmp(folders[i].dir, dir)) return &folders[i];
	}
	return NULL;
}

static int isRoot(const char* dir) {
	return !strcmp(dir, host->sdcard_path);
}

// The folder the game list is in, when it has games to filter.
static FPFolder* currentFolder(void) {
	const char* current = host->current_path ? host->current_path() : NULL;
	if (!current || isRoot(current)) return NULL;
	FPFolder* folder = folderFor(current);
	return folder && folder->games.game_count > 0 ? folder : NULL;
}

// A game's index entry, when the index has it.
static int indexed(const char* path, MosaicIndexEntry* e) {
	memset(e, 0, sizeof(*e));
	e->size = sizeof(*e);
	return MOSAIC_HOST_HAS(host, index_refresh) && host->index_find(path, e) >= 0;
}

// Keeps a folder's games as its list shows them with no preset on, in
// the slot it had or the one used longest ago.
static void listReady(const MosaicList* list, const char* dir_path) {
	if (!dir_path || isRoot(dir_path)) return;
	if (active && !strcmp(dir_path, active_dir)) {
		shown_count = host->list_count(list);
		return;
	}
	FPFolder* folder = folderFor(dir_path);
	FPFolder* oldest = NULL;
	for (int i = 0; !folder && i < FOLDERS_MAX; i++) {
		// never the folder a preset is on, which L2 and R2 still step
		if (active && !strcmp(folders[i].dir, active_dir)) continue;
		if (!oldest || folder_used[i] < folder_used[oldest - folders]) oldest = &folders[i];
	}
	if (!folder) folder = oldest;
	if (!folder) return;
	FP_folderFree(folder);
	FP_folderInit(folder, dir_path);
	folder_used[folder - folders] = ++uses;

	int count = host->list_count(list);
	for (int i = 0; i < count; i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		const char* path = host->entry_path(entry);
		int type = host->entry_type(entry);
		MosaicIndexEntry e;
		int known = indexed(path, &e);
		// a file is a game; a folder or pak only when the index says so
		if (type != MOSAIC_ENTRY_ROM && (!known || e.kind != MOSAIC_INDEX_GAME)) continue;
		if (FP_folderAdd(folder, path, host->entry_name(entry), type, known ? e.genre : NULL, known ? e.players : NULL,
				known ? e.year : NULL) != 0) {
			say("out of memory listing a folder");
			break;
		}
	}
}

///////////////////////////////////////
// what presets ask plugins

// the set a tag: names, found once each time presets are matched
static int tag_known = 0;
static char tag_arg[SE_QUERY_LEN];
static char tag_set[SE_NAME_LEN + 8];

static void findTag(const char* name, void* data) {
	(void)data;
	if (!tag_set[0] && SE_tagMatches(name, tag_arg)) snprintf(tag_set, sizeof(tag_set), "%s", name);
}

static int setHas(const char* set, const SEGame* game) {
	if (host->set_has(set, game->path)) return 1;
	// a set holds a game folder by its .m3u or .cue, as stock records it
	if (game->type != MOSAIC_ENTRY_DIR) return 0;
	const char* name = strrchr(game->path, '/');
	char file[FP_PATH_LEN * 2];
	snprintf(file, sizeof(file), "%s%s.m3u", game->path, name ? name : "");
	if (host->set_has(set, file)) return 1;
	snprintf(file, sizeof(file), "%s%s.cue", game->path, name ? name : "");
	return host->set_has(set, file);
}

static int inSet(int kind, const char* arg, const SEGame* game, void* data) {
	(void)data;
	if (!MOSAIC_HOST_HAS(host, set_names)) return 0;
	if (kind == SE_FILTER_FAV) return setHas("favorites", game);
	if (!tag_known || strcmp(tag_arg, arg) != 0) {
		tag_known = 1;
		snprintf(tag_arg, sizeof(tag_arg), "%s", arg);
		tag_set[0] = '\0';
		host->set_names(findTag, NULL);
	}
	return tag_set[0] && setHas(tag_set, game);
}

static int played(const SEGame* game, void* data) {
	(void)data;
	if (MOSAIC_HOST_HAS(host, call)) {
		char args[FP_PATH_LEN + 8], reply[64], value[8];
		snprintf(args, sizeof(args), "path=%s", game->path);
		if (host->call("games.played", args, reply, sizeof(reply)) >= 0
				&& host->event_value(reply, "played", value, sizeof(value)) > 0) return !strcmp(value, "1");
	}
	if (!MOSAIC_HOST_HAS(host, recents_count)) return 0;
	size_t len = strlen(game->path);
	int count = host->recents_count();
	for (int i = 0; i < count; i++) {
		const char* recent = host->recent_path(i);
		if (recent && (!strcmp(recent, game->path) || (!strncmp(recent, game->path, len) && recent[len] == '/'))) return 1;
	}
	return 0;
}

static int hasSave(const SEGame* game, void* data) {
	(void)data;
	if (!MOSAIC_HOST_HAS(host, call)) return 0;
	char args[FP_PATH_LEN + 8], reply[MOSAIC_CALL_MAX], value[16];
	snprintf(args, sizeof(args), "path=%s", game->path);
	return host->call("states.last", args, reply, sizeof(reply)) >= 0
		&& host->event_value(reply, "count", value, sizeof(value)) > 0 && atoi(value) > 0;
}

// played:'s last played time, hours:' seconds played and added:'s time
// the file was put on the card: the later of its modified and changed
// times, which on a FAT card is when it was copied there.
static long long playValue(int kind, const char* path, void* data) {
	(void)data;
	if (kind == SE_FILTER_ADDED) {
		struct stat st;
		if (stat(path, &st) != 0) return 0;
		return (long long)(st.st_mtime > st.st_ctime ? st.st_mtime : st.st_ctime);
	}
	if (!MOSAIC_HOST_HAS(host, play_time)) return 0;
	return kind == SE_FILTER_PLAYED ? host->last_played(path) : host->play_time(path);
}

static const FPTests tests = { inSet, played, hasSave, roms_path, NULL, playValue, 0 };

// Which of folder's games preset keeps, into matched, or -1.
static int match(const FPFolder* folder, const FPPreset* preset, unsigned char* matched) {
	tag_known = 0;
	return FP_match(folder, preset, &tests, matched);
}

///////////////////////////////////////
// the preset on

static void label(const FPPreset* p, char* out, size_t size) {
	switch (p->label) {
	case FP_LABEL_UNPLAYED: snprintf(out, size, "%s", tr("Unplayed")); break;
	case FP_LABEL_FAVORITES: snprintf(out, size, "%s", tr("Favorites")); break;
	case FP_LABEL_SAVE: snprintf(out, size, "%s", tr("Has save")); break;
	case FP_LABEL_GENRE: snprintf(out, size, tr("Genre: %s"), p->genre); break;
	case FP_LABEL_PLAYERS: snprintf(out, size, tr("%d+ players"), p->players); break;
	default: snprintf(out, size, "%s", p->query); break;
	}
}

static void clearActive(void) {
	active = 0;
	active_dir[0] = '\0';
	active_key[0] = '\0';
	active_label[0] = '\0';
	SE_pathsFree(&shown);
	shown_count = 0;
}

static int buildPresets(const FPFolder* folder) {
	preset_count = FP_presets(folder, user, presets, FP_PRESETS_MAX);
	return preset_count;
}

// Puts preset index (-1 for none) on in folder, and lists it again. 0, or
// -1 when it keeps nothing, leaving it as it was.
static int apply(const FPFolder* folder, int index) {
	if (index < 0) {
		int was = active;
		clearActive();
		if (was) {
			say("all games");
			reload();
		}
		return 0;
	}
	int games = folder->games.game_count;
	unsigned char* matched = malloc((size_t)games);
	int kept = matched ? match(folder, &presets[index], matched) : -1;
	if (kept <= 0) {
		free(matched);
		return -1;
	}
	SEPaths keep;
	SE_pathsInit(&keep);
	for (int i = 0; i < games; i++) {
		if (matched[i] && SE_pathsAdd(&keep, folder->games.games[i].path) != 0) {
			free(matched);
			SE_pathsFree(&keep);
			say("out of memory filtering");
			return -1;
		}
	}
	free(matched);

	char dir[FP_PATH_LEN];
	snprintf(dir, sizeof(dir), "%s", folder->dir);
	clearActive();
	active = 1;
	shown = keep;
	shown_count = kept;
	snprintf(active_dir, sizeof(active_dir), "%s", dir);
	snprintf(active_key, sizeof(active_key), "%s", presets[index].key);
	label(&presets[index], active_label, sizeof(active_label));

	char message[sizeof(active_key) + sizeof(active_label) + sizeof(active_dir) + 64]; // room for every field whole
	snprintf(message, sizeof(message), "%s (%s): %d of %d games in %s", presets[index].key, active_label, kept, games, active_dir);
	say(message);
	reload();
	return 0;
}

// The preset on in the current folder, or -1.
static int currentPreset(const FPFolder* folder) {
	if (!active || strcmp(folder->dir, active_dir) != 0) return -1;
	return FP_find(presets, preset_count, active_key);
}

typedef struct Keeps {
	const FPFolder* folder;
	unsigned char* matched;
} Keeps;

static int keepsAny(int index, void* data) {
	Keeps* k = data;
	return match(k->folder, &presets[index], k->matched) > 0;
}

static void step(int dir) {
	FPFolder* folder = currentFolder();
	if (!folder) return;
	buildPresets(folder);
	int current = currentPreset(folder);
	Keeps keeps = { folder, malloc((size_t)folder->games.game_count) };
	if (!keeps.matched) return;
	int next = FP_step(preset_count, current, dir, keepsAny, &keeps);
	free(keeps.matched);
	if (next < 0 && current < 0) {
		toast(tr("No filter matches these games"));
		return;
	}
	if (apply(folder, next) != 0) {
		clearActive();
		reload();
	}
}

// A set or the index changed: the preset's games may have too.
static void refreshActive(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	if (!active) return;
	FPFolder* folder = folderFor(active_dir);
	int index = -1;
	if (folder) {
		buildPresets(folder);
		index = FP_find(presets, preset_count, active_key);
	}
	if (index < 0 || apply(folder, index) != 0) {
		clearActive();
		reload();
	}
}

// The index changed: the folder's genres and players may have too, so
// it's listed again, or with a preset on, its games worked out again.
static void indexReady(const char* name, const char* payload, void* data) {
	if (active) refreshActive(name, payload, data);
	else if (currentFolder()) reload();
}

///////////////////////////////////////
// actions

static int canStep(const char* path, void* data) {
	(void)path;
	(void)data;
	return currentFolder() != NULL;
}

static void prev(const char* path, void* data) {
	(void)path;
	(void)data;
	step(-1);
}

static void next(const char* path, void* data) {
	(void)path;
	(void)data;
	step(1);
}

static void picked(int index, void* data) {
	(void)data;
	FPFolder* folder = currentFolder();
	if (index < 0 || !folder || strcmp(folder->dir, pick_dir) != 0) return;
	buildPresets(folder);
	int at = index == 0 ? -1 : FP_find(presets, preset_count, pick_keys[index]);
	if (index > 0 && at < 0) return;
	if (apply(folder, at) != 0) toast(tr("No filter matches these games"));
}

static int canPick(const char* path, void* data) {
	(void)path;
	(void)data;
	return MOSAIC_HOST_HAS(host, choose);
}

static void pick(const char* path, void* data) {
	(void)path;
	(void)data;
	FPFolder* folder = currentFolder();
	if (!folder) {
		toast(tr("Open a system or collection to filter it"));
		return;
	}
	buildPresets(folder);
	int current = currentPreset(folder);
	unsigned char* matched = malloc((size_t)folder->games.game_count);
	if (!matched) return;
	snprintf(pick_dir, sizeof(pick_dir), "%s", folder->dir);
	const char* choices[FP_PRESETS_MAX + 1];
	int count = 0, selected = 0;
	snprintf(pick_keys[count], sizeof(pick_keys[0]), "%s", "");
	snprintf(pick_labels[count], sizeof(pick_labels[0]), tr("%s (%d)"), tr("All games"), folder->games.game_count);
	choices[count] = pick_labels[count];
	count++;
	for (int i = 0; i < preset_count; i++) {
		int kept = match(folder, &presets[i], matched);
		if (kept <= 0) continue;
		char name[LABEL_LEN];
		label(&presets[i], name, sizeof(name));
		if (i == current) selected = count;
		snprintf(pick_keys[count], sizeof(pick_keys[0]), "%s", presets[i].key);
		snprintf(pick_labels[count], sizeof(pick_labels[0]), tr("%s (%d)"), name, kept);
		choices[count] = pick_labels[count];
		count++;
	}
	free(matched);
	if (host->choose(tr("Filter"), choices, count, selected, picked, NULL) != 0) say("couldn't list the filters");
}

///////////////////////////////////////
// buttons

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

static void bindSetting(const char* key, const char* fallback, const char* action) {
	const char* name = host->cfg_get(key, fallback);
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		char who[64], message[192];
		if (taken(buttons[i].button, who, sizeof(who))) {
			snprintf(message, sizeof(message), "%s is %s's on the game list, so %s isn't on it", name, who, action);
			say(message);
		}
		else if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, action) != 0) {
			snprintf(message, sizeof(message), "couldn't put %s on %s", action, name);
			say(message);
		}
		else {
			snprintf(message, sizeof(message), "%s on %s", action, name);
			say(message);
		}
	}
}

///////////////////////////////////////
// hooks

static int filter(const MosaicEntry* entry, const char* dir_path) {
	if (!active || !dir_path || strcmp(dir_path, active_dir) != 0) return 1;
	return SE_pathsHas(&shown, host->entry_path(entry));
}

static void tick(unsigned long now) {
	(void)now;
	if (!bound) {
		// once every plugin has bound its own buttons
		bound = 1;
		bindSetting("prev_button", "L2", PREV_ID);
		bindSetting("next_button", "R2", NEXT_ID);
	}
	if (!active) return;
	const char* current = host->current_path ? host->current_path() : NULL;
	if (!current || !strcmp(current, active_dir)) return;
	// left it: the folder lists all its games again, now or when it's back
	size_t len = strlen(active_dir);
	int inside = !strncmp(current, active_dir, len) && current[len] == '/';
	say("left the folder: all games");
	clearActive();
	if (inside) reload();
}

static int headerText(char* out, size_t size) {
	const char* current = host->current_path ? host->current_path() : NULL;
	if (!active || !current || strcmp(current, active_dir) != 0) return 0;
	snprintf(out, size, tr("%s (%d)"), active_label, shown_count);
	return 1;
}

static int pad(void) {
	int scale = host->screen_scale();
	return 12 * (scale > 0 ? scale : 1); // stock's BUTTON_PADDING, as its pills use
}

static int headerWidth(int max_width, int height) {
	(void)height;
	char text[LABEL_LEN + 32];
	if (!headerText(text, sizeof(text))) return 0;
	int width = host->text_width(FONT, text);
	if (width < 0) return -1;
	width += pad() * 2;
	return width <= max_width ? width : max_width;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	static char logged_text[LABEL_LEN + 32];
	char text[LABEL_LEN + 32];
	if (!headerText(text, sizeof(text))) return 0;
	if (strcmp(text, logged_text) != 0) {
		char message[LABEL_LEN + 48];
		snprintf(message, sizeof(message), "header: %s", text);
		say(message);
		snprintf(logged_text, sizeof(logged_text), "%s", text);
	}
	int pill = host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	host->fill_round_rect(screen, rect, rect->h / 2, mosaic_color((unsigned int)pill, 255));
	int text_y = rect->y + (rect->h - host->text_height(FONT)) / 2;
	host->draw_text(screen, FONT, text, rect->x + pad(), text_y, mosaic_color((unsigned int)ink, 255), rect->w - pad() * 2);
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	for (int i = 0; i < FOLDERS_MAX; i++) {
		FP_folderInit(&folders[i], NULL);
		folder_used[i] = 0;
	}
	uses = 0;
	SE_pathsInit(&shown);
	clearActive();
	bound = 0;
	for (int i = 0; i < FP_USER_MAX; i++) {
		char key[16];
		snprintf(key, sizeof(key), "preset%d", i + 1);
		snprintf(user_queries[i], sizeof(user_queries[0]), "%s", host->cfg_get(key, ""));
		user[i] = user_queries[i];
	}

	if (!MOSAIC_HOST_HAS(host, decorate_cell) || !MOSAIC_HOST_HAS(host, reload_lists)) {
		say("unsupported: this launcher has no header widgets or can't list folders again");
		return 1;
	}
	if (MOSAIC_HOST_HAS(host, set_names)) host->subscribe("launcher.set_changed", refreshActive, NULL);
	// another profile's recents and play history, for Played and Unplayed
	if (MOSAIC_HOST_HAS(host, profile_path)) host->subscribe("launcher.profile_changed", refreshActive, NULL);
	if (MOSAIC_HOST_HAS(host, index_refresh)) {
		host->subscribe("launcher.index_ready", indexReady, NULL);
		host->index_count(NULL); // starts reading the card, for genres and players
	}
	if (host->register_action(PREV_ID, "FILTER", MOSAIC_ACTION_EDITS, canStep, prev, NULL) != 0
			|| host->register_action(NEXT_ID, "FILTER", MOSAIC_ACTION_EDITS, canStep, next, NULL) != 0) {
		say("couldn't register the filter actions");
		return 1;
	}
	if (host->register_action(PICK_ID, "Filter", MOSAIC_ACTION_EDITS, canPick, pick, NULL) != 0) say("couldn't register " PICK_ID);
	else if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(PICK_ID, MOSAIC_QUICK_TOP, "quick/filter.png", NULL, NULL) != 0)) {
		say("couldn't add the Filter tile");
	}
	return 0;
}

static void quit(void) {
	clearActive();
	for (int i = 0; i < FOLDERS_MAX; i++) FP_folderFree(&folders[i]);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.filter = filter,
	.tick = tick,
	.list_ready = listReady,
	.header_width = headerWidth,
	.header_widget = headerWidget,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
