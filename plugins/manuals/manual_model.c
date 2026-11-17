#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "manual_model.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static int endsWith(const char* s, const char* ext) {
	size_t n = strlen(s), e = strlen(ext);
	return n > e && strcasecmp(s + n - e, ext) == 0;
}

static int copy(char* out, size_t size, const char* s) {
	return snprintf(out, size, "%s", s) < (int)size ? 0 : -1;
}

///////////////////////////////////////
// names

int MN_isPage(const char* file) {
	if (!file || file[0] == '.' || strchr(file, '/')) return 0;
	return endsWith(file, ".png") || endsWith(file, ".jpg") || endsWith(file, ".jpeg");
}

int MN_nameCompare(const char* a, const char* b) {
	const char *p = a, *q = b;
	while (*p && *q) {
		if (isdigit((unsigned char)*p) && isdigit((unsigned char)*q)) {
			while (*p == '0' && isdigit((unsigned char)p[1])) p++;
			while (*q == '0' && isdigit((unsigned char)q[1])) q++;
			size_t n = 0, m = 0;
			while (isdigit((unsigned char)p[n])) n++;
			while (isdigit((unsigned char)q[m])) m++;
			if (n != m) return n < m ? -1 : 1;
			int c = strncmp(p, q, n);
			if (c) return c;
			p += n;
			q += m;
			continue;
		}
		int c = tolower((unsigned char)*p) - tolower((unsigned char)*q);
		if (c) return c;
		p++;
		q++;
	}
	if (*p || *q) return *p ? 1 : -1;
	return strcmp(a, b);
}

// The length of a disc marker at s, " (Disc 1)" and the rest, or 0.
static size_t discMarker(const char* s) {
	static const char* const words[] = { "Disc", "Disk", "CD" };
	if (s[0] != ' ' || s[1] != '(') return 0;
	const char* p = s + 2;
	size_t word = 0;
	for (size_t i = 0; i < sizeof(words) / sizeof(words[0]) && !word; i++) {
		size_t n = strlen(words[i]);
		if (strncasecmp(p, words[i], n) == 0 && p[n] == ' ') word = n;
	}
	if (!word) return 0;
	p += word + 1;
	if (!isdigit((unsigned char)*p)) return 0;
	while (isdigit((unsigned char)*p)) p++;
	if (strncasecmp(p, " of ", 4) == 0 && isdigit((unsigned char)p[4])) {
		p += 4;
		while (isdigit((unsigned char)*p)) p++;
	}
	return *p == ')' ? (size_t)(p + 1 - s) : 0;
}

int MN_stripDisc(const char* name, char* out, size_t size) {
	for (const char* s = name; *s; s++) {
		size_t n = discMarker(s);
		if (!n || s == name) continue;
		snprintf(out, size, "%.*s%s", (int)(s - name), name, s + n);
		return 1;
	}
	snprintf(out, size, "%s", name);
	return 0;
}

static void addName(MNWhere* w, const char* name) {
	if (!name || !name[0] || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) return;
	for (int i = 0; i < w->name_count; i++) {
		if (!strcmp(w->names[i], name)) return;
	}
	if (w->name_count < 3 && copy(w->names[w->name_count], MN_NAME_MAX, name) == 0) w->name_count++;
}

static void addDir(MNWhere* w, const char* dir) {
	for (int i = 0; i < w->dir_count; i++) {
		if (!strcmp(w->dirs[i], dir)) return;
	}
	if (w->dir_count < 2 && copy(w->dirs[w->dir_count], MN_PATH_MAX, dir) == 0) w->dir_count++;
}

