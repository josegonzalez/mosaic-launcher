// layoutmodel.c: the layout slot's state, the buttons a layout sees and
// consumes, and where it draws.

#include "check.h"
#include "layoutmodel.h"

static void testSlot(void) {
	LayoutSlot slot = { LAYOUT_NONE, -1 };
	CHECK(!LayoutSlot_active(&slot));
	CHECK(!LayoutSlot_needsInit(&slot));
	// nothing to turn on or off without an owner
	CHECK(LayoutSlot_result(&slot, 0) == 0);
	CHECK(slot.state == LAYOUT_NONE);

	// an owner without layout_render leaves the built-in list
	LayoutSlot_claim(&slot, 2, 0);
	CHECK(slot.state == LAYOUT_NONE);
	CHECK(!LayoutSlot_active(&slot));

	// waiting for init, then on
	LayoutSlot_claim(&slot, 2, 1);
	CHECK(slot.owner == 2);
	CHECK(LayoutSlot_active(&slot));
	CHECK(LayoutSlot_needsInit(&slot));
	CHECK(LayoutSlot_result(&slot, 0) == 1);
	CHECK(slot.state == LAYOUT_ON);
	CHECK(!LayoutSlot_needsInit(&slot));
	CHECK(LayoutSlot_result(&slot, 0) == 1);

	// any error turns it off for good
	CHECK(LayoutSlot_result(&slot, -1) == 0);
	CHECK(slot.state == LAYOUT_OFF);
	CHECK(!LayoutSlot_active(&slot));
	CHECK(LayoutSlot_result(&slot, 0) == 0);
	CHECK(slot.state == LAYOUT_OFF);

	// so does declining init, with any non-zero value
	LayoutSlot_claim(&slot, 0, 1);
	CHECK(LayoutSlot_result(&slot, 1) == 0);
	CHECK(slot.state == LAYOUT_OFF);
	CHECK(!LayoutSlot_needsInit(&slot));

	LayoutSlot_claim(&slot, 1, 1);
	LayoutSlot_clear(&slot);
	CHECK(slot.state == LAYOUT_NONE);
	CHECK(slot.owner == -1);
	CHECK(!LayoutSlot_active(&slot));
}

// pad bits as the platform might number them, not in MosaicButton's order
static const LayoutButton map[] = {
	{ MOSAIC_BUTTON_A, 1 << 8 },
	{ MOSAIC_BUTTON_B, 1 << 9 },
	{ MOSAIC_BUTTON_SELECT, 1 << 2 },
	{ MOSAIC_BUTTON_UP, 1 << 0 },
	{ MOSAIC_BUTTON_RIGHT, 1 << 3 },
	{ 40, 1 << 12 }, // past what a mask holds, so never seen
};
#define MAP_COUNT (int)(sizeof(map) / sizeof(map[0]))

static void testInput(void) {
	CHECK(LayoutInput_bits(map, MAP_COUNT, 0) == 0);
	CHECK(LayoutInput_bits(map, MAP_COUNT, 1 << 8) == MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A));
	CHECK(LayoutInput_bits(map, MAP_COUNT, (1 << 0) | (1 << 3)) == (MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT)));
	// pad bits the map doesn't have, such as MENU or power, aren't passed on
	CHECK(LayoutInput_bits(map, MAP_COUNT, (1 << 5) | (1 << 12)) == 0);

	CHECK(LayoutInput_padBits(map, MAP_COUNT, 0) == 0);
	CHECK(LayoutInput_padBits(map, MAP_COUNT, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) == ((1 << 0) | (1 << 9)));
	// a layout can only consume buttons it's shown
	CHECK(LayoutInput_padBits(map, MAP_COUNT, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X)) == 0);
	CHECK(LayoutInput_padBits(map, MAP_COUNT, 0xFFFFFFFFu) == ((1 << 8) | (1 << 9) | (1 << 2) | (1 << 0) | (1 << 3)));

	MosaicInput in;
	LayoutInput_fill(&in, map, MAP_COUNT, 1 << 8, 1 << 0, 1 << 9, (1 << 8) | (1 << 0));
	CHECK(in.size == sizeof(MosaicInput));
	CHECK(in.pressed == MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A));
	// a press counts as a repeat, as stock's d-pad moves on the press too
	CHECK(in.repeated == (MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A)));
	CHECK(in.released == MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B));
	CHECK(in.held == (MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)));

	// what a layout sees and what it gives back round-trip
	unsigned int bits = LayoutInput_bits(map, MAP_COUNT, (1 << 2) | (1 << 3));
	CHECK(LayoutInput_padBits(map, MAP_COUNT, bits) == ((1 << 2) | (1 << 3)));
}

static void testArea(void) {
	MosaicRect area, status;
	// stock's 1024x768 desktop at scale 3: hints 120 from the bottom, a 90 wide pill
	LayoutView_area(1024, 768, 768 - 120, 30, 90, 90, &area, &status);
	CHECK(area.x == 0 && area.y == 0 && area.w == 1024 && area.h == 648);
	CHECK(status.x == 1024 - 30 - 90 && status.y == 30 && status.w == 90 && status.h == 90);

	// no status pill
	LayoutView_area(640, 480, 400, 20, 0, 60, &area, &status);
	CHECK(area.w == 640 && area.h == 400);
	CHECK(status.x == 0 && status.y == 0 && status.w == 0 && status.h == 0);

	// hints past either edge are kept on the screen
	LayoutView_area(640, 480, 900, 20, 0, 60, &area, &status);
	CHECK(area.h == 480);
	LayoutView_area(640, 480, -5, 20, 0, 60, &area, &status);
	CHECK(area.h == 0);
}

int main(void) {
	testSlot();
	testInput();
	testArea();
	CHECK_DONE();
}
