// Smart collections, without the host: which franchise a game's name
// belongs to, the collections that come out of a scan, and the set of
// collections the user dismissed. No SDL and no file access, so it's
// tested on the host.

#ifndef SMART_MODEL_H
#define SMART_MODEL_H

#include <stddef.h>

#define SM_MAX_FRANCHISES 128
#define SM_MAX_KEYWORDS 4
#define SM_NAME_LEN 64

// A franchise: the name it shows as, and the words that put a game in it.
typedef struct SMFranchise {
	char name[SM_NAME_LEN];
	char keywords[SM_MAX_KEYWORDS][SM_NAME_LEN]; // normalized, see SM_normalize
	int keyword_count;
} SMFranchise;

typedef struct SMCatalog {
	SMFranchise items[SM_MAX_FRANCHISES];
	int count;
} SMCatalog;

// Empties the catalog.
void SM_catalogInit(SMCatalog* catalog);

// Adds the franchises that ship with the plugin.
void SM_catalogAddBuiltin(SMCatalog* catalog);

// Adds a comma-separated list of names, each its own keyword, eg. "Kirby,
// Bomberman". Names already in the catalog (ignoring case), empty names and
// names holding a slash are skipped. Returns how many were added.
int SM_catalogAddList(SMCatalog* catalog, const char* list);

// The name stock shows for a file or folder name: no extensions (1 to 4
// letters each), nothing from the first ( or [ on, no trailing spaces.
void SM_cleanName(const char* file_name, char* out, size_t size);

// Lowercase ASCII words separated by single spaces: punctuation splits
// words, "é" counts as "e", other non-ASCII bytes stay as they are.
void SM_normalize(const char* in, char* out, size_t size);

// True when a normalized title holds a normalized keyword as whole words.
// A number right after the keyword still counts, so "sonic2" is Sonic.
int SM_hasKeyword(const char* title, const char* keyword);

// True when a normalized title belongs to the franchise.
int SM_inFranchise(const SMFranchise* franchise, const char* title);

// The alias a map.txt gives a file name, or 0. map holds the whole file:
// one "file name<tab>alias" per line.
int SM_mapLookup(const char* map, const char* file_name, char* out, size_t size);

///////////////////////////////////////
// the games a scan found, grouped by franchise

typedef struct SMGame {
	char* path;
	char* name; // as the game list shows it
	int type;   // the host's entry type
} SMGame;

typedef struct SMIndex {
	SMGame* games;
	int game_count;
	int game_cap;
	int* members[SM_MAX_FRANCHISES]; // indexes into games, per catalog item
	int member_count[SM_MAX_FRANCHISES];
	int member_cap[SM_MAX_FRANCHISES];
} SMIndex;

void SM_indexInit(SMIndex* index);
void SM_indexFree(SMIndex* index);

// Files a game under every franchise its name belongs to. Games that
// belong to none aren't kept. Returns how many franchises it joined.
int SM_indexAdd(SMIndex* index, const SMCatalog* catalog, const char* path, const char* name, int type);

// Sorts each franchise's games by name, then path.
void SM_indexSort(SMIndex* index, const SMCatalog* catalog);

///////////////////////////////////////
// dismissed collections

typedef struct SMNameSet {
	char names[SM_MAX_FRANCHISES][SM_NAME_LEN];
	int count;
} SMNameSet;

// Reads a comma-separated list, as saved by SM_setFormat.
void SM_setParse(SMNameSet* set, const char* list);
void SM_setFormat(const SMNameSet* set, char* out, size_t size);
int SM_setHas(const SMNameSet* set, const char* name);
// Both return 1 when the set changed.
int SM_setAdd(SMNameSet* set, const char* name);
int SM_setRemove(SMNameSet* set, const char* name);

// The catalog items to show, sorted by name: those with at least min games
// that aren't dismissed. Returns how many it wrote to out (at most
// SM_MAX_FRANCHISES). With dismissed NULL, counts dismissed ones too.
int SM_visible(const SMIndex* index, const SMCatalog* catalog, int min, const SMNameSet* dismissed, int* out);

// The catalog item with this name (ignoring case), or -1.
int SM_find(const SMCatalog* catalog, const char* name);

#endif