int MN_where(const char* roms, const char* path, int is_dir, const char* shown, MNWhere* out) {
	memset(out, 0, sizeof(*out));
	if (!path) return 0;
	char dir[MN_PATH_MAX], name[MN_NAME_MAX];
	if (copy(dir, sizeof(dir), path) != 0) return 0;
	size_t len = strlen(dir);
	while (len > 1 && dir[len - 1] == '/') dir[--len] = '\0';
	char* slash = strrchr(dir, '/');
	if (!slash || !slash[1]) return 0;
	if (copy(name, sizeof(name), slash + 1) != 0) return 0;
	*slash = '\0';
	if (!is_dir) {
		char* dot = strrchr(name, '.');
		if (dot && dot != name) {
			int sheet = !strcasecmp(dot, ".m3u") || !strcasecmp(dot, ".cue");
			*dot = '\0';
			// Game/Game.m3u is the game Game, beside its folder
			const char* parent = strrchr(dir, '/');
			if (sheet && parent && !strcmp(parent + 1, name)) *strrchr(dir, '/') = '\0';
		}
	}
	if (!dir[0]) return 0;
	addDir(out, dir);
	size_t rl = roms ? strlen(roms) : 0;
	if (rl && !strncmp(dir, roms, rl) && dir[rl] == '/') {
		const char* deeper = strchr(dir + rl + 1, '/');
		if (deeper) {
			char system[MN_PATH_MAX];
			snprintf(system, sizeof(system), "%.*s", (int)(deeper - dir), dir);
			addDir(out, system);
		}
	}
	char stripped[MN_NAME_MAX];
	addName(out, name);
	if (MN_stripDisc(name, stripped, sizeof(stripped))) addName(out, stripped);
	addName(out, shown);
	return out->name_count;
}

///////////////////////////////////////
// finding manuals

void MN_shelfInit(MNShelf* shelf) {
	memset(shelf, 0, sizeof(*shelf));
}

void MN_shelfClear(MNShelf* shelf) {
	for (int i = 0; i < shelf->count; i++) free(shelf->dirs[i].items);
	MN_shelfInit(shelf);
}

// 1 when path is a real folder, not a link to one.
static int realDir(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int anyPage(const char* dir) {
	if (!realDir(dir)) return 0;
	DIR* dh = opendir(dir);
	if (!dh) return 0;
	struct dirent* dp;
	int found = 0;
	while (!found && (dp = readdir(dh)) != NULL) {
		char path[MN_PATH_MAX];
		struct stat st;
		if (!MN_isPage(dp->d_name) || snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;
		found = lstat(path, &st) == 0 && S_ISREG(st.st_mode);
	}
	closedir(dh);
	return found;
}

static int itemCompare(const void* a, const void* b) {
	const MNShelfItem *x = a, *y = b;
	int c = strcasecmp(x->name, y->name);
	// the same name: pages first, so they win
	return c ? c : x->kind - y->kind;
}

static void readShelf(MNShelfDir* d) {
	char manuals[MN_PATH_MAX];
	if (snprintf(manuals, sizeof(manuals), "%s/.manuals", d->dir) >= (int)sizeof(manuals) || !realDir(manuals)) return;
	DIR* dh = opendir(manuals);
	if (!dh) return;
	int cap = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (dp->d_name[0] == '.') continue;
		char path[MN_PATH_MAX];
		struct stat st;
		if (snprintf(path, sizeof(path), "%s/%s", manuals, dp->d_name) >= (int)sizeof(path) || lstat(path, &st) != 0) continue;
		MNShelfItem item = { .kind = MN_NONE };
		if (copy(item.file, sizeof(item.file), dp->d_name) != 0 || copy(item.name, sizeof(item.name), dp->d_name) != 0) continue;
		if (S_ISDIR(st.st_mode) && anyPage(path)) {
			item.kind = MN_PAGES;
		}
		else if (S_ISREG(st.st_mode) && endsWith(dp->d_name, ".txt")) {
			item.name[strlen(item.name) - 4] = '\0';
			item.kind = MN_TEXT;
		}
		if (!item.kind) continue;
		if (d->count == cap) {
			int more = cap ? cap * 2 : 16;
			MNShelfItem* grown = realloc(d->items, sizeof(MNShelfItem) * more);
			if (!grown) break;
			d->items = grown;
			cap = more;
		}
		d->items[d->count++] = item;
	}
	closedir(dh);
	if (d->count) qsort(d->items, d->count, sizeof(MNShelfItem), itemCompare);
}

static MNShelfDir* shelfDir(MNShelf* shelf, const char* dir) {
	shelf->clock++;
	for (int i = 0; i < shelf->count; i++) {
		if (!strcmp(shelf->dirs[i].dir, dir)) {
			shelf->dirs[i].used = shelf->clock;
			return &shelf->dirs[i];
		}
	}
	MNShelfDir* d;
	if (shelf->count < MN_SHELF_MAX) {
		d = &shelf->dirs[shelf->count++];
	}
	else {
		d = &shelf->dirs[0];
		for (int i = 1; i < shelf->count; i++) {
			if (shelf->dirs[i].used < d->used) d = &shelf->dirs[i];
		}
		free(d->items);
	}
	memset(d, 0, sizeof(*d));
	if (copy(d->dir, sizeof(d->dir), dir) == 0) readShelf(d);
	d->used = shelf->clock;
	return d;
}

const MNShelfItem* MN_shelfFind(MNShelf* shelf, const char* dir, const char* name) {
	if (!shelf || !dir || !name || !name[0]) return NULL;
	MNShelfDir* d = shelfDir(shelf, dir);
	int lo = 0, hi = d->count - 1, found = -1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = strcasecmp(d->items[mid].name, name);
		if (c < 0) lo = mid + 1;
		else {
			if (c == 0) found = mid; // the first with the name, the pages if any
			hi = mid - 1;
		}
	}
	return found < 0 ? NULL : &d->items[found];
}

