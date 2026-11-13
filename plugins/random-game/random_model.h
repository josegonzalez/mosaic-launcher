#ifndef RANDOM_MODEL_H
#define RANDOM_MODEL_H

#include <stddef.h>

// Picking a random game: the games found on the card, which of them are
// in the set picked from, the weights from which are played, and a
// weighted pick. No SDL and no host here, so the
// host tests can check it on its own with a random source of their own.

#define RG_PATH_LEN 512
#define RG_UNPLAYED_WEIGHT 4 // how much likelier an unplayed game is

// One game: a file, a game folder (one with a .cue or .m3u named after
// itself) or a pak, and the system it's in, as an index into systems.
typedef struct RGGame {
	char* path;
	int type; // an enum MosaicEntryType
	int system;
} RGGame;

// Every game found, and the systems they're in. A system is the part of a
// Roms/ folder name that stock collates on: up to and including its last
// "(", or the whole name without one, so "Game Boy (GB)" and
// "Game Boy (MGB)" are one system.
typedef struct RGLibrary {
	RGGame* games;
	int count;
	int capacity;
	char** systems;
	int system_count;
	int system_capacity;
} RGLibrary;

void RG_init(RGLibrary* lib);
void RG_free(RGLibrary* lib);

// Adds a game to a system (a Roms/ folder name). Returns 0, or -1 when
// memory ran out.
int RG_add(RGLibrary* lib, const char* path, int type, const char* folder);

// The system key for a Roms/ folder name.
void RG_systemKey(const char* folder, char* out, size_t size);

// The system index for a path inside <roms>/<folder>/, or -1 when it isn't
// in one or that system has no games.
int RG_systemOf(const RGLibrary* lib, const char* roms, const char* path);

// Names stock leaves out of its lists: dot files, .disabled and map.txt.
int RG_hiddenName(const char* name);

// Looks up a file name in a map.txt's text (file name, a tab, the name to
// show, one per line). Writes the name and returns 1, or returns 0.
int RG_mapLookup(const char* map, const char* filename, char* out, size_t size);

// Full paths, kept sorted for quick lookups.
typedef struct RGPathSet {
	char** paths;
	int count;
	int capacity;
} RGPathSet;

void RG_setInit(RGPathSet* set);
void RG_setClear(RGPathSet* set);

int RG_setAdd(RGPathSet* set, const char* path);
int RG_setHas(const RGPathSet* set, const char* path);

// True when set has the game at path: the path itself, or for a game
// folder (type MOSAIC_ENTRY_DIR) the .m3u or .cue named after it inside,
// as stock records one.
int RG_inSet(const RGPathSet* set, const char* path, int type);

// The "from" setting's sets: every one but hidden, whose games are never
// picked. 1 when name can be picked from.
int RG_pickable(const char* name);

// What the pick from list shows for a set: fav for favorites, tag (a
// printf format with one %s) with the name for tags:<name>, smart the same
// for smart:<name>, and other names as they are.
void RG_setLabel(const char* name, const char* fav, const char* tag, const char* smart, char* out, size_t size);

// True when a game was played by recents alone, the fallback without
// row-badges' games.played: one of recents (full paths, as the game
// switcher keeps them) is it, or is inside it, as a game folder's .m3u or
// .cue is.
int RG_isPlayed(const char* path, const char* const* recents, int count);

// Whether a game was played: 1 or 0.
typedef int (*RGPlayedFn)(const RGGame* game, void* data);

// Fills weights (one per game) for a pick from system, or from every
// system with -1, and from the games in only, or every game with NULL: 0
// for games outside them, unplayed_weight for unplayed ones and 1 for
// played ones, or 0 for played ones with unplayed_only. played is asked
// only about games in the pick, and only when the weights or
// unplayed_only need it; with a NULL played every game counts as
// unplayed. Returns the total.
unsigned long RG_weights(const RGLibrary* lib, int system, const RGPathSet* only, RGPlayedFn played, void* data,
	unsigned unplayed_weight, int unplayed_only, unsigned* weights);

// A random source: returns a number from 0 to below - 1.
typedef unsigned long (*RGRandom)(unsigned long below, void* data);

// Picks an index with a chance in proportion to its weight, so with even
// weights a system with more games is picked more often. Returns -1 when
// the total is 0.
int RG_pick(const unsigned* weights, int count, unsigned long total, RGRandom random, void* data);

// A small generator for the plugin (xorshift64): state is the seed, then
// kept between calls. A zero seed is replaced with a fixed one.
unsigned long long RG_xorshift(unsigned long long* state);

#endif
