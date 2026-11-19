#include "theme_plus_packs.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void say(TPLogFn log, void* ctx, const char* format, ...) {
	if (!log) return;
	char line[320];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	log(ctx, line);
}

int TP_rotateMode(const char* value) {
	if (!value) return TP_ROTATE_OFF;
	if (!strcmp(value, "start")) return TP_ROTATE_START;
	if (!strcmp(value, "daily")) return TP_ROTATE_DAILY;
	return TP_ROTATE_OFF;
}

int TP_rotateCandidates(const TPCatalog* catalog, const char* between, int* out, int max, TPLogFn log, void* ctx) {
	int count = 0;
	if (!catalog || !out || max <= 0) return 0;
	if (!between || !between[0]) {
		for (int i = 0; i < catalog->theme_count && count < max; i++) out[count++] = i;
		return count;
	}
	const char* p = between;
	while (*p && count < max) {
		const char* end = strchr(p, ',');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		while (len && (*p == ' ' || *p == '\t')) {
			p++;
			len--;
		}
		while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
		if (len && len < TP_NAME_MAX) {
			char name[TP_NAME_MAX];
			memcpy(name, p, len);
			name[len] = '\0';
			const TPEntry* entry = TP_find(catalog, name);
			if (!entry) say(log, ctx, "rotate_between: no theme or palette is called %s, so it's left out", name);
			else {
				int index = (int)(entry - catalog->themes);
				int seen = 0;
				for (int i = 0; i < count; i++) seen |= out[i] == index;
				if (!seen) out[count++] = index;
			}
		}
		else if (len) say(log, ctx, "rotate_between: a name is longer than %d bytes, so it's left out", TP_NAME_MAX - 1);
		if (!end) break;
		p = end + 1;
	}
	return count;
}

long TP_dayKey(int year, int month, int day) {
	return (long)year * 10000 + month * 100 + day;
}

// Spreads neighbouring days apart, so day after day doesn't step
// through the list in order.
static unsigned long mix(unsigned long x) {
	x &= 0xFFFFFFFFul;
	x ^= x >> 16;
	x = (x * 0x7FEB352Dul) & 0xFFFFFFFFul;
	x ^= x >> 15;
	x = (x * 0x846CA68Bul) & 0xFFFFFFFFul;
	x ^= x >> 16;
	return x;
}

int TP_rotatePick(int mode, int count, long day, unsigned long random, int last) {
	if (count <= 0) return -1;
	if (mode == TP_ROTATE_DAILY) return (int)(mix((unsigned long)day) % (unsigned long)count);
	if (mode != TP_ROTATE_START) return -1;
	if (count == 1 || last < 0 || last >= count) return (int)(random % (unsigned long)count);
	int pick = (int)(random % (unsigned long)(count - 1));
	return pick >= last ? pick + 1 : pick;
}

int TP_systemOf(const char* roms, const char* path, char* tag, size_t size) {
	if (!roms || !path || !tag || size == 0) return -1;
	tag[0] = '\0';
	size_t n = strlen(roms);
	if (!n || strncmp(path, roms, n) != 0 || path[n] != '/') return -1;
	const char* name = path + n + 1;
	const char* slash = strchr(name, '/');
	size_t len = slash ? (size_t)(slash - name) : strlen(name);
	if (!len || name[0] == '.') return -1;
	// the last parentheses' inside, which must end the name
	if (name[len - 1] != ')') return -1;
	const char* open = NULL;
	for (const char* c = name; c < name + len - 1; c++) {
		if (*c == '(') open = c;
	}
	if (!open) return -1;
	size_t tag_len = (size_t)(name + len - 1 - open - 1);
	if (!tag_len || tag_len >= size) return -1;
	memcpy(tag, open + 1, tag_len);
	tag[tag_len] = '\0';
	if (strchr(tag, '/') || tag[0] == '.') {
		tag[0] = '\0';
		return -1;
	}
	return slash && slash[1] ? 1 : 0;
}

int TP_packNameOk(const char* name) {
	if (!name || !name[0] || name[0] == '.' || strlen(name) >= TP_NAME_MAX) return 0;
	return strchr(name, '/') == NULL && strchr(name, '\\') == NULL;
}

// Writes a, b and c into out, a TP_PATH_MAX buffer: 1, or 0 when they
// don't fit.
static int joined(char* out, const char* a, const char* b, const char* c) {
	int n = snprintf(out, TP_PATH_MAX, "%s%s%s", a, b, c);
	return n >= 0 && n < TP_PATH_MAX;
}

int TP_stockArt(int kind, const char* card, const char* path, char out[][TP_PATH_MAX], int max) {
	if (!card || !path || !out || max < 2 || strlen(path) >= TP_PATH_MAX) return 0;
	char dir[TP_PATH_MAX];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	const char* name = slash ? slash + 1 : path;
	if (slash) *slash = '\0';
	switch (kind) {
		case TP_ART_ICON: {
			char base[TP_PATH_MAX];
			snprintf(base, sizeof(base), "%s", name);
			char* dot = strrchr(base, '.');
			if (dot) *dot = '\0';
			char media[TP_PATH_MAX];
			if (!joined(media, dir, "/.media/", base)) return 0;
			return joined(out[0], media, ".png", "");
		}
		case TP_ART_FOLDER_BG:
			if (!joined(out[0], path, "/.media/bg.png", "")) return 0;
			return joined(out[1], card, "/bg.png", "") ? 2 : 1;
		case TP_ART_LIST_BG:
			if (!joined(out[0], dir, "/.media/bglist.png", "")) return 0;
			return joined(out[1], card, "/bg.png", "") ? 2 : 1;
		default:
			return 0;
	}
}

static int regularFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int TP_systemArt(const char stock[][TP_PATH_MAX], int stock_count, const char* pack_dir, const char* theme_dir,
		const char* tag, char* out, size_t size) {
	if (!tag || !tag[0] || !out || size == 0) return 0;
	out[0] = '\0';
	for (int i = 0; i < stock_count; i++) {
		if (regularFile(stock[i])) return 0;
	}
	char file[TP_NAME_MAX + 8];
	if ((size_t)snprintf(file, sizeof(file), "%s.png", tag) >= sizeof(file)) return 0;
	const char* dirs[] = { pack_dir, theme_dir };
	for (int i = 0; i < 2; i++) {
		if (dirs[i] && dirs[i][0] && TP_resolve(dirs[i], file, out, size, NULL) == 0) return 1;
	}
	out[0] = '\0';
	return 0;
}

void TP_previewColors(const TPTheme* parsed, const TPPalette* palette, const int* base, int* out) {
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) {
		out[i] = base ? base[i] : 0;
		if (palette) out[i] = palette->colors[i];
		if (parsed && parsed->colors[i] != MOSAIC_THEME_UNSET) out[i] = parsed->colors[i];
	}
}

int TP_move(int cursor, int count, int step, int wrap) {
	if (count <= 0) return -1;
	if (cursor < 0) cursor = 0;
	if (cursor >= count) cursor = count - 1;
	int next = cursor + step;
	if (next < 0) return wrap ? count - 1 : 0;
	if (next >= count) return wrap ? 0 : count - 1;
	return next;
}

int TP_window(int cursor, int count, int fit, int first) {
	if (fit <= 0 || count <= fit) return 0;
	if (first > count - fit) first = count - fit;
	if (first < 0) first = 0;
	if (cursor < first) first = cursor;
	if (cursor >= first + fit) first = cursor - fit + 1;
	return first < 0 ? 0 : first;
}
