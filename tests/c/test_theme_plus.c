// The theme-plus plugin: its model on its own (theme.txt, malformed ones
// too, paths that try to leave a theme's folder, files on the card, the
// list of themes with stock's palettes in the host's order and the merge
// with the settings, the fallback font among them), then the plugin
// against a fake host, on a card of its own under TEST_SDCARD_PATH.

#define _GNU_SOURCE // dladdr, in theme_plus.c
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/theme-plus/theme_plus.c"

#ifndef TEST_SDCARD_PATH
#define TEST_SDCARD_PATH "/tmp/theme-plus-card"
#endif
#define CARD TEST_SDCARD_PATH

// the lines a parse logged
static char logged[32][320];
static int logCount;

static void capture(void* ctx, const char* message) {
	(void)ctx;
	if (logCount < 32) snprintf(logged[logCount], sizeof(logged[0]), "%s", message);
	logCount++;
}

static int loggedLine(const char* text) {
	for (int i = 0; i < logCount && i < 32; i++) {
		if (strstr(logged[i], text)) return 1;
	}
	return 0;
}

static void writeFile(const char* path, const char* text) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char* p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static void testValues(void) {
	CHECK(TP_parseColor("0xABCDEF") == 0xABCDEF);
	CHECK(TP_parseColor("0Xabcdef") == 0xABCDEF);
	CHECK(TP_parseColor("abcdef") == 0xABCDEF);
	CHECK(TP_parseColor("#102030") == 0x102030);
	CHECK(TP_parseColor("0x9b2257ff") == 0x9b2257);
	CHECK(TP_parseColor("0xCBA6F7FF") == 0xCBA6F7);
	CHECK(TP_parseColor("0x000000") == 0);
	CHECK(TP_parseColor("") == -1);
	CHECK(TP_parseColor("0x") == -1);
	CHECK(TP_parseColor("0x12345") == -1);
	CHECK(TP_parseColor("0x1234567") == -1);
	CHECK(TP_parseColor("0x1234567890") == -1);
	CHECK(TP_parseColor("0xGGGGGG") == -1);
	CHECK(TP_parseColor("0xABCDEF;") == -1);
	CHECK(TP_parseColor("red") == -1);
	CHECK(TP_parseColor(NULL) == -1);

	CHECK(TP_parseContrast("4.5") == 45);
	CHECK(TP_parseContrast("7") == 70);
	CHECK(TP_parseContrast("1") == 10);
	CHECK(TP_parseContrast("21") == 210);
	CHECK(TP_parseContrast("3.0") == 30);
	CHECK(TP_parseContrast("0.9") == -1);
	CHECK(TP_parseContrast("21.1") == -1);
	CHECK(TP_parseContrast("4.55") == -1);
	CHECK(TP_parseContrast("4.") == -1);
	CHECK(TP_parseContrast(".5") == -1);
	CHECK(TP_parseContrast("-3") == -1);
	CHECK(TP_parseContrast("100") == -1);
	CHECK(TP_parseContrast("high") == -1);
	CHECK(TP_parseContrast(NULL) == -1);

	int n = 99;
	CHECK(TP_parseInt("-2", -2, 8, &n) == 0 && n == -2);
	CHECK(TP_parseInt("+8", -2, 8, &n) == 0 && n == 8);
	CHECK(TP_parseInt("9", -2, 8, &n) == -1 && n == 8);
	CHECK(TP_parseInt("3px", 0, 10, &n) == -1);
	CHECK(TP_parseInt("", 0, 10, &n) == -1);
	CHECK(TP_parseInt("-", 0, 10, &n) == -1);
	CHECK(TP_parseInt("99999", 0, 100000, &n) == -1);

	CHECK(TP_relativePathOk("font.ttf"));
	CHECK(TP_relativePathOk("fonts/main.ttf"));
	CHECK(TP_relativePathOk("...ttf"));
	CHECK(TP_relativePathOk("a..b/c"));
	CHECK(!TP_relativePathOk(""));
	CHECK(!TP_relativePathOk(NULL));
	CHECK(!TP_relativePathOk("/mnt/SDCARD/font.ttf"));
	CHECK(!TP_relativePathOk(".."));
	CHECK(!TP_relativePathOk("../font.ttf"));
	CHECK(!TP_relativePathOk("fonts/../../font.ttf"));
	CHECK(!TP_relativePathOk("fonts/.."));
	CHECK(!TP_relativePathOk("./font.ttf"));
	CHECK(!TP_relativePathOk("fonts//font.ttf"));
	CHECK(!TP_relativePathOk("fonts/"));
	CHECK(!TP_relativePathOk("..\\font.ttf"));
}

