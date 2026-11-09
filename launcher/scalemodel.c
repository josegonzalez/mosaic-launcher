#include <stdlib.h>
#include <string.h>
#include "scalemodel.h"

// One axis's filter: which source pixels each output pixel mixes, and by
// how much. Weights are whole numbers, and every output pixel's add up to
// total, so mirrored pixels get mirrored weights with no rounding between.
typedef struct {
	int* first;  // the first source pixel of each output pixel
	int* count;  // how many it mixes
	int* weight; // max_taps for each output pixel
	int max_taps;
	int total;
} Taps;

static void tapsFree(Taps* t) {
	free(t->first);
	free(t->count);
	free(t->weight);
}

static int tapsMake(Taps* t, int in, int out) {
	// shrinking covers in/out source pixels, plus one at each end it cuts
	t->max_taps = in > out ? in / out + 2 : 2;
	t->first = malloc(sizeof(int) * out);
	t->count = malloc(sizeof(int) * out);
	t->weight = malloc(sizeof(int) * out * t->max_taps);
	if (!t->first || !t->count || !t->weight) {
		tapsFree(t);
		return -1;
	}
	for (int i = 0; i < out; i++) {
		int* w = t->weight + i * t->max_taps;
		if (in == out) {
			t->total = 1;
			t->first[i] = i;
			t->count[i] = 1;
			w[0] = 1;
		}
		else if (in > out) {
			// area: output pixel i covers [i * in, (i + 1) * in) and source
			// pixel j covers [j * out, (j + 1) * out)
			t->total = in;
			int lo = i * in, hi = (i + 1) * in;
			int j = lo / out, last = (hi - 1) / out;
			t->first[i] = j;
			t->count[i] = last - j + 1;
			for (int k = 0; j + k <= last; k++) {
				int a = (j + k) * out, b = a + out;
				w[k] = (hi < b ? hi : b) - (lo > a ? lo : a);
			}
		}
		else {
			// bilinear: output pixel i's centre lands at s / total source
			// pixels from the first source pixel's centre
			t->total = 2 * out;
			int s = (2 * i + 1) * in - out;
			int j = s > 0 ? s / t->total : 0;
			int f = s > 0 ? s % t->total : 0;
			if (j >= in - 1) {
				j = in - 1;
				f = 0;
			}
			t->first[i] = j;
			t->count[i] = f ? 2 : 1;
			w[0] = t->total - f;
			w[1] = f;
		}
	}
	return 0;
}

// Mixes lines of 4 channel pixels along one axis: pixel k of a line is at
// in + k * step, and the result is scaled by mul before it's rounded.
static void tapsApply(const Taps* t, int out_n, const int32_t* in, int in_step, int32_t* out, int out_step, int mul) {
	for (int i = 0; i < out_n; i++) {
		const int* w = t->weight + i * t->max_taps;
		const int32_t* p = in + t->first[i] * in_step;
		for (int c = 0; c < 4; c++) {
			int64_t acc = 0;
			for (int k = 0; k < t->count[i]; k++) acc += (int64_t)w[k] * p[k * in_step + c];
			out[i * out_step + c] = (int32_t)((acc * mul + t->total / 2) / t->total);
		}
	}
}

int ScaleModel_smooth(const uint32_t* src, int sw, int sh, int src_pitch,
	uint32_t* dst, int dw, int dh, int dst_pitch) {
	if (sw < 1 || sh < 1 || dw < 1 || dh < 1) return -1;
	if (sw == dw && sh == dh) {
		for (int y = 0; y < sh; y++) memcpy(dst + y * dst_pitch, src + y * src_pitch, sizeof(uint32_t) * sw);
		return 0;
	}

	Taps h = {0}, v = {0};
	int32_t* pm = malloc(sizeof(int32_t) * 4 * sw * sh);
	int32_t* mid = malloc(sizeof(int32_t) * 4 * dw * sh);
	int32_t* fin = malloc(sizeof(int32_t) * 4 * dw * dh);
	int ok = pm && mid && fin && tapsMake(&h, sw, dw) == 0;
	if (ok && tapsMake(&v, sh, dh) != 0) {
		tapsFree(&h);
		ok = 0;
	}
	if (!ok) {
		free(pm);
		free(mid);
		free(fin);
		return -1;
	}

	// premultiplied, as a, r, g, b
	for (int y = 0; y < sh; y++) {
		for (int x = 0; x < sw; x++) {
			uint32_t p = src[y * src_pitch + x];
			int a = p >> 24;
			int32_t* q = pm + (y * sw + x) * 4;
			q[0] = a;
			for (int c = 1; c < 4; c++) q[c] = (((int)(p >> (24 - 8 * c)) & 0xff) * a + 127) / 255;
		}
	}
	// across, keeping 8 more bits for the pass down
	for (int y = 0; y < sh; y++) tapsApply(&h, dw, pm + y * sw * 4, 4, mid + y * dw * 4, 4, 256);
	for (int x = 0; x < dw; x++) tapsApply(&v, dh, mid + x * 4, dw * 4, fin + x * 4, dw * 4, 1);

	for (int y = 0; y < dh; y++) {
		for (int x = 0; x < dw; x++) {
			const int32_t* q = fin + (y * dw + x) * 4;
			int a = (q[0] + 128) >> 8;
			uint32_t p = (uint32_t)a << 24;
			for (int c = 1; c < 4 && a; c++) {
				int v8 = (q[c] + 128) >> 8;
				int s = (v8 * 255 + a / 2) / a;
				p |= (uint32_t)(s > 255 ? 255 : s) << (24 - 8 * c);
			}
			dst[y * dst_pitch + x] = p;
		}
	}

	tapsFree(&h);
	tapsFree(&v);
	free(pm);
	free(mid);
	free(fin);
	return 0;
}
