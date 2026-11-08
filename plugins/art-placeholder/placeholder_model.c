#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "placeholder_model.h"
#include "mosaic_plugin.h"

int PA_style(const char* value) {
	return value && !strcmp(value, "pattern") ? PA_STYLE_PATTERN : PA_STYLE_TITLE;
}

int PA_size(int kind, int screen_w, int screen_h, int* w, int* h) {
	// as big as the built-in list shows a thumbnail at stock's default
	// game art width: 45% of the screen's width and 60% of its height
	int side = screen_w * 45 / 100;
	if (screen_h * 60 / 100 < side) side = screen_h * 60 / 100;
	if (side < 16) return 0;
	if (kind == MOSAIC_ART_THUMBNAIL) {
		*w = side;
		*h = side;
		return 1;
	}
	if (kind == MOSAIC_ART_BOX) {
		*w = side * 3 / 4;
		*h = side;
		return 1;
	}
	return 0;
}

uint32_t PA_hash(const char* text) {
	uint32_t hash = 2166136261u;
	for (const unsigned char* c = (const unsigned char*)(text ? text : ""); *c; c++) {
		hash ^= *c;
		hash *= 16777619u;
	}
	return hash;
}

uint32_t PA_next(uint32_t* state) {
	uint32_t x = *state ? *state : 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

unsigned int PA_hsv(int hue, int saturation, int value) {
	hue = ((hue % 360) + 360) % 360;
	if (saturation < 0) saturation = 0;
	if (saturation > 100) saturation = 100;
	if (value < 0) value = 0;
	if (value > 100) value = 100;
	int v = value * 255 / 100;
	int c = v * saturation / 100;
	int x = c * (60 - abs(hue % 120 - 60)) / 60;
	int m = v - c;
	int r, g, b;
	switch (hue / 60) {
		case 0: r = c; g = x; b = 0; break;
		case 1: r = x; g = c; b = 0; break;
		case 2: r = 0; g = c; b = x; break;
		case 3: r = 0; g = x; b = c; break;
		case 4: r = x; g = 0; b = c; break;
		default: r = c; g = 0; b = x; break;
	}
	return (unsigned int)((r + m) << 16 | (g + m) << 8 | (b + m));
}

int PA_luma(unsigned int rgb) {
	int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	return (299 * r + 587 * g + 114 * b) / 1000;
}

unsigned int PA_contrast(unsigned int bg, unsigned int a, unsigned int b) {
	int base = PA_luma(bg);
	return abs(PA_luma(a) - base) >= abs(PA_luma(b) - base) ? a : b;
}

unsigned int PA_themeOr(int color, unsigned int fallback) {
	return color < 0 ? fallback : (unsigned int)color & 0xFFFFFF;
}

///////////////////////////////////////
// title cards

// Appends word to line, after a space unless line is empty, cut to fit.
static void append(char* line, const char* word, size_t len) {
	size_t used = strlen(line);
	if (used && used + 1 < PA_LINE_LEN) line[used++] = ' ';
	if (used + len >= PA_LINE_LEN) len = PA_LINE_LEN - 1 - used;
	memcpy(line + used, word, len);
	line[used + len] = '\0';
}

int PA_wrap(const char* name, int max_width, PAWidth width, void* data, char lines[PA_LINES][PA_LINE_LEN]) {
	for (int i = 0; i < PA_LINES; i++) lines[i][0] = '\0';
	int count = 0; // lines finished
	const char* at = name ? name : "";
	for (;;) {
		while (*at == ' ') at++;
		if (!*at) break;
		const char* end = at;
		while (*end && *end != ' ') end++;
		size_t len = (size_t)(end - at);
		char* line = lines[count];
		if (count == PA_LINES - 1 || !line[0]) {
			// the last line takes the rest, and a line's first word always fits
			append(line, at, len);
		}
		else {
			char tried[PA_LINE_LEN];
			snprintf(tried, sizeof(tried), "%s", line);
			append(tried, at, len);
			if (width(tried, data) <= max_width) {
				memcpy(line, tried, sizeof(tried));
			}
			else {
				count++;
				append(lines[count], at, len);
			}
		}
		// a word too wide for its own line ends it
		line = lines[count];
		if (count < PA_LINES - 1 && !strchr(line, ' ') && width(line, data) > max_width) count++;
		at = end;
	}
	return count < PA_LINES && lines[count][0] ? count + 1 : count;
}

void PA_titleLayout(int w, int h, int scale, PATitle* out) {
	int shorter = w < h ? w : h;
	int unit = shorter / 16;
	if (unit < 2 * scale) unit = 2 * scale;
	out->margin = unit;
	out->radius = unit;
	out->padding = unit;
	out->text_w = w - 2 * out->margin - 2 * out->padding;
	if (out->text_w < 1) out->text_w = 1;
}

int PA_textTop(int h, int count, int line_h) {
	return (h - count * line_h) / 2;
}

///////////////////////////////////////
// patterns

void PA_pattern(const char* path, int w, int h, PAPattern* out) {
	memset(out, 0, sizeof(*out));
	uint32_t state = PA_hash(path);
	int hue = (int)(state % 360);
	out->bg = PA_hsv(hue, 45, 20);
	out->colors[0] = PA_hsv(hue, 60, 90);
	// the second colour a third to two thirds of the way round from the first
	out->colors[1] = PA_hsv(hue + 120 + (int)(PA_next(&state) % 121), 55, 80);
	int rows = w > 0 ? (PA_COLS * h + w / 2) / w : 1;
	if (rows < 1) rows = 1;
	if (rows > PA_MAX_ROWS) rows = PA_MAX_ROWS;
	out->rows = rows;
	int any = 0;
	for (int row = 0; row < rows; row++) {
		for (int col = 0; col < (PA_COLS + 1) / 2; col++) {
			// three in eight empty, three the first colour, two the second
			int pick = (int)(PA_next(&state) >> 8) % 8;
			unsigned char tile = pick < 3 ? 0 : pick < 6 ? 1 : 2;
			out->tiles[row][col] = tile;
			out->tiles[row][PA_COLS - 1 - col] = tile;
			any |= tile;
		}
	}
	// never a blank card
	if (!any) {
		out->tiles[rows / 2][PA_COLS / 2 - 1] = 1;
		out->tiles[rows / 2][PA_COLS / 2] = 1;
	}
}

int PA_tile(int w, int h, int rows, int col, int row, int* x, int* y, int* size) {
	if (rows < 1 || col < 0 || col >= PA_COLS || row < 0 || row >= rows) return 0;
	int cell = w / PA_COLS;
	if (h / rows < cell) cell = h / rows;
	int gap = cell / 10;
	if (cell - 2 * gap < 1) return 0;
	*x = (w - cell * PA_COLS) / 2 + col * cell + gap;
	*y = (h - cell * rows) / 2 + row * cell + gap;
	*size = cell - 2 * gap;
	return 1;
}
