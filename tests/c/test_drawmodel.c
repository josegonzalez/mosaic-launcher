// drawmodel.c: who owns which surface, text truncation, rounded corners and
// fitting, with a fake font one pixel per byte wide.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "drawmodel.h"

static int measured = 0;

static int bytesWide(const char* text, void* data) {
	(void)data;
	measured++;
	return (int)strlen(text);
}

static int failing(const char* text, void* data) {
	(void)text;
	(void)data;
	return -1;
}

static void testOwners(void) {
	static int a, b, c, d;
	CHECK(DrawReg_add(&a, "one", 100) == 0);
	CHECK(DrawReg_add(&b, "one", 200) == 0);
	CHECK(DrawReg_add(&c, "two", 50) == 0);
	CHECK(DrawReg_add(&d, DRAW_HOST, 1000) == 0);
	CHECK_STR(DrawReg_owner(&a), "one");
	CHECK_STR(DrawReg_owner(&c), "two");
	CHECK_STR(DrawReg_owner(&d), DRAW_HOST);
	CHECK(DrawReg_bytes("one") == 300);
	CHECK(DrawReg_bytes(DRAW_HOST) == 0);

	// refused: already recorded, NULLs, an owner too long to keep
	CHECK(DrawReg_add(&a, "two", 1) == -1);
	CHECK(DrawReg_add(NULL, "one", 1) == -1);
	CHECK(DrawReg_add(&a, NULL, 1) == -1);
	char longer[80];
	memset(longer, 'x', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = '\0';
	static int e;
	CHECK(DrawReg_add(&e, longer, 1) == -1);
	CHECK(DrawReg_owner(&e) == NULL);
	CHECK(DrawReg_owner(NULL) == NULL);

	// only the owner can forget one
	CHECK(DrawReg_remove(&a, "two") == -1);
	CHECK(DrawReg_remove(&a, DRAW_HOST) == -1);
	CHECK(DrawReg_remove(&a, "one") == 0);
	CHECK(DrawReg_owner(&a) == NULL);
	CHECK(DrawReg_remove(&a, "one") == -1);
	CHECK(DrawReg_remove(NULL, "one") == -1);
	CHECK(DrawReg_bytes("one") == 200);

	// taking leaves other owners' alone
	void* out[4];
	CHECK(DrawReg_take("one", out, 4) == 1);
	CHECK(out[0] == &b);
	CHECK(DrawReg_take("one", out, 4) == 0);
	CHECK_STR(DrawReg_owner(&c), "two");
	CHECK(DrawReg_take("two", out, 4) == 1);
	CHECK(DrawReg_remove(&d, DRAW_HOST) == 0);
}

static void testReadOnly(void) {
	static int art, lent, mine;
	CHECK(DrawReg_add(&art, DRAW_HOST, 0) == 0);
	CHECK(DrawReg_add(&lent, DRAW_HOST, 0) == 0);
	CHECK(DrawReg_add(&mine, "one", 10) == 0);
	CHECK(DrawReg_writable(&art) && DrawReg_writable(&lent) && DrawReg_writable(&mine));
	// only the launcher's own can be made read-only
	CHECK(DrawReg_lock(&art) == 0);
	CHECK(DrawReg_lock(&mine) == -1);
	CHECK(DrawReg_lock(NULL) == -1);
	static int unknown;
	CHECK(DrawReg_lock(&unknown) == -1);
	CHECK(!DrawReg_writable(&art));
	CHECK(DrawReg_writable(&lent) && DrawReg_writable(&mine));
	CHECK(!DrawReg_writable(&unknown) && !DrawReg_writable(NULL));
	// still known, so it can be drawn from, and plugins can't free it
	CHECK_STR(DrawReg_owner(&art), DRAW_HOST);
	CHECK(DrawReg_remove(&art, "one") == -1);
	CHECK(DrawReg_remove(&art, DRAW_HOST) == 0);
	// a new surface at the same address starts out writable
	CHECK(DrawReg_add(&art, DRAW_HOST, 0) == 0);
	CHECK(DrawReg_writable(&art));
	CHECK(DrawReg_remove(&art, DRAW_HOST) == 0);
	CHECK(DrawReg_remove(&lent, DRAW_HOST) == 0);
	CHECK(DrawReg_remove(&mine, "one") == 0);
}

static void testBudget(void) {
	CHECK(DrawReg_fits("one", DRAW_PLUGIN_BYTES));
	CHECK(!DrawReg_fits("one", DRAW_PLUGIN_BYTES + 1));
	CHECK(!DrawReg_fits(NULL, 1));
	CHECK(DrawReg_fits(DRAW_HOST, (size_t)-1));

	static int a, b, c;
	CHECK(DrawReg_add(&a, "one", DRAW_PLUGIN_BYTES - 10) == 0);
	CHECK(DrawReg_fits("one", 10));
	CHECK(!DrawReg_fits("one", 11));
	CHECK(DrawReg_add(&b, "one", 11) == -1);
	CHECK(DrawReg_owner(&b) == NULL);
	CHECK(DrawReg_add(&b, "one", 10) == 0);
	// another plugin's budget is its own
	CHECK(DrawReg_add(&c, "two", 1000) == 0);
	CHECK(!DrawReg_fits("one", 1));

	// freeing makes room again
	CHECK(DrawReg_remove(&a, "one") == 0);
	CHECK(DrawReg_fits("one", DRAW_PLUGIN_BYTES - 10));
	void* out[4];
	CHECK(DrawReg_take("one", out, 4) == 1);
	CHECK(DrawReg_take("two", out, 4) == 1);
}

static void testGive(void) {
	static int made, other, lent, art;
	CHECK(DrawReg_add(&made, "one", 1000) == 0);
	CHECK(DrawReg_add(&other, "two", 10) == 0);
	CHECK(DrawReg_add(&lent, DRAW_HOST, 0) == 0);
	CHECK(DrawReg_add(&art, DRAW_HOST, 0) == 0);
	CHECK(DrawReg_lock(&art) == 0);
	// only its owner's, and not the launcher's own
	CHECK(DrawReg_give(&made, "two") == -1);
	CHECK(DrawReg_give(&other, "one") == -1);
	CHECK(DrawReg_give(&lent, DRAW_HOST) == -1);
	CHECK(DrawReg_give(&art, DRAW_HOST) == -1);
	CHECK(DrawReg_give(NULL, "one") == -1);
	CHECK(DrawReg_give(&made, NULL) == -1);
	static int unknown;
	CHECK(DrawReg_give(&unknown, "one") == -1);
	CHECK_STR(DrawReg_owner(&made), "one");

	// the launcher's now, read-only, and off the plugin's budget
	CHECK(DrawReg_give(&made, "one") == 0);
	CHECK_STR(DrawReg_owner(&made), DRAW_HOST);
	CHECK(!DrawReg_writable(&made));
	CHECK(DrawReg_bytes("one") == 0);
	CHECK(DrawReg_fits("one", DRAW_PLUGIN_BYTES));
	// the plugin can't free it, or give it again
	CHECK(DrawReg_remove(&made, "one") == -1);
	CHECK(DrawReg_give(&made, "one") == -1);
	void* out[4];
	CHECK(DrawReg_take("one", out, 4) == 0);
	CHECK(DrawReg_remove(&made, DRAW_HOST) == 0);
	CHECK(DrawReg_remove(&other, "two") == 0);
	CHECK(DrawReg_remove(&lent, DRAW_HOST) == 0);
	CHECK(DrawReg_remove(&art, DRAW_HOST) == 0);
}

static void testMany(void) {
	// more than the first allocation, taken in batches smaller than the count
	static int handles[100];
	for (int i = 0; i < 100; i++) CHECK(DrawReg_add(&handles[i], "many", 1) == 0);
	CHECK(DrawReg_bytes("many") == 100);
	void* out[30];
	int total = 0, n;
	while ((n = DrawReg_take("many", out, 30)) > 0) {
		CHECK(n <= 30);
		total += n;
	}
	CHECK(total == 100);
	for (int i = 0; i < 100; i++) CHECK(DrawReg_owner(&handles[i]) == NULL);
}

static void testTruncate(void) {
	char out[64];
	CHECK(Draw_truncate("Hello", 10, bytesWide, NULL, out, sizeof(out)) == 5);
	CHECK_STR(out, "Hello");
	CHECK(Draw_truncate("Hello", 5, bytesWide, NULL, out, sizeof(out)) == 5);
	CHECK_STR(out, "Hello");

	// the longest prefix that fits with "..."
	CHECK(Draw_truncate("Hello world", 8, bytesWide, NULL, out, sizeof(out)) == 8);
	CHECK_STR(out, "Hello...");
	CHECK(Draw_truncate("Hello", 4, bytesWide, NULL, out, sizeof(out)) == 4);
	CHECK_STR(out, "H...");
	CHECK(Draw_truncate("Hello", 3, bytesWide, NULL, out, sizeof(out)) == 3);
	CHECK_STR(out, "...");
	// not even "..." fits
	CHECK(Draw_truncate("Hello", 2, bytesWide, NULL, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(Draw_truncate("", 0, bytesWide, NULL, out, sizeof(out)) == 0);
	CHECK_STR(out, "");

	// only the first line
	CHECK(Draw_truncate("One\nTwo", 10, bytesWide, NULL, out, sizeof(out)) == 3);
	CHECK_STR(out, "One");
	CHECK(Draw_truncate("Longer line\nTwo", 7, bytesWide, NULL, out, sizeof(out)) == 7);
	CHECK_STR(out, "Long...");

	// short of room in out, as well as width
	char small[8];
	CHECK(Draw_truncate("Hello world", 100, bytesWide, NULL, small, sizeof(small)) == 7);
	CHECK_STR(small, "Hell...");
	CHECK(Draw_truncate("Hello w", 100, bytesWide, NULL, small, sizeof(small)) == 7);
	CHECK_STR(small, "Hello w");
	char tiny[3];
	CHECK(Draw_truncate("Hello", 100, bytesWide, NULL, tiny, sizeof(tiny)) == 0);
	CHECK_STR(tiny, "");
	CHECK(Draw_truncate("Hi", 100, bytesWide, NULL, tiny, sizeof(tiny)) == 2);
	CHECK_STR(tiny, "Hi");

	// never between the bytes of a character: "é" is two bytes
	CHECK(Draw_truncate("Pok\xc3\xa9mon", 7, bytesWide, NULL, out, sizeof(out)) == 6);
	CHECK_STR(out, "Pok...");
	CHECK(Draw_truncate("Pok\xc3\xa9mon Red", 8, bytesWide, NULL, out, sizeof(out)) == 8);
	CHECK_STR(out, "Pok\xc3\xa9...");
	char six[6];
	CHECK(Draw_truncate("\xc3\xa9\xc3\xa9\xc3\xa9", 100, bytesWide, NULL, six, sizeof(six)) == 5);
	CHECK_STR(six, "\xc3\xa9...");

	// a long name takes a handful of measures, not one per byte
	char name[201];
	memset(name, 'a', 200);
	name[200] = '\0';
	measured = 0;
	CHECK(Draw_truncate(name, 50, bytesWide, NULL, out, sizeof(out)) == 50);
	CHECK(strlen(out) == 50);
	CHECK(measured < 20);

	// bad arguments
	CHECK(Draw_truncate(NULL, 10, bytesWide, NULL, out, sizeof(out)) == -1);
	CHECK(Draw_truncate("Hi", 10, NULL, NULL, out, sizeof(out)) == -1);
	CHECK(Draw_truncate("Hi", 10, bytesWide, NULL, NULL, 4) == -1);
	CHECK(Draw_truncate("Hi", 10, bytesWide, NULL, out, 0) == -1);
	CHECK(Draw_truncate("Hi", -1, bytesWide, NULL, out, sizeof(out)) == -1);
	CHECK(Draw_truncate("Hi", 10, failing, NULL, out, sizeof(out)) == -1);
}

static void testRound(void) {
	CHECK(Draw_clampRadius(4, 20, 10) == 4);
	CHECK(Draw_clampRadius(8, 20, 10) == 5);
	CHECK(Draw_clampRadius(8, 3, 10) == 1);
	CHECK(Draw_clampRadius(0, 20, 10) == 0);
	CHECK(Draw_clampRadius(-1, 20, 10) == -1);

	// a radius 10 corner on a 40 tall rect
	CHECK(Draw_roundInset(10, 40, 0) == 7);
	CHECK(Draw_roundInset(10, 40, 9) == 0);
	CHECK(Draw_roundInset(10, 40, 10) == 0);
	CHECK(Draw_roundInset(10, 40, 20) == 0);
	CHECK(Draw_roundInset(10, 40, 39) == 7);
	// symmetric, and never wider further from the edge
	for (int row = 0; row < 10; row++) {
		CHECK(Draw_roundInset(10, 40, row) == Draw_roundInset(10, 40, 39 - row));
		CHECK(Draw_roundInset(10, 40, row + 1) <= Draw_roundInset(10, 40, row));
	}
	// a pill: half as tall, rounded all the way
	CHECK(Draw_roundInset(5, 10, 0) > 0);
	CHECK(Draw_roundInset(5, 10, 4) == 0);
	CHECK(Draw_roundInset(5, 10, 5) == 0);
	CHECK(Draw_roundInset(0, 10, 0) == 0);
	CHECK(Draw_roundInset(5, 10, -1) == 0);
	CHECK(Draw_roundInset(5, 10, 10) == 0);
}

static void testFit(void) {
	MosaicRect src, dst;
	CHECK(Draw_fit(200, 100, 50, 50, MOSAIC_FIT_STRETCH, &src, &dst) == 0);
	CHECK(src.x == 0 && src.y == 0 && src.w == 200 && src.h == 100);
	CHECK(dst.x == 0 && dst.y == 0 && dst.w == 50 && dst.h == 50);

	// wide into square: bars above and below
	CHECK(Draw_fit(200, 100, 50, 50, MOSAIC_FIT_CONTAIN, &src, &dst) == 0);
	CHECK(src.w == 200 && src.h == 100);
	CHECK(dst.x == 0 && dst.y == 12 && dst.w == 50 && dst.h == 25);
	// tall into square: bars at the sides
	CHECK(Draw_fit(100, 200, 50, 50, MOSAIC_FIT_CONTAIN, &src, &dst) == 0);
	CHECK(dst.x == 12 && dst.y == 0 && dst.w == 25 && dst.h == 50);
	// same shape
	CHECK(Draw_fit(100, 100, 50, 50, MOSAIC_FIT_CONTAIN, &src, &dst) == 0);
	CHECK(dst.x == 0 && dst.y == 0 && dst.w == 50 && dst.h == 50);

	// wide into square: the middle of it
	CHECK(Draw_fit(200, 100, 50, 50, MOSAIC_FIT_COVER, &src, &dst) == 0);
	CHECK(src.x == 50 && src.y == 0 && src.w == 100 && src.h == 100);
	CHECK(dst.w == 50 && dst.h == 50);
	CHECK(Draw_fit(100, 200, 50, 50, MOSAIC_FIT_COVER, &src, &dst) == 0);
	CHECK(src.x == 0 && src.y == 50 && src.w == 100 && src.h == 100);

	// never down to nothing
	CHECK(Draw_fit(4000, 1, 10, 10, MOSAIC_FIT_CONTAIN, &src, &dst) == 0);
	CHECK(dst.h == 1);
	CHECK(Draw_fit(4000, 1, 10, 10, MOSAIC_FIT_COVER, &src, &dst) == 0);
	CHECK(src.w == 1);

	CHECK(Draw_fit(0, 100, 50, 50, MOSAIC_FIT_CONTAIN, &src, &dst) == -1);
	CHECK(Draw_fit(100, 100, 50, -1, MOSAIC_FIT_CONTAIN, &src, &dst) == -1);
	CHECK(Draw_fit(100, 100, 50, 50, 3, &src, &dst) == -1);
	CHECK(Draw_fit(100, 100, 50, 50, MOSAIC_FIT_CONTAIN, NULL, &dst) == -1);
}

static void testMine(void) {
	static int made, other, lent, art, given, unknown;
	CHECK(DrawReg_add(&made, "one", 10) == 0);
	CHECK(DrawReg_add(&other, "two", 10) == 0);
	CHECK(DrawReg_add(&lent, DRAW_HOST, 0) == 0);
	CHECK(DrawReg_add(&art, DRAW_HOST, 0) == 0);
	CHECK(DrawReg_lock(&art) == 0);
	CHECK(DrawReg_add(&given, "one", 10) == 0);
	CHECK(DrawReg_give(&given, "one") == 0);
	CHECK(DrawReg_mine(&made, "one"));
	// another plugin's, lent, the art cache's, given away, unknown
	CHECK(!DrawReg_mine(&made, "two"));
	CHECK(!DrawReg_mine(&other, "one"));
	CHECK(!DrawReg_mine(&lent, "one") && !DrawReg_mine(&lent, DRAW_HOST));
	CHECK(!DrawReg_mine(&art, "one") && !DrawReg_mine(&art, DRAW_HOST));
	CHECK(!DrawReg_mine(&given, "one"));
	CHECK(!DrawReg_mine(&unknown, "one"));
	CHECK(!DrawReg_mine(NULL, "one") && !DrawReg_mine(&made, NULL));
	// and no longer once freed
	CHECK(DrawReg_remove(&made, "one") == 0);
	CHECK(!DrawReg_mine(&made, "one"));
	CHECK(DrawReg_remove(&other, "two") == 0);
	CHECK(DrawReg_remove(&lent, DRAW_HOST) == 0);
	CHECK(DrawReg_remove(&art, DRAW_HOST) == 0);
	CHECK(DrawReg_remove(&given, DRAW_HOST) == 0);
}

static void testWriteArea(void) {
	MosaicRect area;
	size_t offset;
	// all of it, with a NULL rect
	CHECK(Draw_writeArea(40, 20, NULL, 160, &area, &offset) == 1);
	CHECK(area.x == 0 && area.y == 0 && area.w == 40 && area.h == 20 && offset == 0);
	CHECK(Draw_writeArea(40, 20, NULL, 159, &area, &offset) == -1);
	// inside, with rows padded
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 5, 6, 10, 4 }, 64, &area, &offset) == 1);
	CHECK(area.x == 5 && area.y == 6 && area.w == 10 && area.h == 4 && offset == 0);
	// past the top left: the rows and pixels before the surface are skipped
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ -3, -2, 10, 5 }, 48, &area, &offset) == 1);
	CHECK(area.x == 0 && area.y == 0 && area.w == 7 && area.h == 3 && offset == 2 * 48 + 3 * 4);
	// past the bottom right
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 35, 18, 10, 5 }, 40, &area, &offset) == 1);
	CHECK(area.x == 35 && area.y == 18 && area.w == 5 && area.h == 2 && offset == 0);
	// over every edge
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ -1, -1, 42, 22 }, 168, &area, &offset) == 1);
	CHECK(area.x == 0 && area.y == 0 && area.w == 40 && area.h == 20 && offset == 168 + 4);
	// nothing on the surface, or nothing at all
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 40, 0, 5, 5 }, 20, &area, &offset) == 0);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, -5, 5, 5 }, 20, &area, &offset) == 0);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, 0, 5 }, 0, &area, &offset) == 0);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, 5, 0 }, 20, &area, &offset) == 0);
	// refused: a pitch under the width, negative sizes, out of range
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, 5, 5 }, 19, &area, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, 5, 5 }, -20, &area, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, -1, 5 }, 20, &area, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, 5, -1 }, 20, &area, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, 1 << 30, 1 }, INT32_MAX, &area, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ INT32_MIN, 0, 5, 5 }, 20, &area, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, INT32_MAX, 5, 5 }, 20, &area, &offset) == -1);
	// the widest rect pitch can hold, with no overflow
	const int far = DRAW_COORD_MAX - 1;
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ -far, -far, DRAW_COORD_MAX, DRAW_COORD_MAX }, INT32_MAX, &area, &offset) == 1);
	CHECK(area.w == 1 && area.h == 1 && offset == (size_t)far * INT32_MAX + (size_t)far * 4);
	CHECK(Draw_writeArea(40, 20, &(MosaicRect){ 0, 0, DRAW_COORD_MAX + 1, 1 }, INT32_MAX, &area, &offset) == -1);
	CHECK(Draw_writeArea(0, 20, NULL, 0, &area, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, NULL, 160, NULL, &offset) == -1);
	CHECK(Draw_writeArea(40, 20, NULL, 160, &area, NULL) == -1);
}


static void testColor(void) {
	MosaicColor c = mosaic_color(0x9B2257, 128);
	CHECK(c.r == 0x9B && c.g == 0x22 && c.b == 0x57 && c.a == 128);
}

int main(void) {
	testOwners();
	testReadOnly();
	testBudget();
	testGive();
	testMany();
	testTruncate();
	testRound();
	testFit();
	testColor();
	testMine();
	testWriteArea();
	CHECK_DONE();
}
