#include <string.h>
#include "blurmodel.h"

static int lesser(int a, int b) { return a < b ? a : b; }
static int greater(int a, int b) { return a > b ? a : b; }
static int up(int a, int b) { return (a + b - 1) / b; }

int Blur_plan(int src_w, int src_h, int w, int h, int radius, int* sw, int* sh, int* r) {
	if (src_w < 1 || src_h < 1 || w < 1 || h < 1 || radius < 1 || radius > BLUR_RADIUS_MAX || !sw || !sh || !r) return -1;
	int factor = greater(up(radius, BLUR_SMALL_RADIUS), up(greater(w, h), BLUR_SMALL_MAX));
	*sw = lesser(up(w, factor), src_w);
	*sh = lesser(up(h, factor), src_h);
	// the radius across the small copy, as far as it is across w
	*r = (int)(((long long)radius * *sw + w / 2) / w);
	if (*r < 1) *r = 1;
	return 0;
}

void Blur_shrinkStart(BlurShrink* s, int w, int h, int sw, int sh, uint64_t* sums, uint32_t* small) {
	*s = (BlurShrink){ w, h, sw, sh, 0, 0, sums, small };
	memset(sums, 0, sizeof(uint64_t) * 4 * sw);
}

// where the image's rows (or columns) for small row i start, of n across size
static int edge(int i, int size, int n) {
	return (int)((long long)i * size / n);
}

int Blur_shrinkRow(BlurShrink* s, const uint32_t* row) {
	if (s->row >= s->h) return -1;
	for (int i = 0; i < s->sw; i++) {
		uint64_t* sum = &s->sums[4 * i];
		int x1 = edge(i + 1, s->w, s->sw);
		for (int x = edge(i, s->w, s->sw); x < x1; x++) {
			uint32_t p = row[x];
			uint32_t a = p >> 24;
			sum[0] += a;
			sum[1] += ((p >> 16) & 0xFF) * a;
			sum[2] += ((p >> 8) & 0xFF) * a;
			sum[3] += (p & 0xFF) * a;
		}
	}
	s->row++;
	int j = s->small_row;
	if (s->row < edge(j + 1, s->h, s->sh)) return 0;

	// the small row's last image row: its averages, premultiplied
	int rows = edge(j + 1, s->h, s->sh) - edge(j, s->h, s->sh);
	uint32_t* out = &s->small[(size_t)j * s->sw];
	for (int i = 0; i < s->sw; i++) {
		uint64_t* sum = &s->sums[4 * i];
		uint64_t n = (uint64_t)rows * (edge(i + 1, s->w, s->sw) - edge(i, s->w, s->sw));
		uint32_t a = (uint32_t)((sum[0] + n / 2) / n);
		uint32_t r = (uint32_t)((sum[1] + n * 255 / 2) / (n * 255));
		uint32_t g = (uint32_t)((sum[2] + n * 255 / 2) / (n * 255));
		uint32_t b = (uint32_t)((sum[3] + n * 255 / 2) / (n * 255));
		out[i] = a << 24 | r << 16 | g << 8 | b;
	}
	memset(s->sums, 0, sizeof(uint64_t) * 4 * s->sw);
	s->small_row++;
	return 0;
}

// one box of radius r along n pixels step apart from px, by way of tmp
static void boxLine(uint32_t* px, int n, int step, int r, uint32_t* tmp) {
	for (int i = 0; i < n; i++) tmp[i] = px[(size_t)i * step];
	uint32_t d = 2 * r + 1;
	uint32_t sum[4] = { 0, 0, 0, 0 };
	for (int k = -r; k <= r; k++) {
		uint32_t p = tmp[lesser(greater(k, 0), n - 1)];
		for (int c = 0; c < 4; c++) sum[c] += (p >> (24 - 8 * c)) & 0xFF;
	}
	for (int i = 0; i < n; i++) {
		uint32_t out = 0;
		for (int c = 0; c < 4; c++) out |= ((sum[c] + d / 2) / d) << (24 - 8 * c);
		px[(size_t)i * step] = out;
		uint32_t in = tmp[lesser(i + r + 1, n - 1)];
		uint32_t gone = tmp[greater(i - r, 0)];
		for (int c = 0; c < 4; c++) sum[c] += ((in >> (24 - 8 * c)) & 0xFF) - ((gone >> (24 - 8 * c)) & 0xFF);
	}
}

void Blur_box(uint32_t* px, int w, int h, int r, uint32_t* tmp) {
	if (r < 1 || w < 1 || h < 1) return;
	for (int y = 0; y < h; y++) boxLine(px + (size_t)y * w, w, 1, r, tmp);
	for (int x = 0; x < w; x++) boxLine(px + x, h, w, r, tmp);
}

// where dst pixel i of n samples a line of size: the pixel at or before
// it in *at and how far on to the next, out of 256, in *weight
static void sample(int i, int n, int size, int* at, int* weight) {
	int64_t f = ((int64_t)(2 * i + 1) * size * 256) / (2 * n) - 128;
	if (f < 0) f = 0;
	if (f > (int64_t)(size - 1) * 256) f = (int64_t)(size - 1) * 256;
	*at = (int)(f >> 8);
	*weight = (int)(f & 0xFF);
}

void Blur_grow(const uint32_t* small, int sw, int sh, uint32_t* dst, int pitch, int w, int h) {
	// each column's samples, worked out once
	uint16_t xs[BLUR_SIDE_MAX];
	uint8_t xw[BLUR_SIDE_MAX];
	if (w > BLUR_SIDE_MAX) return;
	for (int x = 0; x < w; x++) {
		int at = 0, weight = 0;
		sample(x, w, sw, &at, &weight);
		xs[x] = (uint16_t)at;
		xw[x] = (uint8_t)weight;
	}
	for (int y = 0; y < h; y++) {
		int y0 = 0, wy = 0;
		sample(y, h, sh, &y0, &wy);
		int y1 = lesser(y0 + 1, sh - 1);
		const uint32_t* top = small + (size_t)y0 * sw;
		const uint32_t* bottom = small + (size_t)y1 * sw;
		uint32_t* out = dst + (size_t)y * pitch;
		for (int x = 0; x < w; x++) {
			int x0 = xs[x], x1 = lesser(x0 + 1, sw - 1);
			uint32_t wx = xw[x];
			uint32_t c[4];
			for (int k = 0; k < 4; k++) {
				int shift = 24 - 8 * k;
				uint32_t t = ((top[x0] >> shift) & 0xFF) * (256 - wx) + ((top[x1] >> shift) & 0xFF) * wx;
				uint32_t b = ((bottom[x0] >> shift) & 0xFF) * (256 - wx) + ((bottom[x1] >> shift) & 0xFF) * wx;
				c[k] = (t * (256 - wy) + b * wy + 32768) >> 16;
			}
			uint32_t a = c[0];
			if (a == 0) {
				out[x] = 0;
				continue;
			}
			for (int k = 1; k < 4; k++) c[k] = c[k] >= a ? 255 : (c[k] * 255 + a / 2) / a;
			out[x] = a << 24 | c[1] << 16 | c[2] << 8 | c[3];
		}
	}
}
