#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "titles.h"

// New names stay under the launcher's 256-byte name buffers, with room for
// the " (tag)" its own fallback adds to names that still match.
#define LABEL_SIZE 200

// Copies len bytes of from, or as many whole characters as fit.
static void copyCut(char* out, size_t size, const char* from, size_t len) {
	if (size == 0) return;
	if (len >= size) {
		len = size - 1;
		while (len > 0 && ((unsigned char)from[len] & 0xC0) == 0x80) len--;
	}
	memcpy(out, from, len);
	out[len] = '\0';
}

static int isSpace(char c) {
	return c == ' ' || c == '\t';
}

// The launcher drops a dot and 2 to 4 more characters from the end of a
// name, as many times as they're there (eg. ".p8.png"), then everything
// from the first ( or [ after the first character. Here what it drops is
// kept as the extension and the tags.
static void splitName(const char* filename, TitleParts* out) {
	char stem[512];
	copyCut(stem, sizeof(stem), filename, strlen(filename));
	char* dot;
	while ((dot = strrchr(stem, '.')) != NULL) {
		size_t len = strlen(dot);
		if (len > 2 && len <= 5) *dot = '\0';
		else break;
	}
	const char* ext = filename + strlen(stem);
	copyCut(out->ext, sizeof(out->ext), ext, strlen(ext));

	char base[512];
	strcpy(base, stem);
	char* cut;
	while ((cut = strrchr(base, '(')) != NULL || (cut = strrchr(base, '[')) != NULL) {
		if (cut == base) break;
		*cut = '\0';
	}
	const char* tags = stem + strlen(base);
	while (isSpace(*tags)) tags++;
	size_t len = strlen(tags);
	while (len > 0 && isSpace(tags[len - 1])) len--;
	copyCut(out->tags, sizeof(out->tags), tags, len);
}

void Titles_parts(const char* roms, const char* path, TitleParts* out) {
	memset(out, 0, sizeof(*out));
	if (!path) return;

	size_t path_len = strlen(path);
	while (path_len > 1 && path[path_len - 1] == '/') path_len--;
	const char* filename = path;
	for (size_t i = 0; i < path_len; i++) {
		if (path[i] == '/') filename = path + i + 1;
	}
	char name[512];
	copyCut(name, sizeof(name), filename, path_len - (size_t)(filename - path));
	copyCut(out->file, sizeof(out->file), name, strlen(name));
	splitName(name, out);

	if (filename > path) {
		const char* end = filename - 1; // the slash before the file name
		const char* start = end;
		while (start > path && start[-1] != '/') start--;
		copyCut(out->folder, sizeof(out->folder), start, (size_t)(end - start));
	}

	size_t roms_len = roms ? strlen(roms) : 0;
	if (roms_len && !strncmp(path, roms, roms_len) && path[roms_len] == '/') {
		const char* console = path + roms_len + 1;
		const char* end = strchr(console, '/');
		size_t len = end ? (size_t)(end - console) : strlen(console);
		const char* open = NULL;
		for (size_t i = 0; i < len; i++) {
			if (console[i] == '(') open = console + i;
		}
		const char* close = open ? memchr(open, ')', len - (size_t)(open - console)) : NULL;
		if (open && close) copyCut(out->system, sizeof(out->system), open + 1, (size_t)(close - open - 1));
		else copyCut(out->system, sizeof(out->system), console, len);
	}
}

void Titles_label(const char* name, const TitleParts* parts, int mask, char* out, size_t size) {
	char label[1024];
	size_t len = 0;
	label[0] = '\0';
#define ADD(...) len += (size_t)snprintf(label + len, len < sizeof(label) ? sizeof(label) - len : 0, __VA_ARGS__); \
	if (len >= sizeof(label)) len = sizeof(label) - 1
	ADD("%s", name ? name : "");
	if ((mask & TITLES_SYSTEM) && parts->system[0]) { ADD(" (%s)", parts->system); }
	if ((mask & TITLES_FOLDER) && parts->folder[0]) { ADD(" (%s)", parts->folder); }
	if (mask & TITLES_FILE) {
		if (parts->file[0]) { ADD(" (%s)", parts->file); }
	} else {
		if ((mask & TITLES_TAGS) && parts->tags[0]) { ADD(" %s", parts->tags); }
		if ((mask & TITLES_EXT) && parts->ext[0]) { ADD("%s", parts->ext); }
	}
#undef ADD
	copyCut(out, size, label, len);
}

