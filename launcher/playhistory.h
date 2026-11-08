#ifndef PLAYHISTORY_H
#define PLAYHISTORY_H

// Play history for plugins, read-only from stock's play tracker database
// (gametimectl's game_logs.sqlite). Nothing is read until a plugin first
// asks: then libsqlite3 is loaded with dlopen, the database is opened
// read-only with a busy timeout, every game's totals and the sessions that
// touch today are read in one go, and the database is closed again. The
// answers are kept until PlayHistory_invalidate, so asking every frame
// costs a lookup. When the library or the database is missing, or can't
// be read, every answer is PLAY_UNKNOWN until the next invalidate.

#define PLAY_UNKNOWN -1

// Where to read from, and a log for what happened on each read. The
// launcher sets its own at start; tests point them at a fixture.
void PlayHistory_configure(const char* db_path, const char* roms_path, void (*log)(const char* message));

// Forgets the answers, so the next ask reads the database again, eg. after
// a game returns. Touches nothing on disk.
void PlayHistory_invalidate(void);

// Forgets the answers and unloads the library.
void PlayHistory_quit(void);

// For a game's full path on the card: seconds played in all, when its
// latest session started (a Unix time), and how many sessions it has. A
// game the tracker hasn't seen gets 0. PLAY_UNKNOWN for a NULL path or a
// history that can't be read.
long long PlayHistory_seconds(const char* path);
long long PlayHistory_lastPlayed(const char* path);
int PlayHistory_count(const char* path);

// Seconds played across every game since local midnight, or PLAY_UNKNOWN.
long long PlayHistory_today(void);

// 1 once the database has been read (or found unreadable) since the last
// invalidate, for tests.
int PlayHistory_loaded(void);

#endif
