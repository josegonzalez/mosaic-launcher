#include <stdlib.h>
#include <string.h>
#include "shrinkmodel.h"

static unsigned int be16(const unsigned char* p) {
	return (unsigned int)p[0] << 8 | p[1];
}

static unsigned long be32(const unsigned char* p) {
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static int pngSize(const unsigned char* data, size_t size, int* w, int* h) {
	static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	if (size < 24 || memcmp(data, sig, 8) != 0 || memcmp(data + 12, "IHDR", 4) != 0) return -1;
	unsigned long pw = be32(data + 16), ph = be32(data + 20);
	if (pw == 0 || ph == 0 || pw > 0x7FFFFFFFul || ph > 0x7FFFFFFFul) return -1;
	*w = (int)pw;
	*h = (int)ph;
	return 0;
}

// The markers that start a frame and give its size: SOF0 to SOF15, but
// not DHT (C4), JPG (C8) or DAC (CC), which share the range.
static int isFrame(unsigned char m) {
	return m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC;
}

static int jpegSize(const unsigned char* data, size_t size, int* w, int* h) {
	if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return -1;
	size_t at = 2;
	while (at + 1 < size) {
		if (data[at] != 0xFF) return -1;
		unsigned char m = data[at + 1];
		if (m == 0xFF) {
			at++; // fill
			continue;
		}
		at += 2;
		if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) continue; // no length
		if (m == 0xD9 || m == 0xDA || at + 2 > size) return -1; // the end, or the data, before a frame
		unsigned int len = be16(data + at);
		if (len < 2) return -1;
		if (isFrame(m)) {
			if (len < 7 || at + 7 > size) return -1;
			unsigned int fh = be16(data + at + 3), fw = be16(data + at + 5);
			if (fw == 0 || fh == 0) return -1;
			*w = (int)fw;
			*h = (int)fh;
			return 0;
		}
		at += len;
	}
	return -1;
}

int Shrink_headerSize(const unsigned char* data, size_t size, int* w, int* h) {
	if (!data || !w || !h) return -1;
	if (pngSize(data, size, w, h) == 0) return 0;
	return jpegSize(data, size, w, h);
}

int Shrink_allowed(int w, int h) {
	return w >= 1 && h >= 1 && w <= SHRINK_MAX_SIDE && h <= SHRINK_MAX_SIDE && (long long)w * h <= SHRINK_MAX_PIXELS;
}

int Shrink_headerCheck(const unsigned char* data, size_t size, int* w, int* h) {
	static const unsigned char png[4] = { 0x89, 'P', 'N', 'G' };
	if (!data || !w || !h) return -1;
	// what SDL_image takes for a PNG or a JPEG
	int known = (size >= 4 && memcmp(data, png, 4) == 0) || (size >= 2 && data[0] == 0xFF && data[1] == 0xD8);
	return known ? Shrink_headerSize(data, size, w, h) : SHRINK_UNKNOWN;
}

int Shrink_readCheck(ShrinkRead reader, void* ctx, int* w, int* h) {
	if (!reader || !w || !h) return -1;
	unsigned char* buf = NULL;
	size_t have = 0, want = 16 * 1024;
	int rc = -1, end = 0;
	for (;;) {
		unsigned char* more = realloc(buf, want);
		if (!more) break;
		buf = more;
		while (have < want) {
			size_t n = reader(ctx, buf + have, want - have);
			if (n == 0 || n > want - have) {
				end = 1;
				break;
			}
			have += n;
		}
		rc = Shrink_headerCheck(buf, have, w, h);
		// a JPEG's size can come after other segments, so it reads on
		if (rc != -1 || end || want >= SHRINK_HEADER_MAX) break;
		want = want * 2 > SHRINK_HEADER_MAX ? SHRINK_HEADER_MAX : want * 2;
	}
	free(buf);
	return rc;
}

int Shrink_fit(int sw, int sh, int max_w, int max_h, int* dw, int* dh) {
	if (sw < 1 || sh < 1 || max_w < 1 || max_h < 1 || !dw || !dh) return -1;
	if (sw <= max_w && sh <= max_h) {
		*dw = sw;
		*dh = sh;
		return 0;
	}
	// the side that's furthest over decides: sw / max_w against sh / max_h
	if ((long long)sw * max_h >= (long long)sh * max_w) {
		*dw = max_w;
		*dh = (int)(((long long)sh * max_w * 2 + sw) / ((long long)sw * 2));
	} else {
		*dh = max_h;
		*dw = (int)(((long long)sw * max_h * 2 + sh) / ((long long)sh * 2));
	}
	if (*dw < 1) *dw = 1;
	if (*dh < 1) *dh = 1;
	return 0;
}

