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

#define BUSY_TIMEOUT_MS 250

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
} sql;

static char db_path[512];
static char roms_path[512];
static void (*say)(const char* message);

static int loaded = 0;   // read, or found unreadable, since the last invalidate
static int readable = 0; // and the answers are in table
static PlayTable table;
static long long today = 0;        // seconds today, as of the day below
static long long today_start = 0;  // the day today was summed for
static long long today_end = 0;

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

void PlayHistory_invalidate(void) {
	PlayTable_free(&table);
	loaded = 0;
	readable = 0;
	today = 0;
	today_start = today_end = 0;
}

void PlayHistory_quit(void) {
	PlayHistory_invalidate();
	if (sql.handle) dlclose(sql.handle);
	memset(&sql, 0, sizeof(sql));
}

int PlayHistory_loaded(void) {
	return loaded;
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

// Every game's totals. A session still open (gametimectl stop_all hasn't
// closed it yet) counts up to now, as stop_all would close it.
static int readGames(sqlite3* db, long long now) {
	char query[640];
	snprintf(query, sizeof(query),
		"SELECT rom.file_path, rom.name, rom.type, COUNT(pa.rom_id), "
		"SUM(CASE WHEN pa.play_time IS NULL THEN MAX(%lld - pa.created_at, 0) "
		"WHEN pa.play_time > 0 THEN pa.play_time ELSE 0 END), MAX(pa.created_at) "
		"FROM rom LEFT JOIN play_activity AS pa ON pa.rom_id = rom.id "
		"GROUP BY rom.id ORDER BY rom.id;",
		now);
	sqlite3_stmt* stmt = NULL;
	if (sql.prepare_v2(db, query, -1, &stmt, NULL) != SQL_OK) return 0;
	int rc;
	while ((rc = sql.step(stmt)) == SQL_ROW) {
		const char* path = text(stmt, 0);
		const char* name = text(stmt, 1);
		const char* type = text(stmt, 2);
		int orphan = type && !strcmp(type, "ORPHAN");
		long long last = sql.column_type(stmt, 5) == SQL_NULL ? 0 : sql.column_int64(stmt, 5);
		long long seconds = sql.column_int64(stmt, 4);
		int count = (int)sql.column_int64(stmt, 3);
		if (PlayTable_addGame(&table, path, 0, seconds, last, count)) break;
		if (orphan && PlayTable_addGame(&table, name, 1, seconds, last, count)) break;
	}
	sql.finalize(stmt);
	if (rc != SQL_DONE) return 0;
	PlayTable_finish(&table);
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

static void load(void) {
	if (loaded) return;
	loaded = 1;
	struct stat st;
	if (!db_path[0] || stat(db_path, &st) != 0 || !S_ISREG(st.st_mode)) {
		note("play history: unknown, no %s", db_path[0] ? db_path : "database");
		return;
	}
	if (!loadLibrary()) return;

	sqlite3* db = NULL;
	if (sql.open_v2(db_path, &db, SQL_OPEN_READONLY, NULL) != SQL_OK) {
		note("play history: unknown, can't open %s: %s", db_path, db ? sql.errmsg(db) : "out of memory");
		if (db) sql.close(db);
		return;
	}
	sql.busy_timeout(db, BUSY_TIMEOUT_MS);
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

// Summed once a day: the sessions are fixed until the next read, so only
// midnight passing changes the answer. time() reads no files, unlike
// working out the day.
long long PlayHistory_today(void) {
	load();
	if (!readable) return PLAY_UNKNOWN;
	long long now = (long long)time(NULL);
	if (now < today_start || now >= today_end) {
		Play_dayBounds(now, &today_start, &today_end);
		today = Play_secondsBetween(table.sessions, table.session_count, today_start, today_end);
	}
	return today;
}
