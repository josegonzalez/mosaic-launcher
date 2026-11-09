#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "shot_model.h"

// stock's sizes, at scale 1
#define PADDING 10
#define PILL_SIZE 30
#define BUTTON_PADDING 12

static const char* extensionOf(const char* file) {
	const char* dot = strrchr(file, '.');
	if (!dot || dot == file) return NULL;
	if (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return dot;
	return NULL;
}

int SV_isImage(const char* file) {
	if (!file || !file[0] || file[0] == '.' || strchr(file, '/')) return 0;
	return extensionOf(file) != NULL;
}

// n digits at s as a number, or -1
static int digits(const char* s, int n) {
	int value = 0;
	for (int i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9') return -1;
		value = value * 10 + (s[i] - '0');
	}
	return value;
}

static long long keyFrom(int year, int month, int day, int hour, int minute, int second) {
	return (((((long long)year * 100 + month) * 100 + day) * 100 + hour) * 100 + minute) * 100 + second;
}

long long SV_keyOf(const struct tm* tm) {
	return keyFrom(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int SV_parse(const char* file, SVShot* out) {
	memset(out, 0, sizeof(*out));
	if (!SV_isImage(file)) return -1;
	snprintf(out->file, sizeof(out->file), "%s", file);
	size_t stem = (size_t)(extensionOf(file) - file);
	if (stem >= sizeof(out->game)) stem = sizeof(out->game) - 1;
	memcpy(out->game, file, stem);
	out->game[stem] = '\0';

	// .YYYY-MM-DD-HH-MM-SS at the end of the stem, with a game before it
	enum { STAMP = 20 };
	if (stem <= STAMP) return 0;
	const char* s = out->game + stem - STAMP;
	if (s[0] != '.' || s[5] != '-' || s[8] != '-' || s[11] != '-' || s[14] != '-' || s[17] != '-') return 0;
	int year = digits(s + 1, 4), month = digits(s + 6, 2), day = digits(s + 9, 2);
	int hour = digits(s + 12, 2), minute = digits(s + 15, 2), second = digits(s + 18, 2);
	if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0
			|| minute > 59 || second < 0 || second > 59) {
		return 0;
	}
	out->game[stem - STAMP] = '\0';
	out->has_date = 1;
	out->year = year;
	out->month = month;
	out->day = day;
	out->hour = hour;
	out->minute = minute;
	out->second = second;
	out->key = keyFrom(year, month, day, hour, minute, second);
	return 0;
}

void SV_date(const SVShot* shot, char* out, size_t size) {
	if (!shot->has_date) {
		if (size) out[0] = '\0';
		return;
	}
	snprintf(out, size, "%04d-%02d-%02d %02d:%02d", shot->year, shot->month, shot->day, shot->hour, shot->minute);
}

void SV_title(const SVShot* shot, char* out, size_t size) {
	char date[32];
	SV_date(shot, date, sizeof(date));
	if (date[0]) snprintf(out, size, "%s \xc2\xb7 %s", shot->game, date);
	else snprintf(out, size, "%s", shot->game);
}

///////////////////////////////////////
// the folder

void SV_listInit(SVList* list) {
	memset(list, 0, sizeof(*list));
}

void SV_listFree(SVList* list) {
	free(list->shots);
	SV_listInit(list);
}

int SV_listAdd(SVList* list, const SVShot* shot) {
	if (list->count >= SV_MAX_SHOTS) return -1;
	if (list->count == list->cap) {
		int cap = list->cap ? list->cap * 2 : 32;
		if (cap > SV_MAX_SHOTS) cap = SV_MAX_SHOTS;
		SVShot* grown = realloc(list->shots, (size_t)cap * sizeof(SVShot));
		if (!grown) return -1;
		list->shots = grown;
		list->cap = cap;
	}
	list->shots[list->count++] = *shot;
	return 0;
}

// newest first, then by file name, last first
static int compareShots(const void* a, const void* b) {
	const SVShot* x = a;
	const SVShot* y = b;
	if (x->key != y->key) return x->key > y->key ? -1 : 1;
	return -strcmp(x->file, y->file);
}

void SV_sort(SVList* list) {
	if (list->count > 1) qsort(list->shots, (size_t)list->count, sizeof(SVShot), compareShots);
}

// 1 for a real folder at path, not a link
static int isFolder(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// fills shot from name in dir when it's a screenshot: 0, or -1
static int shotAt(const char* dir, const char* name, SVShot* shot) {
	if (SV_parse(name, shot) != 0) return -1;
	char path[SV_PATH_MAX];
	if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return -1;
	struct stat st;
	if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
	if (!shot->has_date) {
		struct tm tm;
		time_t when = st.st_mtime;
		if (localtime_r(&when, &tm)) shot->key = SV_keyOf(&tm);
	}
	return 0;
}

int SV_scan(const char* dir, SVList* list) {
	list->count = 0;
	if (!isFolder(dir)) return -1;
	DIR* d = opendir(dir);
	if (!d) return -1;
	struct dirent* e;
	while ((e = readdir(d))) {
		SVShot shot;
		if (shotAt(dir, e->d_name, &shot) != 0) continue;
		if (SV_listAdd(list, &shot) == 0) continue;
		// full: keep the newest, putting this one over the oldest
		int oldest = -1;
		for (int i = 0; i < list->count; i++) {
			if (oldest < 0 || compareShots(&list->shots[i], &list->shots[oldest]) > 0) oldest = i;
		}
		if (oldest >= 0 && compareShots(&shot, &list->shots[oldest]) < 0) list->shots[oldest] = shot;
	}
	closedir(d);
	SV_sort(list);
	return list->count;
}

int SV_any(const char* dir) {
	if (!isFolder(dir)) return 0;
	DIR* d = opendir(dir);
	if (!d) return 0;
	int found = 0;
	struct dirent* e;
	while (!found && (e = readdir(d))) {
		SVShot shot;
		found = shotAt(dir, e->d_name, &shot) == 0;
	}
	closedir(d);
	return found;
}

int SV_remove(const char* dir, SVList* list, int index) {
	if (index < 0 || index >= list->count || !isFolder(dir)) return -1;
	const char* name = list->shots[index].file;
	SVShot check;
	if (shotAt(dir, name, &check) != 0) return -1;
	char path[SV_PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (unlink(path) != 0) return -1;
	memmove(&list->shots[index], &list->shots[index + 1], (size_t)(list->count - index - 1) * sizeof(SVShot));
	list->count--;
	return 0;
}

///////////////////////////////////////
// moving

int SV_step(int selected, int count, int by, int fresh) {
	if (count <= 1 || selected < 0 || selected >= count || by == 0) return selected;
	if (by < 0) {
		if (selected > 0) return selected + by > 0 ? selected + by : 0;
		return fresh ? count - 1 : selected;
	}
	if (selected < count - 1) return selected + by < count - 1 ? selected + by : count - 1;
	return fresh ? 0 : selected;
}

int SV_row(int selected, int count, int cols, int dy) {
	if (count <= 0 || cols <= 0 || selected < 0 || selected >= count) return selected;
	if (dy < 0) return selected - cols >= 0 ? selected - cols : selected;
	if (dy > 0) {
		if (selected / cols >= (count - 1) / cols) return selected;
		return selected + cols < count ? selected + cols : count - 1;
	}
	return selected;
}

int SV_afterRemove(int selected, int count) {
	if (count <= 0 || selected < 0) return 0;
	return selected < count ? selected : count - 1;
}

int SV_firstRow(int first, int selected, int count, int cols, int rows) {
	if (count <= 0 || cols <= 0 || rows <= 0) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int row = selected / cols;
	if (row < first) first = row;
	if (row >= first + rows) first = row - rows + 1;
	int total = (count + cols - 1) / cols;
	if (first > total - rows) first = total - rows;
	return first < 0 ? 0 : first;
}

void SV_hints(int count, int full, int can_delete, MosaicHint left[2], int* left_count, MosaicHint right[2],
		int* right_count) {
	*left_count = 0;
	*right_count = 0;
	if (count <= 0) {
		right[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
		*right_count = 1;
		return;
	}
	left[0] = (MosaicHint){ MOSAIC_BUTTON_B, "BACK" };
	*left_count = 1;
	if (can_delete) right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_Y, "DELETE" };
	if (!full) right[(*right_count)++] = (MosaicHint){ MOSAIC_BUTTON_A, "VIEW" };
}

///////////////////////////////////////
// layout

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

int SV_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]) {
	if (empty(r)) return 0;
	int x1 = r->x, y1 = r->y, x2 = r->x + r->w, y2 = r->y + r->h;
	if (empty(hole) || hole->x >= x2 || hole->y >= y2 || hole->x + hole->w <= x1 || hole->y + hole->h <= y1) {
		out[0] = *r;
		return 1;
	}
	int hx1 = hole->x > x1 ? hole->x : x1, hy1 = hole->y > y1 ? hole->y : y1;
	int hx2 = hole->x + hole->w < x2 ? hole->x + hole->w : x2, hy2 = hole->y + hole->h < y2 ? hole->y + hole->h : y2;
	int n = 0;
	if (hy1 > y1) out[n++] = (MosaicRect){ x1, y1, r->w, hy1 - y1 };
	if (hx1 > x1) out[n++] = (MosaicRect){ x1, hy1, hx1 - x1, hy2 - hy1 };
	if (hx2 < x2) out[n++] = (MosaicRect){ hx2, hy1, x2 - hx2, hy2 - hy1 };
	if (hy2 < y2) out[n++] = (MosaicRect){ x1, hy2, r->w, y2 - hy2 };
	return n;
}

MosaicRect SV_hole(const MosaicRect* status, const MosaicRect* header) {
	MosaicRect none = { 0, 0, 0, 0 };
	int has_status = !empty(status), has_header = !empty(header);
	if (!has_status && !has_header) return none;
	if (!has_header) return *status;
	if (!has_status) return *header;
	int x1 = status->x < header->x ? status->x : header->x;
	int y1 = status->y < header->y ? status->y : header->y;
	int x2 = status->x + status->w > header->x + header->w ? status->x + status->w : header->x + header->w;
	int y2 = status->y + status->h > header->y + header->h ? status->y + status->h : header->y + header->h;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

MosaicRect SV_fit(int src_w, int src_h, const MosaicRect* box) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (src_w <= 0 || src_h <= 0 || empty(box)) return none;
	int w = box->w, h = (int)((long long)src_h * box->w / src_w);
	if (h > box->h) {
		h = box->h;
		w = (int)((long long)src_w * box->h / src_h);
	}
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	return (MosaicRect){ box->x + (box->w - w) / 2, box->y + (box->h - h) / 2, w, h };
}

MosaicRect SV_source(const MosaicRect* dst, const MosaicRect* part, int src_w, int src_h) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(dst) || empty(part)) return none;
	int x1 = (int)((long long)(part->x - dst->x) * src_w / dst->w);
	int y1 = (int)((long long)(part->y - dst->y) * src_h / dst->h);
	int x2 = (int)((long long)(part->x + part->w - dst->x) * src_w / dst->w);
	int y2 = (int)((long long)(part->y + part->h - dst->y) * src_h / dst->h);
	if (x2 <= x1) x2 = x1 + 1;
	if (y2 <= y1) y2 = y1 + 1;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

MosaicRect SV_titlePill(const MosaicRect* area, const MosaicRect* hole, int scale, int text_w) {
	MosaicRect none = { 0, 0, 0, 0 };
	int pad = PADDING * scale, inner = BUTTON_PADDING * scale;
	int x = area->x + pad, y = area->y + pad, h = PILL_SIZE * scale;
	int right = area->x + area->w - pad;
	if (!empty(hole) && hole->y < y + h && hole->y + hole->h > y && hole->x - pad < right) right = hole->x - pad;
	int w = text_w + 2 * inner;
	if (w > right - x) w = right - x;
	if (text_w <= 0 || w <= 2 * inner) return none;
	return (MosaicRect){ x, y, w, h };
}

int SV_grid(const MosaicRect* area, const MosaicRect* hole, int scale, int aspect_w, int aspect_h, int name_h,
		SVGrid* out) {
	memset(out, 0, sizeof(*out));
	if (empty(area) || scale < 1 || aspect_w <= 0 || aspect_h <= 0 || name_h < 0) return -1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int top = area->y + pad + pill + pad;
	if (!empty(hole) && hole->y + hole->h + pad > top) top = hole->y + hole->h + pad;
	int bottom = area->y + area->h - pad - pill - pad;
	MosaicRect room = { area->x + pad, top, area->w - 2 * pad, bottom - top };
	if (empty(&room)) return -1;
	out->gap = pad;
	out->name_h = name_h;
	// the fewest columns from 3 that fit two rows
	for (int cols = 3; cols <= SV_MAX_COLS; cols++) {
		int cell_w = (room.w - pad * (cols - 1)) / cols;
		if (cell_w <= 0) break;
		int thumb_h = (int)((long long)cell_w * aspect_h / aspect_w);
		int rows = (room.h + pad) / (thumb_h + name_h + pad);
		out->cols = cols;
		out->cell_w = cell_w;
		out->thumb_h = thumb_h;
		out->rows = rows;
		if (rows >= 2) break;
	}
	if (out->cols <= 0 || out->cell_w <= 0) return -1;
	if (out->rows < 1) {
		// one row, the thumbnails as tall as it leaves
		out->rows = 1;
		out->thumb_h = room.h - name_h;
		if (out->thumb_h <= 0) return -1;
	}
	out->cell_h = out->thumb_h + name_h;
	// centred in the room
	int used_w = out->cols * out->cell_w + (out->cols - 1) * pad;
	int used_h = out->rows * out->cell_h + (out->rows - 1) * pad;
	out->room = (MosaicRect){ room.x + (room.w - used_w) / 2, room.y + (room.h - used_h) / 2, used_w, used_h };
	return 0;
}

MosaicRect SV_cell(const SVGrid* grid, int slot) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (grid->cols <= 0 || slot < 0 || slot >= grid->cols * grid->rows) return none;
	int col = slot % grid->cols, row = slot / grid->cols;
	return (MosaicRect){ grid->room.x + col * (grid->cell_w + grid->gap), grid->room.y + row * (grid->cell_h + grid->gap),
		grid->cell_w, grid->cell_h };
}

///////////////////////////////////////
// scaled copies

SVCopy* SV_copyFind(SVCopy* copies, int count, const char* file, int w, int h) {
	for (int i = 0; i < count; i++) {
		SVCopy* c = &copies[i];
		if (c->surface && c->w == w && c->h == h && strcmp(c->file, file) == 0) return c;
	}
	return NULL;
}

SVCopy* SV_copyPick(SVCopy* copies, int count) {
	SVCopy* pick = NULL;
	for (int i = 0; i < count; i++) {
		SVCopy* c = &copies[i];
		if (!c->surface) return c;
		if (!pick || c->used < pick->used) pick = c;
	}
	return pick;
}
