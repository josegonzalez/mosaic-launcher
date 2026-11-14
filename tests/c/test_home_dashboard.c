// The home dashboard plugin: its model on its own, then the plugin against
// a fake host, on a card of its own under TEST_SDCARD_PATH.

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "check.h"
#include "fakeloaded.h"
#include "fakeproviders.h"
#include "fakesets.h"
#include "words.h"
#include "../../plugins/home-dashboard/home_dashboard.c"

#ifndef TEST_SDCARD_PATH
#define TEST_SDCARD_PATH "/tmp/home-dashboard-card"
#endif

static void testModes(void) {
	CHECK(HD_modeOf("dashboard") == HD_MODE_DASHBOARD);
	CHECK(HD_modeOf("minui") == HD_MODE_MINUI);
	CHECK(HD_modeOf("retroachievements") == HD_MODE_RETROACHIEVEMENTS);
	CHECK(HD_modeOf("pico8") == HD_MODE_PICO8);
	CHECK(HD_modeOf("nope") == HD_MODE_DASHBOARD);
	CHECK(HD_modeOf(NULL) == HD_MODE_DASHBOARD);
	CHECK_STR(HD_modeName(HD_MODE_PICO8), "pico8");
	CHECK_STR(HD_modeName(42), "dashboard");
}

static void testTabs(void) {
	int tabs[HD_TABS_MAX];
	CHECK(HD_tabs(1, 1, 1, tabs) == 3);
	CHECK(tabs[0] == HD_TAB_HOME && tabs[1] == HD_TAB_PINNED && tabs[2] == HD_TAB_STATS);
	// pinned needs favorites loaded, and the setting
	CHECK(HD_tabs(1, 0, 1, tabs) == 2);
	CHECK(tabs[1] == HD_TAB_STATS);
	CHECK(HD_tabs(0, 1, 1, tabs) == 2);
	CHECK(HD_tabs(0, 0, 0, tabs) == 1);
	CHECK(tabs[0] == HD_TAB_HOME);
	CHECK_STR(HD_tabLabel(HD_TAB_PINNED), "Pinned");

	CHECK(HD_tabs(1, 1, 1, tabs) == 3);
	CHECK(HD_tabIndex(tabs, 3, HD_TAB_STATS) == 2);
	HD_tabs(1, 0, 1, tabs);
	CHECK(HD_tabIndex(tabs, 2, HD_TAB_PINNED) == 0); // gone, back to home

	CHECK(HD_tabStep(0, 3, 1) == 1);
	CHECK(HD_tabStep(2, 3, 1) == 0);
	CHECK(HD_tabStep(0, 3, -1) == 2);
	CHECK(HD_tabStep(0, 1, 1) == 0);
	CHECK(HD_tabStep(0, 0, 1) == 0);
}

static void testHomeMove(void) {
	const int C = HD_FOCUS_CONTINUE;
	CHECK(HD_homeMove(C, 3, MOSAIC_BUTTON_DOWN) == 0);
	CHECK(HD_homeMove(C, 3, MOSAIC_BUTTON_RIGHT) == 0);
	CHECK(HD_homeMove(C, 3, MOSAIC_BUTTON_LEFT) == C);
	CHECK(HD_homeMove(C, 3, MOSAIC_BUTTON_UP) == C);
	CHECK(HD_homeMove(1, 3, MOSAIC_BUTTON_UP) == C);
	CHECK(HD_homeMove(1, 3, MOSAIC_BUTTON_DOWN) == 1);
	CHECK(HD_homeMove(1, 3, MOSAIC_BUTTON_RIGHT) == 2);
	CHECK(HD_homeMove(2, 3, MOSAIC_BUTTON_RIGHT) == 2); // stops at the end
	CHECK(HD_homeMove(1, 3, MOSAIC_BUTTON_LEFT) == 0);
	CHECK(HD_homeMove(0, 3, MOSAIC_BUTTON_LEFT) == C);
	// no shelf: always the card
	CHECK(HD_homeMove(C, 0, MOSAIC_BUTTON_DOWN) == C);
	CHECK(HD_homeMove(2, 0, MOSAIC_BUTTON_RIGHT) == C);
	// a shelf that shrank
	CHECK(HD_homeMove(5, 2, MOSAIC_BUTTON_LEFT) == 0);
}