// The parts that aren't the same for every entry in the group.
static int differing(const TitleParts* parts, int n) {
	int mask = 0;
	for (int i = 1; i < n; i++) {
		if (strcmp(parts[i].system, parts[0].system)) mask |= TITLES_SYSTEM;
		if (strcmp(parts[i].tags, parts[0].tags)) mask |= TITLES_TAGS;
		if (strcmp(parts[i].ext, parts[0].ext)) mask |= TITLES_EXT;
		if (strcmp(parts[i].folder, parts[0].folder)) mask |= TITLES_FOLDER;
		if (strcmp(parts[i].file, parts[0].file)) mask |= TITLES_FILE;
	}
	return mask;
}

// Sets of parts to try, fewest first, then by preference, with the file
// name last.
#define S TITLES_SYSTEM
#define T TITLES_TAGS
#define E TITLES_EXT
#define F TITLES_FOLDER
#define N TITLES_FILE
static const int choices[] = {
	S, T, E, F,
	S|T, S|E, T|E, S|F, T|F, E|F,
	S|T|E, S|T|F, S|E|F, T|E|F,
	S|T|E|F,
	N, S|N, F|N, S|F|N,
};
#undef N
#undef S
#undef T
#undef E
#undef F

static int allDifferent(const char* name, const TitleParts* parts, int n, int mask, char (*labels)[LABEL_SIZE]) {
	for (int i = 0; i < n; i++) Titles_label(name, &parts[i], mask, labels[i], LABEL_SIZE);
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			if (!strcmp(labels[i], labels[j])) return 0;
		}
	}
	return 1;
}

int Titles_pick(const char* name, const TitleParts* parts, int n) {
	int differ = n > 1 ? differing(parts, n) : 0;
	if (!differ) return 0;
	char (*labels)[LABEL_SIZE] = malloc((size_t)n * LABEL_SIZE);
	if (!labels) return differ;
	int picked = differ;
	for (size_t c = 0; c < sizeof(choices) / sizeof(choices[0]); c++) {
		int mask = choices[c];
		if (mask & ~differ) continue;
		if (allDifferent(name, parts, n, mask, labels)) {
			picked = mask;
			break;
		}
	}
	free(labels);
	return picked;
}

static const char* const* sort_names;

static int byName(const void* a, const void* b) {
	int ia = *(const int*)a;
	int ib = *(const int*)b;
	int c = strcmp(sort_names[ia], sort_names[ib]);
	if (c) return c;
	return ia < ib ? -1 : ia > ib;
}

static void freeLabels(char** labels, int n) {
	for (int i = 0; i < n; i++) {
		free(labels[i]);
		labels[i] = NULL;
	}
}

// Names one group of count entries, order[0..count), that share a name.
// Returns how many got a new name, or -1 when memory ran out.
static int nameGroup(const char* roms, int same_system, const int* order, int count,
	const char* const* names, const char* const* paths, char** labels) {
	TitleParts* parts = malloc((size_t)count * sizeof(TitleParts));
	if (!parts) return -1;
	const char* name = names[order[0]];
	for (int k = 0; k < count; k++) Titles_parts(roms, paths[order[k]], &parts[k]);
	int mask = (same_system || (differing(parts, count) & TITLES_SYSTEM)) ? Titles_pick(name, parts, count) : 0;
	int renamed = 0;
	for (int k = 0; mask && k < count; k++) {
		char label[LABEL_SIZE];
		Titles_label(name, &parts[k], mask, label, sizeof(label));
		if (!strcmp(label, name)) continue;
		labels[order[k]] = strdup(label);
		if (!labels[order[k]]) {
			renamed = -1;
			break;
		}
		renamed++;
	}
	free(parts);
	return renamed;
}

int Titles_list(const char* roms, int same_system, int n,
	const char* const* names, const char* const* paths, char** labels) {
	for (int i = 0; i < n; i++) labels[i] = NULL;
	if (n < 2) return 0;

	int* order = malloc((size_t)n * sizeof(int));
	if (!order) return -1;
	int kept = 0;
	for (int i = 0; i < n; i++) {
		if (names[i] && paths[i]) order[kept++] = i;
	}
	sort_names = names;
	qsort(order, (size_t)kept, sizeof(int), byName);

	int renamed = 0;
	for (int start = 0; start < kept;) {
		int end = start + 1;
		while (end < kept && !strcmp(names[order[end]], names[order[start]])) end++;
		if (end - start > 1) {
			int got = nameGroup(roms, same_system, order + start, end - start, names, paths, labels);
			if (got < 0) {
				freeLabels(labels, n);
				free(order);
				return -1;
			}
			renamed += got;
		}
		start = end;
	}
	free(order);
	return renamed;
}
