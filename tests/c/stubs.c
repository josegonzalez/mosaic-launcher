// Stand-ins for the parts of NextUI's api.c and the launcher that the
// tested modules call but the tests don't exercise.

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "containers.h"
#include "stubs.h"

// Root list settings, defaulting to stock's CFG_DEFAULT_* values. Tests
// flip these to exercise each branch of getRoot().
bool stub_show_recents = true;
bool stub_show_collections = true;
bool stub_show_tools = true;

bool CFG_getShowRecents(void) { return stub_show_recents; }
bool CFG_getShowCollections(void) { return stub_show_collections; }
bool CFG_getShowTools(void) { return stub_show_tools; }

void LOG_note(int level, const char* fmt, ...) {
	(void)level;
	(void)fmt;
}

// quickitems.c reaches into platform code (Wi-Fi, Bluetooth, power).
Array* QuickItem_entries(void) {
	return Array_new();
}
