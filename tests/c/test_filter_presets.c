// The filter presets plugin: its model on its own, then the plugin
// against a fake host with fake sets, services, index and lists.

#include <stdlib.h>
#include "check.h"
#include "fakeproviders.h"
#include "fakesets.h"
#include "../../plugins/filter-presets/filter_presets.c"

struct MosaicEntry {
	char path[512];
	char name[128];
	int type;
};
struct MosaicList {
	MosaicEntry items[16];
	int count;
};

#define CARD "/card"
#define GB CARD "/Roms/Game Boy (GB)"
#define GBA CARD "/Roms/Game Boy Advance (GBA)"

///////////////////////////////////////
// the model

static int test_played(const SEGame* game, void* data) {
	(void)data;
	return strstr(game->path, "Tetris") != NULL;
}
static int test_save(const SEGame* game, void* data) {
	(void)data;
	return strstr(game->path, "Kirby") != NULL;
}
static int test_in_set(int kind, const char* arg, const SEGame* game, void* data) {
	(void)data;
	if (kind == SE_FILTER_FAV) return strstr(game->path, "Super Mario") != NULL || strstr(game->path, "Kirby") != NULL;
	return SE_tagMatches("tags:rpg", arg) && strstr(game->path, "Zelda") != NULL;
}

// Tetris was played an hour ago for 20 hours; the rest never were
static long long test_play_value(int kind, const char* path, void* data) {
	(void)data;
	if (!strstr(path, "Tetris")) return 0;
	return kind == SE_FILTER_HOURS ? 20 * 3600 : 1000000 - 3600;
}

static int keeps_table[8];
static int keepsTable(int i, void* data) {
	(void)data;
	return keeps_table[i];
}

