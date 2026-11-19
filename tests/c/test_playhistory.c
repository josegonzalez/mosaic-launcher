// playhistory.c: reading a fixture game_logs.sqlite, made here the way
// gametimectl makes it, through the system's sqlite library.

#include <dlfcn.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "playhistory.h"
#include "playmodel.h"

#ifndef TEST_DIR
#error "build with -DTEST_DIR=\"...\""
#endif

#define DB TEST_DIR "/game_logs.sqlite"
#define ROMS "/card/Roms"

typedef struct sqlite3 sqlite3;
static int (*db_open)(const char*, sqlite3**, int, const char*);
static int (*db_exec)(sqlite3*, const char*, void*, void*, char**);
static int (*db_close)(sqlite3*);

static char last_log[1024];
static int logs = 0;
static void onLog(const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
	logs++;
}

static int bindWriter(void) {
#ifdef __APPLE__
	void* lib = dlopen("libsqlite3.dylib", RTLD_NOW);
#else
	void* lib = dlopen("libsqlite3.so.0", RTLD_NOW);
#endif
	if (!lib) return 0;
	*(void**)&db_open = dlsym(lib, "sqlite3_open_v2");
	*(void**)&db_exec = dlsym(lib, "sqlite3_exec");
	*(void**)&db_close = dlsym(lib, "sqlite3_close");
	return db_open && db_exec && db_close;
}

static sqlite3* openWriter(void) {
	sqlite3* db = NULL;
	CHECK(db_open(DB, &db, 0x2 | 0x4, NULL) == 0); // read-write, create
	return db;
}

static void exec(sqlite3* db, const char* sql) {
	char* error = NULL;
	int rc = db_exec(db, sql, NULL, NULL, &error);
	if (rc) fprintf(stderr, "sqlite: %s: %s\n", error ? error : "?", sql);
	CHECK(rc == 0);
}

// gametimectl's own schema
static const char* SCHEMA =
	"CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, "
	"created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);"
	"CREATE UNIQUE INDEX rom_id_index ON rom(id);"
	"CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, "
	"created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);"
	"CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);";

static long long midnight;

static void session(sqlite3* db, int rom, long long start, const char* seconds) {
	char sql[256];
	snprintf(sql, sizeof(sql), "INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(%i, %s, %lld);",
		rom, seconds, start);
	exec(db, sql);
}

static void makeFixture(void) {
	unlink(DB);
	sqlite3* db = openWriter();
	exec(db, SCHEMA);
	exec(db, "INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Tetris', 'GB (GB)/Tetris.gb');"
		"INSERT INTO rom(id, type, name, file_path) VALUES(2, '', 'Zelda', 'GBA (MGBA)/Zelda.gba');"
		"INSERT INTO rom(id, type, name, file_path) VALUES(3, 'ORPHAN', 'Metroid', NULL);"
		"INSERT INTO rom(id, type, name, file_path) VALUES(4, '', 'Unplayed', 'NES (FCEUMM)/Unplayed.nes');"
		"INSERT INTO rom(id, type, name, file_path) VALUES(5, '', 'Tetris again', 'GB (GB)/Tetris.gb');");
	session(db, 1, midnight - 7200, "3600"); // yesterday
	session(db, 1, midnight - 600, "900");   // over midnight: 300 today
	session(db, 1, midnight + 3600, "1200"); // today
	session(db, 2, midnight + 7200, "NULL"); // still open: counts up to now
	session(db, 3, midnight - 3 * 86400, "50");
	session(db, 5, midnight + 100, "77"); // a second row for Tetris's path: the first wins
	db_close(db);
}

// Local midnight about 12 hours ago, whatever the time, so sessions either
// side of it are all in the past.
static void middayZone(void) {
	time_t now = time(NULL);
	struct tm utc;
	gmtime_r(&now, &utc);
	int behind = utc.tm_hour - 12; // hours local time is behind UTC
	char tz[32];
	snprintf(tz, sizeof(tz), "TST%+i", behind);
	setenv("TZ", tz, 1);
	tzset();
	long long end;
	Play_dayBounds((long long)now, &midnight, &end);
}

static char* slurp(const char* path, long* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* data = malloc(*len + 1);
	if (fread(data, 1, *len, f) != (size_t)*len) *len = -1;
	fclose(f);
	return data;
}

