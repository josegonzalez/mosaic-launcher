// Media viewer, without the host: every image a game has and its clip,
// found beside the game and in the screenshots folder and put in the order
// they're shown, the title saying which file each one is, and the hints.
// No SDL; it looks at files with libc only, without following links, so
// it's tested on the host.
//
// For a game at <dir>/<file>, named by its file name without its last
// extension (a folder by its whole name), as stock names its .media
// thumbnail, they're:
//
//   1. stock's thumbnail: <dir>/.media/<name>.png
//   2. nx-redux's art: <dir>/.media/<kind>/<name>.png, .jpg and .jpeg, for
//      boxart, boxart2d, mix, screenshot and wheel in that order
//   3. MyMinUI's: <dir>/Imgs/<name>.png
//   4. its screenshots: <shots>/<game>.<date>.png and the like, newest
//      first, where <game> is the name the game list shows, as stock's
//      minarch names them
//   5. its clip, when clips play: <dir>/.videos/<file less its last
//      extension>.mp4, as video-previews finds it
//
// Only regular files are taken, and only in real folders: a link, to a
// file or to a folder, is left out.

#ifndef MEDIA_MODEL_H
#define MEDIA_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"
#include "shot_model.h"

// a game's media, at most; past it the rest are left out
#define SM_MAX_ITEMS 512

enum SMKind {
	SM_IMAGE,
	SM_CLIP,
};

typedef struct SMItem {
	char path[SV_PATH_MAX]; // its full path
	int label;              // where the name it's shown by starts in path
	int kind;               // enum SMKind
} SMItem;

typedef struct SMList {
	SMItem* items; // in the order above
	int count;
	int cap;
} SMList;

void SM_listInit(SMList* list);
void SM_listFree(SMList* list);

// The name a game's art is filed under: its file name without its last
// extension, or a folder's (is_dir 1) whole name. Returns 1, or 0 (writing
// "") for a path with no folder or name, or one too long for out.
int SM_artName(const char* path, int is_dir, char* out, size_t size);

// The name the game list shows without a map.txt alias, as stock tidies
// it: the art name less the trailing (...) and [...] tags, as in
// "Tetris (USA) [!]" for "Tetris". Returns 1, or 0 (writing "").
int SM_listName(const char* path, int is_dir, char* out, size_t size);

// The game's media into list, in the order above. rom is the game's full
// path, or NULL or "" for none (then only the screenshots of game are
// found); game is the name its screenshots have, or NULL or "" to leave
// them out; shots is the screenshots folder; clips is 1 when clips play.
// Returns how many.
int SM_find(const char* rom, int is_dir, const char* game, const char* shots, int clips, SMList* list);

// 1 when SM_find would find at least one; stops at the first.
int SM_any(const char* rom, int is_dir, const char* game, const char* shots, int clips);

// The name an item is shown by, such as ".media/boxart/Tetris.png" or
// "Screenshots/Tetris.2026-11-08-14-03-05.png".
const char* SM_label(const SMItem* item);

// "3/7 · .media/boxart/Tetris.png" for the item at index, or "" for an
// index not in list.
void SM_title(const SMList* list, int index, char* out, size_t size);

// The hints: B BACK on the left, and A PLAY on the right on a clip.
void SM_hints(const SMList* list, int index, MosaicHint left[1], int* left_count, MosaicHint right[1],
	int* right_count);

#endif