static void testModel(void) {
	CHECK(FP_players("1-4") == 4);
	CHECK(FP_players("2") == 2);
	CHECK(FP_players("up to 8 players") == 8);
	CHECK(FP_players("") == 0);
	CHECK(FP_players(NULL) == 0);
	CHECK(FP_players("one") == 0);

	FPFolder folder;
	FP_folderInit(&folder, GB);
	CHECK(FP_folderAdd(&folder, GB "/Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM, "Puzzle", "1-2", "1989") == 0);
	CHECK(FP_folderAdd(&folder, GB "/Super Mario Land.gb", "Super Mario Land", MOSAIC_ENTRY_ROM, "Platform", "1", NULL) == 0);
	CHECK(FP_folderAdd(&folder, GB "/Kirby.gb", "Kirby's Dream Land", MOSAIC_ENTRY_ROM, "platform", NULL, NULL) == 0);
	CHECK(FP_folderAdd(&folder, GB "/Zelda.gb", "Link's Awakening", MOSAIC_ENTRY_ROM, "", "4", NULL) == 0);
	CHECK(FP_folderAdd(&folder, GB "/Dr. Mario.gb", "Dr. Mario", MOSAIC_ENTRY_ROM, "Puzzle", "2", "1990") == 0);
	CHECK(folder.games.game_count == 5);

	// built-ins, genres by name, players from 2, then the user's that aren't empty
	const char* user[FP_USER_MAX] = { "  fav: mario ", "", "   ", "-fav: system:gb" };
	FPPreset presets[FP_PRESETS_MAX];
	int count = FP_presets(&folder, user, presets, FP_PRESETS_MAX);
	CHECK(count == 9);
	const char* keys[] = { "unplayed", "favorites", "save", "genre:platform", "genre:puzzle", "players:2", "players:4", "user1", "user4" };
	for (int i = 0; i < count && i < 9; i++) CHECK_STR(presets[i].key, keys[i]);
	CHECK(presets[0].kind == FP_QUERY && !strcmp(presets[0].query, "unplayed"));
	CHECK(presets[1].kind == FP_QUERY && !strcmp(presets[1].query, "fav:"));
	CHECK_STR(presets[3].genre, "Platform"); // as the first game with it has it
	CHECK(presets[5].players == 2 && presets[6].players == 4);
	CHECK_STR(presets[7].query, "fav: mario");
	CHECK(presets[7].label == FP_LABEL_QUERY);
	CHECK(FP_find(presets, count, "players:4") == 6);
	CHECK(FP_find(presets, count, "genre:racing") == -1);
	CHECK(FP_presets(&folder, user, presets, 2) == 2);

	FPTests tests = { test_in_set, test_played, test_save, CARD "/Roms", NULL };
	unsigned char m[8];
	count = FP_presets(&folder, user, presets, FP_PRESETS_MAX);
	CHECK(FP_match(&folder, &presets[0], &tests, m) == 4 && !m[0] && m[1]); // unplayed
	CHECK(FP_match(&folder, &presets[1], &tests, m) == 2 && m[1] && m[2] && !m[4]); // favorites
	CHECK(FP_match(&folder, &presets[2], &tests, m) == 1 && m[2]); // a save
	CHECK(FP_match(&folder, &presets[3], &tests, m) == 2 && m[1] && m[2]); // Platform, either case
	CHECK(FP_match(&folder, &presets[5], &tests, m) == 3 && m[0] && m[3] && m[4]); // 2 or more
	CHECK(FP_match(&folder, &presets[6], &tests, m) == 1 && m[3]);
	CHECK(FP_match(&folder, &presets[7], &tests, m) == 1 && m[1]); // a favorite with mario in its name
	CHECK(FP_match(&folder, &presets[8], &tests, m) == 3 && m[0] && m[3] && m[4]); // not a favorite, in GB

	// queries are search's: tag:, system: and negation
	FPPreset q = { .kind = FP_QUERY };
	snprintf(q.query, sizeof(q.query), "tag:RPG");
	CHECK(FP_match(&folder, &q, &tests, m) == 1 && m[3]);
	snprintf(q.query, sizeof(q.query), "system:gba");
	CHECK(FP_match(&folder, &q, &tests, m) == 0);
	snprintf(q.query, sizeof(q.query), "-unplayed");
	CHECK(FP_match(&folder, &q, &tests, m) == 1 && m[0]);
	// and genre:, year:, decade:, played: and hours:
	snprintf(q.query, sizeof(q.query), "genre:puzzle decade:80s");
	CHECK(FP_match(&folder, &q, &tests, m) == 1 && m[0]);
	snprintf(q.query, sizeof(q.query), "year:1990");
	CHECK(FP_match(&folder, &q, &tests, m) == 1 && m[4]);
	snprintf(q.query, sizeof(q.query), "played:1 hours:10");
	CHECK(FP_match(&folder, &q, &tests, m) == 0); // no play history without play_value
	tests.play_value = test_play_value;
	tests.now = 1000000;
	CHECK(FP_match(&folder, &q, &tests, m) == 1 && m[0]);
	snprintf(q.query, sizeof(q.query), "-hours:50");
	CHECK(FP_match(&folder, &q, &tests, m) == 5);
	// a plugin that's off keeps nothing
	FPTests none = { NULL, NULL, NULL, NULL, NULL };
	CHECK(FP_match(&folder, &presets[1], &none, m) == 0);
	CHECK(FP_match(&folder, &presets[2], &none, m) == 0);

	FP_folderFree(&folder);
	CHECK(folder.games.game_count == 0 && !folder.dir[0]);
	CHECK(FP_presets(&folder, NULL, presets, FP_PRESETS_MAX) == 3); // the built-ins alone
	CHECK(FP_match(&folder, &presets[0], &tests, m) == 0);

	// stepping skips presets that keep nothing, through none
	int table[] = { 0, 1, 0, 1 };
	memcpy(keeps_table, table, sizeof(table));
	CHECK(FP_step(4, -1, 1, keepsTable, NULL) == 1);
	CHECK(FP_step(4, 1, 1, keepsTable, NULL) == 3);
	CHECK(FP_step(4, 3, 1, keepsTable, NULL) == -1);
	CHECK(FP_step(4, -1, -1, keepsTable, NULL) == 3);
	CHECK(FP_step(4, 1, -1, keepsTable, NULL) == -1);
	memset(keeps_table, 0, sizeof(keeps_table));
	CHECK(FP_step(4, -1, 1, keepsTable, NULL) == -1);
	CHECK(FP_step(0, -1, 1, keepsTable, NULL) == -1);
	CHECK(FP_step(4, 9, 1, keepsTable, NULL) == -1); // a current that isn't there is none
}

///////////////////////////////////////
// a fake host

static char cfg_prev[8] = "", cfg_next[8] = "", cfg_tile[8] = "", cfg_preset1[64] = "";
static char current[512] = CARD;
static int reloads = 0, redraws = 0;
static char toasted[128];
static char logged[4096];

#define ACTIONS_MAX 8
static struct {
	char id[64];
	int (*available)(const char*, void*);
	void (*run)(const char*, void*);
} actions[ACTIONS_MAX];
static int action_count = 0;
static char tiles[ACTIONS_MAX][64];
static int tile_count = 0;
static struct {
	int button;
	char id[64];
} binds[8];
static int bind_count = 0;
static const char* other_on_l2 = NULL; // another plugin's action on L2

static void (*on_set_changed)(const char*, const char*, void*);
static void (*on_index_ready)(const char*, const char*, void*);
static int chooses = 0, choice_count = 0, choice_selected = -1;
static char choices[16][160];
static void (*choice_pick)(int, void*);

static void fakeLog(const char* id, const char* m) {
	(void)id;
	size_t len = strlen(logged);
	snprintf(logged + len, sizeof(logged) - len, "%s\n", m);
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "prev_button") && cfg_prev[0]) return cfg_prev;
	if (!strcmp(key, "next_button") && cfg_next[0]) return cfg_next;
	if (!strcmp(key, "tile") && cfg_tile[0]) return cfg_tile;
	if (!strcmp(key, "preset1") && cfg_preset1[0]) return cfg_preset1;
	return fallback;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static int fakeListCount(const MosaicList* l) { return l->count; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) { return (MosaicEntry*)&l->items[i]; }
static const char* fakeCurrentPath(void) { return current; }
static void fakeRedraw(void) { redraws++; }
static int fakeRegister(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)label;
	(void)flags;
	(void)data;
	if (action_count >= ACTIONS_MAX) return -1;
	snprintf(actions[action_count].id, sizeof(actions[0].id), "%s", id);
	actions[action_count].available = available;
	actions[action_count].run = run;
	action_count++;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)trigger;
	if (screen != MOSAIC_SCREEN_GAMELIST || bind_count >= 8) return -1;
	binds[bind_count].button = button;
	snprintf(binds[bind_count++].id, sizeof(binds[0].id), "%s", id);
	return 0;
}
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)row; (void)icon; (void)icon_off; (void)is_on;
	snprintf(tiles[tile_count++], sizeof(tiles[0]), "%s", id);
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeReload(void) { reloads++; }
static int fakeChoose(const char* title, const char* const* list, int count, int selected, void (*on_pick)(int, void*), void* data) {
	(void)title;
	(void)data;
	chooses++;
	choice_count = count;
	choice_selected = selected;
	for (int i = 0; i < count && i < 16; i++) snprintf(choices[i], sizeof(choices[0]), "%s", list[i]);
	choice_pick = on_pick;
	return 0;
}
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	if (!strcmp(name, "launcher.set_changed")) on_set_changed = on_event;
	if (!strcmp(name, "launcher.index_ready")) on_index_ready = on_event;
	return 0;
}
static int fakeHasSet(const char* name, const char* path) {
	int has = fakeSetHas(name, path);
	return has > 0;
}
static int fakeRecentsCount(void) { return 1; }
static const char* fakeRecentPath(int i) { return i == 0 ? GB "/Tetris.gb" : NULL; }

