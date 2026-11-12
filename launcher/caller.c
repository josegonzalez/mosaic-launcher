#include <stddef.h>
#include "caller.h"

static __thread const char* current = NULL;

const char* Caller_current(void) {
	return current;
}

CallerScope Caller_scope(const char* plugin_id) {
	CallerScope scope = { current, 1 };
	current = plugin_id;
	return scope;
}

void Caller_end(CallerScope* scope) {
	current = scope->was;
}