int MN_find(MNShelf* shelf, const MNWhere* where, char* path, size_t size) {
	for (int n = 0; n < where->name_count; n++) {
		for (int d = 0; d < where->dir_count; d++) {
			const MNShelfItem* item = MN_shelfFind(shelf, where->dirs[d], where->names[n]);
			if (!item) continue;
			if (path && snprintf(path, size, "%s/.manuals/%s", where->dirs[d], item->file) >= (int)size) continue;
			return item->kind;
		}
	}
	if (path && size) path[0] = '\0';
	return MN_NONE;
}

///////////////////////////////////////
// pages

void MN_pagesFree(MNPages* pages) {
	free(pages->files);
	pages->files = NULL;
	pages->count = 0;
}

static int pageCompare(const void* a, const void* b) {
	return MN_nameCompare(a, b);
}

int MN_pagesScan(const char* dir, MNPages* pages) {
	MN_pagesFree(pages);
	if (!realDir(dir)) return -1;
	DIR* dh = opendir(dir);
	if (!dh) return -1;
	int cap = 0;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL && pages->count < MN_MAX_PAGES) {
		char path[MN_PATH_MAX];
		struct stat st;
		if (!MN_isPage(dp->d_name) || strlen(dp->d_name) >= MN_NAME_MAX) continue;
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;
		if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		if (pages->count == cap) {
			int more = cap ? cap * 2 : 16;
			char(*grown)[MN_NAME_MAX] = realloc(pages->files, sizeof(*grown) * more);
			if (!grown) break;
			pages->files = grown;
			cap = more;
		}
		copy(pages->files[pages->count++], MN_NAME_MAX, dp->d_name);
	}
	closedir(dh);
	if (pages->count) qsort(pages->files, pages->count, MN_NAME_MAX, pageCompare);
	return pages->count;
}

///////////////////////////////////////
// text

void MN_textFree(MNText* text) {
	free(text->text);
	free(text->lines);
	memset(text, 0, sizeof(*text));
}

// The length of the valid UTF-8 character at s, of n bytes left, or 0.
static int utf8Len(const unsigned char* s, size_t n) {
	unsigned char c = s[0];
	int len;
	unsigned int cp;
	if (c < 0x80) return 1;
	if (c >= 0xC2 && c <= 0xDF) len = 2, cp = c & 0x1F;
	else if (c >= 0xE0 && c <= 0xEF) len = 3, cp = c & 0x0F;
	else if (c >= 0xF0 && c <= 0xF4) len = 4, cp = c & 0x07;
	else return 0;
	if ((size_t)len > n) return 0;
	for (int i = 1; i < len; i++) {
		if ((s[i] & 0xC0) != 0x80) return 0;
		cp = cp << 6 | (s[i] & 0x3F);
	}
	if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
	// C1 controls
	if (cp >= 0x80 && cp <= 0x9F) return -len;
	return len;
}

