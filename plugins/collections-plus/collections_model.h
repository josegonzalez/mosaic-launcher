// Collections plus, without the host: the user's collection names and
// order, map.txt files, a collection file's own order and where a
// collection's art and map are. No SDL; file access only for reading the
// files named, so it's tested on the host.

#ifndef COLLECTIONS_MODEL_H
#define COLLECTIONS_MODEL_H

#include <stddef.h>

#define CP_NAMES_MAX 64
#define CP_NAME_LEN 128
#define CP_PATH_LEN 512

// Collection names from a setting, in the order given.
typedef struct CPNames {
	char items[CP_NAMES_MAX][CP_NAME_LEN];
	int count;
	int all; // the setting was "*"
} CPNames;

// Parses a comma-separated list of names, eg. "RPGs, Shooters", trimming
// the spaces around each and leaving out empty ones and repeats (ignoring
// case). A list of just "*" sets all instead. Names past CP_NAMES_MAX, or
// longer than CP_NAME_LEN - 1, are left out.
void CP_parseNames(const char* text, CPNames* out);

// Where a collection is in names: matched ignoring case against its file
// name without ".txt" (file, eg. "RPGs") or the name it shows (shown, eg.
// a map.txt alias; NULL to skip). -1 when it isn't there.
int CP_rank(const CPNames* names, const char* file, const char* shown);

// The sort_compare answer for two ranks: ranked ones first, by rank, and
// no opinion between two that aren't ranked.
int CP_compareRanks(int a, int b);

// True when path is a collection: a .txt anywhere below collections, the
// collections folder, eg. /mnt/SDCARD/Collections.
int CP_isCollection(const char* collections, const char* path);

// For a collection directly in the collections folder, writes its name,
// the file name without ".txt", and returns 1; otherwise 0.
int CP_topCollection(const char* collections, const char* path, char* out, size_t size);

// A map.txt: file name -> display name, one tab-separated pair a line, as
// stock reads them. The first line for a name wins.
typedef struct CPMap {
	char** keys;
	char** values;
	int count;
	int cap;
} CPMap;

void CP_mapInit(CPMap* map);
// Reads path into an empty map; returns 1 when the file was read.
int CP_mapLoad(CPMap* map, const char* path);
// The display name for a file name, or NULL.
const char* CP_mapGet(const CPMap* map, const char* key);
void CP_mapFree(CPMap* map);

// The map files a collection's own names can come from, most specific
// first: <dir>/.media/<name>.map.txt beside its art, then <dir>/map.txt
// when the collection is in a folder below the collections folder. Writes
// up to max paths and returns how many.
int CP_mapPaths(const char* collections, const char* path, char out[][CP_PATH_LEN], int max);

// A collection file's own order: its card-relative lines, eg.
// /Roms/GB/Tetris.gb, for looking up where each is.
typedef struct CPOrder {
	char** lines; // in file order
	int* sorted;  // indexes into lines, sorted by line, for lookups
	int count;
} CPOrder;

void CP_orderInit(CPOrder* order);
// Reads a collection file into an empty order; returns 1 when it was read.
int CP_orderLoad(CPOrder* order, const char* path);
// The first line that is rel, or -1.
int CP_orderIndex(const CPOrder* order, const char* rel);
void CP_orderFree(CPOrder* order);

// The images a collection's art can be, in order: <dir>/.media/<name>.png
// and <dir>/.media/<name>.jpg, for a collection <dir>/<name>.txt. Writes up
// to max paths and returns how many, 0 for a path that isn't a .txt.
int CP_artPaths(const char* path, char out[][CP_PATH_LEN], int max);

#endif
