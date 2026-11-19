#ifndef PLAYHISTORY_H
#define PLAYHISTORY_H

// Play history for plugins, read-only from stock's play tracker database
// (gametimectl's game_logs.sqlite). Nothing is read until a plugin first
// asks: then libsqlite3 is loaded with dlopen, the database is opened
// read-only with a busy timeout, every game's totals and the sessions that
// touch today are read in one go, and the database is closed again. The
// answers are kept until PlayHistory_invalidate, so asking every frame
// costs a lookup. When the library or the database is missing, or can't
// be read, every answer is PLAY_UNKNOWN until the next invalidate. Every
// session, for the time played on other days, is a read of its own, only
// once a plugin asks for a day.

#include <stddef.h>
#include "playmodel.h"

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

// The whole history, from the same read: how many games it lists (see
// PlayTable_list), or PLAY_UNKNOWN, and the one at index, or NULL.
// PlayHistory_gamePath writes a listed game's full path on the card to
// out, or "" for an orphan, and returns 1, or 0 for "".
int PlayHistory_gameCount(void);
const PlayGame* PlayHistory_game(int index);
int PlayHistory_gamePath(const PlayGame* game, char* out, size_t size);

// Seconds played across every game on the local day days_ago days before
// today (0 is today). Every session is read the first time it's asked,
// apart from the rest, and kept until the next invalidate. PLAY_UNKNOWN
// for a negative days_ago, a history that can't be read, or a day from
// before the newest PLAY_SESSIONS_MAX sessions when there are more.
long long PlayHistory_day(int days_ago);

// Moves a game's history from one full path on the card to another, such
// as after its files moved, opening the database read-write for it: its
// rows are given the new path, or, when the new path already has one, its
// sessions go to that one. Returns 1 when it moved any, 0 when from had
// none, or -1 for a path outside Roms, the same path twice, or a database
// that can't be written. Forgets the answers, as PlayHistory_invalidate.
int PlayHistory_move(const char* from, const char* to);

// Every session with its game, oldest first: how many there are, up to
// the newest PLAY_SESSIONS_MAX, or PLAY_UNKNOWN. Read the first time it's
// asked, in a read of its own after the rest, and kept until the next
// invalidate. PlayHistory_session writes the one at index's start and
// seconds, its game's row (NULL when the session's rom_id has none) and
// returns 0, or -1 past the end. PlayHistory_sessionsCapped says older
// sessions than those weren't read.
int PlayHistory_sessionCount(void);
int PlayHistory_session(int index, long long* start, long long* seconds, const PlayRom** rom);
int PlayHistory_sessionsCapped(void);

// Deletes a game's sessions, the play_activity rows of the rom rows
// PlayRoms_match finds for its full path, or with a NULL path every
// session, opening the database read-write for it, in one transaction.
// The rom table and every other table are left as they are. Returns how
// many sessions it deleted, or -1 for a path outside Roms, or a database
// that's missing, locked past the busy timeout or can't be written, when
// nothing is deleted. Forgets the answers, as PlayHistory_invalidate.
int PlayHistory_forget(const char* path);

// At most the PLAY_GAMES_MAX most played games are read, and the newest
// PLAY_SESSIONS_MAX sessions for PlayHistory_day.
#define PLAY_GAMES_MAX 10000
#define PLAY_SESSIONS_MAX 100000

// Other caps in their place, for tests; 0 puts one back.
void PlayHistory_setCaps(int games, int sessions);

// 1 once the database has been read (or found unreadable) since the last
// invalidate, and the same for every session, for tests.
int PlayHistory_loaded(void);
int PlayHistory_daysLoaded(void);

#endif
