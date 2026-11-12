// hintmodel.c: checking plugins' button hints, the names stock draws for
// their buttons, and where stock's GFX_blitButtonGroup puts the pill.

#include "check.h"
#include "hintmodel.h"

static void testGlyphs(void) {
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_A, NULL), "A");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_B, NULL), "B");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_X, NULL), "X");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_Y, NULL), "Y");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_L1, NULL), "L1");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_R1, NULL), "R1");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_L2, NULL), "L2");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_R2, NULL), "R2");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_START, NULL), "START");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_SELECT, NULL), "SELECT");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_MENU, NULL), "MENU");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_POWER, NULL), "POWER");
	// stock draws no hint for the d-pad, and FN buttons differ by device
	CHECK(Hint_glyph(MOSAIC_BUTTON_UP, NULL) == NULL);
	CHECK(Hint_glyph(MOSAIC_BUTTON_DOWN, NULL) == NULL);
	CHECK(Hint_glyph(MOSAIC_BUTTON_LEFT, NULL) == NULL);
	CHECK(Hint_glyph(MOSAIC_BUTTON_RIGHT, NULL) == NULL);
	CHECK(Hint_glyph(MOSAIC_BUTTON_FN1, NULL) == NULL);
	CHECK(Hint_glyph(MOSAIC_BUTTON_FN3, NULL) == NULL);
	CHECK(Hint_glyph(-1, NULL) == NULL);
	CHECK(Hint_glyph(99, NULL) == NULL);

	// by the device's names, where it has them: HOME alone on the tg5050
	const char* tg5050[] = { "", "", "HOME" };
	CHECK(Hint_glyph(MOSAIC_BUTTON_FN1, tg5050) == NULL);
	CHECK(Hint_glyph(MOSAIC_BUTTON_FN2, tg5050) == NULL);
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_FN3, tg5050), "HOME");
	const char* brick[] = { "L3", "R3", NULL };
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_FN1, brick), "L3");
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_FN2, brick), "R3");
	CHECK(Hint_glyph(MOSAIC_BUTTON_FN3, brick) == NULL);
	CHECK_STR(Hint_glyph(MOSAIC_BUTTON_A, brick), "A");
	CHECK(Hint_glyph(MOSAIC_BUTTON_UP, brick) == NULL);
	CHECK(Hint_glyph(99, brick) == NULL);
}

