// blurmodel.c: the small copy's size, shrinking into it, the box blur and
// enlarging it again, and how long a hero-sized blur takes.

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "check.h"
#include "blurmodel.h"

#define ARGB(a, r, g, b) ((uint32_t)(a) << 24 | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define CH(p, shift) (((p) >> (shift)) & 0xFF)

static void testPlan(void) {
	int sw = 0, sh = 0, r = 0;
	// a band across a 1280 wide screen: an eighth the size, a box of 3
	CHECK(Blur_plan(1920, 1080, 1280, 400, 24, &sw, &sh, &r) == 0);
	CHECK(sw == 160 && sh == 50 && r == 3);
	// never more than BLUR_SMALL_MAX a side, however small the radius
	CHECK(Blur_plan(4096, 4096, 4096, 4096, 1, &sw, &sh, &r) == 0);
	CHECK(sw == BLUR_SMALL_MAX && sh == BLUR_SMALL_MAX && r == 1);
	CHECK(Blur_plan(2000, 100, 2000, 100, 1, &sw, &sh, &r) == 0);
	CHECK(sw == 250 && sh == 13 && r == 1);
	// never larger than the image, with the box grown to match
	CHECK(Blur_plan(10, 10, 100, 100, 30, &sw, &sh, &r) == 0);
	CHECK(sw == 10 && sh == 10 && r == 3);
	CHECK(Blur_plan(4, 4, 400, 400, 30, &sw, &sh, &r) == 0);
	CHECK(sw == 4 && sh == 4 && r == 1);
	// refused
	CHECK(Blur_plan(0, 10, 10, 10, 1, &sw, &sh, &r) == -1);
	CHECK(Blur_plan(10, 10, 0, 10, 1, &sw, &sh, &r) == -1);
	CHECK(Blur_plan(10, 10, 10, 10, 0, &sw, &sh, &r) == -1);
	CHECK(Blur_plan(10, 10, 10, 10, BLUR_RADIUS_MAX + 1, &sw, &sh, &r) == -1);
	CHECK(Blur_plan(10, 10, 10, 10, 1, NULL, &sh, &r) == -1);
}

static void testShrink(void) {
	// 4 by 2 into 2 by 1: each the average of a 2 by 2 square
	uint32_t image[2][4] = {
		{ ARGB(255, 0, 0, 0), ARGB(255, 200, 100, 40), ARGB(255, 10, 10, 10), ARGB(255, 10, 10, 10) },
		{ ARGB(255, 0, 0, 0), ARGB(255, 200, 100, 40), ARGB(255, 10, 10, 10), ARGB(255, 30, 30, 30) },
	};
	uint64_t sums[2 * 4];
	uint32_t small[2] = { 0, 0 };
	BlurShrink s;
	Blur_shrinkStart(&s, 4, 2, 2, 1, sums, small);
	CHECK(Blur_shrinkRow(&s, image[0]) == 0);
	CHECK(small[0] == 0); // not until the small row's last image row
	CHECK(Blur_shrinkRow(&s, image[1]) == 0);
	CHECK(Blur_shrinkRow(&s, image[1]) == -1);
	CHECK(small[0] == ARGB(255, 100, 50, 20));
	CHECK(small[1] == ARGB(255, 15, 15, 15));

	// clear pixels don't darken: premultiplied, a clear black and an opaque
	// white make a half clear white
	uint32_t row[2] = { ARGB(0, 0, 0, 0), ARGB(255, 255, 255, 255) };
	uint32_t one = 0;
	uint64_t sums1[4];
	Blur_shrinkStart(&s, 2, 1, 1, 1, sums1, &one);
	CHECK(Blur_shrinkRow(&s, row) == 0);
	CHECK(CH(one, 24) == 128);
	uint32_t grown = 0;
	Blur_grow(&one, 1, 1, &grown, 1, 1, 1);
	CHECK(grown == ARGB(128, 255, 255, 255));

	// rows that don't divide evenly: 5 rows into 2, 2 and then 3
	uint32_t column[5] = { ARGB(255, 0, 0, 0), ARGB(255, 0, 0, 0), ARGB(255, 90, 90, 90), ARGB(255, 90, 90, 90),
		ARGB(255, 90, 90, 90) };
	uint32_t two[2] = { 0, 0 };
	uint64_t sums2[4];
	Blur_shrinkStart(&s, 1, 5, 1, 2, sums2, two);
	for (int y = 0; y < 5; y++) CHECK(Blur_shrinkRow(&s, &column[y]) == 0);
	CHECK(two[0] == ARGB(255, 0, 0, 0));
	CHECK(two[1] == ARGB(255, 90, 90, 90));
}

