#include "launcher.h"
#include "caller.h"

// every registered action, growing as they're added
static const Action** registry = NULL;
static int registered = 0;
static int room = 0;

char* Action_label(const Action* action) {
	return action ? Tr_plugin(action->plugin_id, NULL, action->label) : NULL;
}

int Actions_register(const Action* action) {
	if (!action || !action->id) return -1;
	for (int i = 0; i < registered; i++) {
		if (!strcmp(registry[i]->id, action->id)) {
			registry[i] = action; // re-registering replaces
			return 0;
		}
	}
	if (registered >= room) {
		int grown = room ? room * 2 : 64;
		const Action** items = realloc(registry, sizeof(*registry) * (size_t)grown);
		if (!items) {
			LOG_warn("out of memory, ignoring action %s\n", action->id);
			return -1;
		}
		registry = items;
		room = grown;
	}
	registry[registered++] = action;
	return 0;
}

int BindingList_add(BindingList* list, const Binding* binding, int owner) {
	if (list->count >= list->room) {
		int room = list->room ? list->room * 2 : 16;
		Binding* items = realloc(list->items, sizeof(Binding) * (size_t)room);
		if (!items) return -1;
		list->items = items;
		int* owners = realloc(list->owners, sizeof(int) * (size_t)room);
		if (!owners) return -1;
		list->owners = owners;
		list->room = room;
	}
	list->items[list->count] = *binding;
	list->owners[list->count] = owner;
	list->count++;
	return 0;
}

void BindingList_free(BindingList* list) {
	free(list->items);
	free(list->owners);
	memset(list, 0, sizeof(*list));
}

int Actions_count(void) {
	return registered;
}

const Action* Actions_at(int index) {
	return index >= 0 && index < registered ? registry[index] : NULL;
}

const Action* Actions_find(const char* id) {
	if (!id) return NULL;
	for (int i = 0; i < registered; i++) {
		if (!strcmp(registry[i]->id, id)) return registry[i];
	}
	return NULL;
}

int Actions_list(const Action** out, int max) {
	for (int i = 0; i < registered && i < max; i++) out[i] = registry[i];
	return registered;
}

void Actions_restore(const Action* const* saved, int count) {
	// the registry only grows, so what was saved from it fits
	if (count > room) count = room;
	if (count < 0) count = 0;
	for (int i = 0; i < count; i++) registry[i] = saved[i];
	registered = count;
}

static int triggered(const Binding* binding) {
	if (binding->fires) return binding->fires(binding->button);
	if (binding->trigger == TRIGGER_RELEASE) return PAD_justReleased(binding->button);
	if (binding->trigger == TRIGGER_REPEAT) return PAD_justRepeated(binding->button);
	return PAD_justPressed(binding->button);
}

static int fired(const Binding* binding) {
	return triggered(binding) && (!binding->allows || binding->allows(binding->button));
}

static const char* (*selectedPath)(void) = NULL;

void Actions_setSelected(const char* (*selected)(void)) {
	selectedPath = selected;
}

const char* Action_buttonPath(const Action* action) {
	if (!action || !action->takes_path || !selectedPath) return NULL;
	return selectedPath();
}

// The path the action gets: an entry's for one that takes a path, which
// has none without one, and NULL for any other.
static const char* pathFor(const Action* action, const char* path) {
	if (!action->takes_path) return NULL;
	return path && path[0] ? path : NULL;
}

// A plugin's action runs as that plugin (see caller.h).
int Action_available(const Action* action, const char* path) {
	if (!action) return 0;
	path = pathFor(action, path);
	if (action->takes_path && !path) return 0;
	int available = 1;
	if (action->available) AS_CALLER(action->plugin_id) available = action->available(path, action->data);
	return available;
}

#define ACTION_PATH_MAX 1024

static int (*runGate)(const Action* action, const char* path) = NULL;

void Actions_setGate(int (*gate)(const Action* action, const char* path)) {
	runGate = gate;
}

int ActionGate_stricter(int a, int b) {
	return a > b ? a : b;
}

static int run(const Action* action, const char* path, int gated) {
	if (!action) return -1;
	path = pathFor(action, path);
	if (action->takes_path && !path) {
		LOG_warn("can't run %s: it acts on an entry, and there's none\n", action->id);
		return -1;
	}
	// its own copy, as the entry it came from can go while it runs
	char copy[ACTION_PATH_MAX];
	if (path) {
		if (strlen(path) >= sizeof(copy)) {
			LOG_warn("can't run %s: the path is too long\n", action->id);
			return -1;
		}
		strcpy(copy, path);
		path = copy;
	}
	if (gated && runGate) {
		int answer = runGate(action, path);
		if (answer == ACTION_GATE_REFUSE) return -1;
		if (answer != ACTION_GATE_RUN) return 0;
	}
	if (action->run) AS_CALLER(action->plugin_id) action->run(path, action->data);
	return 0;
}

