// Plugins' quick menu tiles: the icon each tile draws, as quick_tile_icon
// gives it.

#include "check.h"
#include "launcher.h"
#include "quicktiles.h"
#include "stubs.h"
#include <sys/stat.h>

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

// A row of built-in tiles by name, as the quick menu lists them.
static Array* builtins(const char* first, const char* second) {
	Array* row = Array_new();
	Array_push(row, Entry_newNamed("/quick/first", ENTRY_DIP, (char*)first));
	Array_push(row, Entry_newNamed("/quick/second", ENTRY_DIP, (char*)second));
	return row;
}

static void freeRow(Array* row) {
	for (int i = 0; i < row->count; i++) Entry_free(row->items[i]);
	Array_free(row);
}

// Whole rows and the top row's labels, as hide_quick_row and
// hide_quick_labels set them.
static void testRows(void) {
	QuickTiles_reset();
	CHECK(QuickTiles_add("test.plain", QUICK_ROW_TOP, NULL, NULL, NULL) == 0);
	CHECK(QuickTiles_add("test.toggle", QUICK_ROW_BOTTOM, NULL, NULL, isOn) == 0);
	CHECK(!QuickTiles_rowHidden(QUICK_ROW_TOP) && !QuickTiles_rowHidden(QUICK_ROW_BOTTOM) && !QuickTiles_labelsHidden());

	// shown: the built-in tiles, then the plugin's
	Array* top = builtins("Recents", "Games");
	QuickTiles_apply(top, QUICK_ROW_TOP);
	CHECK(top->count == 3);
	freeRow(top);

	// the top row hidden: none of its tiles, plugins' too; the bottom row stays
	CHECK(QuickTiles_hideRow(QUICK_ROW_TOP, 1) == 0);
	CHECK(QuickTiles_rowHidden(QUICK_ROW_TOP));
	top = builtins("Recents", "Games");
	QuickTiles_apply(top, QUICK_ROW_TOP);
	CHECK(top->count == 0);
	freeRow(top);
	Array* bottom = builtins("Settings", "Sleep");
	QuickTiles_apply(bottom, QUICK_ROW_BOTTOM);
	CHECK(bottom->count == 3);
	freeRow(bottom);

	// saved and put back with the rest, and shown again with 0
	QuickTilesSaved* saved = QuickTiles_save();
	CHECK(QuickTiles_hideRow(QUICK_ROW_TOP, 0) == 0);
	CHECK(!QuickTiles_rowHidden(QUICK_ROW_TOP));
	QuickTiles_restore(saved);
	CHECK(QuickTiles_rowHidden(QUICK_ROW_TOP));

	// bad rows and values
	CHECK(QuickTiles_hideRow(2, 1) == -1);
	CHECK(QuickTiles_hideRow(-1, 1) == -1);
	CHECK(QuickTiles_hideRow(QUICK_ROW_BOTTOM, 2) == -1);
	CHECK(!QuickTiles_rowHidden(2));

	QuickTiles_hideLabels(1);
	CHECK(QuickTiles_labelsHidden());
	// a reset shows everything again
	QuickTiles_reset();
	CHECK(!QuickTiles_rowHidden(QUICK_ROW_TOP) && !QuickTiles_labelsHidden());
}

static void clearCard(void) {
	CHECK(system("rm -rf " TEST_SDCARD_PATH) == 0);
}

