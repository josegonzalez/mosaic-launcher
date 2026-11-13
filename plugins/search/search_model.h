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

///////////////////////////////////////
// filters

// What a term of a query can ask for besides the name, each one negated
// by a leading '-': fav: (in the favorites set), tag:<name> (in that
// tag's set), unplayed (not played) and system:<name> (in that system).
enum {
	SE_FILTER_FAV,
	SE_FILTER_TAG,
	SE_FILTER_UNPLAYED,
	SE_FILTER_SYSTEM,
};

#define SE_FILTERS_MAX 8

typedef struct SEFilter {
	int kind;   // an SE_FILTER_ value
	int negate; // it had a leading '-'
	char arg[SE_QUERY_LEN]; // tag:'s and system:'s name, as typed
} SEFilter;

// A query split into the words a name has to match and its filters.
typedef struct SEQuery {
	char words[SE_QUERY_LEN]; // the rest of the terms, as typed, space-separated
	SEFilter filters[SE_FILTERS_MAX];
	int filter_count;
} SEQuery;

// Splits a query at its spaces into filters, matched ignoring case (fav:,
// tag:<name>, unplayed and system:<name>, each with an optional leading
// '-'), and words, which is every other term, past SE_FILTERS_MAX
// filters too. A tag: or system: with no name is a word.
void SE_parse(const char* query, SEQuery* out);

// Text folded as SE_fold does, with its spaces dropped too, so "tag:beatemup"
// matches "Beat 'em Up".
void SE_compact(const char* text, char* out, size_t size);

// True when a set's name is tags:<name> for the name a tag: filter gives,
// both compacted.
int SE_tagMatches(const char* set_name, const char* arg);

// True when path is in a system folder of roms that a system: filter's
// name gives: the folder's tag, in its last parentheses, ignoring case
// (gb for "Game Boy (GB)"), or the start of its name before them,
// compacted (gameboy, or game for every "Game ..." system).
int SE_systemMatches(const char* roms, const char* path, const char* arg);

// Whether a game passes a filter, before its negation: 1 or 0.
typedef int (*SEFilterFn)(const SEGame* game, int filter, const SEFilter* f, void* data);

///////////////////////////////////////
// searching

typedef struct SEHit {
	int game; // an index into the index's games
	int rank;
} SEHit;

// Every game matching query, best first: by rank, then by folded name,
// then by path. A game matches when its name has every word (folded here)
// and it passes every filter as test says, negated ones failing it,
// unplayed ones asked last as they cost the most. With filters and no
// words every game that passes them matches, as SE_RANK_EXACT. With
// neither, or filters and no test, nothing matches. hits needs room for
// every game. Returns how many matched.
int SE_search(const SEIndex* index, const SEQuery* query, SEFilterFn test, void* data, SEHit* hits);

///////////////////////////////////////
// paths a set lists

// Full paths, sorted once filled, for asking a set's members quickly.
typedef struct SEPaths {
	char** items;
	int count;
	int cap;
	int sorted;
} SEPaths;

void SE_pathsInit(SEPaths* paths);
void SE_pathsFree(SEPaths* paths);
// Returns 0, or -1 when out of memory.
int SE_pathsAdd(SEPaths* paths, const char* path);
int SE_pathsHas(SEPaths* paths, const char* path);

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
