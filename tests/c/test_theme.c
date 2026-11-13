// thememodel.c: the theme slot's checks, the UI sizes worked out from a
// theme's, the contrast clamp, card paths and wallpaper spots.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "check.h"
#include "thememodel.h"

// stock's sizes on each device: screen height in layout units, PADDING and
// MAIN_ROW_COUNT from its platform.h (defines.h's for the default)
static const struct { const char* device; int height, padding, rows; } devices[] = {
	{ "default 640x480", 240, 10, 6 },
	{ "desktop 1024x768", 256, 5, 7 },
	{ "tg5040 1280x720", 360, 10, 10 },
	{ "tg5040 brick", 256, 5, 7 },
	{ "tg5050", 360, 10, 10 },
	{ "h700 640x480", 240, 10, 6 },
	{ "h700 720p", 360, 5, 10 },
};

static ThemeStock stockOf(int height, int padding, int rows) {
	ThemeStock stock = { height, 30, 20, padding, rows, 16, 14, 7 };
	return stock;
}

static MosaicTheme blank(void) {
	MosaicTheme theme;
	memset(&theme, 0, sizeof(theme));
	theme.size = sizeof(theme);
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) theme.colors[i] = MOSAIC_THEME_UNSET;
	return theme;
}

static void testStockFormulas(void) {
	for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
		// the rows formula gives every device's MAIN_ROW_COUNT from its PADDING
		if (ThemeModel_rows(devices[i].height, 30, devices[i].padding) != devices[i].rows)
			fprintf(stderr, "rows on %s\n", devices[i].device);
		CHECK(ThemeModel_rows(devices[i].height, 30, devices[i].padding) == devices[i].rows);
	}
	// the padding formula gives every stock PADDING but h700's at 720p
	CHECK(ThemeModel_padding(240, 30) == 10);
	CHECK(ThemeModel_padding(256, 30) == 5);
	CHECK(ThemeModel_padding(360, 30) == 10);
	// under the least, a third of the pill
	CHECK(ThemeModel_padding(256, 36) == 12); // 256 % 36 = 4
	CHECK(ThemeModel_padding(256, 32) == 10); // divides
	CHECK(ThemeModel_padding(256, 40) == 5);  // 16 left
	CHECK(ThemeModel_rows(256, 40, 5) == 5);
	CHECK(ThemeModel_rows(100, 60, 30) == 0);
	CHECK(ThemeModel_rows(100, 0, 0) == 0);
}

static void testResolve(void) {
	for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
		ThemeStock stock = stockOf(devices[i].height, devices[i].padding, devices[i].rows);
		// nothing set, or stock's own pill: stock's padding and rows, even on h700 at 720p
		ThemeSizes none = {0, 0, 0, 0};
		ThemeSizes same = {30, 0, 0, 0};
		CHECK(ThemeModel_pill(&none, &stock) == 30);
		CHECK(ThemeModel_button(&none, &stock) == 20);
		CHECK(ThemeModel_paddingFor(&none, &stock) == devices[i].padding);
		CHECK(ThemeModel_rowsFor(&none, &stock) == devices[i].rows);
		CHECK(ThemeModel_paddingFor(&same, &stock) == devices[i].padding);
		CHECK(ThemeModel_rowsFor(&same, &stock) == devices[i].rows);
	}
	ThemeStock desktop = stockOf(256, 5, 7);
	// a bigger pill works out its padding and rows
	ThemeSizes big = {40, 0, 0, 0};
	CHECK(ThemeModel_paddingFor(&big, &desktop) == 5);
	CHECK(ThemeModel_rowsFor(&big, &desktop) == 5);
	// a padding alone keeps the pill and counts the rows again
	ThemeSizes wide = {0, 0, 20, 0};
	CHECK(ThemeModel_paddingFor(&wide, &desktop) == 20);
	CHECK(ThemeModel_rowsFor(&wide, &desktop) == 6);
	ThemeSizes small = {24, 0, 0, 0};
	CHECK(ThemeModel_paddingFor(&small, &desktop) == 5); // 256 % 24 = 16
	CHECK(ThemeModel_rowsFor(&small, &desktop) == 9);
}