static void testMissing(void) {
	unlink(DB);
	PlayHistory_configure(DB, ROMS, onLog);
	CHECK(!PlayHistory_loaded());
	CHECK(PlayHistory_today() == PLAY_UNKNOWN);
	CHECK(PlayHistory_loaded());
	CHECK(strstr(last_log, "play history: unknown, no " DB) != NULL);
	int seen = logs;
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == PLAY_UNKNOWN);
	CHECK(PlayHistory_lastPlayed(ROMS "/GB (GB)/Tetris.gb") == PLAY_UNKNOWN);
	CHECK(PlayHistory_count(ROMS "/GB (GB)/Tetris.gb") == PLAY_UNKNOWN);
	// looked once until the next invalidate
	CHECK(logs == seen);
}

static void testRead(void) {
	makeFixture();
	PlayHistory_configure(DB, ROMS, onLog);
	// unknown for a NULL path, without reading anything
	CHECK(PlayHistory_seconds(NULL) == PLAY_UNKNOWN);
	CHECK(!PlayHistory_loaded());

	const char* tetris = ROMS "/GB (GB)/Tetris.gb";
	long long now = (long long)time(NULL);
	CHECK(PlayHistory_seconds(tetris) == 3600 + 900 + 1200);
	CHECK(PlayHistory_loaded());
	CHECK(strstr(last_log, "play history: read 5 games and 4 sessions since midnight from " DB) != NULL);
	CHECK(PlayHistory_lastPlayed(tetris) == midnight + 3600);
	CHECK(PlayHistory_count(tetris) == 3);

	const char* zelda = ROMS "/GBA (MGBA)/Zelda.gba";
	long long open = PlayHistory_seconds(zelda);
	CHECK(open >= now - (midnight + 7200) && open <= now - (midnight + 7200) + 5);
	CHECK(PlayHistory_count(zelda) == 1);
	CHECK(PlayHistory_lastPlayed(zelda) == midnight + 7200);

	// an orphan, by name in any folder
	const char* metroid = ROMS "/NES (FCEUMM)/Metroid.nes";
	CHECK(PlayHistory_seconds(metroid) == 50);
	CHECK(PlayHistory_count(metroid) == 1);

	const char* unplayed = ROMS "/NES (FCEUMM)/Unplayed.nes";
	CHECK(PlayHistory_seconds(unplayed) == 0);
	CHECK(PlayHistory_lastPlayed(unplayed) == 0);
	CHECK(PlayHistory_count(unplayed) == 0);
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Never.gb") == 0);
	CHECK(PlayHistory_seconds("/elsewhere/Tetris.gb") == 0);

	// the second Tetris row's session counts today: it's on the card
	long long today = PlayHistory_today();
	CHECK(today >= 300 + 1200 + 77 + open && today <= 300 + 1200 + 77 + open + 5);

	// answers are kept: the file can go
	int seen = logs;
	unlink(DB);
	CHECK(PlayHistory_seconds(tetris) == 5700);
	CHECK(PlayHistory_today() == today);
	CHECK(logs == seen);

	// invalidate drops them: with the file gone, they're unknown
	PlayHistory_invalidate();
	CHECK(!PlayHistory_loaded());
	CHECK(PlayHistory_seconds(tetris) == PLAY_UNKNOWN);
}

// Reading leaves the file as it was, with no journal behind.
static void testReadOnly(void) {
	makeFixture();
	long before_len = 0, after_len = 0;
	char* before = slurp(DB, &before_len);
	PlayHistory_configure(DB, ROMS, onLog);
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == 5700);
	char* after = slurp(DB, &after_len);
	CHECK(before && after && before_len == after_len && before_len > 0 && !memcmp(before, after, before_len));
	CHECK(access(DB "-journal", F_OK) != 0);
	CHECK(access(DB "-wal", F_OK) != 0);
	free(before);
	free(after);
}

