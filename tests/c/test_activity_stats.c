// The activity stats plugin: its model on its own, then the plugin against
// a fake host with a fixture play history.

#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "words.h"
#include "../../plugins/activity-stats/activity_stats.c"

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms/Stats"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

///////////////////////////////////////
// the model

static void testName(void) {
	char out[64];
	AS_name("Tetris", "/card/Roms/GB/Other.gb", out, sizeof(out));
	CHECK_STR(out, "Tetris");
	AS_name("", "/card/Roms/GB/Tetris (USA).gb", out, sizeof(out));
	CHECK_STR(out, "Tetris (USA)");
	AS_name(NULL, "/card/Roms/GB/Disc.v1.m3u", out, sizeof(out));
	CHECK_STR(out, "Disc.v1");
	AS_name("", "/card/Roms/GB/.hidden", out, sizeof(out));
	CHECK_STR(out, ".hidden");
	AS_name("", "", out, sizeof(out));
	CHECK_STR(out, "Unknown game");
	AS_name(NULL, NULL, out, sizeof(out));
	CHECK_STR(out, "Unknown game");
	AS_name("", "/card/Roms/GB/", out, sizeof(out));
	CHECK_STR(out, "Unknown game");
	char small[4];
	AS_name("", "/x/Longer.gb", small, sizeof(small));
	CHECK_STR(small, "Lon");
}

static void testRank(void) {
	ASGame games[5];
	memset(games, 0, sizeof(games));
	// seconds, last, count
	games[0] = (ASGame){ "", "A", 100, 10, 9 };
	games[1] = (ASGame){ "", "B", 500, 20, 1 };
	games[2] = (ASGame){ "", "C", 100, 30, 9 };
	games[3] = (ASGame){ "", "D", 100, 5, 3 };
	games[4] = (ASGame){ "", "E", 500, 20, 1 };
	int order[5];
	AS_rankPlayed(games, 5, order);
	// B and E tie on everything, so the first listed goes first; then C over
	// A by the latest played, then D with fewer sessions
	CHECK(order[0] == 1 && order[1] == 4 && order[2] == 2 && order[3] == 0 && order[4] == 3);
	AS_rankLaunched(games, 5, order);
	CHECK(order[0] == 2 && order[1] == 0 && order[2] == 3 && order[3] == 1 && order[4] == 4);
	AS_rankPlayed(games, 0, order); // nothing to do

	long long seconds = 0, sessions = 0;
	games[3].seconds = -1;
	games[3].count = -1;
	AS_totals(games, 5, &seconds, &sessions);
	CHECK(seconds == 1200);
	CHECK(sessions == 20);
	AS_totals(games, 0, &seconds, &sessions);
	CHECK(seconds == 0 && sessions == 0);
}

static void testFormat(void) {
	char out[64];
	AS_duration(-1, out, sizeof(out));
	CHECK_STR(out, "");
	AS_duration(0, out, sizeof(out));
	CHECK_STR(out, "0m");
	AS_duration(30, out, sizeof(out));
	CHECK_STR(out, "<1m");
	AS_duration(60, out, sizeof(out));
	CHECK_STR(out, "1m");
	AS_duration(2700, out, sizeof(out));
	CHECK_STR(out, "45m");
	AS_duration(7200, out, sizeof(out));
	CHECK_STR(out, "2h");
	AS_duration(7500, out, sizeof(out));
	CHECK_STR(out, "2h 5m");
	AS_duration(360059, out, sizeof(out));
	CHECK_STR(out, "100h");

	AS_sessions(1, out, sizeof(out));
	CHECK_STR(out, "1 session");
	AS_sessions(0, out, sizeof(out));
	CHECK_STR(out, "0 sessions");
	AS_sessions(4, out, sizeof(out));
	CHECK_STR(out, "4 sessions");

	ASGame game = { "", "Zelda", 8100, 0, 2 };
	AS_rowRight(&game, 0, out, sizeof(out));
	CHECK_STR(out, "2h 15m \xc2\xb7 2 sessions");
	AS_rowRight(&game, 1, out, sizeof(out));
	CHECK_STR(out, "2 sessions \xc2\xb7 2h 15m");
	game.count = 1;
	game.seconds = 20;
	AS_rowRight(&game, 0, out, sizeof(out));
	CHECK_STR(out, "<1m \xc2\xb7 1 session");

	AS_listCaption(3, 9000, 5, out, sizeof(out));
	CHECK_STR(out, "3 games \xc2\xb7 2h 30m \xc2\xb7 5 sessions");
	AS_listCaption(1, 60, 1, out, sizeof(out));
	CHECK_STR(out, "1 game \xc2\xb7 1m \xc2\xb7 1 session");

	long long week[7] = { 900, 1600, 7200, 50, 300, 0, 0 };
	AS_chartCaption(week, 7, out, sizeof(out));
	CHECK_STR(out, "7 days: 2h 47m \xc2\xb7 23m a day");
	// unknown days are left out of the average
	long long some[3] = { 3600, -1, -1 };
	AS_chartCaption(some, 3, out, sizeof(out));
	CHECK_STR(out, "3 days: 1h \xc2\xb7 1h a day");
	long long none[2] = { -1, -1 };
	AS_chartCaption(none, 2, out, sizeof(out));
	CHECK_STR(out, "2 days: no play history");
}

