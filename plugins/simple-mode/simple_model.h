#ifndef SIMPLE_MODEL_H
#define SIMPLE_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// What simple mode hides and how it's left, and what its kid tag and
// kiosk settings let through. No SDL and no host here, so the host tests
// can check it on its own.

#define SIMPLE_PIN_LEN 64
#define SIMPLE_TILES_MAX 8

// 1 unless the "on" setting is "off". NULL counts as on, the default.
int Simple_isOn(const char* setting);

// 1 for <sdcard>/Tools/<platform> and anything inside it, such as a pak.
int Simple_isTools(const char* path, const char* sdcard, const char* platform);

// The built-in quick menu tiles simple mode hides, by name: Tools and Pak
// Store, and with hide_settings also Settings, Wifi, Bluetooth and Reboot.
// out holds SIMPLE_TILES_MAX. Returns the count.
int Simple_tiles(int hide_settings, const char** out);

// The PIN with spaces, tabs and line breaks trimmed from both ends. Longer
// than SIMPLE_PIN_LEN - 1 is cut. NULL is empty.
void Simple_cleanPin(const char* in, char out[SIMPLE_PIN_LEN]);

// What leaving takes: the PIN when one is set, otherwise just a confirm.
enum SimpleLeave {
	SIMPLE_LEAVE_CONFIRM,
	SIMPLE_LEAVE_PIN,
};
int Simple_leaveStep(const char* pin);

// 1 when typed is the PIN, both trimmed. An empty PIN never matches, and
// a NULL typed (the keyboard was cancelled) never does either.
int Simple_pinMatches(const char* pin, const char* typed);

///////////////////////////////////////
// the kid tag: while it's set, lists show only the games with it

// The games with the kid tag, by full path.
typedef struct KidList {
	char** paths;
	int count;
	int room;
} KidList;

// Adds a copy of path. 0, or -1 when out of memory, adding nothing.
int Kid_add(KidList* list, const char* path);
// Empties the list, keeping its room.
void Kid_clear(KidList* list);
void Kid_free(KidList* list);

// 1 when path is one of the games, a folder holding one (a system, a
// subfolder or a game folder), or a disc beside a game's .m3u or .cue in
// a folder named after it, as tags records a multi-disc game.
int Kid_allows(const KidList* list, const char* path);

// What deciding on an entry takes, by where it is.
enum KidPlace {
	KID_KEEP,        // shown as it is: a folder of a plugin's own, or a toggle
	KID_GAME,        // shown when Kid_allows it: a game, or a folder in Roms
	KID_RECENTS,     // Recently Played: shown when a recent is allowed
	KID_COLLECTIONS, // the Collections folder: shown when a collection has an allowed game
	KID_COLLECTION,  // one collection, a .txt in Collections: shown when it has an allowed game
};
// type is an enum MosaicEntryType.
int Kid_place(const char* path, int type, const char* sdcard);

// 1 when a collection's text, a card-relative game path on each line, has
// a game Kid_allows.
int Kid_collectionAllows(const KidList* list, const char* text, const char* sdcard);

// The kid tag setting, trimmed, into out. Returns 1 when one is set.
int Kid_tag(const char* setting, char* out, size_t size);

///////////////////////////////////////
// kiosk: editing actions behind the PIN

// The answer to an action about to run (an enum MosaicGate), for simple
// mode on or off, the kiosk setting, the action's enum MosaicActionFlag
// bits and whether the launcher has a PIN: while both are on, an action
// that edits asks for the PIN, or with none set is refused; anything else
// runs.
int Kiosk_gate(int simple_on, int kiosk_on, int flags, int has_pin);

#endif
