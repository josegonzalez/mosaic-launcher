#ifndef THEME_PLUS_MODEL_H
#define THEME_PLUS_MODEL_H

// The SDL-free half of theme-plus: theme.txt and stock's palette files,
// paths that must stay inside a theme's folder, the list of themes on the
// card, in the plugin and from stock's palettes, and the settings that
// override a theme's values, merged into a MosaicTheme.

#include <stddef.h>
#include "mosaic_plugin.h"

#define TP_NAME_MAX 64
#define TP_PATH_MAX 512
#define TP_FILE_MAX 16384 // theme.txt and palette files are read up to this many bytes
#define TP_THEMES_MAX 128
#define TP_PALETTES_MAX 128
#define TP_VERSION_MAX 1 // the theme.txt and palette format versions this understands
#define TP_STEP_UNSET -100

// Logs one line about a file being read, for host->log.
typedef void (*TPLogFn)(void* ctx, const char* message);

// A theme.txt as written: paths are still relative to its folder.
typedef struct TPTheme {
	char name[TP_NAME_MAX];       // name=, or "" for the folder's name
	char palette[TP_NAME_MAX];    // palette=, or ""
	int colors[MOSAIC_THEME_COLORS]; // color1= to color7= as 0xRRGGBB, or MOSAIC_THEME_UNSET
	char font[TP_PATH_MAX];
	int text_step;                // -2 to 8, or TP_STEP_UNSET
	int contrast;                 // tenths, 10 to 210, or 0
	int pill_size;                // 18 to 60, or 0
	int button_size;              // 12 to 56, or 0
	int padding;                  // 1 to 30, or 0
	char wallpaper[TP_PATH_MAX];
	char wallpaper_list[TP_PATH_MAX];
	char wallpaper_quick[TP_PATH_MAX];
	char fallback_font[TP_PATH_MAX];
} TPTheme;

// A palette file in stock's format.
typedef struct TPPalette {
	char name[TP_NAME_MAX];
	int colors[MOSAIC_THEME_COLORS]; // each 0xRRGGBB; ones the file leaves out are stock's defaults
} TPPalette;

// Stock's default colours, which a palette file's missing colours get.
extern const int TP_STOCK_COLORS[MOSAIC_THEME_COLORS];

// A colour as stock's palettes write it: 0xRRGGBB or 0xRRGGBBAA (the 0x,
// or a #, optional), with nothing else on the line. The alpha is dropped.
// Returns 0xRRGGBB, or -1 for anything else.
int TP_parseColor(const char* text);

// A contrast ratio such as "4.5" or "7", from 1 to 21 with at most one
// decimal, in tenths (45). Returns -1 for anything else.
int TP_parseContrast(const char* text);

// A whole number from lo to hi, optionally signed. Returns 0 and sets out,
// or -1.
int TP_parseInt(const char* text, int lo, int hi, int* out);

// 1 for a path a theme may name: relative, not empty, no "." or ".."
// part, no empty part, no backslash and shorter than TP_PATH_MAX.
int TP_relativePathOk(const char* path);

// Parses theme.txt's text. Each line is key=value; blank lines and ones
// starting with # are skipped. Unknown keys and lines without = are
// logged and ignored, and bad values are logged and dropped. Returns 0,
// or -1 for a version= newer than TP_VERSION_MAX (also logged), when the
// theme isn't used.
int TP_parseTheme(const char* text, TPTheme* out, TPLogFn log, void* ctx);

// Parses a palette file's text the way stock reads it: version=, name=
// and color1= to color7=, other lines ignored. A missing name comes from
// file_name (without .txt, underscores as spaces), and a colour left out
// or unreadable is stock's default. Returns 0, or -1 for a version newer
// than TP_VERSION_MAX, which stock skips too.
int TP_parsePalette(const char* text, const char* file_name, TPPalette* out);

// Reads a regular file of at most TP_FILE_MAX bytes into a new string.
// Returns NULL when it's missing, isn't a regular file or is too big.
char* TP_readFile(const char* path);

