#ifndef ACTIONS_H
#define ACTIONS_H

// Things the user can do, and the buttons that do them.
//
// An Action is what happens; a Binding says which button triggers it and
// how its hint is drawn. Screens walk their bindings in priority order and
// run the first one whose button fired and whose action is available,
// which is how stock's if/else-if input chains behaved.

typedef struct Action {
	const char* id;          // eg. "gamelist.open"; FN buttons bind as "action:<id>"
	const char* label;       // hint text, passed through TR() when drawn
	int (*available)(void);  // may it run now, and should its hint show
	void (*run)(void);
} Action;

enum ActionTrigger {
	TRIGGER_PRESS,
	TRIGGER_RELEASE,
};

typedef struct Binding {
	int button;              // BTN_*
	int trigger;             // TRIGGER_PRESS or TRIGGER_RELEASE
	const char* glyph;       // the button name GFX_blitButtonGroup draws, eg. "A"
	const Action* action;
} Binding;

void Actions_register(const Action* action);
const Action* Actions_find(const char* id);

// Runs the first binding whose button fired and whose action is available.
// Returns 1 if one ran.
int Bindings_dispatch(const Binding* bindings, int count);

// Fills pairs with glyph/label pairs for the available bindings, in the
// order given, NULL-terminated as GFX_blitButtonGroup expects. pairs needs
// room for 2 * count + 1 entries. Returns the number of hints.
int Bindings_hints(const Binding* const* bindings, int count, char** pairs);

#define FN_ACTION_ACTION_PREFIX "action:"

#endif
