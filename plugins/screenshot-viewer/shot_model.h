// Screenshot viewer, without the host: which files in the screenshots
// folder are screenshots, the game and date their names give, newest
// first, the grid and where the d-pad moves in it, the button hints, the
// scaled copies kept and how they fit. No SDL; it reads and deletes files
// in the folder with libc only, so it's tested on the host.

#ifndef SHOT_MODEL_H
#define SHOT_MODEL_H

#include <stddef.h>
#include <time.h>
#include "mosaic_plugin.h"

#define SV_NAME_MAX 256
#define SV_DIR_MAX 512 // the folder's path, so it and a name fit in SV_PATH_MAX
#define SV_PATH_MAX 1024
// screenshots listed, at most; past it the oldest are left out
#define SV_MAX_SHOTS 4096
// columns the grid has, at most
#define SV_MAX_COLS 8

// One screenshot. Stock's minarch names them
// <game>.<YYYY-MM-DD-HH-MM-SS>.png, the game as its list names it.
typedef struct SVShot {
	char file[SV_NAME_MAX]; // its name in the folder
	char game[SV_NAME_MAX]; // the game its name gives, or the name less its extension
	int has_date;           // 1 when the name gives the date
	int year, month, day, hour, minute, second;
	long long key;          // local time as YYYYMMDDhhmmss, from the name or else the file's mtime
} SVShot;

// 1 for a name that could be a screenshot: a .png, .jpg or .jpeg (any
// case) with something before the extension, not hidden (a leading dot,
// as macOS's ._ files have) and with no slash.
int SV_isImage(const char* file);

// Fills out from a file name: the game and, when the name ends in
// .<YYYY-MM-DD-HH-MM-SS> before its extension with a real date and time,
// the date (has_date 1, key from it; else has_date and key 0). Returns 0,
// or -1 for a name SV_isImage refuses.
int SV_parse(const char* file, SVShot* out);

// key for a local time.
long long SV_keyOf(const struct tm* tm);

// "2026-11-08 14:03", or "" without a date.
void SV_date(const SVShot* shot, char* out, size_t size);

// The game and date together, "Tetris · 2026-11-08 14:03", or the game
// alone without a date.
void SV_title(const SVShot* shot, char* out, size_t size);

///////////////////////////////////////
// the folder

typedef struct SVList {
	SVShot* shots; // newest first
	int count;
	int cap;
} SVList;

void SV_listInit(SVList* list);
void SV_listFree(SVList* list);

// Appends a shot; -1 when the list is full or out of memory.
int SV_listAdd(SVList* list, const SVShot* shot);

// Sorts newest first by key, and the same key by file name, last first.
void SV_sort(SVList* list);

// Lists dir's screenshots into list, newest first: regular files only, so
// links and folders are left out, looked at without following links.
// Names without a date are dated by their mtime. Returns how many, or -1
// when dir isn't a real folder (a link to one is refused too).
int SV_scan(const char* dir, SVList* list);

// 1 when dir has at least one screenshot SV_scan would list; stops at the
// first.
int SV_any(const char* dir);

// Deletes the shot at index from dir and from list. Only a regular file
// whose name SV_isImage takes, directly in dir, itself a real folder and
// not a link, is removed; a link is never followed or removed. Returns 0,
// or -1 when it's refused or unlink fails (list is left as it was).
int SV_remove(const char* dir, SVList* list, int index);

///////////////////////////////////////
// moving

// Where a step of by moves the selection in count shots: past an end it
// stops at the first or last, and from the first or last a press (fresh
// is 1) that way wraps to the other end while a held repeat stays.
int SV_step(int selected, int count, int by, int fresh);

// Up (dy -1) or down (dy 1) a row in a grid of cols: up from the top row
// and down from the bottom row stay; down onto a short last row lands on
// its last shot.
int SV_row(int selected, int count, int cols, int dy);

// The selection after the shot at selected was removed, leaving count:
// the same place, which is now the next older one, or the last. 0 when
// none are left.
int SV_afterRemove(int selected, int count);

// The first row in view (as a row number), given the one before, so the
// grid scrolls only as far as it takes to show the selected shot's row.
int SV_firstRow(int first, int selected, int count, int cols, int rows);

// The grid's hints, or the full view's (full 1): B BACK on the left and
// Y DELETE (when can_delete) and A VIEW (not in the full view) on the
// right; with no shots, B BACK alone on the right.
void SV_hints(int count, int full, int can_delete, MosaicHint left[2], int* left_count, MosaicHint right[2],
	int* right_count);

///////////////////////////////////////
// layout

// The parts of r outside hole, as switcher-plus does: up to four, or r
// itself when they don't meet. 0 for an empty r.
int SV_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]);

// status and header together (either can be NULL or empty); w 0 for none.
MosaicRect SV_hole(const MosaicRect* status, const MosaicRect* header);

// A src_w by src_h image fitted inside box keeping its shape, centred; w 0
// for an empty size.
MosaicRect SV_fit(int src_w, int src_h, const MosaicRect* box);

// part of dst, where a src_w by src_h image is stretched across dst, as
// the part of the image that lands there.
MosaicRect SV_source(const MosaicRect* dst, const MosaicRect* part, int src_w, int src_h);

// Stock's title pill in area's top left, PADDING in, PILL_SIZE tall, for
// text text_w wide with BUTTON_PADDING either side, as wide as the room
// left of hole allows. w 0 when there's no room.
MosaicRect SV_titlePill(const MosaicRect* area, const MosaicRect* hole, int scale, int text_w);

typedef struct SVGrid {
	MosaicRect room; // where the cells go
	int cols, rows;
	int cell_w, cell_h; // a cell: the thumbnail, then the name under it
	int thumb_h;
	int name_h;
	int gap;
} SVGrid;

// Lays the grid out in area: below the title pill (PADDING, PILL_SIZE and
// a PADDING) and hole, above the hint pills, PADDING in at the sides,
// centred. Cells have thumbnails in the screen's shape (aspect_w by
// aspect_h) and a name_h line under each, a PADDING apart, in the fewest
// columns from 3 that fit two rows, up to SV_MAX_COLS. With no room for a
// whole row the thumbnails get shorter to fit one. Returns 0, or -1 when
// nothing fits.
int SV_grid(const MosaicRect* area, const MosaicRect* hole, int scale, int aspect_w, int aspect_h, int name_h,
	SVGrid* out);

// The cell for the shot index places past the first one in view.
MosaicRect SV_cell(const SVGrid* grid, int slot);

///////////////////////////////////////
// scaled copies, the least recently used going first

typedef struct SVCopy {
	char file[SV_NAME_MAX];
	int w, h;
	MosaicSurface* surface; // NULL for a free slot
	unsigned long used;
} SVCopy;

// The copy of file at w by h, or NULL.
SVCopy* SV_copyFind(SVCopy* copies, int count, const char* file, int w, int h);

// A slot for a new copy: a free one, or else the least recently used (its
// surface is the caller's to free).
SVCopy* SV_copyPick(SVCopy* copies, int count);

#endif
