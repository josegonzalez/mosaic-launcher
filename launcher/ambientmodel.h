#ifndef AMBIENTMODEL_H
#define AMBIENTMODEL_H

// Ambient screens, without SDL or the loader: the screens that open by
// themselves, such as the lock, a charging screen or a screensaver, and
// the order the user puts them in on the Launcher page. The launcher
// opens the first in that order that wants to show. plugins.c asks them;
// settings.c moves them.

#include <stddef.h>

#define AMBIENT_MAX 16
#define AMBIENT_ID_MAX 64 // a screen id's length, its terminator included

// The launcher's own lock, always there.
#define AMBIENT_LOCK "lock"

// The setting, in the launcher's settings file: ambient screens' ids,
// comma-separated, first first. Unset, it's the default.
#define AMBIENT_ORDER_KEY "ambient_order"
#define AMBIENT_ORDER_DEFAULT "lock,charging-screen.main,screensaver.main"

// Writes the order of ids, as indexes into them, to out: those setting
// names first, in its order, then the rest in the order given. NULL or ""
// is the default. Returns count.
int Ambient_order(const char* setting, const char* const* ids, int count, int* out);

// Moves shown[index] one step, -1 up or 1 down, among shown, the ambient
// screens in their order as Ambient_order gives it, and writes the
// setting that puts them there to out. Ids the setting names that shown
// doesn't, such as a plugin that's off, keep their places. Returns the
// moved screen's new index, or -1 when it can't move that way or the
// setting doesn't fit.
int Ambient_move(const char* setting, const char* const* shown, int count, int index, int step, char* out, size_t size);

// The ambient screens registered this run.
typedef struct Ambient {
	char id[AMBIENT_ID_MAX]; // the screen's id
	int owner;               // the loaded plugin's index, or -1 for the launcher's
	int (*wants)(unsigned long idle_ms, void* data);
	void* data;
	int refused; // didn't open when it wanted to; not asked again until the next button
} Ambient;

typedef struct AmbientSet {
	Ambient items[AMBIENT_MAX];
	int count;
	int order[AMBIENT_MAX]; // indexes into items, first first, from AmbientSet_sort
} AmbientSet;

void AmbientSet_clear(AmbientSet* set);

// Why AmbientSet_add refused, or 0.
enum AmbientAddResult {
	AMBIENT_ADDED = 0,
	AMBIENT_BAD = -1,   // no id, too long, or no wants
	AMBIENT_TAKEN = -2, // already an ambient screen
	AMBIENT_FULL = -3,
};

// Adds an ambient screen at the end of the order; AmbientSet_sort puts it
// in place. Returns an enum AmbientAddResult.
int AmbientSet_add(AmbientSet* set, const char* id, int owner, int (*wants)(unsigned long idle_ms, void* data), void* data);

// The ambient screen with id, or -1.
int AmbientSet_find(const AmbientSet* set, const char* id);

// Forgets owner's ambient screens. Returns how many.
int AmbientSet_dropOwner(AmbientSet* set, int owner);

// Orders them by the setting, as Ambient_order does.
void AmbientSet_sort(AmbientSet* set, const char* setting);

#endif
