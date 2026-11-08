// layoutmodel.c: the layout slot's state, the buttons a layout sees and
// consumes, and where it draws, and where row decorators and header widgets
// draw, and where the selection is for transitions.

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

static void testClip(void) {
	MosaicRect area = { 0, 0, 1024, 648 }, out;
	// inside is kept whole
	CHECK(LayoutRect_clip(&(MosaicRect){ 10, 20, 100, 50 }, &area, &out) == 1);
	CHECK(out.x == 10 && out.y == 20 && out.w == 100 && out.h == 50);
	// across an edge is cut to it
	CHECK(LayoutRect_clip(&(MosaicRect){ -10, 600, 100, 100 }, &area, &out) == 1);
	CHECK(out.x == 0 && out.y == 600 && out.w == 90 && out.h == 48);
	// either way round
	CHECK(LayoutRect_clip(&area, &(MosaicRect){ 1000, -5, 100, 10 }, &out) == 1);
	CHECK(out.x == 1000 && out.y == 0 && out.w == 24 && out.h == 5);
	// outside, touching or empty is nothing
	CHECK(LayoutRect_clip(&(MosaicRect){ 0, 648, 100, 10 }, &area, &out) == 0);
	CHECK(out.x == 0 && out.y == 0 && out.w == 0 && out.h == 0);
	CHECK(LayoutRect_clip(&(MosaicRect){ 2000, 0, 10, 10 }, &area, &out) == 0);
	CHECK(LayoutRect_clip(&(MosaicRect){ 10, 10, 0, 10 }, &area, &out) == 0);
	CHECK(LayoutRect_clip(&(MosaicRect){ 10, 10, 10, -1 }, &area, &out) == 0);
	// sizes near the edge of an int don't wrap
	CHECK(LayoutRect_clip(&(MosaicRect){ 2147483000, 0, 2147483000, 10 }, &(MosaicRect){ 0, 0, 2147483647, 10 }, &out) == 1);
	CHECK(out.x == 2147483000 && out.w == 647);
}

static void testListRow(void) {
	MosaicRow row;
	// the built-in list at scale 3: margin 15, padding 30, rows 90 tall
	ListRow_rects(0, 15, 30, 90, 600, 200, 1, &row);
	CHECK(row.rect.x == 15 && row.rect.y == 30 && row.rect.w == 600 && row.rect.h == 90);
	CHECK(row.name.x == 15 && row.name.y == 30 && row.name.w == 200 && row.name.h == 90);
	ListRow_rects(3, 15, 30, 90, 600, 200, 1, &row);
	CHECK(row.rect.y == 30 + 3 * 90 && row.name.y == row.rect.y);
	// no names, no name
	ListRow_rects(1, 15, 30, 90, 600, 200, 0, &row);
	CHECK(row.rect.w == 600 && row.name.w == 0);
	// the name never runs past the row, nor below nothing
	ListRow_rects(1, 15, 30, 90, 100, 200, 1, &row);
	CHECK(row.name.w == 100);
	ListRow_rects(1, 15, 30, 90, -4, 200, 1, &row);
	CHECK(row.rect.w == 0 && row.name.w == 0);
}

