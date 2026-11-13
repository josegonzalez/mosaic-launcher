#ifndef LAUNCHER_THEMEMODEL_H
#define LAUNCHER_THEMEMODEL_H

// The SDL-free half of the theme slot: which of an owner's values are
// safe to use, the UI sizes worked out from them the way stock's comments
// in defines.h and platform.h work out its own, the contrast clamp, and
// which wallpaper fills which spot. theme.c applies the result.

#include <stddef.h>
#include <stdint.h>
#include "mosaic_plugin.h"

#define THEME_PILL_MIN 18
#define THEME_PILL_MAX 60
#define THEME_BUTTON_MIN 12
#define THEME_PADDING_MIN 1
#define THEME_PADDING_MAX 30
#define THEME_STEP_MIN -2
#define THEME_STEP_MAX 8
#define THEME_CONTRAST_MIN 10
#define THEME_CONTRAST_MAX 210
#define THEME_ROWS_MIN 3 // the built-in list always shows at least this many rows

// Stock's values on this device right now, before scaling.
typedef struct ThemeStock {
	int height;  // the screen's height in layout units (FIXED_HEIGHT / FIXED_SCALE)
	int pill;    // PILL_SIZE
	int button;  // BUTTON_SIZE
	int padding; // PADDING
	int rows;    // MAIN_ROW_COUNT
	int font_large, font_medium, font_micro; // FONT_LARGE, FONT_MEDIUM and FONT_MICRO
} ThemeStock;

// The sizes a theme set, 0 for each it left to stock.
typedef struct ThemeSizes {
	int pill;
	int button;
	int padding;
	int text_step;
} ThemeSizes;

// Which fields of a MosaicTheme were refused, and why, for the log.
#define THEME_REFUSED_MAX 16
typedef struct ThemeRefusals {
	int count;
	char lines[THEME_REFUSED_MAX][160];
} ThemeRefusals;

// The rows that fit above the button hints, as h700's platform.h puts it:
// (height - 2 * padding - pill) / pill. It gives every stock device's
// MAIN_ROW_COUNT from its PADDING.
int ThemeModel_rows(int height, int pill, int padding);

// The margins for a pill size, as defines.h's comment puts it: a third of
// the pill when it divides the screen's height, otherwise a third of what's
// left over, or a third of the pill when that's under an eighth of it
// (too thin to see).
int ThemeModel_padding(int height, int pill);

// The size the launcher uses for each, from what a theme set (0 for
// stock's) and stock's: a set pill with no padding gets a worked out
// padding, and the rows follow whatever pill and padding are used. With
// neither set (or the pill set to stock's), padding and rows are stock's
// own, which on some devices aren't what the formulas give.
int ThemeModel_pill(const ThemeSizes* sizes, const ThemeStock* stock);
int ThemeModel_button(const ThemeSizes* sizes, const ThemeStock* stock);
int ThemeModel_paddingFor(const ThemeSizes* sizes, const ThemeStock* stock);
int ThemeModel_rowsFor(const ThemeSizes* sizes, const ThemeStock* stock);

// Checks a theme's sizes against stock's: each out of range, or that
// leaves the list fewer than THEME_ROWS_MIN rows or text that doesn't fit
// its pill or button, is refused (left at 0) with a line in refused.
// Returns the sizes to use.
ThemeSizes ThemeModel_checkSizes(const MosaicTheme* theme, const ThemeStock* stock, ThemeRefusals* refused);

// 1 for a colour a theme may set: MOSAIC_THEME_UNSET or 0xRRGGBB.
int ThemeModel_colorOk(int color);

// Stock's colour format (0xRRGGBBAA, opaque) for 0xRRGGBB.
uint32_t ThemeModel_stockColor(int rgb);

// The WCAG contrast ratio of two stock colours, times 10.
int ThemeModel_contrast(uint32_t a, uint32_t b);

// Moves the text colours of colors (stock's 7, 0xRRGGBBAA) towards black
// or white, whichever their backdrop allows more contrast with, until each
// keeps a contrast of at least min_tenths against it: list text (color4)
// on the background (color7), selected list text (color5) on the selected
// row's pill (color1), and hints (color6) on the hint and status pills
// (color2). The backdrops are left alone. Returns how many it moved.
int ThemeModel_clampContrast(uint32_t colors[7], int min_tenths);

// Resolves path to a regular file under card_root (both may have
// symlinks in them; the file they end at must be under card_root's real
// path). Writes the real path to out and returns 0, or returns -1 with a
// reason in why.
int ThemeModel_cardFile(const char* card_root, const char* path, char* out, size_t size, const char** why);

// Stock's hint styles (config.h's INPUT_STYLE_*, which theme.c checks
// these against): text, then its three icon sets.
#define THEME_HINT_STOCK_TEXT -1
#define THEME_HINT_STOCK_SHAPES 2

// 1 for a hint style a theme may set: an enum MosaicHintStyle,
// MOSAIC_HINT_STYLE_USER included.
int ThemeModel_hintStyleOk(int style);

// The stock hint style the launcher draws in: theme_style's, or stock (the
// user's, from Settings) for MOSAIC_HINT_STYLE_USER or anything else.
int ThemeModel_hintStock(int theme_style, int stock);

// The enum MosaicHintStyle for a stock hint style. One stock doesn't have,
// which stock's Settings never writes, is MOSAIC_HINT_STYLE_TEXT.
int ThemeModel_hintStyle(int stock);

// The wallpaper spots.
enum {
	THEME_WALL_MAIN,  // stock's <sd>/bg.png, which a folder with no .media/bg.png falls back to
	THEME_WALL_LIST,  // a game folder's .media/bglist.png, falling back to bg.png
	THEME_WALL_QUICK, // the quick menu's .media/quick_<name>.png and quick.png
};

// The wallpapers a theme set, checked, "" for none.
typedef struct ThemeWalls {
	char main[512];
	char list[512];
	char quick[512];
} ThemeWalls;

// The theme's wallpaper for a spot whose stock files are all missing, or
// NULL for none: the list falls back to the main one, as stock's bglist
// falls back to bg.png.
const char* ThemeModel_wallpaper(const ThemeWalls* walls, int spot);

#endif
