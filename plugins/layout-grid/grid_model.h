// Grid layout, without the host: how many columns and rows fit, where each
// cell and its parts go, which rows are in view, where the d-pad moves the
// selection, and the letter a tile without art shows. No SDL and no file
// access, so it's tested on the host.

#ifndef GRID_MODEL_H
#define GRID_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

#define LG_MAX_COLUMNS 8
#define LG_MAX_ROWS 6

// A columns or rows setting: a whole number from min to max, or 0 for
// "auto" and anything else.
int LG_count(const char* value, int min, int max);

// Columns for a grid width pixels wide when the setting is auto: one per 80
// units of stock's layout (scale pixels each), from 2 to LG_MAX_COLUMNS,
// eg. 3 on a 640 pixel screen at scale 2, 4 on 1024 at scale 3 and 5 on
// 1280 at scale 3.
int LG_autoColumns(int width, int scale);

// Rows when the setting is auto: about as many as keep the cells square
// for cols columns in w by h, rounding up from 0.4 of a row, from 1 to
// LG_MAX_ROWS.
int LG_autoRows(int w, int h, int cols);

typedef struct LGGrid {
	MosaicRect grid; // where the cells are, centred in the room left
	int cols, rows;
	int cell_w, cell_h;
} LGGrid;

// Lays the grid out in area, below status and header (the status pill and
// header widgets, which stay clear; w 0 when there are none), with a gap
// of 4 * scale around it. cols and rows of 0 are auto. While a cell would
// be under min_w by min_h pixels, a row goes and then a column, down to
// one of each. Returns 0, or -1 when not even one cell fits.
int LG_grid(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int cols,
	int rows, int min_w, int min_h, LGGrid* out);

// The cell at slot, counted from the top left of the grid in reading
// order (slot 0 is the first column of the first row in view).
MosaicRect LG_cell(const LGGrid* grid, int slot);

// The parts of a cell: the selected cell's highlight, inset by gap; the
// tile inside it, inset by frame, which row decorators get; the art at
// the tile's top, and the name strip name_h tall below it (h 0 when
// name_h is 0, with the art taking the whole tile).
void LG_parts(const MosaicRect* cell, int gap, int frame, int name_h, MosaicRect* highlight, MosaicRect* tile,
	MosaicRect* art, MosaicRect* name);

// The first row in view, given the one in view before (first), so the
// window moves only as far as it takes to show the selected entry, and
// never past the last row. 0 for an empty list.
int LG_window(int first, int selected, int count, int cols, int rows);

// Where a d-pad direction (MOSAIC_BUTTON_UP, DOWN, LEFT or RIGHT) moves
// the selection in count entries of cols columns. Left and right step
// through the entries in reading order; up and down keep the column. Past
// the ends, a press wraps (fresh is 1) and a held repeat stops:
//   left from the first entry goes to the last, right from the last to
//     the first;
//   up from the top row goes to the column's lowest entry;
//   down from the last row goes to the column's top entry, and down from
//     a row whose column has nothing below, as the last row is shorter,
//     goes to the last entry, press or repeat.
// Returns the new index, or selected when it doesn't move.
int LG_move(int selected, int count, int cols, int dir, int fresh);

// The letter a tile shows for a name: its first letter or digit, upper
// case, or first character that isn't ASCII (a whole UTF-8 sequence), or
// else its first character that isn't a space; "" for a name with none.
void LG_letter(const char* name, char* out, size_t size);

#endif
