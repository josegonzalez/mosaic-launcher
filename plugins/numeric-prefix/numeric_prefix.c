// Numeric prefix ordering: names that start with a number, like
// "1) Super Mario Land", "02. Tetris" or "[3] Dr. Mario", sort by that
// number, and the number is left off the name shown. It works on file and
// folder names and on map.txt names alike, since it reads the name the
// launcher would show. See numprefix.h for the forms that count.
//
// In each list the numbered entries come first, by number (2 before 10),
// then the rest in the launcher's order. On the root only the consoles
// move: Recently Played stays first, and Collections, plugin folders and
// Tools stay after the consoles. Recently Played and collections keep
// their own order, though their names still lose the number.
//
// A root another plugin replaced, such as all games' or focus mode's,
// keeps its owner's order, whatever the load order. It's told apart by an
// entry stock's root never lists (see NumPrefix_replacedRoot): filter sees
// every entry before the list is sorted, so it notes one there. Names on
// it still lose the number.
//
// The "hide" setting keeps the number in the name when off.

#include <stdio.h>
#include <string.h>
#include "nextui_plugin.h"
#include "numprefix.h"

#define ID "numeric-prefix"

static const NUIHost* host;
static int hide_number = 1;
static NumCache numbers;
static int root_replaced = 0; // the root being listed isn't stock's
static int root_listed = 1;   // the next root filter starts a new listing

static int isRoot(const char* dir_path) {
	return dir_path && !strcmp(dir_path, host->sdcard_path);
}

// Keeps every entry; it only looks at the root's.
static int filter(const NUIEntry* entry, const char* dir_path) {
	if (!isRoot(dir_path)) return 1;
	if (root_listed) {
		root_replaced = 0;
		root_listed = 0;
	}
	if (NumPrefix_replacedRoot(host->sdcard_path, host->entry_path(entry))) root_replaced = 1;
	return 1;
}

static int displayName(const NUIEntry* entry, char* out, size_t size) {
	root_listed = 1; // filtering is over, for the root too
	const char* path = host->entry_path(entry);
	const char* name = host->entry_name(entry);
	long number;
	const char* rest;
	if (!path || !NumPrefix_parse(name, &number, &rest)) {
		NumCache_remove(&numbers, path);
		return 0;
	}
	if (NumCache_set(&numbers, path, number) != 0) return 0; // leave the number showing
	if (!hide_number) return 0;
	snprintf(out, size, "%s", rest);
	return 1;
}

static NumKey keyFor(const NUIEntry* entry, int on_root) {
	const char* path = host->entry_path(entry);
	NumKey key = { NUMPREFIX_ORDERED, 0, 0 };
	if (on_root) key.group = NumPrefix_rootGroup(host->sdcard_path, path);
	key.numbered = NumCache_get(&numbers, path, &key.number)
		|| NumPrefix_parse(host->entry_name(entry), &key.number, NULL);
	return key;
}

static int sortCompare(const NUIEntry* a, const NUIEntry* b, const char* dir_path) {
	if (NumPrefix_keepsOrder(host->sdcard_path, dir_path)) return 0;
	int on_root = isRoot(dir_path);
	if (on_root && root_replaced) return 0; // the owner's order
	NumKey ka = keyFor(a, on_root);
	NumKey kb = keyFor(b, on_root);
	return NumPrefix_compare(&ka, &kb);
}

// Ends a listing even when it had nothing to name.
static void listReady(const NUIList* list, const char* dir_path) {
	(void)list;
	(void)dir_path;
	root_listed = 1;
}

static void quit(void) {
	NumCache_clear(&numbers);
}

static int init(const NUIHost* h) {
	host = h;
	NumCache_init(&numbers);
	root_replaced = 0;
	root_listed = 1;
	hide_number = strcmp(host->cfg_get(ID, "hide", "on"), "off") != 0;
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.filter = filter,
	.display_name = displayName,
	.sort_compare = sortCompare,
	.list_ready = listReady,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
