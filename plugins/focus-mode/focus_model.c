#include <stdio.h>
#include <string.h>
#include "focus_model.h"
#include "nextui_plugin.h"

const char* Focus_collectionName(const char* setting) {
	if (!setting || !setting[0] || strchr(setting, '/')) return "Favorites";
	return setting;
}

void Focus_listPath(const char* sdcard, const char* collection, char* out, size_t size) {
	snprintf(out, size, "%s/Collections/%s.txt", sdcard, Focus_collectionName(collection));
}

static int has(const FocusList* list, const char* rel) {
	for (int i = 0; i < list->count; i++) {
		if (!strcmp(list->items[i], rel)) return 1;
	}
	return 0;
}

int Focus_load(FocusList* list, const char* path) {
	list->count = 0;
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	char line[FOCUS_PATH_LEN];
	while (list->count < FOCUS_MAX && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0] || has(list, line)) continue;
		snprintf(list->items[list->count++], FOCUS_PATH_LEN, "%s", line);
	}
	fclose(f);
	return list->count;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

int Focus_rows(const FocusList* list, const char* sdcard, int (*exists)(const char* path), FocusRow* out) {
	int n = 0;
	for (int i = 0; i < list->count; i++) {
		FocusRow* row = &out[n];
		snprintf(row->path, sizeof(row->path), "%s%s", sdcard, list->items[i]);
		if (!exists(row->path)) continue; // kept in the file in case the card comes back
		// as the launcher types a collection's entries
		row->type = endsWith(row->path, ".pak") ? NUI_ENTRY_PAK : NUI_ENTRY_ROM;
		row->name = NULL;
		n++;
	}
	if (n == 0) return 0;

	FocusRow* all = &out[n++];
	snprintf(all->path, sizeof(all->path), "%s/Roms", sdcard);
	all->type = NUI_ENTRY_DIR;
	all->name = FOCUS_ALL_NAME;
	return n;
}
