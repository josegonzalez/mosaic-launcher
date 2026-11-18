// Smart collections, without the host: which franchise a game's name
// belongs to, the collections that come out of a scan, the collections
// rules make (built-in ones and the user's rules.txt, each a search
// query, matched by search's own model), and the set of collections the
// user dismissed. No SDL and no file access, so it's tested on the host.

#ifndef SMART_MODEL_H
#define SMART_MODEL_H

#include <stddef.h>
#include "search_model.h"

#define SM_MAX_FRANCHISES 128
#define SM_MAX_RULES 128
#define SM_MAX_COLLECTIONS (SM_MAX_FRANCHISES + SM_MAX_RULES)
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

// Lowercase ASCII words separated by single spaces: punctuation splits
// words, "é" counts as "e", other non-ASCII bytes stay as they are.
void SM_normalize(const char* in, char* out, size_t size);

// True when a normalized title holds a normalized keyword as whole words.
// A number right after the keyword still counts, so "sonic2" is Sonic.
int SM_hasKeyword(const char* title, const char* keyword);

// True when a normalized title belongs to the franchise.
int SM_inFranchise(const SMFranchise* franchise, const char* title);

///////////////////////////////////////
// the games the card index lists, grouped by franchise

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
	char names[SM_MAX_COLLECTIONS][SM_NAME_LEN];
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

///////////////////////////////////////
// rules: collections a search query makes

// The kinds of rule, in the order the folder lists them.
enum {
	SM_RULE_NEVER,  // never played: unplayed
	SM_RULE_RECENT, // played in the last number days: played:<number>
	SM_RULE_LONG,   // played over number hours: hours:<number>
	SM_RULE_ADDED,  // added in the last number days: added:<number>
	SM_RULE_USER,   // a line of the user's rules.txt
	SM_RULE_DECADE, // the games from the decade starting in number: decade:<number>s
	SM_RULE_GENRE,  // the games of a genre: genre:<the genre, compacted>
};

typedef struct SMRule {
	char name[SM_NAME_LEN];   // its folder's and its set's name
	char query[SE_QUERY_LEN]; // search's
	int kind;                 // an SM_RULE_ value
	int number;               // its days, hours or first year, else 0
} SMRule;

typedef struct SMRules {
	SMRule items[SM_MAX_RULES];
	int count;
	int dropped; // rules past SM_MAX_RULES, left out
} SMRules;

void SM_rulesInit(SMRules* rules);

// What SM_rulesAdd returns.
enum {
	SM_ADDED = 0,
	SM_TAKEN = -1, // a rule or franchise has the name (ignoring case)
	SM_FULL = -2,  // SM_MAX_RULES rules already; counted in dropped
};

// Adds a rule. Returns an SM_ value above.
int SM_rulesAdd(SMRules* rules, const SMCatalog* catalog, int kind, int number, const char* name, const char* query);

// The rule with this name (ignoring case), or -1.
int SM_ruleFind(const SMRules* rules, const char* name);

// Adds the built-in rule of kind (SM_RULE_NEVER to SM_RULE_ADDED) with
// its number, named Never played, Recently played, Long played or
// Recently added. Returns as SM_rulesAdd does.
int SM_rulesAddBuiltin(SMRules* rules, const SMCatalog* catalog, int kind, int number);

// Called for each line of rules.txt that's left out: its number from 1,
// why, and the line.
typedef void (*SMRuleError)(int line, const char* reason, const char* text, void* data);

// Adds the user's rules from rules.txt's text: a line each, the
// collection's name, '=' and a search query, such as "RPGs I haven't
// played = genre:rpg unplayed", both trimmed. Blank lines and lines
// starting with '#' are skipped. A line is left out, and told to error,
// when it has no '=', no name or query, a name longer than
// SM_NAME_LEN - 1 bytes, starting with '.' or holding '/' or ',', a
// query longer than SE_QUERY_LEN - 1 bytes, a term that starts as a
// filter but isn't one (see SE_parse), more than SE_FILTERS_MAX filters,
// or a name a rule or franchise already has, and when rules are full.
// Returns how many it added.
int SM_rulesParse(SMRules* rules, const SMCatalog* catalog, const char* text, SMRuleError error, void* data);

// The decades games' years fall in, each once, oldest first, written to
// out as their first years. Returns how many (at most max).
int SM_decades(const SEIndex* games, int* out, int max);

// The name a genre's collection has: the genre trimmed, with each run of
// '/', ',', ';' and '|' and the spaces around it as " - ", so "Action /
// Platform" is "Action - Platform". Returns 0, or -1 when there's
// nothing left or it's SM_NAME_LEN bytes or more.
int SM_genreName(const char* genre, char* out, size_t size);

// Adds a genre's rule to genres (kept apart, to be sorted): genre:<the
// genre compacted, see SE_compact>, named as SM_genreName says, once for
// each compacted genre. Returns as SM_rulesAdd does, or 1 when it's there
// already, or -3 when it has no name or its query doesn't fit.
int SM_genresAdd(SMRules* genres, const char* genre);

// Sorts rules by name, ignoring case.
void SM_rulesSort(SMRules* rules);

// The games each rule's query matches, by index into games, sorted by
// folded name then path. test answers the filters search's model doesn't
// (see SEFilterFn), with data.
typedef struct SMRuleHits {
	int* members[SM_MAX_RULES];
	int count[SM_MAX_RULES];
} SMRuleHits;

void SM_ruleHitsInit(SMRuleHits* hits);
void SM_ruleHitsFree(SMRuleHits* hits);
// Fills hits for every rule. Returns 0, or -1 when out of memory (the
// rules it couldn't fill are empty).
int SM_rulesMatch(const SMRules* rules, const SEIndex* games, SEFilterFn test, void* data, SMRuleHits* hits);

// A hash of the paths of count games, members by index into games, to
// tell whether a rule's set changed.
unsigned long SM_hitsHash(const SEIndex* games, const int* members, int count);

#endif