struct Shrink {
	int sw, sh, dw, dh;
	int y;        // the next source row
	int written;  // result rows written
	int* first;   // across: each result pixel's first source pixel
	int* count;   // how many it covers
	int* weight;  // max_taps each, adding up to sw
	int max_taps;
	long long* across; // the row being fed, shrunk across: dw times a, r*a, g*a, b*a
	long long* acc[2]; // result rows being added up, by row number mod 2
};

void Shrink_free(Shrink* s) {
	if (!s) return;
	free(s->first);
	free(s->count);
	free(s->weight);
	free(s->across);
	free(s->acc[0]);
	free(s->acc[1]);
	free(s);
}

Shrink* Shrink_new(int sw, int sh, int dw, int dh) {
	if (dw < 1 || dh < 1 || dw > sw || dh > sh || !Shrink_allowed(sw, sh)) return NULL;
	Shrink* s = calloc(1, sizeof(Shrink));
	if (!s) return NULL;
	s->sw = sw;
	s->sh = sh;
	s->dw = dw;
	s->dh = dh;
	// result pixel i covers [i * sw, (i + 1) * sw) and source pixel j
	// covers [j * dw, (j + 1) * dw), so it covers sw / dw of them, plus one
	// at each end it cuts
	s->max_taps = sw / dw + 2;
	s->first = malloc(sizeof(int) * dw);
	s->count = malloc(sizeof(int) * dw);
	s->weight = malloc(sizeof(int) * (size_t)dw * s->max_taps);
	s->across = malloc(sizeof(long long) * 4 * dw);
	s->acc[0] = calloc((size_t)dw * 4, sizeof(long long));
	s->acc[1] = calloc((size_t)dw * 4, sizeof(long long));
	if (!s->first || !s->count || !s->weight || !s->across || !s->acc[0] || !s->acc[1]) {
		Shrink_free(s);
		return NULL;
	}
	for (int i = 0; i < dw; i++) {
		long long lo = (long long)i * sw, hi = lo + sw;
		long long j = lo / dw, last = (hi - 1) / dw;
		int* w = s->weight + (size_t)i * s->max_taps;
		s->first[i] = (int)j;
		s->count[i] = (int)(last - j + 1);
		for (int k = 0; j + k <= last; k++) {
			long long a = (j + k) * dw, b = a + dw;
			w[k] = (int)((hi < b ? hi : b) - (lo > a ? lo : a));
		}
	}
	return s;
}

// a result row, from its sums, out of sw * sh
static void finish(const Shrink* s, const long long* sums, uint32_t* out) {
	long long total = (long long)s->sw * s->sh;
	for (int x = 0; x < s->dw; x++) {
		const long long* q = sums + x * 4;
		uint32_t a = (uint32_t)((q[0] + total / 2) / total);
		uint32_t p = a << 24;
		for (int c = 1; c < 4 && q[0] > 0; c++) {
			long long v = (q[c] + q[0] / 2) / q[0];
			p |= (uint32_t)(v > 255 ? 255 : v) << (24 - 8 * c);
		}
		out[x] = a ? p : 0;
	}
}

int Shrink_row(Shrink* s, const uint32_t* row, uint32_t* dst, int dst_pitch) {
	if (!s || !row || !dst || s->y >= s->sh) return -1;
	for (int i = 0; i < s->dw; i++) {
		const int* w = s->weight + (size_t)i * s->max_taps;
		const uint32_t* p = row + s->first[i];
		long long sum[4] = { 0, 0, 0, 0 };
		for (int k = 0; k < s->count[i]; k++) {
			uint32_t px = p[k];
			long long a = px >> 24;
			sum[0] += w[k] * a;
			sum[1] += w[k] * ((px >> 16 & 0xFF) * a);
			sum[2] += w[k] * ((px >> 8 & 0xFF) * a);
			sum[3] += w[k] * ((px & 0xFF) * a);
		}
		memcpy(s->across + i * 4, sum, sizeof(sum));
	}
	// source row y covers [y * dh, (y + 1) * dh), result row k [k * sh,
	// (k + 1) * sh), so it adds to one result row or two
	long long lo = (long long)s->y * s->dh, hi = lo + s->dh;
	for (long long k = lo / s->sh; k <= (hi - 1) / s->sh; k++) {
		long long a = k * s->sh, b = a + s->sh;
		long long part = (hi < b ? hi : b) - (lo > a ? lo : a);
		long long* acc = s->acc[k % 2];
		for (int i = 0; i < s->dw * 4; i++) acc[i] += part * s->across[i];
		if (hi >= b) {
			finish(s, acc, dst + (size_t)k * dst_pitch);
			memset(acc, 0, sizeof(long long) * 4 * s->dw);
			s->written++;
		}
	}
	s->y++;
	return 0;
}

int Shrink_done(const Shrink* s) {
	return s && s->written == s->dh;
}
