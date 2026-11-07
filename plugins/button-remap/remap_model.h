#ifndef REMAP_MODEL_H
#define REMAP_MODEL_H

// What each game list button is remapped to, read from the plugin's
// settings, and the host calls that put it in place. No SDL and no host
// here, so the host tests can check it on its own.

#define REMAP_BUTTONS 10 // one per enum MosaicButton
#define REMAP_ACTIONS_MAX 4
#define REMAP_ID_LEN 64
#define REMAP_CALLS_MAX (REMAP_BUTTONS * REMAP_ACTIONS_MAX)

typedef struct RemapButton {
	int set;     // 1 when the button is remapped; 0 leaves it as it was
	int trigger; // MOSAIC_TRIGGER_PRESS or MOSAIC_TRIGGER_RELEASE
	int count;   // 0 with set: the button does nothing
	char actions[REMAP_ACTIONS_MAX][REMAP_ID_LEN]; // tried in order, the first available runs
} RemapButton;

typedef struct RemapTable {
	RemapButton buttons[REMAP_BUTTONS]; // indexed by enum MosaicButton
} RemapTable;

// The setting key for a button, eg. "a" or "l1", or NULL for one that
// isn't a MosaicButton.
const char* Remap_key(int button);

// 1 for the buttons the game list does something with itself (A, B, X,
// L1, R1 and SELECT), which a remap overrides; the rest are left free, and
// a remap shares them with other plugins' bindings.
int Remap_stockOwned(int button);

enum RemapParse {
	REMAP_UNSET, // empty: the button stays as it was
	REMAP_SET,
	REMAP_BAD,   // out is left unset
};

// Parses one button's setting: an optional "press:" or "release:" (press
// when left out), then action ids separated by commas, eg.
// "release:gamelist.resume, gamelist.open", or "none" for nothing. Ids are
// letters, digits and . _ -. More than REMAP_ACTIONS_MAX ids, or a repeated
// one, is bad. NULL is unset.
int Remap_parse(const char* value, RemapButton* out);

// Starts a table from the presets. resume_with_a swaps A and X: A resumes
// the selected game from its save state, or opens it when there's nothing
// to resume, and X opens it without resuming. resume_on_press makes the X
// button act on press instead of release: resume where it's stock's, open
// with resume_with_a.
void Remap_presets(RemapTable* table, int resume_with_a, int resume_on_press);

// One host call: override_button, or bind_button for a free button.
typedef struct RemapCall {
	int bind;           // 1 for bind_button, 0 for override_button
	int button;         // enum MosaicButton
	int trigger;
	const char* action; // NULL leaves the button doing nothing
} RemapCall;

// The calls that put table in place, pointing into it, in an order that
// keeps each button's own actions in order and puts every button's first
// action before any button's second, so the game list's hints land on the
// button where an action comes first (with resume_with_a, OPEN on X).
// Free buttons are bound, except "none", which overrides them with nothing.
// out holds REMAP_CALLS_MAX. Returns the count.
int Remap_calls(const RemapTable* table, RemapCall* out);

#endif
