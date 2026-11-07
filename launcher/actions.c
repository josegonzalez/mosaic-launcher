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
	if (binding->trigger == TRIGGER_RELEASE) return PAD_justReleased(binding->button);
	return PAD_justPressed(binding->button);
}

static int available(const Action* action) {
	return action && (!action->available || action->available());
}

int Bindings_dispatch(const Binding* bindings, int count) {
	for (int i = 0; i < count; i++) {
		const Binding* binding = &bindings[i];
		if (fired(binding) && available(binding->action)) {
			binding->action->run();
			return 1;
		}
	}
	return 0;
}

int Bindings_hints(const Binding* const* bindings, int count, char** pairs) {
	int hints = 0;
	for (int i = 0; i < count; i++) {
		const Binding* binding = bindings[i];
		if (!available(binding->action)) continue;
		pairs[hints * 2] = (char*)binding->glyph;
		pairs[hints * 2 + 1] = TR(binding->action->label);
		hints++;
	}
	pairs[hints * 2] = NULL;
	return hints;
}
