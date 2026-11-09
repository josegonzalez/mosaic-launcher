// scalemodel.c: smooth scaling of the sprite sheet's pieces.

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "scalemodel.h"

#define GUARD 0xDEADBEEFu

static const int sizes[] = { 1, 2, 3, 5, 20, 24, 29, 30, 31, 36, 45, 60, 90 };
#define SIZES (int)(sizeof(sizes) / sizeof(sizes[0]))

// dw x dh pixels with a guard column and row around them, all GUARD
static uint32_t* guarded(int dw, int dh) {
	int n = (dw + 2) * (dh + 2);
	uint32_t* buf = malloc(sizeof(uint32_t) * n);
	for (int i = 0; i < n; i++) buf[i] = GUARD;
	return buf;
}
#define AT(buf, dw, x, y) ((buf)[((y) + 1) * ((dw) + 2) + (x) + 1])

static int guardsKept(const uint32_t* buf, int dw, int dh) {
	for (int y = -1; y <= dh; y++)
		for (int x = -1; x <= dw; x++)
			if ((x < 0 || y < 0 || x == dw || y == dh) && AT(buf, dw, x, y) != GUARD) return 0;
	return 1;
}

static int scale(const uint32_t* src, int sw, int sh, uint32_t* buf, int dw, int dh) {
	return ScaleModel_smooth(src, sw, sh, sw, &AT(buf, dw, 0, 0), dw, dh, dw + 2);
}

static void testSameSize(void) {
	// partly and wholly transparent pixels too, colours and all
	uint32_t src[3][5];
	for (int y = 0; y < 3; y++)
		for (int x = 0; x < 5; x++) src[y][x] = 0x13579BDFu * (uint32_t)(y * 5 + x + 1);
	src[0][0] = 0x0000FF00u;
	src[1][2] = 0x01FFFFFFu;
	uint32_t* buf = guarded(5, 3);
	CHECK(scale(&src[0][0], 5, 3, buf, 5, 3) == 0);
	int same = 1;
	for (int y = 0; y < 3; y++)
		for (int x = 0; x < 5; x++) same &= AT(buf, 5, x, y) == src[y][x];
	CHECK(same);
	CHECK(guardsKept(buf, 5, 3));
	free(buf);
}

static void testUniform(void) {
	// an even colour stays that colour at any size, opaque or not
	static const uint32_t colors[] = { 0xFF336699u, 0x80FFFFFFu, 0x00000000u };
	uint32_t src[30 * 30];
	for (int c = 0; c < 3; c++) {
		for (int i = 0; i < 30 * 30; i++) src[i] = colors[c];
		for (int a = 0; a < SIZES; a++) {
			for (int b = 0; b < SIZES; b++) {
				int dw = sizes[a], dh = sizes[b];
				uint32_t* buf = guarded(dw, dh);
				CHECK(scale(src, 30, 30, buf, dw, dh) == 0);
				int even = 1;
				for (int y = 0; y < dh; y++)
					for (int x = 0; x < dw; x++) even &= AT(buf, dw, x, y) == colors[c];
				if (!even) fprintf(stderr, "%08X at %dx%d isn't even\n", colors[c], dw, dh);
				CHECK(even);
				CHECK(guardsKept(buf, dw, dh));
				free(buf);
			}
		}
	}
}

// a pill's end: a white disc with soft edges, the same mirrored either way,
// with colour that changes from the middle out
static void disc(uint32_t* src, int n) {
	double r = n / 2.0;
	for (int y = 0; y < n; y++) {
		for (int x = 0; x < n; x++) {
			double d = hypot(x + 0.5 - r, y + 0.5 - r);
			double a = r - d + 0.5;
			int alpha = a <= 0 ? 0 : a >= 1 ? 255 : (int)(a * 255);
			int shade = 255 - (int)d * 4;
			src[y * n + x] = (uint32_t)alpha << 24 | (uint32_t)shade << 16 | 0xFF00u | (uint32_t)(x < r ? x : n - 1 - x);
		}
	}
}

static void testMirrored(void) {
	uint32_t src[90 * 90];
	disc(src, 90);
	for (int a = 0; a < SIZES; a++) {
		for (int b = 0; b < SIZES; b++) {
			int dw = sizes[a], dh = sizes[b];
			uint32_t* buf = guarded(dw, dh);
			CHECK(scale(src, 90, 90, buf, dw, dh) == 0);
			int mirrored = 1;
			for (int y = 0; y < dh; y++) {
				for (int x = 0; x < dw; x++) {
					uint32_t p = AT(buf, dw, x, y);
					mirrored &= p == AT(buf, dw, dw - 1 - x, y) && p == AT(buf, dw, x, dh - 1 - y);
				}
			}
			if (!mirrored) fprintf(stderr, "90 to %dx%d isn't mirrored\n", dw, dh);
			CHECK(mirrored);
			CHECK(guardsKept(buf, dw, dh));
			free(buf);
		}
	}
}