static void testParseTheme(void) {
	TPTheme t;
	logCount = 0;
	CHECK(TP_parseTheme("# a night theme\r\n"
		"version=1\r\n"
		"name = Night \r\n"
		"\r\n"
		"palette=Catppuccin Mocha\n"
		"color1=0x112233\n"
		"color7=0x445566ff\n"
		"font=fonts/night.ttf\n"
		"text_step=-2\n"
		"contrast=4.5\n"
		"pill_size=36\n"
		"button_size=24\n"
		"padding=8\n"
		"wallpaper=bg.png\n"
		"wallpaper_list=list.png\n"
		"wallpaper_quick=quick.png\n"
		"fallback_font=fonts/cjk.ttf",
		&t, capture, NULL) == 0);
	CHECK(logCount == 0);
	CHECK_STR(t.name, "Night");
	CHECK_STR(t.palette, "Catppuccin Mocha");
	CHECK(t.colors[0] == 0x112233);
	CHECK(t.colors[1] == MOSAIC_THEME_UNSET);
	CHECK(t.colors[6] == 0x445566);
	CHECK_STR(t.font, "fonts/night.ttf");
	CHECK(t.text_step == -2);
	CHECK(t.contrast == 45);
	CHECK(t.pill_size == 36 && t.button_size == 24 && t.padding == 8);
	CHECK_STR(t.wallpaper, "bg.png");
	CHECK_STR(t.wallpaper_list, "list.png");
	CHECK_STR(t.wallpaper_quick, "quick.png");
	CHECK_STR(t.fallback_font, "fonts/cjk.ttf");

	// nothing set: everything left to stock
	CHECK(TP_parseTheme("", &t, capture, NULL) == 0);
	CHECK(t.name[0] == '\0' && t.palette[0] == '\0' && t.font[0] == '\0' && t.fallback_font[0] == '\0');
	CHECK(t.colors[0] == MOSAIC_THEME_UNSET && t.colors[6] == MOSAIC_THEME_UNSET);
	CHECK(t.text_step == TP_STEP_UNSET && t.contrast == 0 && t.pill_size == 0);
	CHECK(TP_parseTheme(NULL, &t, capture, NULL) == 0);

	// malformed lines and bad values are logged and dropped; the rest stays
	char longLine[TP_PATH_MAX + 128];
	memset(longLine, 'a', sizeof(longLine) - 1);
	longLine[sizeof(longLine) - 1] = '\0';
	memcpy(longLine, "font=", 5);
	char text[4096];
	snprintf(text, sizeof(text),
		"just some words\n"
		"colour1=0x112233\n"
		"color8=0x112233\n"
		"color2=blue\n"
		"color3=0x1234567\n"
		"color4=0x445566\n"
		"text_step=9\n"
		"text_step=big\n"
		"contrast=0.5\n"
		"pill_size=200\n"
		"button_size=4\n"
		"padding=-1\n"
		"font=../escape.ttf\n"
		"wallpaper=/mnt/SDCARD/bg.png\n"
		"wallpaper_list=walls/../../bg.png\n"
		"wallpaper_quick=\n"
		"name=\n"
		"version=one\n"
		"%s\n"
		"=0x123456\n"
		"color5=0x778899\n"
		"fallback_font=../cjk.ttf\n"
		"fallback_font=/mnt/SDCARD/cjk.ttf\n",
		longLine);
	logCount = 0;
	CHECK(TP_parseTheme(text, &t, capture, NULL) == 0);
	CHECK(loggedLine("line 1 isn't key=value, ignored"));
	CHECK(loggedLine("line 2: unknown key colour1, ignored"));
	CHECK(loggedLine("line 3: unknown key color8, ignored"));
	CHECK(loggedLine("line 4: color2 blue isn't 0xRRGGBB or 0xRRGGBBAA, dropped"));
	CHECK(loggedLine("line 5: color3 0x1234567 isn't"));
	CHECK(loggedLine("line 7: text_step 9 isn't from -2 to 8, dropped"));
	CHECK(loggedLine("line 8: text_step big isn't"));
	CHECK(loggedLine("line 9: contrast 0.5 isn't a ratio from 1 to 21, dropped"));
	CHECK(loggedLine("line 10: pill_size 200 isn't from 18 to 60"));
	CHECK(loggedLine("line 11: button_size 4 isn't from 12 to 56"));
	CHECK(loggedLine("line 12: padding -1 isn't from 1 to 30"));
	CHECK(loggedLine("line 13: font (../escape.ttf) isn't a path inside the theme's folder, dropped"));
	CHECK(loggedLine("line 14: wallpaper (/mnt/SDCARD/bg.png) isn't a path inside"));
	CHECK(loggedLine("line 15: wallpaper_list (walls/../../bg.png) isn't a path inside"));
	CHECK(loggedLine("line 16: wallpaper_quick () isn't a path inside"));
	CHECK(loggedLine("line 17: name isn't 1 to 63 characters, dropped"));
	CHECK(loggedLine("line 18: version one isn't a number, dropped"));
	CHECK(loggedLine("line 19 is too long, ignored"));
	CHECK(loggedLine("line 20: unknown key , ignored"));
	CHECK(loggedLine("line 22: fallback_font (../cjk.ttf) isn't a path inside the theme's folder, dropped"));
	CHECK(loggedLine("line 23: fallback_font (/mnt/SDCARD/cjk.ttf) isn't a path inside"));
	CHECK(logCount == 21);
	CHECK(t.colors[1] == MOSAIC_THEME_UNSET && t.colors[2] == MOSAIC_THEME_UNSET);
	CHECK(t.colors[3] == 0x445566 && t.colors[4] == 0x778899);
	CHECK(t.text_step == TP_STEP_UNSET && t.contrast == 0);
	CHECK(t.pill_size == 0 && t.button_size == 0 && t.padding == 0);
	CHECK(t.font[0] == '\0' && t.wallpaper[0] == '\0' && t.wallpaper_list[0] == '\0' && t.wallpaper_quick[0] == '\0');
	CHECK(t.fallback_font[0] == '\0');

	// the hint style, as stock's Settings names them
	CHECK(TP_parseHintStyle("text") == MOSAIC_HINT_STYLE_TEXT);
	CHECK(TP_parseHintStyle("ABXY") == MOSAIC_HINT_STYLE_ABXY);
	CHECK(TP_parseHintStyle("Cardinals") == MOSAIC_HINT_STYLE_CARDINALS);
	CHECK(TP_parseHintStyle("shapes") == MOSAIC_HINT_STYLE_SHAPES);
	CHECK(TP_parseHintStyle("shape") == -1);
	CHECK(TP_parseHintStyle("user") == -1);
	CHECK(TP_parseHintStyle("") == -1);
	CHECK(TP_parseHintStyle("2") == -1);
	CHECK(TP_parseHintStyle(NULL) == -1);
	logCount = 0;
	CHECK(TP_parseTheme("hint_style = Shapes\n", &t, capture, NULL) == 0);
	CHECK(logCount == 0);
	CHECK(t.hint_style == MOSAIC_HINT_STYLE_SHAPES);
	CHECK(TP_parseTheme("color1=0x112233\n", &t, capture, NULL) == 0);
	CHECK(t.hint_style == MOSAIC_HINT_STYLE_USER);
	CHECK(TP_parseTheme("hint_style=abxy\nhint_style=round\n", &t, capture, NULL) == 0);
	CHECK(loggedLine("line 2: hint_style round isn't text, abxy, cardinals or shapes, dropped"));
	CHECK(t.hint_style == MOSAIC_HINT_STYLE_ABXY);

	// a newer format isn't used at all
	logCount = 0;
	CHECK(TP_parseTheme("version=2\ncolor1=0x112233\n", &t, capture, NULL) == -1);
	CHECK(loggedLine("version 2 is newer than this plugin reads (1), so the theme isn't used"));
	// the last value wins, as in stock's palettes
	CHECK(TP_parseTheme("color1=0x111111\ncolor1=0x222222\n", &t, NULL, NULL) == 0);
	CHECK(t.colors[0] == 0x222222);
}