static void testLists(void) {
	CHECK(HD_listMove(0, 0, 1, 1) == -1);
	CHECK(HD_listMove(0, 5, 1, 0) == 1);
	CHECK(HD_listMove(4, 5, 1, 0) == 4);
	CHECK(HD_listMove(4, 5, 1, 1) == 0);
	CHECK(HD_listMove(0, 5, -1, 1) == 4);
	CHECK(HD_listMove(0, 5, -1, 0) == 0);
	CHECK(HD_listMove(2, 5, 5, 1) == 4); // a page never wraps
	CHECK(HD_listMove(2, 5, -5, 1) == 0);
	CHECK(HD_listMove(9, 5, 0, 0) == 4); // clamps a stale selection
	CHECK(HD_listMove(-1, 5, 0, 0) == 0);

	CHECK(HD_window(0, 10, 4, 0) == 0);
	CHECK(HD_window(3, 10, 4, 0) == 0);
	CHECK(HD_window(4, 10, 4, 0) == 1);
	CHECK(HD_window(9, 10, 4, 0) == 6);
	CHECK(HD_window(2, 10, 4, 6) == 2);
	CHECK(HD_window(7, 10, 4, 6) == 6);
	CHECK(HD_window(1, 3, 4, 2) == 0); // all fit
	CHECK(HD_window(5, 10, 4, 9) == 5); // a first past the end comes back
	CHECK(HD_window(0, 0, 4, 3) == 0);
}

// the Stats tab: scrolling with no selection, never a heading last
static void testScroll(void) {
	CHECK(HD_scroll(0, 1, 6, 4) == 1);
	CHECK(HD_scroll(1, 1, 6, 4) == 2);
	CHECK(HD_scroll(2, 1, 6, 4) == 2); // the last page
	CHECK(HD_scroll(2, -1, 6, 4) == 1);
	CHECK(HD_scroll(0, -1, 6, 4) == 0);
	CHECK(HD_scroll(3, 1, 3, 4) == 0); // all fit
	CHECK(HD_scroll(5, 0, 6, 0) == 5);

	// Played today, Time left, Recently played, MOST PLAYED, two games
	const int header[] = { 0, 0, 0, 1, 0, 0 };
	CHECK(HD_shown(header, 6, 0, 4) == 3); // the heading would be last
	CHECK(HD_shown(header, 6, 1, 4) == 4);
	CHECK(HD_shown(header, 6, 2, 4) == 4);
	CHECK(HD_shown(header, 6, 0, 6) == 6);
	CHECK(HD_shown(header, 6, 0, 10) == 6);
	CHECK(HD_shown(header, 4, 0, 4) == 3); // a heading with no games after it
	CHECK(HD_shown(header, 6, 3, 1) == 0);
}

