#include "storage_model.h"
#include "info_model.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define PATH_MAX_LEN 1024
#define NAMES_MAX 512 // folders read from one of the card's top folders

// Fixed text and a count's form in system info's words, for make pot
static const char* tr(const char* text) {
	return Si_tr(text);
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	return Si_trPlural(singular, plural, n);
}

void St_init(StTotals* t) {
	memset(t, 0, sizeof(*t));
}

static void copy(char* out, size_t size, const char* s, size_t n) {
	if (!out || !size) return;
	if (n >= size) n = size - 1;
	memcpy(out, s, n);
	out[n] = '\0';
}

// Where the trailing " (TAG)" starts, and the tag's start and length, or
// NULL without one.
static const char* tagStart(const char* folder, const char** tag, size_t* len) {
	size_t n = strlen(folder);
	if (n < 3 || folder[n - 1] != ')') return NULL;
	const char* open = NULL;
	for (const char* p = folder + n - 2; p >= folder; p--) {
		if (*p == '(') {
			open = p;
			break;
		}
		if (*p == ')') return NULL;
	}
	if (!open || open + 1 == folder + n - 1) return NULL;
	*tag = open + 1;
	*len = (size_t)(folder + n - 1 - (open + 1));
	return open;
}

void St_tag(const char* folder, char* out, size_t size) {
	const char* tag;
	size_t len;
	if (tagStart(folder, &tag, &len)) copy(out, size, tag, len);
	else copy(out, size, folder, strlen(folder));
}

void St_name(const char* folder, char* out, size_t size) {
	const char* start = folder;
	// a sort prefix: digits and ") "
	const char* p = folder;
	while (*p >= '0' && *p <= '9') p++;
	if (p > folder && p[0] == ')' && p[1] == ' ') start = p + 2;
	const char* tag;
	size_t len;
	const char* open = tagStart(start, &tag, &len);
	const char* end = open ? open : start + strlen(start);
	while (end > start && end[-1] == ' ') end--;
	if (end == start) {
		// nothing before the tag: the folder's whole name
		copy(out, size, folder, strlen(folder));
		return;
	}
	copy(out, size, start, (size_t)(end - start));
}

int St_stateTag(const char* folder, char* out, size_t size) {
	const char* dash = strchr(folder, '-');
	if (!dash || dash == folder || !dash[1]) return -1;
	copy(out, size, folder, (size_t)(dash - folder));
	return 0;
}

int St_find(const StTotals* t, const char* tag) {
	for (int i = 0; i < t->count; i++) {
		if (strcmp(t->rows[i].tag, tag) == 0) return i;
	}
	return -1;
}

int St_row(StTotals* t, const char* tag, const char* name) {
	if (!tag || !*tag) return -1;
	int i = St_find(t, tag);
	if (i < 0) {
		if (t->count >= ST_ROWS_MAX) return -1;
		i = t->count++;
		memset(&t->rows[i], 0, sizeof(t->rows[i]));
		snprintf(t->rows[i].tag, sizeof(t->rows[i].tag), "%s", tag);
	}
	if (name && *name && !t->rows[i].name[0]) snprintf(t->rows[i].name, sizeof(t->rows[i].name), "%s", name);
	return i;
}

unsigned long long St_rowTotal(const StRow* row) {
	unsigned long long sum = 0;
	for (int p = 0; p < ST_PARTS; p++) sum += row->bytes[p];
	return sum;
}

unsigned long long St_total(const StTotals* t) {
	unsigned long long sum = t->bios + t->screenshots;
	for (int i = 0; i < t->count; i++) sum += St_rowTotal(&t->rows[i]);
	return sum;
}

static const char* rowName(const StRow* r) {
	return r->name[0] ? r->name : r->tag;
}

static int byTotal(const void* a, const void* b) {
	const StRow* x = a;
	const StRow* y = b;
	unsigned long long tx = St_rowTotal(x), ty = St_rowTotal(y);
	if (tx != ty) return tx > ty ? -1 : 1;
	int c = strcasecmp(rowName(x), rowName(y));
	return c ? c : strcmp(x->tag, y->tag);
}

