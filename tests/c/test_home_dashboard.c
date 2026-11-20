// The home dashboard plugin: its model on its own, then the plugin against
// a fake host, on a card of its own under TEST_SDCARD_PATH.

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "check.h"
#include "fakeindex.h"
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
	CHECK(HD_modeOf("minimal") == HD_MODE_MINIMAL);
	CHECK(HD_modeOf("consoles") == HD_MODE_CONSOLES);
	CHECK_STR(HD_modeName(HD_MODE_CONSOLES), "consoles");
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

static void testRowsMove(void) {
	const int C = HD_FOCUS_CONTINUE;
	int counts[] = { 3, 2 };
	HDFocus f = { C, 0 };
	// one row: as the top shelf always went
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_DOWN);
	CHECK(f.row == 0 && f.col == 0);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_UP);
	CHECK(f.row == C);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_RIGHT);
	CHECK(f.row == 0 && f.col == 0);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_RIGHT);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_RIGHT);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_RIGHT);
	CHECK(f.row == 0 && f.col == 2); // stops at the end
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_DOWN);
	CHECK(f.row == 0 && f.col == 2);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_LEFT);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_LEFT);
	CHECK(f.col == 0);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_LEFT);
	CHECK(f.row == C);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_LEFT);
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_UP);
	CHECK(f.row == C && f.col == 0);

	// two rows: down to the next, keeping the column as far as it reaches
	f = (HDFocus){ 0, 2 };
	HD_rowsMove(&f, counts, 2, MOSAIC_BUTTON_DOWN);
	CHECK(f.row == 1 && f.col == 1);
	HD_rowsMove(&f, counts, 2, MOSAIC_BUTTON_DOWN);
	CHECK(f.row == 1);
	HD_rowsMove(&f, counts, 2, MOSAIC_BUTTON_UP);
	CHECK(f.row == 0 && f.col == 1);
	f = (HDFocus){ 1, 0 };
	HD_rowsMove(&f, counts, 2, MOSAIC_BUTTON_LEFT);
	CHECK(f.row == 1 && f.col == 0); // only the first row's left goes to the card

	// no rows: always the card; rows that went away
	f = (HDFocus){ C, 0 };
	HD_rowsMove(&f, counts, 0, MOSAIC_BUTTON_DOWN);
	CHECK(f.row == C);
	f = (HDFocus){ 4, 9 };
	HD_rowsMove(&f, counts, 2, MOSAIC_BUTTON_LEFT);
	CHECK(f.row == 1 && f.col == 0);
	f = (HDFocus){ 0, 9 };
	HD_rowsMove(&f, counts, 1, MOSAIC_BUTTON_LEFT);
	CHECK(f.row == 0 && f.col == 1);
}

static void testRows(void) {
	int rows[HD_ROW_KINDS];
	char unknown[64];
	CHECK(HD_parseRows(HD_ROWS_DEFAULT, rows, HD_ROW_KINDS, unknown, sizeof(unknown)) == 2);
	CHECK(rows[0] == HD_ROW_CONTINUE && rows[1] == HD_ROW_RECENT && !unknown[0]);
	CHECK(HD_parseRows(" Random,played ,, nope, added, random, shortcuts,recent,bad", rows, HD_ROW_KINDS, unknown,
		sizeof(unknown)) == 5);
	CHECK(rows[0] == HD_ROW_RANDOM && rows[1] == HD_ROW_PLAYED && rows[2] == HD_ROW_ADDED && rows[3] == HD_ROW_SHORTCUTS
		&& rows[4] == HD_ROW_RECENT);
	CHECK_STR(unknown, "nope,bad");
	CHECK(HD_parseRows("", rows, HD_ROW_KINDS, unknown, sizeof(unknown)) == 0);
	CHECK(HD_parseRows(NULL, rows, HD_ROW_KINDS, unknown, sizeof(unknown)) == 0);
	CHECK(HD_parseRows("played, random", rows, 1, unknown, sizeof(unknown)) == 1);
	CHECK_STR(HD_rowName(HD_ROW_ADDED), "added");
	CHECK_STR(HD_rowName(42), "");
	CHECK_STR(HD_rowLabel(HD_ROW_RECENT), "RECENT");
	CHECK_STR(HD_rowLabel(HD_ROW_SHORTCUTS), "SHORTCUTS");

	const char* p = " search ,random-game,,  system-info ";
	char name[16];
	CHECK(HD_listNext(&p, name, sizeof(name)) && !strcmp(name, "search"));
	CHECK(HD_listNext(&p, name, sizeof(name)) && !strcmp(name, "random-game"));
	CHECK(HD_listNext(&p, name, sizeof(name)) && !strcmp(name, "system-info"));
	CHECK(!HD_listNext(&p, name, sizeof(name)) && !name[0]);

	// continue: the other recents with a state to resume; recent: all after the first
	int resume[] = { 1, 0, 1, 1, 1 }, available[] = { 1, 1, 1, 0, 1 }, out[4];
	CHECK(HD_recentPick(HD_ROW_CONTINUE, resume, available, 5, out, 4) == 2);
	CHECK(out[0] == 2 && out[1] == 4);
	CHECK(HD_recentPick(HD_ROW_RECENT, resume, available, 5, out, 4) == 4);
	CHECK(out[0] == 1 && out[3] == 4);
	CHECK(HD_recentPick(HD_ROW_RECENT, resume, available, 5, out, 2) == 2);
	CHECK(HD_recentPick(HD_ROW_RECENT, resume, available, 1, out, 4) == 0);
}

