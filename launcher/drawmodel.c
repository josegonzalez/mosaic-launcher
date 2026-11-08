#include <stdlib.h>
#include <string.h>
#include "drawmodel.h"

///////////////////////////////////////
// surfaces and owners

typedef struct Recorded {
	const void* surface;
	char owner[64];
	size_t bytes;
	int read_only;
} Recorded;

static Recorded* recorded = NULL;
static int recorded_count = 0;
static int recorded_cap = 0;

static int find(const void* surface) {
	for (int i = 0; i < recorded_count; i++) {
		if (recorded[i].surface == surface) return i;
	}
	return -1;
}

size_t DrawReg_bytes(const char* owner) {
	size_t total = 0;
	if (!owner) return 0;
	for (int i = 0; i < recorded_count; i++) {
		if (!strcmp(recorded[i].owner, owner)) total += recorded[i].bytes;
	}
	return total;
}

int DrawReg_fits(const char* owner, size_t bytes) {
	if (!owner) return 0;
	if (!strcmp(owner, DRAW_HOST)) return 1;
	size_t used = DrawReg_bytes(owner);
	return bytes <= DRAW_PLUGIN_BYTES && used <= DRAW_PLUGIN_BYTES - bytes;
}

int DrawReg_add(const void* surface, const char* owner, size_t bytes) {
	if (!surface || !owner || strlen(owner) >= sizeof(recorded[0].owner)) return -1;
	if (find(surface) >= 0) return -1;
	if (!strcmp(owner, DRAW_HOST)) bytes = 0;
	else if (!DrawReg_fits(owner, bytes)) return -1;
	if (recorded_count == recorded_cap) {
		int cap = recorded_cap ? recorded_cap * 2 : 16;
		Recorded* grown = realloc(recorded, cap * sizeof(Recorded));
		if (!grown) return -1;
		recorded = grown;
		recorded_cap = cap;
	}
	Recorded* r = &recorded[recorded_count++];
	r->surface = surface;
	strcpy(r->owner, owner);
	r->bytes = bytes;
	r->read_only = 0;
	return 0;
}

int DrawReg_lock(const void* surface) {
	int i = surface ? find(surface) : -1;
	if (i < 0 || strcmp(recorded[i].owner, DRAW_HOST) != 0) return -1;
	recorded[i].read_only = 1;
	return 0;
}

int DrawReg_writable(const void* surface) {
	int i = surface ? find(surface) : -1;
	return i >= 0 && !recorded[i].read_only;
}

const char* DrawReg_owner(const void* surface) {
	int i = surface ? find(surface) : -1;
	return i >= 0 ? recorded[i].owner : NULL;
}

static void drop(int i) {
	recorded[i] = recorded[--recorded_count];
}

int DrawReg_remove(const void* surface, const char* owner) {
	int i = surface && owner ? find(surface) : -1;
	if (i < 0 || strcmp(recorded[i].owner, owner) != 0) return -1;
	drop(i);
	return 0;
}

int DrawReg_take(const char* owner, void** out, int max) {
	int taken = 0;
	if (!owner || !out) return 0;
	for (int i = recorded_count - 1; i >= 0 && taken < max; i--) {
		if (strcmp(recorded[i].owner, owner) != 0) continue;
		out[taken++] = (void*)recorded[i].surface;
		drop(i);
	}
	return taken;
}

///////////////////////////////////////
// text

#define ELLIPSIS "..."
#define ELLIPSIS_LEN 3

static int isContinuation(char c) {
	return ((unsigned char)c & 0xC0) == 0x80;
}

// out holds the first len bytes of text, plus "..." when dots is set.
static int measurePrefix(const char* text, size_t len, int dots, DrawMeasure measure, void* data, char* out) {
	memcpy(out, text, len);
	if (dots) memcpy(out + len, ELLIPSIS, ELLIPSIS_LEN);
	out[len + (dots ? ELLIPSIS_LEN : 0)] = '\0';
	return measure(out, data);
}

