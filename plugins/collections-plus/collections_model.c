#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "collections_model.h"

#define LINE_LEN 1024

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// True when path is base + "/" + something.
static int under(const char* base, const char* path) {
	size_t len = strlen(base);
	return !strncmp(path, base, len) && path[len] == '/' && path[len + 1];
}

// Strips a line's newline, as stock does for \n, \r\n and a lone \r.
static void chomp(char* line) {
	line[strcspn(line, "\r\n")] = '\0';
}

///////////////////////////////////////
// names

void CP_parseNames(const char* text, CPNames* out) {
	memset(out, 0, sizeof(*out));
	if (!text) return;
	const char* p = text;
	while (*p) {
		const char* end = strchr(p, ',');
		if (!end) end = p + strlen(p);
		const char* a = p;
		const char* b = end;
		while (a < b && (*a == ' ' || *a == '\t')) a++;
		while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
		size_t len = (size_t)(b - a);
		if (len > 0 && len < CP_NAME_LEN && out->count < CP_NAMES_MAX) {
			char name[CP_NAME_LEN];
			memcpy(name, a, len);
			name[len] = '\0';
			int seen = 0;
			for (int i = 0; i < out->count && !seen; i++) seen = !strcasecmp(out->items[i], name);
			if (!seen) snprintf(out->items[out->count++], CP_NAME_LEN, "%s", name);
		}
		p = *end ? end + 1 : end;
	}
	if (out->count == 1 && !strcmp(out->items[0], "*")) {
		out->count = 0;
		out->all = 1;
	}
}

int CP_rank(const CPNames* names, const char* file, const char* shown) {
	for (int i = 0; i < names->count; i++) {
		if ((file && !strcasecmp(names->items[i], file)) || (shown && !strcasecmp(names->items[i], shown))) return i;
	}
	return -1;
}

int CP_compareRanks(int a, int b) {
	if (a < 0 && b < 0) return 0;
	if (a < 0) return 1;
	if (b < 0) return -1;
	return a < b ? -1 : a > b ? 1 : 0;
}

///////////////////////////////////////
// paths

int CP_isCollection(const char* collections, const char* path) {
	return collections && path && under(collections, path) && endsWith(path, ".txt");
}

int CP_topCollection(const char* collections, const char* path, char* out, size_t size) {
	if (!CP_isCollection(collections, path)) return 0;
	const char* name = path + strlen(collections) + 1;
	if (strchr(name, '/')) return 0;
	size_t len = strlen(name) - 4; // ".txt"
	if (len == 0) return 0;
	snprintf(out, size, "%.*s", (int)len, name);
	return 1;
}

// Splits <dir>/<name>.txt into its folder and its name.
static int split(const char* path, char* dir, size_t dir_size, char* name, size_t name_size) {
	if (!path || !endsWith(path, ".txt")) return 0;
	const char* slash = strrchr(path, '/');
	if (!slash) return 0;
	size_t len = strlen(slash + 1) - 4;
	if (len == 0) return 0;
	snprintf(dir, dir_size, "%.*s", (int)(slash - path), path);
	snprintf(name, name_size, "%.*s", (int)len, slash + 1);
	return 1;
}

int CP_mapPaths(const char* collections, const char* path, char out[][CP_PATH_LEN], int max) {
	char dir[CP_PATH_LEN], name[CP_PATH_LEN];
	if (max <= 0 || !CP_isCollection(collections, path) || !split(path, dir, sizeof(dir), name, sizeof(name))) return 0;
	int n = 0;
	snprintf(out[n++], CP_PATH_LEN, "%.200s/.media/%.200s.map.txt", dir, name);
	if (n < max && strcmp(dir, collections) != 0) snprintf(out[n++], CP_PATH_LEN, "%.400s/map.txt", dir);
	return n;
}

int CP_artPaths(const char* path, char out[][CP_PATH_LEN], int max) {
	char dir[CP_PATH_LEN], name[CP_PATH_LEN];
	if (max <= 0 || !split(path, dir, sizeof(dir), name, sizeof(name))) return 0;
	static const char* exts[] = { "png", "jpg" };
	int n = 0;
	for (int i = 0; i < 2 && n < max; i++) snprintf(out[n++], CP_PATH_LEN, "%.200s/.media/%.200s.%s", dir, name, exts[i]);
	return n;
}

