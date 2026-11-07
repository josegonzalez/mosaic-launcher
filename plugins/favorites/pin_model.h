// Pin to top, without the host: which entries are favorites, quickly, and
// which folders put them first. No SDL and no file access, so it's tested
// on the host.

#ifndef PIN_MODEL_H
#define PIN_MODEL_H

#define PIN_MAX 256

// The favorites, sorted for lookup. Holds pointers to the caller's
// strings, so build it again whenever they change.
typedef struct PinSet {
	const char* items[PIN_MAX];
	int count;
} PinSet;

// Fills the set from count card-relative paths, eg. /Roms/GB/Tetris.gb.
// Paths past PIN_MAX are left out.
void Pin_build(PinSet* set, const char* const* paths, int count);

// True when the card-relative path is in the set.
int Pin_has(const PinSet* set, const char* rel);

// True when an entry (a card-relative path and an enum NUIEntryType) is a
// favorite, matching what the favorites toggle records: a folder that
// launches a game means its .m3u or .cue, and a disc inside one means the
// folder's .m3u. Checks names only, without looking at the card.
int Pin_isPinned(const PinSet* set, const char* rel, int type);

// True when a folder (card-relative, "" for the root) puts favorites first.
// Recently Played, collections and the favorites folder itself keep their
// own order. favorites_dir is the favorites folder, eg. /Favorites.
int Pin_applies(const char* dir_rel, const char* favorites_dir);

// The sort_compare answer for two entries: pinned first, otherwise no
// opinion, so each group keeps the launcher's order.
int Pin_compare(int a_pinned, int b_pinned);

#endif
