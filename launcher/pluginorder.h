#ifndef PLUGINORDER_H
#define PLUGINORDER_H

// The Launcher pak's Plugins page order. Plugins that claim an exclusive
// slot are grouped under it, in the order docs/plugin-api.md lists the slots, and
// the rest come last; each group is A-Z by name. Only the page's order:
// the hook order plugins.txt sets is left alone. Pure, so it can be tested
// on its own.

#include "pluginset.h"

#define PLUGIN_GROUP_COUNT 9  // the exclusive slots, each a group
#define PLUGIN_GROUP_OTHER -1 // plugins that claim none of them

typedef struct PluginPageRow {
	int group;  // a slot's index, or PLUGIN_GROUP_OTHER
	int plugin; // manifest index, or -1 for the group's header
} PluginPageRow;

// The group a plugin is listed under: the first slot it claims, in the
// slots' order, or PLUGIN_GROUP_OTHER.
int PluginOrder_group(const PluginManifest* m);

// A group's header, in English, for TR() where it's drawn.
const char* PluginOrder_title(int group);

// Compares names A-Z, ignoring case and the accents on Latin letters.
int PluginOrder_compareNames(const char* a, const char* b);

// Fills out with the page's rows: each group with plugins, its header
// first, then the others, with their own header only when a group came
// before them. Returns how many, at most max.
int PluginOrder_rows(const PluginManifest* manifests, int count, PluginPageRow* out, int max);

#endif
