#ifndef FIRST_RUN_WIZARD_MODEL_H
#define FIRST_RUN_WIZARD_MODEL_H

// The first run wizard's model, apart from SDL and the host: which pages
// it has, how the buttons move through them, when it opens by itself,
// the hints, the suggested plugins, other plugins' setup services' replies
// and manifests, the buttons page's lines and wrapping text into lines.

#include <stddef.h>
#include "mosaic_plugin.h"

enum FrPage {
	FR_WELCOME,
	FR_TOGGLES,
	FR_SETUP,
	FR_PIN,
	FR_PLUGINS,
	FR_BUTTONS,
	FR_DONE,
	FR_PAGES,
};

// What a press on the wizard does, for the plugin to carry out.
enum FrAction {
	FR_NOTHING, // nothing changed
	FR_REDRAW,  // the page, the selected row or the scroll changed
	FR_FLIP,    // flip the selected stock toggle (row), then redraw
	FR_CHANGE,  // move the selected setup row's value by dir, then redraw
	FR_FINISH,  // A on the last page: done, close and don't show again
	FR_SKIP,    // Y: close and don't show again
	FR_CLOSE,   // A or B on the buttons page opened on its own: close
};

#define FR_TOGGLES_MAX 8

typedef struct FrWizard {
	int has[FR_PAGES];  // 1 for each page the wizard shows
	int rows[FR_PAGES]; // each page's rows: toggles, setup rows, plugins, or the buttons page's lines
	int page;           // the page showing
	int row;            // the selected row, or on the buttons page the first line shown
	int visible;        // the buttons page's lines that fit, as last drawn
	int dir;            // with FR_CHANGE: -1 for left, 1 for right
	int alone;          // 1 for the buttons page opened on its own
} FrWizard;

// Starts on the welcome page. The welcome and last pages always show; the
// toggles, setup, plugins and buttons pages show with rows for them (the
// toggles capped at FR_TOGGLES_MAX), and the PIN page with pin 1.
void FR_init(FrWizard* w, int toggles, int setups, int pin, int plugins, int lines);

// Only the buttons page, with lines lines, for opening it on its own.
void FR_initButtons(FrWizard* w, int lines);

// The next or previous page shown, or -1 past either end.
int FR_nextPage(const FrWizard* w, int page);
int FR_prevPage(const FrWizard* w, int page);

// The page's number among those shown, from 1, and how many are shown.
int FR_pageNumber(const FrWizard* w, int page);
int FR_pageCount(const FrWizard* w);

// What the buttons pressed this frame (MOSAIC_BUTTON_BIT masks) do: A goes
// on, or finishes on the last page; B goes back, and does nothing on the
// first; Y skips the rest. Up and down move through the toggles, setup and
// plugins pages' rows, wrapping, and scroll the buttons page a line at a
// time while there's more past what's visible; left and right flip the
// selected toggle or change the selected setup row. On the buttons page
// alone, A and B close it and Y does nothing. The first button in that
// order wins.
int FR_input(FrWizard* w, unsigned int pressed);

// Sets how many of the buttons page's lines fit, moving the first line
// shown up when the page would show less than it could.
void FR_setVisible(FrWizard* w, int visible);

// 1 when the wizard should open by itself: it hasn't been seen (finished
// or skipped), it hasn't opened by itself this run yet, and what shows is
// the game list or the home screen (home NULL when there's none), not
// the quick menu, the game switcher, a dialog or another plugin's screen.
int FR_shouldOpen(int seen, int opened, const char* current, const char* home);

// The hints for page: B BACK (but on the first page) and Y SKIP (but on
// the last) on the left, A NEXT or A DONE on the right. The buttons page
// alone has B BACK only.
void FR_hints(const FrWizard* w, MosaicHint left[2], int* left_count, MosaicHint right[1], int* right_count);

// The buttons the wizard does something with on its page, for its
// screens' buttons hook, in English: A, B and Y as FR_input takes them,
// then up and down to pick a row or scroll the buttons page while there's
// more than fits, and left and right to change a toggle or a setup row.
// Fills up to max of out and returns how many there are.
int FR_wizardButtons(const FrWizard* w, MosaicHint* out, int max);

