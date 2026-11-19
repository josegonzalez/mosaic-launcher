#ifndef STATUS_MODEL_H
#define STATUS_MODEL_H

#include <stddef.h>
#include "tags_model.h"

// The status flags, with no SDL and no host: Completed, Beaten, Broken and
// Backlog, each on or off by itself for a game, kept apart from the tags.
//
// They're stored as a TGStore of their own, in a file of their own in the
// same format as tags.txt, with each flag's key (completed, beaten, broken
// or backlog) as a tag's name. A line with any other name is kept as it
// is, so a newer flag survives an older plugin.

enum STFlag {
	ST_COMPLETED,
	ST_BEATEN,
	ST_BROKEN,
	ST_BACKLOG,
	ST_FLAGS,
};

// The flag's key, as the file and its entry set (tags.status.<key>) name
// it, or NULL for a flag that isn't one.
const char* ST_key(int flag);
// The flag's name to show, in English, or NULL.
const char* ST_label(int flag);
// The flag a key or an entry set's name (tags.status.<key>) names, or -1.
int ST_fromKey(const char* key);
int ST_fromSet(const char* set);

// Whether the game at rel (card-relative) has the flag: 1 or 0.
int ST_has(const TGStore* store, int flag, const char* rel);
// The game's flags, a bit (1 << flag) for each.
unsigned int ST_flags(const TGStore* store, const char* rel);
// Sets or clears a flag on a game, adding the flag's line when it's the
// first and dropping it with the last. Returns 1 when it changed, 0 when
// it already was so, -1 for a bad flag or path, past TG_GAMES_MAX or when
// memory ran out.
int ST_set(TGStore* store, int flag, const char* rel, int on);
// How many games have the flag, on the card or not.
int ST_count(const TGStore* store, int flag);

#endif
