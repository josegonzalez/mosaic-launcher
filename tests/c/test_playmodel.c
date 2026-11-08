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

int main(void) {
	testFind();
	testMany();
	testRelPath();
	testDayBounds("UTC");
	testDayBounds("America/Los_Angeles");
	testDayBounds("Asia/Kolkata");
	testDst();
	testSecondsBetween();
	CHECK_DONE();
}