static void real(const char* path, char* out) {
	if (!realpath(path, out)) snprintf(out, PATH_MAX, "(missing %s)", path);
}

// The card: themes and bundled themes, with paths that try to leave
// their folder.
static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "'") == 0);
	writeFile(CARD "/Themes/Night/theme.txt", "name=Night\npalette=catppuccin mocha\ncolor1=0x112233\nfont=font.ttf\n"
		"wallpaper=walls/bg.png\nwallpaper_list=inside.png\nwallpaper_quick=outside.png\npill_size=36\nfallback_font=fonts/cjk.ttf\n");
	writeFile(CARD "/Themes/Night/font.ttf", "font");
	writeFile(CARD "/Themes/Night/fonts/cjk.ttf", "font");
	writeFile(CARD "/Fonts/glyphs.ttf", "font");
	writeFile(CARD "/Fonts/notes.txt", "not linked\n");
	CHECK(symlink("../../../Fonts/glyphs.ttf", CARD "/Themes/Night/fonts/card.ttf") == 0);
	writeFile(CARD "/Themes/Night/walls/bg.png", "png");
	writeFile(CARD "/secret.png", "png");
	CHECK(symlink("walls/bg.png", CARD "/Themes/Night/inside.png") == 0);
	CHECK(symlink("../../secret.png", CARD "/Themes/Night/outside.png") == 0);
	writeFile(CARD "/Themes/Bare/theme.txt", "");
	writeFile(CARD "/Themes/.hidden/theme.txt", "name=Hidden\n");
	writeFile(CARD "/Themes/NoFile/readme.txt", "no theme.txt\n");
	writeFile(CARD "/Themes/Future/theme.txt", "version=2\n");
	writeFile(CARD "/Themes/stock/theme.txt", "name=Not stock\n");
	writeFile(CARD "/Themes/plain.txt", "a file, not a folder\n");
	writeFile(CARD "/Elsewhere/Linked/theme.txt", "name=Linked\n");
	CHECK(symlink("../Elsewhere/Linked", CARD "/Themes/Linked") == 0);
	writeFile(CARD "/Bundled/nord/theme.txt", "name=Nord\ncolor1=0x88C0D0\n");
	writeFile(CARD "/Bundled/night/theme.txt", "name=Bundled night\n");
}

