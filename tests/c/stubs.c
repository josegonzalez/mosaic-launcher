// Stand-ins for the parts of NextUI's api.c and the launcher that the
// tested modules call but the tests don't exercise.

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "containers.h"
#include "listsort.h"
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

// plugins.c loads shared objects; listing tests run with no plugins.
void Plugins_rootEntries(Array* root) { (void)root; }
// A test sets this to stand in for a root slot owner's list; it's used once.
Array* stub_root_replace = NULL;
Array* Plugins_rootReplace(void) {
	Array* root = stub_root_replace;
	stub_root_replace = NULL;
	return root;
}
Array* Plugins_virtualDir(const char* path) { (void)path; return NULL; }
Array* Plugins_filter(Array* entries, const char* dir_path) { (void)dir_path; return entries; }
int Plugins_displayNames(Array* entries) { (void)entries; return 0; }
// Tests set these to stand in for plugins' sort_compare and list_ready.
ListSortCompare stub_sort_compare = NULL;
int (*stub_list_ready)(Array* entries, const char* dir_path) = NULL;
void Plugins_sort(Array* entries, const char* dir_path) {
	if (stub_sort_compare) ListSort_chain(entries, &stub_sort_compare, 1, dir_path);
}
int Plugins_listReady(Array* entries, const char* dir_path) {
	return stub_list_ready ? stub_list_ready(entries, dir_path) : 0;
}
