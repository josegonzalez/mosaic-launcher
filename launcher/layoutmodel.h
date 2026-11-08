#ifndef LAYOUTMODEL_H
#define LAYOUTMODEL_H

// The parts of the layout slot that don't need SDL or the loader: whether
// the owner's layout is still in use, the buttons it sees and consumes, and
// where on the screen it draws. plugins.c calls the layout; screen_gamelist.c
// builds its view. Also where row decorators and header widgets draw.

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

// The part of a inside b, written to out. Returns 0 when they don't overlap,
// writing an empty rect.
int LayoutRect_clip(const MosaicRect* a, const MosaicRect* b, MosaicRect* out);

// A row of the built-in list, from 0 at the top, for a row decorator: the
// row is width wide (the widest its pill can be) from margin, padding plus
// row pill_h rows down, and its name name_w of that, or none when names
// don't show.
void ListRow_rects(int row, int margin, int padding, int pill_h, int width, int name_w, int show_names,
	MosaicRow* out);

// Plugins' header widgets, and the status file pill (statuspill.c) before
// them, laid out right to left beside stock's status pill, which is status_w wide in the top right of a screen_w wide screen,
// inset by padding, and pill_h tall. Each widget sits gap left of the last,
// the first gap left of the pill. Together they take at most half the
// header left of the pill, so stock's items and the start of the top row
// always keep their room.
typedef struct HeaderRow {
	int y, h;
	int status_x; // the pill's left edge
	int limit;    // the leftmost a widget can reach
	int next;     // where the next widget's right edge goes
	int gap;
	int count;    // widgets placed
} HeaderRow;

void HeaderRow_init(HeaderRow* row, int screen_w, int padding, int gap, int status_w, int pill_h);

// How wide the next widget can be, 0 when there's no room left.
int HeaderRow_room(const HeaderRow* row);

// Places a widget want pixels wide, cut to the room left, writing its rect.
// Returns 1, or 0 for a want of 0 or less or no room, placing nothing.
int HeaderRow_place(HeaderRow* row, int want, MosaicRect* out);

// How far the widgets reach left of the pill, gaps included, or 0 with none.
int HeaderRow_width(const HeaderRow* row);

// The widgets' rect from the leftmost one to the pill, or an empty rect.
void HeaderRow_rect(const HeaderRow* row, MosaicRect* out);

// The widest the top row's pill can be once header widgets take header_w
// left of a status_w wide pill, from a list row that starts at margin and
// could be available wide: stock leaves padding between the row's pill and
// the status pill, and the row keeps that gap from the widgets. With no
// widgets (header_w 0) it's available, as stock has it.
int HeaderRow_topRowWidth(int available, int screen_w, int padding, int margin, int status_w, int header_w);

#endif
