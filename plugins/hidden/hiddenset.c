// The hidden set, without SDL or the host, so it can be tested on its own.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hiddenset.h"

void HiddenSet_init(HiddenSet* set) {
	set->paths = NULL;
	set->count = 0;
	set->capacity = 0;
}

void HiddenSet_clear(HiddenSet* set) {
	for (int i = 0; i < set->count; i++) free(set->paths[i]);
	free(set->paths);
	HiddenSet_init(set);
}

// Where path is, or where it would go, by binary search. found says which.
static int position(const HiddenSet* set, const char* path, int* found) {
	int lo = 0, hi = set->count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		int cmp = strcmp(set->paths[mid], path);
		if (cmp == 0) {
			*found = 1;
			return mid;
		}
		if (cmp < 0) lo = mid + 1;
		else hi = mid;
	}
	*found = 0;
	return lo;
}

// A copy of path without a line ending or trailing slashes, or NULL for an
// empty path.
static char* normalized(const char* path) {
	size_t len = strcspn(path, "\r\n");
	while (len > 1 && path[len - 1] == '/') len--;
	if (!len) return NULL;
	char* copy = malloc(len + 1);
	if (!copy) return NULL;
	memcpy(copy, path, len);
	copy[len] = '\0';
	return copy;
}

int HiddenSet_contains(const HiddenSet* set, const char* path) {
	int found;
	position(set, path, &found);
	return found;
}

int HiddenSet_add(HiddenSet* set, const char* path) {
	if (!path) return -1;
	char* copy = normalized(path);
	if (!copy) return -1;
	int found;
	int at = position(set, copy, &found);
	if (found) {
		free(copy);
		return 0;
	}
	if (set->count == set->capacity) {
		int capacity = set->capacity ? set->capacity * 2 : 32;
		char** paths = realloc(set->paths, (size_t)capacity * sizeof(char*));
		if (!paths) {
			free(copy);
			return -1;
		}
		set->paths = paths;
		set->capacity = capacity;
	}
	memmove(&set->paths[at + 1], &set->paths[at], (size_t)(set->count - at) * sizeof(char*));
	set->paths[at] = copy;
	set->count++;
	return 1;
}

int HiddenSet_remove(HiddenSet* set, const char* path) {
	if (!path) return 0;
	char* copy = normalized(path);
	if (!copy) return 0;
	int found;
	int at = position(set, copy, &found);
	free(copy);
	if (!found) return 0;
	free(set->paths[at]);
	memmove(&set->paths[at], &set->paths[at + 1], (size_t)(set->count - at - 1) * sizeof(char*));
	set->count--;
	return 1;
}

int HiddenSet_load(HiddenSet* set, const char* file) {
	HiddenSet_clear(set);
	FILE* f = fopen(file, "r");
	if (!f) return errno == ENOENT ? 0 : -1;
	char* line = NULL;
	size_t size = 0;
	int result = 0;
	while (getline(&line, &size, f) != -1) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[strspn(line, " \t")] == '\0' || line[0] == '#') continue; // blank, or a comment
		// what's left isn't empty, so this only fails when memory runs out
		if (HiddenSet_add(set, line) < 0) {
			result = -1;
			break;
		}
	}
	free(line);
	fclose(f);
	return result;
}

int HiddenSet_save(const HiddenSet* set, const char* file) {
	size_t len = strlen(file);
	char* tmp = malloc(len + 5);
	if (!tmp) return -1;
	snprintf(tmp, len + 5, "%s.tmp", file);
	FILE* f = fopen(tmp, "w");
	if (!f) {
		free(tmp);
		return -1;
	}
	int ok = 1;
	for (int i = 0; i < set->count; i++) {
		if (fprintf(f, "%s\n", set->paths[i]) < 0) ok = 0;
	}
	if (fclose(f) != 0) ok = 0;
	if (ok && rename(tmp, file) != 0) ok = 0;
	if (!ok) remove(tmp);
	free(tmp);
	return ok ? 0 : -1;
}
