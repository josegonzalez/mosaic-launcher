#include <stdio.h>
#include <string.h>
#include "focus_model.h"
#include "mosaic_plugin.h"

int Focus_add(FocusList* list, const char* path) {
	if (!path || !path[0] || strlen(path) >= FOCUS_PATH_LEN || list->count >= FOCUS_MAX) return 0;
	for (int i = 0; i < list->count; i++) {
		if (!strcmp(list->items[i], path)) return 0;
	}
	snprintf(list->items[list->count++], FOCUS_PATH_LEN, "%s", path);
	return 1;
}

int Focus_rows(const FocusList* list, const char* sdcard, int (*typeOf)(const char* path), FocusRow* out) {
	int n = 0;
	for (int i = 0; i < list->count; i++) {
		int type = typeOf(list->items[i]);
		if (type < 0) continue; // kept in the set in case the card comes back
		FocusRow* row = &out[n++];
		snprintf(row->path, sizeof(row->path), "%s", list->items[i]);
		row->type = type;
		row->name = NULL;
	}
	if (n == 0) return 0;

	FocusRow* all = &out[n++];
	snprintf(all->path, sizeof(all->path), "%s/Roms", sdcard);
	all->type = MOSAIC_ENTRY_DIR;
	all->name = FOCUS_ALL_NAME;
	return n;
}

int Focus_focusable(const char* name) {
	return name && name[0] && strcmp(name, "hidden") != 0;
}

void Focus_setLabel(const char* name, const char* fav, const char* tag, const char* smart, char* out, size_t size) {
	if (!strcmp(name, "favorites")) snprintf(out, size, "%s", fav);
	else if (!strncmp(name, "tags:", 5)) snprintf(out, size, tag, name + 5);
	else if (!strncmp(name, "smart:", 6)) snprintf(out, size, smart, name + 6);
	else snprintf(out, size, "%s", name);
}
