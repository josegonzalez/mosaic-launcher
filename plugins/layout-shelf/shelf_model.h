// Cartridge shelf layout, without the host: which shape an entry's
// cartridge takes, how many shelves and columns fit, where each cartridge,
// its label and its name tag go, which shelves are in view, where the
// d-pad moves the selection, how far a lift has got, and the letter a
// label without art shows. No SDL and no file access, so it's tested on
// the host.

#ifndef SHELF_MODEL_H
#define SHELF_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

#define LS_MAX_SHELVES 4
#define LS_MAX_COLUMNS 12
// a lift's progress, from 0 (on the shelf) to LS_UNIT (lifted)
#define LS_UNIT 1000

// The shapes a cartridge can take.
enum LSShape {
	LS_SHAPE_CASE, // anything else: a game case standing face out, 5:7 portrait
	LS_SHAPE_GB,   // Game Boy and Game Boy Color: 57:65, nearly square
	LS_SHAPE_GBA,  // Game Boy Advance: 57:35, landscape
	LS_SHAPE_COUNT,
};

// A shape's width and height, in proportion.
void LS_aspect(int shape, int* w, int* h);

// A shape's name for the log: "GB cartridges", "GBA cartridges" or "cases".
const char* LS_shapeName(int shape);

// The shape for a system tag, matched without case: GB, GBC and SGB are
// LS_SHAPE_GB, GBA and MGBA LS_SHAPE_GBA, and anything else, "" too,
// LS_SHAPE_CASE.
int LS_shapeForTag(const char* tag);

// The system tag a path is in: the text in the parentheses that end the
// name of the outermost folder that has them, among the path's folders
// and, when is_dir is 1, the path itself, as "GB" for
// ".../Game Boy (GB)/Tetris.gb". System folders sit at the top of Roms,
// so a game in ".../Game Boy (GB)/Pokemon (USA)/" is still in GB. Writes
// "" when no folder has one.
void LS_systemTag(const char* path, int is_dir, char* out, size_t size);

// An entry's shape: by the system tag it's in, or for a game (is_dir 0)
// with none, by its extension (.gb, .gbc and .sgb, or .gba), or else
// LS_SHAPE_CASE.
int LS_shapeOf(const char* path, int is_dir);

// The shape a folder's columns are sized for: shape when all n shapes are
// it, else LS_SHAPE_CASE. LS_SHAPE_CASE for none.
int LS_commonShape(const int* shapes, int n);

// A shelves setting: a whole number from min to max, or 0 for "auto" and
// anything else.
int LS_count(const char* value, int min, int max);

// Shelves for a room h pixels tall when the setting is auto: one per 110
// units of stock's layout (scale pixels each), rounded from a half, from 1
// to LS_MAX_SHELVES; 2 on each of today's screens.
int LS_autoShelves(int h, int scale);

typedef struct LSShelves {
	MosaicRect room; // what's left of the area for the shelves
	int shelves;     // shelves in view
	int cols;        // cartridges on a shelf
	int col_x;       // where the first column starts; the columns are centred in the room
	int col_w;
	int shelf_h;     // each shelf's share of the room, from the top of its lift to the gap under its plank
	int lift;        // how far the selected cartridge rises off its shelf
	int cart_h;      // the tallest a cartridge stands
	int plank_h;     // the shelf's board, under the cartridges, with the name tags on it
	int gap;
	int shape;       // the shape the columns are sized for
} LSShelves;

// Lays the shelves out in area, below status and header (the status pill
// and header widgets, which stay clear; w 0 when there are none), with a
// gap of 4 * scale around them. shelves of 0 is auto. Each shelf has room
// for the lift (a tenth of what's left after its plank and gap) above
// cartridges cart_h tall, standing on a plank as tall as name_h (0 for no
// names) and at least 4 * scale, with a gap under it. The columns are as
// many as fit cartridges of shape at cart_h tall with a gap between them,
// from 1 to LS_MAX_COLUMNS. While a cartridge would be under min_h tall a
// shelf goes, down to one. Returns 0, or -1 when not even one fits.
int LS_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int shelves,
	int name_h, int shape, int min_h, LSShelves* out);

// The cartridge at slot, counted from the top left in reading order (slot
// 0 is the first column of the first shelf in view), of shape: as big as
// fits its column less a gap and cart_h, keeping its shape, centred in
// the column and standing on the plank, raised by lift * progress /
// LS_UNIT. An empty rect for a slot that isn't in view.
MosaicRect LS_cart(const LSShelves* shelves, int slot, int shape, int progress);

// The plank of shelf (from 0, the top one in view), across the room.
MosaicRect LS_plank(const LSShelves* shelves, int shelf);

// The name tag under slot's cartridge: the plank under its column, less
// half a gap either side. h 0 when the plank is too short for names
// (name_h was 0).
MosaicRect LS_tag(const LSShelves* shelves, int slot, int name_h);

// Where a cartridge of shape in cart has its label, which shows the game's
// art or its letter: a GB cartridge's in its middle under the grip, a GBA
// one's across its lower part, and a case's on all of its front but the
// spine.
MosaicRect LS_label(int shape, const MosaicRect* cart);

// The first shelf in view, given the one in view before (first), so the
// window moves only as far as it takes to show the selected entry, and
// never past the last shelf. 0 for an empty list.
int LS_window(int first, int selected, int count, int cols, int shelves);

// Where a d-pad direction (MOSAIC_BUTTON_UP, DOWN, LEFT or RIGHT) moves
// the selection in count entries on shelves of cols. Left and right step
// along the shelves in reading order, on to the next shelf; up and down
// go between shelves, keeping the column. Past the ends, a press wraps
// (fresh is 1) and a held repeat stops:
//   left from the first entry goes to the last, right from the last to
//     the first;
//   up from the top shelf goes to the column's lowest entry;
//   down from the last shelf goes to the column's top entry, and down
//     from a shelf whose column has nothing below, as the last shelf is
//     shorter, goes to the last entry, press or repeat.
// Returns the new index, or selected when it doesn't move.
int LS_move(int selected, int count, int cols, int dir, int fresh);

// How far a lift of duration ms has got after elapsed ms, from 0 to
// LS_UNIT, easing out: quick at first and slowing to a stop.
int LS_ease(unsigned long elapsed, unsigned long duration);

// The letter a label shows for a name: its first letter or digit, upper
// case, or first character that isn't ASCII (a whole UTF-8 sequence), or
// else its first character that isn't a space; "" for a name with none.
void LS_letter(const char* name, char* out, size_t size);

#endif
