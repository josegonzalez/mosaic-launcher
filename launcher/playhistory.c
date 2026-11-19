#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "playhistory.h"
#include "playmodel.h"

// The few parts of sqlite's C API used here, looked up with dlopen so the
// launcher doesn't need sqlite's headers or a link to it, and starts
// without it as stock does. Their values are part of sqlite's stable ABI.
#define SQL_OK 0
#define SQL_ROW 100
#define SQL_DONE 101
#define SQL_NULL 5
#define SQL_OPEN_READONLY 0x00000001
#define SQL_OPEN_READWRITE 0x00000002

#define BUSY_TIMEOUT_MS 250

// Days back whose totals are kept once worked out, about a year's chart.
#define DAY_CACHE 400

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

static struct {
	void* handle;
	int (*open_v2)(const char* filename, sqlite3** db, int flags, const char* vfs);
	int (*close)(sqlite3* db);
	int (*busy_timeout)(sqlite3* db, int ms);
	int (*prepare_v2)(sqlite3* db, const char* sql, int bytes, sqlite3_stmt** stmt, const char** tail);
	int (*step)(sqlite3_stmt* stmt);
	int (*finalize)(sqlite3_stmt* stmt);
	int (*column_type)(sqlite3_stmt* stmt, int col);
	long long (*column_int64)(sqlite3_stmt* stmt, int col);
	const unsigned char* (*column_text)(sqlite3_stmt* stmt, int col);
	const char* (*errmsg)(sqlite3* db);
	int (*bind_text)(sqlite3_stmt* stmt, int index, const char* text, int bytes, void (*destructor)(void*));
	int (*changes)(sqlite3* db);
} sql;

static char db_path[512];
static char roms_path[512];
static void (*say)(const char* message);
static int games_max = PLAY_GAMES_MAX;
static int sessions_max = PLAY_SESSIONS_MAX;

static int loaded = 0;   // read, or found unreadable, since the last invalidate
static int readable = 0; // and the answers are in table
static PlayTable table;
static long long today = 0;        // seconds today, as of the day below
static long long today_start = 0;  // the day today was summed for
static long long today_end = 0;

static int days_loaded = 0;   // every session read, or found unreadable, since the last invalidate
static int days_readable = 0; // and they're in days
static PlayDays days;
static int log_loaded = 0;   // every session with its game read, or found unreadable, since the last invalidate
static int log_readable = 0; // and they're in entries and roms
static int log_capped = 0;
static PlayEntry* entries = NULL;
static int entry_count = 0;
static PlayRoms roms;
static long long day_cache[DAY_CACHE]; // each day back's total
static char day_cached[DAY_CACHE];     // once it's worked out

__attribute__((format(printf, 1, 2)))
static void note(const char* fmt, ...) {
	if (!say) return;
	char message[768];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	say(message);
}

void PlayHistory_configure(const char* db, const char* roms, void (*log)(const char* message)) {
	PlayHistory_invalidate();
	snprintf(db_path, sizeof(db_path), "%s", db ? db : "");
	snprintf(roms_path, sizeof(roms_path), "%s", roms ? roms : "");
	say = log;
}

void PlayHistory_setCaps(int games, int sessions) {
	PlayHistory_invalidate();
	games_max = games > 0 ? games : PLAY_GAMES_MAX;
	sessions_max = sessions > 0 ? sessions : PLAY_SESSIONS_MAX;
}

static void forgetDays(void) {
	memset(day_cached, 0, sizeof(day_cached));
}

void PlayHistory_invalidate(void) {
	PlayTable_free(&table);
	loaded = 0;
	readable = 0;
	today = 0;
	today_start = today_end = 0;
	PlayDays_free(&days);
	days_loaded = 0;
	days_readable = 0;
	forgetDays();
	free(entries);
	entries = NULL;
	entry_count = 0;
	PlayRoms_free(&roms);
	log_loaded = 0;
	log_readable = 0;
	log_capped = 0;
}

void PlayHistory_quit(void) {
	PlayHistory_invalidate();
	if (sql.handle) dlclose(sql.handle);
	memset(&sql, 0, sizeof(sql));
}

int PlayHistory_loaded(void) {
	return loaded;
}

