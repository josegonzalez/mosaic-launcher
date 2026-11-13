// Placeholder art, without the host: the sizes, colours and layout of the
// cards art_placeholder.c draws for games with no art. No SDL, and text is
// only measured through a callback, so it's tested on the host.
//
// Two styles:
//
//   title    the game's name on a card in the theme's colours: an accent
//            panel on the secondary accent, the name wrapped over up to
//            PA_LINES lines in whichever of the main colour and the
//            selected list text stands out more on the accent
//   pattern  a mirrored grid of rounded tiles in a palette, both picked
//            from a hash of the rom's path, so a game always gets the same
//            one and different games get different ones
//
// The thumbnail is square and the box 3:4, each as big as the built-in
// list shows a thumbnail, so neither is scaled up.

#ifndef PLACEHOLDER_MODEL_H
#define PLACEHOLDER_MODEL_H

#include <stddef.h>
#include <stdint.h>

enum PAStyle {
	PA_STYLE_TITLE,
	PA_STYLE_PATTERN,
};

// The style a setting's value names, or title for anything else.
int PA_style(const char* value);

// The value the style setting gives: the setting itself, or for theme (or
// NULL or "", unset) the theme's art-placeholder.style, theme_value,
// which is NULL when the theme doesn't set it. NULL for neither, which
// PA_style takes as title.
const char* PA_themed(const char* setting, const char* theme_value);

// The size of a card of kind (an enum MosaicArtKind) on a w by h screen:
// writes it and returns 1 for the thumbnail and the box, or returns 0 for
// any other kind or a screen too small for one.
int PA_size(int kind, int screen_w, int screen_h, int* w, int* h);

// FNV-1a: the same 32 bits for the same text on every run and device.
uint32_t PA_hash(const char* text);

// The next number from a xorshift32 generator whose state is *state
// (never 0; a 0 state is reseeded).
uint32_t PA_next(uint32_t* state);

// 0xRRGGBB for a hue from 0 to 359 and a saturation and value from 0 to
// 100.
unsigned int PA_hsv(int hue, int saturation, int value);

// Relative brightness of 0xRRGGBB, from 0 to 255.
int PA_luma(unsigned int rgb);

// Whichever of a and b stands out more on bg; a on a tie.
unsigned int PA_contrast(unsigned int bg, unsigned int a, unsigned int b);

// A theme colour as theme_color returns it, or fallback for -1.
unsigned int PA_themeOr(int color, unsigned int fallback);

///////////////////////////////////////
// title cards

#define PA_LINES 3
#define PA_LINE_LEN 256

// How wide text is, as text_width measures it; data is passed through.
typedef int (*PAWidth)(const char* text, void* data);

// Splits name into up to PA_LINES lines of at most max_width, breaking at
// spaces. A word wider than max_width gets a line of its own, and the last
// line takes whatever is left, for the caller to truncate. Leading,
// trailing and repeated spaces are dropped. Returns how many lines, 0 for
// an empty name.
int PA_wrap(const char* name, int max_width, PAWidth width, void* data, char lines[PA_LINES][PA_LINE_LEN]);

// Where a title card's panel and text go on a w by h card, at the
// launcher's scale (FIXED_SCALE).
typedef struct PATitle {
	int margin;    // around the panel
	int radius;    // the panel's corners
	int padding;   // inside the panel, around the text
	int text_w;    // the widest a line can be
} PATitle;

void PA_titleLayout(int w, int h, int scale, PATitle* out);

// The top of the first of count lines line_h tall, centred on a card h
// tall.
int PA_textTop(int h, int count, int line_h);

///////////////////////////////////////
// patterns

#define PA_COLS 6
#define PA_MAX_ROWS 12

// A pattern: tiles in a grid PA_COLS wide, mirrored left to right, each
// 0 (none), 1 or 2 (one of two colours).
typedef struct PAPattern {
	unsigned int bg;         // behind the tiles
	unsigned int colors[2];  // tiles 1 and 2
	int rows;                // as many as keep the tiles square
	unsigned char tiles[PA_MAX_ROWS][PA_COLS];
} PAPattern;

// The pattern for a game, from its path, on a w by h card.
void PA_pattern(const char* path, int w, int h, PAPattern* out);

// The tile at col, row of a w by h card with rows rows: writes x, y and
// size and returns 1, or returns 0 outside the grid. Tiles are square,
// with a gap of a tenth of a cell around each, and the grid is centred.
int PA_tile(int w, int h, int rows, int col, int row, int* x, int* y, int* size);

#endif