int MN_textSet(MNText* text, const char* raw, size_t size) {
	MN_textFree(text);
	char* out = malloc(size * 4 + 1);
	if (!out) return -1;
	const unsigned char* s = (const unsigned char*)raw;
	size_t i = 0, o = 0;
	if (size >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) i = 3;
	while (i < size) {
		unsigned char c = s[i];
		if (c == '\r') {
			out[o++] = '\n';
			i += i + 1 < size && s[i + 1] == '\n' ? 2 : 1;
		}
		else if (c == '\n') {
			out[o++] = '\n';
			i++;
		}
		else if (c == '\t') {
			memcpy(out + o, "    ", 4);
			o += 4;
			i++;
		}
		else if (c < 0x20 || c == 0x7F) {
			i++;
		}
		else {
			int len = utf8Len(s + i, size - i);
			if (len > 0) {
				memcpy(out + o, s + i, len);
				o += len;
				i += len;
			}
			else if (len < 0) {
				i += -len;
			}
			else {
				out[o++] = '?';
				i++;
			}
		}
	}
	out[o] = '\0';
	char* fitted = realloc(out, o + 1);
	text->text = fitted ? fitted : out;
	text->size = (int)o;
	return 0;
}

int MN_textLoad(const char* path, MNText* text) {
	MN_textFree(text);
	int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return -1;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return -1;
	}
	size_t want = st.st_size > MN_TEXT_MAX ? MN_TEXT_MAX : (size_t)st.st_size;
	char* raw = malloc(want + 1);
	size_t got = 0;
	while (raw && got < want) {
		ssize_t n = read(fd, raw + got, want - got);
		if (n <= 0) break;
		got += (size_t)n;
	}
	close(fd);
	if (!raw) return -1;
	int rc = MN_textSet(text, raw, got);
	free(raw);
	if (rc == 0) text->truncated = st.st_size > MN_TEXT_MAX;
	return rc;
}

static int addLine(MNText* text, int* cap, int start, int len) {
	if (text->line_count == *cap) {
		int more = *cap ? *cap * 2 : 256;
		MNLine* grown = realloc(text->lines, sizeof(MNLine) * more);
		if (!grown) return -1;
		text->lines = grown;
		*cap = more;
	}
	text->lines[text->line_count++] = (MNLine){ start, len };
	return 0;
}

int MN_wrap(MNText* text, int width, MNCharWidth char_width, void* ctx) {
	free(text->lines);
	text->lines = NULL;
	text->line_count = 0;
	if (!text->text || width <= 0 || !char_width) return -1;
	const char* t = text->text;
	int size = text->size, cap = 0, p = 0;
	while (p < size || (p == size && size == 0)) {
		int end = p;
		while (end < size && t[end] != '\n') end++;
		int pos = p;
		if (pos == end && addLine(text, &cap, pos, 0) != 0) return -1;
		while (pos < end) {
			int start = pos, w = 0, space = -1, i = pos;
			while (i < end) {
				int cl = utf8Len((const unsigned char*)t + i, (size_t)(end - i));
				if (cl <= 0) cl = 1;
				int cw = char_width(t + i, cl, ctx);
				if (w + cw > width && i > start) break;
				if (t[i] == ' ') space = i;
				w += cw;
				i += cl;
			}
			int len;
			if (i >= end) {
				len = end - start;
				pos = end;
			}
			else if (t[i] == ' ') {
				len = i - start;
				pos = i + 1;
			}
			else if (space > start) {
				len = space - start;
				pos = space + 1;
			}
			else {
				len = i - start;
				pos = i;
			}
			if (addLine(text, &cap, start, len) != 0) return -1;
		}
		if (size == 0) break;
		p = end + 1;
	}
	return text->line_count;
}

int MN_lastTop(int line_count, int rows) {
	return line_count > rows ? line_count - rows : 0;
}