static void testCheckSizes(void) {
	ThemeStock desktop = stockOf(256, 5, 7);
	ThemeRefusals refused;

	// nothing set: nothing refused
	MosaicTheme theme = blank();
	memset(&refused, 0, sizeof(refused));
	ThemeSizes sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 0);
	CHECK(!sizes.pill && !sizes.button && !sizes.padding && !sizes.text_step);

	// good ones kept
	theme = blank();
	theme.pill_size = 36;
	theme.button_size = 24;
	theme.padding = 8;
	theme.text_step = 2;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 0);
	CHECK(sizes.pill == 36 && sizes.button == 24 && sizes.padding == 8 && sizes.text_step == 2);
	CHECK(ThemeModel_rowsFor(&sizes, &desktop) == 5);

	// out of range
	theme = blank();
	theme.pill_size = 200;
	theme.button_size = 4;
	theme.padding = -3;
	theme.text_step = 40;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 4);
	CHECK(!sizes.pill && !sizes.button && !sizes.padding && !sizes.text_step);
	CHECK(strstr(refused.lines[0], "pill_size 200") != NULL);

	// too few rows: the padding goes before the pill
	theme = blank();
	theme.pill_size = 50;
	theme.padding = 30;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 1);
	CHECK(sizes.pill == 50 && sizes.padding == 0);
	CHECK(ThemeModel_rowsFor(&sizes, &desktop) >= THEME_ROWS_MIN);
	CHECK(strstr(refused.lines[0], "padding 30 leaves the list") != NULL);
	// on a short screen the pill goes too
	ThemeStock tiny = stockOf(160, 10, 3);
	theme = blank();
	theme.pill_size = 50;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &tiny, &refused);
	CHECK(refused.count == 1 && sizes.pill == 0);

	// stock's button doesn't fit a small pill, and stock's text doesn't fit a small button
	theme = blank();
	theme.pill_size = 22;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 1 && sizes.pill == 0);
	CHECK(strstr(refused.lines[0], "pill_size 22 is too small for a 20 button") != NULL);
	theme = blank();
	theme.button_size = 14;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 1 && sizes.button == 0);
	CHECK(strstr(refused.lines[0], "button_size 14 is too small") != NULL);
	// a smaller button and text together fit
	theme.text_step = -2;
	theme.pill_size = 24;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 0);
	CHECK(sizes.button == 14 && sizes.pill == 24 && sizes.text_step == -2);

	// a step too big for stock's pill and button goes, the sizes stay
	theme = blank();
	theme.text_step = 8;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 1 && sizes.text_step == 0);
	theme.pill_size = 40;
	theme.button_size = 30;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(refused.count == 0 && sizes.text_step == 8);
	// a step whose pill was refused goes with it
	theme.pill_size = 200;
	memset(&refused, 0, sizeof(refused));
	sizes = ThemeModel_checkSizes(&theme, &desktop, &refused);
	CHECK(sizes.pill == 0 && sizes.text_step == 0);
	CHECK(sizes.button == 0); // 30 doesn't fit stock's pill
}

static void testColors(void) {
	CHECK(ThemeModel_colorOk(MOSAIC_THEME_UNSET));
	CHECK(ThemeModel_colorOk(0));
	CHECK(ThemeModel_colorOk(0xffffff));
	CHECK(!ThemeModel_colorOk(0x1000000));
	CHECK(!ThemeModel_colorOk(-2));
	CHECK(ThemeModel_stockColor(0x9b2257) == 0x9b2257ffU);

	CHECK(ThemeModel_contrast(0xffffffff, 0x000000ff) == 210);
	CHECK(ThemeModel_contrast(0x000000ff, 0xffffffff) == 210);
	CHECK(ThemeModel_contrast(0x777777ff, 0x777777ff) == 10);

	// stock's default palette already passes 4.5:1
	uint32_t stock[7] = { 0xffffffff, 0x9b2257ff, 0x1e2329ff, 0xffffffff, 0x000000ff, 0xffffffff, 0x000000ff };
	uint32_t copy[7];
	memcpy(copy, stock, sizeof(copy));
	CHECK(ThemeModel_clampContrast(copy, 45) == 0);
	CHECK(memcmp(copy, stock, sizeof(copy)) == 0);
	CHECK(ThemeModel_clampContrast(copy, 0) == 0);

	// grey on grey: the texts move, the backdrops don't
	uint32_t grey[7] = { 0x808080ff, 0x707070ff, 0x1e2329ff, 0x606060ff, 0x909090ff, 0x808080ff, 0x505050ff };
	memcpy(copy, grey, sizeof(copy));
	CHECK(ThemeModel_clampContrast(copy, 45) == 3);
	CHECK(copy[0] == grey[0] && copy[1] == grey[1] && copy[2] == grey[2] && copy[6] == grey[6]);
	CHECK(ThemeModel_contrast(copy[3], copy[6]) >= 45);
	CHECK(ThemeModel_contrast(copy[4], copy[0]) >= 45);
	CHECK(ThemeModel_contrast(copy[5], copy[1]) >= 45);
	// list text on a dark background went lighter
	CHECK((copy[3] >> 24) > 0x60);
	// an unreachable ratio ends at black or white
	memcpy(copy, grey, sizeof(copy));
	ThemeModel_clampContrast(copy, 210);
	CHECK(copy[3] == 0xffffffff || copy[3] == 0x000000ff);
}

