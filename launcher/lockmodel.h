#ifndef LOCKMODEL_H
#define LOCKMODEL_H

// The launcher's built-in lock, without SDL or the loader: the chord that
// locks and unlocks, held for a while, the hold switch when it's used, a
// plugin's request to lock, and the padlock's shapes. plugins.c runs it
// each frame and draws.

#include <stddef.h>
#include "mosaic_plugin.h"

#define LOCK_HOLD_MS_DEFAULT 1000
#define LOCK_HOLD_SECONDS_MAX 5

// The Launcher page's settings for it, in the launcher's settings file.
#define LOCK_CHORD_KEY "lock_chord"
#define LOCK_CHORD_DEFAULT "off"
#define LOCK_CHORD_OPTIONS "select+start,l1+r1,l2+r2,off"
#define LOCK_HOLD_KEY "lock_hold"
#define LOCK_HOLD_DEFAULT "1"
#define LOCK_HOLD_OPTIONS "1,2,3"
#define LOCK_SWITCH_KEY "lock_switch"
#define LOCK_SWITCH_DEFAULT "off"

// What changed in a frame, from Lock_frame.
enum LockChange {
	LOCK_SAME,
	LOCK_LOCKED_CHORD,
	LOCK_UNLOCKED_CHORD,
	LOCK_LOCKED_SWITCH,
	LOCK_UNLOCKED_SWITCH,
};

typedef struct LockState {
	unsigned int chord;    // MOSAIC_BUTTON_BIT masks, or 0 for no chord
	unsigned long hold_ms; // how long the chord is held to lock or unlock
	int use_switch;        // the hold switch locks while it's on

	int chord_locked;  // the chord, or a plugin, locked it
	int switch_locked; // the switch is on, and used
	int holding;       // the whole chord is held, since since
	unsigned long since;
	int fired;    // this hold of the chord already locked or unlocked
	int draining; // unlocked, but buttons are still held from before
} LockState;

// A chord from a setting: button names joined by "+", such as
// "select+start", from a, b, x, y, l1, r1, l2, r2, start, select, menu, up,
// down, left and right, without case, or "off" for none. Writes the mask
// and returns 1, or returns 0 for anything else, leaving it as it was.
int Lock_parseChord(const char* text, unsigned int* mask);

// How long to hold the chord, from a setting in whole seconds, 1 to
// LOCK_HOLD_SECONDS_MAX, in milliseconds, or fallback for anything else.
unsigned long Lock_parseHoldMs(const char* text, unsigned long fallback);

void Lock_init(LockState* lock, unsigned int chord, unsigned long hold_ms, int use_switch);

// One frame: the buttons held now, the switch's position (1, 0, or -1
// where there's none) and the time in milliseconds. Writes the buttons to
// take this frame to *consumed: all of them while locked, and after an
// unlock until every button is let go, so the chord's own releases don't
// reach the screen. Returns an enum LockChange. While the switch holds the
// lock, the chord neither locks nor unlocks.
int Lock_frame(LockState* lock, unsigned int held, int switch_pos, unsigned long now, unsigned int* consumed);

// Locks as the chord would, for a plugin's request. A chord held now
// doesn't count towards unlocking until it's let go. Returns 1 when it
// locked, 0 when it was locked already, or -1 when nothing could unlock
// it: there's no chord, and the switch isn't what holds it.
int Lock_request(LockState* lock);

int Lock_locked(const LockState* lock);

// What unlocks it, for the lock screen: the chord's buttons in upper case
// joined by "+", such as "SELECT+START", or "" with no chord.
void Lock_chordName(unsigned int chord, char* out, size_t size);

// The padlock in a header rect: the shackle and the hole in it, in the
// pill's colour, then the body over the shackle's foot and the keyhole in
// it.
typedef struct LockPadlock {
	MosaicRect shackle, hole, body, keyhole;
	int shackle_radius, hole_radius, body_radius;
} LockPadlock;

void Lock_padlock(const MosaicRect* rect, LockPadlock* out);

#endif
