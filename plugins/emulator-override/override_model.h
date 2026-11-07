#ifndef OVERRIDE_MODEL_H
#define OVERRIDE_MODEL_H

#include <stddef.h>

// The emulator override's logic, with no SDL and no host: which game a path
// belongs to, which emulator paks are on the card and which suit a system,
// and the file of per-game choices. Paths are full paths on the card
// unless a comment says card-relative.

#define EO_PATH_LEN 1024
#define EO_NAME_LEN 64 // a pak's name, without .pak

///////////////////////////////////////
// games

// The game a path belongs to, as a card-relative key, and its system's tag
// (what's in the last parentheses of its Roms/ folder name, or the whole
// name, as stock reads it). A game folder (one with a .m3u or .cue named
// after it) and anything in a folder with such a .m3u are keyed by that
// file, so a game is the same whether it's opened from its folder, a disc
// or recents; anything else by its own path. Returns 1, or 0 for a path
// that isn't in a system folder under Roms/ or has an empty, "." or ".."
// part.
int EO_gameKey(const char* sdcard, const char* path, int is_dir, char* key, size_t key_size, char* tag, size_t tag_size);

///////////////////////////////////////
// emulator paks

// A name a pak can have here: not empty, under EO_NAME_LEN, not starting
// with a dot, and no slash, backslash or control character.
int EO_validName(const char* name);

// Where stock would find the pak called name: <sdcard>/Emus/<platform>/
// <name>.pak, then <sdcard>/.system/<platform>/paks/Emus/<name>.pak, the
// first that has a launch.sh. It must be a regular file that, once links
// and ".." are resolved, is still exactly <folder>/<name>.pak/launch.sh in
// the folder it was found in, and that folder must resolve to somewhere on
// the card. Writes the resolved launch.sh and returns 1, or returns 0.
int EO_pakLaunch(const char* sdcard, const char* platform, const char* name, char* out, size_t size);

// 1 when a pak called pak is known to run the games of the system tagged
// tag, from the overlaps between stock NextUI's own paks (GB, GBC, SGB and
// MGBA for Game Boy games, MGBA for GBA, SUPA and SFC for each other).
int EO_compatible(const char* tag, const char* pak);

// The paks EO_pakLaunch accepts, other than tag's own, sorted by name and
// each listed once: only those EO_compatible with tag, or with all every
// one. Writes at most max names (free each with free()) and returns how
// many.
int EO_paks(const char* sdcard, const char* platform, const char* tag, int all, char** names, int max);

///////////////////////////////////////
// the choices file

// Card-relative game key -> pak name. On disk, one per line: the key, a
// tab, the name.
typedef struct EOMap {
	char** keys;
	char** paks;
	int count;
	int capacity;
} EOMap;

void EO_mapInit(EOMap* map);
void EO_mapFree(EOMap* map);

// Replaces the map with the file's, skipping lines without a tab. A missing
// file is an empty map. Returns 0, or -1 when it can't be read.
int EO_mapLoad(EOMap* map, const char* file);
// Writes through a temporary file. Returns 0 or -1.
int EO_mapSave(const EOMap* map, const char* file);

const char* EO_mapGet(const EOMap* map, const char* key);
// Sets the pak for a key, or with NULL or "" drops it. Returns 1 when it
// changed, 0 when not, -1 for a key with a tab or newline, a name
// EO_validName refuses, or no memory.
int EO_mapSet(EOMap* map, const char* key, const char* pak);

#endif