int Draw_truncate(const char* text, int max_width, DrawMeasure measure, void* data, char* out, size_t size) {
	if (!text || !measure || !out || size == 0 || max_width < 0) return -1;
	size_t line = strcspn(text, "\n");

	if (line < size) {
		int width = measurePrefix(text, line, 0, measure, data, out);
		if (width < 0) return -1;
		if (width <= max_width) return width;
	}

	out[0] = '\0';
	if (size <= ELLIPSIS_LEN) return 0;
	size_t most = size - ELLIPSIS_LEN - 1;
	if (most > line) most = line;

	// the cut points from 0 to most, between characters
	size_t* cuts = malloc((most + 1) * sizeof(size_t));
	if (!cuts) return -1;
	int count = 0;
	for (size_t i = 0; i <= most; i++) {
		if (i == line || !isContinuation(text[i])) cuts[count++] = i;
	}

	// the longest prefix that fits with its "...", assuming a longer
	// prefix is never narrower
	int width = measurePrefix(text, 0, 1, measure, data, out);
	if (width < 0 || width > max_width) {
		free(cuts);
		out[0] = '\0';
		return width < 0 ? -1 : 0;
	}
	int lo = 0, hi = count - 1;
	while (lo < hi) {
		int mid = (lo + hi + 1) / 2;
		int w = measurePrefix(text, cuts[mid], 1, measure, data, out);
		if (w < 0) {
			free(cuts);
			out[0] = '\0';
			return -1;
		}
		if (w <= max_width) lo = mid;
		else hi = mid - 1;
	}
	width = measurePrefix(text, cuts[lo], 1, measure, data, out);
	free(cuts);
	if (width < 0) out[0] = '\0';
	return width;
}

///////////////////////////////////////
// shapes

int Draw_clampRadius(int radius, int w, int h) {
	if (radius < 0) return -1;
	int most = (w < h ? w : h) / 2;
	if (most < 0) most = 0;
	return radius < most ? radius : most;
}

int Draw_roundInset(int radius, int h, int row) {
	if (radius <= 0 || row < 0 || row >= h) return 0;
	// in half pixels, from the corner circle's centre to the row's middle
	long dy;
	if (row < radius) dy = 2L * (radius - row) - 1;
	else if (row >= h - radius) dy = 2L * (row - (h - radius)) + 1;
	else return 0;
	long r2 = 4L * radius * radius;
	for (int i = 0; i < radius; i++) {
		long dx = 2L * (radius - i) - 1;
		if (dx * dx + dy * dy <= r2) return i;
	}
	return radius;
}

int Draw_fit(int sw, int sh, int dw, int dh, int fit, MosaicRect* src, MosaicRect* dst) {
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !src || !dst) return -1;
	*src = (MosaicRect){ 0, 0, sw, sh };
	*dst = (MosaicRect){ 0, 0, dw, dh };
	long long wide = (long long)sw * dh; // > tall when the source is wider in shape
	long long tall = (long long)dw * sh;
	switch (fit) {
		case MOSAIC_FIT_STRETCH:
			return 0;
		case MOSAIC_FIT_CONTAIN:
			if (wide > tall) {
				dst->h = (int)(tall / sw);
				if (dst->h < 1) dst->h = 1;
				dst->y = (dh - dst->h) / 2;
			}
			else if (wide < tall) {
				dst->w = (int)(wide / sh);
				if (dst->w < 1) dst->w = 1;
				dst->x = (dw - dst->w) / 2;
			}
			return 0;
		case MOSAIC_FIT_COVER:
			if (wide > tall) {
				src->w = (int)(tall / dh);
				if (src->w < 1) src->w = 1;
				src->x = (sw - src->w) / 2;
			}
			else if (wide < tall) {
				src->h = (int)(wide / dw);
				if (src->h < 1) src->h = 1;
				src->y = (sh - src->h) / 2;
			}
			return 0;
		default:
			return -1;
	}
}
