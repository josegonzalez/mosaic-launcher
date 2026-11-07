#ifndef FOCUS_MODEL_H
#define FOCUS_MODEL_H

#include <stddef.h>

// What focus mode puts on the root: the favorites, in the collection's
// order, then one folder holding every console. No SDL and no host here,
// so the host tests can check it on its own.

#define FOCUS_MAX 256       // as many favorites as the favorites plugin keeps
#define FOCUS_PATH_LEN 512
#define FOCUS_ALL_NAME "All systems"

// The collection favorites keeps its list in, from favorites' own
// "collection" setting, read the way favorites reads it: empty or with a
// slash falls back to Favorites.
const char* Focus_collectionName(const char* setting);

// <sdcard>/Collections/<collection>.txt
void Focus_listPath(const char* sdcard, const char* collection, char* out, size_t size);

// Card-relative paths, eg. /Roms/GB/Tetris.gb, without blanks or repeats.
typedef struct FocusList {
	char items[FOCUS_MAX][FOCUS_PATH_LEN];
	int count;
} FocusList;

// Reads a collection file into list, replacing what it held. A missing
// file is an empty list. Returns the count.
int Focus_load(FocusList* list, const char* path);

// One row of the root. name is NULL to let the launcher name it.
typedef struct FocusRow {
	char path[FOCUS_PATH_LEN * 2];
	int type; // an enum NUIEntryType
	const char* name;
} FocusRow;

// The root's rows for list: each favorite still on the card (exists says),
// then <sdcard>/Roms as FOCUS_ALL_NAME. With no favorite on the card it
// returns 0, so the launcher keeps its own root. out holds FOCUS_MAX + 1.
int Focus_rows(const FocusList* list, const char* sdcard, int (*exists)(const char* path), FocusRow* out);

#endif
