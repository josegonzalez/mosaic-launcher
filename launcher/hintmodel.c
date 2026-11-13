#include <string.h>
#include "hintmodel.h"

static const struct {
	int button;
	const char* glyph;
} glyphs[] = {
	{ MOSAIC_BUTTON_A, "A" },
	{ MOSAIC_BUTTON_B, "B" },
	{ MOSAIC_BUTTON_X, "X" },
	{ MOSAIC_BUTTON_Y, "Y" },
	{ MOSAIC_BUTTON_L1, "L1" },
	{ MOSAIC_BUTTON_R1, "R1" },
	{ MOSAIC_BUTTON_L2, "L2" },
	{ MOSAIC_BUTTON_R2, "R2" },
	{ MOSAIC_BUTTON_START, "START" },
	{ MOSAIC_BUTTON_SELECT, "SELECT" },
	{ MOSAIC_BUTTON_MENU, "MENU" },
	{ MOSAIC_BUTTON_POWER, "POWER" },
};

// The d-pad's names, in the order Hint_dpad numbers them. Stock draws none
// of them, so its own hints never match one.
static const struct {
	int button;
	const char* glyph;
} dpad[] = {
	{ MOSAIC_BUTTON_UP, "UP" },
	{ MOSAIC_BUTTON_DOWN, "DOWN" },
	{ MOSAIC_BUTTON_LEFT, "LEFT" },
	{ MOSAIC_BUTTON_RIGHT, "RIGHT" },
	{ MOSAIC_GLYPH_UP_DOWN, "U/D" },
	{ MOSAIC_GLYPH_LEFT_RIGHT, "L/R" },
	{ MOSAIC_GLYPH_DPAD, "DPAD" },
};

const char* Hint_glyph(int button, const char* const* fn_names) {
	for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++) {
		if (glyphs[i].button == button) return glyphs[i].glyph;
	}
	for (size_t i = 0; i < sizeof(dpad) / sizeof(dpad[0]); i++) {
		if (dpad[i].button == button) return dpad[i].glyph;
	}
	if (fn_names && button >= MOSAIC_BUTTON_FN1 && button <= MOSAIC_BUTTON_FN3) {
		const char* name = fn_names[button - MOSAIC_BUTTON_FN1];
		return name && name[0] ? name : NULL;
	}
	return NULL;
}

int Hint_dpad(const char* glyph) {
	for (size_t i = 0; glyph && i < sizeof(dpad) / sizeof(dpad[0]); i++) {
		if (!strcmp(dpad[i].glyph, glyph)) return (int)i;
	}
	return -1;
}

int Hints_check(int group, const MosaicHint* hints, int count, const char* const* fn_names, const char** names) {
	if (group != MOSAIC_HINTS_LEFT && group != MOSAIC_HINTS_RIGHT) return -1;
	if (!hints || count < 1 || count > HINTS_MAX || !names) return -1;
	for (int i = 0; i < count; i++) {
		names[i] = Hint_glyph(hints[i].button, fn_names);
		if (!names[i] || !hints[i].label || !hints[i].label[0] || hints[i].label[0] == '\n') return -1;
	}
	return 0;
}

int Hints_primary(int group, int count) {
	return group == MOSAIC_HINTS_RIGHT && count > 0 ? count - 1 : 0;
}

int Hints_label(const char* text, char* out, size_t size) {
	if (!text || !out || size < 2) return -1;
	size_t len = strcspn(text, "\n");
	if (len >= size) {
		len = size - 1;
		// back off a UTF-8 character cut in half
		while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) len--;
	}
	memcpy(out, text, len);
	out[len] = '\0';
	return len ? (int)len : -1;
}

int Hints_place(int group, int screen_w, int screen_h, const HintMetrics* m, const int* widths, int count, MosaicRect* out) {
	if (!m || !widths || !out || count < 1 || count > HINTS_MAX || screen_w <= 0 || screen_h <= 0) return -1;
	if (group != MOSAIC_HINTS_LEFT && group != MOSAIC_HINTS_RIGHT) return -1;
	// as GFX_blitButtonGroup: a margin before each button and one after the last
	int w = m->margin;
	for (int i = 0; i < count; i++) {
		if (widths[i] < 0) return -1;
		w += m->margin + widths[i];
	}
	int x = group == MOSAIC_HINTS_RIGHT ? screen_w - m->padding - w : m->padding;
	*out = (MosaicRect){ x, screen_h - (m->padding + m->pill), w, m->pill };
	return 0;
}

static int overlaps(const MosaicRect* a, const MosaicRect* b) {
	if (!b || b->w <= 0 || b->h <= 0) return 0;
	return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

int Hints_clear(const MosaicRect* pill, int screen_w, int screen_h, const MosaicRect* avoid, int avoid_count) {
	if (!pill || pill->w <= 0 || pill->h <= 0 || avoid_count < 0 || (avoid_count && !avoid)) return 0;
	if (pill->x < 0 || pill->y < 0 || pill->x + pill->w > screen_w || pill->y + pill->h > screen_h) return 0;
	for (int i = 0; i < avoid_count; i++) {
		if (overlaps(pill, &avoid[i])) return 0;
	}
	return 1;
}