int Action_run(const Action* action, const char* path) {
	return run(action, path, 1);
}

int Action_runUngated(const Action* action, const char* path) {
	return run(action, path, 0);
}

// A binding's action, on what a button gives it.
static int buttonAvailable(const Action* action) {
	return Action_available(action, Action_buttonPath(action));
}
static void buttonRun(const Action* action) {
	Action_run(action, Action_buttonPath(action));
}

int Bindings_dispatch(const Binding* bindings, int count) {
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (fired(binding) && buttonAvailable(binding->action)) {
			buttonRun(binding->action);
			return 1;
		}
	}
	return 0;
}

int Bindings_hints(const Binding* const* bindings, int count, char** pairs) {
	int hints = 0;
	for (int i = 0; i < count; i++) {
		const Binding* binding = bindings[i];
		if (!buttonAvailable(binding->action)) continue;
		pairs[hints * 2] = (char*)binding->glyph;
		pairs[hints * 2 + 1] = Action_label(binding->action);
		hints++;
	}
	pairs[hints * 2] = NULL;
	return hints;
}

int Bindings_shared(const Binding* bindings, int count, int button, int trigger, const Action** out, int max) {
	int n = 0;
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (binding->button != button || binding->trigger != trigger) continue;
		if (!buttonAvailable(binding->action)) continue;
		int seen = 0;
		for (int j = 0; j < i; j++) {
			if (bindings[j].action == binding->action && bindings[j].button == button && bindings[j].trigger == trigger) seen = 1;
		}
		if (seen) continue;
		if (n < max) out[n] = binding->action;
		n++;
	}
	return n;
}

// The actions past BINDINGS_SHARED_MAX on a shared button, which its menu
// leaves out; said once, the first time it happens.
static void sharedDropped(const Binding* bindings, int count, const Binding* pressed, int n) {
	static int warned = 0;
	if (warned) return;
	warned = 1;
	const Action** all = malloc(sizeof(const Action*) * (size_t)n);
	if (!all) return;
	Bindings_shared(bindings, count, pressed->button, pressed->trigger, all, n);
	for (int i = BINDINGS_SHARED_MAX; i < n; i++) {
		LOG_warn("plugins: %s's action %s is left off %s's menu, which lists %i actions at most\n",
			all[i]->plugin_id ? all[i]->plugin_id : "launcher", all[i]->id, pressed->glyph, BINDINGS_SHARED_MAX);
	}
	free(all);
}

int Bindings_dispatchShared(const Binding* bindings, int count, void (*menu)(const Action** actions, int n)) {
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (!fired(binding) || !buttonAvailable(binding->action)) continue;
		const Action* actions[BINDINGS_SHARED_MAX];
		int n = Bindings_shared(bindings, count, binding->button, binding->trigger, actions, BINDINGS_SHARED_MAX);
		if (n > BINDINGS_SHARED_MAX) {
			sharedDropped(bindings, count, binding, n);
			n = BINDINGS_SHARED_MAX;
		}
		if (n == 1 || !menu) buttonRun(actions[0]);
		else menu(actions, n);
		return 1;
	}
	return 0;
}

int Bindings_sharedHint(const Binding* bindings, int count, const char** glyph, const char** label) {
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (!buttonAvailable(binding->action)) continue;
		const Action* first;
		int n = Bindings_shared(bindings, count, binding->button, binding->trigger, &first, 1);
		*glyph = binding->glyph;
		*label = n > 1 ? TR("MORE") : Action_label(binding->action);
		return 1;
	}
	return 0;
}

int Bindings_drop(const Binding* bindings, int count, const Binding* taken, int taken_count, Binding* out) {
	int kept = 0;
	for (int i = 0; i < count; i++) {
		int dropped = 0;
		for (int j = 0; j < taken_count && !dropped; j++) {
			if (taken[j].button == bindings[i].button) dropped = 1;
		}
		if (!dropped) out[kept++] = bindings[i];
	}
	return kept;
}

const Binding* Bindings_find(const Binding* bindings, int count, const Action* action) {
	if (!action) return NULL;
	for (int i = 0; i < count; i++) {
		if (bindings[i].action == action) return &bindings[i];
	}
	return NULL;
}

void Bindings_keepChecks(Binding* overrides, int count, const Binding* own, int own_count) {
	for (int i = 0; i < count; i++) {
		for (int j = 0; j < own_count; j++) {
			if (own[j].button != overrides[i].button || own[j].action != overrides[i].action) continue;
			overrides[i].allows = own[j].allows;
			break;
		}
	}
}
