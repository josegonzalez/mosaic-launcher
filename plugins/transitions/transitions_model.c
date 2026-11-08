#include <string.h>
#include "transitions_model.h"

int TR_style(const char* value) {
	if (!value) return TR_STYLE_SLIDE;
	if (!strcmp(value, "stock")) return TR_STYLE_STOCK;
	if (!strcmp(value, "fade")) return TR_STYLE_FADE;
	if (!strcmp(value, "slide_fade")) return TR_STYLE_SLIDE_FADE;
	if (!strcmp(value, "off")) return TR_STYLE_OFF;
	return TR_STYLE_SLIDE;
}

int TR_speed(const char* value) {
	if (value && !strcmp(value, "fast")) return TR_FAST_MS;
	if (value && !strcmp(value, "slow")) return TR_SLOW_MS;
	return TR_NORMAL_MS;
}

int TR_ease(const char* value) {
	if (value && !strcmp(value, "linear")) return MOSAIC_EASE_LINEAR;
	if (value && !strcmp(value, "in")) return MOSAIC_EASE_IN;
	if (value && !strcmp(value, "in_out")) return MOSAIC_EASE_IN_OUT;
	return MOSAIC_EASE_OUT;
}

static int drawn(int style) {
	return style == TR_STYLE_SLIDE || style == TR_STYLE_FADE || style == TR_STYLE_SLIDE_FADE;
}

static int menuKind(int kind) {
	return kind == MOSAIC_TRANSITION_PUSH || kind == MOSAIC_TRANSITION_POP || kind == MOSAIC_TRANSITION_SCREEN;
}

int TR_begin(int style, int kind) {
	if (style == TR_STYLE_OFF) return MOSAIC_STYLE_OFF;
	if (drawn(style) && menuKind(kind)) return MOSAIC_STYLE_CUSTOM;
	return MOSAIC_STYLE_STOCK;
}

int TR_duration(int ms, int max_ms) {
	if (ms < 0) ms = 0;
	return max_ms > 0 && ms > max_ms ? max_ms : ms;
}

// progress of n, rounded
static int part(float progress, int n) {
	return (int)(progress * (float)n + 0.5f);
}

static TRLayer layer(int frame, int x, int y, int alpha) {
	TRLayer l = { frame, x, y, alpha };
	return l;
}

// a push, or a pop played backwards with its frames swapped: from is what
// goes and to what comes in
static void push(int style, int from, int to, float progress, int w, TRLayer out[2]) {
	switch (style) {
		case TR_STYLE_SLIDE: {
			int moved = part(progress, w);
			out[0] = layer(from, -moved, 0, 255);
			out[1] = layer(to, w - moved, 0, 255);
			break;
		}
		case TR_STYLE_FADE:
			out[0] = layer(from, 0, 0, 255);
			out[1] = layer(to, 0, 0, part(progress, 255));
			break;
		default: { // slide_fade
			int quarter = w / 4;
			out[0] = layer(from, 0, 0, 255);
			out[1] = layer(to, quarter - part(progress, quarter), 0, part(progress, 255));
			break;
		}
	}
}

int TR_layers(int style, int kind, float progress, int w, int h, TRLayer out[2]) {
	if (!drawn(style) || !menuKind(kind) || w < 1 || h < 1 || !out) return -1;
	if (!(progress > 0.0f)) progress = 0.0f; // NaN too
	if (progress > 1.0f) progress = 1.0f;
	switch (kind) {
		case MOSAIC_TRANSITION_PUSH:
			push(style, TR_OLD, TR_NEW, progress, w, out);
			break;
		case MOSAIC_TRANSITION_POP:
			push(style, TR_NEW, TR_OLD, 1.0f - progress, w, out);
			break;
		default: // the screen: the new frame comes down over the old one
			out[0] = layer(TR_OLD, 0, 0, 255);
			if (style == TR_STYLE_SLIDE) out[1] = layer(TR_NEW, 0, part(progress, h) - h, 255);
			else if (style == TR_STYLE_FADE) out[1] = layer(TR_NEW, 0, 0, part(progress, 255));
			else out[1] = layer(TR_NEW, 0, part(progress, h / 4) - h / 4, part(progress, 255));
			break;
	}
	return 0;
}
