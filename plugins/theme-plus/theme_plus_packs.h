#ifndef THEME_PLUS_PACKS_H
#define THEME_PLUS_PACKS_H

// The SDL-free half of theme-plus's rotation, per-system wallpapers and
// icons and the picker's preview: which theme a rotation picks, which
// system a path is in, which image wins for it, and a theme's colours
// for the preview.

#include <stddef.h>
#include "theme_plus_model.h"

enum {
	TP_ROTATE_OFF = 0,
	TP_ROTATE_START = 1, // a random theme each time the launcher starts
	TP_ROTATE_DAILY = 2, // one theme a day
};

// The rotate setting: off, start or daily. NULL or anything else is off.
int TP_rotateMode(const char* value);

// The themes a rotation picks from, as indexes into catalog's themes:
// every one when between is NULL or empty, else the ones it names,
// separated by commas (spaces around each ignored), as TP_find finds them,
// each once, in the order given. One it can't find is logged. Returns
// how many it wrote to out (at most max).
int TP_rotateCandidates(const TPCatalog* catalog, const char* between, int* out, int max, TPLogFn log, void* ctx);

// A day as a number that changes each day: year * 10000 + month * 100 +
// day, with month from 1.
long TP_dayKey(int year, int month, int day);

// Which of count candidates a rotation shows, or -1 for off or none.
// Daily mixes day into the same index all day. Start takes random, and
// with more than one candidate never last (the index shown before, or -1).
int TP_rotatePick(int mode, int count, long day, unsigned long random, int last);

// The system a path on the card is in: a folder directly in roms (the
// card's Roms) with a tag in its name's last parentheses, such as GB in
// "Game Boy (GB)". Writes the tag to tag. Returns 0 when path is that
// folder, 1 when it's inside it, or -1 when it's in no system.
int TP_systemOf(const char* roms, const char* path, char* tag, size_t size);

// 1 for a pack's name as the settings hold it: a folder's name, not
// empty, no '/', not starting with '.', shorter than TP_NAME_MAX.
int TP_packNameOk(const char* name);

// The kinds of system art.
enum {
	TP_ART_ICON = 0,      // a system folder's thumbnail
	TP_ART_FOLDER_BG = 1, // the background while a folder is selected
	TP_ART_LIST_BG = 2,   // the background while a game in a folder is selected
};

// The files stock shows for an entry at path (on the card card) and an
// art kind, which win over a pack's and a theme's: a folder's
// .media/bg.png and the card's bg.png, a game folder's .media/bglist.png
// and the card's bg.png, or a system's <Roms>/.media/<name>.png. Writes
// up to 2 paths to out and returns how many.
int TP_stockArt(int kind, const char* card, const char* path, char out[][TP_PATH_MAX], int max);

// The system art to show for tag: none when any of stock's files exist
// (0), else <tag>.png in pack_dir, else in theme_dir (either may be NULL),
// each a regular file inside its folder. Writes its real path to out and
// returns 1, or returns 0.
int TP_systemArt(const char stock[][TP_PATH_MAX], int stock_count, const char* pack_dir, const char* theme_dir,
	const char* tag, char* out, size_t size);

// The seven colours a theme shows, for the picker's preview: base (the
// colours in use without a theme, each 0xRRGGBB), the palette's (NULL
// for none) over them, then the theme's own (parsed may be NULL).
void TP_previewColors(const TPTheme* parsed, const TPPalette* palette, const int* base, int* out);

// The picker's cursor after moving step rows through count, stopping at
// the ends or, with wrap, going round to the other end. -1 for none.
int TP_move(int cursor, int count, int step, int wrap);

// The first of fit rows the picker shows so cursor is among them, kept
// from first where it can be.
int TP_window(int cursor, int count, int fit, int first);

#endif
