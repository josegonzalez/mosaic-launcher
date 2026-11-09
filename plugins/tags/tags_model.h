#ifndef TAGS_MODEL_H
#define TAGS_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// The tags plugin's logic, with no SDL and no host: the tag store, its
// file, what a game list entry records, and moving through the checklist.
//
// Games are card-relative paths, eg. /Roms/Game Boy (GB)/Tetris.gb, so the
// file survives a card mounted somewhere else. On disk it's plain text, one
// line each: a tag's name on its own declares the tag, and a name, a tab
// and a path tags that game. Blank lines and lines starting with # are
// skipped. Saving writes the tags by name, each followed by its games in
// the order they were tagged.

#define TG_NAME_LEN 64
#define TG_PATH_LEN 1024
#define TG_TAGS_MAX 256
#define TG_GAMES_MAX 4096 // per tag

typedef struct TGTag {
	char name[TG_NAME_LEN];
	char** games; // card-relative paths, in the order they were tagged
	int count;
	int capacity;
} TGTag;

typedef struct TGStore {
	TGTag* tags; // by name, ignoring case
	int count;
	int capacity;
} TGStore;

void TG_init(TGStore* store);
void TG_free(TGStore* store);

// Replaces the store with the tags in text. Lines with a bad name or path
// are skipped. Returns 0, or -1 when memory ran out.
int TG_parse(TGStore* store, const char* text);
// The store as text, to free(). NULL when memory ran out.
char* TG_format(const TGStore* store);

// Replaces the store with the file's tags. A missing file is no tags.
// Returns 0, or -1 when it couldn't be read.
int TG_load(TGStore* store, const char* file);
// Writes the store through a temporary file beside it, synced before it
// replaces the old one, so a power cut leaves the old file or the new one.
// Returns 0 or -1.
int TG_save(const TGStore* store, const char* file);

// Cleans a typed tag name into out: control characters become spaces,
// spaces are trimmed from both ends, and it's cut to TG_NAME_LEN - 1 bytes
// between characters. Returns 1 when the result is a name a tag can have:
// not empty, no slash, not starting with a dot or #.
int TG_cleanName(const char* name, char* out, size_t size);

// The tag's index, matched ignoring case, or -1.
int TG_find(const TGStore* store, const char* name);
// Adds a tag (cleaned as TG_cleanName does) and returns its index, or the
// index of the tag already named so. -1 for a bad name, past TG_TAGS_MAX
// or when memory ran out. *added is 1 when it's new; it can be NULL.
// Indexes after it move up by one.
int TG_add(TGStore* store, const char* name, int* added);
// Removes the tag at index with its games. Returns 0, or -1 for a bad index.
int TG_remove(TGStore* store, int index);

int TG_has(const TGStore* store, int tag, const char* rel);
// Tags or untags a game. Returns 1 when it changed, 0 when it already was
// so, -1 for a bad tag or path, past TG_GAMES_MAX or when memory ran out.
int TG_set(TGStore* store, int tag, const char* rel, int on);

// A card-relative path a tag can hold: starts with a slash, no tab or
// newline, no "." or ".." part, under TG_PATH_LEN.
int TG_validPath(const char* rel);

// What a game list entry records, card-relative, the same as stock's
// recents: a pak or rom itself, a game folder's .m3u (or else .cue) named
// after it, and for a disc in a game folder the folder's .m3u. type is an
// enum MosaicEntryType. Returns 1, or 0 for anything else, such as a plain
// folder or a path off the card.
int TG_gamePath(const char* sdcard, const char* path, int type, char* out, size_t size);

// The tag a folder path under the Tags folder names, eg. <sd>/Tags/RPG,
// or -1.
int TG_folderTag(const TGStore* store, const char* tags_folder, const char* path);

// The checklist: up or down by step through count rows, wrapping past
// either end when wrap is 1 and stopping there otherwise. -1 for no rows.
int TG_move(int selected, int count, int step, int wrap);
// The first of fit rows to show so selected is in view, moving as little
// as it can from first.
int TG_window(int selected, int count, int fit, int first);

#endif