// Resolves rel, checked with TP_relativePathOk, under the folder dir to
// a regular file whose real path is inside dir's real path (so a symlink
// may point within the folder but not out of it). Writes the real path
// to out and returns 0, or returns -1 with a reason in why.
int TP_resolve(const char* dir, const char* rel, char* out, size_t size, const char** why);

// Resolves a path on the card, relative to the folder card or a full
// path, to a regular file whose real path is inside card's real path.
// Writes the real path to out and returns 0, or returns -1 with a reason
// in why.
int TP_cardFile(const char* card, const char* path, char* out, size_t size, const char** why);

enum {
	TP_KIND_CARD = 1,    // <sd>/Themes/<folder>/theme.txt
	TP_KIND_BUNDLED = 2, // <plugin>/themes/<folder>/theme.txt
	TP_KIND_PALETTE = 3, // a stock palette, as a colours-only theme
};

// A theme the setting can name.
typedef struct TPEntry {
	int kind;
	char id[TP_NAME_MAX];    // what the setting holds: the folder's name, or the palette's
	char label[TP_NAME_MAX]; // what the picker shows: the theme's name=, or the id
	char dir[TP_PATH_MAX];   // the folder, for a card or bundled theme
} TPEntry;

typedef struct TPCatalog {
	TPEntry themes[TP_THEMES_MAX];
	int theme_count;
	TPPalette palettes[TP_PALETTES_MAX]; // every palette, for palette=
	int palette_count;
	int dropped; // themes or palettes past the caps
} TPCatalog;

// Lists the themes: folders in themes_dir (<sd>/Themes) then bundled_dir
// (or NULL), each a real folder (not a symlink, not starting with .) with
// a theme.txt, then the palettes in user_palettes (<sd>/Palettes) then
// builtin_palettes (<sd>/.system/res/palettes). Each group is sorted by
// label, and one whose id matches an earlier one's (ignoring case) isn't
// listed, so the card's themes come first, then the plugin's, then the
// palettes. "stock" is never an id.
void TP_catalog(TPCatalog* catalog, const char* themes_dir, const char* bundled_dir,
	const char* user_palettes, const char* builtin_palettes);

// The listed theme a setting names: the id exactly, else ignoring case,
// or NULL.
const TPEntry* TP_find(const TPCatalog* catalog, const char* id);

// The palette named name (exactly, else ignoring case), user palettes
// before stock's own, or NULL.
const TPPalette* TP_findPalette(const TPCatalog* catalog, const char* name);

// The settings that override a theme: each "theme" (or missing) to keep
// the theme's value.
typedef struct TPOverrides {
	int background;  // 0xRRGGBB, or -1 for the theme's
	int text_step;   // TP_STEP_UNSET for the theme's
	int contrast;    // tenths, 0 for none, -1 for the theme's
	int ui;          // a pill size, 0 for stock's, -1 for the theme's
	int padding;     // 1 to 30, 0 for worked out from the pill, -1 for the theme's
	char fallback_font[TP_PATH_MAX]; // a real path on the card, or "" for the theme's
} TPOverrides;

// Reads the settings' values (NULL as "theme"); an unknown one is the
// theme's, logged.
TPOverrides TP_overrides(const char* background, const char* text_size, const char* contrast,
	const char* ui_size, const char* margins, TPLogFn log, void* ctx);

// Reads the fallback_font setting (NULL or "" for the theme's) into
// overrides: a file on the card, as TP_cardFile. One that won't resolve
// is logged and the theme's is used.
void TP_overrideFallback(TPOverrides* overrides, const char* card, const char* value, TPLogFn log, void* ctx);

// The button size theme-plus uses with a ui_size pill, or 0 for stock's.
int TP_buttonFor(int pill);

// Fills theme (at stock's values, as the host hands it) from a parsed
// theme in the folder dir (NULL for a palette theme), its palette (NULL
// for none), then the overrides. A palette sets all seven colours and the
// theme's own colours go over it. A path that won't resolve inside dir is
// logged and dropped. Fields theme doesn't have (MOSAIC_THEME_HAS) are
// skipped.
void TP_apply(MosaicTheme* theme, const TPTheme* parsed, const char* dir, const TPPalette* palette,
	const TPOverrides* overrides, TPLogFn log, void* ctx);

#endif