static time_t localTime(int year, int month, int day, int hour, int minute) {
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = minute;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

static void testDays(void) {
	setenv("TZ", "UTC0", 1);
	tzset();
	time_t now = localTime(2026, 11, 9, 12, 0); // a Monday
	char out[16];
	AS_dayLabel(now, 7, 0, out, sizeof(out));
	CHECK_STR(out, "Mon");
	AS_dayLabel(now, 7, 1, out, sizeof(out));
	CHECK_STR(out, "Sun");
	AS_dayLabel(now, 7, 6, out, sizeof(out));
	CHECK_STR(out, "Tue");
	AS_dayLabel(now, 7, 7, out, sizeof(out));
	CHECK_STR(out, "");
	AS_dayLabel(now, 7, -1, out, sizeof(out));
	CHECK_STR(out, "");
	AS_dayLabel(now, 30, 0, out, sizeof(out));
	CHECK_STR(out, "9");
	AS_dayLabel(now, 30, 3, out, sizeof(out));
	CHECK_STR(out, "");
	AS_dayLabel(now, 30, 7, out, sizeof(out));
	CHECK_STR(out, "2");
	AS_dayLabel(now, 30, 14, out, sizeof(out));
	CHECK_STR(out, "26");
	AS_dayLabel(now, 30, 28, out, sizeof(out));
	CHECK_STR(out, "12");
	AS_dayLabel(now, 30, 30, out, sizeof(out));
	CHECK_STR(out, "");

	// just after a 25 hour day, early in the morning
	setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
	tzset();
	now = localTime(2026, 11, 2, 0, 30);
	struct tm tm;
	AS_dayOf(now, 0, &tm);
	CHECK(tm.tm_mday == 2 && tm.tm_mon == 10 && tm.tm_hour == 12);
	AS_dayOf(now, 1, &tm);
	CHECK(tm.tm_mday == 1 && tm.tm_mon == 10);
	AS_dayOf(now, 2, &tm);
	CHECK(tm.tm_mday == 31 && tm.tm_mon == 9);
	// and late in the evening of a 23 hour one
	now = localTime(2026, 3, 8, 23, 50);
	AS_dayOf(now, 1, &tm);
	CHECK(tm.tm_mday == 7 && tm.tm_mon == 2);
	unsetenv("TZ");
	tzset();
}

static void testChart(void) {
	long long zeros[3] = { 0, 0, -1 };
	CHECK(AS_axisMax(zeros, 3) == 3600);
	CHECK(AS_axisMax(zeros, 0) == 3600);
	long long a[2] = { 100, 0 };
	CHECK(AS_axisMax(a, 2) == 900);
	long long b[1] = { 900 };
	CHECK(AS_axisMax(b, 1) == 900);
	long long c[2] = { 0, 901 };
	CHECK(AS_axisMax(c, 2) == 1800);
	long long d[3] = { 5000, -1, 20 };
	CHECK(AS_axisMax(d, 3) == 7200);
	long long e[1] = { 50000 };
	CHECK(AS_axisMax(e, 1) == 86400);
	long long f[1] = { 90000 };
	CHECK(AS_axisMax(f, 1) == 172800);

	CHECK(AS_barHeight(0, 3600, 100) == 0);
	CHECK(AS_barHeight(-1, 3600, 100) == 0);
	CHECK(AS_barHeight(1, 3600, 100) == 1);
	CHECK(AS_barHeight(1800, 3600, 100) == 50);
	CHECK(AS_barHeight(3600, 3600, 100) == 100);
	CHECK(AS_barHeight(7200, 3600, 100) == 100);
	CHECK(AS_barHeight(100, 0, 100) == 0);
	CHECK(AS_barHeight(100, 3600, 0) == 0);

	MosaicRect plot = { 10, 20, 70, 100 };
	// the oldest on the left, today on the right
	CHECK(rectIs(AS_slot(&plot, 7, 6), 10, 20, 10, 100));
	CHECK(rectIs(AS_slot(&plot, 7, 0), 70, 20, 10, 100));
	CHECK(AS_slot(&plot, 7, 7).w == 0);
	CHECK(AS_slot(&plot, 7, -1).w == 0);
	CHECK(rectIs(AS_bar(&plot, 7, 0, 1800, 3600), 72, 70, 6, 50));
	CHECK(rectIs(AS_bar(&plot, 7, 6, 3600, 3600), 12, 20, 6, 100));
	CHECK(AS_bar(&plot, 7, 3, 0, 3600).w == 0);
	CHECK(AS_bar(&plot, 7, 3, -1, 3600).w == 0);
	// thirty slots in 70: uneven, but they cover the plot with no gaps
	int x = plot.x;
	for (int i = 29; i >= 0; i--) {
		MosaicRect slot = AS_slot(&plot, 30, i);
		CHECK(slot.x == x);
		CHECK(slot.w >= 2 && slot.w <= 3);
		CHECK(AS_bar(&plot, 30, i, 60, 3600).w >= 1);
		x += slot.w;
	}
	CHECK(x == plot.x + plot.w);
	MosaicRect none = { 0, 0, 0, 0 };
	CHECK(AS_bar(&none, 7, 0, 60, 3600).w == 0);
}

static void testTabs(void) {
	CHECK_STR(AS_tabLabel(AS_TAB_WEEK), "7 days");
	CHECK_STR(AS_tabLabel(AS_TAB_MONTH), "30 days");
	CHECK_STR(AS_tabLabel(AS_TAB_ALL), "All time");
	CHECK(!AS_isList(AS_TAB_ALL));
	CHECK(AS_tabStep(AS_TAB_MONTH, 1) == AS_TAB_ALL && AS_tabStep(AS_TAB_ALL, 1) == AS_TAB_GAMES);
	CHECK_STR(AS_tabLabel(AS_TAB_GAMES), "Games");
	CHECK_STR(AS_tabLabel(AS_TAB_LAUNCHES), "Launches");
	CHECK_STR(AS_tabLabel(AS_TABS), "");
	CHECK(!AS_isList(AS_TAB_WEEK) && !AS_isList(AS_TAB_MONTH));
	CHECK(AS_isList(AS_TAB_GAMES) && AS_isList(AS_TAB_LAUNCHES));
	CHECK(AS_tabStep(AS_TAB_WEEK, 1) == AS_TAB_MONTH);
	CHECK(AS_tabStep(AS_TAB_LAUNCHES, 1) == AS_TAB_WEEK);
	CHECK(AS_tabStep(AS_TAB_WEEK, -1) == AS_TAB_LAUNCHES);

	CHECK(AS_step(0, 5, 1, 1) == 1);
	CHECK(AS_step(4, 5, 1, 1) == 0);
	CHECK(AS_step(4, 5, 1, 0) == 4);
	CHECK(AS_step(0, 5, -1, 1) == 4);
	CHECK(AS_step(0, 5, -1, 0) == 0);
	CHECK(AS_step(9, 5, -1, 0) == 3);
	CHECK(AS_step(0, 0, 1, 1) == 0);
	CHECK(AS_step(0, 1, 1, 1) == 0);

	CHECK(AS_window(0, 10, 4, 0) == 0);
	CHECK(AS_window(3, 10, 4, 0) == 0);
	CHECK(AS_window(4, 10, 4, 0) == 1);
	CHECK(AS_window(2, 10, 4, 1) == 1);
	CHECK(AS_window(0, 10, 4, 5) == 0);
	CHECK(AS_window(9, 10, 4, 0) == 6);
	CHECK(AS_window(2, 3, 4, 2) == 0);
	CHECK(AS_window(0, 0, 4, 2) == 0);

	MosaicHint left[2], right[2];
	int lc = -1, rc = -1;
	AS_hints(AS_HINT_OPEN, left, &lc, right, &rc);
	CHECK(lc == 1 && left[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(left[0].label, "BACK");
	CHECK(rc == 1 && right[0].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "PLAY");
	AS_hints(0, left, &lc, right, &rc);
	CHECK(lc == 1 && rc == 0);
	AS_hints(AS_HINT_OPEN | AS_HINT_EXPORT | AS_HINT_RESET, left, &lc, right, &rc);
	CHECK(lc == 2 && left[1].button == MOSAIC_BUTTON_Y);
	CHECK_STR(left[1].label, "EXPORT");
	CHECK(rc == 2 && right[0].button == MOSAIC_BUTTON_X && right[1].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "RESET");
	AS_hints(AS_HINT_EXPORT | AS_HINT_RESET_ALL, left, &lc, right, &rc);
	CHECK(lc == 2 && rc == 1);
	CHECK_STR(right[0].label, "RESET ALL");
}

static void testLayout(void) {
	MosaicRect area = { 0, 0, 640, 480 };
	MosaicRect status = { 500, 10, 130, 30 }, header = { 440, 10, 50, 30 }, none = { 0, 0, 0, 0 };
	CHECK(rectIs(AS_hole(&status, &header), 440, 10, 190, 30));
	CHECK(rectIs(AS_hole(&status, &none), 500, 10, 130, 30));
	CHECK(rectIs(AS_hole(NULL, &header), 440, 10, 50, 30));
	CHECK(AS_hole(NULL, NULL).w == 0);

	MosaicRect parts[4];
	MosaicRect hole = AS_hole(&status, &header);
	CHECK(AS_avoid(&area, &hole, parts) == 4);
	CHECK(rectIs(parts[0], 0, 0, 640, 10));
	CHECK(rectIs(parts[1], 0, 10, 440, 30));
	CHECK(rectIs(parts[2], 630, 10, 10, 30));
	CHECK(rectIs(parts[3], 0, 40, 640, 440));
	CHECK(AS_avoid(&area, &none, parts) == 1 && rectIs(parts[0], 0, 0, 640, 480));
	CHECK(AS_avoid(&none, &hole, parts) == 0);

	CHECK(rectIs(AS_tabRoom(&area, &hole, 1), 10, 10, 420, 30));
	CHECK(rectIs(AS_tabRoom(&area, &none, 1), 10, 10, 620, 30));
	CHECK(rectIs(AS_tabRoom(&area, &none, 2), 20, 20, 600, 60));
	MosaicRect wide = { 0, 0, 640, 480 }, full = { 15, 0, 620, 40 };
	CHECK(AS_tabRoom(&wide, &full, 1).w == 0);
	CHECK(AS_tabRoom(&area, &hole, 0).w == 0);

	int widths[4] = { 100, 100, 100, 100 };
	CHECK(AS_tabsFit(widths, 4, 20, 420));
	CHECK(!AS_tabsFit(widths, 4, 21, 420));

	CHECK(rectIs(AS_room(&area, &hole, 1), 10, 50, 620, 380));
	MosaicRect tall = { 400, 10, 200, 80 };
	CHECK(rectIs(AS_room(&area, &tall, 1), 10, 100, 620, 330));
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(AS_room(&tiny, &none, 1).w == 0);

	MosaicRect room = AS_room(&area, &hole, 1);
	// caption 12 and a PADDING; axis 20 and half a PADDING; labels 8 and half a PADDING
	CHECK(rectIs(AS_plot(&room, 1, 12, 20, 8), 35, 72, 595, 345));
	CHECK(AS_plot(&room, 1, 400, 20, 8).w == 0);
	CHECK(AS_plot(&room, 0, 12, 20, 8).w == 0);
}

static time_t localTimeOf(int hour, int minute) {
	struct tm tm = { .tm_year = 2026 - 1900, .tm_mon = 10, .tm_mday = 18, .tm_hour = hour, .tm_min = minute,
		.tm_isdst = -1 };
	return mktime(&tm);
}

static void testAllTime(void) {
	static const ASGame games[] = {
		{ "/c/Roms/GBA (MGBA)/Zelda.gba", "Zelda", 8100, 900, 2 },
		{ "/c/Roms/GB (GB)/Tetris.gb", "Tetris", 1600, 800, 2 },
		{ "", "Metroid", 50, 50, 1 },
		{ "/c/Roms/GB (GB)/Never.gb", "Never", 0, 0, 0 },
	};
	ASAllTime all;
	AS_allTimeStart(&all, games, 4);
	CHECK(all.seconds == 9750 && all.sessions == 5 && all.games == 3);
	CHECK(all.longest == 0 && AS_busiestHour(all.hours) == -1);
	char out[256];
	AS_longestCaption(&all, out, sizeof(out));
	CHECK_STR(out, "");
	AS_hourCaption(all.hours, out, sizeof(out));
	CHECK_STR(out, "No play by hour yet");

	// 20:30 for 2h: 30m at 20, 1h at 21, 30m at 22
	AS_allTimeAdd(&all, localTimeOf(20, 30), 7200, "Zelda");
	CHECK(all.hours[20] == 1800 && all.hours[21] == 3600 && all.hours[22] == 1800 && all.hours[19] == 0);
	AS_allTimeAdd(&all, localTimeOf(9, 0), 7200, "Tetris"); // as long: the first stays
	AS_allTimeAdd(&all, localTimeOf(23, 50), 1200, "Metroid"); // over midnight
	CHECK(all.hours[23] == 600 && all.hours[0] == 600);
	AS_allTimeAdd(&all, localTimeOf(1, 0), 0, "None");
	AS_allTimeAdd(&all, localTimeOf(1, 0), -5, "None");
	CHECK(all.hours[1] == 0);
	CHECK(all.longest == 7200);
	CHECK_STR(all.longest_name, "Zelda");
	CHECK(AS_busiestHour(all.hours) == 9); // 9 and 10 have 3600 as 21 does: the first
	AS_hourCaption(all.hours, out, sizeof(out));
	CHECK_STR(out, "Most active hour: 09:00");
	AS_allTimeCaption(&all, out, sizeof(out));
	CHECK_STR(out, "2h 42m \xc2\xb7 5 sessions \xc2\xb7 3 games");
	AS_longestCaption(&all, out, sizeof(out));
	CHECK_STR(out, "Longest session: 2h (Zelda)");

	// whole days go to every hour
	long long hours[AS_HOURS] = { 0 };
	AS_addHours(hours, localTimeOf(6, 0), 2 * 86400 + 600);
	CHECK(hours[6] == 7200 + 600 && hours[5] == 7200 && hours[7] == 7200);

	// systems
	char folder[64];
	AS_systemFolder("/c/Roms", "/c/Roms/GBA (MGBA)/Zelda.gba", folder, sizeof(folder));
	CHECK_STR(folder, "GBA (MGBA)");
	AS_systemFolder("/c/Roms", "/c/Roms/GBA (MGBA)/Sub/Zelda.gba", folder, sizeof(folder));
	CHECK_STR(folder, "GBA (MGBA)");
	AS_systemFolder("/c/Roms", "/c/Roms/Zelda.gba", folder, sizeof(folder));
	CHECK_STR(folder, "");
	AS_systemFolder("/c/Roms", "/c/Romsx/GB/Zelda.gba", folder, sizeof(folder));
	CHECK_STR(folder, "");
	AS_systemFolder("/c/Roms", "", folder, sizeof(folder));
	CHECK_STR(folder, "");
	AS_systemLabel("GBA (MGBA)", folder, sizeof(folder));
	CHECK_STR(folder, "GBA");
	AS_systemLabel("Game Boy", folder, sizeof(folder));
	CHECK_STR(folder, "Game Boy");
	AS_systemLabel("(GB)", folder, sizeof(folder));
	CHECK_STR(folder, "(GB)");
	AS_systemLabel("", folder, sizeof(folder));
	CHECK_STR(folder, "Other");

	static const ASGame spread[] = {
		{ "/c/Roms/GB (GB)/Tetris.gb", "Tetris", 1600, 800, 2 },
		{ "/c/Roms/GBA (MGBA)/Zelda.gba", "Zelda", 8100, 900, 2 },
		{ "/c/Roms/GB (GB2)/Kirby.gb", "Kirby", 500, 800, 1 },
		{ "", "Metroid", 50, 50, 1 },
		{ "/elsewhere/Doom.wad", "Doom", 50, 50, 1 },
	};
	ASSystem systems[6];
	CHECK(AS_systems(spread, 5, "/c/Roms", systems) == 3);
	CHECK_STR(systems[0].label, "GBA");
	CHECK(systems[0].seconds == 8100 && systems[0].games == 1);
	CHECK_STR(systems[1].label, "GB");
	CHECK(systems[1].seconds == 2100 && systems[1].games == 2);
	CHECK_STR(systems[2].label, "Other");
	CHECK(systems[2].seconds == 100 && systems[2].games == 2);
	CHECK(AS_systems(spread, 0, "/c/Roms", systems) == 0);
}

static void testTop(void) {
	ASGame games[40];
	unsigned char keep[40];
	int order[40], top[AS_TOP];
	memset(games, 0, sizeof(games));
	for (int i = 0; i < 40; i++) {
		snprintf(games[i].path, sizeof(games[i].path), "/c/Roms/GB/%02i.gb", i);
		games[i].seconds = 100 * (i % 20); // 0 to 1900, twice
		games[i].last_played = i;
		games[i].count = 1;
		keep[i] = i % 7 != 0; // every 7th hidden
	}
	int n = AS_pickTop(games, 40, keep, AS_TOP, order, top);
	CHECK(n == AS_TOP);
	// most played first, ties to the latest played, none left out kept
	CHECK(top[0] == 39 && top[1] == 19 && top[2] == 38 && top[3] == 18);
	for (int i = 0; i < n; i++) CHECK(keep[top[i]]);
	for (int i = 1; i < n; i++) CHECK(games[top[i - 1]].seconds >= games[top[i]].seconds);
	CHECK(AS_pickTop(games, 3, keep, AS_TOP, order, top) == 2 && top[0] == 2 && top[1] == 1);
	CHECK(AS_pickTop(games, 0, keep, AS_TOP, order, top) == 0);
	CHECK(AS_pickTop(games, 40, keep, 0, order, top) == 0);

	const char* paths[] = { "/c/b", "/c/a", "/c/c" };
	AS_sortPaths(paths, 3);
	CHECK_STR(paths[0], "/c/a");
	CHECK(AS_pathIn(paths, 3, "/c/b") && AS_pathIn(paths, 3, "/c/c") && !AS_pathIn(paths, 3, "/c/d"));
	CHECK(!AS_pathIn(paths, 0, "/c/a") && !AS_pathIn(paths, 3, NULL));

	ASGame orphan = { "", "Metroid", 50, 50, 1 };
	CHECK(AS_resetPath(&orphan) == NULL);
	CHECK(AS_resetPath(NULL) == NULL);
	CHECK_STR(AS_resetPath(&games[3]), "/c/Roms/GB/03.gb");
	char out[512];
	AS_resetQuestion("Zelda", out, sizeof(out));
	CHECK_STR(out, "Reset the play time of Zelda? Its sessions are deleted from the play history, so the Game Tracker loses them too. This can't be undone.");
	AS_resetAllQuestion(out, sizeof(out));
	CHECK(strstr(out, "Every session is deleted") && strstr(out, "Game Tracker loses them too") && strstr(out, "can't be undone"));
}

static char* slurpFile(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	static char data[65536];
	size_t n = fread(data, 1, sizeof(data) - 1, f);
	data[n] = '\0';
	fclose(f);
	return data;
}

static void testReport(void) {
	CHECK(system("mkdir -p '" CARD "'") == 0);
	const char* path = CARD "/escape.txt";
	FILE* f = fopen(path, "w");
	AR_csvField(f, "Plain");
	fputc('|', f);
	AR_csvField(f, "Pok\xc3\xa9mon, Red");
	fputc('|', f);
	AR_csvField(f, "Say \"hi\"");
	fputc('|', f);
	AR_csvField(f, "two\nlines");
	fputc('|', f);
	AR_csvField(f, " padded");
	fputc('|', f);
	AR_csvField(f, "");
	fputc('|', f);
	AR_csvField(f, NULL);
	fputc('|', f);
	AR_html(f, "Tom & Jerry <\"Cat's\"> & co");
	fclose(f);
	CHECK_STR(slurpFile(path), "Plain|\"Pok\xc3\xa9mon, Red\"|\"Say \"\"hi\"\"\"|\"two\nlines\"|\" padded\"|||"
		"Tom &amp; Jerry &lt;&quot;Cat&#39;s&quot;&gt; &amp; co");

	f = fopen(path, "w");
	AR_csvHeader(f);
	AR_csvRow(f, localTimeOf(20, 30), 7230, "Tetris, \"DX\"", "GB", "/c/Roms/GB (GB)/Tetris, DX.gb");
	fclose(f);
	char want[256];
	snprintf(want, sizeof(want), "started,start_unix,seconds,minutes,game,system,path\r\n"
		"2026-11-18 20:30:00,%lld,7230,121,\"Tetris, \"\"DX\"\"\",GB,\"/c/Roms/GB (GB)/Tetris, DX.gb\"\r\n",
		(long long)localTimeOf(20, 30));
	CHECK_STR(slurpFile(path), want);

	char stamp[32];
	AR_stamp(localTimeOf(9, 5) + 7, stamp, sizeof(stamp));
	CHECK_STR(stamp, "2026-11-18-090507");

	ASGame games[] = {
		{ "/c/Roms/GB (GB)/Tetris.gb", "Tetris & <Friends>", 1600, 800, 2 },
		{ "/c/Roms/GBA (MGBA)/Zelda.gba", "Zelda", 8100, 900, 2 },
	};
	int order[] = { 1, 0 };
	ASAllTime all;
	AS_allTimeStart(&all, games, 2);
	AS_allTimeAdd(&all, localTimeOf(21, 0), 3600, "Zelda");
	ASSystem systems[3];
	int system_count = AS_systems(games, 2, "/c/Roms", systems);
	long long days[3] = { 600, -1, 0 };
	ARSummary summary = { localTimeOf(12, 0), &all, systems, system_count, games, order, 2, days, 3 };
	f = fopen(path, "w");
	AR_htmlWrite(f, &summary);
	fclose(f);
	const char* html = slurpFile(path);
	CHECK(strstr(html, "<!DOCTYPE html>") == html);
	CHECK(strstr(html, "<title>Activity</title>") != NULL);
	CHECK(strstr(html, "2h 41m \xc2\xb7 4 sessions \xc2\xb7 2 games") != NULL);
	CHECK(strstr(html, "Longest session: 1h (Zelda)") != NULL);
	CHECK(strstr(html, "Most active hour: 21:00") != NULL);
	CHECK(strstr(html, "<tr><td>2026-11-18</td><td>10m</td></tr>") != NULL);
	CHECK(strstr(html, "<tr><td>2026-11-17</td><td></td></tr>") != NULL);
	CHECK(strstr(html, "<tr><td>21:00</td><td>1h</td></tr>") != NULL);
	CHECK(strstr(html, "<tr><td>20:00</td><td>0m</td></tr>") != NULL);
	CHECK(strstr(html, "<tr><td>GBA</td><td>2h 15m</td></tr>") != NULL);
	// most played first, names escaped
	const char* zelda = strstr(html, "<tr><td>Zelda</td><td>2h 15m, 2 sessions</td></tr>");
	const char* tetris = strstr(html, "<tr><td>Tetris &amp; &lt;Friends&gt;</td><td>26m, 2 sessions</td></tr>");
	CHECK(zelda && tetris && zelda < tetris);
	CHECK(strstr(html, "<Friends>") == NULL);
	CHECK(strstr(html, "</html>\n") != NULL);
	unlink(path);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
};

static MosaicSurface screen_surface = { 1024, 768 };
static const MosaicRect AREA = { 0, 0, 1024, 768 };
static const MosaicRect STATUS = { 800, 10, 214, 30 };

static MosaicHost fake_host;
static MosaicScreenHooks registered;
static char registered_id[64];
static char action_id[64], action_label[64], tile_id[64];
static int (*action_available)(const char*, void*);
static void (*action_run)(const char*, void*);
static int bound_button = -1;
static char last_log[1200];
static char opened_screen[64];
static char opened_path[600];
static int opened_type = -1, open_answer = 1;
static int redraws = 0;
static MosaicHint hints_right[2], hints_left[2];
static int hints_right_count = 0, hints_left_count = 0;
static char toast_text[300];
static const char* tile_setting = "on";
static const char* button_setting = "none";
static const char* folder_setting = "off";
static int fills_into_hole = 0, texts_into_hole = 0;

typedef struct {
	const char* path;
	const char* name;
	long long seconds, first, last;
	int count, orphan;
} Fixture;
static const Fixture* fixture = NULL;
static int fixture_count = -1;
static long long days_played[AS_DAYS + 1];

static int meetsHole(const MosaicRect* r) {
	return r->x < STATUS.x + STATUS.w && STATUS.x < r->x + r->w && r->y < STATUS.y + STATUS.h && STATUS.y < r->y + r->h;
}

static void fakeLog(const char* id, const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "tile")) return tile_setting;
	if (!strcmp(key, "button")) return button_setting;
	if (!strcmp(key, "folder")) return folder_setting;
	return fallback;
}

static void fakeRedraw(void) {
	redraws++;
}

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(registered_id, sizeof(registered_id), "%s", id);
	registered = *hooks;
	return 0;
}

