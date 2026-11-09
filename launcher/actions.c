#include "launcher.h"

static const Action* registry[ACTIONS_MAX];
static int registered = 0;

char* Action_label(const Action* action) {
	return action ? TRC(action->context, action->label) : NULL;
}

void Actions_register(const Action* action) {
	if (!action || !action->id) return;
	for (int i = 0; i < registered; i++) {
		if (!strcmp(registry[i]->id, action->id)) {
			registry[i] = action; // re-registering replaces
			return;
		}
	}
	if (registered >= ACTIONS_MAX) {
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

int Actions_list(const Action** out, int max) {
	for (int i = 0; i < registered && i < max; i++) out[i] = registry[i];
	return registered;
}

void Actions_restore(const Action* const* saved, int count) {
	if (count > ACTIONS_MAX) count = ACTIONS_MAX;
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
