#ifndef RANDOM_MODEL_H
#define RANDOM_MODEL_H

#include <stddef.h>

// Picking a random game: the games found on the card, which of them are
// played or hidden, and a weighted pick. No SDL and no host here, so the
// host tests can check it on its own with a random source of their own.

#define RG_PATH_LEN 512
#define RG_UNPLAYED_WEIGHT 4 // how much likelier an unplayed game is

// One game: a file, a game folder (one with a .cue or .m3u named after
// itself) or a pak, and the system it's in, as an index into systems.
typedef struct RGGame {
	char* path;
	int type; // an enum NUIEntryType
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

// Replaces the set with a file's lines, read the way the hidden plugin
// reads its hidden.txt: blank lines and lines starting with # are skipped,
// and a trailing slash is dropped. A missing file is an empty set. Returns
// 0, or -1 when memory ran out.
int RG_setLoad(RGPathSet* set, const char* file);
int RG_setAdd(RGPathSet* set, const char* path);
int RG_setHas(const RGPathSet* set, const char* path);

// True when the hidden plugin hides path: it, or the folder it's in, is in
// the set.
int RG_isHidden(const RGPathSet* hidden, const char* path);

// True when a game was played: one of recents (full paths, as the game
// switcher keeps them) is it, or is inside it, as a game folder's .m3u or
// .cue is.
int RG_isPlayed(const char* path, const char* const* recents, int count);

// Fills weights (one per game) for a pick from system, or from every
// system with -1: 0 for games outside it or hidden, unplayed_weight for
// unplayed ones and 1 for played ones. Returns the total.
unsigned long RG_weights(const RGLibrary* lib, int system, const RGPathSet* hidden,
	const char* const* recents, int recent_count, unsigned unplayed_weight, unsigned* weights);

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