// every action registered, by id
typedef struct {
	char id[64], label[64];
	int flags;
	int (*available)(const char*, void*);
	void (*run)(const char*, void*);
} FakeAction;
static FakeAction actions[8];
static int action_count = 0;

static const FakeAction* actionOf(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) return &actions[i];
	}
	return NULL;
}

static int fakeAction(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)data;
	if (!actionOf(id) && action_count < 8) {
		FakeAction* a = &actions[action_count++];
		snprintf(a->id, sizeof(a->id), "%s", id);
		snprintf(a->label, sizeof(a->label), "%s", label);
		a->flags = flags;
		a->available = available;
		a->run = run;
	}
	if (!strcmp(id, "activity-stats.open")) {
		snprintf(action_id, sizeof(action_id), "%s", id);
		snprintf(action_label, sizeof(action_label), "%s", label);
		action_available = available;
		action_run = run;
	}
	return 0;
}

// run_action as the launcher runs it: the gate (a PIN always typed right
// here) is only counted
static char ran_action[64], ran_path[600];
static int gated = 0;
static int fakeRunAction(const char* id, const char* path) {
	snprintf(ran_action, sizeof(ran_action), "%s", id);
	snprintf(ran_path, sizeof(ran_path), "%s", path ? path : "");
	const FakeAction* a = actionOf(id);
	if (!a) return -1;
	const char* given = a->flags & MOSAIC_ACTION_PATH ? path : NULL;
	if (a->available && !a->available(given, NULL)) return -1;
	if (a->flags & MOSAIC_ACTION_EDITS) gated++;
	a->run(given, NULL);
	return 0;
}