static void testText(void) {
	char out[64];
	HD_duration(0, out, sizeof(out));
	CHECK_STR(out, "");
	HD_duration(-1, out, sizeof(out));
	CHECK_STR(out, "");
	HD_duration(30, out, sizeof(out));
	CHECK_STR(out, "<1m");
	HD_duration(45 * 60 + 10, out, sizeof(out));
	CHECK_STR(out, "45m");
	HD_duration(2 * 3600 + 15 * 60, out, sizeof(out));
	CHECK_STR(out, "2h 15m");
	HD_duration(3600, out, sizeof(out));
	CHECK_STR(out, "1h 0m");

	HD_playedLine(2 * 3600 + 15 * 60, out, sizeof(out));
	CHECK_STR(out, "2h 15m played");
	HD_playedLine(0, out, sizeof(out));
	CHECK_STR(out, "");
	HD_playedLine(MOSAIC_PLAY_UNKNOWN, out, sizeof(out));
	CHECK_STR(out, "");

	// time left, rounded up to the minute, of the limit and today's grant
	CHECK(HD_timeLeft("ok", 60, 0, 35 * 60 - 20, out, sizeof(out)) == 1);
	CHECK_STR(out, "35m of 1h 0m");
	CHECK(HD_timeLeft("warn", 60, 30, 61, out, sizeof(out)) == 1);
	CHECK_STR(out, "2m of 1h 30m");
	CHECK(HD_timeLeft("up", 45, 0, 0, out, sizeof(out)) == 1);
	CHECK_STR(out, "0m of 45m");
	// no limit, or an unknown history: no row
	CHECK(HD_timeLeft("off", 0, 0, -1, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(HD_timeLeft("unknown", 60, 0, -1, out, sizeof(out)) == 0);
	CHECK(HD_timeLeft("ok", 0, 0, 600, out, sizeof(out)) == 0);
	CHECK(HD_timeLeft(NULL, 60, 0, 600, out, sizeof(out)) == 0);

	HD_achievementLine(12, 40, 150, 400, out, sizeof(out));
	CHECK_STR(out, "12/40 \xc2\xb7 150/400 points");
	HD_achievementLine(3, 12, 0, 0, out, sizeof(out));
	CHECK_STR(out, "3/12");
	HD_achievementLine(0, 0, 0, 0, out, sizeof(out));
	CHECK_STR(out, "");

	CHECK_STR(HD_openLabel(MOSAIC_ENTRY_ROM, 1), "RESUME");
	CHECK_STR(HD_openLabel(MOSAIC_ENTRY_ROM, 0), "PLAY");
	CHECK_STR(HD_openLabel(MOSAIC_ENTRY_PAK, 1), "OPEN");
}

// the text in the words it's given, then English again
static void testWords(void) {
	static const char* const words[] = { "%lldh %lldm", "%lld h %lld min", "%s played", "%s jugado", "%s of %s", "%s de %s",
		"%i/%i points", "%i/%i puntos", NULL };
	static const char* const plurals[] = { NULL };
	fake_words = words;
	fake_plurals = plurals;
	HDWords set = { fakeTr, fakePlural };
	HD_setWords(&set);
	char out[128];
	HD_playedLine(2 * 3600 + 15 * 60, out, sizeof(out));
	CHECK_STR(out, "2 h 15 min jugado");
	HD_timeLeft("ok", 120, 15, 2 * 3600 + 15 * 60, out, sizeof(out));
	CHECK_STR(out, "2 h 15 min de 2 h 15 min");
	HD_achievementLine(12, 40, 150, 400, out, sizeof(out));
	CHECK_STR(out, "12/40 \xc2\xb7 150/400 puntos");
	HD_setWords(NULL);
	HD_playedLine(2 * 3600 + 15 * 60, out, sizeof(out));
	CHECK_STR(out, "2h 15m played");
}

static void testMostPlayed(void) {
	long long times[] = { 10, 0, 50, 30, 50, -1, 20 };
	int top[3];
	int n = HD_mostPlayed(times, 7, top, 3);
	CHECK(n == 3);
	CHECK(top[0] == 2 && top[1] == 4 && top[2] == 3); // ties in index order
	int all[8];
	n = HD_mostPlayed(times, 7, all, 8);
	CHECK(n == 5); // none for 0 or unknown
	CHECK(all[3] == 6 && all[4] == 0);
	long long none[] = { 0, -1 };
	CHECK(HD_mostPlayed(none, 2, all, 8) == 0);
	CHECK(HD_mostPlayed(times, 0, all, 8) == 0);
}

static void testPico(void) {
	char tag[16];
	CHECK(HD_tagOf("Pico-8 (P8)", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "P8");
	CHECK(HD_tagOf("Game Boy (GB) (Hacks)", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "Hacks");
	CHECK(HD_tagOf("Tools", tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "");
	CHECK(HD_tagOf("Odd ()", tag, sizeof(tag)) == 0);
	CHECK(HD_tagOf("Open (P8", tag, sizeof(tag)) == 0);

	CHECK(HD_tagIn("P8", "P8,PICO8,PICO"));
	CHECK(HD_tagIn("pico", "P8, PICO8 , PICO"));
	CHECK(!HD_tagIn("PIC", "P8,PICO8,PICO"));
	CHECK(!HD_tagIn("GB", "P8,PICO8,PICO"));
	CHECK(!HD_tagIn("", "P8"));
	CHECK(!HD_tagIn("P8", ""));
	CHECK(HD_tagIn("P8", ",,P8,"));

	CHECK(HD_isPicoCart("celeste.p8"));
	CHECK(HD_isPicoCart("Celeste.P8.PNG"));
	CHECK(!HD_isPicoCart("celeste.png"));
	CHECK(!HD_isPicoCart(".p8"));
	CHECK(!HD_isPicoCart("._celeste.p8.png"));
	CHECK(!HD_isPicoCart("p8"));
}

static void testFavorites(void) {
	MosaicHost h = { .size = sizeof(MosaicHost), .plugin_loaded = fakePluginLoaded };
	fake_loaded = "hidden,favorites";
	CHECK(HD_pluginLoaded(&h, "favorites"));
	CHECK(!HD_pluginLoaded(&h, "favorites-plus"));
	CHECK(!HD_pluginLoaded(&h, ""));
	CHECK(!HD_pluginLoaded(&h, NULL));
	CHECK(!HD_pluginLoaded(NULL, "favorites"));
	// an older launcher's table, without plugin_loaded
	h.size = FAKE_HOST_BEFORE_LOADED;
	CHECK(!HD_pluginLoaded(&h, "favorites"));
	fake_loaded = "";
}

static void testLayout(void) {
	// 1024x768 at scale 3, the status pill in the top right
	MosaicRect area = { 0, 0, 1024, 768 };
	MosaicRect status = { 900, 30, 94, 90 };
	MosaicRect none = { 0, 0, 0, 0 };
	HDLayout l;
	CHECK(HD_layout(&area, &status, &none, 90, 680, 3, 40, 1, &l) == 0);
	CHECK(l.tabs.x == 30 && l.tabs.y == 30 && l.tabs.h == 90);
	CHECK(l.tabs.x + l.tabs.w == 870); // stops a padding short of the pill
	CHECK(l.content.y == 150 && l.content.h == 500);
	CHECK(l.card.h == 275);
	CHECK(l.shelf.y == l.card.y + l.card.h + 30);
	CHECK(l.shelf.y + l.shelf.h == l.content.y + l.content.h);
	CHECK(l.cell == l.shelf.h - 40 - 12);
	CHECK(l.cells == HD_cellsFit(l.shelf.w, l.cell, 30));
	CHECK(l.cells > 0);
	CHECK(l.art.w == l.art.h && l.art.h == l.card.h - 60);
	CHECK(l.text.x == l.art.x + l.art.w + 30);
	CHECK(l.text.x + l.text.w == l.card.x + l.card.w - 30);

	// header widgets left of the pill: the tabs stop before them
	MosaicRect header = { 700, 30, 190, 90 };
	CHECK(HD_layout(&area, &status, &header, 90, 680, 3, 40, 1, &l) == 0);
	CHECK(l.tabs.x + l.tabs.w == 670);

	// no shelf: the card takes the content
	CHECK(HD_layout(&area, &status, &none, 90, 680, 3, 40, 0, &l) == 0);
	CHECK(l.card.h == l.content.h && l.cells == 0 && l.shelf.h == 0);

	// no status pill (a full screen): tabs from the top padding
	CHECK(HD_layout(&area, &none, &none, 0, 680, 3, 40, 1, &l) == 0);
	CHECK(l.tabs.y == 30 && l.tabs.h == 90);

	// too short a shelf for cards gives the card the room
	CHECK(HD_layout(&area, &status, &none, 90, 300, 3, 40, 1, &l) == 0);
	CHECK(l.cells == 0 && l.card.h == l.content.h);
	// no room at all
	CHECK(HD_layout(&area, &status, &none, 90, 200, 3, 40, 1, &l) == -1);

	CHECK(HD_cellsFit(100, 30, 5) == 3);
	CHECK(HD_cellsFit(29, 30, 5) == 0);
	CHECK(HD_cellsFit(100, 0, 5) == 0);

	MosaicRect box = { 10, 20, 100, 100 }, fit;
	CHECK(HD_fit(200, 100, &box, &fit) == 0);
	CHECK(fit.x == 10 && fit.y == 45 && fit.w == 100 && fit.h == 50);
	CHECK(HD_fit(50, 100, &box, &fit) == 0);
	CHECK(fit.x == 35 && fit.y == 20 && fit.w == 50 && fit.h == 100);
	CHECK(HD_fit(0, 100, &box, &fit) == -1);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicEntry {
	char path[512];
	char name[256];
	int type;
};

static char cfg_mode[32] = "dashboard";
static char logged[4096];
static void fakeLog(const char* id, const char* m) {
	snprintf(logged + strlen(logged), sizeof(logged) - strlen(logged), "%s\n", m);
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "mode")) return cfg_mode;
	return fallback;
}
static int entries_made = 0;
static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = calloc(1, sizeof(MosaicEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	const char* slash = strrchr(path, '/');
	snprintf(e->name, sizeof(e->name), "%s", name ? name : slash ? slash + 1 : path);
	char* dot = strrchr(e->name, '.');
	if (dot) *dot = '\0';
	e->type = type;
	entries_made++;
	return e;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }

// recents: Tetris with a save state, Metroid without, then a pak
static const char* recent_paths[] = {
	TEST_SDCARD_PATH "/Roms/GB/Tetris.gb",
	TEST_SDCARD_PATH "/Roms/GBA/Metroid.gba",
	TEST_SDCARD_PATH "/Tools/desktop/Clock.pak",
};
static int recent_total = 3;

// favorites, and what is_filtered hides
static const char* fav_paths[] = {
	TEST_SDCARD_PATH "/Roms/GB/Tetris.gb",
	TEST_SDCARD_PATH "/Roms/GB/Gone.gb",
	TEST_SDCARD_PATH "/Roms/NES/Zelda.nes",
};
static void favList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	for (int i = 0; i < 3; i++) add(fav_paths[i], ctx);
}
static int noHas(const char* name, const char* path) {
	(void)name;
	(void)path;
	return 0;
}
static int fakeSetListCall(const char* name, void (*each)(const char*, void*), void* data) {
	int i = fakeSetFind(name);
	if (i < 0) return -1;
	if (each) fake_set_list[i](name, each, data);
	return 0;
}
static const char* hidden_path = "";
static int fakeFiltered(const char* path) { return hidden_path[0] && !strcmp(path, hidden_path); }
static int fakeRecentsCount(void) { return recent_total; }
static const char* fakeRecentPath(int i) { return i >= 0 && i < recent_total ? recent_paths[i] : NULL; }
static int fakeRecentInfo(int i, MosaicRecent* out) {
	if (i < 0 || i >= recent_total) return -1;
	static const char* names[] = { "Tetris", "Metroid", "Clock" };
	snprintf(out->name, sizeof(out->name), "%s", names[i]);
	out->type = i == 2 ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
	out->available = 1;
	out->can_resume = i == 0;
	snprintf(out->preview, sizeof(out->preview), "%s", i == 0 ? "/state/Tetris.bmp" : "");
	return 0;
}
static int opened_recent = -1, opened_resume = -1;
static int fakeRecentOpen(int i, int resume) {
	opened_recent = i;
	opened_resume = resume;
	return 1;
}
static char opened_path[512];
static int fakeOpenPath(const char* path, int type) {
	(void)type;
	snprintf(opened_path, sizeof(opened_path), "%s", path);
	return 1;
}
static char ran[64];
static int fakeRunAction(const char* id, const char* path) {
	(void)path;
	snprintf(ran, sizeof(ran), "%s", id);
	return 0;
}
static int redraws = 0;
static void fakeRedraw(void) { redraws++; }
static void fakeToast(const char* m) { (void)m; }
static MosaicScreenHooks registered;
static char registered_id[64];
static int fakeRegisterScreen(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(registered_id, sizeof(registered_id), "%s", id);
	registered = *hooks;
	return 0;
}
static int fakeDrawHints(MosaicSurface* s, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	(void)s; (void)group; (void)hints; (void)count; (void)drawn;
	return -1;
}
static long long fakeToday(void) { return 600; }
static long long fakePlayTime(const char* p) { return strstr(p, "Metroid") ? 7200 : strstr(p, "Tetris") ? 300 : 0; }

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.plugin_loaded = fakePluginLoaded,
	.sdcard_path = TEST_SDCARD_PATH,
	.userdata_path = TEST_SDCARD_PATH "/.userdata/desktop",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_new = fakeEntryNew,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.request_redraw = fakeRedraw,
	.toast = fakeToast,
	.open_path = fakeOpenPath,
	.recents_count = fakeRecentsCount,
	.recent_path = fakeRecentPath,
	.play_time_today = fakeToday,
	.play_time = fakePlayTime,
	.register_screen = fakeRegisterScreen,
	.run_action = fakeRunAction,
	.recent_info = fakeRecentInfo,
	.recent_open = fakeRecentOpen,
	.draw_hints = fakeDrawHints,
	.set_list = fakeSetListCall,
	.is_filtered = fakeFiltered,
};

static void writeFile(const char* path, const char* text) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char* p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static int press(int button) {
	MosaicInput input = { sizeof(MosaicInput), MOSAIC_BUTTON_BIT(button), MOSAIC_BUTTON_BIT(button), 0, MOSAIC_BUTTON_BIT(button) };
	return registered.input(&input, registered.data);
}

static void reset(const char* m) {
	snprintf(cfg_mode, sizeof(cfg_mode), "%s", m);
	logged[0] = '\0';
	tab = HD_TAB_HOME;
	home_focus = HD_FOCUS_CONTINUE;
	list_selected = 0;
	cart_selected = 0;
	opened_recent = opened_resume = -1;
	opened_path[0] = '\0';
	ran[0] = '\0';
	CHECK(init(&fake) == 0);
	CHECK(registered.enter(registered.data) == 0);
}

static void testPlugin(void) {
	fake_loaded = "favorites,home-dashboard";
	fake_set_count = 0;
	fakeSetRegister("favorites", noHas, favList);
	writeFile(TEST_SDCARD_PATH "/Roms/GB/Tetris.gb", "");
	writeFile(TEST_SDCARD_PATH "/Roms/NES/Zelda.nes", "");
	writeFile(TEST_SDCARD_PATH "/Roms/Pico-8 (P8)/celeste.p8.png", "");
	writeFile(TEST_SDCARD_PATH "/Roms/Pico-8 (P8)/Alpha.p8", "");
	writeFile(TEST_SDCARD_PATH "/Roms/Pico-8 (P8)/notes.txt", "");
	writeFile(TEST_SDCARD_PATH "/Roms/GB/stray.p8", "");

	reset("dashboard");
	CHECK_STR(registered_id, SCREEN_ID);
	CHECK(registered.flags == MOSAIC_SCREEN_HOME);
	CHECK(strstr(logged, "favorites on") != NULL);
	CHECK(strstr(logged, "home: mode dashboard, tab Home of 3, recents 3, pinned 2, carts 0, history on") != NULL);
	CHECK(pinned_count == 2 && !strcmp(pinned[1].name, "Zelda")); // the missing one left out

	// MENU and SELECT run the root list's actions; X closes on to the games, B stays
	CHECK(press(MOSAIC_BUTTON_MENU) == MOSAIC_SCREEN_STAY);
	CHECK_STR(ran, "gamelist.quick_menu");
	CHECK(press(MOSAIC_BUTTON_SELECT) == MOSAIC_SCREEN_STAY);
	CHECK_STR(ran, "gamelist.switcher");
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_STAY);
	CHECK(press(MOSAIC_BUTTON_X) == MOSAIC_SCREEN_CLOSE);

	// A on the continue card resumes the last game; down and A the next one
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK(opened_recent == 0 && opened_resume == 1);
	CHECK_STR(openLabel(), "RESUME");
	press(MOSAIC_BUTTON_DOWN);
	CHECK(home_focus == 0);
	CHECK_STR(openLabel(), "PLAY");
	press(MOSAIC_BUTTON_RIGHT);
	CHECK_STR(openLabel(), "OPEN");
	press(MOSAIC_BUTTON_A);
	CHECK(opened_recent == 2);

	// R1 to pinned, A opens through open_path; the tab outlives a reopen
	press(MOSAIC_BUTTON_R1);
	CHECK(tab == HD_TAB_PINNED);
	CHECK(strstr(logged, "tab Pinned") != NULL);
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_A);
	CHECK_STR(opened_path, TEST_SDCARD_PATH "/Roms/NES/Zelda.nes");
	registered.exit(registered.data);
	CHECK(registered.enter(registered.data) == 0);
	CHECK(tab == HD_TAB_PINNED);
	press(MOSAIC_BUTTON_R1);
	CHECK(tab == HD_TAB_STATS);
	press(MOSAIC_BUTTON_R1);
	CHECK(tab == HD_TAB_HOME);
	press(MOSAIC_BUTTON_L1);
	CHECK(tab == HD_TAB_STATS);

	// what a filter hides is left out of the recents, pinned and carts
	hidden_path = TEST_SDCARD_PATH "/Roms/NES/Zelda.nes";
	CHECK(registered.enter(registered.data) == 0);
	CHECK(pinned_count == 1 && !strcmp(pinned[0].name, "Tetris"));
	hidden_path = TEST_SDCARD_PATH "/Roms/GBA/Metroid.gba";
	CHECK(registered.enter(registered.data) == 0);
	CHECK(recent_count == 2 && recents[1].recent == 2); // the pak, still opened by its own index
	hidden_path = "";
	CHECK(registered.enter(registered.data) == 0);

	// without favorites loaded, no pinned tab, and stats keeps its place,
	// even with plugins.txt turning it on (it's on but declined to start)
	writeFile(TEST_SDCARD_PATH "/.userdata/desktop/launcher/plugins.txt", "favorites=on\nhome-dashboard=on\n");
	fake_loaded = "home-dashboard";
	logged[0] = '\0';
	CHECK(init(&fake) == 0);
	CHECK(registered.enter(registered.data) == 0);
	CHECK(tab_count == 2 && tab == HD_TAB_STATS && pinned_count == 0);

	// minui: the recents, then Games, which closes
	reset("minui");
	CHECK(strstr(logged, "home: mode minui") != NULL);
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_A);
	CHECK(opened_recent == 1);
	press(MOSAIC_BUTTON_UP); // wraps from the top to Games
	press(MOSAIC_BUTTON_UP);
	CHECK(list_selected == 3);
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_CLOSE);

	// pico8: carts from folders with a Pico-8 tag only, by name
	reset("pico8");
	CHECK(cart_count == 2);
	CHECK_STR(carts[0].name, "Alpha");
	CHECK_STR(carts[1].name, "celeste");
	hidden_path = TEST_SDCARD_PATH "/Roms/Pico-8 (P8)/Alpha.p8";
	CHECK(registered.enter(registered.data) == 0);
	CHECK(cart_count == 1 && !strcmp(carts[0].name, "celeste"));
	hidden_path = "";
	CHECK(registered.enter(registered.data) == 0);
	press(MOSAIC_BUTTON_RIGHT);
	press(MOSAIC_BUTTON_A);
	CHECK_STR(opened_path, TEST_SDCARD_PATH "/Roms/Pico-8 (P8)/celeste.p8.png");

	// entries for art are made once per path
	int made = entries_made;
	CHECK(registered.enter(registered.data) == 0);
	CHECK(entries_made == made);

	// an older launcher, without draw_hints, is declined
	MosaicHost old = fake;
	old.size = offsetof(MosaicHost, draw_hints);
	CHECK(init(&old) != 0);
}

