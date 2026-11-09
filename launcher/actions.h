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
	int (*available)(void* data); // may it run now, and should its hint show; NULL means always
	void (*run)(void* data);
	void* data;              // passed to both, eg. a plugin's own state
	const char* context;     // the label's TRC context: NULL for the launcher's own, the plugin's id for a plugin's
} Action;

// True when the action exists and may run now.
int Action_available(const Action* action);
void Action_run(const Action* action);

// The label to draw, translated in the action's context.
char* Action_label(const Action* action);

enum ActionTrigger {
	TRIGGER_PRESS,
	TRIGGER_RELEASE,
	TRIGGER_REPEAT, // the press, then again while held (PAD_justRepeated)
};

typedef struct Binding {
	int button;              // BTN_*
	int trigger;             // TRIGGER_PRESS, TRIGGER_RELEASE or TRIGGER_REPEAT
	const char* glyph;       // the button name GFX_blitButtonGroup draws, eg. "A"
	const Action* action;
	int (*fires)(int button); // when set, decides whether it fired instead of trigger
	int (*allows)(int button); // when set, must also return 1 for it to fire, eg. stock's other shoulder check
} Binding;

#define ACTIONS_MAX 64

void Actions_register(const Action* action);
const Action* Actions_find(const char* id);

// The registered actions, in order, to put back later with Actions_restore.
// Fills up to max of them into out and returns how many there are.
int Actions_list(const Action** out, int max);
void Actions_restore(const Action* const* saved, int count);

// Runs the first binding whose button fired and whose action is available.
// Returns 1 if one ran.
int Bindings_dispatch(const Binding* bindings, int count);

// Fills pairs with glyph/label pairs for the available bindings, in the
// order given, NULL-terminated as GFX_blitButtonGroup expects. pairs needs
// room for 2 * count + 1 entries. Returns the number of hints.
int Bindings_hints(const Binding* const* bindings, int count, char** pairs);

// Bindings that can share a button, like plugins' on the game list. When
// one fires, the available actions bound to its button and trigger decide:
// one runs straight away, and two or more go to menu to pick from (in
// binding order, each action once). Returns 1 if one ran or the menu opened.
int Bindings_dispatchShared(const Binding* bindings, int count, void (*menu)(const Action** actions, int n));

// The available actions bound to button and trigger, in binding order and
// each once. Fills up to max of them into out and returns how many there are.
int Bindings_shared(const Binding* bindings, int count, int button, int trigger, const Action** out, int max);

// The hint for the first binding whose action is available: its glyph, and
// its action's label, or "MORE" when other available actions share the
// button and it opens a menu, translated. Returns 0 when nothing is
// available.
int Bindings_sharedHint(const Binding* bindings, int count, const char** glyph, const char** label);

// Copies the bindings whose button none of taken uses into out, which needs
// room for count. Returns how many it copied. This is how an override takes
// a button from a screen's own bindings, whatever their trigger.
int Bindings_drop(const Binding* bindings, int count, const Binding* taken, int taken_count, Binding* out);

// Gives each of overrides the allows of the binding in own with the same
// button and action, so an override that puts an action back on its own
// button keeps the screen's check for it, whatever its trigger.
void Bindings_keepChecks(Binding* overrides, int count, const Binding* own, int own_count);

// The first binding for action, or NULL.
const Binding* Bindings_find(const Binding* bindings, int count, const Action* action);

#define FN_ACTION_ACTION_PREFIX "action:"

#endif
