#ifndef FIRST_RUN_HELP_MODEL_H
#define FIRST_RUN_HELP_MODEL_H

// The help overlay's model, apart from SDL and the host: which buttons do
// something on the screen under it and what, one line per button, and how
// it takes the buttons while it shows and opens from a held button.

#include "mosaic_plugin.h"

// What the overlay lists from.
typedef struct FrHelpSource {
	const char* screen;              // what's showing, as current_screen returns it
	const MosaicActionInfo* actions; // every action, from action_info
	const int* available;            // for each action, 1 when it would run now (action_available)
	int count;
	const MosaicHint* own;           // the screen's own buttons that aren't actions (screen_buttons)
	int own_count;
	const int* fn_there;             // FN1 to FN3: 1 for each the device has, or NULL for none
	const char* const* fn_pak;       // FN1 to FN3: the pak's name when it runs one, else NULL or ""; NULL for none
	// An action's wording, from its id and its label (the id without one),
	// such as one of the launcher's own whose hint doesn't say what it does;
	// NULL keeps the label.
	const char* (*label)(const char* id, const char* label);
	// The shared button's menu, with the labels of its actions joined by
	// ", ", such as "Menu: %s"; NULL for "%s".
	const char* menu;
} FrHelpSource;

#define FR_HELP_TEXT 256
typedef struct FrHelpLine {
	int button;               // enum MosaicButton
	char text[FR_HELP_TEXT];  // what it does there, each thing it does joined by ", "
} FrHelpLine;
#define FR_HELP_LINES 24

// The lines, one for each button that does something on the screen, in
// the order A, B, X, Y, L1, R1, L2, R2, START, SELECT, the d-pad, MENU,
// POWER, FN1 to FN3. A button's line has the screen's own buttons on it
// first, then the available actions on it: on the game list, its own and
// plugins' overrides, then plugins' bind_button ones, less those with a
// trigger an own or override one has, as the game list runs them; on
// every screen but a dialog, an FN button's action, or the pak it runs.
// Each action shows once. Two or more bind_button ones left show as the
// shared button's menu. Returns how many lines it filled, up to max.
int FR_helpLines(const FrHelpSource* source, FrHelpLine* out, int max);

// A help line's button's name, such as "A", "MENU" or "UP/DOWN", or NULL
// for the FN buttons, which the device names, and anything else.
const char* FR_helpButtonName(int button);

// How long a button is held to open the overlay, in milliseconds.
#define FR_HELP_HOLD_MS 1000

typedef struct FrHelp {
	int open;             // the overlay shows, or will on the next frame drawn
	int drawn;            // it was drawn since it opened
	unsigned int held;    // the buttons down, as the last input had them
	unsigned int swallow; // buttons down as it opened or closed, whose repeats and release it still takes
	int hold;             // the enum MosaicButton held to open it, or -1 for none
	int holding;          // hold went down alone and is still down alone
	int fired;            // this hold opened it already
	unsigned long since;  // when hold went down
	unsigned long now;    // the last tick's time
} FrHelp;

// Closed, opening from a hold of hold, an enum MosaicButton, or -1 for none.
void FR_helpInit(FrHelp* h, int hold);

// Opens it, taking the release of what's down now.
void FR_helpOpen(FrHelp* h);

// Closes it, taking the release of what's down now.
void FR_helpClose(FrHelp* h);

// Notes it was drawn: until it is, as over a screen that hides overlays,
// a press closes it and goes through.
void FR_helpDrawn(FrHelp* h);

// The buttons to take this frame, for input_filter. While it shows and has
// been drawn it takes every button but power, and any press closes it,
// power's too, which goes through. It takes the repeats and release of
// what was down as it opened or closed, until they come up. It also
// follows the hold button. *closed is set to 1 when this input closed it.
unsigned int FR_helpFilter(FrHelp* h, const MosaicInput* input, int* closed);

// 1 when the hold button has been down alone for FR_HELP_HOLD_MS while it's
// closed, once a hold, for tick.
int FR_helpTick(FrHelp* h, unsigned long now);

#endif
