// All games, without the host: the names a game shows as, the games a
// scan found, how the root orders them against folders, and which games
// share a name with a game from another system. No SDL and no file
// access, so it's tested on the host.

#ifndef GAMES_MODEL_H
#define GAMES_MODEL_H

#include <stddef.h>
#include <time.h>

#define AG_NAME_LEN 256
#define AG_SYSTEMS_NAME "Systems"

// The name stock shows for a file or folder name: no extensions (a dot
// and 2 to 4 more characters, as many as there are), then nothing from
// the last ( or [ on, again and again until only a leading one is left,
// and no trailing spaces. A name that would end up empty keeps its
// extensionless form.
void AG_cleanName(const char* file_name, char* out, size_t size);

// A folder's map.txt, read once and looked up by file name, so a big map
// isn't searched line by line for every game.
typedef struct AGMapItem {
	const char* key;
	const char* value;
	int line;
} AGMapItem;

typedef struct AGMap {
	char* text;
	AGMapItem* items;
	int count;
} AGMap;

// Takes text, the whole file (one "file name<tab>alias" per line), and
// frees it with the map. Lines without a tab or with an empty alias are
// skipped, and for a file name listed twice the later line wins, as in
// stock. Returns 0, or -1 when out of memory (the map is then empty).
int AG_mapParse(AGMap* map, char* text);
// The alias for a file name, or NULL. A NULL map has none.
const char* AG_mapGet(const AGMap* map, const char* file_name);
void AG_mapFree(AGMap* map);

// Names stock leaves out of its lists: dot files, *.disabled and map.txt.
int AG_hidden(const char* name);

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
// the games a scan found

typedef struct AGGame {
	char* path;
	char* name; // as the game list shows it
	int type;   // the host's entry type
} AGGame;

// A folder the scan read, and its modification time then, so a change on
// the card can be noticed without reading every folder again.
typedef struct AGFolder {
	char* path;
	time_t mtime;
} AGFolder;

typedef struct AGIndex {
	AGGame* games;
	int game_count;
	int game_cap;
	AGFolder* folders;
	int folder_count;
	int folder_cap;
} AGIndex;

void AG_indexInit(AGIndex* index);
// Frees everything and leaves it empty, ready to fill again.
void AG_indexFree(AGIndex* index);
// Both return 0, or -1 when out of memory.
int AG_indexAdd(AGIndex* index, const char* path, const char* name, int type);
int AG_indexAddFolder(AGIndex* index, const char* path, time_t mtime);
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
