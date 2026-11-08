#include "badges_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////
// played games

void RB_setInit(RBSet* set) {
	set->keys = NULL;
	set->count = 0;
	set->cap = 0;
}

void RB_setFree(RBSet* set) {
	for (int i = 0; i < set->count; i++) free(set->keys[i]);
	free(set->keys);
	RB_setInit(set);
}

static int validPart(const char* s) {
	return s && *s && !strchr(s, '/');
}

int RB_setAdd(RBSet* set, const char* tag, const char* file) {
	if (!validPart(tag) || !validPart(file) || set->count >= RB_MAX_KEYS) return -1;
	if (set->count == set->cap) {
		int cap = set->cap ? set->cap * 2 : 64;
		char** keys = realloc(set->keys, (size_t)cap * sizeof(char*));
		if (!keys) return -1;
		set->keys = keys;
		set->cap = cap;
	}
	size_t n = strlen(tag) + 1 + strlen(file) + 1;
	char* key = malloc(n);
	if (!key) return -1;
	snprintf(key, n, "%s/%s", tag, file);
	set->keys[set->count++] = key;
	return 0;
}

static int compareKeys(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

void RB_setFinish(RBSet* set) {
	if (set->count < 2) return;
	qsort(set->keys, (size_t)set->count, sizeof(char*), compareKeys);
	int kept = 1;
	for (int i = 1; i < set->count; i++) {
		if (!strcmp(set->keys[i], set->keys[kept - 1])) free(set->keys[i]);
		else set->keys[kept++] = set->keys[i];
	}
	set->count = kept;
}

int RB_setHas(const RBSet* set, const char* tag, const char* file) {
	if (!set->count || !validPart(tag) || !validPart(file)) return 0;
	char key[1024];
	if (snprintf(key, sizeof(key), "%s/%s", tag, file) >= (int)sizeof(key)) return 0;
	const char* k = key;
	return bsearch(&k, set->keys, (size_t)set->count, sizeof(char*), compareKeys) != NULL;
}

// The system folder's name in path below roms, and where the rest starts
// (past its slash), or NULL for a path that isn't a file inside one.
static const char* systemFolder(const char* roms, const char* path, char* folder, size_t size) {
	size_t n = strlen(roms);
	if (!n || strncmp(path, roms, n) != 0 || path[n] != '/') return NULL;
	const char* start = path + n + 1;
	const char* slash = strchr(start, '/');
	if (!slash || slash == start || !slash[1] || (size_t)(slash - start) >= size) return NULL;
	memcpy(folder, start, (size_t)(slash - start));
	folder[slash - start] = '\0';
	return slash + 1;
}

int RB_tagOf(const char* roms, const char* path, char* out, size_t size) {
	char folder[512];
	if (size) out[0] = '\0';
	if (!roms || !path || !size || !systemFolder(roms, path, folder, sizeof(folder))) return 0;
	const char* tag = folder;
	char* open = strrchr(folder, '(');
	if (open) {
		tag = open + 1;
		char* close = strchr(open, ')');
		if (close) *close = '\0';
	}
	if (!*tag || strlen(tag) >= size) return 0;
	snprintf(out, size, "%s", tag);
	return 1;
}

int RB_played(const RBSet* set, const char* roms, const char* path) {
	char tag[256], folder[512];
	if (!RB_tagOf(roms, path, tag, sizeof(tag))) return -1;
	const char* rest = systemFolder(roms, path, folder, sizeof(folder));
	const char* file = strrchr(rest, '/');
	if (RB_setHas(set, tag, file ? file + 1 : rest)) return 1;
	if (!file) return 0;
	// a disc of a multi-disc game, played by its .m3u
	const char* parent = file;
	while (parent > rest && parent[-1] != '/') parent--;
	char m3u[512];
	if (snprintf(m3u, sizeof(m3u), "%.*s.m3u", (int)(file - parent), parent) >= (int)sizeof(m3u)) return 0;
	return RB_setHas(set, tag, m3u);
}

// Copies name less its last len bytes to out, when that leaves something.
static int stripped(const char* name, size_t len, char* out, size_t size) {
	size_t n = strlen(name);
	if (n <= len || n - len >= size) return 0;
	memcpy(out, name, n - len);
	out[n - len] = '\0';
	return 1;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// How many digits name ends with.
static size_t trailingDigits(const char* name) {
	size_t n = strlen(name), d = 0;
	while (d < n && isdigit((unsigned char)name[n - d - 1])) d++;
	return d;
}

int RB_slotFile(const char* name, char* out, size_t size) {
	char base[512];
	if (!name || name[0] == '.') return 0;
	int bmp = endsWith(name, ".bmp");
	if (!bmp && !endsWith(name, ".txt")) return 0;
	if (!stripped(name, 4, base, sizeof(base))) return 0;
	// "<game>.<slot>" for a state's name or preview
	size_t d = trailingDigits(base), n = strlen(base);
	if (d && d < n && base[n - d - 1] == '.') return stripped(base, d + 1, out, size);
	if (bmp) return 0;
	if (strlen(base) >= size) return 0;
	snprintf(out, size, "%s", base);
	return 1;
}

int RB_stateFile(const char* name, char* out, size_t size) {
	if (!name || name[0] == '.') return 0;
	size_t d = trailingDigits(name), n = strlen(name);
	if (!d || n < d + 4 || strncmp(name + n - d - 3, ".st", 3) != 0) return 0;
	return stripped(name, d + 3, out, size);
}

int RB_saveFile(const char* name, char* out, size_t size) {
	if (!name || name[0] == '.' || !endsWith(name, ".sav")) return 0;
	return stripped(name, 4, out, size);
}

int RB_stateDir(const char* name, char* out, size_t size) {
	if (!name || name[0] == '.' || name[0] == '-') return 0;
	const char* dash = strchr(name, '-');
	if (!dash || !dash[1] || (size_t)(dash - name) >= size) return 0;
	memcpy(out, name, (size_t)(dash - name));
	out[dash - name] = '\0';
	return 1;
}

///////////////////////////////////////
// tiers and dots

int RB_tier(const char* value) {
	if (!value) return 0;
	while (*value == ' ' || *value == '\t') value++;
	int tier = 0, digits = 0;
	while (isdigit((unsigned char)*value) && digits < 3) {
		tier = tier * 10 + (*value++ - '0');
		digits++;
	}
	while (*value == ' ' || *value == '\t') value++;
	if (!digits || *value || tier < 1 || tier > RB_MAX_TIER) return 0;
	return tier;
}

int RB_place(const MosaicRect* rect, const MosaicRect* name, int dot, int gap, int unplayed, int tier,
	MosaicRect* out) {
	if (!rect || dot <= 0 || gap < 0) return 0;
	unplayed = unplayed ? 1 : 0;
	if (tier < 0) tier = 0;
	if (tier > RB_MAX_TIER) tier = RB_MAX_TIER;
	int count = unplayed + tier;
	if (!count) return 0;
	int width = count * dot + (count - 1) * gap + (unplayed && tier ? gap : 0);
	int margin = gap * 2;
	int right = rect->x + rect->w - margin;
	int x = name && name->w > 0 ? name->x + name->w + margin : right;
	int y;
	if (x + width <= right) {
		y = name->y + (name->h - dot) / 2;
	} else {
		x = right - width;
		y = rect->y + margin;
	}
	if (x < rect->x || y < rect->y || y + dot > rect->y + rect->h) return 0;
	for (int i = 0; i < count; i++) {
		out[i] = (MosaicRect){ x, y, dot, dot };
		x += dot + gap + (unplayed && i == 0 ? gap : 0);
	}
	return count;
}
