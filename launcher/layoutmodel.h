#ifndef LAYOUTMODEL_H
#define LAYOUTMODEL_H

// The parts of the layout slot that don't need SDL or the loader: whether
// the owner's layout is still in use, the buttons it sees and consumes, and
// where on the screen it draws. plugins.c calls the layout; screen_gamelist.c
// builds its view. Also where row decorators, header widgets and art
// decorators draw, and the background slot.

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

// The background slot: its owner draws what's behind the game list, over
// stock's background, and the result goes on the background layer in
// place of stock's.
typedef struct BackgroundSlot {
	int owner;      // the loaded plugin's index, or -1
	int off;        // its background_render failed: stock's until the launcher restarts
	int drawn;      // what it last drew is the one to show
	unsigned built; // the stock background that was under it
} BackgroundSlot;

// What goes on the background layer.
enum BackgroundShow {
	BACKGROUND_STOCK,  // stock's
	BACKGROUND_PLUGIN, // the owner's
	BACKGROUND_REDRAW, // the owner's, but stock's changed under it: draw the game list again
};

void BackgroundSlot_claim(BackgroundSlot* slot, int owner);
void BackgroundSlot_clear(BackgroundSlot* slot);

// 1 while the owner is asked to draw.
int BackgroundSlot_active(const BackgroundSlot* slot);

// What background_render returned, drawn over stock's background number
// serial: 0 shows it, anything else turns the owner off. Returns 1 while
// it's on.
int BackgroundSlot_result(BackgroundSlot* slot, int rc, unsigned serial);

// The owner wasn't asked this time, eg. no surface to lend: stock's shows.
void BackgroundSlot_skip(BackgroundSlot* slot);

// What to show: stock's off the game list, without an owner or until it
// has drawn, else the owner's, or BACKGROUND_REDRAW once stock's
// background is no longer serial.
int BackgroundSlot_show(const BackgroundSlot* slot, int on_gamelist, unsigned serial);

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

// Whether the built-in list draws a row's name: names show in the folder
// (show_names), and either no plugin hid it (hidden) or there's no art to
// stand in for it, which is while Show game art is off (show_art), or on
// the selected row when no thumbnail shows beside the list (has_art).
int ListRow_nameShown(int show_names, int hidden, int show_art, int selected, int has_art);

// Where the game list's selection is on a frame, for transitions: the
// selected row and the pill on it, each empty (w 0) when unknown or not
// drawn.
typedef struct ListSelection {
	MosaicRect row;
	MosaicRect pill;
} ListSelection;

void ListSelection_clear(ListSelection* sel);

// The built-in list's selection on row, from ListRow_rects with the same
// arguments: the pill is the row's name, which stock draws the pill
// under, and empty when names don't show, as stock draws none then.
void ListSelection_row(ListSelection* sel, int row, int margin, int padding, int pill_h, int width, int name_w,
	int show_names);

// A cell a plugin layout passed to decorate_cell for index: when it's the
// selected entry, the part of rect in area becomes the row, replacing an
// earlier one, with no pill, as the layout draws its own.
void ListSelection_cell(ListSelection* sel, int index, int selected, const MosaicRect* rect, const MosaicRect* area);

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

// Art decorators. Where the built-in list puts the selected game's
// thumbnail, an img_w by img_h image on a w by h screen, as stock does: as
// wide as art_w (the Game art width setting, a share of w) and at most 0.6
// of h tall, keeping its shape, margin from the right edge and centred on
// the middle. Returns 1, or 0 for an empty image or an empty rect, still
// writing the rect stock would use.
int ListArt_rect(int w, int h, double art_w, int margin, int img_w, int img_h, MosaicRect* out);

// The free space under the built-in list's thumbnail art: the art's
// column, as wide as the widest thumbnail (art_w of w) and margin from the
// right edge, which the list's rows stop short of, from the art's bottom
// down to hints_top, where the button hints start. Returns 1, or 0 when
// there's no room, writing an empty rect.
int ListArt_below(int w, double art_w, int margin, int hints_top, const MosaicRect* art, MosaicRect* out);

// Under a plugin layout: the part of area under art, as wide as art, down
// to area's bottom. Returns 1, or 0 when there's no room, writing an empty
// rect.
int LayoutArt_below(const MosaicRect* art, const MosaicRect* area, MosaicRect* out);

// Art replacement (art_replace). The corner radius in pixels for an img_w
// wide image shown art_w wide, from the Thumbnail radius setting in stock's
// units at scale pixels a unit, cast for cast as stock rounds a thumbnail's
// corners, and cut down to half of w and h; 0 for an empty size.
int ListArt_radius(int radius, int scale, int img_w, int art_w, int w, int h);

// How many pixels at each end of a row of a rounded image go clear, for
// stock's rounded corners: dy is how far into a corner the row is, radius
// for the outermost row down to 1 for the innermost, and 0 for a row
// between the corners. Never more than radius.
int ListArt_cornerCut(int radius, int dy);

// A plugin's surface shown in place of the built-in list's thumbnail.
// Pointers are only compared, never read.
typedef struct ArtSwap {
	int owner;           // the loaded plugin's index, or -1
	const void* surface; // and its surface
	int w, h;            // the surface's size when last shown
} ArtSwap;

// What ArtSwap_set did.
enum ArtSwapResult {
	ARTSWAP_REFUSED = -1, // another plugin's surface shows
	ARTSWAP_NONE,         // a clear when the plugin had nothing showing
	ARTSWAP_SHOWN,        // nothing showed before, or another surface of the plugin's or another size
	ARTSWAP_FRAME,        // the same surface at the same size: only its pixels changed
	ARTSWAP_CLEARED,      // its surface no longer shows
};

void ArtSwap_clear(ArtSwap* swap);

// 1 while a surface shows.
int ArtSwap_active(const ArtSwap* swap);

// owner shows surface, w by h, or with surface NULL clears its own. The
// first plugin to show one keeps the place until it clears it or it's
// dropped.
int ArtSwap_set(ArtSwap* swap, int owner, const void* surface, int w, int h);

// Drops surface, or every surface of owner's, eg. after it's freed or its
// plugin quits. Returns 1 when that was what showed.
int ArtSwap_dropSurface(ArtSwap* swap, const void* surface);
int ArtSwap_dropOwner(ArtSwap* swap, int owner);

// What an ArtSwap_set result (an enum ArtSwapResult) needs drawn again.
enum ArtSwapRedraw {
	ARTSWAP_REDRAW_NONE,
	ARTSWAP_REDRAW_THUMB, // the built-in list's thumbnail layer
	ARTSWAP_REDRAW_LIST,  // the whole list, a plugin layout's included, and the thumbnail layer
};

// On the built-in list (layout 0) a new frame redraws only the thumbnail
// layer, and anything else that changed what shows redraws the list too
// when the rows beside it had no thumbnail (had_thumb 0) or there are art
// decorators to place under it. Under a plugin layout (layout 1), which
// draws the surface itself, every change redraws the list, except that a
// new frame redraws nothing unless the layout drew the surface the last
// time it drew (drawn).
int ArtSwap_redraw(int result, int layout, int drawn, int had_thumb, int decorators);

#endif
