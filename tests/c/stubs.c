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

// quickitems.c reaches into platform code (Wi-Fi, Bluetooth, power). A test
// sets stub_quick_items to stand in for its bottom row.
Array* (*stub_quick_items)(void) = NULL;
Array* QuickItem_entries(void) {
	return stub_quick_items ? stub_quick_items() : Array_new();
}

// actions.c reads the pad; nothing is pressed in these tests.
int PAD_justPressed(int btn) { (void)btn; return 0; }
int PAD_justReleased(int btn) { (void)btn; return 0; }
int PAD_justRepeated(int btn) { (void)btn; return 0; }

// plugins.c loads shared objects; listing tests run with no plugins.
// A test sets stub_root_entries to stand in for plugins' root_entries.
void (*stub_root_entries)(Array* root) = NULL;
void Plugins_rootEntries(Array* root) {
	if (stub_root_entries) stub_root_entries(root);
}
// A test sets these to stand in for a root slot owner's list and its
// root_with_entries; the list is used once.
Array* stub_root_replace = NULL;
int stub_root_with_entries = 0;
Array* Plugins_rootReplace(int* with_entries) {
	Array* root = stub_root_replace;
	stub_root_replace = NULL;
	*with_entries = root ? stub_root_with_entries : 0;
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
// A test sets this to stand in for plugins' hide_name.
ListHideName stub_hide_name = NULL;
unsigned char* Plugins_hideNames(Array* entries, const char* dir_path) {
	return stub_hide_name ? ListHide_chain(entries, &stub_hide_name, 1, dir_path) : NULL;
}
// A test sets this to stand in for plugins' repair_path.
int (*stub_repair_path)(const char* missing, char* out, size_t size) = NULL;
int Plugins_repairPath(const char* missing, char* out, size_t size) {
	if (!stub_repair_path) return 0;
	out[0] = '\0';
	return stub_repair_path(missing, out, size) == 1 && out[0];
}