// A writer holding the database: the read gives up after its busy timeout
// rather than waiting on it, and the next read after invalidate gets in.
static void testBusy(void) {
	makeFixture();
	sqlite3* writer = openWriter();
	exec(writer, "BEGIN EXCLUSIVE; INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 10, 1);");
	PlayHistory_configure(DB, ROMS, onLog);
	time_t started = time(NULL);
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == PLAY_UNKNOWN);
	CHECK(time(NULL) - started <= 2);
	CHECK(strstr(last_log, "play history: unknown, can't read " DB) != NULL);
	exec(writer, "COMMIT;");
	db_close(writer);
	PlayHistory_invalidate();
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == 5710);
	CHECK(PlayHistory_count(ROMS "/GB (GB)/Tetris.gb") == 4);
}

static void testBadFiles(void) {
	// not a database
	FILE* f = fopen(DB, "w");
	fputs("not sqlite at all, just some text that is long enough to have a header", f);
	fclose(f);
	PlayHistory_configure(DB, ROMS, onLog);
	CHECK(PlayHistory_today() == PLAY_UNKNOWN);
	CHECK(strstr(last_log, "play history: unknown, can't") != NULL);

	// an empty file, as gametimectl touches it before making the tables
	f = fopen(DB, "w");
	fclose(f);
	PlayHistory_configure(DB, ROMS, onLog);
	CHECK(PlayHistory_today() == PLAY_UNKNOWN);
	CHECK(strstr(last_log, "play history: unknown, can't read " DB) != NULL);
	unlink(DB);

	// a folder in its place
	mkdir(DB, 0755);
	PlayHistory_configure(DB, ROMS, onLog);
	CHECK(PlayHistory_today() == PLAY_UNKNOWN);
	CHECK(strstr(last_log, "play history: unknown, no " DB) != NULL);
	rmdir(DB);
}

static void testNoLibrary(void) {
	makeFixture();
	PlayHistory_quit();
	setenv("MOSAIC_LAUNCHER_SQLITE", TEST_DIR "/no-such-libsqlite3.so", 1);
	PlayHistory_configure(DB, ROMS, onLog);
	CHECK(PlayHistory_today() == PLAY_UNKNOWN);
	CHECK(strstr(last_log, "play history: unknown, no sqlite library (") != NULL);
	unsetenv("MOSAIC_LAUNCHER_SQLITE");
	// tried again after an invalidate
	PlayHistory_invalidate();
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == 5700);
	PlayHistory_quit();
	unlink(DB);
}

// The whole history: every game, most played first, and each day's total.
static void testList(void) {
	makeFixture();
	PlayHistory_configure(DB, ROMS, onLog);
	long long now = (long long)time(NULL);
	long long open = now - (midnight + 7200);
	// Tetris's second row and Unplayed aren't listed
	CHECK(PlayHistory_gameCount() == 3);
	CHECK(PlayHistory_loaded());
	CHECK(!PlayHistory_daysLoaded());

	char path[512];
	const PlayGame* zelda = PlayHistory_game(0);
	CHECK(zelda && zelda->seconds >= open && zelda->seconds <= open + 5 && zelda->count == 1);
	CHECK(PlayHistory_gamePath(zelda, path, sizeof(path)) == 1);
	CHECK_STR(path, ROMS "/GBA (MGBA)/Zelda.gba");
	const PlayGame* tetris = PlayHistory_game(1);
	CHECK(tetris && tetris->seconds == 5700 && tetris->count == 3);
	CHECK(tetris && tetris->first == midnight - 7200 && tetris->last == midnight + 3600);
	CHECK(tetris && tetris->name && !strcmp(tetris->name, "Tetris"));
	CHECK(PlayHistory_gamePath(tetris, path, sizeof(path)) == 1);
	CHECK_STR(path, ROMS "/GB (GB)/Tetris.gb");
	// as the lookups answer for its path
	CHECK(PlayHistory_seconds(path) == tetris->seconds);
	const PlayGame* metroid = PlayHistory_game(2);
	CHECK(metroid && metroid->orphan && metroid->seconds == 50 && !strcmp(metroid->key, "Metroid"));
	CHECK(PlayHistory_gamePath(metroid, path, sizeof(path)) == 0);
	CHECK_STR(path, "");
	CHECK(PlayHistory_game(3) == NULL);
	CHECK(PlayHistory_game(-1) == NULL);
	CHECK(PlayHistory_gamePath(NULL, path, sizeof(path)) == 0);

	// days are a read of their own, once
	int seen = logs;
	long long today = PlayHistory_day(0);
	CHECK(PlayHistory_daysLoaded());
	CHECK(logs == seen + 1);
	CHECK(strstr(last_log, "play history: days from 6 sessions in " DB) != NULL);
	CHECK(today >= 300 + 1200 + 77 + open && today <= 300 + 1200 + 77 + open + 5);
	CHECK(today - PlayHistory_today() >= -5 && today - PlayHistory_today() <= 5);
	// yesterday: an hour of Tetris, and its session up to midnight
	CHECK(PlayHistory_day(1) == 3600 + 600);
	CHECK(PlayHistory_day(2) == 0);
	CHECK(PlayHistory_day(3) == 50);
	CHECK(PlayHistory_day(4) == 0);
	CHECK(PlayHistory_day(1000) == 0);
	CHECK(PlayHistory_day(-1) == PLAY_UNKNOWN);
	// kept: the file can go
	unlink(DB);
	CHECK(PlayHistory_day(1) == 3600 + 600);
	CHECK(PlayHistory_day(5) == 0);
	CHECK(PlayHistory_gameCount() == 3);
	CHECK(logs == seen + 1);
	PlayHistory_invalidate();
	CHECK(!PlayHistory_daysLoaded());
	CHECK(PlayHistory_gameCount() == PLAY_UNKNOWN);
	CHECK(PlayHistory_game(0) == NULL);
	CHECK(PlayHistory_day(0) == PLAY_UNKNOWN);
	// unknown already, so the days aren't tried
	CHECK(strstr(last_log, "play history: unknown, no " DB) != NULL);
}