// confirm: the question, and A pressed on it when confirm_yes
static char confirm_text[1024], confirm_label[64];
static int confirm_yes = 1;
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	snprintf(confirm_text, sizeof(confirm_text), "%s", message);
	snprintf(confirm_label, sizeof(confirm_label), "%s", yes);
	if (confirm_yes) on_yes(data);
}

static int fakeTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)row;
	(void)icon;
	(void)icon_off;
	(void)is_on;
	snprintf(tile_id, sizeof(tile_id), "%s", id);
	return 0;
}

static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen;
	(void)trigger;
	bound_button = button;
	return 0;
}

static int fakeOpenScreen(const char* id) {
	snprintf(opened_screen, sizeof(opened_screen), "%s", id);
	return 0;
}

static int fakeOpenPath(const char* path, int type) {
	snprintf(opened_path, sizeof(opened_path), "%s", path);
	opened_type = type;
	return open_answer;
}

static void fakeToast(const char* message) {
	snprintf(toast_text, sizeof(toast_text), "%s", message);
}

static int fakeThemeColor(int index) {
	return index == MOSAIC_COLOR_BACKGROUND ? 0x000000 : 0xFFFFFF;
}

static int fakeScale(void) {
	return 3;
}

static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)target;
	(void)color;
	if (meetsHole(rect)) fills_into_hole++;
	return 0;
}

static int fakeFillRound(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	(void)radius;
	return fakeFill(target, rect, color);
}

static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return 20 * (int)strlen(text);
}

static int fakeTextHeight(int font) {
	return font == MOSAIC_FONT_LARGE ? 48 : font == MOSAIC_FONT_SMALL ? 36 : 24;
}

static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	snprintf(out, size, "%s", text);
	int w = fakeTextWidth(font, out);
	return w > max_width ? max_width : w;
}