static void testCheck(void) {
	const char* names[HINTS_MAX];
	// the stock switcher's
	MosaicHint back[] = { { MOSAIC_BUTTON_B, "BACK" } };
	MosaicHint resume[] = { { MOSAIC_BUTTON_Y, "REMOVE" }, { MOSAIC_BUTTON_A, "RESUME" } };
	MosaicHint open[] = { { MOSAIC_BUTTON_A, "OPEN" } };
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, back, 1, NULL, names) == 0);
	CHECK_STR(names[0], "B");
	CHECK(Hints_check(MOSAIC_HINTS_RIGHT, resume, 2, NULL, names) == 0);
	CHECK_STR(names[0], "Y");
	CHECK_STR(names[1], "A");
	CHECK(Hints_check(MOSAIC_HINTS_RIGHT, open, 1, NULL, names) == 0);
	CHECK(Hints_check(MOSAIC_HINTS_RIGHT, back, 1, NULL, names) == 0);
	MosaicHint sleep[] = { { MOSAIC_BUTTON_POWER, "SLEEP" }, { MOSAIC_BUTTON_MENU, "INFO" } };
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, sleep, 2, NULL, names) == 0);
	CHECK_STR(names[0], "POWER");
	CHECK_STR(names[1], "MENU");

	// groups and counts
	CHECK(Hints_check(-1, back, 1, NULL, names) == -1);
	CHECK(Hints_check(2, back, 1, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, back, 0, NULL, names) == -1);
	MosaicHint three[] = { { MOSAIC_BUTTON_A, "A" }, { MOSAIC_BUTTON_B, "B" }, { MOSAIC_BUTTON_X, "X" } };
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, three, 3, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, NULL, 1, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, back, 1, NULL, NULL) == -1);

	// buttons and labels
	MosaicHint dpad[] = { { MOSAIC_BUTTON_UP, "UP" } };
	MosaicHint fn[] = { { MOSAIC_BUTTON_FN2, "FN" } };
	MosaicHint none[] = { { MOSAIC_BUTTON_A, NULL } };
	MosaicHint empty[] = { { MOSAIC_BUTTON_A, "" } };
	MosaicHint blank[] = { { MOSAIC_BUTTON_A, "\nOPEN" } };
	MosaicHint second[] = { { MOSAIC_BUTTON_A, "OPEN" }, { MOSAIC_BUTTON_FN1, "FN" } };
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, dpad, 1, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, fn, 1, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, none, 1, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, empty, 1, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, blank, 1, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_RIGHT, second, 2, NULL, names) == -1);

	// FN buttons on a device that has them
	const char* tg5050[] = { "", "", "HOME" };
	MosaicHint home[] = { { MOSAIC_BUTTON_FN3, "REMAP" }, { MOSAIC_BUTTON_A, "OPEN" } };
	CHECK(Hints_check(MOSAIC_HINTS_RIGHT, home, 2, tg5050, names) == 0);
	CHECK_STR(names[0], "HOME");
	CHECK_STR(names[1], "A");
	CHECK(Hints_check(MOSAIC_HINTS_RIGHT, home, 2, NULL, names) == -1);
	CHECK(Hints_check(MOSAIC_HINTS_LEFT, fn, 1, tg5050, names) == -1);
}

static void testPrimary(void) {
	// as stock's screens pass it: the left group's first, the right's last
	CHECK(Hints_primary(MOSAIC_HINTS_LEFT, 1) == 0);
	CHECK(Hints_primary(MOSAIC_HINTS_LEFT, 2) == 0);
	CHECK(Hints_primary(MOSAIC_HINTS_RIGHT, 1) == 0);
	CHECK(Hints_primary(MOSAIC_HINTS_RIGHT, 2) == 1);
}

static void testLabel(void) {
	char out[HINT_LABEL_MAX];
	CHECK(Hints_label("BACK", out, sizeof(out)) == 4);
	CHECK_STR(out, "BACK");
	// one line
	CHECK(Hints_label("OPEN\nNOW", out, sizeof(out)) == 4);
	CHECK_STR(out, "OPEN");
	CHECK(Hints_label("\nOPEN", out, sizeof(out)) == -1);
	CHECK(Hints_label("", out, sizeof(out)) == -1);
	CHECK(Hints_label(NULL, out, sizeof(out)) == -1);
	CHECK(Hints_label("A", NULL, 4) == -1);
	CHECK(Hints_label("A", out, 1) == -1);
	// cut to fit, between UTF-8 characters
	CHECK(Hints_label("ABCDEF", out, 4) == 3);
	CHECK_STR(out, "ABC");
	CHECK(Hints_label("AB\xC3\xA9", out, 4) == 2); // "ABé" needs 5 bytes with its terminator
	CHECK_STR(out, "AB");
	CHECK(Hints_label("\xC3\xA9", out, 2) == -1);
	char longest[HINT_LABEL_MAX * 2];
	memset(longest, 'X', sizeof(longest) - 1);
	longest[sizeof(longest) - 1] = '\0';
	CHECK(Hints_label(longest, out, sizeof(out)) == HINT_LABEL_MAX - 1);
}

