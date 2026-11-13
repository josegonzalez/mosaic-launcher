#include "random_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

static char* copyString(const char* s) {
	size_t len = strlen(s) + 1;
	char* copy = malloc(len);
	if (copy) memcpy(copy, s, len);
	return copy;
}

static int grow(void** items, int* capacity, int count, size_t size) {
	if (count < *capacity) return 0;
	int next = *capacity ? *capacity * 2 : 64;
	void* grown = realloc(*items, (size_t)next * size);
	if (!grown) return -1;
	*items = grown;
	*capacity = next;
	return 0;
}

///////////////////////////////////////
// the library

void RG_init(RGLibrary* lib) {
	memset(lib, 0, sizeof(*lib));
}

void RG_free(RGLibrary* lib) {
	for (int i = 0; i < lib->count; i++) free(lib->games[i].path);
	for (int i = 0; i < lib->system_count; i++) free(lib->systems[i]);
	free(lib->games);
	free(lib->systems);
	RG_init(lib);
}

void RG_systemKey(const char* folder, char* out, size_t size) {
	if (!size) return;
	const char* open = strrchr(folder, '(');
	size_t len = open ? (size_t)(open - folder) + 1 : strlen(folder);
	if (len >= size) len = size - 1;
	memcpy(out, folder, len);
	out[len] = '\0';
}

static int findSystem(const RGLibrary* lib, const char* key) {
	for (int i = 0; i < lib->system_count; i++) {
		if (!strcmp(lib->systems[i], key)) return i;
	}
	return -1;
}

int RG_add(RGLibrary* lib, const char* path, int type, const char* folder) {
	char key[RG_PATH_LEN];
	RG_systemKey(folder, key, sizeof(key));
	int system = findSystem(lib, key);
	if (system < 0) {
		if (grow((void**)&lib->systems, &lib->system_capacity, lib->system_count, sizeof(char*)) != 0) return -1;
		char* copy = copyString(key);
		if (!copy) return -1;
		system = lib->system_count;
		lib->systems[lib->system_count++] = copy;
	}

	if (grow((void**)&lib->games, &lib->capacity, lib->count, sizeof(RGGame)) != 0) return -1;
	char* copy = copyString(path);
	if (!copy) return -1;
	lib->games[lib->count++] = (RGGame){ .path = copy, .type = type, .system = system };
	return 0;
}

int RG_systemOf(const RGLibrary* lib, const char* roms, const char* path) {
	size_t len = strlen(roms);
	if (!path || strncmp(path, roms, len) != 0 || path[len] != '/' || !path[len + 1]) return -1;
	char folder[RG_PATH_LEN];
	const char* start = path + len + 1;
	const char* end = strchr(start, '/');
	size_t n = end ? (size_t)(end - start) : strlen(start);
	if (n >= sizeof(folder)) return -1;
	memcpy(folder, start, n);
	folder[n] = '\0';
	char key[RG_PATH_LEN];
	RG_systemKey(folder, key, sizeof(key));
	return findSystem(lib, key);
}

///////////////////////////////////////
// names stock hides

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

int RG_hiddenName(const char* name) {
	return name[0] == '.' || endsWith(name, ".disabled") || !strcmp(name, "map.txt");
}

int RG_mapLookup(const char* map, const char* filename, char* out, size_t size) {
	size_t len = strlen(filename);
	const char* line = map;
	while (line && *line) {
		const char* end = strchr(line, '\n');
		size_t line_len = end ? (size_t)(end - line) : strlen(line);
		if (line_len > len && !strncmp(line, filename, len) && line[len] == '\t') {
			const char* value = line + len + 1;
			size_t value_len = line_len - len - 1;
			while (value_len && value[value_len - 1] == '\r') value_len--;
			if (value_len >= size) value_len = size - 1;
			memcpy(out, value, value_len);
			out[value_len] = '\0';
			return 1;
		}
		line = end ? end + 1 : NULL;
	}
	return 0;
}

///////////////////////////////////////
// path sets

void RG_setInit(RGPathSet* set) {
	memset(set, 0, sizeof(*set));
}

