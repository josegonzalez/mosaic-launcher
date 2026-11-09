#ifndef PLAYMODEL_H
#define PLAYMODEL_H

// Play history as stock's play tracker keeps it, without SDL, sqlite or the
// card: a table of games read from game_logs.sqlite, looked up by a game's
// path, and the sessions that touch today, for today's total in local time.
// gametimectl keeps each game by its path relative to the Roms folder, and
// games carried over from Onion as orphans by name, which a lookup falls
// back to the way gametimectl does. For the whole history there's also the
// list of every game, most played first, and every session in start order
// for the time played on any day.

#include <stddef.h>

typedef struct PlayGame {
	char* key;          // the path relative to Roms, or an orphan's name
	char* name;         // the name gametimectl keeps for it, or NULL
	long long seconds;  // played in all
	long long first;    // when its first session started, as a Unix time, or 0
	long long last;     // when its latest session started, as a Unix time, or 0
	int count;          // sessions
	int orphan;         // matched by name rather than by path
	int alias;          // an orphan's name for a game also kept by its path: looked up, never listed
	int order;          // read order, so the first of two rows for one path wins
} PlayGame;

typedef struct PlaySession {
	long long start;   // a Unix time
	long long seconds; // how long it ran
} PlaySession;

typedef struct PlayTable {
	PlayGame* games;
	int count, cap;
	PlaySession* sessions;
	int session_count, session_cap;
	int sorted;
	const PlayGame** list; // PlayTable_list's games, once it's been asked
	int list_count;
	int listed;
} PlayTable;

void PlayTable_init(PlayTable* table);
void PlayTable_free(PlayTable* table);

// Adds a game; key is copied. A NULL or empty key is skipped. Returns 0, or
// -1 when out of memory.
int PlayTable_addGame(PlayTable* table, const char* key, int orphan, long long seconds, long long last, int count);
int PlayTable_addSession(PlayTable* table, long long start, long long seconds);

// Adds a row of gametimectl's rom table with its totals: by path, and an
// orphan by name too. A row with neither is skipped. Returns 0, or -1 when
// out of memory.
int PlayTable_addRom(PlayTable* table, const char* path, const char* name, int orphan, long long seconds,
	long long first, long long last, int count);

// Sorts the games for PlayTable_find; call it once they're all added.
void PlayTable_finish(PlayTable* table);

// The game rel_path (relative to Roms) names, as gametimectl matches it:
// by path, or failing that an orphan named for the file without its
// extension, or with it. NULL when the tracker hasn't seen it.
const PlayGame* PlayTable_find(const PlayTable* table, const char* rel_path);

// After PlayTable_finish: how many games the whole history lists, worked
// out on the first ask, or -1 when out of memory. One per game with a
// session: each path once, as the row PlayTable_find answers with, and
// orphans known only by name. Most played first, then the latest played,
// then in read order. PlayTable_listed returns the one at index, or NULL
// past the end.
int PlayTable_list(PlayTable* table);
const PlayGame* PlayTable_listed(const PlayTable* table, int index);

// A full path relative to roms_path, written to out: the part after
// "<roms_path>/". Returns 1, or 0 for a path outside it, writing "".
int Play_relPath(const char* roms_path, const char* path, char* out, size_t size);

// The other way: "<roms_path>/<rel_path>" in out. Returns 1, or 0 when it
// doesn't fit or either is missing, writing "".
int Play_fullPath(const char* roms_path, const char* rel_path, char* out, size_t size);

// The local day now is in: its midnight, and the next day's, as Unix times.
void Play_dayBounds(long long now, long long* start, long long* end);

// The same for the local day days_ago days before the one now is in.
void Play_dayBoundsAgo(long long now, int days_ago, long long* start, long long* end);

// How many seconds of the sessions fall between from and to.
long long Play_secondsBetween(const PlaySession* sessions, int count, long long from, long long to);

// Every session, for the time played on any day: added in any order, then
// sorted by start, so a day only looks at the sessions near it.
typedef struct PlayDays {
	PlaySession* sessions;
	int count, cap;
	long long longest; // the longest session, how far before a day one can start and still run into it
	long long cut;     // with capped, when the oldest session read started: older ones weren't read
	int capped;
	int sorted;
} PlayDays;

void PlayDays_init(PlayDays* days);
void PlayDays_free(PlayDays* days);
int PlayDays_add(PlayDays* days, long long start, long long seconds);

// Sorts the sessions; call it once they're all added. capped says older
// sessions than the ones added weren't read.
void PlayDays_finish(PlayDays* days, int capped);

// How many seconds of the sessions fall between from and to, or -1 for a
// window that starts at or before the cut, when the sessions it needs
// weren't all read.
long long PlayDays_seconds(const PlayDays* days, long long from, long long to);

#endif
