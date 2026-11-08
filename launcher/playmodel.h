#ifndef PLAYMODEL_H
#define PLAYMODEL_H

// Play history as stock's play tracker keeps it, without SDL, sqlite or the
// card: a table of games read from game_logs.sqlite, looked up by a game's
// path, and the sessions that touch today, for today's total in local time.
// gametimectl keeps each game by its path relative to the Roms folder, and
// games carried over from Onion as orphans by name, which a lookup falls
// back to the way gametimectl does.

#include <stddef.h>

typedef struct PlayGame {
	char* key;          // the path relative to Roms, or an orphan's name
	long long seconds;  // played in all
	long long last;     // when its latest session started, as a Unix time, or 0
	int count;          // sessions
	int orphan;         // matched by name rather than by path
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
} PlayTable;

void PlayTable_init(PlayTable* table);
void PlayTable_free(PlayTable* table);

// Adds a game; key is copied. A NULL or empty key is skipped. Returns 0, or
// -1 when out of memory.
int PlayTable_addGame(PlayTable* table, const char* key, int orphan, long long seconds, long long last, int count);
int PlayTable_addSession(PlayTable* table, long long start, long long seconds);

// Sorts the games for PlayTable_find; call it once they're all added.
void PlayTable_finish(PlayTable* table);

// The game rel_path (relative to Roms) names, as gametimectl matches it:
// by path, or failing that an orphan named for the file without its
// extension, or with it. NULL when the tracker hasn't seen it.
const PlayGame* PlayTable_find(const PlayTable* table, const char* rel_path);

// A full path relative to roms_path, written to out: the part after
// "<roms_path>/". Returns 1, or 0 for a path outside it, writing "".
int Play_relPath(const char* roms_path, const char* path, char* out, size_t size);

// The local day now is in: its midnight, and the next day's, as Unix times.
void Play_dayBounds(long long now, long long* start, long long* end);

// How many seconds of the sessions fall between from and to.
long long Play_secondsBetween(const PlaySession* sessions, int count, long long from, long long to);

#endif
