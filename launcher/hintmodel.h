#ifndef HINTMODEL_H
#define HINTMODEL_H

// The SDL-free half of host->draw_hints: checking a plugin's hints, the
// names stock draws for their buttons, and where stock's
// GFX_blitButtonGroup puts the pill. screen_plugin.c measures and draws.

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
// for one it has no hint for (the d-pad, FN1 to FN3, anything else).
const char* Hint_glyph(int button);

// Checks a group and its hints, writing each one's button name to glyphs
// (room for HINTS_MAX). Returns 0, or -1 for a bad group, a count other
// than 1 to HINTS_MAX, a NULL hints, an unnamed button or a NULL or
// empty label.
int Hints_check(int group, const MosaicHint* hints, int count, const char** glyphs);

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