int PlayHistory_daysLoaded(void) {
	return days_loaded;
}

#define BIND(field, name) \
	if (!(*(void**)&sql.field = dlsym(sql.handle, name))) return 0

static int bindLibrary(void) {
	BIND(open_v2, "sqlite3_open_v2");
	BIND(close, "sqlite3_close");
	BIND(busy_timeout, "sqlite3_busy_timeout");
	BIND(prepare_v2, "sqlite3_prepare_v2");
	BIND(step, "sqlite3_step");
	BIND(finalize, "sqlite3_finalize");
	BIND(column_type, "sqlite3_column_type");
	BIND(column_int64, "sqlite3_column_int64");
	BIND(column_text, "sqlite3_column_text");
	BIND(errmsg, "sqlite3_errmsg");
	BIND(bind_text, "sqlite3_bind_text");
	BIND(changes, "sqlite3_changes");
	return 1;
}

// MOSAIC_LAUNCHER_SQLITE names the library to load in place of the
// system's, for tests.
static int loadLibrary(void) {
	if (sql.handle) return 1;
	const char* names[] = {
#ifdef __APPLE__
		"libsqlite3.dylib", "/usr/lib/libsqlite3.dylib",
#else
		"libsqlite3.so.0", "libsqlite3.so",
#endif
	};
	const char* forced = getenv("MOSAIC_LAUNCHER_SQLITE");
	int count = (int)(sizeof(names) / sizeof(names[0]));
	for (int i = 0; i < count && !sql.handle; i++) {
		const char* name = forced && forced[0] ? forced : names[i];
		sql.handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
		if (forced && forced[0]) break;
	}
	if (!sql.handle) {
		const char* why = dlerror();
		note("play history: unknown, no sqlite library (%s)", why ? why : "not found");
		return 0;
	}
	if (!bindLibrary()) {
		note("play history: unknown, the sqlite library is missing a function");
		dlclose(sql.handle);
		memset(&sql, 0, sizeof(sql));
		return 0;
	}
	return 1;
}

static const char* text(sqlite3_stmt* stmt, int col) {
	return (const char*)sql.column_text(stmt, col);
}

static long long timeOrZero(sqlite3_stmt* stmt, int col) {
	return sql.column_type(stmt, col) == SQL_NULL ? 0 : sql.column_int64(stmt, col);
}

// One number, or -1.
static long long single(sqlite3* db, const char* query) {
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, query, -1, &stmt, NULL) != SQL_OK) return -1;
	long long value = sql.step(stmt) == SQL_ROW ? sql.column_int64(stmt, 0) : -1;
	sql.finalize(stmt);
	return value;
}

// Every game's totals, up to the most played games_max of them, in rom
// order so the first of two rows for one path wins as in gametimectl. A
// session still open (gametimectl stop_all hasn't closed it yet) counts up
// to now, as stop_all would close it. Picking the most played costs a
// sort, so it's only done past the cap.
static int readGames(sqlite3* db, long long now) {
	long long all = single(db, "SELECT COUNT(*) FROM rom;");
	if (all < 0) return 0;
	char most[80] = "";
	if (all > games_max) snprintf(most, sizeof(most), " ORDER BY total DESC, last DESC, rom.id LIMIT %i", games_max);
	char query[1024];
	snprintf(query, sizeof(query),
		"SELECT file_path, name, type, sessions, total, first, last FROM ("
		"SELECT rom.id AS id, rom.file_path AS file_path, rom.name AS name, rom.type AS type, "
		"COUNT(pa.rom_id) AS sessions, "
		"SUM(CASE WHEN pa.play_time IS NULL THEN MAX(%lld - pa.created_at, 0) "
		"WHEN pa.play_time > 0 THEN pa.play_time ELSE 0 END) AS total, "
		"MIN(pa.created_at) AS first, MAX(pa.created_at) AS last "
		"FROM rom LEFT JOIN play_activity AS pa ON pa.rom_id = rom.id "
		"GROUP BY rom.id%s) ORDER BY id;",
		now, most);
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, query, -1, &stmt, NULL) != SQL_OK) return 0;
	int rc;
	while ((rc = sql.step(stmt)) == SQL_ROW) {
		const char* type = text(stmt, 2);
		int orphan = type && !strcmp(type, "ORPHAN");
		int count = (int)sql.column_int64(stmt, 3);
		long long seconds = sql.column_int64(stmt, 4);
		if (PlayTable_addRom(&table, text(stmt, 0), text(stmt, 1), orphan, seconds, timeOrZero(stmt, 5),
			timeOrZero(stmt, 6), count)) break;
	}
	sql.finalize(stmt);
	if (rc != SQL_DONE) return 0;
	PlayTable_finish(&table);
	if (all > games_max) note("play history: kept the %i most played of %lld games", games_max, all);
	return 1;
}

