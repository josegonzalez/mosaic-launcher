#include <stdlib.h>
#include <string.h>
#include "hero_model.h"

int HR_cardPercent(const char* value) {
	if (value && !strcmp(value, "small")) return 22;
	if (value && !strcmp(value, "large")) return 34;
	return 28;
}

// a whole number from min to max, or fallback
static int number(const char* value, int min, int max, int fallback) {
	if (!value || !*value) return fallback;
	char* end = NULL;
	long n = strtol(value, &end, 10);
	if (*end || n < min || n > max) return fallback;
	return (int)n;
}

int HR_tint(const char* value) {
	return (number(value, 0, 100, HR_TINT_DEFAULT) * 255 + 50) / 100;
}

int HR_fadeMs(const char* value) {
	return number(value, 0, 2000, HR_FADE_DEFAULT);
}

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

// the smallest rect around both, either of which can be empty
static MosaicRect around(const MosaicRect* a, const MosaicRect* b) {
	if (empty(a)) return empty(b) ? (MosaicRect){ 0, 0, 0, 0 } : *b;
	if (empty(b)) return *a;
	int x0 = a->x < b->x ? a->x : b->x;
	int y0 = a->y < b->y ? a->y : b->y;
	int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
	int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
}

int HR_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int percent,
		int name_h, HRLayout* out) {
	memset(out, 0, sizeof(*out));
	if (scale < 1) scale = 1;
	if (name_h < 0) name_h = 0;
	if (percent < 1) percent = 28;
	int gap = 4 * scale;
	int card_h = area->h * percent / 100;
	int card_w = card_h * 3 / 4;
	if (card_h < 16 * scale || card_w + 2 * gap > area->w) return -1;
	int raise = card_h / 8;
	int frame = 2 * scale;
	int card_y = area->y + area->h - gap - name_h - card_h;
	int raised_top = card_y - raise - frame;
	if (raised_top <= area->y + gap) return -1;

	int cards = (area->w - gap) / (card_w + gap);
	if (cards < 1) cards = 1;
	if (cards > HR_MAX_CARDS) cards = HR_MAX_CARDS;
	int total = cards * card_w + (cards - 1) * gap;

	out->gap = gap;
	out->frame = frame;
	out->raise = raise;
	out->name_h = name_h;
	out->card_w = card_w;
	out->card_h = card_h;
	out->card_y = card_y;
	out->cards = cards;
	out->card_x = area->x + (area->w - total) / 2;
	out->hole = around(status, header);

	out->hero = (MosaicRect){ area->x, area->y, area->w, card_y + card_h / 2 - area->y };
	out->fade_y = raised_top - card_h / 2 - area->y;
	if (out->fade_y < 0) out->fade_y = 0;
	int hero_bottom = out->hero.y + out->hero.h;
	out->below = (MosaicRect){ area->x, hero_bottom, area->w, area->y + area->h - hero_bottom };

	// the logo: two fifths of the width from the first card's left, up to
	// two fifths of the hero tall, sitting a gap above the raised card and
	// kept below the pill where they'd meet
	MosaicRect logo = { out->card_x, 0, area->w * 2 / 5, 0 };
	int top = area->y + gap;
	const MosaicRect* hole = &out->hole;
	if (!empty(hole) && hole->x < logo.x + logo.w && logo.x < hole->x + hole->w && hole->y + hole->h + gap > top) {
		top = hole->y + hole->h + gap;
	}
	int bottom = raised_top - gap;
	logo.h = bottom - top;
	if (logo.h > out->hero.h * 2 / 5) logo.h = out->hero.h * 2 / 5;
	if (logo.h < 0) logo.h = 0;
	logo.y = bottom - logo.h;
	out->logo = logo;
	return 0;
}

MosaicRect HR_card(const HRLayout* layout, int slot, int raised) {
	if (slot < 0 || slot >= layout->cards) return (MosaicRect){ 0, 0, 0, 0 };
	return (MosaicRect){ layout->card_x + slot * (layout->card_w + layout->gap),
		layout->card_y - (raised ? layout->raise : 0), layout->card_w, layout->card_h };
}

MosaicRect HR_nameStrip(const HRLayout* layout, const MosaicRect* card) {
	return (MosaicRect){ card->x, card->y + card->h, card->w, layout->name_h };
}

int HR_window(int first, int selected, int count, int cards) {
	if (count <= 0 || cards < 1) return 0;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count - 1;
	int margin = cards >= 3 ? 1 : 0;
	if (selected - margin < first) first = selected - margin;
	if (selected + margin >= first + cards) first = selected + margin - cards + 1;
	int last_first = count > cards ? count - cards : 0;
	if (first > last_first) first = last_first;
	if (first < 0) first = 0;
	return first;
}

