#include "input_model.h"

#include <stdio.h>
#include <string.h>

#define GRID_W 32
#define GRID_H 16

int It_shown(int button, unsigned int fn) {
	if (button < 0 || button >= IT_BUTTONS) return 0;
	if (button >= MOSAIC_BUTTON_FN1) return (fn >> (button - MOSAIC_BUTTON_FN1)) & 1;
	return 1;
}

const char* It_name(int button) {
	static const char* names[IT_BUTTONS] = {
		[MOSAIC_BUTTON_A] = "A",
		[MOSAIC_BUTTON_B] = "B",
		[MOSAIC_BUTTON_X] = "X",
		[MOSAIC_BUTTON_Y] = "Y",
		[MOSAIC_BUTTON_L1] = "L1",
		[MOSAIC_BUTTON_R1] = "R1",
		[MOSAIC_BUTTON_L2] = "L2",
		[MOSAIC_BUTTON_R2] = "R2",
		[MOSAIC_BUTTON_START] = "START",
		[MOSAIC_BUTTON_SELECT] = "SELECT",
		[MOSAIC_BUTTON_UP] = "UP",
		[MOSAIC_BUTTON_DOWN] = "DOWN",
		[MOSAIC_BUTTON_LEFT] = "LEFT",
		[MOSAIC_BUTTON_RIGHT] = "RIGHT",
		[MOSAIC_BUTTON_MENU] = "MENU",
		[MOSAIC_BUTTON_POWER] = "POWER",
	};
	if (button < 0 || button >= IT_BUTTONS || !names[button]) return "";
	return names[button];
}

const char* It_label(int button) {
	switch (button) {
		case MOSAIC_BUTTON_UP: return "U";
		case MOSAIC_BUTTON_DOWN: return "D";
		case MOSAIC_BUTTON_LEFT: return "L";
		case MOSAIC_BUTTON_RIGHT: return "R";
		default: return It_name(button);
	}
}

void It_historyInit(ItHistory* h) {
	memset(h, 0, sizeof(*h));
}

int It_historyPress(ItHistory* h, unsigned int pressed) {
	int added = 0;
	for (int b = 0; b < IT_BUTTONS; b++) {
		if (!(pressed & MOSAIC_BUTTON_BIT(b))) continue;
		int keep = h->count < IT_HISTORY ? h->count : IT_HISTORY - 1;
		memmove(&h->buttons[1], &h->buttons[0], (size_t)keep * sizeof(h->buttons[0]));
		h->buttons[0] = b;
		h->count = keep + 1;
		added++;
	}
	return added;
}

void It_holdInit(ItHold* h) {
	memset(h, 0, sizeof(*h));
}

int It_holdStep(ItHold* h, unsigned int held, unsigned long now) {
	unsigned int both = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_START);
	if ((held & both) != both) {
		h->holding = 0;
		return 0;
	}
	if (!h->holding) {
		h->holding = 1;
		h->since = now;
	}
	return now - h->since >= IT_EXIT_MS;
}

int It_holdPercent(const ItHold* h, unsigned long now) {
	if (!h->holding || now < h->since) return 0;
	unsigned long d = now - h->since;
	return d >= IT_EXIT_MS ? 100 : (int)(d * 100 / IT_EXIT_MS);
}

int It_stickOffset(int raw, int radius) {
	if (radius <= 0) return 0;
	long long v = (long long)raw * radius / IT_STICK_MAX;
	if (v > radius) v = radius;
	if (v < -radius) v = -radius;
	return (int)v;
}

void It_formatStick(int x, int y, char* out, size_t size) {
	snprintf(out, size, "%i, %i", x, y);
}

static MosaicRect cell(int ox, int oy, int u, int x, int y, int w, int h) {
	return (MosaicRect){ ox + x * u, oy + y * u, w * u, h * u };
}

int It_layout(const MosaicRect* area, unsigned int fn, unsigned int sticks, ItLayout* out) {
	memset(out, 0, sizeof(*out));
	int u = area->w / GRID_W < area->h / GRID_H ? area->w / GRID_W : area->h / GRID_H;
	if (u < 2) return -1;
	int ox = area->x + (area->w - GRID_W * u) / 2, oy = area->y + (area->h - GRID_H * u) / 2;
	out->unit = u;
	static const struct {
		int button, x, y, w, h;
	} places[] = {
		{ MOSAIC_BUTTON_L2, 1, 0, 6, 2 },
		{ MOSAIC_BUTTON_L1, 1, 3, 6, 2 },
		{ MOSAIC_BUTTON_R2, 25, 0, 6, 2 },
		{ MOSAIC_BUTTON_R1, 25, 3, 6, 2 },
		{ MOSAIC_BUTTON_MENU, 10, 0, 5, 2 },
		{ MOSAIC_BUTTON_POWER, 17, 0, 5, 2 },
		{ MOSAIC_BUTTON_SELECT, 10, 3, 5, 2 },
		{ MOSAIC_BUTTON_START, 17, 3, 5, 2 },
		{ MOSAIC_BUTTON_UP, 4, 6, 3, 3 },
		{ MOSAIC_BUTTON_LEFT, 1, 9, 3, 3 },
		{ MOSAIC_BUTTON_RIGHT, 7, 9, 3, 3 },
		{ MOSAIC_BUTTON_DOWN, 4, 12, 3, 3 },
		{ MOSAIC_BUTTON_X, 25, 6, 3, 3 },
		{ MOSAIC_BUTTON_Y, 22, 9, 3, 3 },
		{ MOSAIC_BUTTON_A, 28, 9, 3, 3 },
		{ MOSAIC_BUTTON_B, 25, 12, 3, 3 },
	};
	for (size_t i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
		out->buttons[places[i].button] = cell(ox, oy, u, places[i].x, places[i].y, places[i].w, places[i].h);
	}
	// the FN buttons the device has, side by side under SELECT and START
	int count = 0;
	for (int b = MOSAIC_BUTTON_FN1; b <= MOSAIC_BUTTON_FN3; b++) count += It_shown(b, fn);
	int x = 16 - (count * 4 + (count - 1)) / 2;
	for (int b = MOSAIC_BUTTON_FN1; b <= MOSAIC_BUTTON_FN3 && count; b++) {
		if (!It_shown(b, fn)) continue;
		out->buttons[b] = cell(ox, oy, u, x, 6, 4, 2);
		x += 5;
	}
	for (int s = 0; s < 2; s++) {
		if (!((sticks >> s) & 1)) continue;
		out->sticks[s] = cell(ox, oy, u, s ? 17 : 10, 9, 5, 5);
		out->stick_text[s] = cell(ox, oy, u, s ? 16 : 7, 14, 9, 2);
	}
	return 0;
}