// the missions row's cards: quests.cards's reply, their layout and bar
static void testMissions(void) {
	int rows[HD_ROW_KINDS];
	char unknown[64];
	CHECK(HD_parseRows("Missions, recent", rows, HD_ROW_KINDS, unknown, sizeof(unknown)) == 2);
	CHECK(rows[0] == HD_ROW_MISSIONS && !unknown[0]);
	CHECK_STR(HD_rowName(HD_ROW_MISSIONS), "missions");
	CHECK_STR(HD_rowLabel(HD_ROW_MISSIONS), "MISSIONS");

	HDMission m[HD_MISSIONS_MAX];
	const char* reply = "count=3\nid.0=games\ntitle.0=Play 3 different games today\nprogress.0=1 / 3\nvalue.0=1\n"
		"target.0=3\ndone.0=0\nreward.0=+1 quest done\nid.1=bad\nprogress.1=1 / 3\n"
		"id.2=play\ntitle.2=Play for 30 minutes today\nprogress.2=Done\nvalue.2=45\ntarget.2=30\ndone.2=1\n";
	CHECK(HD_parseMissions(reply, fakeEventValue, m, HD_MISSIONS_MAX) == 2); // the one without a title left out
	CHECK_STR(m[0].id, "games");
	CHECK_STR(m[0].title, "Play 3 different games today");
	CHECK_STR(m[0].progress, "1 / 3");
	CHECK_STR(m[0].reward, "+1 quest done");
	CHECK(m[0].value == 1 && m[0].target == 3 && !m[0].done);
	CHECK_STR(m[1].id, "play");
	CHECK(m[1].value == 45 && m[1].target == 30 && m[1].done && !m[1].reward[0]);
	CHECK(HD_parseMissions(reply, fakeEventValue, m, 1) == 1);
	CHECK(HD_parseMissions("", fakeEventValue, m, HD_MISSIONS_MAX) == 0);
	CHECK(HD_parseMissions(NULL, fakeEventValue, m, HD_MISSIONS_MAX) == 0);
	CHECK(HD_parseMissions("count=0\n", fakeEventValue, m, HD_MISSIONS_MAX) == 0);
	CHECK(HD_parseMissions(reply, NULL, m, HD_MISSIONS_MAX) == 0);

	// cards across: as many as are HD_MISSION_MIN_W wide, at least one
	int w;
	CHECK(HD_missionCards(620, 1, 10, &w) == 2 && w == 305);
	CHECK(HD_missionCards(740, 1, 10, &w) == 3 && w == 240);
	CHECK(HD_missionCards(1000, 2, 20, &w) == 2 && w == 490);
	CHECK(HD_missionCards(150, 1, 10, &w) == 1 && w == 150);
	CHECK(HD_missionCards(0, 1, 10, &w) == 0 && w == 0);

	// a card's parts: title, bar, then progress and reward, centred down it
	HDMissionLayout parts;
	MosaicRect card = { 10, 100, 300, 120 };
	CHECK(HD_missionLayout(&card, 1, 32, 24, 100, &parts) == 0);
	// 32 + 4 + 4 + 4 + 24 = 68 tall, from 100 + (120 - 68) / 2
	CHECK(parts.title.x == 18 && parts.title.y == 126 && parts.title.w == 284 && parts.title.h == 32);
	CHECK(parts.bar.y == 162 && parts.bar.h == 4 && parts.bar.w == 284);
	CHECK(parts.reward.x == 202 && parts.reward.y == 170 && parts.reward.w == 100 && parts.reward.h == 24);
	CHECK(parts.progress.x == 18 && parts.progress.w == 176);
	// a long reward gets half, none leaves the progress the width
	CHECK(HD_missionLayout(&card, 1, 32, 24, 900, &parts) == 0 && parts.reward.w == 142);
	CHECK(HD_missionLayout(&card, 1, 32, 24, 0, &parts) == 0 && parts.reward.w == 0 && parts.progress.w == 284);
	// too small
	MosaicRect fits = { 0, 0, 300, 68 };
	CHECK(HD_missionLayout(&fits, 1, 32, 24, 0, &parts) == 0 && parts.title.y == 0 && parts.progress.y == 44);
	MosaicRect small = { 0, 0, 300, 67 };
	CHECK(HD_missionLayout(&small, 1, 32, 24, 0, &parts) == -1);
	MosaicRect thin = { 0, 0, 16, 200 };
	CHECK(HD_missionLayout(&thin, 1, 32, 24, 0, &parts) == -1);

	CHECK(HD_barWidth(0, 30, 100) == 0);
	CHECK(HD_barWidth(15, 30, 100) == 50);
	CHECK(HD_barWidth(1, 1000, 100) == 1);
	CHECK(HD_barWidth(45, 30, 100) == 100);
	CHECK(HD_barWidth(5, 0, 100) == 0);
	CHECK(HD_barWidth(5, 10, 0) == 0);
}

