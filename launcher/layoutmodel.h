#ifndef LAYOUTMODEL_H
#define LAYOUTMODEL_H

// The parts of the layout slot that don't need SDL or the loader: whether
// the owner's layout is still in use, the buttons it sees and consumes, and
// where on the screen it draws. plugins.c calls the layout; screen_gamelist.c
// builds its view.

#include "mosaic_plugin.h"

enum LayoutState {
	LAYOUT_NONE,    // nobody owns the slot, or the owner has no layout_render
	LAYOUT_WAITING, // the owner's layout_init hasn't run yet
	LAYOUT_ON,
	LAYOUT_OFF,     // it declined or failed; the built-in list draws until the launcher restarts
};

typedef struct LayoutSlot {
	int state;
	int owner; // the loaded plugin's index, while state isn't LAYOUT_NONE
} LayoutSlot;

// The owner, from load order, with its layout_render; NONE without one.
void LayoutSlot_claim(LayoutSlot* slot, int owner, int has_render);

// 1 while the owner's layout draws the game list: waiting for its init or on.
int LayoutSlot_active(const LayoutSlot* slot);

// 1 when layout_init is due before the next input or render.
int LayoutSlot_needsInit(const LayoutSlot* slot);

// What a layout call returned: 0 keeps the layout on (and turns a waiting
// one on), anything else turns it off. Returns 1 while it's still on.
int LayoutSlot_result(LayoutSlot* slot, int rc);

// Drops the owner, eg. after its quit.
void LayoutSlot_clear(LayoutSlot* slot);

// One button: its MOSAIC_BUTTON_* and the pad's own bit for it.
typedef struct LayoutButton {
	int mosaic;
	int pad;
} LayoutButton;

// The MOSAIC_BUTTON_BIT mask for a mask of pad bits. Pad bits not in map are
// left out.
unsigned int LayoutInput_bits(const LayoutButton* map, int count, int pad_bits);

// The pad bits for a mask of MOSAIC_BUTTON_BIT, eg. what a layout consumed.
// Bits not in map are left out.
int LayoutInput_padBits(const LayoutButton* map, int count, unsigned int bits);

// The pad's state this frame as a MosaicInput.
void LayoutInput_fill(MosaicInput* out, const LayoutButton* map, int count,
	int pressed, int repeated, int released, int held);

// Where a layout draws on a w by h screen: everything above the button
// hints, which start hints_top pixels down, and the status pill's rect in
// its top right, status_w wide and status_h tall, inset by padding. A
// status_w of 0 leaves status empty.
void LayoutView_area(int w, int h, int hints_top, int padding, int status_w, int status_h,
	MosaicRect* area, MosaicRect* status);

#endif