// Stock numbers its hint styles -1 (text) to 2 (shapes), as config.h's
// INPUT_STYLE_* do.
static void testHintStyle(void) {
	CHECK(ThemeModel_hintStyleOk(MOSAIC_HINT_STYLE_USER));
	CHECK(ThemeModel_hintStyleOk(MOSAIC_HINT_STYLE_TEXT));
	CHECK(ThemeModel_hintStyleOk(MOSAIC_HINT_STYLE_SHAPES));
	CHECK(!ThemeModel_hintStyleOk(-1));
	CHECK(!ThemeModel_hintStyleOk(5));

	// unset, the user's Settings style, whatever it is
	for (int stock = -1; stock <= 2; stock++) CHECK(ThemeModel_hintStock(MOSAIC_HINT_STYLE_USER, stock) == stock);
	CHECK(ThemeModel_hintStock(MOSAIC_HINT_STYLE_USER, 7) == 7);
	// a theme's style over the user's
	for (int stock = -1; stock <= 2; stock++) {
		CHECK(ThemeModel_hintStock(MOSAIC_HINT_STYLE_TEXT, stock) == -1);
		CHECK(ThemeModel_hintStock(MOSAIC_HINT_STYLE_ABXY, stock) == 0);
		CHECK(ThemeModel_hintStock(MOSAIC_HINT_STYLE_CARDINALS, stock) == 1);
		CHECK(ThemeModel_hintStock(MOSAIC_HINT_STYLE_SHAPES, stock) == 2);
	}
	// one the theme can't set leaves the user's
	CHECK(ThemeModel_hintStock(9, 1) == 1);
	CHECK(ThemeModel_hintStock(-1, 2) == 2);

	// back to the SDK's numbers for host->hint_style
	CHECK(ThemeModel_hintStyle(-1) == MOSAIC_HINT_STYLE_TEXT);
	CHECK(ThemeModel_hintStyle(0) == MOSAIC_HINT_STYLE_ABXY);
	CHECK(ThemeModel_hintStyle(1) == MOSAIC_HINT_STYLE_CARDINALS);
	CHECK(ThemeModel_hintStyle(2) == MOSAIC_HINT_STYLE_SHAPES);
	CHECK(ThemeModel_hintStyle(3) == MOSAIC_HINT_STYLE_TEXT);
	CHECK(ThemeModel_hintStyle(-2) == MOSAIC_HINT_STYLE_TEXT);
	for (int style = MOSAIC_HINT_STYLE_TEXT; style <= MOSAIC_HINT_STYLE_SHAPES; style++) {
		CHECK(ThemeModel_hintStyle(ThemeModel_hintStock(style, 0)) == style);
	}
}