static void testPicks(void) {
	// the newest kept, ties in the order added
	long long keys[3];
	int ids[3], n = 0;
	long long times[] = { 5, 9, 1, 9, 7, 3 };
	for (int i = 0; i < 6; i++) n = HD_topAdd(keys, ids, n, 3, times[i], i);
	CHECK(n == 3);
	CHECK(ids[0] == 1 && ids[1] == 3 && ids[2] == 4);
	CHECK(keys[0] == 9 && keys[2] == 7);
	CHECK(HD_topAdd(keys, ids, 0, 0, 1, 0) == 0);

	// random picks: all of a short stream, and spread over a long one
	unsigned int state = 0;
	CHECK(HD_random(&state) != 0 && state != 0);
	int picks[4], got = 0;
	state = 42;
	for (int i = 0; i < 3; i++) got = HD_reservoir(picks, 4, i, i * 10, &state);
	CHECK(got == 3 && picks[0] == 0 && picks[2] == 20);
	int hits[100] = { 0 };
	for (int round = 0; round < 500; round++) {
		got = 0;
		for (int i = 0; i < 100; i++) got = HD_reservoir(picks, 4, i, i, &state);
		CHECK(got == 4);
		for (int k = 0; k < 4; k++) hits[picks[k]]++;
		// never the same one twice
		CHECK(picks[0] != picks[1] && picks[0] != picks[2] && picks[1] != picks[3] && picks[2] != picks[3]);
	}
	int late = 0, early = 0;
	for (int i = 0; i < 50; i++) early += hits[i];
	for (int i = 50; i < 100; i++) late += hits[i];
	CHECK(early > 800 && late > 800); // 1000 each, about
	CHECK(HD_reservoir(picks, 0, 0, 1, &state) == 0);
}

static void testConsoles(void) {
	char name[64];
	HD_systemName("Game Boy (GB)", name, sizeof(name));
	CHECK_STR(name, "Game Boy");
	HD_systemName("Pico-8", name, sizeof(name));
	CHECK_STR(name, "Pico-8");
	HD_systemName("(GB)", name, sizeof(name));
	CHECK_STR(name, "(GB)");

	HDConsole list[3];
	int n = 0;
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_SYSTEM, 1, "Game Boy (GB)", "/Roms/Game Boy (GB)", "Game Boy (GB)", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_GAME, 1, "Game Boy (GB)", "/Roms/Game Boy (GB)/a.gb", "a", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_FOLDER, 1, "Game Boy (GB)", "/Roms/Game Boy (GB)/Hacks", "Hacks", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_GAME, 1, "Game Boy (GB)", "/Roms/Game Boy (GB)/Hacks/b.gb", "b", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_GAME, 1, "Game Boy (GB)", "/Roms/Game Boy (GB)/c.gb", "c", 0); // hidden
	// no emulator: not a console, and its games count nowhere
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_SYSTEM, 0, "Lynx (LYNX)", "/Roms/Lynx (LYNX)", "Lynx (LYNX)", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_GAME, 1, "Lynx (LYNX)", "/Roms/Lynx (LYNX)/x.lnx", "x", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_SYSTEM, 1, "NES (FC)", "/Roms/NES (FC)", "Famicom (FC)", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_SYSTEM, 1, "SNES (SFC)", "/Roms/SNES (SFC)", "SNES (SFC)", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_GAME, 1, "SNES (SFC)", "/Roms/SNES (SFC)/m.sfc", "m", 1);
	n = HD_consoleAdd(list, n, 3, MOSAIC_INDEX_SYSTEM, 1, "PS (PS)", "/Roms/PS (PS)", "PS (PS)", 1); // past max
	CHECK(n == 3);
	CHECK_STR(list[0].name, "Game Boy");
	CHECK(list[0].games == 2);
	CHECK_STR(list[1].name, "Famicom"); // its map.txt name
	CHECK(list[1].games == 0 && list[2].games == 1);
	n = HD_consolesKeep(list, n);
	CHECK(n == 2);
	CHECK_STR(list[1].system, "SNES (SFC)");
	CHECK_STR(list[1].path, "/Roms/SNES (SFC)");
}

// a font where every byte is 10 wide
static int measureTen(const char* text, void* data) {
	(void)data;
	return (int)strlen(text) * 10;
}