static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)target;
	(void)color;
	int w = fakeTextWidth(font, text);
	if (w > max_width) w = max_width;
	MosaicRect r = { x, y, w, fakeTextHeight(font) };
	if (w > 0 && meetsHole(&r)) texts_into_hole++;
	return w;
}

static int fakeDrawHints(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	(void)screen;
	(void)drawn;
	if (group == MOSAIC_HINTS_RIGHT) {
		hints_right_count = count;
		for (int i = 0; i < count && i < 2; i++) hints_right[i] = hints[i];
	} else {
		hints_left_count = count;
		for (int i = 0; i < count && i < 2; i++) hints_left[i] = hints[i];
	}
	return 0;
}

static int fakeGamesCount(void) {
	return fixture_count;
}

static int fakeGameInfo(int index, MosaicPlayGame* out) {
	if (!out || index < 0 || index >= fixture_count) return -1;
	const Fixture* f = &fixture[index];
	snprintf(out->path, sizeof(out->path), "%s", f->path);
	snprintf(out->name, sizeof(out->name), "%s", f->name);
	out->seconds = f->seconds;
	out->first_played = f->first;
	out->last_played = f->last;
	out->count = f->count;
	out->orphan = f->orphan;
	return 0;
}

static long long fakeTimeOn(int days_ago) {
	if (fixture_count < 0 || days_ago < 0) return MOSAIC_PLAY_UNKNOWN;
	return days_ago <= AS_DAYS ? days_played[days_ago] : MOSAIC_PLAY_UNKNOWN;
}

static const char* filtered_path = "";
static int filtered_asks = 0;
static int fakeFiltered(const char* path) {
	filtered_asks++;
	return filtered_path[0] && !strcmp(path, filtered_path);
}

typedef struct {
	const char* path; // "" for an orphan
	const char* name;
	long long start, seconds;
} SessionFixture;
static const SessionFixture* sessions_fixture = NULL;
static int sessions_count = 0;

static int fakeSessionsCount(void) {
	return fixture_count < 0 ? MOSAIC_PLAY_UNKNOWN : sessions_count;
}

static int fakeSessionInfo(int index, MosaicPlaySession* out) {
	if (!out || index < 0 || index >= sessions_count) return -1;
	const SessionFixture* f = &sessions_fixture[index];
	snprintf(out->path, sizeof(out->path), "%s", f->path);
	snprintf(out->name, sizeof(out->name), "%s", f->name);
	out->start = f->start;
	out->seconds = f->seconds;
	out->orphan = !f->path[0];
	return 0;
}

static char forgot[600];
static int forget_calls = 0, forget_answer = 3;
static int fakeForget(const char* path) {
	forget_calls++;
	snprintf(forgot, sizeof(forgot), "%s", path ? path : "(everything)");
	return forget_answer;
}

static int fakePlayCount(const char* path) {
	for (int i = 0; i < fixture_count; i++) {
		if (!fixture[i].orphan && !strcmp(fixture[i].path, path)) return fixture[i].count;
	}
	return fixture_count < 0 ? MOSAIC_PLAY_UNKNOWN : 0;
}

// the set and the lists
static int (*set_has)(const char*, const char*);
static void (*set_list)(const char*, void (*)(const char*, void*), void*);
static char set_name[64];
static int fakeSetRegister(const char* name, int (*has)(const char*, const char*),
	void (*list)(const char*, void (*)(const char*, void*), void*)) {
	snprintf(set_name, sizeof(set_name), "%s", name);
	set_has = has;
	set_list = list;
	return 0;
}

static int sets_changed = 0, reloads = 0;
static int fakeSetChanged(const char* name) {
	(void)name;
	sets_changed++;
	return 0;
}

static int fakeSetNames(void (*each)(const char*, void*), void* data) {
	(void)each;
	(void)data;
	return 0;
}

static void (*on_set_changed)(const char*, const char*, void*);
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	if (!strcmp(name, "launcher.set_changed")) on_set_changed = on_event;
	return 0;
}

static int fakeEventValue(const char* payload, const char* key, char* out, size_t size) {
	size_t n = strlen(key);
	if (strncmp(payload, key, n) || payload[n] != '=') return -1;
	snprintf(out, size, "%s", payload + n + 1);
	return (int)strlen(payload + n + 1);
}

static void fakeReload(void) {
	reloads++;
}

struct MosaicList {
	char paths[AS_TOP + 2][600];
	int types[AS_TOP + 2];
	char name[64];
	int count;
};
struct MosaicEntry {
	char path[600];
	char name[64];
	int type;
};
static MosaicList lists[2];
static int lists_made = 0;
static MosaicEntry entries[AS_TOP + 2];
static int entries_made = 0;

static MosaicList* fakeListNew(void) {
	MosaicList* list = &lists[lists_made++ % 2];
	memset(list, 0, sizeof(*list));
	return list;
}

static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = &entries[entries_made++ % (AS_TOP + 2)];
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
	e->type = type;
	return e;
}

static void fakeListPush(MosaicList* list, MosaicEntry* e) {
	if (list->count >= AS_TOP + 2) return;
	snprintf(list->paths[list->count], sizeof(list->paths[0]), "%s", e->path);
	snprintf(list->name, sizeof(list->name), "%s", e->name);
	list->types[list->count++] = e->type;
}

static void setupHost(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.sdcard_path = CARD;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.request_redraw = fakeRedraw;
	fake_host.register_action = fakeAction;
	fake_host.bind_button = fakeBind;
	fake_host.toast = fakeToast;
	fake_host.quick_tile = fakeTile;
	fake_host.open_path = fakeOpenPath;
	fake_host.register_screen = fakeRegister;
	fake_host.open_screen = fakeOpenScreen;
	fake_host.theme_color = fakeThemeColor;
	fake_host.screen_scale = fakeScale;
	fake_host.fill_rect = fakeFill;
	fake_host.fill_round_rect = fakeFillRound;
	fake_host.text_width = fakeTextWidth;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.draw_hints = fakeDrawHints;
	fake_host.play_games_count = fakeGamesCount;
	fake_host.play_game_info = fakeGameInfo;
	fake_host.play_time_on = fakeTimeOn;
	fake_host.is_filtered = fakeFiltered;
	fake_host.play_count = fakePlayCount;
	fake_host.run_action = fakeRunAction;
	fake_host.confirm = fakeConfirm;
	fake_host.set_register = fakeSetRegister;
	fake_host.set_changed = fakeSetChanged;
	fake_host.set_names = fakeSetNames;
	fake_host.subscribe = fakeSubscribe;
	fake_host.event_value = fakeEventValue;
	fake_host.reload_lists = fakeReload;
	fake_host.list_new = fakeListNew;
	fake_host.entry_new = fakeEntryNew;
	fake_host.list_push = fakeListPush;
	fake_host.play_sessions_count = fakeSessionsCount;
	fake_host.play_session_info = fakeSessionInfo;
	fake_host.play_forget = fakeForget;
}

static const MosaicPlugin* start(void) {
	setupHost();
	action_count = 0;
	tile_id[0] = '\0';
	bound_button = -1;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	return p;
}

static void draw(void) {
	MosaicScreenView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.area = AREA;
	v.status = STATUS;
	v.screen = SCREEN_ID;
	hints_right_count = 0;
	hints_left_count = 0;
	hints_left[1].button = -1;
	CHECK(registered.render(&screen_surface, &v, registered.data) == 0);
}

static int press(int button) {
	MosaicInput in = { sizeof(MosaicInput), MOSAIC_BUTTON_BIT(button), MOSAIC_BUTTON_BIT(button), 0,
		MOSAIC_BUTTON_BIT(button) };
	return registered.input(&in, registered.data);
}

static void testInit(void) {
	start();
	CHECK_STR(registered_id, "activity-stats.main");
	CHECK(registered.flags == 0);
	CHECK(registered.enter && registered.exit && registered.input && registered.render);
	CHECK_STR(action_id, "activity-stats.open");
	CHECK_STR(action_label, "Activity");
	CHECK_STR(tile_id, "activity-stats.open");
	CHECK(bound_button == -1);
	CHECK(action_available(NULL, NULL) == 1);
	action_run(NULL, NULL);
	CHECK_STR(opened_screen, "activity-stats.main");

	tile_setting = "off";
	button_setting = "SELECT";
	start();
	CHECK_STR(tile_id, "");
	CHECK(bound_button == MOSAIC_BUTTON_SELECT);
	tile_setting = "on";
	button_setting = "none";

	// a launcher without the whole play history
	setupHost();
	fake_host.size = offsetof(MosaicHost, play_games_count);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) != 0);
}