// Past the caps: the most played games, and the newest sessions with the
// days before them unknown.
static void testCaps(void) {
	makeFixture();
	PlayHistory_configure(DB, ROMS, onLog);
	PlayHistory_setCaps(2, 4);
	CHECK(PlayHistory_gameCount() == 2);
	CHECK(strstr(last_log, "play history: read 2 games and ") != NULL);
	const PlayGame* game = PlayHistory_game(1);
	CHECK(game && game->seconds == 5700);
	// the least played aren't read
	CHECK(PlayHistory_seconds(ROMS "/NES (FCEUMM)/Metroid.nes") == 0);
	long long open = (long long)time(NULL) - (midnight + 7200);
	long long today = PlayHistory_day(0);
	CHECK(today >= 300 + 1200 + 77 + open && today <= 300 + 1200 + 77 + open + 5);
	CHECK(strstr(last_log, "play history: days from the newest 4 sessions in " DB ", days from before them unknown") != NULL);
	CHECK(PlayHistory_day(1) == PLAY_UNKNOWN);
	CHECK(PlayHistory_day(3) == PLAY_UNKNOWN);
	PlayHistory_setCaps(0, 0);
	CHECK(PlayHistory_gameCount() == 3);
	CHECK(PlayHistory_day(1) == 3600 + 600);
	PlayHistory_invalidate();
	unlink(DB);
}

static double msSince(const struct timespec* start) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - start->tv_sec) * 1000.0 + (now.tv_nsec - start->tv_nsec) / 1e6;
}