static void testWrap(void) {
	char lines[3][HD_LINE_MAX];
	CHECK(HD_wrap("Game Boy Advance", 100, measureTen, NULL, lines, 3) == 2);
	CHECK_STR(lines[0], "Game Boy");
	CHECK_STR(lines[1], "Advance");
	CHECK(HD_wrap("Nintendo Entertainment System Classic", 100, measureTen, NULL, lines, 2) == 2);
	CHECK_STR(lines[0], "Nintendo");
	CHECK_STR(lines[1], "Entertainment System Classic"); // the rest, cut when drawn
	CHECK(HD_wrap("Supercalifragilistic NES", 100, measureTen, NULL, lines, 3) == 2);
	CHECK_STR(lines[0], "Supercalifragilistic"); // too wide alone
	CHECK_STR(lines[1], "NES");
	CHECK(HD_wrap("  NES  ", 100, measureTen, NULL, lines, 3) == 1);
	CHECK_STR(lines[0], "NES");
	CHECK(HD_wrap("", 100, measureTen, NULL, lines, 3) == 0);
	CHECK(HD_wrap("NES", 100, measureTen, NULL, lines, 0) == 0);
	MosaicRect box = { 0, 10, 100, 100 };
	CHECK(HD_linesTop(&box, 2, 20) == 40);
	CHECK(HD_wrap("Game Boy Advance", 60, measureTen, NULL, lines, 3) == 3);
	CHECK(!HD_linesFit(lines, 3, 60, measureTen, NULL)); // "Advance" is 70
	CHECK(HD_linesFit(lines, 2, 60, measureTen, NULL));

	char clock[16];
	HD_clock(14, 5, 1, clock, sizeof(clock));
	CHECK_STR(clock, "14:05");
	HD_clock(14, 5, 0, clock, sizeof(clock));
	CHECK_STR(clock, "2:05 PM");
	HD_clock(0, 30, 0, clock, sizeof(clock));
	CHECK_STR(clock, "12:30 AM");
	HD_clock(12, 0, 0, clock, sizeof(clock));
	CHECK_STR(clock, "12:00 PM");
	HD_clock(7, 9, 1, clock, sizeof(clock));
	CHECK_STR(clock, "07:09");
}

static void testGrids(void) {
	// two rows of tiles when they fit, as the pico8 grid always had
	MosaicRect content = { 10, 50, 620, 360 };
	int cell, cols, rows;
	CHECK(HD_grid(&content, 1, 20, &cell, &cols, &rows) == 0);
	CHECK(rows == 2 && cell == (360 - 40 - 10) / 2 && cols == HD_cellsFit(620, cell, 10));
	content.h = 60;
	CHECK(HD_grid(&content, 1, 20, &cell, &cols, &rows) == 0);
	CHECK(rows == 1 && cell == 40);
	content.h = 25;
	CHECK(HD_grid(&content, 1, 20, &cell, &cols, &rows) == -1);

	// the minimal mode: the clock on top, the card centred under it
	HDLayout layout;
	content = (MosaicRect){ 10, 50, 620, 360 };
	CHECK(HD_minimalLayout(&content, 30, 1, &layout) == 0);
	CHECK(layout.card.w == 496 && layout.card.x == 10 + (620 - 496) / 2);
	CHECK(layout.card.h == 198 && layout.card.y == 50 + 40 + (320 - 198) / 2);
	CHECK(layout.art.x == layout.card.x + 10 && layout.text.x > layout.art.x + layout.art.w);
	CHECK(layout.cells == 0);
	content.h = 60;
	CHECK(HD_minimalLayout(&content, 30, 1, &layout) == -1);

	// a heading over the shelf takes its room from the card, as far as the
	// card keeps its text, then from the shelf's cards
	MosaicRect area = { 0, 0, 640, 480 }, status = { 500, 10, 130, 30 }, none = { 0, 0, 0, 0 };
	CHECK(HD_layout(&area, &status, &none, 30, 440, 1, 20, 1, &layout) == 0);
	HDLayout titled = layout;
	HD_layoutTitle(&titled, 16, 40, 20, 1);
	CHECK(titled.card.h == layout.card.h - 16 && titled.card.y == layout.card.y);
	CHECK(titled.art.h == layout.art.h - 16);
	CHECK(titled.shelf.y == layout.shelf.y && titled.cell == layout.cell && titled.cells == layout.cells);
	titled = layout;
	HD_layoutTitle(&titled, 16, layout.card.h - 6, 20, 1);
	CHECK(titled.card.h == layout.card.h - 6);
	CHECK(titled.shelf.y == layout.shelf.y + 10 && titled.cell == layout.cell - 10);
	// not when the shelf's cards wouldn't fit, or with no shelf
	HDLayout tall = layout;
	HD_layoutTitle(&tall, layout.shelf.h, layout.card.h, 20, 1);
	CHECK(tall.card.h == layout.card.h && tall.shelf.y == layout.shelf.y);
	HDLayout bare = layout;
	bare.cells = 0;
	HD_layoutTitle(&bare, 16, 40, 20, 1);
	CHECK(bare.card.h == layout.card.h);
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

	HD_daysAgo(0, out, sizeof(out));
	CHECK_STR(out, "Today");
	HD_daysAgo(-2, out, sizeof(out));
	CHECK_STR(out, "Today");
	HD_daysAgo(1, out, sizeof(out));
	CHECK_STR(out, "Yesterday");
	HD_daysAgo(4, out, sizeof(out));
	CHECK_STR(out, "4 days ago");

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
static const char* cfg_rows = NULL; // NULL for the default
static const char* cfg_shortcuts = NULL;
static const char* cfg_clock = NULL;
static char logged[4096];
static void fakeLog(const char* id, const char* m) {
	snprintf(logged + strlen(logged), sizeof(logged) - strlen(logged), "%s\n", m);
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "mode")) return cfg_mode;
	if (!strcmp(key, "rows") && cfg_rows) return cfg_rows;
	if (!strcmp(key, "shortcuts") && cfg_shortcuts) return cfg_shortcuts;
	if (!strcmp(key, "clock") && cfg_clock) return cfg_clock;
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