static void testBox(void) {
	enum { W = 9, H = 7 };
	uint32_t px[W * H], tmp[W];
	// an even colour stays even, edges included
	for (int i = 0; i < W * H; i++) px[i] = ARGB(255, 40, 80, 120);
	Blur_box(px, W, H, 2, tmp);
	for (int i = 0; i < W * H; i++) CHECK(px[i] == ARGB(255, 40, 80, 120));

	// a white dot in black spreads evenly round it, and dims
	for (int i = 0; i < W * H; i++) px[i] = ARGB(255, 0, 0, 0);
	px[3 * W + 4] = ARGB(255, 225, 225, 225);
	Blur_box(px, W, H, 1, tmp);
	CHECK(px[3 * W + 4] == ARGB(255, 25, 25, 25));
	CHECK(px[2 * W + 3] == px[4 * W + 5]);
	CHECK(px[3 * W + 3] == px[3 * W + 5]);
	CHECK(px[2 * W + 4] == px[4 * W + 4]);
	CHECK(px[0] == ARGB(255, 0, 0, 0));
	// a radius of 0 changes nothing
	uint32_t before = px[3 * W + 4];
	Blur_box(px, W, H, 0, tmp);
	CHECK(px[3 * W + 4] == before);
	// a radius past the image's size repeats the edges
	uint32_t line[3] = { ARGB(255, 0, 0, 0), ARGB(255, 0, 0, 0), ARGB(255, 90, 90, 90) };
	Blur_box(line, 3, 1, 5, tmp);
	CHECK(CH(line[0], 16) < CH(line[2], 16));
}

static void testGrow(void) {
	// from black to white, left to right, smoothly and in order
	uint32_t small[2] = { ARGB(255, 0, 0, 0), ARGB(255, 255, 255, 255) };
	uint32_t out[8];
	Blur_grow(small, 2, 1, out, 8, 8, 1);
	CHECK(out[0] == ARGB(255, 0, 0, 0));
	CHECK(out[7] == ARGB(255, 255, 255, 255));
	for (int i = 1; i < 8; i++) CHECK(CH(out[i], 16) >= CH(out[i - 1], 16));
	CHECK(CH(out[3], 16) > 0 && CH(out[3], 16) < 255);
	// rows pitch apart, the rest of each row left alone
	uint32_t block[2 * 4] = { 0, 0, 0, 7, 0, 0, 0, 7 };
	uint32_t one = ARGB(255, 10, 20, 30);
	Blur_grow(&one, 1, 1, block, 4, 3, 2);
	CHECK(block[0] == one && block[2] == one && block[4] == one && block[6] == one);
	CHECK(block[3] == 7 && block[7] == 7);
	// clear stays clear
	uint32_t clear = 0, got = 5;
	Blur_grow(&clear, 1, 1, &got, 1, 1, 1);
	CHECK(got == 0);
}

// the whole of it, as surface_blur runs it, for a band of a full-size hero
static double blur(const uint32_t* image, int iw, int ih, uint32_t* out, int w, int h, int radius) {
	clock_t started = clock();
	int sw = 0, sh = 0, r = 0;
	if (Blur_plan(iw, ih, w, h, radius, &sw, &sh, &r) != 0) return -1;
	uint32_t* small = malloc(sizeof(uint32_t) * sw * sh);
	uint64_t* sums = malloc(sizeof(uint64_t) * 4 * sw);
	uint32_t* tmp = malloc(sizeof(uint32_t) * (iw > sw ? iw : sw) + sizeof(uint32_t) * sh);
	BlurShrink s;
	Blur_shrinkStart(&s, iw, ih, sw, sh, sums, small);
	for (int y = 0; y < ih; y++) Blur_shrinkRow(&s, image + (size_t)y * iw);
	for (int i = 0; i < BLUR_PASSES; i++) Blur_box(small, sw, sh, r, tmp);
	Blur_grow(small, sw, sh, out, w, w, h);
	free(tmp);
	free(sums);
	free(small);
	return (double)(clock() - started) * 1000 / CLOCKS_PER_SEC;
}

static void testWhole(void) {
	enum { IW = 1920, IH = 1080, W = 1280, H = 720 };
	uint32_t* image = malloc(sizeof(uint32_t) * IW * IH);
	uint32_t* out = malloc(sizeof(uint32_t) * W * H);
	// left half red, right half blue
	for (int y = 0; y < IH; y++) {
		for (int x = 0; x < IW; x++) image[(size_t)y * IW + x] = x < IW / 2 ? ARGB(255, 255, 0, 0) : ARGB(255, 0, 0, 255);
	}
	double ms = blur(image, IW, IH, out, W, H, 24);
	CHECK(ms >= 0);
	// far from the edge between them unchanged, and blended across it
	CHECK(out[(size_t)360 * W + 10] == ARGB(255, 255, 0, 0));
	CHECK(out[(size_t)360 * W + W - 10] == ARGB(255, 0, 0, 255));
	uint32_t mid = out[(size_t)360 * W + W / 2];
	CHECK(CH(mid, 16) > 64 && CH(mid, 0) > 64);
	printf("blurred %ix%i into %ix%i by 24 in %.1f ms\n", IW, IH, W, H, ms);
	// layout-hero-rail's band on a 1280x720 screen, from its own copy
	ms = blur(image, 1280, 400, out, 1280, 400, 37);
	CHECK(ms >= 0);
	printf("blurred a 1280x400 band by 37 in %.1f ms\n", ms);
	free(out);
	free(image);
}

int main(void) {
	testPlan();
	testShrink();
	testBox();
	testGrow();
	testWhole();
	CHECK_DONE();
}