// The sessions that run past since, the start of today, for its total and
// later days'.
static int readSessions(sqlite3* db, long long since, long long now) {
	char query[320];
	snprintf(query, sizeof(query),
		"SELECT created_at, play_time FROM play_activity WHERE created_at IS NOT NULL "
		"AND (play_time IS NULL OR created_at + play_time > %lld);",
		since);
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, query, -1, &stmt, NULL) != SQL_OK) return 0;
	int rc;
	while ((rc = sql.step(stmt)) == SQL_ROW) {
		long long start = sql.column_int64(stmt, 0);
		long long seconds = sql.column_type(stmt, 1) == SQL_NULL ? now - start : sql.column_int64(stmt, 1);
		if (PlayTable_addSession(&table, start, seconds)) break;
	}
	sql.finalize(stmt);
	return rc == SQL_DONE;
}

// Logs why it's unknown, and returns NULL, when it can't be opened.
static sqlite3* openDatabaseAs(int flags) {
	struct stat st;
	if (!db_path[0] || stat(db_path, &st) != 0 || !S_ISREG(st.st_mode)) {
		note("play history: unknown, no %s", db_path[0] ? db_path : "database");
		return NULL;
	}
	if (!loadLibrary()) return NULL;

	sqlite3* db = NULL;
	if (sql.open_v2(db_path, &db, flags, NULL) != SQL_OK) {
		note("play history: unknown, can't open %s: %s", db_path, db ? sql.errmsg(db) : "out of memory");
		if (db) sql.close(db);
		return NULL;
	}
	sql.busy_timeout(db, BUSY_TIMEOUT_MS);
	return db;
}

static sqlite3* openDatabase(void) {
	return openDatabaseAs(SQL_OPEN_READONLY);
}

static void load(void) {
	if (loaded) return;
	loaded = 1;
	sqlite3* db = openDatabase();
	if (!db) return;
	long long now = (long long)time(NULL);
	long long start, end;
	Play_dayBounds(now, &start, &end);
	int ok = readGames(db, now) && readSessions(db, start, now);
	if (!ok) {
		note("play history: unknown, can't read %s: %s", db_path, sql.errmsg(db));
		PlayTable_free(&table);
	}
	sql.close(db);
	if (!ok) return;
	readable = 1;
	note("play history: read %i game%s and %i session%s since midnight from %s", table.count,
		table.count == 1 ? "" : "s", table.session_count, table.session_count == 1 ? "" : "s", db_path);
}

// Every session, newest first up to sessions_max: one more says there
// were more.
static int readDays(sqlite3* db, long long now) {
	char query[256];
	snprintf(query, sizeof(query),
		"SELECT created_at, play_time FROM play_activity WHERE created_at IS NOT NULL "
		"ORDER BY created_at DESC LIMIT %i;",
		sessions_max + 1);
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, query, -1, &stmt, NULL) != SQL_OK) return 0;
	int rc;
	int capped = 0;
	while ((rc = sql.step(stmt)) == SQL_ROW) {
		if (days.count == sessions_max) {
			capped = 1;
			continue;
		}
		long long start = sql.column_int64(stmt, 0);
		long long seconds = sql.column_type(stmt, 1) == SQL_NULL ? now - start : sql.column_int64(stmt, 1);
		if (PlayDays_add(&days, start, seconds)) break;
	}
	sql.finalize(stmt);
	if (rc != SQL_DONE) return 0;
	PlayDays_finish(&days, capped);
	return 1;
}