static void testSelection(void) {
	ListSelection sel;
	// the built-in list's selected row and the pill on it, as decorators get them
	ListSelection_row(&sel, 2, 15, 30, 90, 600, 200, 1);
	CHECK(sel.row.x == 15 && sel.row.y == 30 + 2 * 90 && sel.row.w == 600 && sel.row.h == 90);
	CHECK(sel.pill.x == 15 && sel.pill.y == sel.row.y && sel.pill.w == 200 && sel.pill.h == 90);
	MosaicRow row;
	ListRow_rects(2, 15, 30, 90, 600, 200, 1, &row);
	CHECK(!memcmp(&sel.row, &row.rect, sizeof(sel.row)) && !memcmp(&sel.pill, &row.name, sizeof(sel.pill)));
	// the pill never runs past the row
	ListSelection_row(&sel, 0, 15, 30, 90, 100, 200, 1);
	CHECK(sel.row.w == 100 && sel.pill.w == 100);
	// with names hidden there's no pill, only the row
	ListSelection_row(&sel, 1, 15, 30, 90, 600, 200, 0);
	CHECK(sel.row.w == 600 && sel.pill.x == 0 && sel.pill.y == 0 && sel.pill.w == 0 && sel.pill.h == 0);
	// a row with no room is unknown
	ListSelection_row(&sel, 1, 15, 30, 90, -4, 200, 1);
	CHECK(sel.row.w == 0 && sel.row.x == 0 && sel.pill.w == 0);
	ListSelection_clear(&sel);
	CHECK(sel.row.w == 0 && sel.pill.w == 0);

	// a layout's cells: the selected one's part in the area, the last one winning
	MosaicRect area = { 0, 0, 1024, 648 };
	ListSelection_row(&sel, 1, 15, 30, 90, 600, 200, 1);
	ListSelection_cell(&sel, 3, 4, &(MosaicRect){ 10, 10, 100, 100 }, &area);
	CHECK(sel.row.y == 120 && sel.pill.w == 200);
	ListSelection_cell(&sel, 4, 4, &(MosaicRect){ 10, 10, 100, 100 }, &area);
	CHECK(sel.row.x == 10 && sel.row.y == 10 && sel.row.w == 100 && sel.row.h == 100 && sel.pill.w == 0);
	ListSelection_cell(&sel, 4, 4, &(MosaicRect){ 900, 600, 200, 100 }, &area);
	CHECK(sel.row.x == 900 && sel.row.y == 600 && sel.row.w == 124 && sel.row.h == 48);
	// one outside the area, or no rect, changes nothing
	ListSelection_cell(&sel, 4, 4, &(MosaicRect){ 2000, 0, 10, 10 }, &area);
	CHECK(sel.row.x == 900 && sel.row.w == 124);
	ListSelection_cell(&sel, 4, 4, NULL, &area);
	CHECK(sel.row.x == 900 && sel.row.w == 124);
}

static void testHeader(void) {
	// 1024 wide at scale 3: padding 30, gap 15, a 200 wide pill 90 tall
	HeaderRow row;
	HeaderRow_init(&row, 1024, 30, 15, 200, 90);
	int pill_x = 1024 - 30 - 200;
	CHECK(HeaderRow_width(&row) == 0);
	MosaicRect r;
	HeaderRow_rect(&row, &r);
	CHECK(r.w == 0 && r.h == 0);
	// half of what's left of the pill, less the gap to it
	int half = (pill_x - 30) / 2;
	CHECK(HeaderRow_room(&row) == half - 15);

	// right to left, the first next to the pill
	CHECK(HeaderRow_place(&row, 100, &r) == 1);
	CHECK(r.x == pill_x - 15 - 100 && r.y == 30 && r.w == 100 && r.h == 90);
	CHECK(HeaderRow_width(&row) == 115);
	CHECK(HeaderRow_place(&row, 50, &r) == 1);
	CHECK(r.x == pill_x - 15 - 100 - 15 - 50 && r.w == 50);
	CHECK(HeaderRow_width(&row) == 180);
	HeaderRow_rect(&row, &r);
	CHECK(r.x == pill_x - 180 && r.y == 30 && r.w == 180 && r.h == 90);

	// nothing asked, nothing placed
	CHECK(HeaderRow_place(&row, 0, &r) == 0);
	CHECK(HeaderRow_place(&row, -3, &r) == 0);
	CHECK(HeaderRow_width(&row) == 180);

	// too wide is cut to the room left, and then there's none
	int room = HeaderRow_room(&row);
	CHECK(room == half - 180 - 15);
	CHECK(HeaderRow_place(&row, 5000, &r) == 1);
	CHECK(r.w == room && r.x == pill_x - half);
	CHECK(HeaderRow_room(&row) == 0);
	CHECK(HeaderRow_place(&row, 10, &r) == 0);
	CHECK(HeaderRow_width(&row) == half);
	// so the pill and the left half of the header stay clear
	CHECK(r.x >= 30 + (pill_x - 30) / 2 - 1);

	// a wider pill (the brightness overlay) moves the widgets over with it
	HeaderRow_init(&row, 1024, 30, 15, 400, 90);
	CHECK(HeaderRow_place(&row, 100, &r) == 1);
	CHECK(r.x == 1024 - 30 - 400 - 15 - 100);

	// a screen too small for any
	HeaderRow_init(&row, 100, 30, 15, 60, 90);
	CHECK(HeaderRow_room(&row) == 0);
	CHECK(HeaderRow_place(&row, 10, &r) == 0);
}

