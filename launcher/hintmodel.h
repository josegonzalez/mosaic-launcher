#ifndef HINTMODEL_H
#define HINTMODEL_H

// The SDL-free half of host->draw_hints and host->draw_button: checking a
// plugin's hints, the names stock draws for their buttons, and where
// stock's GFX_blitButtonGroup puts the pill. screen_plugin.c and
// plugindraw.c measure and draw.

#include <stddef.h>
#include "mosaic_plugin.h"

#define HINTS_MAX 2          // a group's hints, as stock's GFX_blitButtonGroup takes
#define HINT_LABEL_MAX 64    // a label's bytes, its terminator included

// stock's layout units, already scaled: PADDING, PILL_SIZE and BUTTON_MARGIN
typedef struct HintMetrics {
	int padding;
	int pill;
	int margin;
} HintMetrics;

// The name stock draws for button, eg. "A", "MENU" or "POWER", or NULL
// for one it has no hint for. FN1 to FN3 take the device's names for them
// from fn_names, eg. "HOME", and are NULL where it's NULL or the name is
// "", as on a device without the button. The d-pad and enum MosaicGlyph,
// which stock has icons for but never draws, are "UP", "DOWN", "LEFT",
// "RIGHT", "U/D", "L/R" and "DPAD".
const char* Hint_glyph(int button, const char* const* fn_names);

// For the launcher's build of api.c: which of the d-pad's names glyph is,
// 0 to 6 in the order Hint_glyph lists them, or -1 for any other name.
int Hint_dpad(const char* glyph);

// Checks a group and its hints, writing each one's button name to glyphs
// (room for HINTS_MAX). Returns 0, or -1 for a bad group, a count other
// than 1 to HINTS_MAX, a NULL hints, an unnamed button or a NULL or
// empty label. fn_names is as for Hint_glyph.
int Hints_check(int group, const MosaicHint* hints, int count, const char* const* fn_names, const char** glyphs);

// Which hint stock keeps on a skinny screen: the left group's first and
// the right group's last, as stock's own screens pass them.
int Hints_primary(int group, int count);

// A label's first line, cut to fit size between UTF-8 characters, into
// out. Returns its length, or -1 for a bad argument or an empty line.
int Hints_label(const char* text, char* out, size_t size);

// The pill GFX_blitButtonGroup draws on a screen_w by screen_h screen for
// hints of widths (each from GFX_getButtonWidth), into out. Returns 0, or
// -1 for a bad argument.
int Hints_place(int group, int screen_w, int screen_h, const HintMetrics* m, const int* widths, int count, MosaicRect* out);

// 1 when pill is inside the screen and clear of each of avoid, such as the
// status pill, the header and the other group's pill (any can be empty, w
// 0), else 0.
int Hints_clear(const MosaicRect* pill, int screen_w, int screen_h, const MosaicRect* avoid, int avoid_count);

#endif
