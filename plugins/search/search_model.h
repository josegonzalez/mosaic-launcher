#ifndef SEARCH_MODEL_H
#define SEARCH_MODEL_H

#include <stddef.h>

// Search, without the host: folding names and queries so case and
// accents don't matter, matching and ranking games against a query, the
// order results keep, and recent queries. No SDL and no file access, so
// it's tested on the host.

#define SE_NAME_LEN 256
#define SE_QUERY_LEN 128 // the keyboard's longest text
#define SE_RECENTS_MAX 8

// Folds text for matching: ASCII letters in lower case, Latin letters
// with accents as their plain letters (é is e, ß is ss, æ is ae),
// combining accents and apostrophes dropped, so "Link's" is "links", and
// any other punctuation or space as one space between words, none at
// either end. Other UTF-8 characters, such as Japanese, are kept as they
// are. Writes as much as fits and returns the folded length.
size_t SE_fold(const char* text, char* out, size_t size);

///////////////////////////////////////
// the games, from the card index

typedef struct SEGame {
	char* path;
	char* name;   // as the game list shows it
	char* folded; // name, folded
	int type;     // the host's entry type
	char* genre;  // its .info genre, folded, or NULL for none
	int year;     // the year its .info year names (see SE_year), or 0 for none
} SEGame;

typedef struct SEIndex {
	SEGame* games;
	int game_count;
	int game_cap;
} SEIndex;

void SE_indexInit(SEIndex* index);
// Frees everything and leaves it empty, ready to fill again.
void SE_indexFree(SEIndex* index);
// Returns 0, or -1 when out of memory.
int SE_indexAdd(SEIndex* index, const char* path, const char* name, int type);
// The same, with the game's .info genre and year as the card index gives
// them (either can be NULL or "" for none), for genre:, year: and decade:.
int SE_indexAddInfo(SEIndex* index, const char* path, const char* name, int type, const char* genre, const char* year);

// The year a .info year names: its first four digits in a row, from 1900
// to 2099, so "1991" and "1991-03-21" are 1991, or 0 when it has none.
int SE_year(const char* text);

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
// tag's set), unplayed (not played), system:<name> (in that system),
// genre:<name> (its .info genre is that name, ignoring case, accents,
// spaces and punctuation, or has each of its words starting a word),
// year:<year> or year:<from>-<to> (its .info year is that one or in that
// range), decade:<decade> (its .info year is in that decade: 1990s, 1990
// and 90s are all 1990 to 1999, two digits under 50 being the 2000s),
// played:<days> (last played in the last that many days), hours:<hours>
// (played for more than that many hours in all) and added:<days> (put on
// the card in the last that many days). Days count back from now in 24
// hours each.
enum {
	SE_FILTER_FAV,
	SE_FILTER_TAG,
	SE_FILTER_UNPLAYED,
	SE_FILTER_SYSTEM,
	SE_FILTER_GENRE,
	SE_FILTER_YEAR,   // year: and decade:, as a range of years
	SE_FILTER_PLAYED, // played:
	SE_FILTER_HOURS,  // hours:
	SE_FILTER_ADDED,  // added:
};

#define SE_FILTERS_MAX 8
#define SE_NUMBER_MAX 100000 // the most days or hours a filter takes

typedef struct SEFilter {
	int kind;   // an SE_FILTER_ value
	int negate; // it had a leading '-'
	char arg[SE_QUERY_LEN]; // tag:'s, system:'s and genre:'s name, as typed
	int from, to; // year: and decade:'s years; played:, hours: and added:'s number in from
} SEFilter;

// A query split into the words a name has to match and its filters.
typedef struct SEQuery {
	char words[SE_QUERY_LEN]; // the rest of the terms, as typed, space-separated
	SEFilter filters[SE_FILTERS_MAX];
	int filter_count;
	int dropped;            // filters past SE_FILTERS_MAX, which went to words
	char bad[SE_QUERY_LEN]; // the first term that starts as a filter but isn't one, such as played:x, or ""
} SEQuery;

// Splits a query at its spaces into filters, matched ignoring case (fav:,
// tag:<name>, unplayed, system:<name>, genre:<name>, year:<year>,
// decade:<decade>, played:<days>, hours:<hours> and added:<days>, each
// with an optional leading '-'), and words, which is every other term,
// past SE_FILTERS_MAX filters too. A filter's name missing, or a number
// that isn't one from 1 to SE_NUMBER_MAX or a year that isn't one, makes
// the term a word, and the first such term is kept in bad.
void SE_parse(const char* query, SEQuery* out);

// Whether a filter needs the caller's test, as SE_search asks it: every
// kind but genre:, year: and decade:, which it answers from the index.
int SE_filterNeedsTest(int kind);

// Whether a played:, hours: or added: filter passes for value, before its
// negation: the last time played or the time added, as a Unix time, or
// the seconds played, with now the time it is. 0 and less, for not
// played or not known, never pass.
int SE_timeMatches(const SEFilter* f, long long value, long long now);

// What a played:, hours: or added: filter compares for a game: ask's
// answer for its path, or for a game folder (is_dir) whose answer is 0 or
// less, the first above 0 of its .m3u's and .cue's, as stock plays it.
long long SE_playValue(const SEGame* game, int is_dir, int kind, long long (*ask)(int kind, const char* path, void* data),
	void* data);

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
// and it passes every filter, negated ones failing it: genre:, year: and
// decade: as its genre and year say, and the rest as test says, played:
// and hours: after those and unplayed and added: last, as they cost the
// most. With filters and no words every game that passes them matches,
// as SE_RANK_EXACT. With neither, or filters that need test and no test,
// nothing matches. hits needs room for every game. Returns how many
// matched.
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
