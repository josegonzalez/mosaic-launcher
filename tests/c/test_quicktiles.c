// Plugins' quick menu tiles: the icon each tile draws, as quick_tile_icon
// gives it.

#include "check.h"
#include "launcher.h"
#include "quicktiles.h"
#include "stubs.h"

// What nav.c reaches beyond the listing code; none of it runs here.
int openRom(char* path, char* last) { (void)path; (void)last; return 0; }
int openPak(char* path) { (void)path; return 0; }
void readyResume(Entry* entry) { (void)entry; }
void QuickItem_activate(const Entry* entry) { (void)entry; }
void QuickMenu_refit(void) {}

static void run(const char* path, void* data) { (void)path; (void)data; }
static int on = 1;
static int isOn(void* data) { (void)data; return on; }

static const Action iconAction = { "test.icon", "Icon", NULL, run };
static const Action plainAction = { "test.plain", "Plain", NULL, run };
static const Action toggleAction = { "test.toggle", "Toggle", NULL, run };
static const Action untiledAction = { "test.untiled", "Untiled", NULL, run };

static void testIcon(void) {
	char out[64];
	CHECK(Actions_register(&iconAction) == 0);
	CHECK(Actions_register(&plainAction) == 0);
	CHECK(Actions_register(&toggleAction) == 0);
	CHECK(Actions_register(&untiledAction) == 0);
	CHECK(QuickTiles_add("test.icon", QUICK_ROW_TOP, "/icons/star.png", NULL, NULL) == 0);
	CHECK(QuickTiles_add("test.plain", QUICK_ROW_TOP, NULL, NULL, NULL) == 0);
	CHECK(QuickTiles_add("test.toggle", QUICK_ROW_BOTTOM, "/icons/on.png", "/icons/off.png", isOn) == 0);

	// a tile with an icon
	CHECK(QuickTiles_icon("test.icon", out, sizeof(out)) == 0);
	CHECK_STR(out, "/icons/star.png");
	// a tile without one
	snprintf(out, sizeof(out), "%s", "junk");
	CHECK(QuickTiles_icon("test.plain", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	// an on/off tile's icon for its state
	CHECK(QuickTiles_icon("test.toggle", out, sizeof(out)) == 0);
	CHECK_STR(out, "/icons/on.png");
	on = 0;
	CHECK(QuickTiles_icon("test.toggle", out, sizeof(out)) == 0);
	CHECK_STR(out, "/icons/off.png");
	// an action with no tile, and one that isn't there
	snprintf(out, sizeof(out), "%s", "junk");
	CHECK(QuickTiles_icon("test.untiled", out, sizeof(out)) == -1);
	CHECK_STR(out, "");
	CHECK(QuickTiles_icon("test.nope", out, sizeof(out)) == -1);
	CHECK(QuickTiles_icon(NULL, out, sizeof(out)) == -1);
	// a buffer too small for the path, and none
	char small[8];
	CHECK(QuickTiles_icon("test.icon", small, sizeof(small)) == -1);
	CHECK_STR(small, "");
	CHECK(QuickTiles_icon("test.icon", NULL, 64) == -1);
	CHECK(QuickTiles_icon("test.icon", out, 0) == -1);
	// gone once the tiles are reset
	QuickTiles_reset();
	CHECK(QuickTiles_icon("test.icon", out, sizeof(out)) == -1);
}

int main(void) {
	testIcon();
	CHECK_DONE();
}