// A big history: 2000 games and 20000 sessions over a year. Prints how
// long each part takes; checks the days add up to the whole.
#ifndef BIG_GAMES
#define BIG_GAMES 2000
#endif
#ifndef BIG_SESSIONS
#define BIG_SESSIONS 20000
#endif
static void testBig(void) {
	unlink(DB);
	sqlite3* db = openWriter();
	exec(db, SCHEMA);
	long long now = (long long)time(NULL);
	char sql[1024];
	snprintf(sql, sizeof(sql),
		"BEGIN; WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < %i) "
		"INSERT INTO rom(id, type, name, file_path) SELECT i, '', 'Game ' || i, 'SYS/Game ' || i || '.bin' FROM n; "
		"WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < %i) "
		"INSERT INTO play_activity(rom_id, play_time, created_at) "
		"SELECT (i * 7919) %% %i + 1, (i * 31) %% 3600 + 60, %lld - (i * 3137) %% (365 * 86400) FROM n; COMMIT;",
		BIG_GAMES, BIG_SESSIONS, BIG_GAMES, now - 7200);
	exec(db, sql);
	db_close(db);
	long long all = 0;
	for (long long i = 1; i <= BIG_SESSIONS; i++) all += (i * 31) % 3600 + 60;

	PlayHistory_configure(DB, ROMS, onLog);
	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	CHECK(PlayHistory_seconds(ROMS "/SYS/Game 1.bin") > 0);
	double read_ms = msSince(&start);
	clock_gettime(CLOCK_MONOTONIC, &start);
	CHECK(PlayHistory_gameCount() == BIG_GAMES);
	double list_ms = msSince(&start);
	long long listed = 0;
	for (int i = 0; i < BIG_GAMES; i++) {
		const PlayGame* game = PlayHistory_game(i);
		if (game) listed += game->seconds;
		if (i && game && PlayHistory_game(i - 1)->seconds < game->seconds) listed = -1;
	}
	CHECK(listed == all);
	clock_gettime(CLOCK_MONOTONIC, &start);
	CHECK(PlayHistory_day(0) >= 0);
	double days_ms = msSince(&start);
	clock_gettime(CLOCK_MONOTONIC, &start);
	long long summed = 0;
	for (int d = 0; d < 367; d++) summed += PlayHistory_day(d);
	double year_ms = msSince(&start);
	CHECK(summed == all);
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (int d = 0; d < 367; d++) PlayHistory_day(d);
	double again_ms = msSince(&start);
	printf("play history: %i games, %i sessions: read %.2f ms, list %.2f ms, sessions read %.2f ms, "
		"a year of days %.3f ms, again %.3f ms\n",
		BIG_GAMES, BIG_SESSIONS, read_ms, list_ms, days_ms, year_ms, again_ms);
	PlayHistory_invalidate();
	unlink(DB);
}

// Moving a game's history: renamed when the new path has none, onto the
// new path's row when it has one, and refused off Roms or onto itself.
static void testMove(void) {
	makeFixture();
	PlayHistory_configure(DB, ROMS, onLog);
	long long zelda = PlayHistory_seconds(ROMS "/GBA (MGBA)/Zelda.gba");
	CHECK(zelda > 0);
	CHECK(PlayHistory_move(ROMS "/GBA (MGBA)/Zelda.gba", ROMS "/GBA (MGBA)/Zelda/Zelda.gba") == 1);
	CHECK(!PlayHistory_loaded());
	CHECK(PlayHistory_seconds(ROMS "/GBA (MGBA)/Zelda.gba") == 0);
	CHECK(PlayHistory_seconds(ROMS "/GBA (MGBA)/Zelda/Zelda.gba") >= zelda);
	CHECK(strstr(last_log, "play history:") != NULL);

	// both of Tetris's rows' sessions, onto Unplayed's row
	CHECK(PlayHistory_move(ROMS "/GB (GB)/Tetris.gb", ROMS "/NES (FCEUMM)/Unplayed.nes") == 1);
	CHECK(PlayHistory_seconds(ROMS "/NES (FCEUMM)/Unplayed.nes") == 3600 + 900 + 1200 + 77);
	CHECK(PlayHistory_count(ROMS "/NES (FCEUMM)/Unplayed.nes") == 4);
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == 0);

	CHECK(PlayHistory_move(ROMS "/GB (GB)/None.gb", ROMS "/GB (GB)/Other.gb") == 0);
	CHECK(PlayHistory_move("/elsewhere/Tetris.gb", ROMS "/GB (GB)/Tetris.gb") == -1);
	CHECK(PlayHistory_move(ROMS "/GB (GB)/Tetris.gb", ROMS "/GB (GB)/Tetris.gb") == -1);
	CHECK(PlayHistory_move(NULL, ROMS "/GB (GB)/Tetris.gb") == -1);
	PlayHistory_invalidate();
}

