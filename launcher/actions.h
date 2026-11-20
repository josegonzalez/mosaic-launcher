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
	int (*available)(const char* path, void* data); // may it run now, and should its hint show; NULL means always
	void (*run)(const char* path, void* data);
	void* data;              // passed to both, eg. a plugin's own state
	const char* plugin_id;   // whose label it is: NULL for the launcher's own, the plugin's id for a plugin's
	int takes_path;          // it acts on an entry, and gets its full path; any other gets NULL
	int edits;               // it changes what the user keeps (MOSAIC_ACTION_EDITS), which plugins' action_gate can hold back
} Action;

// True when the action exists and may run now on path, an entry's full
// path for an action that takes one, which isn't available without one.
int Action_available(const Action* action, const char* path);

// Runs the action on path, as Action_available takes it, once the gate
// (see Actions_setGate) lets it. Returns 0, also when the gate asked for
// the PIN first, or -1 for no action, one that takes a path given none,
// which is logged, or one the gate refused.
int Action_run(const Action* action, const char* path);

// What a gate says about an action about to run, as MosaicGate's values
// in the SDK, from least to most strict.
enum ActionGate {
	ACTION_GATE_RUN = 0,    // it runs now
	ACTION_GATE_PIN = 1,    // the gate asked for the PIN, and runs it with Action_runUngated after
	ACTION_GATE_REFUSE = 2, // it doesn't run
};

// The stricter of two gates' answers.
int ActionGate_stricter(int a, int b);

// What Action_run asks before each action runs, such as the plugins'
// action_gate, with the action and its path as Action_run gives it; NULL,
// as at the start, lets every action run.
void Actions_setGate(int (*gate)(const Action* action, const char* path));

// Edits a plugin makes on a screen of its own rather than through an
// action, such as a delete, asked about as an action with
// MOSAIC_ACTION_EDITS would be (see gate_edit in mosaic_plugin.h). answer
// is what plugins' action_gate say about edit_id, as an ActionGate;
// ask_pin opens the PIN prompt and calls typed, 1 for the right PIN and 0
// for a wrong one or none, once it closes. NULL for answer, as at the
// start, lets every edit run; NULL for ask_pin refuses one that needs it.
void Actions_setEditGate(int (*answer)(const char* edit_id), void (*ask_pin)(void (*typed)(int right)));

// Asks before the edit edit_id runs. on_done gets ACTION_GATE_RUN or
// ACTION_GATE_REFUSE exactly once, as the code that asked: straight away
// when now is 1, from Edits_deliver when it's 0 (while drawing or from
// init, where it's refused without asking), or once the PIN prompt
// answers. Returns 0, or -1, with on_done refused, for a missing, empty
// or too long edit_id, a call with now 0, or a PIN asked while another
// edit waits for one; with no on_done it's -1 and nothing runs. Past
// EDITS_WAITING_MAX waiting at once, another with now 0 or a PIN is
// dropped, logged, and on_done never runs.
#define EDITS_WAITING_MAX 16
int Edit_gate(const char* edit_id, int now, void (*on_done)(int answer, void* data), void* data);

// Gives the edits Edit_gate refused with now 0 their answer, in the order
// asked, and refuses one still waiting for its PIN when prompt_open is 0,
// as its prompt went without an answer (another dialog took its place).
// Called once a frame, outside drawing.
void Edits_deliver(int prompt_open);

// Refuses every edit plugin_id still waits on, before its quit.
void Edits_drop(const char* plugin_id);

// Runs the action as Action_run does, without asking the gate, such as
// once the PIN a gate asked for is typed.
int Action_runUngated(const Action* action, const char* path);

// What a button gives the action: the selected entry's path while the
// game list shows, for an action that takes a path, else NULL.
const char* Action_buttonPath(const Action* action);

// Where Action_buttonPath finds the selected entry: selected returns its
// full path while the game list shows, and NULL elsewhere or with nothing
// selected. NULL, as at the start, gives none.
void Actions_setSelected(const char* (*selected)(void));

// The label to draw, translated as its owner's text.
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

// Bindings with their owners, as many as are added, such as plugins'
// bind_button ones.
typedef struct BindingList {
	Binding* items;
	int* owners; // one per binding, such as the index of the plugin that made it, or -1
	int count;
	int room;
} BindingList;

// Adds a copy of binding with its owner, growing the list. 0, or -1 when
// out of memory, adding nothing.
int BindingList_add(BindingList* list, const Binding* binding, int owner);
// Frees the list, leaving it empty.
void BindingList_free(BindingList* list);

// Adds action, or replaces the one with its id. The registry grows as
// actions are added, with no fixed limit. 0, or -1 for one without an id
// or when out of memory, adding nothing.
int Actions_register(const Action* action);
// How many actions are registered, and the one at index (NULL past the end).
int Actions_count(void);
const Action* Actions_at(int index);
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

// The most actions a shared button's menu lists; past it, the rest are left
// out, with a warning the first time.
#define BINDINGS_SHARED_MAX 16

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