static void testResolve(void) {
	char out[TP_PATH_MAX], want[PATH_MAX];
	const char* why = "";
	CHECK(TP_resolve(CARD "/Themes/Night", "font.ttf", out, sizeof(out), &why) == 0);
	real(CARD "/Themes/Night/font.ttf", want);
	CHECK_STR(out, want);
	// a symlink inside the folder is followed
	CHECK(TP_resolve(CARD "/Themes/Night", "inside.png", out, sizeof(out), &why) == 0);
	real(CARD "/Themes/Night/walls/bg.png", want);
	CHECK_STR(out, want);
	CHECK(TP_resolve(CARD "/Themes/Night", "outside.png", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "is outside the theme's folder");
	CHECK(TP_resolve(CARD "/Themes/Night", "../../secret.png", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't a path inside the theme's folder");
	CHECK(TP_resolve(CARD "/Themes/Night", "walls", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't a regular file");
	CHECK(TP_resolve(CARD "/Themes/Night", "missing.png", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "doesn't exist");
	CHECK(TP_resolve(CARD "/Themes/Gone", "font.ttf", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "has no folder");
	// a folder whose name starts like the theme's isn't inside it
	writeFile(CARD "/Themes/Night2/x.png", "png");
	CHECK(symlink("../Night2/x.png", CARD "/Themes/Night/sibling.png") == 0);
	CHECK(TP_resolve(CARD "/Themes/Night", "sibling.png", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "is outside the theme's folder");
	CHECK(system("rm -rf '" CARD "/Themes/Night2' '" CARD "/Themes/Night/sibling.png'") == 0);
	CHECK(TP_resolve(CARD "/Themes/Night", "font.ttf", out, 8, &why) == -1);
	CHECK_STR(why, "is too long");

	// a file on the card, relative to it or a full path, not off it
	CHECK(TP_cardFile(CARD, "Fonts/glyphs.ttf", out, sizeof(out), &why) == 0);
	real(CARD "/Fonts/glyphs.ttf", want);
	CHECK_STR(out, want);
	CHECK(TP_cardFile(CARD, want, out, sizeof(out), &why) == 0);
	CHECK_STR(out, want);
	CHECK(TP_cardFile(CARD, "Themes/Night/fonts/card.ttf", out, sizeof(out), &why) == 0);
	CHECK_STR(out, want);
	char outside[PATH_MAX];
	snprintf(outside, sizeof(outside), "%s", CARD "-outside.ttf");
	writeFile(outside, "font");
	char away[PATH_MAX];
	real(outside, away);
	CHECK(TP_cardFile(CARD, away, out, sizeof(out), &why) == -1);
	CHECK_STR(why, "is outside the card");
	const char* base = strrchr(outside, '/');
	char up[PATH_MAX + 4];
	snprintf(up, sizeof(up), "../%s", base ? base + 1 : outside);
	CHECK(TP_cardFile(CARD, up, out, sizeof(out), &why) == -1);
	CHECK_STR(why, "is outside the card");
	CHECK(symlink(away, CARD "/Fonts/away.ttf") == 0);
	CHECK(TP_cardFile(CARD, "Fonts/away.ttf", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "is outside the card");
	CHECK(unlink(CARD "/Fonts/away.ttf") == 0 && unlink(outside) == 0);
	CHECK(TP_cardFile(CARD, "Fonts", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't a regular file");
	CHECK(TP_cardFile(CARD, "Fonts/none.ttf", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "doesn't exist");
	CHECK(TP_cardFile(CARD, "", out, sizeof(out), &why) == -1);
	CHECK_STR(why, "isn't a path on the card");
	CHECK(TP_cardFile(CARD, NULL, out, sizeof(out), &why) == -1);
	CHECK(TP_cardFile(CARD, "Fonts/glyphs.ttf", out, 8, &why) == -1);
	CHECK_STR(why, "is too long");

	char* text = TP_readFile(CARD "/Themes/Night/theme.txt");
	CHECK(text && strstr(text, "name=Night"));
	free(text);
	CHECK(TP_readFile(CARD "/Themes/Night") == NULL);
	CHECK(TP_readFile(CARD "/nothing.txt") == NULL);
	char big[TP_FILE_MAX + 2];
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	writeFile(CARD "/big.txt", big);
	CHECK(TP_readFile(CARD "/big.txt") == NULL);
}

static TPCatalog cat;

// stock's palettes as the host lists them, in Settings' order: its own,
// then the card's, with the user's (Mine) picked in Settings
static const TPPalette hostPalettes[] = {
	{ "Default", { 0xFFFFFF, 0x9B2257, 0x1E2329, 0xFFFFFF, 0x000000, 0xFFFFFF, 0x000000 } },
	{ "Catppuccin Mocha", { 0xCBA6F7, 0x181825, 0x1E1E2E, 0xCDD6F4, 0x1E1E2E, 0xA6ADC8, 0x1E1E2E } },
	{ "Night", { 0x999999, 0x9B2257, 0x1E2329, 0xFFFFFF, 0x000000, 0xFFFFFF, 0x000000 } },
	{ "Mine", { 0xFFFFFF, 0x9B2257, 0x1E2329, 0xFFFFFF, 0x000000, 0xFFFFFF, 0x010203 } },
	{ "Default", { 0xAAAAAA, 0x9B2257, 0x1E2329, 0xFFFFFF, 0x000000, 0xFFFFFF, 0x000000 } },
};
#define HOST_PALETTES (int)(sizeof(hostPalettes) / sizeof(hostPalettes[0]))
#define HOST_SELECTED 3

static void testCatalog(void) {
	TP_catalog(&cat, CARD "/Themes", CARD "/Bundled", hostPalettes, HOST_PALETTES, HOST_SELECTED);
	// the card's (by label), the plugin's, then the palettes in Settings'
	// order, not sorted, leaving out Night (a theme's name) and the card's
	// Default (stock's has its name), with the user's marked
	static const struct { int kind; const char* id; const char* label; int settings; } want[] = {
		{ TP_KIND_CARD, "Bare", "Bare", 0 },
		{ TP_KIND_CARD, "Night", "Night", 0 },
		{ TP_KIND_BUNDLED, "nord", "Nord", 0 },
		{ TP_KIND_PALETTE, "Default", "Default", 0 },
		{ TP_KIND_PALETTE, "Catppuccin Mocha", "Catppuccin Mocha", 0 },
		{ TP_KIND_PALETTE, "Mine", "Mine", 1 },
	};
	int count = (int)(sizeof(want) / sizeof(want[0]));
	CHECK(cat.theme_count == count);
	for (int i = 0; i < count && i < cat.theme_count; i++) {
		CHECK(cat.themes[i].kind == want[i].kind);
		CHECK_STR(cat.themes[i].id, want[i].id);
		CHECK_STR(cat.themes[i].label, want[i].label);
		CHECK(cat.themes[i].settings == want[i].settings);
	}
	CHECK_STR(cat.themes[1].dir, CARD "/Themes/Night");
	CHECK_STR(cat.themes[2].dir, CARD "/Bundled/nord");
	CHECK(cat.palette_count == 5);
	CHECK(cat.dropped == 0);

	CHECK(TP_find(&cat, "Night") == &cat.themes[1]);
	CHECK(TP_find(&cat, "night") == &cat.themes[1]);
	CHECK(TP_find(&cat, "NORD") == &cat.themes[2]);
	CHECK(TP_find(&cat, "Mine") == &cat.themes[5]);
	CHECK(TP_find(&cat, "stock") == NULL);
	CHECK(TP_find(&cat, "Linked") == NULL);
	CHECK(TP_find(&cat, "Future") == NULL);
	CHECK(TP_find(&cat, "") == NULL);
	CHECK(TP_find(&cat, NULL) == NULL);

	// the first in Settings' order: stock's before the card's of the same name
	const TPPalette* p = TP_findPalette(&cat, "Default");
	CHECK(p && p->colors[0] == 0xFFFFFF);
	p = TP_findPalette(&cat, "catppuccin mocha");
	CHECK(p && p->colors[0] == 0xCBA6F7);
	// a palette shadowed in the list by a theme is still there for palette=
	p = TP_findPalette(&cat, "Night");
	CHECK(p && p->colors[0] == 0x999999);
	CHECK(TP_findPalette(&cat, "Newer") == NULL);
	CHECK(TP_findPalette(&cat, NULL) == NULL);

	// a palette exactly named like a theme only ignoring case is listed, and
	// the setting names each by its exact id
	static const TPPalette upper[] = { { "NORD", { 0 } }, { "nord", { 0 } } };
	TP_catalog(&cat, CARD "/none", CARD "/Bundled", upper, 2, -1);
	CHECK(cat.theme_count == 3);
	CHECK(cat.themes[1].kind == TP_KIND_BUNDLED && cat.themes[2].kind == TP_KIND_PALETTE);
	CHECK(TP_find(&cat, "NORD") == &cat.themes[2]);
	CHECK(TP_find(&cat, "nord") == &cat.themes[1]);
	CHECK(cat.themes[2].settings == 0);

	// no palettes from the host, a selected one past the list, missing folders
	TP_catalog(&cat, CARD "/Themes", NULL, NULL, 0, 0);
	CHECK(cat.palette_count == 0 && cat.theme_count == 2);
	TP_catalog(&cat, CARD "/none", NULL, hostPalettes, 2, 7);
	CHECK(cat.theme_count == 2 && !cat.themes[0].settings && !cat.themes[1].settings);
	TP_catalog(&cat, CARD "/none", NULL, NULL, 0, -1);
	CHECK(cat.theme_count == 0 && cat.palette_count == 0);
}

static MosaicTheme stockTheme(void) {
	MosaicTheme t;
	memset(&t, 0, sizeof(t));
	t.size = sizeof(t);
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) t.colors[i] = MOSAIC_THEME_UNSET;
	return t;
}

static void testOverrides(void) {
	logCount = 0;
	TPOverrides o = TP_overrides(NULL, NULL, NULL, NULL, NULL, capture, NULL);
	CHECK(o.background == -1 && o.text_step == TP_STEP_UNSET && o.contrast == -1 && o.ui == -1 && o.padding == -1);
	o = TP_overrides("theme", "theme", "theme", "theme", "theme", capture, NULL);
	CHECK(o.background == -1 && o.text_step == TP_STEP_UNSET && o.contrast == -1 && o.ui == -1 && o.padding == -1);
	o = TP_overrides("", "", "", "", "", capture, NULL);
	CHECK(o.background == -1 && o.ui == -1);
	CHECK(logCount == 0);

	o = TP_overrides("0x102030", "larger", "4.5", "large", "10", capture, NULL);
	CHECK(o.background == 0x102030 && o.text_step == 4 && o.contrast == 45 && o.ui == 36 && o.padding == 10);
	o = TP_overrides("#ABCDEF", "small", "off", "stock", "auto", capture, NULL);
	CHECK(o.background == 0xABCDEF && o.text_step == -2 && o.contrast == 0 && o.ui == 0 && o.padding == 0);
	o = TP_overrides(NULL, "stock", "7", "compact", NULL, capture, NULL);
	CHECK(o.text_step == 0 && o.contrast == 70 && o.ui == 24);
	CHECK(logCount == 0);

	o = TP_overrides("navy", "huge", "max", "giant", "40", capture, NULL);
	CHECK(o.background == -1 && o.text_step == TP_STEP_UNSET && o.contrast == -1 && o.ui == -1 && o.padding == -1);
	CHECK(loggedLine("background navy isn't 0xRRGGBB, so the theme's is used"));
	CHECK(loggedLine("text_size huge isn't known"));
	CHECK(loggedLine("contrast max isn't off or a ratio"));
	CHECK(loggedLine("ui_size giant isn't known"));
	CHECK(loggedLine("margins 40 isn't auto or from 1 to 30"));

	// the fallback font setting: a file on the card, else the theme's
	char want[PATH_MAX];
	real(CARD "/Fonts/glyphs.ttf", want);
	logCount = 0;
	TP_overrideFallback(&o, CARD, "Fonts/glyphs.ttf", capture, NULL);
	CHECK_STR(o.fallback_font, want);
	TP_overrideFallback(&o, CARD, "", capture, NULL);
	CHECK(o.fallback_font[0] == '\0');
	TP_overrideFallback(&o, CARD, NULL, capture, NULL);
	CHECK(o.fallback_font[0] == '\0');
	CHECK(logCount == 0);
	TP_overrideFallback(&o, CARD, "Fonts/none.ttf", capture, NULL);
	CHECK(o.fallback_font[0] == '\0');
	CHECK(loggedLine("fallback_font Fonts/none.ttf doesn't exist, so the theme's is used"));
	TP_overrideFallback(&o, CARD, "/etc/hosts", capture, NULL);
	CHECK(o.fallback_font[0] == '\0');
	CHECK(loggedLine("fallback_font /etc/hosts is outside the card, so the theme's is used"));
	o = TP_overrides(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	CHECK(o.fallback_font[0] == '\0');

	CHECK(TP_buttonFor(0) == 0);
	CHECK(TP_buttonFor(24) == 16);
	CHECK(TP_buttonFor(30) == 20);
	CHECK(TP_buttonFor(36) == 24);
	CHECK(TP_buttonFor(42) == 28);
}

static void testApply(void) {
	TP_catalog(&cat, CARD "/Themes", CARD "/Bundled", hostPalettes, HOST_PALETTES, HOST_SELECTED);
	TPTheme parsed;
	char* text = TP_readFile(CARD "/Themes/Night/theme.txt");
	CHECK(TP_parseTheme(text, &parsed, capture, NULL) == 0);
	free(text);
	const TPPalette* mocha = TP_findPalette(&cat, parsed.palette);
	CHECK(mocha != NULL);
	TPOverrides none = TP_overrides(NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	// the palette, the theme's colours over it, its paths inside its folder
	MosaicTheme t = stockTheme();
	logCount = 0;
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &none, capture, NULL);
	CHECK(t.colors[0] == 0x112233);
	CHECK(t.colors[1] == 0x181825 && t.colors[6] == 0x1E1E2E);
	char want[PATH_MAX];
	real(CARD "/Themes/Night/font.ttf", want);
	CHECK_STR(t.font, want);
	real(CARD "/Themes/Night/walls/bg.png", want);
	CHECK_STR(t.wallpaper, want);
	CHECK_STR(t.wallpaper_list, want);
	CHECK(t.wallpaper_quick[0] == '\0');
	CHECK(loggedLine("wallpaper_quick (outside.png) is outside the theme's folder, dropped"));
	CHECK(logCount == 1);
	CHECK(t.pill_size == 36 && t.button_size == 0 && t.padding == 0);
	CHECK(t.text_step == 0 && t.contrast == 0);
	real(CARD "/Themes/Night/fonts/cjk.ttf", want);
	CHECK_STR(t.fallback_font, want);

	// a fallback font that leaves the theme's folder is dropped, even to the card
	TPTheme escaping = parsed;
	snprintf(escaping.fallback_font, sizeof(escaping.fallback_font), "fonts/card.ttf");
	t = stockTheme();
	logCount = 0;
	TP_apply(&t, &escaping, CARD "/Themes/Night", mocha, &none, capture, NULL);
	CHECK(t.fallback_font[0] == '\0');
	CHECK(loggedLine("fallback_font (fonts/card.ttf) is outside the theme's folder, dropped"));

	// the setting's fallback font wins over the theme's, a palette's and stock's
	char card[PATH_MAX];
	real(CARD "/Fonts/glyphs.ttf", card);
	TPOverrides font = none;
	TP_overrideFallback(&font, CARD, "Fonts/glyphs.ttf", NULL, NULL);
	t = stockTheme();
	logCount = 0;
	TP_apply(&t, &escaping, CARD "/Themes/Night", mocha, &font, capture, NULL);
	CHECK_STR(t.fallback_font, card);
	CHECK(!loggedLine("fallback_font"));
	t = stockTheme();
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &font, NULL, NULL);
	CHECK_STR(t.fallback_font, card);
	t = stockTheme();
	TP_apply(&t, NULL, NULL, mocha, &font, NULL, NULL);
	CHECK_STR(t.fallback_font, card);
	t = stockTheme();
	TP_apply(&t, NULL, NULL, NULL, &font, NULL, NULL);
	CHECK_STR(t.fallback_font, card);
	CHECK(t.colors[0] == MOSAIC_THEME_UNSET && t.font[0] == '\0' && t.pill_size == 0);
	t = stockTheme();
	TP_apply(&t, NULL, NULL, NULL, &none, NULL, NULL);
	CHECK(t.fallback_font[0] == '\0');

	// the settings win over the theme; a UI size drops the theme's padding and button
	parsed.padding = 9;
	parsed.button_size = 26;
	parsed.contrast = 70;
	parsed.text_step = 3;
	TPOverrides o = TP_overrides("0x010101", "large", "off", "compact", NULL, NULL, NULL);
	t = stockTheme();
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &o, NULL, NULL);
	CHECK(t.colors[6] == 0x010101 && t.colors[0] == 0x112233);
	CHECK(t.text_step == 2 && t.contrast == 0);
	CHECK(t.pill_size == 24 && t.button_size == 16 && t.padding == 0);
	o = TP_overrides(NULL, NULL, NULL, NULL, "5", NULL, NULL);
	t = stockTheme();
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &o, NULL, NULL);
	CHECK(t.pill_size == 36 && t.button_size == 26 && t.padding == 5 && t.contrast == 70 && t.text_step == 3);
	o = TP_overrides(NULL, NULL, NULL, "stock", NULL, NULL, NULL);
	t = stockTheme();
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &o, NULL, NULL);
	CHECK(t.pill_size == 0 && t.button_size == 0 && t.padding == 0);

	// stock's theme with settings only, and a palette on its own
	t = stockTheme();
	TP_apply(&t, NULL, NULL, NULL, &none, NULL, NULL);
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) CHECK(t.colors[i] == MOSAIC_THEME_UNSET);
	CHECK(t.font[0] == '\0' && t.text_step == 0 && t.pill_size == 0);
	o = TP_overrides("0x222222", "largest", "4.5", "larger", "auto", NULL, NULL);
	TP_apply(&t, NULL, NULL, NULL, &o, NULL, NULL);
	CHECK(t.colors[6] == 0x222222 && t.colors[0] == MOSAIC_THEME_UNSET);
	CHECK(t.text_step == 6 && t.contrast == 45 && t.pill_size == 42 && t.button_size == 28 && t.padding == 0);
	t = stockTheme();
	TP_apply(&t, NULL, NULL, mocha, &none, NULL, NULL);
	CHECK(t.colors[0] == 0xCBA6F7 && t.colors[6] == 0x1E1E2E);

	// the theme's hint style, and the user's (unset) without one
	CHECK(parsed.hint_style == MOSAIC_HINT_STYLE_USER);
	t = stockTheme();
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &none, NULL, NULL);
	CHECK(t.hint_style == MOSAIC_HINT_STYLE_USER);
	TPTheme styled = parsed;
	styled.hint_style = MOSAIC_HINT_STYLE_CARDINALS;
	t = stockTheme();
	TP_apply(&t, &styled, CARD "/Themes/Night", mocha, &o, NULL, NULL);
	CHECK(t.hint_style == MOSAIC_HINT_STYLE_CARDINALS);
	t = stockTheme();
	TP_apply(&t, NULL, NULL, mocha, &none, NULL, NULL);
	CHECK(t.hint_style == MOSAIC_HINT_STYLE_USER);
	// one from before hint_style
	t = stockTheme();
	t.size = offsetof(MosaicTheme, hint_style);
	t.hint_style = 77;
	TP_apply(&t, &styled, CARD "/Themes/Night", mocha, &none, NULL, NULL);
	CHECK(t.hint_style == 77);

	// an older launcher's smaller MosaicTheme: fields past its size are left alone
	t = stockTheme();
	t.size = offsetof(MosaicTheme, font);
	strcpy(t.font, "untouched");
	t.pill_size = 77;
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &none, NULL, NULL);
	CHECK(t.colors[0] == 0x112233);
	CHECK_STR(t.font, "untouched");
	CHECK(t.pill_size == 77);
	// and one from before fallback_font
	t = stockTheme();
	t.size = offsetof(MosaicTheme, fallback_font);
	strcpy(t.fallback_font, "untouched");
	TP_apply(&t, &parsed, CARD "/Themes/Night", mocha, &font, NULL, NULL);
	CHECK(t.font[0] == '/' && t.wallpaper[0] == '/');
	CHECK_STR(t.fallback_font, "untouched");
}

// a fake host
static char cfgTheme[TP_NAME_MAX] = "stock";
static const char* cfgValues[8][2];
static int reloads, chooseCount, chooseCursor, tiles, actions;
static char chooseLabels[CHOICES_MAX][TP_NAME_MAX];
static void (*chooseDone)(int, void*);
static char hostLog[64][400];
static int hostLogCount;

static void fakeLog(const char* id, const char* message) {
	if (hostLogCount < 64) snprintf(hostLog[hostLogCount++], sizeof(hostLog[0]), "%s", message);
}

static int hostLogged(const char* text) {
	for (int i = 0; i < hostLogCount; i++) {
		if (strstr(hostLog[i], text)) return 1;
	}
	return 0;
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "theme")) return cfgTheme;
	for (int i = 0; i < 8 && cfgValues[i][0]; i++) {
		if (!strcmp(cfgValues[i][0], key)) return cfgValues[i][1];
	}
	return fallback;
}

static void fakeCfgSet(const char* key, const char* value) {
	if (!strcmp(key, "theme")) snprintf(cfgTheme, sizeof(cfgTheme), "%s", value);
}

static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)label, (void)available, (void)run, (void)data;
	actions += !strcmp(id, ACTION_ID);
	return 0;
}

static int fakeTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)row, (void)icon, (void)icon_off, (void)is_on;
	tiles += !strcmp(id, ACTION_ID);
	return 0;
}

