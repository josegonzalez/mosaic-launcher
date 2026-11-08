#include <string.h>
#include "carousel_model.h"

static int bottom(const MosaicRect* r) {
	return r && r->w > 0 && r->h > 0 ? r->y + r->h : 0;
}

int LC_strip(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int name_h,
		int min_size, LCStrip* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	if (name_h < 0) name_h = 0;
	if (min_size < 1) min_size = 1;
	int gap = 4 * scale;
	int top = area->y + gap;
	if (bottom(status) + gap > top) top = bottom(status) + gap;
	if (bottom(header) + gap > top) top = bottom(header) + gap;
	MosaicRect room = { area->x + gap, top, area->w - 2 * gap, area->y + area->h - gap - top };
	if (room.w <= 0 || room.h <= 0) return -1;

	int below = name_h > 0 ? gap + name_h : 0;
	int size = room.h - below;
	if (size > room.w * 3 / 10) size = room.w * 3 / 10;
	if (size < min_size) return -1;

	out->room = room;
	out->gap = gap;
	out->size[0] = size;
	out->cx = room.x + room.w / 2;
	int total = size + below;
	out->cy = room.y + (room.h - total) / 2 + size / 2;
	if (name_h > 0) out->name = (MosaicRect){ room.x, out->cy - size / 2 + size + gap, room.w, name_h };

	for (int d = 1; d <= LC_MAX_SIDES; d++) {
		int s = out->size[d - 1] * 3 / 5;
		int c = out->centre[d - 1] + out->size[d - 1] / 2 + gap + s / 2;
		// whole, both sides: the strip is centred, so the right side says
		if (s < min_size || out->cx + c - s / 2 + s > room.x + room.w || out->cx - c - s / 2 < room.x) break;
		out->size[d] = s;
		out->centre[d] = c;
		out->sides = d;
	}
	return 0;
}

// the size and middle of the cover k steps out, past the last side too
static void at(const LCStrip* strip, int k, int* size, int* centre) {
	if (k <= strip->sides) {
		*size = strip->size[k];
		*centre = strip->centre[k];
		return;
	}
	int s = strip->size[strip->sides];
	*size = s;
	*centre = strip->centre[strip->sides] + (k - strip->sides) * (s + strip->gap);
}

MosaicRect LC_slot(const LCStrip* strip, int pos) {
	if (strip->size[0] <= 0) return (MosaicRect){ 0, 0, 0, 0 };
	int sign = pos < 0 ? -1 : 1;
	long a = pos < 0 ? -(long)pos : pos;
	int k = (int)(a / LC_UNIT), f = (int)(a % LC_UNIT);
	if (k > 1000) k = 1000; // far off the screen anyway
	int s0, c0, s1, c1;
	at(strip, k, &s0, &c0);
	at(strip, k + 1, &s1, &c1);
	int size = s0 + (int)((long)(s1 - s0) * f / LC_UNIT);
	int centre = c0 + (int)((long)(c1 - c0) * f / LC_UNIT);
	return (MosaicRect){ strip->cx + sign * centre - size / 2, strip->cy - size / 2, size, size };
}

int LC_move(int selected, int count, int dir, int fresh, int page) {
	if (count <= 1 || selected < 0 || selected >= count) return selected;
	if (page < 1) page = 1;
	int by;
	switch (dir) {
	case MOSAIC_BUTTON_LEFT: by = -1; break;
	case MOSAIC_BUTTON_RIGHT: by = 1; break;
	case MOSAIC_BUTTON_UP: by = -page; break;
	case MOSAIC_BUTTON_DOWN: by = page; break;
	default: return selected;
	}
	if (by < 0) {
		if (selected > 0) return selected + by > 0 ? selected + by : 0;
		return fresh ? count - 1 : selected;
	}
	if (selected < count - 1) return selected + by < count - 1 ? selected + by : count - 1;
	return fresh ? 0 : selected;
}

int LC_ease(unsigned long elapsed, unsigned long duration) {
	if (!duration || elapsed >= duration) return LC_UNIT;
	long left = LC_UNIT - (long)(elapsed * LC_UNIT / duration);
	return (int)(LC_UNIT - left * left / LC_UNIT);
}

int LC_slideFrom(int shift, int delta, int sides) {
	long from = (long)shift + (long)delta * LC_UNIT;
	long most = (long)sides * LC_UNIT;
	if (from > most || from < -most) return 0;
	return (int)from;
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

void LC_letter(const char* name, char* out, size_t size) {
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

MosaicRect LC_artAbove(const MosaicRect* cover, const MosaicRect* name) {
	MosaicRect r = *cover;
	int bottom = name->y + name->h;
	if (name->h > 0 && name->y >= cover->y + cover->h) r.h = bottom - cover->y;
	return r;
}
