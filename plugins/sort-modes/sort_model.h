#ifndef SORT_MODEL_H
#define SORT_MODEL_H

// The sort-modes plugin's SDL-free half: the modes, which lists they
// order, each entry's key and how two keys compare, the sections a mode
// gives, and the keys kept by path so a sort reads nothing per compare.

#include <stddef.h>

enum {
	SM_NAME,        // the launcher's own order
	SM_MOST_PLAYED, // the most time played first
	SM_LAST_PLAYED, // the latest session first
	SM_YEAR,        // the oldest year first
	SM_PUBLISHER,   // publishers A to Z
	SM_ADDED,       // the newest file first
	SM_MODES,
};

// The mode's id, as its setting saves it: "name", "most played", "last
// played", "year", "publisher" or "recently added".
const char* SM_modeId(int mode);
// Its English label, such as "Most played", to translate where it's shown.
const char* SM_modeLabel(int mode);
// The mode with that id, or -1.
int SM_modeFind(const char* id);
// The mode after it, back to name after the last.
int SM_modeNext(int mode);
// 1 when the mode reads the play history.
int SM_modeReadsPlay(int mode);
// 1 when the mode reads the card index's year or publisher.
int SM_modeReadsIndex(int mode);

// 1 for a list a mode orders: every list but the root, the Roms folder of
// systems, Recently Played, the Collections folder of collections, search's
// results (in their ranked order) and Tools.
int SM_sorts(const char* sdcard, const char* dir);

// 1 for a path inside a system folder in Roms, the only entries with a
// value.
int SM_inRoms(const char* sdcard, const char* path);

enum {
	SM_FOLDER, // a plain folder: first, in the launcher's order
	SM_GAME,   // a game, or anything else in a list
};

typedef struct SMKey {
	int kind;        // SM_FOLDER or SM_GAME
	int has;         // 1 when the game has a value for the mode
	long long value; // seconds played, a Unix time or a year
	char* text;      // the publisher, owned by the cache; NULL for the other modes
} SMKey;

// The order of a and b in mode: folders first, then games with a value,
// by it (most played, last played and recently added newest or most
// first, years oldest first, publishers A to Z ignoring case), then those
// without one, each by name ignoring case. Two folders compare 0, keeping
// the launcher's order, and so does any pair in SM_NAME.
int SM_compare(int mode, const SMKey* a, const SMKey* b, const char* name_a, const char* name_b);

// The year a .info year starts with, such as 1994 for "1994-02-01", or 0.
int SM_yearOf(const char* text);

// The section of an entry in mode, for jumping through the list: NULL
// with out written for one in its own words (a year, a publisher, or the
// year of an older time), an English label to translate (eg. "Today",
// "10h+", "No year"), or NULL with out empty for none (folders, and every
// entry in SM_NAME). now is the time the sections are worked out at, in
// the local time zone.
const char* SM_section(int mode, const SMKey* key, long long now, char* out, size_t size);

// The name of the game a disc is of, such as "Game (USA)" for "Game
// (USA) (Disc 1).chd": file without its one "(Disc N)", "(CD N)" or
// "(Disk N)" marker (with " of M" or not, any case), the space beside it
// and its extension. Returns 1, or 0 for a file with no marker or more
// than one, or a name that doesn't fit.
int SM_discGame(const char* file, char* out, size_t size);

// 1 when file is a disc of the game whose row is row, such as "Game
// (USA) (Disc 1).chd" for "Game (USA).m3u": its SM_discGame is row's name
// without its extension, ignoring case.
int SM_discOf(const char* row, const char* file);

// Keys by path.
typedef struct SMItem SMItem;
typedef struct SMCache {
	SMItem** buckets;
	size_t bucket_count;
	size_t count;
} SMCache;

void SM_cacheInit(SMCache* cache);
void SM_cacheClear(SMCache* cache);
// The key kept for path, or NULL.
const SMKey* SM_cacheGet(const SMCache* cache, const char* path);
// Keeps a copy of key (and its text) for path, replacing one there, and
// returns the copy, or NULL when memory ran out.
const SMKey* SM_cachePut(SMCache* cache, const char* path, const SMKey* key);

// Keys by the entry they're for, while one sort runs: the launcher keeps
// a list's entries in place while it's sorted, so a compare finds both
// keys without hashing their paths. Emptied before each listing, as a
// freed entry's address can come back as another's.
typedef struct SMMemo {
	const void** entries;
	const SMKey** keys;
	size_t room; // a power of 2, or 0
	size_t count;
} SMMemo;

void SM_memoInit(SMMemo* memo);
void SM_memoFree(SMMemo* memo);
// Forgets every entry, keeping the room.
void SM_memoEmpty(SMMemo* memo);
const SMKey* SM_memoGet(const SMMemo* memo, const void* entry);
// 0, or -1 when memory ran out, leaving it out.
int SM_memoPut(SMMemo* memo, const void* entry, const SMKey* key);

#endif