static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen, (void)button, (void)trigger, (void)id;
	return 0;
}

static int fakeChoose(const char* title, const char* const* choices, int count, int selected,
		void (*on_pick)(int, void*), void* data) {
	(void)title, (void)data;
	chooseCount = count;
	chooseCursor = selected;
	for (int i = 0; i < count && i < CHOICES_MAX; i++) snprintf(chooseLabels[i], TP_NAME_MAX, "%s", choices[i]);
	chooseDone = on_pick;
	return 0;
}

static void fakeReload(void) {
	reloads++;
}

static int fakeUiSize(int which) {
	(void)which;
	return 0;
}

static int paletteReads;

static int fakePalettesCount(void) {
	paletteReads++;
	return HOST_PALETTES;
}

static int fakePaletteInfo(int index, MosaicPalette* out) {
	if (!out || index < 0 || index >= HOST_PALETTES) return -1;
	CHECK(out->size == sizeof(MosaicPalette));
	memset(out, 0, sizeof(*out));
	out->size = sizeof(*out);
	snprintf(out->name, sizeof(out->name), "%s", hostPalettes[index].name);
	memcpy(out->colors, hostPalettes[index].colors, sizeof(out->colors));
	return 0;
}

static int fakePaletteSelected(void) {
	return HOST_SELECTED;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "desktop",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/desktop",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.quick_tile = fakeTile,
	.choose = fakeChoose,
	.reload_lists = fakeReload,
	.ui_size = fakeUiSize,
	.palettes_count = fakePalettesCount,
	.palette_info = fakePaletteInfo,
	.palette_selected = fakePaletteSelected,
};