// Suggested plugins, from a comma-separated list of ids (spaces around
// them dropped, empty ones and repeats left out), up to FR_SUGGEST_MAX.
#define FR_SUGGEST_MAX 8
#define FR_ID_MAX 64
int FR_parseIds(const char* text, char ids[][FR_ID_MAX], int max);

// 1 when the plugin id is loaded, as host->plugin_loaded says, or 0 on a
// launcher without it.
int FR_pluginLoaded(const MosaicHost* host, const char* id);

// The value of key in key=value lines, such as a service's reply or a
// plugin.txt: the first line with it wins, spaces around the value and a
// '\r' dropped, copied into out cut to fit. Returns its length, or -1
// without such a line.
int FR_value(const char* text, const char* key, char* out, size_t size);

// What a plugin's plugin.txt says about it, when its id= is id: its name=
// (the id without one) into name, and its description=, or its summary=
// without one, into text ("" without either). Returns 1, or 0 for another
// plugin's.
int FR_manifest(const char* manifest, const char* id, char* name, size_t name_size, char* text, size_t text_size);

// A plugin's <id>.setup reply for one key (see docs/plugin-api.md): its
// value now, its choices' values and labels, and whether a change shows
// only from the next start.
#define FR_CHOICES_MAX 32
#define FR_VALUE_MAX 128
typedef struct FrSetup {
	char value[FR_VALUE_MAX];
	int count;
	char values[FR_CHOICES_MAX][FR_VALUE_MAX];
	char labels[FR_CHOICES_MAX][FR_VALUE_MAX];
	int restart;
} FrSetup;

// Reads reply's key, key.count, key.<i>, key.label.<i> (the value without
// one) and restart, up to FR_CHOICES_MAX choices. Returns 0, or -1
// without key or any choice.
int FR_parseSetup(const char* reply, const char* key, FrSetup* out);

// The current value's choice, or -1 when it isn't one.
int FR_setupIndex(const FrSetup* s);

// The current value's label, or the value itself when it isn't a choice.
const char* FR_setupLabel(const FrSetup* s);

// The choice dir (-1 or 1) from the current one, wrapping, or the first
// or last when the current value isn't one; -1 with no choices.
int FR_setupStep(const FrSetup* s, int dir);

// The buttons page's lines: a heading for each button with something on
// it, in the order A, B, X, Y, L1, R1, L2, R2, START, SELECT, then the FN
// buttons the device has (fn_there[i] for FN1 to FN3), then one for the
// actions on no button, each followed by its items. A button's items are
// the actions' bindings on it, in the order action_info lists them; an FN
// button's are its bindings with screen -1 and, when it runs a pak
// (fn[i].action "pak:<folder>"), that pak. Headings without items are
// left out. Returns how many lines it filled, up to max.
typedef struct FrLine {
	int heading;     // 1 for a heading, 0 for an item
	int button;      // the enum MosaicButton the line is under, or -1 for no button
	int screen;      // an item's enum MosaicScreen, or -1 for an FN button's or no button's
	char label[128]; // an item's text: the action's label (its id without one), or the pak's name
} FrLine;
#define FR_LINES_MAX 192
int FR_buttonLines(const MosaicActionInfo* actions, int count, const MosaicFnBinding fn[3], const int fn_there[3], FrLine* out,
	int max);

// A button's name for a heading or a log, such as "A" or "SELECT", or NULL
// for the FN buttons, which the device names, and anything else.
const char* FR_buttonName(int button);

// Wraps text into lines no wider than max_w by width(line, data), at
// spaces, a word too wide alone taking a line of its own. Fills up to max
// lines of FR_LINE_MAX bytes and returns how many it filled; the last is
// cut with "..." when the text doesn't fit in them.
#define FR_LINE_MAX 160
int FR_wrap(const char* text, int max_w, int (*width)(const char* line, void* data), void* data, char (*lines)[FR_LINE_MAX],
	int max);

#endif