static void testPremultiplied(void) {
	// a transparent pixel's green never shows next to opaque red
	uint32_t src[2] = { 0xFFFF0000u, 0x0000FF00u };
	uint32_t* buf = guarded(1, 1);
	CHECK(scale(src, 2, 1, buf, 1, 1) == 0);
	CHECK(AT(buf, 1, 0, 0) == 0x80FF0000u);
	free(buf);

	for (int dw = 3; dw <= 9; dw++) {
		buf = guarded(dw, 2);
		CHECK(scale(src, 2, 1, buf, dw, 2) == 0);
		int red = 1;
		for (int y = 0; y < 2; y++) {
			for (int x = 0; x < dw; x++) {
				uint32_t p = AT(buf, dw, x, y);
				red &= (p >> 24) == 0 || (p & 0xFFFFFFu) == 0xFF0000u;
			}
		}
		CHECK(red);
		free(buf);
	}
}

static void testFilters(void) {
	// shrinking averages what each pixel covers
	uint32_t grays[4] = { 0xFF000000u, 0xFF646464u, 0xFFC8C8C8u, 0xFF282828u };
	uint32_t* buf = guarded(2, 1);
	CHECK(scale(grays, 4, 1, buf, 2, 1) == 0);
	CHECK(AT(buf, 2, 0, 0) == 0xFF323232u); // (0 + 100) / 2
	CHECK(AT(buf, 2, 1, 0) == 0xFF787878u); // (200 + 40) / 2
	free(buf);
	// and by how much of it, for a size that doesn't divide
	uint32_t three[3] = { 0xFF000000u, 0xFF969696u, 0xFF000000u };
	buf = guarded(2, 1);
	CHECK(scale(three, 3, 1, buf, 2, 1) == 0);
	CHECK(AT(buf, 2, 0, 0) == 0xFF323232u); // (0 + 150 / 2) / 1.5
	CHECK(AT(buf, 2, 1, 0) == 0xFF323232u);
	free(buf);

	// growing is bilinear between centres, holding the end pixels
	uint32_t two[2] = { 0xFF000000u, 0xFFC8C8C8u };
	buf = guarded(4, 1);
	CHECK(scale(two, 2, 1, buf, 4, 1) == 0);
	CHECK(AT(buf, 4, 0, 0) == 0xFF000000u);
	CHECK(AT(buf, 4, 1, 0) == 0xFF323232u); // a quarter of the way
	CHECK(AT(buf, 4, 2, 0) == 0xFF969696u);
	CHECK(AT(buf, 4, 3, 0) == 0xFFC8C8C8u);
	free(buf);

	// a hard edge scaled up gets a partly transparent pixel, where
	// nearest-neighbour only ever repeats a 0 or a 255
	uint32_t edge[4] = { 0x00FFFFFFu, 0x00FFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
	buf = guarded(6, 1);
	CHECK(scale(edge, 4, 1, buf, 6, 1) == 0);
	int partial = 0;
	for (int x = 0; x < 6; x++) {
		uint32_t a = AT(buf, 6, x, 0) >> 24;
		partial += a > 0 && a < 255;
	}
	CHECK(partial > 0);
	free(buf);
}

static void testTiny(void) {
	// every small size to every other, and down to or up from one pixel
	uint32_t src[90 * 90];
	disc(src, 90);
	for (int sw = 1; sw <= 4; sw++) {
		for (int sh = 1; sh <= 4; sh++) {
			for (int dw = 1; dw <= 4; dw++) {
				for (int dh = 1; dh <= 4; dh++) {
					uint32_t* buf = guarded(dw, dh);
					CHECK(ScaleModel_smooth(src, sw, sh, 90, &AT(buf, dw, 0, 0), dw, dh, dw + 2) == 0);
					CHECK(guardsKept(buf, dw, dh));
					free(buf);
				}
			}
		}
	}
	uint32_t* buf = guarded(1, 1);
	CHECK(scale(src, 90, 90, buf, 1, 1) == 0);
	CHECK(guardsKept(buf, 1, 1));
	free(buf);
	buf = guarded(90, 90);
	uint32_t one = 0xFF102030u;
	CHECK(scale(&one, 1, 1, buf, 90, 90) == 0);
	CHECK(guardsKept(buf, 90, 90));
	CHECK(AT(buf, 90, 0, 0) == one && AT(buf, 90, 89, 89) == one);
	free(buf);

	// a size under one is refused and nothing's written
	buf = guarded(2, 2);
	CHECK(scale(src, 0, 2, buf, 2, 2) == -1);
	CHECK(scale(src, 2, 2, buf, 0, 2) == -1);
	CHECK(scale(src, 2, 2, buf, 2, -1) == -1);
	CHECK(AT(buf, 2, 0, 0) == GUARD);
	free(buf);
}

int main(void) {
	testSameSize();
	testUniform();
	testMirrored();
	testPremultiplied();
	testFilters();
	testTiny();
	CHECK_DONE();
}