static MosaicTheme asked(void) {
	MosaicTheme t = stockTheme();
	hostLogCount = 0;
	CHECK(plugin.theme(&t) == 0);
	return t;
}

static void testPlugin(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->theme == theme);
	CHECK(p->init(&fake) == 0);
	CHECK(actions == 1 && tiles == 1);
	snprintf(bundledDir, sizeof(bundledDir), "%s", CARD "/Bundled");

	MosaicTheme t = asked();
	CHECK(hostLogged("theme stock"));
	CHECK(t.colors[0] == MOSAIC_THEME_UNSET && t.font[0] == '\0' && t.pill_size == 0);

	snprintf(cfgTheme, sizeof(cfgTheme), "Night");
	t = asked();
	CHECK(hostLogged("theme Night (card, palette Catppuccin Mocha)"));
	CHECK(hostLogged(CARD "/Themes/Night/theme.txt: wallpaper_quick (outside.png) is outside the theme's folder, dropped"));
	CHECK(t.colors[0] == 0x112233 && t.colors[6] == 0x1E1E2E && t.pill_size == 36 && t.font[0] == '/');
	char want[PATH_MAX];
	real(CARD "/Themes/Night/fonts/cjk.ttf", want);
	CHECK_STR(t.fallback_font, want);

	// the fallback font setting, over the theme's and over stock's
	char card[PATH_MAX];
	real(CARD "/Fonts/glyphs.ttf", card);
	cfgValues[0][0] = "fallback_font";
	cfgValues[0][1] = "Fonts/glyphs.ttf";
	t = asked();
	CHECK_STR(t.fallback_font, card);
	CHECK(t.colors[0] == 0x112233);
	snprintf(cfgTheme, sizeof(cfgTheme), "stock");
	t = asked();
	CHECK(hostLogged("theme stock"));
	CHECK_STR(t.fallback_font, card);
	CHECK(t.colors[0] == MOSAIC_THEME_UNSET && t.font[0] == '\0');
	cfgValues[0][1] = card;
	t = asked();
	CHECK_STR(t.fallback_font, card);
	cfgValues[0][1] = "Fonts/gone.ttf";
	snprintf(cfgTheme, sizeof(cfgTheme), "Night");
	t = asked();
	CHECK(hostLogged("settings: fallback_font Fonts/gone.ttf doesn't exist, so the theme's is used"));
	CHECK_STR(t.fallback_font, want);
	cfgValues[0][0] = NULL;

	snprintf(cfgTheme, sizeof(cfgTheme), "Catppuccin Mocha");
	t = asked();
	CHECK(hostLogged("theme Catppuccin Mocha (palette)"));
	CHECK(t.colors[0] == 0xCBA6F7 && t.font[0] == '\0');

	snprintf(cfgTheme, sizeof(cfgTheme), "nord");
	cfgValues[0][0] = "background";
	cfgValues[0][1] = "0x000001";
	cfgValues[1][0] = "text_size";
	cfgValues[1][1] = "large";
	t = asked();
	CHECK(hostLogged("theme nord (bundled)"));
	CHECK(t.colors[0] == 0x88C0D0 && t.colors[6] == 0x000001 && t.text_step == 2);
	cfgValues[0][0] = cfgValues[1][0] = NULL;

	// a theme whose palette is missing keeps its own colours
	writeFile(CARD "/Themes/Lost/theme.txt", "palette=Gone\ncolor2=0x123456\nmystery=1\n");
	snprintf(cfgTheme, sizeof(cfgTheme), "Lost");
	t = asked();
	CHECK(hostLogged("theme.txt: no palette is called Gone, so it's left out"));
	CHECK(hostLogged("theme.txt: line 3: unknown key mystery, ignored"));
	CHECK(t.colors[0] == MOSAIC_THEME_UNSET && t.colors[1] == 0x123456);

	snprintf(cfgTheme, sizeof(cfgTheme), "Nowhere");
	t = asked();
	CHECK(hostLogged("no theme or palette is called Nowhere, so stock's is used"));
	CHECK(t.colors[0] == MOSAIC_THEME_UNSET);

	// the picker: Stock, then every theme, then the palettes in Settings'
	// order with the user's marked, on the current one; a pick saves and reloads
	snprintf(cfgTheme, sizeof(cfgTheme), "night");
	int reads = paletteReads;
	pick(NULL);
	CHECK(paletteReads == reads + 1);
	CHECK(chooseCount == 8);
	CHECK_STR(chooseLabels[0], "Stock");
	CHECK_STR(chooseLabels[1], "Bare");
	CHECK_STR(chooseLabels[2], "Lost");
	CHECK_STR(chooseLabels[3], "Night");
	CHECK_STR(chooseLabels[4], "Nord");
	CHECK_STR(chooseLabels[5], "Default");
	CHECK_STR(chooseLabels[6], "Catppuccin Mocha");
	CHECK_STR(chooseLabels[7], "Mine (in Settings)");
	CHECK(chooseCursor == 3);
	CHECK(chooseDone != NULL);
	if (chooseDone) chooseDone(4, NULL);
	CHECK_STR(cfgTheme, "nord");
	CHECK(reloads == 1);
	if (chooseDone) chooseDone(-1, NULL);
	CHECK_STR(cfgTheme, "nord");
	CHECK(reloads == 1);
	if (chooseDone) chooseDone(0, NULL);
	CHECK_STR(cfgTheme, "stock");
	CHECK(reloads == 2);
	pick(NULL);
	CHECK(chooseCursor == 0);

	// picking a palette makes it the launcher's colours; theme.txt's own win over palette=
	if (chooseDone) chooseDone(7, NULL);
	CHECK_STR(cfgTheme, "Mine");
	t = asked();
	CHECK(hostLogged("theme Mine (palette)"));
	CHECK(t.colors[6] == 0x010203 && t.colors[0] == 0xFFFFFF && t.font[0] == '\0');
	pick(NULL);
	CHECK(chooseCursor == 7);
	writeFile(CARD "/Themes/Mixed/theme.txt", "palette=Mine\ncolor7=0x445566\n");
	snprintf(cfgTheme, sizeof(cfgTheme), "Mixed");
	t = asked();
	CHECK(hostLogged("theme Mixed (card, palette Mine)"));
	CHECK(t.colors[6] == 0x445566 && t.colors[1] == 0x9B2257);

	// a launcher without the palette calls lists none
	MosaicHost noPalettes = fake;
	noPalettes.size = offsetof(MosaicHost, palettes_count);
	host = &noPalettes;
	pick(NULL);
	CHECK(chooseCount == 6);
	CHECK_STR(chooseLabels[3], "Mixed");
	CHECK_STR(chooseLabels[5], "Nord");
	host = &fake;

	// without the theme slot it doesn't start
	MosaicHost old = fake;
	old.size = offsetof(MosaicHost, ui_size);
	CHECK(p->init(&old) == 1);
}

int main(void) {
	testValues();
	testParseTheme();
	makeCard();
	testResolve();
	testCatalog();
	testOverrides();
	testApply();
	testPlugin();
	CHECK(system("rm -rf '" CARD "'") == 0);
	CHECK_DONE();
}
