#include <stdlib.h>
#include <string.h>
#include "launch_model.h"

int LA_pick(const MosaicRect* pill, const MosaicRect* row, MosaicRect* out) {
	if (pill && pill->w > 0 && pill->h > 0) *out = *pill;
	else if (row && row->w > 0 && row->h > 0) *out = *row;
	else return 0;
	return 1;
}

int LA_progress(float progress) {
	if (!(progress > 0.0f)) return 0;
	if (progress >= 1.0f) return LA_UNIT;
	int p = (int)(progress * LA_UNIT + 0.5f);
	return p > LA_UNIT ? LA_UNIT : p;
}

// how far the glide has got at p, from 0 to LA_UNIT
static long glided(int p) {
	if (p <= 0) return 0;
	if (p >= LA_GLIDE_END) return LA_UNIT;
	return (long)p * LA_UNIT / LA_GLIDE_END;
}

MosaicRect LA_glide(const MosaicRect* from, int screen_w, int screen_h, int p) {
	long q = glided(p);
	long to_x = (screen_w - from->w) / 2;
	long to_y = (screen_h - from->h) / 2;
	MosaicRect at = *from;
	at.x = (int)(from->x + (to_x - from->x) * q / LA_UNIT);
	at.y = (int)(from->y + (to_y - from->y) * q / LA_UNIT);
	return at;
}

int LA_dim(int p) {
	return (int)(255 * (LA_UNIT - glided(p)) / LA_UNIT);
}

int LA_pillAlpha(int p) {
	if (p <= LA_GLIDE_END) return 255;
	if (p >= LA_UNIT) return 0;
	return 255 * (LA_UNIT - p) / (LA_UNIT - LA_GLIDE_END);
}

int LA_fade(int p) {
	if (p <= 0) return 255;
	if (p >= LA_UNIT) return 0;
	return 255 * (LA_UNIT - p) / LA_UNIT;
}

int LA_duration(const char* value, int max) {
	if (!value || !*value) return LA_DURATION;
	char* end = NULL;
	long ms = strtol(value, &end, 10);
	if (*end || ms < 0) return LA_DURATION;
	if (max > 0 && ms > max) return max;
	return (int)ms;
}

int LA_ease(const char* value) {
	if (!value) return MOSAIC_EASE_OUT;
	if (!strcmp(value, "linear")) return MOSAIC_EASE_LINEAR;
	if (!strcmp(value, "in")) return MOSAIC_EASE_IN;
	if (!strcmp(value, "in-out")) return MOSAIC_EASE_IN_OUT;
	return MOSAIC_EASE_OUT;
}
