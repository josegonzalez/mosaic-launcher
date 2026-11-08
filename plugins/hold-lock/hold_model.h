#ifndef HOLD_MODEL_H
#define HOLD_MODEL_H

#include "mosaic_plugin.h"

// The hold lock's state: the chord that locks and unlocks, held for a
// while, the hold switch when it's used, and the padlock's shapes. No SDL
// and no host here, so the host tests can check it on its own.

#define HL_HOLD_MS_DEFAULT 1000
#define HL_HOLD_SECONDS_MAX 5

// What changed in a frame, from HL_frame.
enum HlChange {
	HL_SAME,
	HL_LOCKED_CHORD,
	HL_UNLOCKED_CHORD,
	HL_LOCKED_SWITCH,
	HL_UNLOCKED_SWITCH,
};

typedef struct HoldLock {
	unsigned int chord;    // MOSAIC_BUTTON_BIT masks, or 0 for no chord
	unsigned long hold_ms; // how long the chord is held to lock or unlock
	int use_switch;        // the hold switch locks while it's on

	int chord_locked;  // the chord locked it
	int switch_locked; // the switch is on, and used
	int holding;       // the whole chord is held, since since
	unsigned long since;
	int fired;    // this hold of the chord already locked or unlocked
	int draining; // unlocked, but buttons are still held from before
} HoldLock;

// A chord from a setting: button names joined by "+", such as
// "select+start", from a, b, x, y, l1, r1, l2, r2, start, select, menu, up,
// down, left and right, without case, or "off" for none. Writes the mask
// and returns 1, or returns 0 for anything else, leaving it as it was.
int HL_parseChord(const char* text, unsigned int* mask);

// How long to hold the chord, from a setting in whole seconds, 1 to
// HL_HOLD_SECONDS_MAX, in milliseconds, or fallback for anything else.
unsigned long HL_parseHoldMs(const char* text, unsigned long fallback);

void HL_init(HoldLock* lock, unsigned int chord, unsigned long hold_ms, int use_switch);

// One frame: the buttons held now, the switch's position (1, 0, or -1
// where there's none) and the time in milliseconds. Writes the buttons to
// take this frame to *consumed: all of them while locked, and after an
// unlock until every button is let go, so the chord's own releases don't
// reach the screen. Returns an enum HlChange. While the switch holds the
// lock, the chord neither locks nor unlocks.
int HL_frame(HoldLock* lock, unsigned int held, int switch_pos, unsigned long now, unsigned int* consumed);

int HL_locked(const HoldLock* lock);

// The padlock in a header widget's rect: the shackle and the hole in it,
// in a pill's colour, then the body over the shackle's foot and the
// keyhole in it.
typedef struct HlPadlock {
	MosaicRect shackle, hole, body, keyhole;
	int shackle_radius, hole_radius, body_radius;
} HlPadlock;

void HL_padlock(const MosaicRect* rect, HlPadlock* out);

#endif
