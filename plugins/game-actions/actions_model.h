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

///////////////////////////////////////
// renaming a game's files

// The most paths a rename carries over in recents, sets and play history:
// the game, and for a game folder each file in it.
#define GA_CARRY_MAX 256
// The longest new file name, so its sidecars' names still fit.
#define GA_FILE_MAX 200

// One step of a rename, in order: from moved to to, or copied when another
// game uses the file too.
typedef struct GAMove {
	char from[GA_PATH_LEN];
	char to[GA_PATH_LEN];
	int kind; // an enum GAKind
	int copy;
} GAMove;

// Why a rename can't go ahead.
enum GARenameError {
	GA_RENAME_OK,
	GA_RENAME_GAME,   // the path isn't a game GA_gameFor accepts
	GA_RENAME_NAME,   // the new name can't be a file name
	GA_RENAME_SAME,   // the new name is the old one
	GA_RENAME_EXISTS, // clash is a target that's there already
	GA_RENAME_TAKEN,  // another game in the system has the name clash, so they'd share saves
	GA_RENAME_MEMORY,
};

typedef struct GARename {
	GAMove* moves; // the game first (a folder's .m3u and .cue, then the folder), then its sidecars
	int count;
	int capacity;
	int counts[GA_KINDS];
	GAMove* carries; // every path the game is known by, old and new, for recents, sets and play history
	int carry_count;
	int carry_capacity;
	char sdcard[GA_PATH_LEN]; // the card it's on
	char from[GA_PATH_LEN]; // the game, as it is
	char to[GA_PATH_LEN];   // and after
	int is_dir;
	char lists[2][2][256]; // a game folder's .m3u and .cue named after it: old name, new name
	int list_count;
	int error;               // an enum GARenameError
	char clash[GA_PATH_LEN]; // with GA_RENAME_EXISTS or GA_RENAME_TAKEN
} GARename;

void GA_renameInit(GARename* r);
void GA_renameFree(GARename* r);

// The part of a game's name the keyboard edits: a file's name without its
// extension, or a folder's whole name.
void GA_editableName(const char* path, int is_dir, char* out, size_t size);

// A name a game can be renamed to: not empty, not starting with a dot or
// space, no slash, backslash or control character, at most GA_FILE_MAX
// bytes with its extension.
int GA_validFileName(const char* name);

// Lists every step renaming the game at path to new_name (the editable
// part; a file keeps its extension) takes, and checks each target, before
// anything changes: the game (for a game folder, the .m3u and .cue named
// after it too), its art and .info in .media, and its saves, states,
// resume slot and previews and emulator settings under the name minarch
// launches it by, as GA_planDelete finds them. A sidecar another game
// uses too is copied, not moved, and one that's a link is moved as a link,
// never followed. Returns 0, or -1 with r->error saying why, and for a
// clash r->clash naming the first target that's there (one that's the
// same file, as when only the case changes, isn't a clash).
int GA_planRename(const GACard* card, const char* path, const char* new_name, GARename* r);

// What happened in GA_runRename or GA_undoRename.
typedef struct GARenameResult {
	char failed[GA_PATH_LEN]; // the step that failed, its from
	int stuck;                // steps that couldn't be undone
	char stuck_path[GA_PATH_LEN]; // the first of them, where it is now
} GARenameResult;

// Runs the plan's steps in order, checking each again first and never
// overwriting. When one fails it undoes the ones done and returns -1, with
// out saying which failed and what couldn't be undone; otherwise 0.
int GA_runRename(const GARename* r, GARenameResult* out);

// Undoes the first done steps, last first. Returns how many couldn't be.
int GA_undoRename(const GARename* r, int done, GARenameResult* out);

// Where a path the game was known by is after the rename: the game, and
// for a game folder anything in it. Returns 1, or 0 for any other path.
int GA_renamedPath(const GARename* r, const char* path, char* out, size_t size);

// The confirm or toast's list of what went with it, as GA_planSummary.
void GA_renameSummary(const GARename* r, char* out, size_t size);

// Points a collection file's lines at the game's new paths, dropping a
// line that would then be there twice. Returns how many changed, or -1.
int GA_collectionRename(const char* file, const char* sdcard, const GARename* r);

#endif