int MN_scroll(int top, int delta, int line_count, int rows) {
	long long t = (long long)top + delta;
	int last = MN_lastTop(line_count, rows);
	if (t < 0) return 0;
	return t > last ? last : (int)t;
}

///////////////////////////////////////
// a page on screen

int MN_fit(int w, int h, int box_w, int box_h, int* fw, int* fh) {
	if (w < 1 || h < 1 || box_w < 1 || box_h < 1) return -1;
	if ((long long)w * box_h >= (long long)h * box_w) {
		*fw = box_w;
		*fh = (int)(((long long)h * box_w * 2 + w) / ((long long)w * 2));
	}
	else {
		*fh = box_h;
		*fw = (int)(((long long)w * box_h * 2 + h) / ((long long)h * 2));
	}
	if (*fw < 1) *fw = 1;
	if (*fh < 1) *fh = 1;
	return 0;
}

static void placeAxis(int vx, int vw, int size, int pan, int* src_x, int* src_w, int* dst_x, int* dst_w) {
	if (size <= vw) {
		*src_x = 0;
		*src_w = size;
		*dst_x = vx + (vw - size) / 2;
		*dst_w = size;
		return;
	}
	if (pan < 0) pan = 0;
	if (pan > size - vw) pan = size - vw;
	*src_x = pan;
	*src_w = vw;
	*dst_x = vx;
	*dst_w = vw;
}

void MN_place(const MNRect* view, int w, int h, int pan_x, int pan_y, MNRect* src, MNRect* dst) {
	placeAxis(view->x, view->w, w, pan_x, &src->x, &src->w, &dst->x, &dst->w);
	placeAxis(view->y, view->h, h, pan_y, &src->y, &src->h, &dst->y, &dst->h);
}