// the index: genres and players, and which folders are games
static int fakeIndexCountAll(const char* system) { (void)system; return 10; }
static int fakeIndexRefreshNone(void) { return 0; }
static int fakeIndexFindTable(const char* path, MosaicIndexEntry* out) {
	static const struct {
		const char* path;
		int kind;
		const char* genre;
		const char* players;
	} table[] = {
		{ GB "/Tetris.gb", MOSAIC_INDEX_GAME, "Puzzle", "1-2" },
		{ GB "/Super Mario Land.gb", MOSAIC_INDEX_GAME, "Platform", "1" },
		{ GB "/Kirby.gb", MOSAIC_INDEX_GAME, "Platform", "" },
		{ GB "/Zelda", MOSAIC_INDEX_GAME, "Adventure", "" },
		{ GB "/Hacks", MOSAIC_INDEX_FOLDER, "", "" },
	};
	for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (strcmp(table[i].path, path) != 0) continue;
		out->kind = table[i].kind;
		out->path = table[i].path;
		out->genre = table[i].genre;
		out->players = table[i].players;
		return (int)i;
	}
	return -1;
}

// played: Tetris; a save state: Kirby
static int fakePlayed(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	snprintf(out, size, "played=%d\n", strstr(args, "Tetris") != NULL);
	return 0;
}
static int fakeStates(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	snprintf(out, size, "count=%d\n", strstr(args, "Kirby") != NULL ? 2 : 0);
	return 0;
}

