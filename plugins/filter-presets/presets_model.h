#ifndef PRESETS_MODEL_H
#define PRESETS_MODEL_H

#include "search_model.h"

// Filter presets, without the host: the games of the folder being
// filtered, the presets offered for it, which of its games a preset
// keeps, and stepping from one preset to the next. Queries are search's
// own (search_model.c, built into this plugin), so a preset written as a
// query means what it means in search. No SDL and no file access, so it's
// tested on the host.

#define FP_PATH_LEN 512
#define FP_USER_MAX 4     // presets written as a query, in the settings
#define FP_PRESETS_MAX 63 // what choose takes, after All games

// The games of one folder, as its list showed them with no preset on.
typedef struct FPFolder {
	char dir[FP_PATH_LEN];
	SEIndex games; // path, name and type of each
	char** genres; // each game's genre, "" for none
	int* players;  // the most players each game's players value names, 0 for none
	int cap;
} FPFolder;

void FP_folderInit(FPFolder* folder, const char* dir);
// Frees everything and leaves it empty, for no folder.
void FP_folderFree(FPFolder* folder);
// Adds a game with its .info genre, players and year (any can be NULL).
// Returns 0, or -1 when out of memory.
int FP_folderAdd(FPFolder* folder, const char* path, const char* name, int type, const char* genre, const char* players,
	const char* year);

// The highest number in a players value, so "1-4" and "4" are 4, or 0
// when it has none.
int FP_players(const char* value);

enum {
	FP_QUERY,   // a search query: the built-in Unplayed and Favorites, and the user's
	FP_SAVE,    // games with a save state
	FP_GENRE,   // games of one genre
	FP_PLAYERS, // games for at least this many players
};

// What a preset is called, for the host to put in words.
enum {
	FP_LABEL_UNPLAYED,
	FP_LABEL_FAVORITES,
	FP_LABEL_SAVE,
	FP_LABEL_GENRE,   // with genre
	FP_LABEL_PLAYERS, // with players
	FP_LABEL_QUERY,   // the query itself
};

typedef struct FPPreset {
	int kind;  // an FP_ value above
	int label; // an FP_LABEL_ value
	char key[SE_NAME_LEN + 16]; // stays the same while the folder does, to find it again
	char query[SE_QUERY_LEN];   // FP_QUERY's
	char genre[SE_NAME_LEN];    // FP_GENRE's, as the first game with it has it
	int players;                // FP_PLAYERS'
} FPPreset;

// The presets for a folder, in the order L2 and R2 step through them:
// Unplayed, Favorites and Has save, each genre its games have (by name,
// ignoring case and accents), at least N players for each N above 1 that
// a game's players value reaches, smallest first, then the user's queries
// that aren't empty, trimmed. Writes up to max and returns how many.
int FP_presets(const FPFolder* folder, const char* const user[FP_USER_MAX], FPPreset* out, int max);

// The answers a preset needs from plugins, through the host.
typedef struct FPTests {
	// 1 when game is in the favorites set (fav:), or in the set of the tag
	// arg names (tag:<arg>)
	int (*in_set)(int filter_kind, const char* arg, const SEGame* game, void* data);
	int (*played)(const SEGame* game, void* data);
	int (*has_save)(const SEGame* game, void* data);
	const char* roms; // <card>/Roms, for system:
	void* data;
	// played:'s last played time, hours:' seconds played or added:'s time
	// on the card, for a path (see SE_playValue), or NULL for none
	long long (*play_value)(int filter_kind, const char* path, void* data);
	long long now; // the time it is, for played: and added:, or 0 for time()
} FPTests;

// Which of the folder's games a preset keeps: matched[i] is set to 1 or 0
// for each of them (it holds folder->games.game_count). Returns how many
// it keeps, or -1 when out of memory.
int FP_match(const FPFolder* folder, const FPPreset* preset, const FPTests* tests, unsigned char* matched);

// The preset after current (-1 for none) in the direction dir (1 or -1),
// past those keeps says keep no games, through none after the last and
// before the first: -1 for none, else an index below count.
int FP_step(int count, int current, int dir, int (*keeps)(int preset, void* data), void* data);

// The index of the preset with key, or -1.
int FP_find(const FPPreset* presets, int count, const char* key);

#endif