static int emptyRect(const MNRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

MNRect MN_hole(const MNRect* a, const MNRect* b) {
	MNRect none = { 0, 0, 0, 0 };
	if (emptyRect(a)) return emptyRect(b) ? none : *b;
	if (emptyRect(b)) return *a;
	int x1 = a->x < b->x ? a->x : b->x, y1 = a->y < b->y ? a->y : b->y;
	int x2 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
	int y2 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
	return (MNRect){ x1, y1, x2 - x1, y2 - y1 };
}

int MN_avoid(const MNRect* r, const MNRect* hole, MNRect out[4]) {
	if (emptyRect(r)) return 0;
	int x1 = r->x, y1 = r->y, x2 = r->x + r->w, y2 = r->y + r->h;
	if (emptyRect(hole) || hole->x >= x2 || hole->y >= y2 || hole->x + hole->w <= x1 || hole->y + hole->h <= y1) {
		out[0] = *r;
		return 1;
	}
	int hx1 = hole->x > x1 ? hole->x : x1, hy1 = hole->y > y1 ? hole->y : y1;
	int hx2 = hole->x + hole->w < x2 ? hole->x + hole->w : x2;
	int hy2 = hole->y + hole->h < y2 ? hole->y + hole->h : y2;
	int n = 0;
	if (hy1 > y1) out[n++] = (MNRect){ x1, y1, r->w, hy1 - y1 };
	if (hx1 > x1) out[n++] = (MNRect){ x1, hy1, hx1 - x1, hy2 - hy1 };
	if (hx2 < x2) out[n++] = (MNRect){ hx2, hy1, x2 - hx2, hy2 - hy1 };
	if (hy2 < y2) out[n++] = (MNRect){ x1, hy2, r->w, y2 - hy2 };
	return n;
}

static int clampAxis(int pan, int size, int view) {
	int most = size > view ? size - view : 0;
	return pan < 0 ? 0 : pan > most ? most : pan;
}

void MN_panClamp(const MNRect* view, int w, int h, int* pan_x, int* pan_y) {
	*pan_x = clampAxis(*pan_x, w, view->w);
	*pan_y = clampAxis(*pan_y, h, view->h);
}

static int zoomAxis(int view, int size0, int pan0, int size1) {
	if (size0 <= 0) return 0;
	long long centre0 = size0 <= view ? size0 / 2 : (long long)clampAxis(pan0, size0, view) + view / 2;
	long long centre1 = centre0 * size1 / size0;
	return clampAxis((int)(centre1 - view / 2), size1, view);
}

void MN_panZoom(const MNRect* view, int w0, int h0, int pan_x0, int pan_y0, int w1, int h1, int* pan_x, int* pan_y) {
	*pan_x = zoomAxis(view->w, w0, pan_x0, w1);
	*pan_y = zoomAxis(view->h, h0, pan_y0, h1);
}

///////////////////////////////////////
// the last page read

void MN_memoryInit(MNMemory* memory) {
	memory->items = NULL;
	memory->count = 0;
}

void MN_memoryFree(MNMemory* memory) {
	free(memory->items);
	MN_memoryInit(memory);
}

static int ensure(MNMemory* memory) {
	if (!memory->items) memory->items = calloc(MN_MEMORY_MAX, sizeof(MNMemoryItem));
	return memory->items ? 0 : -1;
}

static int findKey(const MNMemory* memory, const char* key) {
	for (int i = 0; i < memory->count; i++) {
		if (!strcmp(memory->items[i].key, key)) return i;
	}
	return -1;
}

static int keyOk(const char* key) {
	if (!key || !key[0] || strlen(key) >= MN_PATH_MAX) return 0;
	for (const char* p = key; *p; p++) {
		if ((unsigned char)*p < 0x20 || *p == 0x7F) return 0;
	}
	return 1;
}

int MN_memoryLoad(MNMemory* memory, const char* file) {
	MN_memoryFree(memory);
	FILE* f = fopen(file, "r");
	if (!f) return 0;
	if (ensure(memory) != 0) {
		fclose(f);
		return 0;
	}
	char line[MN_PATH_MAX + 32];
	while (memory->count < MN_MEMORY_MAX && fgets(line, sizeof(line), f)) {
		size_t n = strlen(line);
		if (n && line[n - 1] != '\n' && !feof(f)) {
			// too long: skip the rest of it
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n') {}
			continue;
		}
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
		char* tab = strchr(line, '\t');
		if (!tab || tab == line || tab - line > 5) continue;
		int page = 0, ok = 1;
		for (char* p = line; p < tab; p++) {
			if (!isdigit((unsigned char)*p)) ok = 0;
			else page = page * 10 + (*p - '0');
		}
		const char* key = tab + 1;
		if (!ok || !keyOk(key) || findKey(memory, key) >= 0) continue;
		MNMemoryItem* item = &memory->items[memory->count++];
		copy(item->key, sizeof(item->key), key);
		item->page = page;
	}
	fclose(f);
	return memory->count;
}

int MN_memorySave(const MNMemory* memory, const char* file) {
	char tmp[MN_PATH_MAX + 8];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", file) >= (int)sizeof(tmp)) return -1;
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	int ok = 1;
	for (int i = 0; i < memory->count; i++) {
		if (fprintf(f, "%i\t%s\n", memory->items[i].page, memory->items[i].key) < 0) ok = 0;
	}
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, file) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

int MN_memoryGet(const MNMemory* memory, const char* key) {
	int i = key ? findKey(memory, key) : -1;
	return i < 0 ? 0 : memory->items[i].page;
}

int MN_memorySet(MNMemory* memory, const char* key, int page) {
	if (!keyOk(key) || page < 0 || page > 99999 || ensure(memory) != 0) return -1;
	int at = findKey(memory, key);
	if (at == 0 && memory->items[0].page == page) return 0;
	if (at < 0) {
		at = memory->count < MN_MEMORY_MAX ? memory->count++ : MN_MEMORY_MAX - 1;
	}
	memmove(&memory->items[1], &memory->items[0], sizeof(MNMemoryItem) * at);
	copy(memory->items[0].key, sizeof(memory->items[0].key), key);
	memory->items[0].page = page;
	return 1;
}

void MN_memoryKey(const char* sdcard, const char* path, char* out, size_t size) {
	size_t n = sdcard ? strlen(sdcard) : 0;
	if (n && !strncmp(path, sdcard, n) && path[n] == '/') path += n;
	snprintf(out, size, "%s", path);
}