// Only after the rest read: what's unknown there is unknown here, and
// logged once.
static void loadDays(void) {
	if (days_loaded) return;
	days_loaded = 1;
	if (!readable) return;
	sqlite3* db = openDatabase();
	if (!db) return;
	int ok = readDays(db, (long long)time(NULL));
	if (!ok) {
		note("play history: unknown days, can't read %s: %s", db_path, sql.errmsg(db));
		PlayDays_free(&days);
	}
	sql.close(db);
	if (!ok) return;
	days_readable = 1;
	if (days.capped) {
		note("play history: days from the newest %i sessions in %s, days from before them unknown", days.count, db_path);
	}
	else note("play history: days from %i session%s in %s", days.count, days.count == 1 ? "" : "s", db_path);
}

// After known(path).
static const PlayGame* find(const char* path) {
	char rel[512];
	if (!Play_relPath(roms_path, path, rel, sizeof(rel))) return NULL;
	return PlayTable_find(&table, rel);
}

static int known(const char* path) {
	if (!path) return 0;
	load();
	return readable;
}

long long PlayHistory_seconds(const char* path) {
	if (!known(path)) return PLAY_UNKNOWN;
	const PlayGame* game = find(path);
	return game ? game->seconds : 0;
}

long long PlayHistory_lastPlayed(const char* path) {
	if (!known(path)) return PLAY_UNKNOWN;
	const PlayGame* game = find(path);
	return game ? game->last : 0;
}

int PlayHistory_count(const char* path) {
	if (!known(path)) return PLAY_UNKNOWN;
	const PlayGame* game = find(path);
	return game ? game->count : 0;
}

// The day now is in, worked out again only once midnight passes: the
// sessions are fixed until the next read, so only that changes the
// answers. time() reads no files, unlike working out the day.
static void rollDay(long long now) {
	if (now >= today_start && now < today_end) return;
	Play_dayBounds(now, &today_start, &today_end);
	today = Play_secondsBetween(table.sessions, table.session_count, today_start, today_end);
	forgetDays();
}

long long PlayHistory_today(void) {
	load();
	if (!readable) return PLAY_UNKNOWN;
	rollDay((long long)time(NULL));
	return today;
}

int PlayHistory_gameCount(void) {
	load();
	if (!readable) return PLAY_UNKNOWN;
	return PlayTable_list(&table);
}

const PlayGame* PlayHistory_game(int index) {
	if (PlayHistory_gameCount() <= 0) return NULL;
	return PlayTable_listed(&table, index);
}

int PlayHistory_gamePath(const PlayGame* game, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!game || game->orphan) return 0;
	return Play_fullPath(roms_path, game->key, out, size);
}

long long PlayHistory_day(int days_ago) {
	if (days_ago < 0) return PLAY_UNKNOWN;
	load();
	loadDays();
	if (!days_readable) return PLAY_UNKNOWN;
	long long now = (long long)time(NULL);
	rollDay(now);
	if (days_ago < DAY_CACHE && day_cached[days_ago]) return day_cache[days_ago];
	long long start = today_start, end = today_end;
	if (days_ago) Play_dayBoundsAgo(today_start, days_ago, &start, &end);
	long long seconds = PlayDays_seconds(&days, start, end);
	if (seconds < 0) seconds = PLAY_UNKNOWN;
	if (days_ago < DAY_CACHE) {
		day_cache[days_ago] = seconds;
		day_cached[days_ago] = 1;
	}
	return seconds;
}

// Every rom row into into, for the sessions' games and for forgetting.
static int readRoms(sqlite3* db, PlayRoms* into) {
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, "SELECT id, file_path, name, type FROM rom;", -1, &stmt, NULL) != SQL_OK) return 0;
	int rc;
	while ((rc = sql.step(stmt)) == SQL_ROW) {
		const char* type = text(stmt, 3);
		if (PlayRoms_add(into, sql.column_int64(stmt, 0), text(stmt, 1), text(stmt, 2), type && !strcmp(type, "ORPHAN"))) break;
	}
	sql.finalize(stmt);
	if (rc != SQL_DONE) return 0;
	PlayRoms_finish(into);
	return 1;
}

