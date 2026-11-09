#ifndef SEARCH_MODEL_H
#define SEARCH_MODEL_H

#include <stddef.h>
#include <time.h>

// Search, without the host: the names games show as, folding names and
// queries so case and accents don't matter, matching and ranking games
// against a query, the order results keep, and recent queries. No SDL and
// no file access, so it's tested on the host.

#define SE_NAME_LEN 256
#define SE_QUERY_LEN 128 // the keyboard's longest text
#define SE_RECENTS_MAX 8

// The name stock shows for a file or folder name: no extensions (a dot
// and 2 to 4 more characters, as many as there are), nothing from the
// last ( or [ on, again until only a leading one is left, and no trailing
// spaces. A name that would end up empty keeps its extensionless form.
void SE_cleanName(const char* file_name, char* out, size_t size);

// Names stock leaves out of its lists: dot files, *.disabled and map.txt.
int SE_hidden(const char* name);

// Folds text for matching: ASCII letters in lower case, Latin letters
// with accents as their plain letters (é is e, ß is ss, æ is ae),
// combining accents and apostrophes dropped, so "Link's" is "links", and
// any other punctuation or space as one space between words, none at
// either end. Other UTF-8 characters, such as Japanese, are kept as they
// are. Writes as much as fits and returns the folded length.
size_t SE_fold(const char* text, char* out, size_t size);

///////////////////////////////////////
// map.txt

typedef struct SEMapItem {
	const char* key;
	const char* value;
	int line;
} SEMapItem;

// A folder's map.txt ("file name<tab>name to show", one a line), read
// once and looked up by file name.
typedef struct SEMap {
	char* text;
	SEMapItem* items;
	int count;
} SEMap;

// Takes text, the whole file, and frees it with the map. Lines without a
// tab or with nothing after it are skipped, and for a file name listed
// twice the later line wins, as in stock. Returns 0, or -1 when out of
// memory (the map is then empty).
int SE_mapParse(SEMap* map, char* text);
// The name for a file name, or NULL.
const char* SE_mapGet(const SEMap* map, const char* file_name);
void SE_mapFree(SEMap* map);

///////////////////////////////////////
// the games a scan found

typedef struct SEGame {
	char* path;
	char* name;   // as the game list shows it
	char* folded; // name, folded
	int type;     // the host's entry type
} SEGame;

// A folder the scan read, and its modification time then, so a change on
// the card can be noticed without reading every folder again.
typedef struct SEFolder {
	char* path;
	time_t mtime;
} SEFolder;

typedef struct SEIndex {
	SEGame* games;
	int game_count;
	int game_cap;
	SEFolder* folders;
	int folder_count;
	int folder_cap;
} SEIndex;

void SE_indexInit(SEIndex* index);
// Frees everything and leaves it empty, ready to fill again.
void SE_indexFree(SEIndex* index);
// Both return 0, or -1 when out of memory.
int SE_indexAdd(SEIndex* index, const char* path, const char* name, int type);
int SE_indexAddFolder(SEIndex* index, const char* path, time_t mtime);

///////////////////////////////////////
// matching

enum {
	SE_RANK_EXACT,  // the name is the query
	SE_RANK_PREFIX, // the name starts with the query
	SE_RANK_WORDS,  // each word of the query starts a word of the name
	SE_RANK_INSIDE, // each word of the query is somewhere in the name
};

// How well a folded name matches a folded query: an SE_RANK_ value, or -1
// when it doesn't. The query's words can come in any order. An empty
// query matches nothing.
int SE_rank(const char* folded_name, const char* folded_query);

typedef struct SEHit {
	int game; // an index into the index's games
	int rank;
} SEHit;

// Every game matching query, which is folded here, best first: by rank,
// then by folded name, then by path. hits needs room for every game.
// Returns how many matched.
int SE_search(const SEIndex* index, const char* query, SEHit* hits);

///////////////////////////////////////
// the order results keep

typedef struct SEOrderItem {
	const char* path;
	int position;
} SEOrderItem;

// The paths of a search's results by where they came, so the list keeps
// its ranked order after another plugin renames an entry and the launcher
// sorts it by name. The paths are the index's, not copied.
typedef struct SEOrder {
	SEOrderItem* items;
	int count;
} SEOrder;

// Builds it from count hits. Returns 0, or -1 when out of memory (the
// order is then empty).
int SE_orderBuild(SEOrder* order, const SEIndex* index, const SEHit* hits, int count);
// Where a path came, or -1 for one that isn't a result.
int SE_orderOf(const SEOrder* order, const char* path);
void SE_orderFree(SEOrder* order);

///////////////////////////////////////
// recent queries

typedef struct SERecents {
	char items[SE_RECENTS_MAX][SE_QUERY_LEN];
	int count;
} SERecents;

// Trims spaces from both ends of a query, in place.
void SE_trim(char* query);
// Puts query first, dropping an older one that folds the same, and keeps
// at most max (up to SE_RECENTS_MAX). An empty query is left out.
void SE_recentsAdd(SERecents* recents, const char* query, int max);

#endif
