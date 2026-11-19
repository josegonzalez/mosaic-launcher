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

int RB_pathsAdd(RBSet* set, const char* path) {
	if (!path || !path[0] || set->count >= RB_MAX_KEYS) return -1;
	if (set->count == set->cap) {
		int cap = set->cap ? set->cap * 2 : 64;
		char** keys = realloc(set->keys, (size_t)cap * sizeof(char*));
		if (!keys) return -1;
		set->keys = keys;
		set->cap = cap;
	}
	size_t n = strlen(path) + 1;
	char* key = malloc(n);
	if (!key) return -1;
	memcpy(key, path, n);
	set->keys[set->count++] = key;
	return 0;
}

int RB_pathsHas(const RBSet* set, const char* path) {
	if (!set->count || !path) return 0;
	return bsearch(&path, set->keys, (size_t)set->count, sizeof(char*), compareKeys) != NULL;
}

int RB_hasGame(const RBSet* set, const char* path, int is_dir) {
	if (RB_pathsHas(set, path)) return 1;
	if (!is_dir || !path || !set->count) return 0;
	const char* name = strrchr(path, '/');
	if (!name) return 0;
	char file[RB_PATH_MAX * 2];
	snprintf(file, sizeof(file), "%s%s.m3u", path, name);
	if (RB_pathsHas(set, file)) return 1;
	snprintf(file, sizeof(file), "%s%s.cue", path, name);
	return RB_pathsHas(set, file);
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

int RB_gameFiles(const char* roms, const char* path, int is_dir, char out[][RB_PATH_MAX], int max) {
	char folder[512];
	if (!roms || !path || !out || max < 1) return 0;
	const char* rest = systemFolder(roms, path, folder, sizeof(folder));
	if (!rest) return 0;
	size_t len = strlen(path);
	while (len && path[len - 1] == '/') len--;
	const char* name = path + len;
	while (name > rest && name[-1] != '/') name--;
	int name_len = (int)(path + len - name);
	if (!name_len) return 0;
	int count = 0;
	if (is_dir) {
		static const char* const kinds[] = { "m3u", "cue" };
		for (int i = 0; i < 2 && count < max; i++) {
			if (snprintf(out[count], RB_PATH_MAX, "%.*s/%.*s.%s", (int)len, path, name_len, name, kinds[i]) < RB_PATH_MAX) count++;
		}
		return count;
	}
	if (len >= RB_PATH_MAX) return 0;
	snprintf(out[count++], RB_PATH_MAX, "%.*s", (int)len, path);
	// a disc of a multi-disc game, by its folder's .m3u
	if (name == rest || count >= max) return count;
	const char* parent = name - 1;
	while (parent > rest && parent[-1] != '/') parent--;
	int parent_len = (int)(name - 1 - parent);
	char m3u[RB_PATH_MAX];
	if (snprintf(m3u, sizeof(m3u), "%.*s%.*s.m3u", (int)(name - path), path, parent_len, parent) >= (int)sizeof(m3u)) return count;
	if (strcmp(m3u, out[0]) != 0) snprintf(out[count++], RB_PATH_MAX, "%s", m3u);
	return count;
}

int RB_isPlayed(const RBSet* set, const char* roms, const char* path, int is_dir,
		int (*history)(const char* path, void* ctx), void* ctx) {
	char files[RB_GAME_FILES][RB_PATH_MAX];
	int count = RB_gameFiles(roms, path, is_dir, files, RB_GAME_FILES);
	if (!count) return -1;
	for (int i = 0; i < count; i++) {
		char tag[256];
		const char* file = strrchr(files[i], '/');
		if (set && RB_tagOf(roms, files[i], tag, sizeof(tag)) && RB_setHas(set, tag, file + 1)) return 1;
		if (history && history(files[i], ctx)) return 1;
	}
	return 0;
}

int RB_played(const RBSet* set, const char* roms, const char* path) {
	return RB_isPlayed(set, roms, path, 0, NULL, NULL);
}

int RB_reply(int played, char* out, size_t size) {
	if (!out || !size) return -1;
	int n = snprintf(out, size, "played=%i", played == 1);
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return -1;
	}
	return 0;
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

int RB_place(const MosaicRect* rect, const MosaicRect* name, int dot, int gap, int marks, int tier,
	MosaicRect* out) {
	return RB_layout(rect, name, dot, dot, gap, marks, 0, tier, out);
}

int RB_layout(const MosaicRect* rect, const MosaicRect* name, int dot, int icon, int gap, int marks, int icons,
	int tier, MosaicRect* out) {
	if (!rect || dot <= 0 || gap < 0) return 0;
	if (marks < 0) marks = 0;
	if (marks > RB_MAX_MARKS) marks = RB_MAX_MARKS;
	if (icons < 0 || icon <= 0) icons = 0;
	if (icons > RB_ICONS) icons = RB_ICONS;
	if (tier < 0) tier = 0;
	if (tier > RB_MAX_TIER) tier = RB_MAX_TIER;
	int count = marks + icons + tier;
	if (!count) return 0;
	int groups = (marks > 0) + (icons > 0) + (tier > 0);
	int width = (marks + tier) * dot + icons * icon + (count - 1) * gap + (groups - 1) * gap;
	int height = icons && icon > dot ? icon : dot;
	int margin = gap * 2;
	int right = rect->x + rect->w - margin;
	int x = name && name->w > 0 ? name->x + name->w + margin : right;
	int top;
	if (x + width <= right) {
		top = name->y + (name->h - height) / 2;
	} else {
		x = right - width;
		top = rect->y + margin;
	}
	if (x < rect->x || top < rect->y || top + height > rect->y + rect->h) return 0;
	for (int i = 0; i < count; i++) {
		int size = i >= marks && i < marks + icons ? icon : dot;
		out[i] = (MosaicRect){ x, top + (height - size) / 2, size, size };
		x += size + gap;
		// twice the gap after the last of a group with another after it
		if ((i == marks - 1 && icons + tier) || (icons && i == marks + icons - 1 && tier)) x += gap;
	}
	return count;
}

///////////////////////////////////////
// status flags and the trophy

static const char* const flag_sets[RB_FLAGS] = { "tags.status.completed", "tags.status.beaten", "tags.status.broken", "tags.status.backlog" };

// each icon's picture, a row a string, # for a filled cell
static const char* const pictures[RB_ICONS][RB_ICON_CELLS] = {
	// Completed: a star
	{ "...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", ".#...#." },
	// Beaten: a tick
	{ ".....##", "....###", "#..###.", "#####..", ".###...", "..#....", "......." },
	// Broken: a cross
	{ "##...##", "###.###", ".#####.", "..###..", ".#####.", "###.###", "##...##" },
	// Backlog: an hourglass
	{ "#######", ".#...#.", "..#.#..", "...#...", "..###..", ".#####.", "#######" },
	// 100%: a trophy
	{ "#.###.#", "#.###.#", ".#####.", "..###..", "...#...", "..###..", ".#####." },
};

const char* RB_flagSet(int icon) {
	return icon >= 0 && icon < RB_FLAGS ? flag_sets[icon] : NULL;
}

int RB_flagOf(const char* set) {
	if (!set) return -1;
	for (int i = 0; i < RB_FLAGS; i++) {
		if (!strcmp(flag_sets[i], set)) return i;
	}
	return -1;
}

int RB_iconCell(int icon, int x, int y) {
	if (icon < 0 || icon >= RB_ICONS || x < 0 || y < 0 || x >= RB_ICON_CELLS || y >= RB_ICON_CELLS) return 0;
	return pictures[icon][y][x] == '#';
}

int RB_iconRects(int icon, int x, int y, int unit, MosaicRect* out, int max) {
	int count = 0;
	if (unit <= 0) return 0;
	for (int row = 0; row < RB_ICON_CELLS; row++) {
		for (int col = 0; col < RB_ICON_CELLS; col++) {
			if (!RB_iconCell(icon, col, row)) continue;
			int run = col;
			while (run < RB_ICON_CELLS && RB_iconCell(icon, run, row)) run++;
			if (count >= max) return count;
			out[count++] = (MosaicRect){ x + col * unit, y + row * unit, (run - col) * unit, unit };
			col = run;
		}
	}
	return count;
}

int RB_perfect(int rc, int total, int unlocked) {
	if (rc == MOSAIC_ACHIEVEMENTS_UNKNOWN) return -1;
	return rc == 1 && total > 0 && unlocked >= total;
}