// actions for the shortcuts: search's without a tile, random-game's tile
// with an icon, activity-stats' that can't run now and one that needs a game
static const struct {
	const char* id;
	const char* label;
	const char* plugin;
	int takes_path;
	int available;
	const char* icon; // its quick menu tile's, "" for a tile without one, NULL for no tile
} fake_actions[] = {
	{ "gamelist.open", "OPEN", "", 0, 1, NULL },
	{ "search.open", "SEARCH", "search", 0, 1, NULL },
	{ "random-game.pick", "RANDOM", "random-game", 0, 1, NULL },
	{ "random-game.tile", "Random", "random-game", 0, 1, "/icons/dice.png" },
	{ "activity-stats.open", "Activity", "activity-stats", 0, 0, "" },
	{ "netplay.host", "HOST", "netplay", 1, 1, NULL },
};
#define FAKE_ACTIONS (int)(sizeof(fake_actions) / sizeof(fake_actions[0]))
static int fakeActionsCount(void) { return FAKE_ACTIONS; }
static int fakeActionInfo(int i, MosaicActionInfo* out) {
	if (i < 0 || i >= FAKE_ACTIONS || !out) return -1;
	snprintf(out->id, sizeof(out->id), "%s", fake_actions[i].id);
	snprintf(out->label, sizeof(out->label), "%s", fake_actions[i].label);
	snprintf(out->plugin_id, sizeof(out->plugin_id), "%s", fake_actions[i].plugin);
	out->takes_path = fake_actions[i].takes_path;
	return 0;
}
static int fakeActionAvailable(const char* id, const char* path) {
	(void)path;
	for (int i = 0; i < FAKE_ACTIONS; i++) {
		if (!strcmp(fake_actions[i].id, id)) return fake_actions[i].available;
	}
	return -1;
}
static int fakeQuickTileIcon(const char* id, char* out, size_t size) {
	out[0] = '\0';
	for (int i = 0; i < FAKE_ACTIONS; i++) {
		if (strcmp(fake_actions[i].id, id) != 0 || !fake_actions[i].icon) continue;
		if (strlen(fake_actions[i].icon) >= size) return -1;
		snprintf(out, size, "%s", fake_actions[i].icon);
		return 0;
	}
	return -1;
}
// the play history: Zelda most, then one no longer on the card, then Tetris
static int fakePlayGameInfo(int i, MosaicPlayGame* out) {
	static const char* paths[] = { TEST_SDCARD_PATH "/Roms/NES/Zelda.nes", TEST_SDCARD_PATH "/Roms/NES/Gone.nes",
		TEST_SDCARD_PATH "/Roms/GB/Tetris.gb" };
	if (i < 0 || i >= 3) return -1;
	snprintf(out->path, sizeof(out->path), "%s", paths[i]);
	out->seconds = 1000 - i;
	return 0;
}
static int subscribed = 0;
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	subscribed++;
	return fakeIndexSubscribe(name, on_event, data);
}

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
	.provide = fakeProvide,
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
	focus = (HDFocus){ HD_FOCUS_CONTINUE, 0 };
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
	CHECK(home_row_count == 1 && home_rows[0].kind == HD_ROW_RECENT && home_rows[0].count == 2);
	press(MOSAIC_BUTTON_DOWN);
	CHECK(focus.row == 0 && focus.col == 0);
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

// romm's romm.downloads: Tetris today, a game no longer on the card, a
// hidden one and Zelda three days ago
static char downloads_reply[2048];
static int fakeDownloads(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char count[16];
	CHECK(fakeEventValue(args, "count", count, sizeof(count)) > 0);
	CHECK_STR(count, "3");
	snprintf(out, size, "%s", downloads_reply);
	return 0;
}

static void testDownloads(void) {
	fake_provider_count = 0;
	fake.call = NULL;
	reset("dashboard");
	CHECK(download_count == 0);
	CHECK(strstr(logged, "history on, downloads 0") != NULL);
	long long now = (long long)time(NULL);
	snprintf(downloads_reply, sizeof(downloads_reply),
		"count=4\npath.0=%s\nname.0=Tetris\nat.0=%lld\npath.1=%s\nname.1=Gone\nat.1=%lld\n"
		"path.2=%s\nname.2=Hidden\nat.2=%lld\npath.3=%s\nname.3=Zelda\nat.3=%lld\n",
		TEST_SDCARD_PATH "/Roms/GB/Tetris.gb", now, TEST_SDCARD_PATH "/Roms/GB/Gone.gb", now,
		TEST_SDCARD_PATH "/Roms/GB/Hidden.gb", now, TEST_SDCARD_PATH "/Roms/NES/Zelda.nes", now - 3 * 86400);
	writeFile(TEST_SDCARD_PATH "/Roms/GB/Hidden.gb", "");
	hidden_path = TEST_SDCARD_PATH "/Roms/GB/Hidden.gb";
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	fakeProvide("romm.downloads", fakeDownloads);
	logged[0] = '\0';
	CHECK(registered.enter(registered.data) == 0);
	CHECK(download_count == 2);
	CHECK_STR(downloads[0].name, "Tetris");
	CHECK_STR(downloads[1].name, "Zelda");
	CHECK(daysSince(downloaded_at[0]) == 0 && daysSince(downloaded_at[1]) == 3);
	CHECK(strstr(logged, "history on, downloads 2") != NULL);
	// none downloaded: no section
	snprintf(downloads_reply, sizeof(downloads_reply), "count=0\n");
	CHECK(registered.enter(registered.data) == 0);
	CHECK(download_count == 0);
	// only on the dashboard's Stats tab
	download_count = 1;
	snprintf(downloads_reply, sizeof(downloads_reply), "count=1\npath.0=%s\nname.0=Tetris\nat.0=%lld\n",
		TEST_SDCARD_PATH "/Roms/GB/Tetris.gb", now);
	reset("minui");
	CHECK(download_count == 0);
	hidden_path = "";
	fake_provider_count = 0;
	fake.call = NULL;
	fake.event_value = NULL;
}