static void testCardFile(void) {
	char base[] = "/tmp/mosaic-test-theme-XXXXXX";
	CHECK(mkdtemp(base) != NULL);
	char card[600], off[600], path[700], out[1024];
	snprintf(card, sizeof(card), "%s/card", base);
	snprintf(off, sizeof(off), "%s/off", base);
	mkdir(card, 0755);
	mkdir(off, 0755);
	snprintf(path, sizeof(path), "%s/Themes", card);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/Themes/font.ttf", card);
	FILE* f = fopen(path, "w");
	if (f) { fputs("x", f); fclose(f); }
	snprintf(path, sizeof(path), "%s/secret.ttf", off);
	f = fopen(path, "w");
	if (f) { fputs("x", f); fclose(f); }
	char link[700], target[700];
	snprintf(link, sizeof(link), "%s/Themes/escape.ttf", card);
	symlink(path, link);
	snprintf(link, sizeof(link), "%s/Themes/inside.ttf", card);
	snprintf(target, sizeof(target), "%s/Themes/font.ttf", card);
	symlink(target, link);

	const char* why = NULL;
	snprintf(path, sizeof(path), "%s/Themes/font.ttf", card);
	CHECK(ThemeModel_cardFile(card, path, out, sizeof(out), &why) == 0);
	CHECK(strstr(out, "/card/Themes/font.ttf") != NULL);
	// a symlink within the card is fine, one off it isn't
	snprintf(path, sizeof(path), "%s/Themes/inside.ttf", card);
	CHECK(ThemeModel_cardFile(card, path, out, sizeof(out), &why) == 0);
	CHECK(strstr(out, "/card/Themes/font.ttf") != NULL);
	snprintf(path, sizeof(path), "%s/Themes/escape.ttf", card);
	CHECK(ThemeModel_cardFile(card, path, out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't on the card");
	// .. out of it
	snprintf(path, sizeof(path), "%s/../off/secret.ttf", card);
	CHECK(ThemeModel_cardFile(card, path, out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't on the card");
	// a folder, a missing file, a relative path
	snprintf(path, sizeof(path), "%s/Themes", card);
	CHECK(ThemeModel_cardFile(card, path, out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't a regular file");
	snprintf(path, sizeof(path), "%s/Themes/none.ttf", card);
	CHECK(ThemeModel_cardFile(card, path, out, sizeof(out), &why) == -1);
	CHECK_STR(why, "doesn't exist");
	CHECK(ThemeModel_cardFile(card, "Themes/font.ttf", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't a full path");
	// a card whose own name is a prefix of the file's folder
	snprintf(path, sizeof(path), "%s/cardx", base);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/cardx/a.ttf", base);
	f = fopen(path, "w");
	if (f) { fputs("x", f); fclose(f); }
	CHECK(ThemeModel_cardFile(card, path, out, sizeof(out), &why) == -1);
	// too long for out
	snprintf(path, sizeof(path), "%s/Themes/font.ttf", card);
	char small[8];
	CHECK(ThemeModel_cardFile(card, path, small, sizeof(small), &why) == -1);

	char command[700];
	snprintf(command, sizeof(command), "rm -rf '%s'", base);
	CHECK(system(command) == 0);
}

static void testWallpapers(void) {
	ThemeWalls walls;
	memset(&walls, 0, sizeof(walls));
	CHECK(ThemeModel_wallpaper(&walls, THEME_WALL_MAIN) == NULL);
	CHECK(ThemeModel_wallpaper(&walls, THEME_WALL_LIST) == NULL);
	CHECK(ThemeModel_wallpaper(&walls, THEME_WALL_QUICK) == NULL);
	strcpy(walls.main, "/card/Themes/a/bg.png");
	CHECK_STR(ThemeModel_wallpaper(&walls, THEME_WALL_MAIN), "/card/Themes/a/bg.png");
	// the list falls back to the main one, the quick menu doesn't
	CHECK_STR(ThemeModel_wallpaper(&walls, THEME_WALL_LIST), "/card/Themes/a/bg.png");
	CHECK(ThemeModel_wallpaper(&walls, THEME_WALL_QUICK) == NULL);
	strcpy(walls.list, "/card/Themes/a/list.png");
	strcpy(walls.quick, "/card/Themes/a/quick.png");
	CHECK_STR(ThemeModel_wallpaper(&walls, THEME_WALL_LIST), "/card/Themes/a/list.png");
	CHECK_STR(ThemeModel_wallpaper(&walls, THEME_WALL_QUICK), "/card/Themes/a/quick.png");
	CHECK(ThemeModel_wallpaper(&walls, 99) == NULL);
}

int main(void) {
	testStockFormulas();
	testResolve();
	testCheckSizes();
	testColors();
	testHintStyle();
	testCardFile();
	testWallpapers();
	CHECK_DONE();
}
