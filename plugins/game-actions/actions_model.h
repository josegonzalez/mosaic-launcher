#ifndef ACTIONS_MODEL_H
#define ACTIONS_MODEL_H

#include <stddef.h>

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until GA_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct GAWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} GAWords;
void GA_setWords(const GAWords* words);

// The game actions' logic, with no SDL and no host: what deleting a game
// removes, editing collection files, and per-entry name overrides. Paths
// are full paths on the card unless a comment says card-relative.

#define GA_PATH_LEN 1024
#define GA_NAME_LEN 128

///////////////////////////////////////
// deleting a game

// What a path in a delete plan is, for the confirm message.
enum GAKind {
	GA_GAME,    // the rom, or the game's folder with everything in it
	GA_DISC,    // a file a loose .cue or .m3u lists, beside it
	GA_SAVE,    // Saves/<tag>/<name>.sav, .srm or .rtc
	GA_STATE,   // save states, .userdata/shared/<tag>-<core>/<name>.st0 and so on
	GA_SLOT,    // a state's preview and slot files, .userdata/shared/.minui/<tag>/<name>.txt and so on
	GA_ART,    // <dir>/.media/<name>.png
	GA_CONFIG,  // the game's own emulator settings, <userdata>/<tag>-<core>/<name>.cfg
	GA_KINDS,
};

typedef struct GAItem {
	char path[GA_PATH_LEN];
	int kind;   // an enum GAKind
	int is_dir; // only the game's folder
} GAItem;

typedef struct GAPlan {
	GAItem* items; // the game first, then its sidecars
	int count;
	int capacity;
	int counts[GA_KINDS];
	char name[GA_PATH_LEN]; // the game's file or folder name
} GAPlan;

void GA_planInit(GAPlan* plan);
void GA_planFree(GAPlan* plan);

// Where the game is on the card: card is the card's root, userdata its
// .userdata/<platform>. with_saves 0 leaves saves, states and settings.
typedef struct GACard {
	const char* sdcard;
	const char* userdata;
	int with_saves;
} GACard;

// The game a selected path belongs to: a game folder (one holding a .m3u
// or .cue named after itself) for the folder or anything in it, otherwise
// the file. Writes it, sets *is_dir and returns 1, or returns 0 for a path
// that isn't a game in a system folder under Roms/ (a pak, a system
// folder, a plain subfolder, or anything off the card or through a link).
int GA_gameFor(const char* sdcard, const char* path, char* out, size_t size, int* is_dir);

// Lists everything deleting the game at path would remove, as the first
// thing any delete does. Only files that exist go in, sidecars only when
// they're regular files or links (never folders) inside the card, and a
// sidecar another game could share (one with the same name in the same
// system, or art another entry in the folder uses) is left out. Returns 0,
// or -1 when path isn't a game GA_gameFor accepts or memory ran out.
int GA_planDelete(const GACard* card, const char* path, GAPlan* plan);

// The confirm message's second line, eg. "1 save, 2 states and its art",
// or "" with nothing beside the game.
void GA_planSummary(const GAPlan* plan, char* out, size_t size);

// Deletes what the plan lists, checking each path again first: a link is
// removed, never followed, and the game's folder is emptied without
// following links out of it. Returns how many paths in the plan were
// removed; *failed gets how many couldn't be.
int GA_runDelete(const GACard* card, const GAPlan* plan, int* failed);

// The system tag stock uses for a Roms/ folder name: what's in its last
// parentheses, or the whole name, eg. "Game Boy (GB)" -> "GB".
void GA_tagOf(const char* folder, char* out, size_t size);

///////////////////////////////////////
// collections

// What a collection records for a selected path, card-relative, the same
// as stock's recents: a game folder's .m3u (or .cue), a disc's .m3u, a pak
// or a rom in a system folder under Roms/. Returns 1, or 0 for anything
// else, such as a collection or a plain folder.
int GA_collectionPath(const char* sdcard, const char* path, char* out, size_t size);

// The collections in <sdcard>/Collections, by name without .txt, sorted.
// Returns how many were written, at most max. Free each with free().
int GA_collections(const char* sdcard, char** names, int max);

// A name a new collection can have: not empty, no slash, not starting with
// a dot, under GA_NAME_LEN.
int GA_validCollectionName(const char* name);

// Adds a card-relative path at the end of a collection file, creating it.
// Returns 1 when added, 0 when it was there already, -1 on an error.
int GA_collectionAdd(const char* file, const char* rel);

// Drops the lines naming rel, and with is_dir anything inside it, from a
// collection file. Returns how many went, or -1 on an error.
int GA_collectionRemove(const char* file, const char* rel, int is_dir);

///////////////////////////////////////
// name overrides

// Card-relative path -> display name. On disk, one per line: the path, a
// tab, the name.
typedef struct GANames {
	char** paths;
	char** names;
	int count;
	int capacity;
} GANames;

void GA_namesInit(GANames* names);
void GA_namesFree(GANames* names);

// Replaces the set with the file's. A missing file is an empty set.
int GA_namesLoad(GANames* names, const char* file);
// Writes through a temporary file. Returns 0 or -1.
int GA_namesSave(const GANames* names, const char* file);

const char* GA_namesGet(const GANames* names, const char* rel);
// Sets a name, or with NULL or "" drops it. Names are trimmed of spaces
// and tabs and newlines become spaces. Returns 1 when it changed, 0 when
// not, -1 on an error.
int GA_namesSet(GANames* names, const char* rel, const char* name);
// Drops the overrides for rel, and with is_dir anything inside it. Returns
// how many went.
int GA_namesRemove(GANames* names, const char* rel, int is_dir);

#endif
