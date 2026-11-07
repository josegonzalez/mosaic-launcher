// Pin to top, without the host. See pin_model.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nextui_plugin.h"
#include "pin_model.h"

#define PIN_PATH_LEN 1024

static int byPath(const void* a, const void* b) {
	return strcmp(*(const char* const*)a, *(const char* const*)b);
}

void Pin_build(PinSet* set, const char* const* paths, int count) {
	set->count = 0;
	for (int i = 0; i < count && set->count < PIN_MAX; i++) {
		if (paths[i] && paths[i][0]) set->items[set->count++] = paths[i];
	}
	qsort(set->items, (size_t)set->count, sizeof(set->items[0]), byPath);
}

int Pin_has(const PinSet* set, const char* rel) {
	if (!rel || !set->count) return 0;
	return bsearch(&rel, set->items, (size_t)set->count, sizeof(set->items[0]), byPath) != NULL;
}

// Whether <dir>/<dir name><ext> is in the set.
static int hasNamedAfter(const PinSet* set, const char* dir, size_t dir_len, const char* ext) {
	const char* name = NULL;
	for (size_t i = 0; i < dir_len; i++) {
		if (dir[i] == '/') name = dir + i;
	}
	if (!name || name + 1 == dir + dir_len) return 0;
	char path[PIN_PATH_LEN];
	int len = snprintf(path, sizeof(path), "%.*s%.*s%s", (int)dir_len, dir,
		(int)(dir + dir_len - name), name, ext);
	if (len < 0 || (size_t)len >= sizeof(path)) return 0;
	return Pin_has(set, path);
}

int Pin_isPinned(const PinSet* set, const char* rel, int type) {
	if (!rel || !set->count) return 0;
	size_t len = strlen(rel);
	switch (type) {
		case NUI_ENTRY_PAK:
			return Pin_has(set, rel);
		case NUI_ENTRY_DIR:
			return hasNamedAfter(set, rel, len, ".m3u") || hasNamedAfter(set, rel, len, ".cue");
		case NUI_ENTRY_ROM: {
			if (Pin_has(set, rel)) return 1;
			const char* slash = strrchr(rel, '/');
			return slash && hasNamedAfter(set, rel, (size_t)(slash - rel), ".m3u");
		}
	}
	return 0;
}

// True when path is dir or inside it.
static int within(const char* path, const char* dir) {
	size_t len = strlen(dir);
	return !strncmp(path, dir, len) && (path[len] == '\0' || path[len] == '/');
}

int Pin_applies(const char* dir_rel, const char* favorites_dir) {
	if (!dir_rel) return 0;
	if (!strcmp(dir_rel, "/Recently Played")) return 0;
	if (within(dir_rel, "/Collections")) return 0;
	if (favorites_dir && favorites_dir[0] && !strcmp(dir_rel, favorites_dir)) return 0;
	return 1;
}

int Pin_compare(int a_pinned, int b_pinned) {
	return (b_pinned != 0) - (a_pinned != 0);
}