// Every session with its game, oldest first.
static void testSessions(void) {
	makeFixture();
	PlayHistory_configure(DB, ROMS, onLog);
	long long now = (long long)time(NULL);
	CHECK(PlayHistory_sessionCount() == 6);
	CHECK(!PlayHistory_sessionsCapped());
	long long start = 0, seconds = 0;
	const PlayRom* rom = NULL;
	CHECK(PlayHistory_session(0, &start, &seconds, &rom) == 0);
	CHECK(start == midnight - 3 * 86400 && seconds == 50 && rom && rom->orphan && !rom->path);
	CHECK_STR(rom->name, "Metroid");
	CHECK(PlayHistory_session(1, &start, &seconds, &rom) == 0);
	CHECK(start == midnight - 7200 && seconds == 3600 && rom && rom->id == 1);
	CHECK_STR(rom->path, "GB (GB)/Tetris.gb");
	CHECK(PlayHistory_session(3, &start, &seconds, &rom) == 0);
	CHECK(start == midnight + 100 && seconds == 77 && rom->id == 5);
	// still open: up to now
	CHECK(PlayHistory_session(5, &start, &seconds, &rom) == 0);
	CHECK(start == midnight + 7200 && seconds >= now - start && seconds <= now - start + 5);
	CHECK(PlayHistory_session(6, &start, &seconds, &rom) == -1);
	CHECK(PlayHistory_session(-1, &start, &seconds, &rom) == -1);
	CHECK(strstr(last_log, "play history: 6 sessions with their games from " DB) != NULL);

	// capped to the newest
	PlayHistory_setCaps(0, 2);
	CHECK(PlayHistory_sessionCount() == 2);
	CHECK(PlayHistory_sessionsCapped());
	CHECK(PlayHistory_session(0, &start, NULL, NULL) == 0 && start == midnight + 3600);
	PlayHistory_setCaps(0, 0);

	unlink(DB);
	PlayHistory_invalidate();
	CHECK(PlayHistory_sessionCount() == PLAY_UNKNOWN);
	CHECK(PlayHistory_session(0, &start, &seconds, &rom) == -1);
}

// What's left in play_activity, as "rom_id:play_time" in rowid order, and
// the rom table's rows, to check a reset deleted exactly its own.
static char left[512];
static int addLeft(void* data, int columns, char** values, char** names) {
	(void)data;
	(void)names;
	size_t used = strlen(left);
	snprintf(left + used, sizeof(left) - used, "%s%s:%s", used ? " " : "", values[0] ? values[0] : "NULL",
		columns > 1 && values[1] ? values[1] : "NULL");
	return 0;
}

static const char* rowsLeft(const char* query) {
	left[0] = '\0';
	sqlite3* db = openWriter();
	char* error = NULL;
	CHECK(db_exec(db, query, (void*)addLeft, NULL, &error) == 0);
	db_close(db);
	return left;
}

#define SESSIONS_LEFT() rowsLeft("SELECT rom_id, play_time FROM play_activity ORDER BY rowid;")
#define ROMS_LEFT() rowsLeft("SELECT id, name FROM rom ORDER BY id;")

