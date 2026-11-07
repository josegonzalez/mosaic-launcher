#ifndef JUMP_MODEL_H
#define JUMP_MODEL_H

#include <stddef.h>

// Jumping between systems: which system a path is in, the systems in the
// root's order, and where a jump lands. No SDL and no host here, so the
// host tests can check it on its own.

#define PJ_PATH_LEN 512

// The system key for a path inside <roms>/<folder>/, or <roms>/<folder>
// itself: the part of the folder name that stock collates on, up to and
// including its last "(", or the whole name without one, so
// "Game Boy (GB)" and "Game Boy (MGB)" are one system. Writes it and
// returns 1, or returns 0 for a path outside a system folder.
int PJ_systemKey(const char* roms, const char* path, char* out, size_t size);

// True when path is a system folder itself, <roms>/<folder>.
int PJ_isSystemDir(const char* roms, const char* path);

// Systems by key, in the order they were added, each with the folder to
// open for it. Adding a key that's already there keeps the first folder.
typedef struct PJSystems {
	char** keys;
	char** paths;
	int count;
	int capacity;
} PJSystems;

void PJ_systemsInit(PJSystems* s);
void PJ_systemsClear(PJSystems* s);

// Returns the system's index, or -1 when memory ran out.
int PJ_systemsAdd(PJSystems* s, const char* key, const char* path);

// The index of a key, or -1.
int PJ_systemsFind(const PJSystems* s, const char* key);

// How many groups rows fall in. groups holds one group id per row, or -1
// for a row that isn't in one.
int PJ_groupCount(const int* groups, int count);

// Where a jump from the selected row lands: the first row of the next
// group (dir > 0) or the previous one (dir < 0), with groups taken in the
// order they first appear. From a row outside every group, it's the next
// group to start below it or the last to start above it. Past the ends it
// wraps around when wrap is set. Returns the row, or -1 when there's
// nowhere to go.
int PJ_jump(const int* groups, int count, int selected, int dir, int wrap);

#endif