static const Fixture history[] = {
	{ ROMS "/Zelda.gba", "Zelda", 8100, 100, 900, 2, 0 },
	{ ROMS "/Tetris.gb", "Tetris", 1600, 200, 800, 2, 0 },
	{ ROMS "/Puzzle.gb", "", 300, 300, 700, 5, 0 },
	{ "", "Metroid", 50, 50, 50, 1, 1 },
	{ ROMS "/Gone.gb", "Gone", 40, 40, 40, 1, 0 },
};

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" ROMS "'") == 0);
	const char* files[] = { ROMS "/Zelda.gba", ROMS "/Tetris.gb", ROMS "/Puzzle.gb" };
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		FILE* f = fopen(files[i], "w");
		CHECK(f != NULL);
		if (f) fclose(f);
	}
}

static void testScreen(void) {
	makeCard();
	start();
	fixture = history;
	fixture_count = 5;
	memset(days_played, 0, sizeof(days_played));
	days_played[0] = 900;
	days_played[1] = 1600;
	days_played[2] = 7200;
	days_played[3] = 50;
	days_played[4] = 300;
	days_played[20] = 3600;
	days_played[AS_DAYS] = 99999; // past the month: never asked for
	tab = AS_TAB_WEEK;
	CHECK(registered.enter(NULL) == 0);
	CHECK_STR(last_log, "history: 5 games, 11 sessions, 10090 seconds");
	fills_into_hole = texts_into_hole = 0;
	draw();
	CHECK_STR(last_log, "showing 7 days: days 900 1600 7200 50 300 0 0, axis 7200, 7 days: 2h 47m \xc2\xb7 23m a day");
	CHECK(hints_left_count == 2 && hints_right_count == 0);

	redraws = 0;
	CHECK(press(MOSAIC_BUTTON_R1) == MOSAIC_SCREEN_STAY);
	CHECK(redraws == 1);
	draw();
	CHECK(strstr(last_log, "showing 30 days: days 900 1600 7200 50 300 0 ") == last_log);
	CHECK(strstr(last_log, " 3600 0 0 0 0 0 0 0 0 0, axis 7200, 30 days: 3h 47m \xc2\xb7 7m a day") != NULL);

	press(MOSAIC_BUTTON_RIGHT);
	draw();
	CHECK(strstr(last_log, "showing All time: ") == last_log);
	press(MOSAIC_BUTTON_RIGHT);
	draw();
	CHECK_STR(last_log, "showing Games: 0 of 5: Zelda, 2h 15m \xc2\xb7 2 sessions, can open");
	CHECK(hints_right_count == 2 && hints_right[0].button == MOSAIC_BUTTON_X && hints_right[1].button == MOSAIC_BUTTON_A);
	CHECK(hints_left_count == 2 && hints_left[1].button == MOSAIC_BUTTON_Y);
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 1 of 5: Tetris, 26m \xc2\xb7 2 sessions, can open");
	opened_path[0] = '\0';
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK_STR(opened_path, ROMS "/Tetris.gb");
	CHECK(opened_type == MOSAIC_ENTRY_ROM);
	CHECK_STR(last_log, "open " ROMS "/Tetris.gb 1");

	// the orphan and the game no longer on the card can't be opened
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 3 of 5: Metroid, <1m \xc2\xb7 1 session, can't open");
	// an orphan can't be reset on its own either
	CHECK(hints_right_count == 0);
	opened_path[0] = '\0';
	press(MOSAIC_BUTTON_A);
	CHECK_STR(opened_path, "");
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 4 of 5: Gone, <1m \xc2\xb7 1 session, can't open");
	// down from the last wraps on a press
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 0 of 5: Zelda, 2h 15m \xc2\xb7 2 sessions, can open");

	// a game a filter hides is left out of the lists and their totals
	registered.exit(NULL);
	filtered_path = ROMS "/Tetris.gb";
	CHECK(registered.enter(NULL) == 0);
	CHECK(game_count == 4);
	for (int i = 0; i < game_count; i++) CHECK(strcmp(games[i].path, ROMS "/Tetris.gb") != 0);
	CHECK(strstr(last_log, "history: 4 games, 9 sessions,") == last_log);
	registered.exit(NULL);
	filtered_path = "";
	CHECK(registered.enter(NULL) == 0);
	CHECK(game_count == 5);
	tab = AS_TAB_GAMES;
	selected[0] = 0;

	// a launch that doesn't go ahead says so
	open_answer = 0;
	toast_text[0] = '\0';
	press(MOSAIC_BUTTON_A);
	CHECK_STR(toast_text, "Couldn't open Zelda");
	open_answer = 1;

	press(MOSAIC_BUTTON_R1);
	draw();
	CHECK_STR(last_log, "showing Launches: 0 of 5: Puzzle, 5 sessions \xc2\xb7 5m, can open");
	press(MOSAIC_BUTTON_UP);
	draw();
	CHECK_STR(last_log, "showing Launches: 4 of 5: Gone, 1 session \xc2\xb7 <1m, can't open");
	// each list keeps its own selection
	press(MOSAIC_BUTTON_L1);
	draw();
	CHECK_STR(last_log, "showing Games: 0 of 5: Zelda, 2h 15m \xc2\xb7 2 sessions, can open");
	press(MOSAIC_BUTTON_LEFT);
	press(MOSAIC_BUTTON_LEFT);
	press(MOSAIC_BUTTON_LEFT);
	draw();
	CHECK(strstr(last_log, "showing 7 days: ") == last_log);
	// up and down do nothing on a chart
	redraws = 0;
	press(MOSAIC_BUTTON_DOWN);
	CHECK(redraws == 0);
	CHECK(fills_into_hole == 0);
	CHECK(texts_into_hole == 0);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(NULL);
	CHECK(games == NULL && game_count == -1);

	// nothing played yet
	fixture_count = 0;
	memset(days_played, 0, sizeof(days_played));
	tab = AS_TAB_GAMES;
	registered.enter(NULL);
	CHECK_STR(last_log, "history: 0 games, 0 sessions, 0 seconds");
	draw();
	CHECK_STR(last_log, "showing Games: nothing played yet");
	CHECK(hints_right_count == 0);
	press(MOSAIC_BUTTON_A);
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_L1);
	draw();
	CHECK_STR(last_log, "showing All time: 0m \xc2\xb7 0 sessions \xc2\xb7 0 games, no sessions, No play by hour yet, hours 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0");
	CHECK(hints_right_count == 0);
	press(MOSAIC_BUTTON_L1);
	press(MOSAIC_BUTTON_L1);
	draw();
	CHECK_STR(last_log, "showing 7 days: days 0 0 0 0 0 0 0, axis 3600, 7 days: 0m \xc2\xb7 0m a day");
	registered.exit(NULL);

	// no history: every tab says so
	fixture_count = -1;
	registered.enter(NULL);
	CHECK_STR(last_log, "no play history");
	for (int i = 0; i < AS_TABS; i++) {
		draw();
		char want[64];
		snprintf(want, sizeof(want), "showing %s: no play history", AS_tabLabel(tab));
		CHECK_STR(last_log, want);
		CHECK(hints_left_count == 1 && hints_right_count == 0);
		press(MOSAIC_BUTTON_R1);
	}
	// nothing to export or reset
	ran_action[0] = '\0';
	press(MOSAIC_BUTTON_Y);
	press(MOSAIC_BUTTON_X);
	CHECK_STR(ran_action, "");
	press(MOSAIC_BUTTON_A);
	registered.exit(NULL);
	mosaic_plugin_get(&fake_host)->quit();
}

// sessions for the history above: Tetris is filtered in some tests
static SessionFixture history_sessions[6];

static void makeSessions(void) {
	SessionFixture made[] = {
		{ "", "Metroid", localTimeOf(8, 0), 50 },
		{ ROMS "/Zelda.gba", "Zelda", localTimeOf(20, 30), 7200 },
		{ ROMS "/Tetris.gb", "Tetris", localTimeOf(9, 0), 800 },
		{ ROMS "/Tetris.gb", "Tetris", localTimeOf(9, 30), 800 },
		{ ROMS "/Zelda.gba", "Zelda", localTimeOf(22, 0), 900 },
		{ ROMS "/Puzzle.gb", "", localTimeOf(7, 0), 300 },
	};
	memcpy(history_sessions, made, sizeof(made));
	sessions_fixture = history_sessions;
	sessions_count = 6;
}