// playtime-captions' captions.text and playtime-limit's limit.left
static char limit_reply[256] = "state=ok\nlimit=60\ngranted=15\nleft=1190\ntext=20m left";
static int fakeCaption(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char path[512], width[16];
	CHECK(fakeEventValue(args, "path", path, sizeof(path)) > 0);
	CHECK(fakeEventValue(args, "width", width, sizeof(width)) > 0);
	if (strstr(path, "Metroid")) snprintf(out, size, "text=\nwidth=0");
	else snprintf(out, size, "text=5m \xc2\xb7 today (%s)\nwidth=80", width);
	return 0;
}
static int fakeLimit(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "%s", limit_reply);
	return 0;
}

static void testServices(void) {
	char out[160];
	reset("dashboard");
	// without the providers: no caption, so the play time stands in, and no row
	fake.call = NULL;
	CHECK(captionOf(recent_paths[0], 300, out, sizeof(out)) == 0 && !out[0]);
	CHECK(timeLeftOf(out, sizeof(out)) == 0 && !out[0]);
	fake_provider_count = 0;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	CHECK(captionOf(recent_paths[0], 300, out, sizeof(out)) == 0);
	CHECK(timeLeftOf(out, sizeof(out)) == 0);

	fakeProvide("captions.text", fakeCaption);
	fakeProvide("limit.left", fakeLimit);
	CHECK(captionOf(recent_paths[0], 300, out, sizeof(out)) == 1);
	CHECK_STR(out, "5m \xc2\xb7 today (300)");
	// a game with no caption gets none, not the play time
	CHECK(captionOf(recent_paths[1], 300, out, sizeof(out)) == 1);
	CHECK_STR(out, "");

	// time left of today's limit and its grant
	CHECK(timeLeftOf(out, sizeof(out)) == 1);
	CHECK_STR(out, "20m of 1h 15m");
	snprintf(limit_reply, sizeof(limit_reply), "state=up\nlimit=30\ngranted=0\nleft=0\ntext=Time's up");
	CHECK(timeLeftOf(out, sizeof(out)) == 1);
	CHECK_STR(out, "0m of 30m");
	// only while a limit is set and the history is known
	snprintf(limit_reply, sizeof(limit_reply), "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=");
	CHECK(timeLeftOf(out, sizeof(out)) == 0 && !out[0]);
	snprintf(limit_reply, sizeof(limit_reply), "state=unknown\nlimit=60\ngranted=0\nleft=-1\ntext=");
	CHECK(timeLeftOf(out, sizeof(out)) == 0);

	fake_provider_count = 0;
	fake.call = NULL;
	fake.event_value = NULL;
}

int main(void) {
	CHECK(system("rm -rf '" TEST_SDCARD_PATH "'") == 0);
	testModes();
	testTabs();
	testHomeMove();
	testLists();
	testScroll();
	testText();
	testWords();
	testMostPlayed();
	testPico();
	testFavorites();
	testLayout();
	testPlugin();
	testServices();
	CHECK_DONE();
}