// Moving several paths in one transaction: a folder's .m3u and disc
// moved together, nothing else touched, nothing at all when one pair is
// bad or the database is locked, and 0 with no database.
static void testMovePaths(void) {
	makeFixture();
	PlayHistory_configure(DB, ROMS, onLog);
	sqlite3* db = openWriter();
	exec(db, "INSERT INTO rom(id, type, name, file_path) VALUES(6, '', 'FF7', 'PS (PS)/FF7/FF7.m3u');"
		"INSERT INTO rom(id, type, name, file_path) VALUES(7, '', 'FF7 (Disc 2)', 'PS (PS)/FF7/FF7 (Disc 2).cue');");
	session(db, 6, midnight - 50, "40");
	session(db, 7, midnight - 30, "20");
	db_close(db);
	const char* paths_before = rowsLeft("SELECT id, file_path FROM rom ORDER BY id;");
	char saved_paths[512];
	snprintf(saved_paths, sizeof(saved_paths), "%s", paths_before);
	const char* sessions_before = "1:3600 1:900 1:1200 2:NULL 3:50 5:77 6:40 7:20";
	CHECK_STR(SESSIONS_LEFT(), sessions_before);

	const char* from[] = { ROMS "/PS (PS)/FF7/FF7.m3u", ROMS "/PS (PS)/FF7/FF7 (Disc 2).cue", ROMS "/PS (PS)/FF7/FF7 (Disc 1).cue" };
	const char* to[] = { ROMS "/PS (PS)/Final/Final.m3u", ROMS "/PS (PS)/Final/FF7 (Disc 2).cue", ROMS "/PS (PS)/Final/FF7 (Disc 1).cue" };
	CHECK(PlayHistory_seconds(ROMS "/PS (PS)/FF7/FF7.m3u") == 40);

	// a bad pair: nothing moves
	const char* bad_to[] = { ROMS "/PS (PS)/Final/Final.m3u", "/elsewhere/x.cue", ROMS "/PS (PS)/Final/FF7 (Disc 1).cue" };
	CHECK(PlayHistory_movePaths(from, bad_to, 3) == -1);
	const char* same_to[] = { ROMS "/PS (PS)/Final/Final.m3u", ROMS "/PS (PS)/FF7/FF7 (Disc 2).cue", ROMS "/PS (PS)/Final/FF7 (Disc 1).cue" };
	CHECK(PlayHistory_movePaths(from, same_to, 3) == -1);
	CHECK(PlayHistory_movePaths(from, to, 0) == -1);
	CHECK(PlayHistory_movePaths(NULL, to, 3) == -1);
	CHECK_STR(rowsLeft("SELECT id, file_path FROM rom ORDER BY id;"), saved_paths);

	// locked by gametimectl: it gives up and changes nothing
	sqlite3* writer = openWriter();
	exec(writer, "BEGIN IMMEDIATE; INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 10, 1);");
	time_t started = time(NULL);
	CHECK(PlayHistory_movePaths(from, to, 3) == -1);
	CHECK(time(NULL) - started <= 3);
	CHECK(strstr(last_log, "play history: couldn't move 3 paths from PS (PS)/FF7/FF7.m3u") != NULL);
	exec(writer, "ROLLBACK;");
	db_close(writer);
	CHECK_STR(rowsLeft("SELECT id, file_path FROM rom ORDER BY id;"), saved_paths);

	// both rows moved and renamed as gametimectl names them; the third had none
	CHECK(PlayHistory_movePaths(from, to, 3) == 2);
	CHECK(!PlayHistory_loaded());
	CHECK(strstr(last_log, "play history: moved 2 of 3 paths from PS (PS)/FF7/FF7.m3u") != NULL);
	CHECK_STR(rowsLeft("SELECT id, file_path FROM rom WHERE id >= 6 ORDER BY id;"), "6:PS (PS)/Final/Final.m3u 7:PS (PS)/Final/FF7 (Disc 2).cue");
	CHECK_STR(rowsLeft("SELECT id, file_path FROM rom WHERE id < 6 ORDER BY id;"),
		"1:GB (GB)/Tetris.gb 2:GBA (MGBA)/Zelda.gba 3:NULL 4:NES (FCEUMM)/Unplayed.nes 5:GB (GB)/Tetris.gb");
	CHECK_STR(ROMS_LEFT(), "1:Tetris 2:Zelda 3:Metroid 4:Unplayed 5:Tetris again 6:Final 7:FF7 (Disc 2)");
	CHECK_STR(SESSIONS_LEFT(), sessions_before);
	CHECK(PlayHistory_seconds(ROMS "/PS (PS)/Final/Final.m3u") == 40);
	CHECK(PlayHistory_seconds(ROMS "/PS (PS)/FF7/FF7.m3u") == 0);

	// onto a path that has a row already: its sessions join it
	const char* tetris[] = { ROMS "/GB (GB)/Tetris.gb" };
	const char* unplayed[] = { ROMS "/NES (FCEUMM)/Unplayed.nes" };
	CHECK(PlayHistory_movePaths(tetris, unplayed, 1) == 1);
	CHECK(PlayHistory_seconds(ROMS "/NES (FCEUMM)/Unplayed.nes") == 3600 + 900 + 1200 + 77);

	// no database: nothing to move, and none made
	unlink(DB);
	CHECK(PlayHistory_movePaths(from, to, 3) == 0);
	CHECK(access(DB, F_OK) != 0);
	CHECK(PlayHistory_movePaths(from, bad_to, 3) == -1);
	// an empty file, before gametimectl makes its tables
	FILE* f = fopen(DB, "w");
	fclose(f);
	CHECK(PlayHistory_movePaths(from, to, 3) == -1);
	unlink(DB);
	PlayHistory_invalidate();
}

