// All games, without the host: the games it lists from the card index,
// how the root orders them against folders, and which games share a name
// with a game from another system. No SDL and no file access, so it's
// tested on the host.

#ifndef GAMES_MODEL_H
#define GAMES_MODEL_H

#include <stddef.h>

#define AG_NAME_LEN 256
#define AG_SYSTEMS_NAME "Systems"

// A system folder's emulator tag: what's in its last parentheses, eg. GB
// for "Game Boy (GB)", or the whole name when it has none.
void AG_emuTag(const char* system, char* out, size_t size);

// The system folder a path is in, eg. "Game Boy (GB)" for
// <roms>/Game Boy (GB)/Tetris.gb, written to out. Returns 1 only for a
// path inside a system folder, not the folder itself.
int AG_systemOf(const char* roms, const char* path, char* out, size_t size);

// For a launch file inside a game folder, <dir>/<name>/<name>.cue or .m3u,
// the folder's thumbnail as the system's list shows it:
// <dir>/.media/<name>.png. Returns 0 for any other path.
int AG_folderArt(const char* path, char* out, size_t size);

// Root order: anything that isn't a game (Systems and other plugins'
// folders) comes before the games, each kind keeping its order. Negative
// for a first, positive for b first, 0 for the same kind.
int AG_rootCompare(const char* roms, const char* a_path, const char* b_path);

///////////////////////////////////////
// the games the root lists

typedef struct AGGame {
	char* path;
	char* name; // as the game list shows it
	int type;   // the host's entry type
} AGGame;

typedef struct AGIndex {
	AGGame* games;
	int game_count;
	int game_cap;
} AGIndex;

void AG_indexInit(AGIndex* index);
// Frees everything and leaves it empty, ready to fill again.
void AG_indexFree(AGIndex* index);
// Returns 0, or -1 when out of memory.
int AG_indexAdd(AGIndex* index, const char* path, const char* name, int type);
// By name ignoring case, then by path, so games with the same name sit
// together.
void AG_indexSort(AGIndex* index);

///////////////////////////////////////
// telling games apart

// Marks which of count games need their system shown: those whose name
// another game has from a different system. names and systems are per
// game; tagged gets 1 or 0 for each. Names match exactly, as stock's
// duplicate check does. Returns how many were marked, or -1 when out of
// memory.
int AG_collisions(const char* const* names, const char* const* systems, int count, int* tagged);

#endif