void RG_setClear(RGPathSet* set) {
	for (int i = 0; i < set->count; i++) free(set->paths[i]);
	free(set->paths);
	RG_setInit(set);
}

// Where path is, or where it would go, in the sorted set.
static int lowerBound(const RGPathSet* set, const char* path) {
	int lo = 0, hi = set->count;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (strcmp(set->paths[mid], path) < 0) lo = mid + 1;
		else hi = mid;
	}
	return lo;
}

int RG_setHas(const RGPathSet* set, const char* path) {
	int at = lowerBound(set, path);
	return at < set->count && !strcmp(set->paths[at], path);
}

int RG_setAdd(RGPathSet* set, const char* path) {
	int at = lowerBound(set, path);
	if (at < set->count && !strcmp(set->paths[at], path)) return 0;
	if (grow((void**)&set->paths, &set->capacity, set->count, sizeof(char*)) != 0) return -1;
	char* copy = copyString(path);
	if (!copy) return -1;
	memmove(set->paths + at + 1, set->paths + at, (size_t)(set->count - at) * sizeof(char*));
	set->paths[at] = copy;
	set->count++;
	return 0;
}

int RG_inSet(const RGPathSet* set, const char* path, int type) {
	if (!set || !path) return 0;
	if (RG_setHas(set, path)) return 1;
	if (type != MOSAIC_ENTRY_DIR) return 0;
	const char* name = strrchr(path, '/');
	if (!name) return 0;
	char file[RG_PATH_LEN * 2];
	snprintf(file, sizeof(file), "%s%s.m3u", path, name);
	if (RG_setHas(set, file)) return 1;
	snprintf(file, sizeof(file), "%s%s.cue", path, name);
	return RG_setHas(set, file);
}

int RG_pickable(const char* name) {
	return name && name[0] && strcmp(name, "hidden") != 0;
}

void RG_setLabel(const char* name, const char* fav, const char* tag, const char* smart, char* out, size_t size) {
	if (!strcmp(name, "favorites")) snprintf(out, size, "%s", fav);
	else if (!strncmp(name, "tags:", 5)) snprintf(out, size, tag, name + 5);
	else if (!strncmp(name, "smart:", 6)) snprintf(out, size, smart, name + 6);
	else snprintf(out, size, "%s", name);
}

///////////////////////////////////////
// picking

int RG_isPlayed(const char* path, const char* const* recents, int count) {
	size_t len = strlen(path);
	for (int i = 0; i < count; i++) {
		const char* recent = recents[i];
		if (!recent) continue;
		if (!strncmp(recent, path, len) && (recent[len] == '\0' || recent[len] == '/')) return 1;
	}
	return 0;
}

unsigned long RG_weights(const RGLibrary* lib, int system, const RGPathSet* only, RGPlayedFn played, void* data,
		unsigned unplayed_weight, int unplayed_only, unsigned* weights) {
	unsigned long total = 0;
	if (unplayed_weight < 1) unplayed_weight = 1;
	int ask = played && (unplayed_weight > 1 || unplayed_only);
	for (int i = 0; i < lib->count; i++) {
		const RGGame* game = &lib->games[i];
		unsigned weight = 0;
		if ((system < 0 || game->system == system) && (!only || RG_inSet(only, game->path, game->type))) {
			if (!ask || !played(game, data)) weight = unplayed_weight;
			else weight = unplayed_only ? 0 : 1;
		}
		weights[i] = weight;
		total += weight;
	}
	return total;
}

int RG_pick(const unsigned* weights, int count, unsigned long total, RGRandom random, void* data) {
	if (!total) return -1;
	unsigned long r = random(total, data);
	if (r >= total) r = total - 1;
	for (int i = 0; i < count; i++) {
		if (r < weights[i]) return i;
		r -= weights[i];
	}
	return -1;
}

unsigned long long RG_xorshift(unsigned long long* state) {
	unsigned long long x = *state ? *state : 0x9E3779B97F4A7C15ULL;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*state = x;
	return x;
}
