#include <stdlib.h>
#include <string.h>
#include "jump_model.h"

// The folder name below roms that path starts with, as a pointer into path
// and its length, or NULL when path isn't inside roms.
static const char* folderOf(const char* roms, const char* path, size_t* len) {
	size_t n = strlen(roms);
	if (strncmp(path, roms, n) != 0 || path[n] != '/') return NULL;
	const char* folder = path + n + 1;
	const char* slash = strchr(folder, '/');
	*len = slash ? (size_t)(slash - folder) : strlen(folder);
	return *len ? folder : NULL;
}

int PJ_systemKey(const char* roms, const char* path, char* out, size_t size) {
	size_t len;
	const char* folder = folderOf(roms, path, &len);
	if (!folder || !size) return 0;
	// the last "(" in the folder name, not further down the path
	size_t keep = len;
	for (size_t i = len; i > 0; i--) {
		if (folder[i - 1] == '(') {
			keep = i;
			break;
		}
	}
	if (keep >= size) keep = size - 1;
	memcpy(out, folder, keep);
	out[keep] = '\0';
	return 1;
}

int PJ_isSystemDir(const char* roms, const char* path) {
	size_t len;
	const char* folder = folderOf(roms, path, &len);
	return folder && folder[len] == '\0';
}

void PJ_systemsInit(PJSystems* s) {
	memset(s, 0, sizeof(*s));
}

void PJ_systemsClear(PJSystems* s) {
	for (int i = 0; i < s->count; i++) {
		free(s->keys[i]);
		free(s->paths[i]);
	}
	free(s->keys);
	free(s->paths);
	PJ_systemsInit(s);
}

int PJ_systemsFind(const PJSystems* s, const char* key) {
	for (int i = 0; i < s->count; i++) {
		if (!strcmp(s->keys[i], key)) return i;
	}
	return -1;
}

int PJ_systemsAdd(PJSystems* s, const char* key, const char* path) {
	int found = PJ_systemsFind(s, key);
	if (found >= 0) return found;
	if (s->count == s->capacity) {
		int capacity = s->capacity ? s->capacity * 2 : 16;
		char** keys = realloc(s->keys, (size_t)capacity * sizeof(char*));
		if (!keys) return -1;
		s->keys = keys;
		char** paths = realloc(s->paths, (size_t)capacity * sizeof(char*));
		if (!paths) return -1;
		s->paths = paths;
		s->capacity = capacity;
	}
	char* k = strdup(key);
	char* p = strdup(path ? path : "");
	if (!k || !p) {
		free(k);
		free(p);
		return -1;
	}
	s->keys[s->count] = k;
	s->paths[s->count] = p;
	return s->count++;
}

// Each group's first row, in the order groups first appear. Returns how
// many, writing them to first (count long), or -1 when memory ran out.
static int firstRows(const int* groups, int count, int* first) {
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (groups[i] < 0) continue;
		int seen = 0;
		for (int g = 0; g < n && !seen; g++) seen = groups[first[g]] == groups[i];
		if (!seen) first[n++] = i;
	}
	return n;
}

int PJ_groupCount(const int* groups, int count) {
	if (count <= 0) return 0;
	int* first = malloc((size_t)count * sizeof(int));
	if (!first) return 0;
	int n = firstRows(groups, count, first);
	free(first);
	return n;
}

int PJ_jump(const int* groups, int count, int selected, int dir, int wrap) {
	if (count <= 0 || selected < 0 || selected >= count || !dir) return -1;
	int* first = malloc((size_t)count * sizeof(int));
	if (!first) return -1;
	int n = firstRows(groups, count, first);

	// the selected row's group, if it's in one
	int current = -1;
	for (int g = 0; g < n && groups[selected] >= 0; g++) {
		if (groups[first[g]] == groups[selected]) current = g;
	}

	int target = -1;
	if (dir > 0) {
		if (current >= 0) target = current + 1 < n ? current + 1 : -1;
		else for (int g = 0; g < n && target < 0; g++) if (first[g] > selected) target = g;
		if (target < 0 && wrap && n) target = 0;
	}
	else {
		if (current >= 0) target = current - 1;
		else for (int g = n - 1; g >= 0 && target < 0; g--) if (first[g] < selected) target = g;
		if (target < 0 && wrap && n) target = n - 1;
	}
	int row = target >= 0 && target != current ? first[target] : -1;
	free(first);
	return row;
}
