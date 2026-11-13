#include <stdlib.h>
#include <string.h>
#include "backdrop_model.h"

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
