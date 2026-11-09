// playmodel.c: the play history table, path matching and today's total.

#include <stdlib.h>
#include <time.h>
#include "check.h"
#include "playmodel.h"

static void testFind(void) {
	PlayTable table;
	PlayTable_init(&table);
	CHECK(PlayTable_find(&table, "GB/Tetris.gb") == NULL);
	CHECK(PlayTable_addGame(&table, "GBA (MGBA)/Zelda.gba", 0, 3600, 1700000000, 3) == 0);
	CHECK(PlayTable_addGame(&table, "GB (GB)/Tetris.gb", 0, 60, 1600000000, 1) == 0);
	// a second row for one path: the first read wins
	CHECK(PlayTable_addGame(&table, "GB (GB)/Tetris.gb", 0, 999, 1, 9) == 0);
	// orphans carried over from Onion, by name
	CHECK(PlayTable_addGame(&table, "Metroid", 1, 120, 1500000000, 2) == 0);
	CHECK(PlayTable_addGame(&table, "Kirby.gb", 1, 30, 1400000000, 1) == 0);
	// skipped: no key
	CHECK(PlayTable_addGame(&table, NULL, 0, 5, 5, 5) == 0);
	CHECK(PlayTable_addGame(&table, "", 0, 5, 5, 5) == 0);
	// negatives clamp to 0
	CHECK(PlayTable_addGame(&table, "NES/Odd.nes", 0, -5, -1, -2) == 0);
	CHECK(table.count == 6);
	// not sorted yet
	CHECK(PlayTable_find(&table, "GB (GB)/Tetris.gb") == NULL);
	PlayTable_finish(&table);

	const PlayGame* game = PlayTable_find(&table, "GBA (MGBA)/Zelda.gba");
	CHECK(game && game->seconds == 3600 && game->last == 1700000000 && game->count == 3);
	game = PlayTable_find(&table, "GB (GB)/Tetris.gb");
	CHECK(game && game->seconds == 60 && game->count == 1);
	game = PlayTable_find(&table, "NES/Odd.nes");
	CHECK(game && game->seconds == 0 && game->last == 0 && game->count == 0);

	// orphans match the file name without its extension, then with it
	game = PlayTable_find(&table, "NES (FCEUMM)/Metroid.nes");
	CHECK(game && game->seconds == 120 && game->orphan);
	game = PlayTable_find(&table, "GB (GB)/Kirby.gb");
	CHECK(game && game->seconds == 30);
	CHECK(PlayTable_find(&table, "Metroid") != NULL);
	// a path never matches an orphan's key as a path
	CHECK(PlayTable_find(&table, "GB (GB)/Zelda.gba") == NULL);
	CHECK(PlayTable_find(&table, "GBA (MGBA)/zelda.gba") == NULL);
	CHECK(PlayTable_find(&table, "") == NULL);
	CHECK(PlayTable_find(&table, NULL) == NULL);
	CHECK(PlayTable_find(&table, "GB (GB)/") == NULL);
	CHECK(PlayTable_find(NULL, "GB (GB)/Tetris.gb") == NULL);

	PlayTable_free(&table);
	CHECK(table.count == 0 && table.games == NULL);
	CHECK(PlayTable_find(&table, "GB (GB)/Tetris.gb") == NULL);
}

static void testMany(void) {
	PlayTable table;
	PlayTable_init(&table);
	char key[32];
	for (int i = 999; i >= 0; i--) {
		snprintf(key, sizeof(key), "SYS/game%04i.bin", i);
		CHECK(PlayTable_addGame(&table, key, 0, i, i, i) == 0);
	}
	for (int i = 0; i < 300; i++) CHECK(PlayTable_addSession(&table, i, 1) == 0);
	PlayTable_finish(&table);
	CHECK(table.count == 1000 && table.session_count == 300);
	int found = 0;
	for (int i = 0; i < 1000; i++) {
		snprintf(key, sizeof(key), "SYS/game%04i.bin", i);
		const PlayGame* game = PlayTable_find(&table, key);
		if (game && game->seconds == i) found++;
	}
	CHECK(found == 1000);
	CHECK(PlayTable_find(&table, "SYS/game1000.bin") == NULL);
	PlayTable_free(&table);
}

