#ifndef INPUT_MODEL_H
#define INPUT_MODEL_H

// What the input tester works out apart from the host and SDL: the last
// presses, holding B and START to leave, where the sticks' dots go, and
// where each button is drawn, so the host tests can check it on its own.

#include <stddef.h>
#include "mosaic_plugin.h"

#define IT_HISTORY 8     // the last presses listed
#define IT_EXIT_MS 1000  // how long B and START are held to leave
#define IT_BUTTONS (MOSAIC_BUTTON_FN3 + 1)
#define IT_STICK_MAX 32767 // a stick's raw value at full tilt

// The buttons the tester shows: every enum MosaicButton, the FN buttons
// only where the device has them (fn, bit 0 for FN1 to bit 2 for FN3).
int It_shown(int button, unsigned int fn);

// A button's name as the tester draws it, such as "A", "UP" or "MENU", or
// "" for the FN buttons, which take the device's names.
const char* It_name(int button);

// A button's label in the drawing: as It_name, but the d-pad's first
// letters, as "U" for UP, which its small keys have room for.
const char* It_label(int button);

// The last presses, newest first.
typedef struct ItHistory {
	int count;
	int buttons[IT_HISTORY];
} ItHistory;

void It_historyInit(ItHistory* h);

// Adds each button in pressed, a MOSAIC_BUTTON_BIT mask, in enum order.
// Returns how many were added.
int It_historyPress(ItHistory* h, unsigned int pressed);

// Holding B and START together to leave.
typedef struct ItHold {
	unsigned long since; // when both went down
	int holding;         // 1 while both are down
} ItHold;

void It_holdInit(ItHold* h);

// The buttons held now, at now: 1 once B and START have been held
// together for IT_EXIT_MS, else 0. Letting either go starts it over.
int It_holdStep(ItHold* h, unsigned int held, unsigned long now);

// How far along the hold is, from 0 to 100.
int It_holdPercent(const ItHold* h, unsigned long now);

// Where a stick's dot goes, from -radius to radius, for a raw value.
int It_stickOffset(int raw, int radius);

// A stick's raw values as "12000, -3000".
void It_formatStick(int x, int y, char* out, size_t size);

// Where things go: every button's rect (w 0 for one not shown) and each
// stick's box (w 0 without it).
typedef struct ItLayout {
	MosaicRect buttons[IT_BUTTONS];
	MosaicRect sticks[2];      // left, right
	MosaicRect stick_text[2];  // the raw values under each
	int unit;                  // the drawing's grid unit, in pixels
} ItLayout;

// Lays the buttons out in area, as a 32 by 16 grid of square units at
// the most that fit, centred: the shoulders on top, MENU and POWER
// between them, the d-pad on the left, A, B, X and Y on the right, SELECT
// and START in the middle with the FN buttons the device has under them,
// and the sticks (sticks, bit 0 left and bit 1 right) under those.
// Returns 0, or -1 when a unit would be under 2 pixels.
int It_layout(const MosaicRect* area, unsigned int fn, unsigned int sticks, ItLayout* out);

#endif