// favorites: Mario and the Zelda game folder's .m3u
static char favorites[4][512];
static int favorite_count = 0;
static int favoritesHas(const char* name, const char* path) {
	(void)name;
	for (int i = 0; i < favorite_count; i++) {
		if (!strcmp(favorites[i], path)) return 1;
	}
	return 0;
}
static void favoritesList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	for (int i = 0; i < favorite_count; i++) add(favorites[i], ctx);
}

static int fakeActionsCount(void) { return other_on_l2 ? 1 : 0; }
static int fakeActionInfo(int index, MosaicActionInfo* out) {
	if (index != 0 || !other_on_l2) return -1;
	snprintf(out->id, sizeof(out->id), "%s", other_on_l2);
	out->binding_count = 1;
	out->bindings[0].screen = MOSAIC_SCREEN_GAMELIST;
	out->bindings[0].button = MOSAIC_BUTTON_L2;
	return 0;
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) {
	(void)s; (void)i; (void)r; (void)n;
	return 0;
}
static int fakeTextWidth(int font, const char* text) { (void)font; return (int)strlen(text) * 10; }
static int fakeTextHeight(int font) { (void)font; return 20; }
static int fakeScale(void) { return 2; }
static int fakeThemeColor(int index) { (void)index; return 0x123456; }
static int fakeFillRound(MosaicSurface* t, const MosaicRect* r, int radius, MosaicColor c) {
	(void)t; (void)r; (void)radius; (void)c;
	return 0;
}
static char drawn[256];
static int fakeDrawText(MosaicSurface* t, int font, const char* text, int x, int y, MosaicColor c, int w) {
	(void)t; (void)font; (void)x; (void)y; (void)c; (void)w;
	snprintf(drawn, sizeof(drawn), "%s", text);
	return 0;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/test",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.entry_type = fakeEntryType,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.current_path = fakeCurrentPath,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.reload_lists = fakeReload,
	.quick_tile = fakeQuickTile,
	.choose = fakeChoose,
	.recents_count = fakeRecentsCount,
	.recent_path = fakeRecentPath,
	.set_has = fakeHasSet,
	.set_names = fakeSetNames,
	.subscribe = fakeSubscribe,
	.event_value = fakeEventValue,
	.call = fakeCall,
	.actions_count = fakeActionsCount,
	.action_info = fakeActionInfo,
	.decorate_cell = fakeDecorateCell,
	.text_width = fakeTextWidth,
	.text_height = fakeTextHeight,
	.screen_scale = fakeScale,
	.theme_color = fakeThemeColor,
	.fill_round_rect = fakeFillRound,
	.draw_text = fakeDrawText,
	.index_count = fakeIndexCountAll,
	.index_find = fakeIndexFindTable,
	.index_refresh = fakeIndexRefreshNone,
};

static int runAction(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (strcmp(actions[i].id, id) != 0) continue;
		if (actions[i].available && !actions[i].available(NULL, NULL)) return 0;
		actions[i].run(NULL, NULL);
		return 1;
	}
	return 0;
}

static int available(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) return !actions[i].available || actions[i].available(NULL, NULL);
	}
	return 0;
}

static void push(MosaicList* list, const char* path, const char* name, int type) {
	MosaicEntry* e = &list->items[list->count++];
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name);
	e->type = type;
}