int HR_move(int selected, int count, int cards, int dir, int fresh) {
	if (count <= 1 || selected < 0 || selected >= count) return selected;
	if (cards < 1) cards = 1;
	int last = count - 1;
	switch (dir) {
	case MOSAIC_BUTTON_LEFT:
		if (selected > 0) return selected - 1;
		return fresh ? last : selected;
	case MOSAIC_BUTTON_RIGHT:
		if (selected < last) return selected + 1;
		return fresh ? 0 : selected;
	case MOSAIC_BUTTON_UP:
		if (selected > 0) return selected > cards ? selected - cards : 0;
		return fresh ? last : selected;
	case MOSAIC_BUTTON_DOWN:
		if (selected < last) return selected + cards < last ? selected + cards : last;
		return fresh ? 0 : selected;
	}
	return selected;
}

int HR_fade(long elapsed, int duration) {
	if (duration <= 0 || elapsed >= duration) return 255;
	if (elapsed <= 0) return 0;
	// 1 - (1 - t)^2
	long long left = duration - elapsed;
	long long d = duration;
	return 255 - (int)((255 * left * left + d * d / 2) / (d * d));
}

int HR_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]) {
	if (empty(r)) return 0;
	int x0 = hole ? hole->x : 0, y0 = hole ? hole->y : 0;
	int x1 = hole ? hole->x + hole->w : 0, y1 = hole ? hole->y + hole->h : 0;
	if (x0 < r->x) x0 = r->x;
	if (y0 < r->y) y0 = r->y;
	if (x1 > r->x + r->w) x1 = r->x + r->w;
	if (y1 > r->y + r->h) y1 = r->y + r->h;
	if (empty(hole) || x1 <= x0 || y1 <= y0) {
		out[0] = *r;
		return 1;
	}
	int n = 0;
	if (y0 > r->y) out[n++] = (MosaicRect){ r->x, r->y, r->w, y0 - r->y };
	if (x0 > r->x) out[n++] = (MosaicRect){ r->x, y0, x0 - r->x, y1 - y0 };
	if (x1 < r->x + r->w) out[n++] = (MosaicRect){ x1, y0, r->x + r->w - x1, y1 - y0 };
	if (y1 < r->y + r->h) out[n++] = (MosaicRect){ r->x, y1, r->w, r->y + r->h - y1 };
	return n;
}

int HR_cover(int src_w, int src_h, int dst_w, int dst_h, MosaicRect* crop) {
	if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return -1;
	*crop = (MosaicRect){ 0, 0, src_w, src_h };
	if ((long)src_w * dst_h > (long)src_h * dst_w) {
		// wider than the area: the sides go
		crop->w = (int)(((long)src_h * dst_w + dst_h / 2) / dst_h);
		if (crop->w < 1) crop->w = 1;
		crop->x = (src_w - crop->w) / 2;
	} else {
		crop->h = (int)(((long)src_w * dst_h + dst_w / 2) / dst_w);
		if (crop->h < 1) crop->h = 1;
		crop->y = (src_h - crop->h) / 2;
	}
	return 0;
}

MosaicRect HR_fit(int src_w, int src_h, const MosaicRect* box) {
	if (src_w <= 0 || src_h <= 0 || empty(box)) return (MosaicRect){ box ? box->x : 0, box ? box->y : 0, 0, 0 };
	int w = box->w, h = box->h;
	if ((long)src_w * box->h > (long)src_h * box->w) {
		h = (int)((long)box->w * src_h / src_w);
		if (h < 1) h = 1;
	} else {
		w = (int)((long)box->h * src_w / src_h);
		if (w < 1) w = 1;
	}
	return (MosaicRect){ box->x, box->y + box->h - h, w, h };
}

int HR_bands(int fade_y, int h) {
	if (fade_y < 0) fade_y = 0;
	int fade = h - fade_y;
	if (fade <= 0) return 0;
	int n = fade / 2;
	if (n < 1) n = 1;
	if (n > HR_MAX_BANDS) n = HR_MAX_BANDS;
	return n;
}

int HR_band(int fade_y, int h, int i, int* y, int* band_h, int* alpha) {
	if (fade_y < 0) fade_y = 0;
	int n = HR_bands(fade_y, h);
	if (i < 0 || i >= n) return -1;
	int fade = h - fade_y;
	int from = fade_y + (int)((long)fade * i / n);
	int to = fade_y + (int)((long)fade * (i + 1) / n);
	*y = from;
	*band_h = to - from;
	// at the band's middle: near 0 at the top of the fade, near 255 at the bottom
	*alpha = (255 * (2 * i + 1) + n) / (2 * n);
	return 0;
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

void HR_letter(const char* name, char* out, size_t size) {
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
