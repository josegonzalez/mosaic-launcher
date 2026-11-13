#ifndef FOCUS_MODEL_H
#define FOCUS_MODEL_H

#include <stddef.h>

// What focus mode puts on the root: the games in an entry set, in the
// order its owner lists them, then one folder holding every console. No
// SDL and no host here, so the host tests can check it on its own.

#define FOCUS_MAX 256       // as many favorites as the favorites plugin keeps
#define FOCUS_PATH_LEN 1024
#define FOCUS_ALL_NAME "All systems"

// Full paths, eg. /mnt/SDCARD/Roms/GB/Tetris.gb, without blanks or repeats.
typedef struct FocusList {
	char items[FOCUS_MAX][FOCUS_PATH_LEN];
	int count;
} FocusList;

// Adds a path unless it's empty, too long, already there or the list is
// full. Returns 1 when it was added.
int Focus_add(FocusList* list, const char* path);

// One row of the root. name is NULL to let the launcher name it.
typedef struct FocusRow {
	char path[FOCUS_PATH_LEN];
	int type; // an enum MosaicEntryType
	const char* name;
} FocusRow;

// The root's rows for list: each path still on the card, typed as typeOf
// says (an enum MosaicEntryType, or -1 when it isn't there), then
// <sdcard>/Roms as FOCUS_ALL_NAME. With no path on the card it returns 0,
// so the launcher keeps its own root. out holds FOCUS_MAX + 1.
int Focus_rows(const FocusList* list, const char* sdcard, int (*typeOf)(const char* path), FocusRow* out);

// The sets focus mode can focus on: every one but hidden, whose games
// every list leaves out. 1 when name can be.
int Focus_focusable(const char* name);

// What the list of sets shows for one: fav for favorites, tag (a printf
// format with one %s) with the name for tags:<name>, smart the same for
// smart:<name>, and other names as they are.
void Focus_setLabel(const char* name, const char* fav, const char* tag, const char* smart, char* out, size_t size);

#endif