static void testAllTimeScreen(void) {
	makeCard();
	makeSessions();
	start();
	fixture = history;
	fixture_count = 5;
	tab = AS_TAB_ALL;
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(strstr(last_log, "showing All time: 2h 48m \xc2\xb7 11 sessions \xc2\xb7 5 games, Longest session: 2h (Zelda), "
		"Most active hour: 21:00, hours 0 0 0 0 0 0 0 300 50 1600 0 0 0 0 0 0 0 0 0 0 1800 3600 2700 0, "
		"systems Stats 2h 47m, Other <1m") == last_log);
	CHECK(hints_left_count == 2 && hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_X);
	CHECK_STR(hints_right[0].label, "RESET ALL");
	CHECK(fills_into_hole == 0 && texts_into_hole == 0);
	registered.exit(NULL);

	// a filtered game's sessions are left out too
	filtered_path = ROMS "/Tetris.gb";
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(strstr(last_log, "showing All time: 2h 21m \xc2\xb7 9 sessions \xc2\xb7 4 games, Longest session: 2h (Zelda), "
		"Most active hour: 21:00, hours 0 0 0 0 0 0 0 300 50 0 0 0") == last_log);
	registered.exit(NULL);
	filtered_path = "";

	// a launcher without sessions: the totals, but no hours, export or reset
	setupHost();
	fake_host.size = offsetof(MosaicHost, play_sessions_count);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 0);
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(strstr(last_log, "showing All time: 2h 48m \xc2\xb7 11 sessions \xc2\xb7 5 games, no sessions, No play by hour yet") == last_log);
	CHECK(hints_left_count == 1 && hints_right_count == 0);
	registered.exit(NULL);
	sessions_count = 0;
}

static void testExport(void) {
	makeCard();
	makeSessions();
	start();
	fixture = history;
	fixture_count = 5;
	filtered_path = ROMS "/Puzzle.gb";
	const FakeAction* a = actionOf("activity-stats.export");
	CHECK(a && a->flags == 0);
	CHECK_STR(a->label, "EXPORT");
	// from the screen, through run_action
	tab = AS_TAB_WEEK;
	CHECK(registered.enter(NULL) == 0);
	toast_text[0] = '\0';
	press(MOSAIC_BUTTON_Y);
	CHECK_STR(ran_action, "activity-stats.export");
	CHECK(strstr(toast_text, "Saved Reports/activity-") == toast_text);
	CHECK(strstr(toast_text, ".csv and .html") != NULL);
	char stamp[32];
	snprintf(stamp, sizeof(stamp), "%.17s", toast_text + strlen("Saved Reports/activity-"));
	char csv[700], html[700];
	snprintf(csv, sizeof(csv), CARD "/Reports/activity-%s.csv", stamp);
	snprintf(html, sizeof(html), CARD "/Reports/activity-%s.html", stamp);
	char want[1500];
	snprintf(want, sizeof(want), "exported %s and %s", csv, html);
	CHECK_STR(last_log, want);
	const char* text = slurpFile(csv);
	CHECK(text && strstr(text, "started,start_unix,seconds,minutes,game,system,path\r\n") == text);
	// a row per session, oldest first as the launcher lists them, but the filtered game's
	CHECK(text && strstr(text, ",50,1,Metroid,Other,\r\n") != NULL);
	CHECK(text && strstr(text, ",7200,120,Zelda,Stats," ROMS "/Zelda.gba\r\n") != NULL);
	CHECK(text && strstr(text, "Puzzle") == NULL);
	int rows = 0;
	for (const char* c = text; c && *c; c++) rows += *c == '\n';
	CHECK(rows == 6);
	text = slurpFile(html);
	CHECK(text && strstr(text, "<title>Activity</title>") != NULL);
	CHECK(text && strstr(text, "<tr><td>Puzzle</td>") == NULL);
	CHECK(text && strstr(text, "<tr><td>Zelda</td><td>2h 15m, 2 sessions</td></tr>") != NULL);
	struct stat st;
	char temp[720];
	snprintf(temp, sizeof(temp), "%s.tmp", csv);
	CHECK(stat(temp, &st) != 0);
	registered.exit(NULL);

	// from elsewhere, with the screen shut: the games read for it alone
	CHECK(game_count == -1);
	toast_text[0] = '\0';
	CHECK(fakeRunAction("activity-stats.export", NULL) == 0);
	CHECK(strstr(toast_text, "Saved Reports/activity-") == toast_text);
	CHECK(game_count == -1 && games == NULL);

	// a card that can't take it
	CHECK(system("rm -rf '" CARD "/Reports' && touch '" CARD "/Reports'") == 0);
	toast_text[0] = '\0';
	fakeRunAction("activity-stats.export", NULL);
	CHECK_STR(toast_text, "Couldn't save the activity report");
	CHECK(system("rm -f '" CARD "/Reports'") == 0);

	// no history: not available
	fixture_count = -1;
	CHECK(a->available(NULL, NULL) == 0);
	filtered_path = "";
	sessions_count = 0;
}

static void testReset(void) {
	makeCard();
	start();
	fixture = history;
	fixture_count = 5;
	const FakeAction* one = actionOf("activity-stats.reset");
	const FakeAction* all = actionOf("activity-stats.reset_all");
	CHECK(one && one->flags == (MOSAIC_ACTION_PATH | MOSAIC_ACTION_EDITS));
	CHECK(all && all->flags == MOSAIC_ACTION_EDITS);
	CHECK(one->available(ROMS "/Tetris.gb", NULL) == 1);
	CHECK(one->available(ROMS "/Never.gb", NULL) == 0);
	CHECK(one->available(NULL, NULL) == 0);
	CHECK(all->available(NULL, NULL) == 1);

	// X on the Games tab: the selected game, through the gate, after a confirm
	tab = AS_TAB_GAMES;
	selected[0] = 1;
	CHECK(registered.enter(NULL) == 0);
	draw();
	CHECK(strstr(last_log, "showing Games: 1 of 5: Tetris") == last_log);
	gated = forget_calls = sets_changed = 0;
	toast_text[0] = '\0';
	confirm_yes = 0;
	press(MOSAIC_BUTTON_X);
	CHECK_STR(ran_action, "activity-stats.reset");
	CHECK_STR(ran_path, ROMS "/Tetris.gb");
	CHECK(gated == 1);
	CHECK_STR(confirm_text, "Reset the play time of Tetris? Its sessions are deleted from the play history, so the Game Tracker loses them too. This can't be undone.");
	CHECK_STR(confirm_label, "RESET");
	CHECK(forget_calls == 0); // B on the confirm
	confirm_yes = 1;
	redraws = 0;
	press(MOSAIC_BUTTON_X);
	CHECK(forget_calls == 1);
	CHECK_STR(forgot, ROMS "/Tetris.gb");
	CHECK_STR(toast_text, "Play time reset");
	CHECK(redraws >= 1 && sets_changed == 1);
	CHECK(game_count == 5); // read again

	// an orphan has no reset of its own
	selected[0] = 3;
	ran_action[0] = '\0';
	press(MOSAIC_BUTTON_X);
	CHECK_STR(ran_action, "");

	// X on All time: everything
	tab = AS_TAB_ALL;
	makeSessions();
	registered.exit(NULL);
	registered.enter(NULL);
	press(MOSAIC_BUTTON_X);
	CHECK_STR(ran_action, "activity-stats.reset_all");
	CHECK_STR(forgot, "(everything)");
	CHECK(strstr(confirm_text, "Reset all play time? Every session is deleted") == confirm_text);
	CHECK(strstr(confirm_text, "the Game Tracker loses them too. This can't be undone.") != NULL);

	// locked or missing: it says so, and the history is read again
	forget_answer = -1;
	press(MOSAIC_BUTTON_X);
	CHECK_STR(toast_text, "Couldn't reset the play time. Try again after a moment.");
	CHECK_STR(last_log, "reset everything: -1");
	forget_answer = 3;
	registered.exit(NULL);

	// from the game list, on a game, with its tracker name
	CHECK(fakeRunAction("activity-stats.reset", ROMS "/Puzzle.gb") == 0);
	CHECK(strstr(confirm_text, "Reset the play time of Puzzle?") == confirm_text);
	CHECK_STR(forgot, ROMS "/Puzzle.gb");
	sessions_count = 0;

	// a launcher without play_forget registers no reset
	setupHost();
	action_count = 0;
	fake_host.size = offsetof(MosaicHost, play_sessions_count);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 0);
	CHECK(actionOf("activity-stats.reset") == NULL && actionOf("activity-stats.reset_all") == NULL);
}