void St_sort(StTotals* t) {
	qsort(t->rows, (size_t)t->count, sizeof(t->rows[0]), byTotal);
}

///////////////////////////////////////
// files counted once

void St_seenInit(StSeen* s) {
	memset(s, 0, sizeof(*s));
}

void St_seenFree(StSeen* s) {
	free(s->keys);
	memset(s, 0, sizeof(*s));
}

int St_seenAdd(StSeen* s, unsigned long long dev, unsigned long long ino) {
	for (size_t i = 0; i < s->count; i++) {
		if (s->keys[2 * i] == dev && s->keys[2 * i + 1] == ino) return 0;
	}
	if (s->count == s->cap) {
		size_t cap = s->cap ? s->cap * 2 : 64;
		unsigned long long* keys = realloc(s->keys, cap * 2 * sizeof(*keys));
		if (!keys) return -1;
		s->keys = keys;
		s->cap = cap;
	}
	s->keys[2 * s->count] = dev;
	s->keys[2 * s->count + 1] = ino;
	s->count++;
	return 1;
}

void St_walkInit(StWalk* w) {
	memset(w, 0, sizeof(*w));
	St_seenInit(&w->seen);
}

void St_walkFree(StWalk* w) {
	St_seenFree(&w->seen);
}

static int cancelled(const StWalk* w) {
	return w->cancel && *w->cancel;
}

// One file or folder: a folder's files, or the entry itself.
static int walk(StWalk* w, const char* path, int in_art, unsigned long long* bytes, unsigned long long* art, int depth) {
	if (cancelled(w)) return -1;
	struct stat st;
	if (lstat(path, &st) != 0) return 0;
	if (!S_ISDIR(st.st_mode)) {
		// a link is its own few bytes, never its target
		if (st.st_nlink > 1 && St_seenAdd(&w->seen, (unsigned long long)st.st_dev, (unsigned long long)st.st_ino) == 0) return 0;
		*(in_art && art ? art : bytes) += (unsigned long long)st.st_size;
		w->files++;
		return 0;
	}
	if (depth > 64) return 0;
	DIR* d = opendir(path);
	if (!d) return 0;
	int rc = 0;
	struct dirent* e;
	char child[PATH_MAX_LEN];
	while (rc == 0 && (e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) continue;
		rc = walk(w, child, in_art || !strcmp(e->d_name, ".media"), bytes, art, depth + 1);
	}
	closedir(d);
	return rc;
}

int St_dirSize(StWalk* w, const char* path, unsigned long long* bytes, unsigned long long* art) {
	return walk(w, path, 0, bytes, art, 0);
}

///////////////////////////////////////
// the card

