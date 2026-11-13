// Row badges, without the host: which games were played, going by the
// files stock and minarch leave behind, the tier an .info sidecar gives,
// and where the dots go in a row. No SDL and no file access, so it's
// tested on the host.

#ifndef BADGES_MODEL_H
#define BADGES_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

#define RB_MAX_TIER 5   // the most tier dots a row gets
#define RB_MAX_KEYS 20000 // the most played games kept

///////////////////////////////////////
// played games, as a set of "<tag>/<file name>" keys, such as
// "GB/Tetris.gb" for <roms>/Game Boy (GB)/Tetris.gb

typedef struct RBSet {
	char** keys;
	int count;
	int cap;
} RBSet;

void RB_setInit(RBSet* set);
// Frees everything and leaves it empty, ready to fill again.
void RB_setFree(RBSet* set);
// Adds a game by its system's tag and its file name. Returns 0, or -1 when
// out of memory, past RB_MAX_KEYS, or for an empty part or one holding a
// slash.
int RB_setAdd(RBSet* set, const char* tag, const char* file);
// Sorts the keys and drops repeats; call it once filled, before RB_setHas.
void RB_setFinish(RBSet* set);
int RB_setHas(const RBSet* set, const char* tag, const char* file);

// The system tag stock files a game's saves, states and resume slots
// under: the system folder's name (the first folder below roms) or what's
// in its last parentheses, eg. GB for "Game Boy (GB)". Returns 1, or 0
// for a path that isn't a file inside a system folder (writing "").
int RB_tagOf(const char* roms, const char* path, char* out, size_t size);

#define RB_PATH_MAX 1024
#define RB_GAME_FILES 3 // the most files RB_gameFiles gives

// The files a game goes by, the paths stock records it under: path itself,
// and for a file inside a folder below the system folder, that folder's
// .m3u (<folder>/<folder name>.m3u), as stock resumes a disc of a
// multi-disc game by its .m3u; for a folder (is_dir 1), its
// <folder>/<folder name>.m3u and .cue instead, as stock opens a game
// folder. Writes up to max and returns how many, or 0 for a path that
// isn't inside a system folder.
int RB_gameFiles(const char* roms, const char* path, int is_dir, char out[][RB_PATH_MAX], int max);

// Whether a game was played, the one definition the games.played service
// gives every plugin: 1 when one of its files (RB_gameFiles) has its tag
// and file name in set (in recents, or with a save, state or resume slot
// on the card), or history(file, ctx), when given, returns non-zero for
// one (the play history has a session of it). 0 when none does, and -1
// for a path that isn't inside a system folder.
int RB_isPlayed(const RBSet* set, const char* roms, const char* path, int is_dir,
	int (*history)(const char* path, void* ctx), void* ctx);

// RB_isPlayed for a file, by set alone.
int RB_played(const RBSet* set, const char* roms, const char* path);

// The games.played service's reply, "played=1" or "played=0" (for a
// path RB_isPlayed gives -1 too). Returns 0, or -1 when it doesn't fit.
int RB_reply(int played, char* out, size_t size);

// The game a file names, for each place played games leave a file. Each
// writes the game's file name and returns 1, or returns 0 for a file that
// isn't one of them.
//
// A file in .userdata/shared/.minui/<tag>/: "<game>.txt", minarch's last
// used state slot, or "<game>.<slot>.txt" and "<game>.<slot>.bmp", a
// state's name and preview.
int RB_slotFile(const char* name, char* out, size_t size);
// A file in .userdata/shared/<tag>-<core>/: "<game>.st<slot>", a state.
int RB_stateFile(const char* name, char* out, size_t size);
// A file in Saves/<tag>/: "<game>.sav", a game's own save.
int RB_saveFile(const char* name, char* out, size_t size);
// A folder in .userdata/shared/ named "<tag>-<core>", where minarch keeps
// a core's states: writes the tag and returns 1, or returns 0 for any
// other name, such as .minui.
int RB_stateDir(const char* name, char* out, size_t size);

///////////////////////////////////////
// tiers and dots

// How many tier dots an .info sidecar's tier value gives: a whole number
// from 1 to RB_MAX_TIER, with spaces around it allowed. 0 for anything
// else, such as "", "0", "6" or "B".
int RB_tier(const char* value);

// Lays out an unplayed dot (when unplayed is 1) and tier tier dots, in
// that order, each dot pixels square, gap apart, with twice the gap between
// the unplayed dot and the tier dots. With a name (w above 0), they go
// twice the gap after it, centred on it. Without one, as under a layout
// that doesn't say, or when they don't fit after it, they go in rect's top
// right corner, twice the gap in from both edges. Writes up to 1 + RB_MAX_TIER rects to out and returns how many,
// or 0 when there are none or they don't fit in rect.
int RB_place(const MosaicRect* rect, const MosaicRect* name, int dot, int gap, int unplayed, int tier,
	MosaicRect* out);

#endif