static void testRelPath(void) {
	char out[64];
	CHECK(Play_relPath("/mnt/SDCARD/Roms", "/mnt/SDCARD/Roms/GB (GB)/Tetris.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, "GB (GB)/Tetris.gb");
	CHECK(Play_relPath("/mnt/SDCARD/Roms/", "/mnt/SDCARD/Roms//GB/T.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, "GB/T.gb");
	// outside Roms
	CHECK(Play_relPath("/mnt/SDCARD/Roms", "/mnt/SDCARD/Tools/tg5040/Files.pak", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(Play_relPath("/mnt/SDCARD/Roms", "/mnt/SDCARD/RomsX/a.gb", out, sizeof(out)) == 0);
	CHECK(Play_relPath("/mnt/SDCARD/Roms", "/mnt/SDCARD/Roms", out, sizeof(out)) == 0);
	CHECK(Play_relPath("/mnt/SDCARD/Roms", "/mnt/SDCARD/Roms/", out, sizeof(out)) == 0);
	// too long for out
	CHECK(Play_relPath("/r", "/r/0123456789", out, 8) == 0);
	CHECK_STR(out, "");
	CHECK(Play_relPath(NULL, "/r/a", out, sizeof(out)) == 0);
	CHECK(Play_relPath("/r", NULL, out, sizeof(out)) == 0);
	CHECK(Play_relPath("/r", "/r/a", NULL, 0) == 0);
}

static long long at(int year, int month, int day, int hour, int min) {
	struct tm tm = { 0 };
	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_isdst = -1;
	return (long long)mktime(&tm);
}

static void testDayBounds(const char* tz) {
	setenv("TZ", tz, 1);
	tzset();
	long long start, end;
	Play_dayBounds(at(2026, 11, 8, 14, 30), &start, &end);
	CHECK(start == at(2026, 11, 8, 0, 0));
	CHECK(end == at(2026, 11, 9, 0, 0));
	// midnight is the day's own start
	Play_dayBounds(at(2026, 11, 8, 0, 0), &start, &end);
	CHECK(start == at(2026, 11, 8, 0, 0));
	// the last second is still the day
	Play_dayBounds(at(2026, 11, 9, 0, 0) - 1, &start, &end);
	CHECK(start == at(2026, 11, 8, 0, 0));
	// month and year ends
	Play_dayBounds(at(2026, 12, 31, 23, 59), &start, &end);
	CHECK(end == at(2027, 1, 1, 0, 0));
}

static void testDst(void) {
	// New York's clocks went back an hour on 2026-11-01, so that day is 25
	// hours long, and forward on 2026-03-08, a 23 hour day
	setenv("TZ", "America/New_York", 1);
	tzset();
	long long start, end;
	Play_dayBounds(at(2026, 11, 1, 12, 0), &start, &end);
	CHECK(end - start == 25 * 3600);
	Play_dayBounds(at(2026, 3, 8, 12, 0), &start, &end);
	CHECK(end - start == 23 * 3600);
}

static void testSecondsBetween(void) {
	long long from = 1000000, to = from + 86400;
	PlaySession sessions[] = {
		{ from + 100, 600 },  // inside: 600
		{ from - 300, 500 },  // over midnight: 200 of it today
		{ from - 900, 100 },  // all yesterday
		{ to - 50, 200 },     // runs into tomorrow: 50
		{ to + 10, 100 },     // tomorrow
		{ from + 5000, 0 },   // empty
	};
	int count = (int)(sizeof(sessions) / sizeof(sessions[0]));
	CHECK(Play_secondsBetween(sessions, count, from, to) == 600 + 200 + 50);
	CHECK(Play_secondsBetween(sessions, 0, from, to) == 0);
	CHECK(Play_secondsBetween(NULL, 0, from, to) == 0);
	// an empty window
	CHECK(Play_secondsBetween(sessions, count, to, from) == 0);
}

static void testList(void) {
	PlayTable table;
	PlayTable_init(&table);
	// rows as gametimectl's rom table has them, in rom order
	CHECK(PlayTable_addRom(&table, "GB (GB)/Tetris.gb", "Tetris", 0, 600, 100, 900, 3) == 0);
	CHECK(PlayTable_addRom(&table, "GBA (MGBA)/Zelda.gba", "Zelda", 0, 3600, 200, 800, 2) == 0);
	// an orphan known only by name
	CHECK(PlayTable_addRom(&table, NULL, "Metroid", 1, 120, 50, 60, 2) == 0);
	// an orphan with a path too: looked up both ways, listed once
	CHECK(PlayTable_addRom(&table, "NES/Kirby.nes", "Kirby", 1, 600, 10, 950, 1) == 0);
	// a second row for Tetris's path: found and listed as the first
	CHECK(PlayTable_addRom(&table, "GB (GB)/Tetris.gb", "Tetris", 0, 9999, 1, 999, 9) == 0);
	// no sessions: looked up, not listed
	CHECK(PlayTable_addRom(&table, "NES/Unplayed.nes", "Unplayed", 0, 0, 0, 0, 0) == 0);
	// neither a path nor a name
	CHECK(PlayTable_addRom(&table, NULL, NULL, 1, 5, 5, 5, 1) == 0);
	CHECK(PlayTable_addRom(&table, "", "", 0, 5, 5, 5, 1) == 0);
	CHECK(table.count == 7);
	// not until it's finished
	CHECK(PlayTable_list(&table) == 0);
	PlayTable_finish(&table);

	CHECK(PlayTable_list(&table) == 4);
	CHECK(PlayTable_list(&table) == 4);
	const PlayGame* game = PlayTable_listed(&table, 0);
	CHECK(game && !strcmp(game->key, "GBA (MGBA)/Zelda.gba") && game->seconds == 3600 && game->first == 200);
	CHECK(game && game->name && !strcmp(game->name, "Zelda"));
	// 600 each: the latest played first
	game = PlayTable_listed(&table, 1);
	CHECK(game && !strcmp(game->key, "NES/Kirby.nes") && !game->orphan && game->last == 950);
	game = PlayTable_listed(&table, 2);
	CHECK(game && !strcmp(game->key, "GB (GB)/Tetris.gb") && game->seconds == 600 && game->count == 3);
	game = PlayTable_listed(&table, 3);
	CHECK(game && !strcmp(game->key, "Metroid") && game->orphan && game->first == 50 && game->last == 60);
	CHECK(PlayTable_listed(&table, 4) == NULL);
	CHECK(PlayTable_listed(&table, -1) == NULL);
	CHECK(PlayTable_listed(NULL, 0) == NULL);

	// the orphan with a path is found by its name as well
	game = PlayTable_find(&table, "SNES/Kirby.sfc");
	CHECK(game && game->orphan && game->alias && game->seconds == 600);
	game = PlayTable_find(&table, "NES/Unplayed.nes");
	CHECK(game && game->count == 0);

	// adding again drops the list until it's finished again
	CHECK(PlayTable_addRom(&table, "GB (GB)/Mario.gb", "Mario", 0, 7200, 1, 2, 1) == 0);
	CHECK(PlayTable_listed(&table, 0) == NULL);
	PlayTable_finish(&table);
	CHECK(PlayTable_list(&table) == 5);
	game = PlayTable_listed(&table, 0);
	CHECK(game && !strcmp(game->key, "GB (GB)/Mario.gb"));
	PlayTable_free(&table);
	CHECK(table.list == NULL && table.list_count == 0 && !table.listed);

	// an empty history lists nothing
	PlayTable_init(&table);
	PlayTable_finish(&table);
	CHECK(PlayTable_list(&table) == 0);
	CHECK(PlayTable_listed(&table, 0) == NULL);
	PlayTable_free(&table);
	CHECK(PlayTable_list(NULL) == 0);
}

// A path whose first row has no sessions isn't listed by a later one.
static void testListFirstRow(void) {
	PlayTable table;
	PlayTable_init(&table);
	CHECK(PlayTable_addRom(&table, "GB/A.gb", "A", 0, 0, 0, 0, 0) == 0);
	CHECK(PlayTable_addRom(&table, "GB/A.gb", "A", 0, 50, 1, 1, 1) == 0);
	PlayTable_finish(&table);
	CHECK(PlayTable_list(&table) == 0);
	const PlayGame* game = PlayTable_find(&table, "GB/A.gb");
	CHECK(game && game->count == 0);
	PlayTable_free(&table);
}

static void testFullPath(void) {
	char out[64];
	CHECK(Play_fullPath("/mnt/SDCARD/Roms", "GB (GB)/Tetris.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, "/mnt/SDCARD/Roms/GB (GB)/Tetris.gb");
	CHECK(Play_fullPath("/mnt/SDCARD/Roms/", "GB/T.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, "/mnt/SDCARD/Roms/GB/T.gb");
	// and back
	char rel[64];
	CHECK(Play_relPath("/mnt/SDCARD/Roms", out, rel, sizeof(rel)) == 1);
	CHECK_STR(rel, "GB/T.gb");
	CHECK(Play_fullPath("/r", "0123456789", out, 8) == 0);
	CHECK_STR(out, "");
	CHECK(Play_fullPath("/r", "", out, sizeof(out)) == 0);
	CHECK(Play_fullPath("/r", NULL, out, sizeof(out)) == 0);
	CHECK(Play_fullPath(NULL, "a", out, sizeof(out)) == 0);
	CHECK(Play_fullPath("/r", "a", NULL, 0) == 0);
}

static void testDayBoundsAgo(const char* tz) {
	setenv("TZ", tz, 1);
	tzset();
	long long start, end;
	long long now = at(2026, 11, 8, 14, 30);
	Play_dayBoundsAgo(now, 0, &start, &end);
	CHECK(start == at(2026, 11, 8, 0, 0) && end == at(2026, 11, 9, 0, 0));
	Play_dayBoundsAgo(now, 1, &start, &end);
	CHECK(start == at(2026, 11, 7, 0, 0) && end == at(2026, 11, 8, 0, 0));
	// back over a month and a year
	Play_dayBoundsAgo(now, 8, &start, &end);
	CHECK(start == at(2026, 10, 31, 0, 0) && end == at(2026, 11, 1, 0, 0));
	Play_dayBoundsAgo(at(2026, 1, 2, 9, 0), 2, &start, &end);
	CHECK(start == at(2025, 12, 31, 0, 0) && end == at(2026, 1, 1, 0, 0));
	Play_dayBoundsAgo(now, 365, &start, &end);
	CHECK(start == at(2025, 11, 8, 0, 0));
	// from a day's own midnight
	Play_dayBoundsAgo(at(2026, 11, 8, 0, 0), 1, &start, &end);
	CHECK(start == at(2026, 11, 7, 0, 0) && end == at(2026, 11, 8, 0, 0));
}

static void testDstAgo(void) {
	setenv("TZ", "America/New_York", 1);
	tzset();
	long long start, end;
	// back from 2026-11-03 to the 25 hour day
	Play_dayBoundsAgo(at(2026, 11, 3, 12, 0), 2, &start, &end);
	CHECK(start == at(2026, 11, 1, 0, 0) && end - start == 25 * 3600);
	Play_dayBoundsAgo(at(2026, 3, 10, 0, 30), 2, &start, &end);
	CHECK(start == at(2026, 3, 8, 0, 0) && end - start == 23 * 3600);
}

static void testDays(void) {
	PlayDays days;
	PlayDays_init(&days);
	long long day = 86400, d0 = 100 * day;
	// not until it's finished
	CHECK(PlayDays_seconds(&days, d0, d0 + day) == -1);
	CHECK(PlayDays_add(&days, d0 + 3 * day + 100, 600) == 0);  // day 3
	CHECK(PlayDays_add(&days, d0 + 100, 60) == 0);             // day 0
	CHECK(PlayDays_add(&days, d0 + day - 300, 500) == 0);      // day 0 into day 1: 300 and 200
	CHECK(PlayDays_add(&days, d0 + 2 * day + 10, -5) == 0);    // clamps to 0
	// a week long, as a crash leaves one: from day 3 into day 10
	CHECK(PlayDays_add(&days, d0 + 3 * day + 5000, 7 * day) == 0);
	PlayDays_finish(&days, 0);
	CHECK(days.count == 5 && days.longest == 7 * day && !days.capped);
	CHECK(days.sessions[0].start == d0 + 100);
	CHECK(PlayDays_seconds(&days, d0, d0 + day) == 60 + 300);
	CHECK(PlayDays_seconds(&days, d0 + day, d0 + 2 * day) == 200);
	CHECK(PlayDays_seconds(&days, d0 + 2 * day, d0 + 3 * day) == 0);
	CHECK(PlayDays_seconds(&days, d0 + 3 * day, d0 + 4 * day) == 600 + day - 5000);
	CHECK(PlayDays_seconds(&days, d0 + 7 * day, d0 + 8 * day) == day);
	CHECK(PlayDays_seconds(&days, d0 + 10 * day, d0 + 11 * day) == 5000);
	CHECK(PlayDays_seconds(&days, d0 + 11 * day, d0 + 12 * day) == 0);
	// before any of them
	CHECK(PlayDays_seconds(&days, d0 - day, d0) == 0);
	CHECK(PlayDays_seconds(NULL, d0, d0 + day) == -1);
	PlayDays_free(&days);

	// capped: the oldest read is the cut, and a day up to it is unknown
	PlayDays_init(&days);
	CHECK(PlayDays_add(&days, d0 + day + 100, 50) == 0);
	CHECK(PlayDays_add(&days, d0 + 2 * day + 100, 70) == 0);
	PlayDays_finish(&days, 1);
	CHECK(days.capped && days.cut == d0 + day + 100);
	CHECK(PlayDays_seconds(&days, d0 + 2 * day, d0 + 3 * day) == 70);
	CHECK(PlayDays_seconds(&days, d0 + day, d0 + 2 * day) == -1);
	CHECK(PlayDays_seconds(&days, d0, d0 + day) == -1);
	PlayDays_free(&days);
	CHECK(days.count == 0 && days.sessions == NULL);

	// none at all
	PlayDays_init(&days);
	PlayDays_finish(&days, 0);
	CHECK(PlayDays_seconds(&days, d0, d0 + day) == 0);
	PlayDays_free(&days);
}

// Many sessions: each day's total matches a plain sum over all of them.
static void testDaysMany(void) {
	PlayDays days;
	PlayDays_init(&days);
	long long day = 86400, d0 = 1000 * day;
	unsigned seed = 7;
	for (int i = 0; i < 5000; i++) {
		seed = seed * 1103515245u + 12345u;
		long long start = d0 + (long long)(seed >> 8) % (365 * day);
		long long seconds = (long long)(seed >> 4) % 7200;
		CHECK(PlayDays_add(&days, start, seconds) == 0);
	}
	PlayDays_finish(&days, 0);
	int same = 0;
	for (int d = 0; d < 366; d++) {
		long long from = d0 + d * day, to = from + day;
		if (PlayDays_seconds(&days, from, to) == Play_secondsBetween(days.sessions, days.count, from, to)) same++;
	}
	CHECK(same == 366);
	PlayDays_free(&days);
}

int main(void) {
	testFind();
	testMany();
	testList();
	testListFirstRow();
	testFullPath();
	testDays();
	testDaysMany();
	testRelPath();
	testDayBounds("UTC");
	testDayBounds("America/Los_Angeles");
	testDayBounds("Asia/Kolkata");
	testDst();
	testDayBoundsAgo("UTC");
	testDayBoundsAgo("America/Los_Angeles");
	testDayBoundsAgo("Asia/Kolkata");
	testDstAgo();
	testSecondsBetween();
	CHECK_DONE();
}
