#ifndef FIRST_RUN_WIZARD_MODEL_H
#define FIRST_RUN_WIZARD_MODEL_H

// The first run wizard's model, apart from SDL and the host: which pages
// it has, how the buttons move through them, when it opens by itself,
// the hints, the suggested plugins and wrapping text into lines.

#include <stddef.h>
#include "mosaic_plugin.h"

enum FrPage {
	FR_WELCOME,
	FR_TOGGLES,
	FR_PIN,
	FR_PLUGINS,
	FR_DONE,
	FR_PAGES,
};

// What a press on the wizard does, for the plugin to carry out.
enum FrAction {
	FR_NOTHING, // nothing changed
	FR_REDRAW,  // the page or the selected row changed
	FR_FLIP,    // flip the selected stock toggle (row), then redraw
	FR_FINISH,  // A on the last page: done, close and don't show again
	FR_SKIP,    // Y: close and don't show again
};

#define FR_TOGGLES_MAX 8

typedef struct FrWizard {
	int has[FR_PAGES]; // 1 for each page the wizard shows
	int page;          // the page showing
	int rows;          // rows on the toggles page
	int row;           // the selected one
} FrWizard;

// Starts on the welcome page. The welcome and last pages always show; the
// toggles page shows with rows > 0, and the PIN and plugins pages with pin
// and plugins 1.
void FR_init(FrWizard* w, int rows, int pin, int plugins);

// The next or previous page shown, or -1 past either end.
int FR_nextPage(const FrWizard* w, int page);
int FR_prevPage(const FrWizard* w, int page);

// The page's number among those shown, from 1, and how many are shown.
int FR_pageNumber(const FrWizard* w, int page);
int FR_pageCount(const FrWizard* w);

// What the buttons pressed this frame (MOSAIC_BUTTON_BIT masks) do: A goes
// on, or finishes on the last page; B goes back, and does nothing on the
// first; Y skips the rest; up and down move through the toggles page's
// rows, wrapping, and left and right flip the selected one. The first
// button in that order wins.
int FR_input(FrWizard* w, unsigned int pressed);

// 1 when the wizard should open by itself: it hasn't been seen (finished
// or skipped), it hasn't opened by itself this run yet, and what shows is
// the game list or the home screen (home NULL when there's none), not
// the quick menu, the game switcher, a dialog or another plugin's screen.
int FR_shouldOpen(int seen, int opened, const char* current, const char* home);

// The hints for page: B BACK (but on the first page) and Y SKIP (but on
// the last) on the left, A NEXT or A DONE on the right.
void FR_hints(const FrWizard* w, MosaicHint left[2], int* left_count, MosaicHint right[1], int* right_count);

// Suggested plugins, from a comma-separated list of ids (spaces around
// them dropped, empty ones and repeats left out), up to FR_SUGGEST_MAX.
#define FR_SUGGEST_MAX 8
#define FR_ID_MAX 64
int FR_parseIds(const char* text, char ids[][FR_ID_MAX], int max);

// What a suggested plugin does, in a few words, for the bundled ones the
// wizard knows, or NULL.
const char* FR_describe(const char* id);

// 1 when plugins.txt's text turns id on (its last id= line says on).
int FR_pluginOn(const char* plugins_txt, const char* id);

// Wraps text into lines no wider than max_w by width(line, data), at
// spaces, a word too wide alone taking a line of its own. Fills up to max
// lines of FR_LINE_MAX bytes and returns how many it filled; the last is
// cut with "..." when the text doesn't fit in them.
#define FR_LINE_MAX 160
int FR_wrap(const char* text, int max_w, int (*width)(const char* line, void* data), void* data, char (*lines)[FR_LINE_MAX],
	int max);

#endif
