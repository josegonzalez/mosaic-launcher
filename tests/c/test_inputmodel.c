// inputmodel.c: taking what input filters consumed off the pad, and a long
// power press getting through whatever they take. With layoutmodel.c's
// button maps, as plugins.c builds a filter's input.

#include "check.h"
#include "inputmodel.h"
#include "layoutmodel.h"
#include "mosaic_plugin.h"

// pad bits, as stock's are: one per button
#define PAD_A 0x1
#define PAD_B 0x2
#define PAD_UP 0x4
#define PAD_MENU 0x8
#define PAD_POWER 0x10
#define PAD_FN1 0x20

static const LayoutButton map[] = {
	{ MOSAIC_BUTTON_A, PAD_A },
	{ MOSAIC_BUTTON_B, PAD_B },
	{ MOSAIC_BUTTON_UP, PAD_UP },
	{ MOSAIC_BUTTON_MENU, PAD_MENU },
	{ MOSAIC_BUTTON_POWER, PAD_POWER },
	{ MOSAIC_BUTTON_FN1, PAD_FN1 },
	{ MOSAIC_BUTTON_FN2, 0 }, // a device without it: stock's BTN_NONE
};
#define COUNT (int)(sizeof(map) / sizeof(map[0]))

static void testEnum(void) {
	// appended after the d-pad, so older plugins' values stay put
	CHECK(MOSAIC_BUTTON_RIGHT == 13);
	CHECK(MOSAIC_BUTTON_MENU == 14);
	CHECK(MOSAIC_BUTTON_POWER == 15);
	CHECK(MOSAIC_BUTTON_FN1 == 16);
	CHECK(MOSAIC_BUTTON_FN2 == 17);
	CHECK(MOSAIC_BUTTON_FN3 == 18);
	CHECK(MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_FN3) == 1u << 18);
}

static void testTake(void) {
	int pressed = PAD_A | PAD_MENU, repeated = PAD_A | PAD_UP, released = PAD_B | PAD_MENU;
	InputMask_take(&pressed, &repeated, &released, PAD_A | PAD_MENU);
	CHECK(pressed == 0);
	CHECK(repeated == PAD_UP);
	CHECK(released == PAD_B);
	// nothing taken changes nothing
	InputMask_take(&pressed, &repeated, &released, 0);
	CHECK(pressed == 0 && repeated == PAD_UP && released == PAD_B);
}

// What a filter sees and what its consumed takes, with the new buttons.
static void testMap(void) {
	MosaicInput in;
	LayoutInput_fill(&in, map, COUNT, PAD_MENU | PAD_FN1, 0, PAD_POWER, PAD_MENU | PAD_FN1 | PAD_UP);
	CHECK(in.size == sizeof(MosaicInput));
	CHECK(in.pressed == (MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_MENU) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_FN1)));
	CHECK(in.repeated == in.pressed); // stock's repeat starts on the press
	CHECK(in.released == MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER));
	CHECK(in.held == (in.pressed | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)));
	// a button the device doesn't have is never pressed, and taking it takes nothing
	CHECK(!(in.held & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_FN2)));
	CHECK(LayoutInput_padBits(map, COUNT, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_FN2)) == 0);
	// everything a lock takes; bits past the map, such as FN3 here, are left out
	CHECK(LayoutInput_padBits(map, COUNT, ~0u) == (PAD_A | PAD_B | PAD_UP | PAD_MENU | PAD_POWER | PAD_FN1));
	CHECK(LayoutInput_padBits(map, COUNT, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_FN3)) == 0);
}

// step: one frame of the power button, returning whether it powers off
static PowerHold hold;
static int taken;
static int step(unsigned long now, int pressed, int released, int held, int took) {
	taken = took;
	return PowerHold_step(&hold, now, PAD_POWER, pressed, released, held, &taken);
}

static void testLongPressTaken(void) {
	hold = (PowerHold){ 0, 0 };
	// a filter takes the press and keeps taking power
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(taken == PAD_POWER);
	CHECK(step(1500, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(1999, 0, 0, PAD_POWER, PAD_POWER) == 0);
	// held a second: powers off, once
	CHECK(step(2000, 0, 0, PAD_POWER, PAD_POWER) == 1);
	CHECK(step(2016, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(3000, 0, PAD_POWER, 0, PAD_POWER) == 0);
	CHECK(taken == PAD_POWER); // its release stays taken, so it doesn't sleep
}

static void testShortPressTaken(void) {
	hold = (PowerHold){ 0, 0 };
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(1300, 0, PAD_POWER, 0, PAD_POWER) == 0);
	CHECK(taken == PAD_POWER);
	// nothing left timing
	CHECK(step(2500, 0, 0, 0, 0) == 0);
	CHECK(hold.taken_at == 0 && hold.passed == 0);
}

// A filter that takes only the press still powers off with it held, taking
// nothing after.
static void testPressOnlyTaken(void) {
	hold = (PowerHold){ 0, 0 };
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(2000, 0, 0, PAD_POWER, 0) == 1);
}

