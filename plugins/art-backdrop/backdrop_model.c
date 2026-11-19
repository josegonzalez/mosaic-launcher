#include <stdlib.h>
#include <string.h>
#include "backdrop_model.h"

int BD_fill(const char* value) {
	return value && !strcmp(value, "full") ? BD_FILL_FULL : BD_FILL_TOP;
}

const char* const BD_layouts[BD_LAYOUT_COUNT] = {
	"layout-fancy", "layout-grid", "layout-carousel", "layout-shelf", "layout-hero-rail",
};

int BD_side(const char* layout, const char* fancy_position) {
	if (!layout) return BD_SIDE_LEFT;
	if (!strcmp(layout, "layout-fancy")) {
		if (fancy_position && !strcmp(fancy_position, "left")) return BD_SIDE_RIGHT;
		if (fancy_position && !strcmp(fancy_position, "top")) return BD_SIDE_BOTTOM;
		return BD_SIDE_LEFT;
	}
	if (!strcmp(layout, "layout-hero-rail")) return BD_SIDE_BOTTOM;
	if (!strcmp(layout, "layout-grid") || !strcmp(layout, "layout-carousel") || !strcmp(layout, "layout-shelf")) {
		return BD_SIDE_ALL;
	}
	return BD_SIDE_LEFT;
}

const char* BD_sideName(int side) {
	switch (side) {
	case BD_SIDE_RIGHT: return "right";
	case BD_SIDE_TOP: return "top";
	case BD_SIDE_BOTTOM: return "bottom";
	case BD_SIDE_ALL: return "whole screen";
	default: return "left";
	}
}

// the length across the screen from the list's side, and how many bands
// fade after the held third
static int shadeLength(int w, int h, int side) {
	if (w <= 0 || h <= 0) return 0;
	return side == BD_SIDE_TOP || side == BD_SIDE_BOTTOM ? h : w;
}

static int shadeFades(int length) {
	int fade = length - length / 3;
	if (fade <= 0) return 0;
	int n = fade / BD_MIN_BAND;
	if (n < 1) n = 1;
	if (n > BD_MAX_BANDS) n = BD_MAX_BANDS;
	return n;
}

int BD_shades(int w, int h, int side) {
	int length = shadeLength(w, h, side);
	if (length <= 0) return 0;
	if (side == BD_SIDE_ALL) return 1;
	return (length / 3 > 0 ? 1 : 0) + shadeFades(length);
}

int BD_shade(int w, int h, int side, int i, MosaicRect* rect, int* alpha) {
	if (i < 0 || i >= BD_shades(w, h, side)) return -1;
	if (side == BD_SIDE_ALL) {
		*rect = (MosaicRect){ 0, 0, w, h };
		*alpha = BD_SHADE_MAX;
		return 0;
	}
	int length = shadeLength(w, h, side);
	int hold = length / 3;
	int from = 0, size = hold;
	*alpha = BD_SHADE_MAX;
	if (hold <= 0 || i > 0) {
		if (hold > 0) i--;
		int n = shadeFades(length);
		int fade = length - hold;
		from = hold + (int)((long)fade * i / n);
		size = hold + (int)((long)fade * (i + 1) / n) - from;
		// at the band's middle: the max nearest the list, 0 at the far side
		*alpha = (BD_SHADE_MAX * (2 * (n - i) - 1) + n) / (2 * n);
	}
	switch (side) {
	case BD_SIDE_RIGHT: *rect = (MosaicRect){ w - from - size, 0, size, h }; break;
	case BD_SIDE_TOP: *rect = (MosaicRect){ 0, from, w, size }; break;
	case BD_SIDE_BOTTOM: *rect = (MosaicRect){ 0, h - from - size, w, size }; break;
	default: *rect = (MosaicRect){ from, 0, size, h }; break;
	}
	return 0;
}

int BD_style(const char* value) {
	return value && !strcmp(value, "full") ? BD_STYLE_FULL : BD_STYLE_GRADIENT;
}

int BD_dim(const char* value) {
	int percent = BD_DIM_DEFAULT;
	if (value && *value) {
		char* end = NULL;
		long n = strtol(value, &end, 10);
		if (*end == '\0' && n >= 0 && n <= 100) percent = (int)n;
	}
	return (percent * 255 + 50) / 100;
}

const char* BD_themed(const char* setting, const char* theme_value) {
	if (setting && setting[0] && strcmp(setting, "theme") != 0) return setting;
	return theme_value;
}

int BD_height(const char* value) {
	if (value && !strcmp(value, "half")) return 2;
	if (value && !strcmp(value, "third")) return 3;
	return 1;
}

MosaicRect BD_area(int screen_w, int screen_h, int divisor) {
	if (divisor < 1) divisor = 1;
	MosaicRect r = { 0, 0, screen_w > 0 ? screen_w : 0, screen_h > 0 ? screen_h / divisor : 0 };
	return r;
}

int BD_kinds(int screenshot, int thumbnail, int box, int kinds[BD_MAX_KINDS]) {
	int n = 0;
	kinds[n++] = MOSAIC_ART_BACKDROP;
	if (screenshot) kinds[n++] = MOSAIC_ART_SCREENSHOT;
	if (thumbnail) kinds[n++] = MOSAIC_ART_THUMBNAIL;
	if (box) kinds[n++] = MOSAIC_ART_BOX;
	return n;
}

int BD_cover(int src_w, int src_h, int dst_w, int dst_h, MosaicRect* crop) {
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

// how many bands the fading half of an area h tall gets
static int fadeBands(int h) {
	int fade = h - h / 2;
	if (fade <= 0) return 0;
	int n = fade / BD_MIN_BAND;
	if (n < 1) n = 1;
	if (n > BD_MAX_BANDS) n = BD_MAX_BANDS;
	return n;
}

int BD_bands(int h) {
	if (h <= 0) return 0;
	return (h / 2 > 0 ? 1 : 0) + fadeBands(h);
}

int BD_band(int h, int i, int* y, int* band_h, int* alpha) {
	int top = h > 0 ? h / 2 : 0;
	if (i < 0 || i >= BD_bands(h)) return -1;
	if (top > 0) {
		if (i == 0) {
			*y = 0;
			*band_h = top;
			*alpha = 255;
			return 0;
		}
		i--;
	}
	int n = fadeBands(h);
	int fade = h - top;
	int from = top + (int)((long)fade * i / n);
	int to = top + (int)((long)fade * (i + 1) / n);
	*y = from;
	*band_h = to - from;
	// at the band's middle: 255 at the top of the fade, 0 at the bottom
	*alpha = (255 * (2 * (n - i) - 1) + n) / (2 * n);
	return 0;
}

MosaicRect BD_srcBand(const MosaicRect* crop, int h, int y, int band_h) {
	if (h <= 0) return (MosaicRect){ crop->x, crop->y, crop->w, 0 };
	int from = (int)((long)crop->h * y / h);
	int to = (int)((long)crop->h * (y + band_h) / h);
	if (to <= from) to = from + 1;
	if (to > crop->h) to = crop->h;
	if (from > to - 1) from = to - 1;
	if (from < 0) from = 0;
	return (MosaicRect){ crop->x, crop->y + from, crop->w, to - from };
}

int BD_mulAlpha(int a, int b) {
	return (a * b + 127) / 255;
}