static void testTopRow(void) {
	// stock's top row on 1024 wide at scale 3, with no thumbnail: the
	// margin, twice the padding and the pill come off the screen
	int stock = 1024 - 15 - 60 - 200;
	// with no widgets it's left as it is, thumbnail or not
	CHECK(HeaderRow_topRowWidth(stock, 1024, 30, 15, 200, 0) == stock);
	CHECK(HeaderRow_topRowWidth(400, 1024, 30, 15, 200, 0) == 400);
	CHECK(HeaderRow_topRowWidth(stock + 50, 1024, 30, 15, 200, -1) == stock + 50);
	// widgets take their width off it, keeping stock's gap from them
	CHECK(HeaderRow_topRowWidth(stock, 1024, 30, 15, 200, 115) == stock - 115);
	// a row already short of them, beside a thumbnail, is left alone
	CHECK(HeaderRow_topRowWidth(400, 1024, 30, 15, 200, 115) == 400);
	CHECK(HeaderRow_topRowWidth(stock, 1024, 30, 15, 200, 5000) == 0);
}

static void testBackground(void) {
	BackgroundSlot slot;
	BackgroundSlot_clear(&slot);
	CHECK(!BackgroundSlot_active(&slot));
	CHECK(BackgroundSlot_result(&slot, 0, 1) == 0);
	CHECK(BackgroundSlot_show(&slot, 1, 1) == BACKGROUND_STOCK);

	BackgroundSlot_claim(&slot, 3);
	CHECK(BackgroundSlot_active(&slot));
	// stock's until it has drawn
	CHECK(BackgroundSlot_show(&slot, 1, 1) == BACKGROUND_STOCK);
	CHECK(BackgroundSlot_result(&slot, 0, 1) == 1);
	CHECK(BackgroundSlot_show(&slot, 1, 1) == BACKGROUND_PLUGIN);
	// only on the game list
	CHECK(BackgroundSlot_show(&slot, 0, 1) == BACKGROUND_STOCK);
	// stock's background changed under it
	CHECK(BackgroundSlot_show(&slot, 1, 2) == BACKGROUND_REDRAW);
	CHECK(BackgroundSlot_result(&slot, 0, 2) == 1);
	CHECK(BackgroundSlot_show(&slot, 1, 2) == BACKGROUND_PLUGIN);
	// not asked this time
	BackgroundSlot_skip(&slot);
	CHECK(BackgroundSlot_show(&slot, 1, 2) == BACKGROUND_STOCK);
	CHECK(BackgroundSlot_active(&slot));

	// an error, any non-zero value, turns it off for good
	CHECK(BackgroundSlot_result(&slot, 0, 2) == 1);
	CHECK(BackgroundSlot_result(&slot, 5, 2) == 0);
	CHECK(!BackgroundSlot_active(&slot));
	CHECK(BackgroundSlot_show(&slot, 1, 2) == BACKGROUND_STOCK);
	CHECK(BackgroundSlot_result(&slot, 0, 2) == 0);
	CHECK(BackgroundSlot_show(&slot, 1, 2) == BACKGROUND_STOCK);

	// a new claim starts over
	BackgroundSlot_claim(&slot, 0);
	CHECK(BackgroundSlot_active(&slot));
	CHECK(BackgroundSlot_result(&slot, -1, 1) == 0);
	CHECK(!BackgroundSlot_active(&slot));
	BackgroundSlot_clear(&slot);
	CHECK(slot.owner == -1);
}

int main(void) {
	testSlot();
	testInput();
	testArea();
	testClip();
	testListRow();
	testSelection();
	testHeader();
	testTopRow();
	testBackground();
	CHECK_DONE();
}
