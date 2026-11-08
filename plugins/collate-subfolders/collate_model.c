#include "collate_model.h"

#include <stdio.h>
#include <string.h>

int CS_prefix(const char* system, char* out, size_t size) {
	const char* open = strrchr(system, '(');
	if (!open || !size) return 0;
	size_t len = (size_t)(open - system) + 1;
	if (len >= size) return 0;
	memcpy(out, system, len);
	out[len] = '\0';
	return 1;
}

int CS_collates(const char* a, const char* b) {
	char x[GV_NAME_LEN], y[GV_NAME_LEN];
	return CS_prefix(a, x, sizeof(x)) && CS_prefix(b, y, sizeof(y)) && !strcmp(x, y);
}

int CS_consoleDir(const char* roms, const char* dir_path, char* out, size_t size) {
	size_t len = strlen(roms);
	if (!dir_path || strncmp(dir_path, roms, len) != 0 || dir_path[len] != '/') return 0;
	const char* name = dir_path + len + 1;
	if (!name[0] || strchr(name, '/')) return 0;
	if (size) snprintf(out, size, "%s", name);
	return 1;
}

int CS_find(const GVGroups* groups, const char* path) {
	if (!path) return -1;
	for (int i = 0; i < groups->count; i++) {
		const GVGroup* group = &groups->items[i];
		for (int k = 0; k < group->count; k++) {
			if (!strcmp(group->sources[k].path, path)) return i;
		}
	}
	return -1;
}

int CS_shown(const GVGroups* groups, const char* path) {
	int i = CS_find(groups, path);
	return i < 0 || !strcmp(groups->items[i].sources[0].path, path);
}