// the home tab's rows from the card index, the play history and the
// shortcuts, then the minimal and consoles modes
static void mostPlayedList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(TEST_SDCARD_PATH "/Roms/GB/Tetris.gb", ctx);
	add(TEST_SDCARD_PATH "/Roms/GB/Hidden.gb", ctx);
	add(TEST_SDCARD_PATH "/Roms/NES/Zelda.nes", ctx);
}

static int rowHas(int kind, const char* path) {
	for (int i = 0; i < home_row_count; i++) {
		if (home_rows[i].kind != kind) continue;
		for (int k = 0; k < home_rows[i].count; k++) {
			if (!strcmp(home_rows[i].items[k]->path, path)) return 1;
		}
	}
	return 0;
}

static void testRowsAndModes(void) {
	writeFile(TEST_SDCARD_PATH "/Emus/test/GB.pak/launch.sh", "");
	writeFile(TEST_SDCARD_PATH "/Emus/test/NES.pak/launch.sh", "");
	writeFile(TEST_SDCARD_PATH "/Roms/GB/Hidden.gb", "");
	fakeIndexDrop();
	fake_index_ready = 1;
	fake.index_count = fakeIndexCount;
	fake.index_entry = fakeIndexEntry;
	fake.index_find = fakeIndexFind;
	fake.index_refresh = fakeIndexRefresh;
	fake.subscribe = fakeSubscribe;
	fake.actions_count = fakeActionsCount;
	fake.action_info = fakeActionInfo;
	fake.action_available = fakeActionAvailable;
	fake.quick_tile_icon = fakeQuickTileIcon;
	fake.play_game_info = fakePlayGameInfo;
	fake_loaded = "home-dashboard";
	fake_set_count = 0;
	hidden_path = TEST_SDCARD_PATH "/Roms/GB/Hidden.gb";
	cfg_rows = "added, played, random, shortcuts, recent, bogus, added";
	cfg_shortcuts = "search, random-game, activity-stats, nope, netplay.host";

	reset("dashboard");
	CHECK(strstr(logged, "rows: no such row bogus") != NULL);
	CHECK(strstr(logged, "rows: added 3 played 2 random 3 shortcuts 2 recent 2") != NULL);
	CHECK(subscribed > 0);
	// added: what's hidden left out (the order is by the later of each
	// file's times, and the ctime this test can't set)
	CHECK(home_rows[0].kind == HD_ROW_ADDED);
	CHECK(rowHas(HD_ROW_ADDED, TEST_SDCARD_PATH "/Roms/NES/Zelda.nes"));
	CHECK(rowHas(HD_ROW_ADDED, TEST_SDCARD_PATH "/Roms/GB/stray.p8"));
	CHECK(!rowHas(HD_ROW_ADDED, TEST_SDCARD_PATH "/Roms/GB/Hidden.gb"));
	// played: without activity-stats, the history's still on the card
	CHECK_STR(home_rows[1].items[0]->name, "Zelda");
	CHECK_STR(home_rows[1].items[1]->name, "Tetris");
	CHECK(!rowHas(HD_ROW_RANDOM, TEST_SDCARD_PATH "/Roms/GB/Hidden.gb"));
	CHECK(rowHas(HD_ROW_RANDOM, TEST_SDCARD_PATH "/Roms/NES/Zelda.nes"));
	// shortcuts: an action by id, or a plugin's tile, and what can't run is logged once
	CHECK(shortcut_count == 2);
	CHECK_STR(shortcuts[shortcut_shown[0]].id, "search.open");
	CHECK_STR(shortcuts[shortcut_shown[0]].label, "SEARCH");
	CHECK_STR(shortcuts[shortcut_shown[0]].icon, "");
	CHECK_STR(shortcuts[shortcut_shown[1]].id, "random-game.tile");
	CHECK_STR(shortcuts[shortcut_shown[1]].icon, "/icons/dice.png");
	CHECK(strstr(logged, "shortcut activity-stats: activity-stats.open can't run now") != NULL);
	CHECK(strstr(logged, "shortcut nope: no such action or plugin") != NULL);
	CHECK(strstr(logged, "shortcut netplay.host: netplay.host can't run now") != NULL);
	logged[0] = '\0';
	CHECK(registered.enter(registered.data) == 0);
	CHECK(strstr(logged, "shortcut ") == NULL);

	// down through the rows to the shortcuts, which A runs
	press(MOSAIC_BUTTON_DOWN);
	CHECK(focus.row == 0);
	press(MOSAIC_BUTTON_A);
	CHECK_STR(opened_path, home_rows[0].items[0]->path);
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_DOWN);
	CHECK(focus.row == 3 && home_rows[3].kind == HD_ROW_SHORTCUTS);
	CHECK_STR(openLabel(), "OPEN");
	press(MOSAIC_BUTTON_A);
	CHECK_STR(ran, "search.open");
	press(MOSAIC_BUTTON_RIGHT);
	press(MOSAIC_BUTTON_A);
	CHECK_STR(ran, "random-game.tile");
	CHECK(strstr(logged, "shortcut random-game.tile") != NULL);
	press(MOSAIC_BUTTON_DOWN);
	CHECK(focus.row == 4 && focus.col == 1);
	CHECK_STR(openLabel(), "OPEN"); // the pak
	press(MOSAIC_BUTTON_LEFT);
	press(MOSAIC_BUTTON_A);
	CHECK(opened_recent == 1);

	// a card change while the home shows builds the rows again
	hidden_path = TEST_SDCARD_PATH "/Roms/NES/Zelda.nes";
	logged[0] = '\0';
	fakeIndexRebuild();
	CHECK(strstr(logged, "rows: added 3 played 1 random 3 shortcuts 2 recent 2") != NULL);
	CHECK(!rowHas(HD_ROW_ADDED, TEST_SDCARD_PATH "/Roms/NES/Zelda.nes"));
	hidden_path = TEST_SDCARD_PATH "/Roms/GB/Hidden.gb";
	registered.exit(registered.data);

	// played: activity-stats' most played set while it's on
	fake_loaded = "activity-stats,home-dashboard";
	fakeSetRegister("activity-stats.most-played", noHas, mostPlayedList);
	cfg_rows = "played";
	reset("dashboard");
	CHECK(home_row_count == 1 && home_rows[0].count == 2);
	CHECK_STR(home_rows[0].items[0]->name, "Tetris");
	CHECK_STR(home_rows[0].items[1]->name, "Zelda");
	// with nothing in any row, none
	cfg_rows = "random";
	fake.index_refresh = NULL;
	reset("dashboard");
	CHECK(home_row_count == 0 && strstr(logged, "rows: none") != NULL);
	press(MOSAIC_BUTTON_DOWN);
	CHECK(focus.row == HD_FOCUS_CONTINUE);
	fake.index_refresh = fakeIndexRefresh;
	cfg_rows = NULL;
	fake_set_count = 0;

	// minimal: A resumes the last game; the clock as the setting says
	cfg_clock = "12h";
	reset("minimal");
	CHECK(clock24 == 0);
	CHECK_STR(openLabel(), "RESUME");
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_A);
	CHECK(opened_recent == 0 && opened_resume == 1);
	cfg_clock = NULL;
	reset("minimal");
	CHECK(clock24 == 1);

	// consoles: the systems with an emulator and games, counted without
	// what's hidden; A opens one and closes on to it
	reset("consoles");
	CHECK(strstr(logged, "consoles 2: GB 2, NES 1") != NULL);
	CHECK_STR(openLabel(), "OPEN");
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(console_selected == 1);
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_CLOSE);
	CHECK_STR(opened_path, TEST_SDCARD_PATH "/Roms/NES");
	// one that's all hidden goes when the card changes
	hidden_path = TEST_SDCARD_PATH "/Roms/NES/Zelda.nes";
	fakeIndexRebuild();
	CHECK(console_count == 1 && console_selected == 0);
	hidden_path = "";
	// before the index is ready, none, then all as it comes in
	fakeIndexDrop();
	fake_index_ready = 0;
	reset("consoles");
	CHECK(console_count == 0 && card_dirty);
	CHECK(strstr(logged, "card index not ready yet") != NULL);
	fakeIndexRebuild();
	CHECK(console_count == 2 && consoles[0].games == 3);

	fake.index_count = NULL;
	fake.index_entry = NULL;
	fake.index_find = NULL;
	fake.index_refresh = NULL;
	fake.subscribe = NULL;
	fake.actions_count = NULL;
	fake.action_info = NULL;
	fake.action_available = NULL;
	fake.quick_tile_icon = NULL;
	fake.play_game_info = NULL;
	cfg_shortcuts = NULL;
	fake_index_on_ready = NULL;
}

