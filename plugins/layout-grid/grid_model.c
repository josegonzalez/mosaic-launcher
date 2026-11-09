#include <stdlib.h>
#include <string.h>
#include "grid_model.h"

int LG_count(const char* value, int min, int max) {
	if (!value || !*value) return 0;
	char* end;
	long n = strtol(value, &end, 10);
	if (*end || n < min || n > max) return 0;
	return (int)n;
}

int LG_autoColumns(int width, int scale) {
	if (scale < 1) scale = 1;
	int cols = width / scale / 80;
	if (cols < 2) cols = 2;
	if (cols > LG_MAX_COLUMNS) cols = LG_MAX_COLUMNS;
	return cols;
}

int LG_autoRows(int w, int h, int cols) {
	if (w <= 0 || h <= 0 || cols < 1) return 1;
	// h / (w / cols) square cells, rounded up from 0.4, so a grid a little
	// short of two square rows still gets two
	int rows = (int)(((long)h * cols * 10 + (long)w * 6) / ((long)w * 10));
	if (rows < 1) rows = 1;
	if (rows > LG_MAX_ROWS) rows = LG_MAX_ROWS;
	return rows;
}

static int bottom(const MosaicRect* r) {
	return r && r->w > 0 && r->h > 0 ? r->y + r->h : 0;
}

int LG_grid(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int cols,
		int rows, int min_w, int min_h, LGGrid* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	int gap = 4 * scale;
	int top = area->y + gap;
	if (bottom(status) + gap > top) top = bottom(status) + gap;
	if (bottom(header) + gap > top) top = bottom(header) + gap;
	MosaicRect room = { area->x + gap, top, area->w - 2 * gap, area->y + area->h - gap - top };
	if (room.w <= 0 || room.h <= 0) return -1;

	if (cols < 1) cols = LG_autoColumns(room.w, scale);
	if (rows < 1) rows = LG_autoRows(room.w, room.h, cols);
	while (rows > 1 && room.h / rows < min_h) rows--;
	while (cols > 1 && room.w / cols < min_w) cols--;
	int cell_w = room.w / cols, cell_h = room.h / rows;
	if (cell_w < 1 || cell_h < 1 || cell_w < min_w || cell_h < min_h) return -1;

	out->cols = cols;
	out->rows = rows;
	out->cell_w = cell_w;
	out->cell_h = cell_h;
	int w = cell_w * cols, h = cell_h * rows;
	out->grid = (MosaicRect){ room.x + (room.w - w) / 2, room.y + (room.h - h) / 2, w, h };
	return 0;
}

MosaicRect LG_cell(const LGGrid* grid, int slot) {
	if (grid->cols < 1 || slot < 0) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ grid->grid.x + (slot % grid->cols) * grid->cell_w, grid->grid.y + (slot / grid->cols) * grid->cell_h,
		grid->cell_w, grid->cell_h };
}

static MosaicRect inset(const MosaicRect* r, int by) {
	MosaicRect out = { r->x + by, r->y + by, r->w - 2 * by, r->h - 2 * by };
	if (out.w < 0) out.w = 0;
	if (out.h < 0) out.h = 0;
	return out;
}

void LG_parts(const MosaicRect* cell, int gap, int frame, int name_h, MosaicRect* highlight, MosaicRect* tile,
		MosaicRect* art, MosaicRect* name) {
	*highlight = inset(cell, gap);
	*tile = inset(highlight, frame);
	if (name_h < 0) name_h = 0;
	if (name_h > tile->h) name_h = tile->h;
	*art = (MosaicRect){ tile->x, tile->y, tile->w, tile->h - name_h };
	*name = (MosaicRect){ tile->x, tile->y + tile->h - name_h, tile->w, name_h };
}

int LG_window(int first, int selected, int count, int cols, int rows) {
	if (count <= 0 || cols < 1 || rows < 1) return 0;
	int total = (count + cols - 1) / cols;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int row = selected / cols;
	if (first > row) first = row;
	if (row >= first + rows) first = row - rows + 1;
	int last_first = total > rows ? total - rows : 0;
	if (first > last_first) first = last_first;
	if (first < 0) first = 0;
	return first;
}

int LG_move(int selected, int count, int cols, int dir, int fresh) {
	if (count <= 1 || cols < 1 || selected < 0 || selected >= count) return selected;
	int col = selected % cols;
	int last_row = (count - 1) / cols;
	switch (dir) {
	case MOSAIC_BUTTON_LEFT:
		if (selected > 0) return selected - 1;
		return fresh ? count - 1 : selected;
	case MOSAIC_BUTTON_RIGHT:
		if (selected < count - 1) return selected + 1;
		return fresh ? 0 : selected;
	case MOSAIC_BUTTON_UP: {
		if (selected >= cols) return selected - cols;
		if (!fresh) return selected;
		int target = last_row * cols + col;
		return target < count ? target : target - cols;
	}
	case MOSAIC_BUTTON_DOWN:
		if (selected + cols < count) return selected + cols;
		if (selected / cols < last_row) return count - 1;
		return fresh ? col : selected;
	}
	return selected;
}

static int utf8Length(unsigned char c) {
	if (c >= 0xF0) return 4;
	if (c >= 0xE0) return 3;
	if (c >= 0xC0) return 2;
	return 1;
}

static void copyChar(const char* s, char* out, size_t size) {
	size_t n = (size_t)utf8Length((unsigned char)*s);
	for (size_t i = 0; i < n; i++) {
		// a sequence cut short by the end of the name is only its lead byte
		if (!s[i] || (i > 0 && ((unsigned char)s[i] & 0xC0) != 0x80)) {
			n = i;
			break;
		}
	}
	if (n + 1 > size) n = 0;
	memcpy(out, s, n);
	out[n] = '\0';
}

void LG_letter(const char* name, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!name) return;
	for (const char* s = name; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c >= 0x80 && c < 0xC0) continue; // a sequence's continuation byte
		if (c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
			copyChar(s, out, size);
			if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
			return;
		}
	}
	for (const char* s = name; *s; s++) {
		if (*s != ' ' && *s != '\t') {
			copyChar(s, out, size);
			return;
		}
	}
}

int LG_showName(int show_names, int hidden, int drew_art) {
	return show_names && !(hidden && drew_art);
}