// Every session with its rom_id, newest first up to sessions_max: one
// more says there were more.
static int readEntries(sqlite3* db, long long now) {
	char query[256];
	snprintf(query, sizeof(query),
		"SELECT created_at, play_time, rom_id FROM play_activity WHERE created_at IS NOT NULL "
		"ORDER BY created_at DESC LIMIT %i;",
		sessions_max + 1);
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, query, -1, &stmt, NULL) != SQL_OK) return 0;
	int rc, cap = 0;
	while ((rc = sql.step(stmt)) == SQL_ROW) {
		if (entry_count == sessions_max) {
			log_capped = 1;
			continue;
		}
		if (entry_count == cap) {
			int more = cap ? cap * 2 : 256;
			PlayEntry* grown = realloc(entries, more * sizeof(PlayEntry));
			if (!grown) break;
			entries = grown;
			cap = more;
		}
		PlayEntry* entry = &entries[entry_count++];
		entry->start = sql.column_int64(stmt, 0);
		entry->seconds = sql.column_type(stmt, 1) == SQL_NULL ? now - entry->start : sql.column_int64(stmt, 1);
		if (entry->seconds < 0) entry->seconds = 0;
		entry->rom = sql.column_type(stmt, 2) == SQL_NULL ? -1 : sql.column_int64(stmt, 2);
	}
	sql.finalize(stmt);
	if (rc != SQL_DONE) return 0;
	PlayEntries_sort(entries, entry_count);
	return 1;
}

// Only after the rest read, as loadDays.
static void loadLog(void) {
	if (log_loaded) return;
	log_loaded = 1;
	if (!readable) return;
	sqlite3* db = openDatabase();
	if (!db) return;
	int ok = readRoms(db, &roms) && readEntries(db, (long long)time(NULL));
	if (!ok) {
		note("play history: unknown sessions, can't read %s: %s", db_path, sql.errmsg(db));
		free(entries);
		entries = NULL;
		entry_count = 0;
		PlayRoms_free(&roms);
		log_capped = 0;
	}
	sql.close(db);
	if (!ok) return;
	log_readable = 1;
	note("play history: %i session%s with their games from %s%s", entry_count, entry_count == 1 ? "" : "s", db_path,
		log_capped ? ", older ones not read" : "");
}

int PlayHistory_sessionCount(void) {
	load();
	loadLog();
	return log_readable ? entry_count : PLAY_UNKNOWN;
}

int PlayHistory_session(int index, long long* start, long long* seconds, const PlayRom** rom) {
	if (PlayHistory_sessionCount() <= 0 || index < 0 || index >= entry_count) return -1;
	const PlayEntry* entry = &entries[index];
	if (start) *start = entry->start;
	if (seconds) *seconds = entry->seconds;
	if (rom) *rom = PlayRoms_byId(&roms, entry->rom);
	return 0;
}

int PlayHistory_sessionsCapped(void) {
	return PlayHistory_sessionCount() >= 0 && log_capped;
}

// Runs one statement with up to two text arguments. Returns the rows it
// changed, or -1.
static int run(sqlite3* db, const char* query, const char* a, const char* b) {
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, query, -1, &stmt, NULL) != SQL_OK) return -1;
	int ok = (!a || sql.bind_text(stmt, 1, a, -1, NULL) == SQL_OK) && (!b || sql.bind_text(stmt, 2, b, -1, NULL) == SQL_OK);
	ok = ok && sql.step(stmt) == SQL_DONE;
	sql.finalize(stmt);
	return ok ? sql.changes(db) : -1;
}