// The Game Boy folder as the launcher lists it, through every filter.
static MosaicList gb;
static void listGb(void) {
	MosaicList all = { .count = 0 };
	push(&all, GB "/Hacks", "Hacks", MOSAIC_ENTRY_DIR);
	push(&all, GB "/Kirby.gb", "Kirby's Dream Land", MOSAIC_ENTRY_ROM);
	push(&all, GB "/Super Mario Land.gb", "Super Mario Land", MOSAIC_ENTRY_ROM);
	push(&all, GB "/Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM);
	push(&all, GB "/Zelda", "Link's Awakening", MOSAIC_ENTRY_DIR);
	memset(&gb, 0, sizeof(gb));
	for (int i = 0; i < all.count; i++) {
		if (plugin.filter(&all.items[i], GB)) gb.items[gb.count++] = all.items[i];
	}
	plugin.list_ready(&gb, GB);
}

static int bound_to(int button, const char* id) {
	for (int i = 0; i < bind_count; i++) {
		if (binds[i].button == button && !strcmp(binds[i].id, id)) return 1;
	}
	return 0;
}

static const char* header(void) {
	drawn[0] = '\0';
	int w = plugin.header_width(1000, 30);
	if (w <= 0) return "";
	MosaicRect r = { 0, 0, w, 30 };
	plugin.header_widget(NULL, &r);
	return drawn;
}

static void testPlugin(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p == &plugin && p->filter && p->list_ready && p->header_width && p->header_widget && p->tick);
	fakeSetRegister("favorites", favoritesHas, favoritesList);
	snprintf(favorites[favorite_count++], sizeof(favorites[0]), "%s", GB "/Super Mario Land.gb");
	snprintf(favorites[favorite_count++], sizeof(favorites[0]), "%s", GB "/Zelda/Zelda.m3u");
	fakeProvide("games.played", fakePlayed);
	fakeProvide("states.last", fakeStates);
	snprintf(cfg_preset1, sizeof(cfg_preset1), "%s", "-fav: tetris");

	// L2 is taken: only R2 is bound, and only on the first tick
	other_on_l2 = "search.open";
	CHECK(p->init(&fake) == 0);
	CHECK(action_count == 3 && tile_count == 1 && !strcmp(tiles[0], PICK_ID));
	CHECK(bind_count == 0);
	p->tick(0);
	CHECK(bind_count == 1 && bound_to(MOSAIC_BUTTON_R2, NEXT_ID));
	CHECK(strstr(logged, "L2 is search.open's on the game list") != NULL);
	p->tick(1);
	CHECK(bind_count == 1);

	// nothing to filter on the root
	CHECK(!available(NEXT_ID));
	CHECK(runAction(PICK_ID) && !strcmp(toasted, "Open a system or collection to filter it"));
	CHECK(header()[0] == '\0');

	snprintf(current, sizeof(current), "%s", GB);
	listGb();
	CHECK(gb.count == 5);
	CHECK(available(NEXT_ID) && available(PREV_ID));

	// R2: Unplayed, which leaves out Tetris and the Hacks folder
	reloads = 0;
	CHECK(runAction(NEXT_ID));
	CHECK(active && !strcmp(active_key, "unplayed") && reloads == 1);
	listGb();
	CHECK(gb.count == 3);
	for (int i = 0; i < gb.count; i++) CHECK(strstr(gb.items[i].path, "Tetris") == NULL && strstr(gb.items[i].path, "Hacks") == NULL);
	CHECK_STR(header(), "Unplayed (3)");
	// the folder's games are still all four
	CHECK(folderFor(GB) && folderFor(GB)->games.game_count == 4);

	// R2 again: Favorites, the game folder by its .m3u
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "favorites"));
	listGb();
	CHECK(gb.count == 2);
	CHECK_STR(header(), "Favorites (2)");
	// a favorite removed: the preset follows it
	favorite_count = 1;
	reloads = 0;
	on_set_changed("launcher.set_changed", "name=favorites", NULL);
	CHECK(reloads == 1);
	listGb();
	CHECK(gb.count == 1 && !strcmp(gb.items[0].path, GB "/Super Mario Land.gb"));
	// and the last one: back to all games
	favorite_count = 0;
	on_set_changed("launcher.set_changed", "name=favorites", NULL);
	CHECK(!active);
	listGb();
	CHECK(gb.count == 5);

	// R2 from none, past Favorites (none now), Unplayed first
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "unplayed"));
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "save"));
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "genre:adventure"));
	CHECK_STR(active_label, "Genre: Adventure");
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "genre:platform"));
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "genre:puzzle"));
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "players:2"));
	CHECK_STR(active_label, "2+ players");
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "user1"));
	CHECK_STR(active_label, "-fav: tetris");
	listGb();
	CHECK(gb.count == 1 && !strcmp(gb.items[0].path, GB "/Tetris.gb"));
	CHECK(runAction(NEXT_ID) && !active);
	// L2 from none: the last one
	CHECK(runAction(PREV_ID) && !strcmp(active_key, "user1"));
	CHECK(runAction(PREV_ID) && !strcmp(active_key, "players:2"));

	// the pick action lists the ones that keep games, with their counts
	CHECK(runAction(PICK_ID) && chooses == 1);
	CHECK(choice_count == 8);
	CHECK_STR(choices[0], "All games (4)");
	CHECK_STR(choices[1], "Unplayed (3)");
	CHECK_STR(choices[2], "Has save (1)");
	CHECK_STR(choices[6], "2+ players (1)");
	CHECK(choice_selected == 6);
	choice_pick(2, NULL);
	CHECK(!strcmp(active_key, "save"));
	choice_pick(0, NULL);
	CHECK(!active);
	choice_pick(1, NULL);
	CHECK(!strcmp(active_key, "unplayed"));

	// other folders aren't filtered
	MosaicEntry other = { GBA "/Metroid.gba", "Metroid", MOSAIC_ENTRY_ROM };
	CHECK(p->filter(&other, GBA) == 1);

	// leaving the folder puts all games back; opening one inside it lists it again
	reloads = 0;
	snprintf(current, sizeof(current), "%s", GB "/Hacks");
	p->tick(2);
	CHECK(!active && reloads == 1);
	CHECK(header()[0] == '\0');
	snprintf(current, sizeof(current), "%s", GB);
	CHECK(runAction(NEXT_ID) && active);
	reloads = 0;
	snprintf(current, sizeof(current), "%s", CARD);
	p->tick(3);
	CHECK(!active && reloads == 0);
	// the index changing lists a folder with games again, for its genres
	on_index_ready("launcher.index_ready", "count=10", NULL);
	CHECK(reloads == 0); // not on the root
	snprintf(current, sizeof(current), "%s", GB);
	on_index_ready("launcher.index_ready", "count=10", NULL);
	CHECK(reloads == 1 && !active);
	// and with a preset on, works its games out again
	CHECK(runAction(NEXT_ID) && active);
	reloads = 0;
	on_index_ready("launcher.index_ready", "count=10", NULL);
	CHECK(reloads == 1 && active && !strcmp(active_key, "unplayed"));
	snprintf(current, sizeof(current), "%s", CARD);
	p->tick(4);
	CHECK(!active);
	CHECK(strstr(logged, "header: Unplayed (3)") != NULL);

	// a folder without games has nothing to filter
	MosaicList tools = { .count = 0 };
	push(&tools, CARD "/Tools/test/Files.pak", "Files", MOSAIC_ENTRY_PAK);
	plugin.list_ready(&tools, CARD "/Tools/test");
	snprintf(current, sizeof(current), "%s", CARD "/Tools/test");
	CHECK(!available(NEXT_ID));

	// nothing matching: R2 says so
	fake_provider_count = 0; // no played or save services
	MosaicList one = { .count = 0 };
	push(&one, GBA "/Metroid.gba", "Metroid", MOSAIC_ENTRY_ROM);
	plugin.list_ready(&one, GBA);
	snprintf(current, sizeof(current), "%s", GBA);
	snprintf(cfg_preset1, sizeof(cfg_preset1), "%s", "");
	user_queries[0][0] = '\0';
	toasted[0] = '\0';
	fakeSetUnregister("favorites");
	// unplayed without games.played falls back to recents, so Metroid is unplayed
	CHECK(runAction(NEXT_ID) && !strcmp(active_key, "unplayed"));
	CHECK(runAction(NEXT_ID) && !active);

	p->quit();
	CHECK(folderFor(GB) == NULL);
}

static void testBothButtons(void) {
	// with nothing else on them, L2 and R2 are taken
	other_on_l2 = NULL;
	bind_count = 0;
	action_count = 0;
	tile_count = 0;
	snprintf(cfg_tile, sizeof(cfg_tile), "off");
	CHECK(plugin.init(&fake) == 0);
	CHECK(tile_count == 0);
	plugin.tick(0);
	CHECK(bind_count == 2 && bound_to(MOSAIC_BUTTON_L2, PREV_ID) && bound_to(MOSAIC_BUTTON_R2, NEXT_ID));
	// none leaves it off its button
	bind_count = 0;
	snprintf(cfg_prev, sizeof(cfg_prev), "none");
	snprintf(cfg_next, sizeof(cfg_next), "SELECT");
	CHECK(plugin.init(&fake) == 0);
	plugin.tick(0);
	CHECK(bind_count == 1 && bound_to(MOSAIC_BUTTON_SELECT, NEXT_ID));
	plugin.quit();
}

int main(void) {
	testModel();
	testPlugin();
	testBothButtons();
	CHECK_DONE();
}