///////////////////////////////////////
// map.txt

void CP_mapInit(CPMap* map) {
	memset(map, 0, sizeof(*map));
}

static int mapAdd(CPMap* map, const char* key, const char* value) {
	if (map->count == map->cap) {
		int cap = map->cap ? map->cap * 2 : 16;
		char** keys = realloc(map->keys, sizeof(char*) * (size_t)cap);
		if (!keys) return 0;
		map->keys = keys;
		char** values = realloc(map->values, sizeof(char*) * (size_t)cap);
		if (!values) return 0;
		map->values = values;
		map->cap = cap;
	}
	char* k = strdup(key);
	char* v = strdup(value);
	if (!k || !v) {
		free(k);
		free(v);
		return 0;
	}
	map->keys[map->count] = k;
	map->values[map->count] = v;
	map->count++;
	return 1;
}

int CP_mapLoad(CPMap* map, const char* path) {
	FILE* f = path ? fopen(path, "r") : NULL;
	if (!f) return 0;
	char line[LINE_LEN];
	while (fgets(line, sizeof(line), f)) {
		chomp(line);
		char* tab = strchr(line, '\t');
		if (!line[0] || !tab) continue;
		*tab = '\0';
		if (!CP_mapGet(map, line) && !mapAdd(map, line, tab + 1)) break;
	}
	fclose(f);
	return 1;
}

const char* CP_mapGet(const CPMap* map, const char* key) {
	if (!key) return NULL;
	for (int i = 0; i < map->count; i++) {
		if (!strcmp(map->keys[i], key)) return map->values[i];
	}
	return NULL;
}

void CP_mapFree(CPMap* map) {
	for (int i = 0; i < map->count; i++) {
		free(map->keys[i]);
		free(map->values[i]);
	}
	free(map->keys);
	free(map->values);
	CP_mapInit(map);
}

///////////////////////////////////////
// a collection's own order

void CP_orderInit(CPOrder* order) {
	memset(order, 0, sizeof(*order));
}

// qsort has no context argument; the launcher sorts on one thread.
static char** sorting;

static int byLine(const void* x, const void* y) {
	int a = *(const int*)x, b = *(const int*)y;
	int order = strcmp(sorting[a], sorting[b]);
	return order ? order : a - b;
}

int CP_orderLoad(CPOrder* order, const char* path) {
	FILE* f = path ? fopen(path, "r") : NULL;
	if (!f) return 0;
	int cap = 0;
	char line[LINE_LEN];
	while (fgets(line, sizeof(line), f)) {
		chomp(line);
		if (!line[0]) continue;
		if (order->count == cap) {
			int grown = cap ? cap * 2 : 32;
			char** lines = realloc(order->lines, sizeof(char*) * (size_t)grown);
			if (!lines) break;
			order->lines = lines;
			cap = grown;
		}
		char* copy = strdup(line);
		if (!copy) break;
		order->lines[order->count++] = copy;
	}
	fclose(f);
	if (order->count > 0) {
		order->sorted = malloc(sizeof(int) * (size_t)order->count);
		if (order->sorted) {
			for (int i = 0; i < order->count; i++) order->sorted[i] = i;
			sorting = order->lines;
			qsort(order->sorted, (size_t)order->count, sizeof(int), byLine);
		}
	}
	return 1;
}

int CP_orderIndex(const CPOrder* order, const char* rel) {
	if (!rel || !order->sorted) return -1;
	// the first of equal lines, which sort by index
	int lo = 0, hi = order->count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (strcmp(order->lines[order->sorted[mid]], rel) < 0) lo = mid + 1;
		else hi = mid;
	}
	if (lo < order->count && !strcmp(order->lines[order->sorted[lo]], rel)) return order->sorted[lo];
	return -1;
}

void CP_orderFree(CPOrder* order) {
	for (int i = 0; i < order->count; i++) free(order->lines[i]);
	free(order->lines);
	free(order->sorted);
	CP_orderInit(order);
}