static int byName(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

// The folders in path, sorted by name, not those starting with ".", to free
// with freeNames. Returns how many.
static int folders(const char* path, char** names, int max) {
	DIR* d = opendir(path);
	if (!d) return 0;
	int n = 0;
	struct dirent* e;
	char child[PATH_MAX_LEN];
	while (n < max && (e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child)) continue;
		struct stat st;
		if (lstat(child, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		names[n] = strdup(e->d_name);
		if (names[n]) n++;
	}
	closedir(d);
	qsort(names, (size_t)n, sizeof(names[0]), byName);
	return n;
}

static void freeNames(char** names, int n) {
	for (int i = 0; i < n; i++) free(names[i]);
}

static void progress(StWalk* w, const char* name, int done, int total) {
	if (w->progress) w->progress(w->ctx, name, done, total, w->files);
}

int St_count(const char* sdcard, StTotals* t, StWalk* w) {
	St_init(t);
	char* roms[NAMES_MAX];
	char* saves[NAMES_MAX];
	char* states[NAMES_MAX];
	char path[PATH_MAX_LEN];
	snprintf(path, sizeof(path), "%s/Roms", sdcard);
	int nroms = folders(path, roms, NAMES_MAX);
	snprintf(path, sizeof(path), "%s/Saves", sdcard);
	int nsaves = folders(path, saves, NAMES_MAX);
	snprintf(path, sizeof(path), "%s/.userdata/shared", sdcard);
	int nstates = folders(path, states, NAMES_MAX);

	// the rows first, so the progress knows how many systems there are
	char tag[ST_TAG_MAX], name[ST_NAME_MAX];
	for (int i = 0; i < nroms; i++) {
		St_tag(roms[i], tag, sizeof(tag));
		St_name(roms[i], name, sizeof(name));
		St_row(t, tag, name);
	}
	for (int i = 0; i < nsaves; i++) St_row(t, saves[i], NULL);
	int systems = t->count;

	int rc = 0;
	for (int r = 0; r < systems && rc == 0; r++) {
		StRow* row = &t->rows[r];
		progress(w, rowName(row), r + 1, systems);
		for (int i = 0; i < nroms && rc == 0; i++) {
			St_tag(roms[i], tag, sizeof(tag));
			if (strcmp(tag, row->tag) != 0) continue;
			snprintf(path, sizeof(path), "%s/Roms/%s", sdcard, roms[i]);
			rc = St_dirSize(w, path, &row->bytes[ST_GAMES], &row->bytes[ST_ART]);
		}
		for (int i = 0; i < nsaves && rc == 0; i++) {
			if (strcmp(saves[i], row->tag) != 0) continue;
			snprintf(path, sizeof(path), "%s/Saves/%s", sdcard, saves[i]);
			rc = St_dirSize(w, path, &row->bytes[ST_SAVES], NULL);
		}
		for (int i = 0; i < nstates && rc == 0; i++) {
			if (St_stateTag(states[i], tag, sizeof(tag)) != 0 || strcmp(tag, row->tag) != 0) continue;
			snprintf(path, sizeof(path), "%s/.userdata/shared/%s", sdcard, states[i]);
			rc = St_dirSize(w, path, &row->bytes[ST_SAVES], NULL);
		}
	}
	if (rc == 0) {
		progress(w, NULL, systems + 1, systems);
		snprintf(path, sizeof(path), "%s/Bios", sdcard);
		rc = St_dirSize(w, path, &t->bios, NULL);
	}
	if (rc == 0) {
		progress(w, NULL, systems + 2, systems);
		snprintf(path, sizeof(path), "%s/Screenshots", sdcard);
		rc = St_dirSize(w, path, &t->screenshots, NULL);
	}
	freeNames(roms, nroms);
	freeNames(saves, nsaves);
	freeNames(states, nstates);
	t->files = w->files;
	if (rc == 0) St_sort(t);
	return rc;
}

///////////////////////////////////////
// text

void St_formatCount(unsigned long long n, char* out, size_t size) {
	char digits[32];
	snprintf(digits, sizeof(digits), "%llu", n);
	size_t len = strlen(digits), o = 0;
	for (size_t i = 0; i < len && o + 1 < size; i++) {
		if (i && (len - i) % 3 == 0 && o + 2 < size) out[o++] = ',';
		out[o++] = digits[i];
	}
	if (size) out[o < size ? o : size - 1] = '\0';
}

void St_formatProgress(const char* name, int done, int total, unsigned long long files, char* out, size_t size) {
	char count[32], systems[96], counted[96];
	St_formatCount(files, count, sizeof(count));
	if (done > total) name = done == total + 1 ? tr("BIOS") : tr("Screenshots");
	if (done > total) done = total;
	snprintf(systems, sizeof(systems), trPlural("%i of %i system", "%i of %i systems", (unsigned long)total), done, total);
	snprintf(counted, sizeof(counted), trPlural("%s file", "%s files", (unsigned long)files), count);
	snprintf(out, size, tr("Counting %s, %s, %s"), name ? name : "", systems, counted);
}

///////////////////////////////////////
// when to count

void St_cacheInit(StCache* c) {
	memset(c, 0, sizeof(*c));
}

void St_cacheChanged(StCache* c) {
	c->card++;
}

int St_cacheStart(StCache* c) {
	if (c->counting) return 0;
	if (c->have && c->counted == c->card) return 0;
	c->counting = 1;
	c->started = c->card;
	return 1;
}

void St_cacheDone(StCache* c, int ok) {
	if (!c->counting) return;
	c->counting = 0;
	if (!ok) return;
	c->have = 1;
	c->counted = c->started;
}

int St_cacheStale(const StCache* c) {
	return !c->have || c->counted != c->card;
}
