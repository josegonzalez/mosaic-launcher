// The home dashboard plugin: its model on its own, then the plugin against
// a fake host, on a card of its own under TEST_SDCARD_PATH.

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "check.h"
#include "../../plugins/home-dashboard/home_dashboard.c"

#ifndef TEST_SDCARD_PATH
#define TEST_SDCARD_PATH "/tmp/home-dashboard-card"
#endif

// a local time, in whatever TZ is set
static long long at(int year, int month, int day, int hour, int minute) {
	struct tm t = { 0 };
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_isdst = -1;
	return (long long)mktime(&t);
}

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

	setenv("TZ", "UTC", 1);
	tzset();
	long long now = at(2026, 11, 8, 12, 0);
	HD_lastPlayed(at(2026, 11, 8, 1, 0), now, out, sizeof(out));
	CHECK_STR(out, "today");
	HD_lastPlayed(at(2026, 11, 7, 23, 59), now, out, sizeof(out));
	CHECK_STR(out, "yesterday");
	HD_lastPlayed(at(2026, 11, 5, 13, 0), now, out, sizeof(out));
	CHECK_STR(out, "3 days ago");
	HD_lastPlayed(0, now, out, sizeof(out));
	CHECK_STR(out, "");
	HD_lastPlayed(MOSAIC_PLAY_UNKNOWN, now, out, sizeof(out));
	CHECK_STR(out, "");
	CHECK(HD_daysBetween(now + 100, now) == 0);
	// across a daylight saving change, a day is still a day
	setenv("TZ", "America/New_York", 1);
	tzset();
	CHECK(HD_daysBetween(at(2026, 11, 1, 0, 30), at(2026, 11, 2, 0, 30)) == 1);
	CHECK(HD_daysBetween(at(2026, 3, 7, 23, 0), at(2026, 3, 9, 1, 0)) == 2);
	setenv("TZ", "UTC", 1);
	tzset();

	HD_playLine(2 * 3600 + 15 * 60, at(2026, 11, 7, 20, 0), now, out, sizeof(out));
	CHECK_STR(out, "2h 15m played \xc2\xb7 yesterday");
	HD_playLine(600, 0, now, out, sizeof(out));
	CHECK_STR(out, "10m played");
	HD_playLine(0, at(2026, 11, 8, 9, 0), now, out, sizeof(out));
	CHECK_STR(out, "today");
	HD_playLine(0, 0, now, out, sizeof(out));
	CHECK_STR(out, "");

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
	CHECK(HD_pluginOn("favorites=on\n", "favorites"));
	CHECK(HD_pluginOn("hidden=on\r\nfavorites=on\r\n", "favorites"));
	CHECK(HD_pluginOn("favorites=on", "favorites"));
	CHECK(!HD_pluginOn("favorites=off\n", "favorites"));
	CHECK(!HD_pluginOn("favorites=on\nfavorites=off\n", "favorites"));
	CHECK(HD_pluginOn("favorites=off\nfavorites=on\n", "favorites"));
	CHECK(!HD_pluginOn("favorites-plus=on\n", "favorites"));
	CHECK(!HD_pluginOn("slot.home=favorites\n", "favorites"));
	CHECK(!HD_pluginOn("favorites=onward\n", "favorites"));
	CHECK(!HD_pluginOn(NULL, "favorites"));
	CHECK(!HD_pluginOn("", "favorites"));

	CHECK_STR(HD_collectionName("Faves"), "Faves");
	CHECK_STR(HD_collectionName(""), "Favorites");
	CHECK_STR(HD_collectionName("a/b"), "Favorites");
	CHECK_STR(HD_collectionName(NULL), "Favorites");

	const char* text = "/Roms/GB/Tetris.gb\r\n\n/Roms/GBA/Metroid.gba\n/Tools/x.pak";
	const char* cursor = text;
	char line[64];
	CHECK(HD_nextLine(&cursor, line, sizeof(line)));
	CHECK_STR(line, "/Roms/GB/Tetris.gb");
	CHECK(HD_nextLine(&cursor, line, sizeof(line)));
	CHECK_STR(line, "/Roms/GBA/Metroid.gba");
	CHECK(HD_nextLine(&cursor, line, sizeof(line)));
	CHECK_STR(line, "/Tools/x.pak");
	CHECK(!HD_nextLine(&cursor, line, sizeof(line)));
	CHECK(!HD_nextLine(&cursor, line, sizeof(line)));
	cursor = "\n\r\n";
	CHECK(!HD_nextLine(&cursor, line, sizeof(line)));
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
	(void)id;
	snprintf(logged + strlen(logged), sizeof(logged) - strlen(logged), "%s\n", m);
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	if (!strcmp(id, ID) && !strcmp(key, "mode")) return cfg_mode;
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
static int fakeRunAction(const char* id) {
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
	writeFile(TEST_SDCARD_PATH "/.userdata/desktop/launcher/plugins.txt", "favorites=on\nhome-dashboard=on\n");
	writeFile(TEST_SDCARD_PATH "/Collections/Favorites.txt", "/Roms/GB/Tetris.gb\n/Roms/GB/Gone.gb\n/Roms/NES/Zelda.nes\n");
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

	// without favorites on, no pinned tab, and stats keeps its place
	writeFile(TEST_SDCARD_PATH "/.userdata/desktop/launcher/plugins.txt", "home-dashboard=on\n");
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

int main(void) {
	CHECK(system("rm -rf '" TEST_SDCARD_PATH "'") == 0);
	testModes();
	testTabs();
	testHomeMove();
	testLists();
	testText();
	testMostPlayed();
	testPico();
	testFavorites();
	testLayout();
	testPlugin();
	CHECK_DONE();
}