static int set_listed = 0;
static void countListed(const char* path, void* data) {
	(void)path;
	(void)data;
	set_listed++;
}

static void testFolder(void) {
	makeCard();
	// 30 games on the card, most played last, and a hidden one, an orphan and one gone
	static Fixture many[34];
	static char paths[34][600];
	for (int i = 0; i < 30; i++) {
		snprintf(paths[i], sizeof(paths[i]), ROMS "/G%02i.gb", i);
		FILE* f = fopen(paths[i], "w");
		if (f) fclose(f);
		many[i] = (Fixture){ paths[i], "", 1000 + i, 0, i, 1, 0 };
	}
	many[30] = (Fixture){ ROMS "/Hidden.gb", "", 99999, 0, 0, 1, 0 };
	FILE* f = fopen(ROMS "/Hidden.gb", "w");
	if (f) fclose(f);
	many[31] = (Fixture){ "", "Metroid", 99998, 0, 0, 1, 1 };
	many[32] = (Fixture){ ROMS "/Gone.gb", "", 99997, 0, 0, 1, 0 };
	CHECK(system("mkdir -p '" ROMS "/Pak.pak'") == 0);
	many[33] = (Fixture){ ROMS "/Pak.pak", "", 5000, 0, 0, 1, 0 };
	// most played first, as the launcher lists them
	static Fixture listed[34];
	int order[34];
	for (int i = 0; i < 34; i++) order[i] = i;
	for (int i = 0; i < 34; i++) {
		for (int k = i + 1; k < 34; k++) {
			if (many[order[k]].seconds > many[order[i]].seconds) {
				int t = order[i];
				order[i] = order[k];
				order[k] = t;
			}
		}
	}
	for (int i = 0; i < 34; i++) listed[i] = many[order[i]];
	fixture = listed;
	fixture_count = 34;
	filtered_path = ROMS "/Hidden.gb";

	// off by default: no folder, but the set
	folder_setting = "off";
	const MosaicPlugin* p = start();
	CHECK_STR(set_name, "activity-stats.most-played");
	lists_made = 0;
	MosaicList root;
	memset(&root, 0, sizeof(root));
	p->root_entries(&root);
	CHECK(root.count == 0);
	CHECK(p->virtual_dir(CARD "/Most Played") == NULL);
	CHECK(set_has("activity-stats.most-played", ROMS "/G29.gb") == 1);
	CHECK(set_has("activity-stats.most-played", ROMS "/G06.gb") == 1);
	CHECK(set_has("activity-stats.most-played", ROMS "/G05.gb") == 0);
	CHECK(set_has("activity-stats.most-played", ROMS "/Hidden.gb") == 0);
	CHECK(set_has("activity-stats.most-played", ROMS "/Gone.gb") == 0);
	CHECK(set_has("activity-stats.most-played", NULL) == 0);

	folder_setting = "on";
	p = start();
	memset(&root, 0, sizeof(root));
	p->root_entries(&root);
	CHECK(root.count == 1);
	CHECK_STR(root.paths[0], CARD "/Most Played");
	CHECK_STR(root.name, "Most Played");
	CHECK(root.types[0] == MOSAIC_ENTRY_DIR);
	CHECK(p->virtual_dir(CARD "/Other") == NULL);
	MosaicList* list = p->virtual_dir(CARD "/Most Played");
	CHECK(list && list->count == AS_TOP);
	CHECK_STR(list->paths[0], ROMS "/Pak.pak");
	CHECK(list->types[0] == MOSAIC_ENTRY_PAK);
	CHECK_STR(list->paths[1], ROMS "/G29.gb");
	CHECK(list->types[1] == MOSAIC_ENTRY_ROM);
	CHECK_STR(list->paths[AS_TOP - 1], ROMS "/G06.gb");
	CHECK_STR(last_log, "most played: 25 games");
	for (int i = 0; i < list->count; i++) CHECK(strcmp(list->paths[i], ROMS "/Hidden.gb") != 0);

	// kept until something changes: no more is_filtered asks
	int asks = filtered_asks;
	CHECK(set_has("activity-stats.most-played", ROMS "/G29.gb") == 1);
	CHECK(filtered_asks == asks);
	set_listed = 0;
	set_list("activity-stats.most-played", countListed, NULL);
	CHECK(set_listed == AS_TOP);
	// another set changing, such as hidden's, works it out again
	filtered_path = "";
	on_set_changed("launcher.set_changed", "name=hidden", NULL);
	CHECK(set_has("activity-stats.most-played", ROMS "/Hidden.gb") == 1);
	CHECK(filtered_asks > asks);
	// its own set's change doesn't
	asks = filtered_asks;
	on_set_changed("launcher.set_changed", "name=activity-stats.most-played", NULL);
	CHECK(set_has("activity-stats.most-played", ROMS "/Hidden.gb") == 1);
	CHECK(filtered_asks == asks);
	// a game returning announces it
	sets_changed = 0;
	p->post_return();
	CHECK(sets_changed == 1);
	CHECK(set_has("activity-stats.most-played", ROMS "/Hidden.gb") == 1);
	CHECK(filtered_asks > asks);
	// a reset reloads the lists for the folder
	reloads = 0;
	fakeRunAction("activity-stats.reset", ROMS "/G29.gb");
	CHECK(reloads == 1);

	// no history: an empty folder
	fixture_count = -1;
	p->post_return();
	list = p->virtual_dir(CARD "/Most Played");
	CHECK(list && list->count == 0);
	folder_setting = "off";
	filtered_path = "";
}

// the text in the words it's given, and English again without them
static void testWords(void) {
	static const char* const words[] = { "%lldm", "%lld min", "%lldh %lldm", "%lld h %lld min", "<1m", "<1 min",
		"%i days: %s", "%i d\xc3\xad" "as: %s", "%s a day", "%s al d\xc3\xad" "a", "%i days: no play history",
		"%i d\xc3\xad" "as: sin historial", "Unknown game", "Juego desconocido", "Mon", "lun", NULL };
	static const char* const plurals[] = { "%lld session", "%lld sesi\xc3\xb3n", "%lld sesiones", "%i game", "%i juego",
		"%i juegos", NULL };
	fake_words = words;
	fake_plurals = plurals;
	ASWords set = { fakeTr, fakePlural };
	AS_setWords(&set);
	char out[128];
	AS_duration(7500, out, sizeof(out));
	CHECK_STR(out, "2 h 5 min");
	AS_duration(30, out, sizeof(out));
	CHECK_STR(out, "<1 min");
	AS_sessions(1, out, sizeof(out));
	CHECK_STR(out, "1 sesi\xc3\xb3n");
	AS_sessions(4, out, sizeof(out));
	CHECK_STR(out, "4 sesiones");
	AS_listCaption(3, 9000, 1, out, sizeof(out));
	CHECK_STR(out, "3 juegos \xc2\xb7 2 h 30 min \xc2\xb7 1 sesi\xc3\xb3n");
	long long week[7] = { 900, 1600, 7200, 50, 300, 0, 0 };
	AS_chartCaption(week, 7, out, sizeof(out));
	CHECK_STR(out, "7 d\xc3\xad" "as: 2 h 47 min \xc2\xb7 23 min al d\xc3\xad" "a");
	long long none[2] = { -1, -1 };
	AS_chartCaption(none, 2, out, sizeof(out));
	CHECK_STR(out, "2 d\xc3\xad" "as: sin historial");
	AS_name("", "", out, sizeof(out));
	CHECK_STR(out, "Juego desconocido");
	AS_dayLabel(localTime(2026, 11, 9, 12, 0), 7, 0, out, sizeof(out)); // a Monday
	CHECK_STR(out, "lun");
	AS_setWords(NULL);
	AS_duration(7500, out, sizeof(out));
	CHECK_STR(out, "2h 5m");
	AS_sessions(1, out, sizeof(out));
	CHECK_STR(out, "1 session");
	AS_listCaption(1, 60, 1, out, sizeof(out));
	CHECK_STR(out, "1 game \xc2\xb7 1m \xc2\xb7 1 session");
}

int main(void) {
	testName();
	testAllTime();
	testTop();
	testReport();
	testRank();
	testFormat();
	testWords();
	testDays();
	testChart();
	testTabs();
	testLayout();
	testInit();
	testScreen();
	testAllTimeScreen();
	testExport();
	testReset();
	testFolder();
	CHECK_DONE();
}