static void testPlace(void) {
	// desktop: 640x480 at scale 2, so PADDING 20, PILL_SIZE 60, BUTTON_MARGIN 10
	HintMetrics m = { 20, 60, 10 };
	MosaicRect r;
	int one[] = { 100 };
	int two[] = { 100, 80 };
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, &m, one, 1, &r) == 0);
	CHECK(r.x == 20 && r.y == 400 && r.w == 120 && r.h == 60);
	CHECK(Hints_place(MOSAIC_HINTS_RIGHT, 640, 480, &m, one, 1, &r) == 0);
	CHECK(r.x == 500 && r.y == 400 && r.w == 120 && r.h == 60);
	CHECK(Hints_place(MOSAIC_HINTS_RIGHT, 640, 480, &m, two, 2, &r) == 0);
	CHECK(r.x == 640 - 20 - 210 && r.y == 400 && r.w == 210 && r.h == 60);
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, &m, two, 2, &r) == 0);
	CHECK(r.x == 20 && r.w == 210);

	CHECK(Hints_place(3, 640, 480, &m, one, 1, &r) == -1);
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, &m, one, 0, &r) == -1);
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, &m, one, 3, &r) == -1);
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 0, 480, &m, one, 1, &r) == -1);
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, NULL, one, 1, &r) == -1);
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, &m, NULL, 1, &r) == -1);
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, &m, one, 1, NULL) == -1);
	int negative[] = { -1 };
	CHECK(Hints_place(MOSAIC_HINTS_LEFT, 640, 480, &m, negative, 1, &r) == -1);
}

static void testClear(void) {
	MosaicRect status = { 500, 20, 120, 60 };
	MosaicRect header = { 380, 20, 110, 60 };
	MosaicRect none = { 0, 0, 0, 0 };
	MosaicRect pill = { 20, 400, 120, 60 };
	MosaicRect header_bar[] = { status, header };
	MosaicRect empty[] = { none, none };
	CHECK(Hints_clear(&pill, 640, 480, header_bar, 2) == 1);
	CHECK(Hints_clear(&pill, 640, 480, empty, 2) == 1);
	CHECK(Hints_clear(&pill, 640, 480, NULL, 0) == 1);
	CHECK(Hints_clear(&pill, 640, 480, NULL, 1) == 0);
	CHECK(Hints_clear(&pill, 640, 480, header_bar, -1) == 0);
	// a screen too short for both
	MosaicRect low = { 520, 50, 100, 60 };
	CHECK(Hints_clear(&low, 640, 120, header_bar, 2) == 0);
	MosaicRect under_header = { 400, 70, 50, 60 };
	MosaicRect just_header[] = { none, header };
	CHECK(Hints_clear(&under_header, 640, 140, just_header, 2) == 0);
	CHECK(Hints_clear(&under_header, 640, 140, empty, 2) == 1);
	// the other group's pill, such as two long groups on a narrow screen
	MosaicRect right = { 100, 400, 520, 60 };
	MosaicRect taken[] = { status, header, right };
	CHECK(Hints_clear(&pill, 640, 480, taken, 3) == 0);
	MosaicRect beside = { 140, 400, 480, 60 };
	taken[2] = beside;
	CHECK(Hints_clear(&pill, 640, 480, taken, 3) == 1);
	// the same group again
	taken[2] = pill;
	CHECK(Hints_clear(&pill, 640, 480, taken, 3) == 0);
	// past the screen, such as a pill wider than it
	MosaicRect wide = { -100, 400, 840, 60 };
	CHECK(Hints_clear(&wide, 640, 480, empty, 2) == 0);
	MosaicRect off = { 600, 400, 120, 60 };
	CHECK(Hints_clear(&off, 640, 480, empty, 2) == 0);
	MosaicRect below = { 20, 440, 120, 60 };
	CHECK(Hints_clear(&below, 640, 480, empty, 2) == 0);
	CHECK(Hints_clear(NULL, 640, 480, empty, 2) == 0);
	MosaicRect flat = { 20, 400, 0, 60 };
	CHECK(Hints_clear(&flat, 640, 480, empty, 2) == 0);
}

int main(void) {
	testGlyphs();
	testCheck();
	testPrimary();
	testLabel();
	testPlace();
	testClear();
	CHECK_DONE();
}