int PlayHistory_move(const char* from, const char* to) {
	char from_rel[512], to_rel[512];
	if (!from || !to || !Play_relPath(roms_path, from, from_rel, sizeof(from_rel))
			|| !Play_relPath(roms_path, to, to_rel, sizeof(to_rel)) || !strcmp(from_rel, to_rel)) return -1;
	sqlite3* db = openDatabaseAs(SQL_OPEN_READWRITE);
	if (!db) return -1;
	// the name gametimectl gives a game: its file name without the extension
	char name[512];
	const char* base = strrchr(to_rel, '/');
	snprintf(name, sizeof(name), "%s", base ? base + 1 : to_rel);
	char* dot = strrchr(name, '.');
	if (dot && dot != name) *dot = '\0';

	int moved = -1;
	if (run(db, "BEGIN IMMEDIATE;", NULL, NULL) >= 0) {
		sqlite3_stmt* stmt = NULL;
		int has_to = 0;
		if (sql.prepare_v2(db, "SELECT 1 FROM rom WHERE file_path = ?1 LIMIT 1;", -1, &stmt, NULL) == SQL_OK) {
			if (sql.bind_text(stmt, 1, to_rel, -1, NULL) == SQL_OK) has_to = sql.step(stmt) == SQL_ROW;
			sql.finalize(stmt);
		}
		// onto the first row the new path already has, as gametimectl would
		// find it, leaving the old row with no sessions; otherwise renamed
		if (has_to) {
			moved = run(db, "UPDATE play_activity SET rom_id = (SELECT id FROM rom WHERE file_path = ?2 ORDER BY id LIMIT 1) "
				"WHERE rom_id IN (SELECT id FROM rom WHERE file_path = ?1);", from_rel, to_rel);
		}
		else {
			moved = run(db, "UPDATE rom SET file_path = ?2 WHERE file_path = ?1;", from_rel, to_rel);
			if (moved > 0 && run(db, "UPDATE rom SET name = ?1 WHERE file_path = ?2;", name, to_rel) < 0) moved = -1;
		}
		if (run(db, moved < 0 ? "ROLLBACK;" : "COMMIT;", NULL, NULL) < 0) moved = -1;
	}
	if (moved < 0) note("play history: couldn't move %s to %s: %s", from_rel, to_rel, sql.errmsg(db));
	else note("play history: moved %s to %s (%i rows)", from_rel, to_rel, moved);
	sql.close(db);
	PlayHistory_invalidate();
	return moved < 0 ? -1 : moved > 0;
}

// The sessions of the rows in ids, or every session with none: -1 when a
// statement fails.
static int forgetRows(sqlite3* db, const long long* ids, int count, int all) {
	if (all) return run(db, "DELETE FROM play_activity;", NULL, NULL);
	int deleted = 0;
	for (int i = 0; i < count; i++) {
		char query[96];
		snprintf(query, sizeof(query), "DELETE FROM play_activity WHERE rom_id = %lld;", ids[i]);
		int rows = run(db, query, NULL, NULL);
		if (rows < 0) return -1;
		deleted += rows;
	}
	return deleted;
}

int PlayHistory_forget(const char* path) {
	char rel[512] = "";
	if (path && !Play_relPath(roms_path, path, rel, sizeof(rel))) return -1;
	sqlite3* db = openDatabaseAs(SQL_OPEN_READWRITE);
	if (!db) {
		PlayHistory_invalidate();
		return -1;
	}
	int deleted = -1;
	// IMMEDIATE takes the write lock before reading, so the rows matched are
	// the ones deleted, and a writer holding it past the busy timeout leaves
	// the database untouched
	if (run(db, "BEGIN IMMEDIATE;", NULL, NULL) >= 0) {
		PlayRoms rows;
		PlayRoms_init(&rows);
		if (!path || readRoms(db, &rows)) {
			int count = path ? PlayRoms_match(&rows, rel, NULL, 0) : 0;
			long long* ids = count ? malloc(count * sizeof(long long)) : NULL;
			if (!count || ids) {
				if (count) PlayRoms_match(&rows, rel, ids, count);
				deleted = forgetRows(db, ids, count, !path);
			}
			free(ids);
		}
		PlayRoms_free(&rows);
		if (run(db, deleted < 0 ? "ROLLBACK;" : "COMMIT;", NULL, NULL) < 0) {
			if (deleted >= 0) run(db, "ROLLBACK;", NULL, NULL);
			deleted = -1;
		}
	}
	if (deleted < 0) note("play history: couldn't forget %s: %s", path ? rel : "every session", sql.errmsg(db));
	else note("play history: forgot %s (%i session%s)", path ? rel : "every session", deleted, deleted == 1 ? "" : "s");
	sql.close(db);
	PlayHistory_invalidate();
	return deleted;
}