// the missions row: gamification's quests.cards, asked as the home opens
static int cards_asked = 0;
static int fakeCards(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	cards_asked++;
	snprintf(out, size, "count=2\nid.0=games\ntitle.0=Play 3 different games today\nprogress.0=1 / 3\nvalue.0=1\n"
		"target.0=3\ndone.0=0\nreward.0=+1 quest done\nid.1=play\ntitle.1=Play for 30 minutes today\n"
		"progress.1=Done\nvalue.1=45\ntarget.1=30\ndone.1=1\nreward.1=+1 quest done\n");
	return 0;
}

static void testMissionsRow(void) {
	fake_provider_count = 0;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	fake_loaded = "home-dashboard";
	fake_set_count = 0;
	// off unless the row is named: not asked
	cfg_rows = NULL;
	cards_asked = 0;
	fakeProvide("quests.cards", fakeCards);
	reset("dashboard");
	CHECK(cards_asked == 0 && mission_count == 0);

	// without gamification the row has nothing, and is left out
	fake_provider_count = 0;
	cfg_rows = "missions, recent";
	reset("dashboard");
	CHECK(strstr(logged, "missions: none") != NULL);
	CHECK(strstr(logged, "rows: recent 2") != NULL);

	// with it, a card per quest, asked once as the home opens
	fakeProvide("quests.cards", fakeCards);
	cards_asked = 0;
	reset("dashboard");
	CHECK(cards_asked == 1);
	CHECK(strstr(logged, "missions: games 1/3, play 45/30 done") != NULL);
	CHECK(strstr(logged, "rows: missions 2 recent 2") != NULL);
	CHECK(home_rows[0].kind == HD_ROW_MISSIONS && home_rows[0].count == 2);
	// down to it, right to the second card, and A opens the Quests screen
	press(MOSAIC_BUTTON_DOWN);
	CHECK(focus.row == 0 && focus.col == 0);
	CHECK_STR(openLabel(), "OPEN");
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(focus.col == 1);
	press(MOSAIC_BUTTON_A);
	CHECK_STR(ran, "gamification.open");
	CHECK(strstr(logged, "mission play 0") != NULL);
	// back from the Quests screen (or a game), asked again, logged only
	// when the cards change
	logged[0] = '\0';
	CHECK(registered.enter(registered.data) == 0);
	CHECK(cards_asked == 2);
	CHECK(strstr(logged, "missions:") == NULL);
	// not in the other modes
	reset("minui");
	CHECK(cards_asked == 2 && mission_count == 0);

	cfg_rows = NULL;
	fake_provider_count = 0;
	fake.call = NULL;
	fake.event_value = NULL;
}

