// Fancy layout, without the host: where the list's rows and the art panel
// beside them go, what the panel shows, which names show, and where stock
// and minarch keep a game's last save state slot and its preview. No SDL
// and no file access (a callback answers whether a file exists), so it's
// tested on the host.

#ifndef FANCY_MODEL_H
#define FANCY_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// Stock's list metrics, in units of its layout (scale pixels each).
#define FL_PILL 30           // a row's height
#define FL_PADDING 10        // above the first row
#define FL_MARGIN 5          // left of the rows
#define FL_NAME_PADDING 12   // either side of a name on its pill
#define FL_PANEL_SHARE 45    // the panel's share of the width, in percent, as stock's thumbnail

typedef struct FLLayout {
	MosaicRect panel; // where the art goes, right of the rows; w 0 when there's no panel
	int row_x, row_y, row_w, row_h; // the first row in view; each next one row_h lower
	int top_w;        // the first row's width, kept clear of the status pill and header widgets
	int name_pad;     // either side of a name on its pill
} FLLayout;

// Lays the list out in area as stock's list does: rows FL_PILL tall from
// FL_PADDING down and FL_MARGIN in. With panel 1 there's a panel on the
// right, FL_PANEL_SHARE of area's width and ending 3 * FL_MARGIN short of
// its right edge, as stock's thumbnail does; the rows end as they do beside
// stock's thumbnail, and the panel runs from below status and header (the
// status pill and header widgets, w 0 when there are none) to FL_PADDING
// above area's bottom. With panel 0 the rows take the width. The first
// row stops short of status and header where it would reach them. Returns
// 0, or -1 when not even a row's padding fits.
int FL_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int panel,
	FLLayout* out);

// How many rows fit in area under layout, at least 1.
int FL_rows(const FLLayout* layout, const MosaicRect* area);

// The first entry in view: start, the built-in list's, moved only as far as
// it takes to show selected in rows rows, and never past the last row. 0
// for an empty list.
int FL_window(int start, int selected, int count, int rows);

// The row in view at slot (0 for the first), and its name's pill for a
// name text_w wide: name_pad either side of it, cut to the row.
MosaicRect FL_row(const FLLayout* layout, int slot);
MosaicRect FL_name(const FLLayout* layout, const MosaicRect* row, int text_w);

// What the panel shows, as a mask of these.
enum {
	FL_SHOW_BOX = 1,
	FL_SHOW_PREVIEW = 2,
};

// The panel's art: nothing while show_art is 0; else the box (has_box) and
// the last save state's preview (has_preview), or only the preview when
// prefer_preview is 1 and there is one.
int FL_panelArt(int show_art, int prefer_preview, int has_box, int has_preview);

// The panel's art while a plugin's art_replace surface takes the box's
// place: nothing while show_art is 0; else the replacement, which the
// preview never stands in for, and the preview under it (has_preview)
// only when prefer_preview is 0.
int FL_replacedArt(int show_art, int prefer_preview, int has_preview);

// The smallest rect holding a and b, either of which may be empty (w or h
// 0); empty when both are.
MosaicRect FL_around(const MosaicRect* a, const MosaicRect* b);

// Where each part of the panel's art goes: with both, the box in the top
// half and the preview in the bottom half, gap apart; with one, it takes
// the panel. The rect for a part that isn't shown gets w 0.
void FL_panelParts(const MosaicRect* panel, int show, int gap, MosaicRect* box, MosaicRect* preview);

// What's left of to after fitting a w by h image inside it, keeping its
// shape, centred.
MosaicRect FL_contain(const MosaicRect* to, int w, int h);

// Whether a row's name shows, as the built-in list decides: never while
// show_names is 0, always when the name isn't hidden or art is off, and a
// hidden name only on the selected row while the panel has no art.
int FL_nameShown(int show_names, int hidden, int show_art, int selected, int has_art);

///////////////////////////////////////
// save states, where stock looks for them

// The system tag stock files a game's states under, as its getEmuName
// works it out: the system folder's name (the first folder below roms) or
// what's in its last parentheses, eg. GB for "Game Boy (GB)". Returns 1,
// or 0 for a path that isn't inside a system folder (writing "").
int FL_tagOf(const char* roms, const char* path, char* out, size_t size);

// The file stock resumes a game by, given whether a file exists: for a
// folder (is_dir 1), its "<folder>/<folder>.cue", or else
// "<folder>/<folder>.m3u", or nothing; then, for anything but an .m3u, the
// .m3u named for the folder it's in when there is one, so a disc resumes
// by its game's .m3u; or else path itself. Writes it and returns 1, or
// returns 0 for a folder with neither (or a path too long).
int FL_resumeFile(const char* path, int is_dir, int (*exists)(const char* path, void* ctx), void* ctx, char* out,
	size_t size);

// Where minarch keeps the last used state slot for the file stock resumes
// by (from FL_resumeFile), and the start of its states' previews' paths:
// slot is "<sdcard>/.userdata/shared/.minui/<tag>/<file name>.txt" and stem
// the same without ".txt". Returns 1, or 0 for a file outside roms'
// system folders or a path too long.
int FL_slotPaths(const char* sdcard, const char* roms, const char* file, char* slot, char* stem, size_t size);

// The slot a last-slot file's text names, read as stock reads it with
// atoi: spaces, a sign and digits, with "" or anything else being 0.
// Returns -1 for a negative slot or one past 999, which no preview has.
int FL_slot(const char* text);

// A slot's preview: "<stem>.<slot>.bmp". Returns 1, or 0 when it doesn't fit.
int FL_previewPath(const char* stem, int slot, char* out, size_t size);

#endif