// A press that got through is stock's to time: a filter taking its release
// would leave stock powering off a second after the press, so the release
// goes through.
static void testReleaseOfPassedPress(void) {
	hold = (PowerHold){ 0, 0 };
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, 0) == 0);
	CHECK(hold.passed == 1);
	CHECK(step(1200, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(taken == PAD_POWER); // held frames don't matter to stock
	CHECK(step(1300, 0, PAD_POWER, 0, PAD_POWER | PAD_A) == 0);
	CHECK(taken == PAD_A);
	// and the host never powers off for it: that's stock's own
	hold = (PowerHold){ 0, 0 };
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, 0) == 0);
	CHECK(step(2500, 0, 0, PAD_POWER, PAD_POWER) == 0);
}

// Other buttons and an untouched power button change nothing.
static void testOthers(void) {
	hold = (PowerHold){ 0, 0 };
	CHECK(step(1000, PAD_A, PAD_B, PAD_A, PAD_A | PAD_B) == 0);
	CHECK(taken == (PAD_A | PAD_B));
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, 0) == 0);
	CHECK(step(1300, 0, PAD_POWER, 0, 0) == 0);
	CHECK(taken == 0);
}

// Up without a release, as after a sleep resets the pad, stops the timing.
static void testReset(void) {
	hold = (PowerHold){ 0, 0 };
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(1100, 0, 0, 0, 0) == 0);
	CHECK(step(2500, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(hold.taken_at == 0);
}

// Pressed and released in one frame is a tap.
static void testTap(void) {
	hold = (PowerHold){ 0, 0 };
	CHECK(step(1000, PAD_POWER, PAD_POWER, 0, PAD_POWER) == 0);
	CHECK(taken == PAD_POWER);
	CHECK(hold.taken_at == 0 && hold.passed == 0);
}

// At time 0 (a wake just before the 32-bit tick count wrapped), and with
// the clock going backwards, it never powers off early.
static void testClock(void) {
	hold = (PowerHold){ 0, 0, 0 };
	PowerHold_woke(&hold, 4294966000UL);
	CHECK(step(0, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(hold.taken_at != 0);
	CHECK(step(999, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(1001, 0, 0, PAD_POWER, PAD_POWER) == 1);
	hold = (PowerHold){ 0, 0 };
	CHECK(step(5000, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(10, 0, 0, PAD_POWER, PAD_POWER) == 0);
}

// As stock, a taken press in the clock's first second isn't timed, however
// long it's held, and its release stays taken, so it doesn't sleep either.
static void testStart(void) {
	hold = (PowerHold){ 0, 0, 0 };
	CHECK(step(500, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(hold.taken_at == 0 && hold.passed == 0);
	CHECK(step(1500, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(3000, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(3100, 0, PAD_POWER, 0, PAD_POWER) == 0);
	CHECK(taken == PAD_POWER);
	CHECK(step(999, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(2500, 0, 0, PAD_POWER, PAD_POWER) == 0);
	// from a second on it's timed
	hold = (PowerHold){ 0, 0, 0 };
	CHECK(step(1000, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(2000, 0, 0, PAD_POWER, PAD_POWER) == 1);
}

// And in the first second after a wake.
static void testWoke(void) {
	hold = (PowerHold){ 0, 0, 0 };
	PowerHold_woke(&hold, 60000);
	CHECK(step(60200, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(61200, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(62500, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(62600, 0, PAD_POWER, 0, PAD_POWER) == 0);
	CHECK(taken == PAD_POWER);
	CHECK(step(60999, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(62000, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(62100, 0, 0, 0, 0) == 0);
	CHECK(step(61000, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(61999, 0, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(step(62000, 0, 0, PAD_POWER, PAD_POWER) == 1);
	// a press that gets through is stock's, which ignores it itself
	CHECK(step(62100, 0, PAD_POWER, 0, PAD_POWER) == 0);
	PowerHold_woke(&hold, 70000);
	CHECK(step(70100, PAD_POWER, 0, PAD_POWER, 0) == 0);
	CHECK(hold.passed == 1);
	CHECK(step(70300, 0, PAD_POWER, 0, PAD_POWER) == 0);
	CHECK(taken == 0);
	// a wake just before the 32-bit tick count wrapped
	hold = (PowerHold){ 0, 0, 0 };
	PowerHold_woke(&hold, 4294967000UL);
	CHECK(step(200, PAD_POWER, 0, PAD_POWER, PAD_POWER) == 0);
	CHECK(hold.taken_at == 0);
	CHECK(step(1500, 0, 0, PAD_POWER, PAD_POWER) == 0);
}

int main(void) {
	testEnum();
	testTake();
	testMap();
	testLongPressTaken();
	testShortPressTaken();
	testPressOnlyTaken();
	testReleaseOfPassedPress();
	testOthers();
	testReset();
	testTap();
	testClock();
	testStart();
	testWoke();
	CHECK_DONE();
}