// A theme's .media/quick_<action id>.png wins over the plugin's icon, and
// its _off one while the tile is off, falling back file by file.
static void testThemeFirst(void) {
	char out[MAX_PATH];
	clearCard();
	mkdir(TEST_SDCARD_PATH, 0755);
	mkdir(TEST_SDCARD_PATH "/.media", 0755);
	on = 1;
	CHECK(QuickTiles_add("test.icon", QUICK_ROW_TOP, "/icons/star.png", NULL, NULL) == 0);
	CHECK(QuickTiles_add("test.plain", QUICK_ROW_TOP, NULL, NULL, NULL) == 0);
	CHECK(QuickTiles_add("test.toggle", QUICK_ROW_BOTTOM, "/icons/on.png", "/icons/off.png", isOn) == 0);

	// no theme files: the plugin's
	CHECK(QuickTiles_icon("test.icon", out, sizeof(out)) == 0);
	CHECK_STR(out, "/icons/star.png");

	// the theme's, for a tile with an icon and one without
	touch(TEST_SDCARD_PATH "/.media/quick_test.icon.png");
	touch(TEST_SDCARD_PATH "/.media/quick_test.plain.png");
	CHECK(QuickTiles_icon("test.icon", out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/.media/quick_test.icon.png");
	CHECK(QuickTiles_icon("test.plain", out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/.media/quick_test.plain.png");

	// a toggle with only the theme's on icon: on shows it, off the plugin's icon_off
	touch(TEST_SDCARD_PATH "/.media/quick_test.toggle.png");
	CHECK(QuickTiles_icon("test.toggle", out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/.media/quick_test.toggle.png");
	on = 0;
	CHECK(QuickTiles_icon("test.toggle", out, sizeof(out)) == 0);
	CHECK_STR(out, "/icons/off.png");
	// and the theme's off icon once it has one
	touch(TEST_SDCARD_PATH "/.media/quick_test.toggle_off.png");
	CHECK(QuickTiles_icon("test.toggle", out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/.media/quick_test.toggle_off.png");
	on = 1;
	CHECK(QuickTiles_icon("test.toggle", out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/.media/quick_test.toggle.png");

	// a theme path too long for out
	char small[16];
	CHECK(QuickTiles_icon("test.icon", small, sizeof(small)) == -1);
	CHECK_STR(small, "");

	// an off toggle without an icon_off falls back to the on icons
	unlink(TEST_SDCARD_PATH "/.media/quick_test.toggle_off.png");
	unlink(TEST_SDCARD_PATH "/.media/quick_test.toggle.png");
	CHECK(QuickTiles_add("test.toggle", QUICK_ROW_BOTTOM, "/icons/on.png", NULL, isOn) == 0);
	on = 0;
	CHECK(QuickTiles_icon("test.toggle", out, sizeof(out)) == 0);
	CHECK_STR(out, "/icons/on.png");
	on = 1;
	QuickTiles_reset();
	clearCard();
}

// A plugin's icon path: full paths as they are, relative ones in its res
// folder with the scale's file first.
static void testIconPath(void) {
	char out[MAX_PATH];
	const char* res = TEST_SDCARD_PATH "/res";
	clearCard();
	mkdir(TEST_SDCARD_PATH, 0755);
	mkdir(TEST_SDCARD_PATH "/res", 0755);
	mkdir(TEST_SDCARD_PATH "/res/quick", 0755);
	touch(TEST_SDCARD_PATH "/res/quick/star@2x.png");

	CHECK(QuickTiles_iconPath(res, "/icons/star.png", 2, out, sizeof(out)) == 0);
	CHECK_STR(out, "/icons/star.png");
	CHECK(QuickTiles_iconPath(res, "quick/star.png", 2, out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/res/quick/star@2x.png");
	// no file for this scale: the name as given
	CHECK(QuickTiles_iconPath(res, "quick/star.png", 3, out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/res/quick/star.png");
	CHECK(QuickTiles_iconPath(res, "quick/star.jpg", 2, out, sizeof(out)) == 0);
	CHECK_STR(out, TEST_SDCARD_PATH "/res/quick/star.jpg");
	// no res folder, no icon, and too small an out
	CHECK(QuickTiles_iconPath(NULL, "quick/star.png", 2, out, sizeof(out)) == 0);
	CHECK_STR(out, "quick/star.png");
	snprintf(out, sizeof(out), "%s", "junk");
	CHECK(QuickTiles_iconPath(res, NULL, 2, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(QuickTiles_iconPath(res, "", 2, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	char small[8];
	CHECK(QuickTiles_iconPath(res, "quick/star.png", 2, small, sizeof(small)) == -1);
	CHECK_STR(small, "");
	CHECK(QuickTiles_iconPath(res, "quick/star.png", 2, NULL, 8) == -1);
	clearCard();
}

int main(void) {
	testIcon();
	testRows();
	testThemeFirst();
	testIconPath();
	CHECK_DONE();
}