// home-dashboard.setup: a mode it has is saved, any other refused
static void fakeCfgSetMode(const char* key, const char* value) {
	CHECK_STR(key, "mode");
	snprintf(cfg_mode, sizeof(cfg_mode), "%s", value);
}

static void testSetup(void) {
	char out[MOSAIC_CALL_MAX], value[64];
	fake_provider_count = 0;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	fake.cfg_set = fakeCfgSetMode;
	reset("dashboard");
	CHECK(fakeCall("home-dashboard.setup", "", out, sizeof(out)) > 0);
	CHECK(fakeEventValue(out, "mode", value, sizeof(value)) > 0);
	CHECK_STR(value, "dashboard");
	CHECK(fakeEventValue(out, "mode.count", value, sizeof(value)) > 0);
	CHECK_STR(value, "6");
	CHECK(fakeEventValue(out, "mode.3", value, sizeof(value)) > 0);
	CHECK_STR(value, "pico8");
	CHECK(fakeEventValue(out, "mode.5", value, sizeof(value)) > 0);
	CHECK_STR(value, "consoles");
	CHECK(fakeEventValue(out, "mode.label.1", value, sizeof(value)) > 0);
	CHECK_STR(value, "minui");
	CHECK(fakeEventValue(out, "restart", value, sizeof(value)) > 0);
	CHECK_STR(value, "1");
	CHECK(fakeEventValue(out, "error", value, sizeof(value)) < 0);

	CHECK(fakeCall("home-dashboard.setup", "mode=minui", out, sizeof(out)) > 0);
	CHECK_STR(cfg_mode, "minui");
	CHECK(fakeEventValue(out, "mode", value, sizeof(value)) > 0);
	CHECK_STR(value, "minui");
	CHECK(strstr(logged, "setup: mode minui, from the next start") != NULL);
	CHECK(fakeCall("home-dashboard.setup", "mode=fancy", out, sizeof(out)) > 0);
	CHECK_STR(cfg_mode, "minui");
	CHECK(fakeEventValue(out, "error", value, sizeof(value)) > 0);
	CHECK_STR(value, "mode");
	// keys it doesn't take are ignored
	CHECK(fakeCall("home-dashboard.setup", "theme=dark", out, sizeof(out)) > 0);
	CHECK(fakeEventValue(out, "error", value, sizeof(value)) < 0);
	// a reply that doesn't fit is an error
	CHECK(HD_setupReply("dashboard", 0, out, 40) == -1);

	fake_provider_count = 0;
	fake.call = NULL;
	fake.event_value = NULL;
	fake.cfg_set = NULL;
}

int main(void) {
	CHECK(system("rm -rf '" TEST_SDCARD_PATH "'") == 0);
	testModes();
	testTabs();
	testRowsMove();
	testRows();
	testMissions();
	testPicks();
	testConsoles();
	testWrap();
	testGrids();
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
	testDownloads();
	testSetup();
	testRowsAndModes();
	testMissionsRow();
	CHECK_DONE();
}
