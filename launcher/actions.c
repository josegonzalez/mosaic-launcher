#include "launcher.h"

#define MAX_ACTIONS 64
static const Action* registry[MAX_ACTIONS];
static int registered = 0;

void Actions_register(const Action* action) {
	if (!action || !action->id) return;
	for (int i = 0; i < registered; i++) {
		if (!strcmp(registry[i]->id, action->id)) {
			registry[i] = action; // re-registering replaces
			return;
		}
	}
	if (registered >= MAX_ACTIONS) {
		LOG_warn("too many actions, ignoring %s\n", action->id);
		return;
	}
	registry[registered++] = action;
}

const Action* Actions_find(const char* id) {
	if (!id) return NULL;
	for (int i = 0; i < registered; i++) {
		if (!strcmp(registry[i]->id, id)) return registry[i];
	}
	return NULL;
}

static int fired(const Binding* binding) {
	if (binding->fires) return binding->fires(binding->button);
	if (binding->trigger == TRIGGER_RELEASE) return PAD_justReleased(binding->button);
	return PAD_justPressed(binding->button);
}

int Action_available(const Action* action) {
	return action && (!action->available || action->available(action->data));
}

void Action_run(const Action* action) {
	if (action && action->run) action->run(action->data);
}

int Bindings_dispatch(const Binding* bindings, int count) {
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (fired(binding) && Action_available(binding->action)) {
			Action_run(binding->action);
			return 1;
		}
	}
	return 0;
}

int Bindings_hints(const Binding* const* bindings, int count, char** pairs) {
	int hints = 0;
	for (int i = 0; i < count; i++) {
		const Binding* binding = bindings[i];
		if (!Action_available(binding->action)) continue;
		pairs[hints * 2] = (char*)binding->glyph;
		pairs[hints * 2 + 1] = TR(binding->action->label);
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
		if (!Action_available(binding->action)) continue;
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

#define SHARED_MAX 16

int Bindings_dispatchShared(const Binding* bindings, int count, void (*menu)(const Action** actions, int n)) {
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (!fired(binding) || !Action_available(binding->action)) continue;
		const Action* actions[SHARED_MAX];
		int n = Bindings_shared(bindings, count, binding->button, binding->trigger, actions, SHARED_MAX);
		if (n > SHARED_MAX) n = SHARED_MAX;
		if (n == 1 || !menu) Action_run(actions[0]);
		else menu(actions, n);
		return 1;
	}
	return 0;
}

int Bindings_sharedHint(const Binding* bindings, int count, const char** glyph, const char** label) {
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (!Action_available(binding->action)) continue;
		const Action* first;
		int n = Bindings_shared(bindings, count, binding->button, binding->trigger, &first, 1);
		*glyph = binding->glyph;
		*label = n > 1 ? "MORE" : binding->action->label;
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