// Resetting play time: one game's sessions, every one of its rows', and
// nothing else; every session; and nothing at all when the database is
// missing, locked or outside Roms.
static void testForget(void) {
	makeFixture();
	PlayHistory_configure(DB, ROMS, onLog);
	CHECK_STR(SESSIONS_LEFT(), "1:3600 1:900 1:1200 2:NULL 3:50 5:77");
	const char* roms_before = "1:Tetris 2:Zelda 3:Metroid 4:Unplayed 5:Tetris again";
	CHECK_STR(ROMS_LEFT(), roms_before);
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == 5700);

	// both of Tetris's rows' sessions go, and nothing else
	CHECK(PlayHistory_forget(ROMS "/GB (GB)/Tetris.gb") == 4);
	CHECK(!PlayHistory_loaded());
	CHECK(strstr(last_log, "play history: forgot GB (GB)/Tetris.gb (4 sessions)") != NULL);
	CHECK_STR(SESSIONS_LEFT(), "2:NULL 3:50");
	CHECK_STR(ROMS_LEFT(), roms_before);
	CHECK(PlayHistory_seconds(ROMS "/GB (GB)/Tetris.gb") == 0);
	CHECK(PlayHistory_count(ROMS "/GB (GB)/Tetris.gb") == 0);
	// again: none left
	CHECK(PlayHistory_forget(ROMS "/GB (GB)/Tetris.gb") == 0);
	// a game with no row, matched to the orphan of its name, as play_time is
	CHECK(PlayHistory_seconds(ROMS "/NES (FCEUMM)/Metroid.nes") == 50);
	CHECK(PlayHistory_forget(ROMS "/NES (FCEUMM)/Metroid.nes") == 1);
	CHECK_STR(SESSIONS_LEFT(), "2:NULL");
	CHECK(PlayHistory_forget(ROMS "/GB (GB)/Never.gb") == 0);
	CHECK(PlayHistory_forget("/elsewhere/Zelda.gba") == -1);
	CHECK_STR(SESSIONS_LEFT(), "2:NULL");

	// everything
	makeFixture();
	CHECK(PlayHistory_forget(NULL) == 6);
	CHECK(strstr(last_log, "play history: forgot every session (6 sessions)") != NULL);
	CHECK_STR(SESSIONS_LEFT(), "");
	CHECK_STR(ROMS_LEFT(), roms_before);
	CHECK(PlayHistory_today() == 0);
	CHECK(PlayHistory_gameCount() == 0);

	// gametimectl writing: it waits a little, then gives up and deletes nothing
	makeFixture();
	sqlite3* writer = openWriter();
	exec(writer, "BEGIN IMMEDIATE; INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(2, 10, 1);");
	time_t started = time(NULL);
	CHECK(PlayHistory_forget(ROMS "/GB (GB)/Tetris.gb") == -1);
	CHECK(PlayHistory_forget(NULL) == -1);
	CHECK(time(NULL) - started <= 3);
	CHECK(strstr(last_log, "play history: couldn't forget every session") != NULL);
	exec(writer, "COMMIT;");
	db_close(writer);
	CHECK_STR(SESSIONS_LEFT(), "1:3600 1:900 1:1200 2:NULL 3:50 5:77 2:10");
	CHECK(access(DB "-journal", F_OK) != 0);
	// and once it's done, the reset goes through
	CHECK(PlayHistory_forget(ROMS "/GBA (MGBA)/Zelda.gba") == 2);
	CHECK_STR(SESSIONS_LEFT(), "1:3600 1:900 1:1200 3:50 5:77");

	// no database: nothing made
	unlink(DB);
	CHECK(PlayHistory_forget(NULL) == -1);
	CHECK(PlayHistory_forget(ROMS "/GB (GB)/Tetris.gb") == -1);
	CHECK(access(DB, F_OK) != 0);
	// an empty file, before gametimectl makes its tables
	FILE* f = fopen(DB, "w");
	fclose(f);
	CHECK(PlayHistory_forget(NULL) == -1);
	CHECK(PlayHistory_forget(ROMS "/GB (GB)/Tetris.gb") == -1);
	unlink(DB);
	PlayHistory_invalidate();
}

int main(void) {
	mkdir(TEST_DIR, 0755);
	if (!bindWriter()) {
		fprintf(stderr, "no sqlite library to make the fixture with\n");
		return 1;
	}
	middayZone();
	testMissing();
	testRead();
	testReadOnly();
	testBusy();
	testBadFiles();
	testNoLibrary();
	testList();
	testCaps();
	testMove();
	testMovePaths();
	testSessions();
	testForget();
	testBig();
	PlayHistory_quit();
	CHECK_DONE();
}
