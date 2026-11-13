#include <ctype.h>
#include <string.h>
#include "pluginorder.h"
#include "tr.h"

// The exclusive slots plugins.h names, in the order docs/plugin-api.md lists them,
// with what the page calls them.
static const char* const slots[PLUGIN_GROUP_COUNT] = {
	"root", "layout", "transition", "launch", "background", "home", "switcher", "strings", "theme",
};
static const char* const titles[PLUGIN_GROUP_COUNT] = {
	TR_MARK("Main menu"), TR_MARK("Layout"), TR_MARK("Transitions"), TR_MARK("Launch animation"),
	TR_MARK("Background"), TR_MARK("Home screen"), TR_MARK("Game switcher"), TR_MARK("Language"),
	TR_MARK("Theme"),
};

int PluginOrder_group(const PluginManifest* m) {
	for (int i = 0; i < PLUGIN_GROUP_COUNT; i++) {
		if (csvHas(m->slots, slots[i])) return i;
	}
	return PLUGIN_GROUP_OTHER;
}

const char* PluginOrder_title(int group) {
	if (group >= 0 && group < PLUGIN_GROUP_COUNT) return titles[group];
	return TR_MARK("Other plugins");
}

// Latin-1's letters (U+00C0 to U+00FF, UTF-8 C3 80 to C3 BF) as the ASCII
// letter they're written on; 0 keeps the character as it is.
static const char latin1[64] = {
	'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
	'd', 'n', 'o', 'o', 'o', 'o', 'o', 0, 'o', 'u', 'u', 'u', 'u', 'y', 0, 's',
	'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
	'd', 'n', 'o', 'o', 'o', 'o', 'o', 0, 'o', 'u', 'u', 'u', 'u', 'y', 0, 'y',
};

// The next character to compare, folded, moving *p past it.
static int nextKey(const unsigned char** p) {
	const unsigned char* s = *p;
	if (s[0] == 0xC3 && s[1] >= 0x80 && s[1] <= 0xBF && latin1[s[1] - 0x80]) {
		*p += 2;
		return latin1[s[1] - 0x80];
	}
	*p += 1;
	return tolower(s[0]);
}

int PluginOrder_compareNames(const char* a, const char* b) {
	const unsigned char* x = (const unsigned char*)a;
	const unsigned char* y = (const unsigned char*)b;
	while (*x && *y) {
		int cx = nextKey(&x);
		int cy = nextKey(&y);
		if (cx != cy) return cx - cy;
	}
	return (int)*x - (int)*y;
}

static int before(const PluginManifest* a, const PluginManifest* b) {
	int by_name = PluginOrder_compareNames(a->name, b->name);
	if (by_name) return by_name < 0;
	return strcmp(a->id, b->id) < 0;
}

// Adds group's plugins A-Z, after its header when header is set.
static int addGroup(const PluginManifest* manifests, int count, int group, int header, PluginPageRow* out, int n, int max) {
	int members[PLUGIN_MAX];
	int found = 0;
	for (int i = 0; i < count && found < PLUGIN_MAX; i++) {
		if (PluginOrder_group(&manifests[i]) != group) continue;
		int at = found++;
		while (at > 0 && before(&manifests[i], &manifests[members[at - 1]])) {
			members[at] = members[at - 1];
			at--;
		}
		members[at] = i;
	}
	if (!found) return n;
	if (header && n < max) out[n++] = (PluginPageRow){ .group = group, .plugin = -1 };
	for (int i = 0; i < found && n < max; i++) out[n++] = (PluginPageRow){ .group = group, .plugin = members[i] };
	return n;
}

int PluginOrder_rows(const PluginManifest* manifests, int count, PluginPageRow* out, int max) {
	int n = 0;
	for (int g = 0; g < PLUGIN_GROUP_COUNT; g++) n = addGroup(manifests, count, g, 1, out, n, max);
	return addGroup(manifests, count, PLUGIN_GROUP_OTHER, n > 0, out, n, max);
}
