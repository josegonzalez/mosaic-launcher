// The grid layout plugin: its model on its own, then the plugin against a
// fake host, for the d-pad, the cells it draws and decorates, its letter
// tiles, and the art copies it keeps.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/layout-grid/layout_grid.c"

static void testCount(void) {
	CHECK(LG_count("auto", 1, 8) == 0);
	CHECK(LG_count("4", 1, 8) == 4);
	CHECK(LG_count("1", 1, 8) == 1);
	CHECK(LG_count("9", 1, 8) == 0);
	CHECK(LG_count("0", 1, 8) == 0);
	CHECK(LG_count("3x", 1, 8) == 0);
	CHECK(LG_count("", 1, 8) == 0);
	CHECK(LG_count(NULL, 1, 8) == 0);
}

static void testAuto(void) {
	// the room a 640x480 screen at scale 2, 1024x768 at 3 and 1280x720 at 3 leave
	CHECK(LG_autoColumns(624, 2) == 3);
	CHECK(LG_autoColumns(1000, 3) == 4);
	CHECK(LG_autoColumns(1256, 3) == 5);
	CHECK(LG_autoColumns(100, 3) == 2);
	CHECK(LG_autoColumns(5000, 1) == LG_MAX_COLUMNS);
	CHECK(LG_autoColumns(1000, 0) == LG_MAX_COLUMNS);

	CHECK(LG_autoRows(1000, 519, 4) == 2);
	CHECK(LG_autoRows(624, 304, 3) == 2); // 1.46 rows of square cells
	CHECK(LG_autoRows(1256, 456, 5) == 2);
	CHECK(LG_autoRows(1000, 300, 4) == 1); // 1.2
	CHECK(LG_autoRows(1000, 350, 4) == 2); // 1.4
	CHECK(LG_autoRows(100, 5000, 1) == LG_MAX_ROWS);
	CHECK(LG_autoRows(0, 100, 4) == 1);
}

static void testGrid(void) {
	// desktop: 1024x663 above the hints, the status pill 90 tall at 30
	MosaicRect area = { 0, 0, 1024, 663 };
	MosaicRect status = { 794, 30, 200, 90 };
	MosaicRect none = { 0, 0, 0, 0 };
	LGGrid g;
	CHECK(LG_grid(&area, &status, &none, 3, 0, 0, 78, 78, &g) == 0);
	CHECK(g.cols == 4 && g.rows == 2);
	CHECK(g.cell_w == 250 && g.cell_h == 259);
	// below the pill and a gap, centred in the 519 left
	CHECK(g.grid.x == 12 && g.grid.y == 132 && g.grid.w == 1000 && g.grid.h == 518);

	MosaicRect r = LG_cell(&g, 5);
	CHECK(r.x == 262 && r.y == 391 && r.w == 250 && r.h == 259);
	r = LG_cell(&g, 0);
	CHECK(r.x == 12 && r.y == 132);
	r = LG_cell(&g, -1);
	CHECK(r.w == 0);
	// the last cell ends inside the area
	r = LG_cell(&g, 7);
	CHECK(r.x + r.w <= area.w && r.y + r.h <= area.h);

	// header widgets taller than the pill push it down; without either it starts a gap down
	MosaicRect header = { 600, 30, 194, 120 };
	CHECK(LG_grid(&area, &status, &header, 3, 0, 0, 78, 78, &g) == 0);
	CHECK(g.grid.y >= 162);
	CHECK(LG_grid(&area, NULL, NULL, 3, 0, 0, 78, 78, &g) == 0);
	CHECK(g.grid.y == 12);

	// set counts are kept while they fit
	CHECK(LG_grid(&area, &status, &none, 3, 6, 3, 78, 78, &g) == 0);
	CHECK(g.cols == 6 && g.rows == 3);
	CHECK(g.cell_w == 166 && g.cell_h == 173);
	// rows go first, then columns, until a cell is big enough
	CHECK(LG_grid(&area, &status, &none, 3, 8, 6, 200, 200, &g) == 0);
	CHECK(g.rows == 2 && g.cols == 5);
	CHECK(LG_grid(&area, &status, &none, 3, 4, 2, 1001, 10, &g) == -1);
	MosaicRect tiny = { 0, 0, 20, 20 };
	CHECK(LG_grid(&tiny, &none, &none, 3, 0, 0, 1, 1, &g) == -1);
}

static void testParts(void) {
	MosaicRect cell = { 0, 0, 100, 120 };
	MosaicRect hl, tile, art, name;
	LG_parts(&cell, 6, 9, 30, &hl, &tile, &art, &name);
	CHECK(hl.x == 6 && hl.y == 6 && hl.w == 88 && hl.h == 108);
	CHECK(tile.x == 15 && tile.y == 15 && tile.w == 70 && tile.h == 90);
	CHECK(art.x == 15 && art.y == 15 && art.w == 70 && art.h == 60);
	CHECK(name.x == 15 && name.y == 75 && name.w == 70 && name.h == 30);
	// no names: the art takes the tile
	LG_parts(&cell, 6, 9, 0, &hl, &tile, &art, &name);
	CHECK(art.h == 90 && name.h == 0);
	// a name strip taller than the tile leaves no art
	LG_parts(&cell, 6, 9, 500, &hl, &tile, &art, &name);
	CHECK(art.h == 0 && name.h == 90);
	MosaicRect small = { 0, 0, 10, 10 };
	LG_parts(&small, 6, 9, 0, &hl, &tile, &art, &name);
	CHECK(hl.w == 0 && tile.w == 0 && art.w == 0);
}

static void testWindow(void) {
	// 20 entries, 4 columns: 5 rows, 2 in view
	CHECK(LG_window(0, 0, 20, 4, 2) == 0);
	CHECK(LG_window(0, 7, 20, 4, 2) == 0);
	CHECK(LG_window(0, 8, 20, 4, 2) == 1);
	CHECK(LG_window(1, 4, 20, 4, 2) == 1);
	CHECK(LG_window(3, 4, 20, 4, 2) == 1);
	CHECK(LG_window(0, 19, 20, 4, 2) == 3);
	CHECK(LG_window(4, 19, 20, 4, 2) == 3); // never past the last row
	CHECK(LG_window(9, 0, 20, 4, 2) == 0);
	CHECK(LG_window(5, 2, 3, 4, 2) == 0); // fewer rows than fit
	CHECK(LG_window(0, 40, 20, 4, 2) == 3);
	CHECK(LG_window(2, 0, 0, 4, 2) == 0);
	CHECK(LG_window(-3, 0, 20, 4, 2) == 0);
}

static void testMove(void) {
	// 10 entries in rows 0-3, 4-7 and 8-9
	const int U = MOSAIC_BUTTON_UP, D = MOSAIC_BUTTON_DOWN, L = MOSAIC_BUTTON_LEFT, R = MOSAIC_BUTTON_RIGHT;
	CHECK(LG_move(5, 10, 4, L, 1) == 4);
	CHECK(LG_move(4, 10, 4, L, 0) == 3); // left steps back a row
	CHECK(LG_move(0, 10, 4, L, 1) == 9);
	CHECK(LG_move(0, 10, 4, L, 0) == 0);
	CHECK(LG_move(3, 10, 4, R, 0) == 4);
	CHECK(LG_move(9, 10, 4, R, 1) == 0);
	CHECK(LG_move(9, 10, 4, R, 0) == 9);

	CHECK(LG_move(5, 10, 4, U, 0) == 1);
	CHECK(LG_move(1, 10, 4, U, 1) == 9); // the column's lowest
	CHECK(LG_move(2, 10, 4, U, 1) == 6); // the last row is short there
	CHECK(LG_move(2, 10, 4, U, 0) == 2);

	CHECK(LG_move(1, 10, 4, D, 0) == 5);
	CHECK(LG_move(5, 10, 4, D, 0) == 9);
	CHECK(LG_move(6, 10, 4, D, 0) == 9); // nothing below: the last entry
	CHECK(LG_move(7, 10, 4, D, 1) == 9);
	CHECK(LG_move(8, 10, 4, D, 1) == 0);
	CHECK(LG_move(9, 10, 4, D, 1) == 1);
	CHECK(LG_move(9, 10, 4, D, 0) == 9);

	// one row, and one entry
	CHECK(LG_move(1, 3, 4, U, 1) == 1);
	CHECK(LG_move(1, 3, 4, D, 1) == 1);
	CHECK(LG_move(2, 3, 4, R, 1) == 0);
	CHECK(LG_move(0, 1, 4, R, 1) == 0);
	// anything else stays
	CHECK(LG_move(3, 10, 4, MOSAIC_BUTTON_A, 1) == 3);
	CHECK(LG_move(12, 10, 4, R, 1) == 12);
	CHECK(LG_move(3, 10, 0, R, 1) == 3);
}

static void testLetter(void) {
	char out[8];
	LG_letter("super mario", out, sizeof(out));
	CHECK_STR(out, "S");
	LG_letter("'Splosion Man", out, sizeof(out));
	CHECK_STR(out, "S");
	LG_letter("007 GoldenEye", out, sizeof(out));
	CHECK_STR(out, "0");
	LG_letter("\xC3\xA9mile", out, sizeof(out));
	CHECK_STR(out, "\xC3\xA9");
	LG_letter("...\xE3\x81\x82", out, sizeof(out));
	CHECK_STR(out, "\xE3\x81\x82");
	LG_letter("  !!", out, sizeof(out));
	CHECK_STR(out, "!");
	LG_letter("   ", out, sizeof(out));
	CHECK_STR(out, "");
	LG_letter("", out, sizeof(out));
	CHECK_STR(out, "");
	LG_letter(NULL, out, sizeof(out));
	CHECK_STR(out, "");
	// a sequence cut short is its lead byte; one that doesn't fit is left out
	LG_letter("\xC3", out, sizeof(out));
	CHECK_STR(out, "\xC3");
	LG_letter("\xC3\xA9", out, 2);
	CHECK_STR(out, "");
	// a stray continuation byte isn't a letter
	LG_letter("\xA9z", out, sizeof(out));
	CHECK_STR(out, "Z");
}

static void testShowName(void) {
	CHECK(LG_showName(1, 0, 0) == 1);
	CHECK(LG_showName(1, 0, 1) == 1);
	// a hidden name goes only where there's art
	CHECK(LG_showName(1, 1, 1) == 0);
	CHECK(LG_showName(1, 1, 0) == 1);
	// names off: none
	CHECK(LG_showName(0, 0, 0) == 0);
	CHECK(LG_showName(0, 1, 0) == 0);
	CHECK(LG_showName(0, 0, 1) == 0);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicEntry {
	char path[64];
	char name[64];
};
struct MosaicList {
	MosaicEntry items[16];
	int count;
};
struct MosaicSurface {
	int w, h;
	int owned; // made by the plugin
};

static MosaicList fake_list;
static MosaicSurface screen_surface = { 1024, 768, 0 };
static MosaicSurface art_image = { 400, 300, 0 }; // the cache's image for Apple and Date
static MosaicSurface copies[8];
static int copy_count = 0, frees = 0, scales = 0, scale_fail = 0;
static int selects = 0, selected_to = -1;
static int art_gets = 0, box_gets = 0;
static int decorated = 0, decorated_outside = 0, decorated_named = 0;
static MosaicRect view_area = { 0, 0, 1024, 663 };
static MosaicRect view_status = { 794, 30, 200, 90 };
static int tiles = 0; // letter tiles: rounded fills in the accent colour
static char letters[16];
static char names_drawn[128]; // the names under the tiles, each followed by a comma
static int blits = 0, blits_of_image = 0;
static const char* columns_cfg = "auto";
static MosaicSurface video = { 720, 480, 0 }; // another plugin's art_replace surface
static int replaced_index = -1; // the entry it's for, or -1 for none
static int replacement_asks = 0, blits_of_video = 0;
static MosaicRect blit_video;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "columns")) return columns_cfg;
	return fallback;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) {
	return i >= 0 && i < l->count ? (MosaicEntry*)&l->items[i] : NULL;
}
static int fakeSelectIndex(int i) {
	selects++;
	selected_to = i;
	return 0;
}
static int fakeScale(void) { return 3; }
static int fakeTheme(int index) {
	switch (index) {
	case MOSAIC_COLOR_MAIN: return 0xFFFFFF;
	case MOSAIC_COLOR_ACCENT: return 0x9B2257;
	case MOSAIC_COLOR_LIST_TEXT: return 0xFFFFFF;
	case MOSAIC_COLOR_LIST_TEXT_SELECTED: return 0x000000;
	}
	return -1;
}
static int fakeTextHeight(int font) { return font == MOSAIC_FONT_LARGE ? 48 : 24; }
static int fakeTextTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	snprintf(out, size, "%s", text);
	int w = (int)strlen(text) * (font == MOSAIC_FONT_LARGE ? 24 : 12);
	return w > max_width ? max_width : w;
}
static int fakeDrawText(MosaicSurface* t, int font, const char* text, int x, int y, MosaicColor c, int max_width) {
	(void)t; (void)x; (void)y; (void)c; (void)max_width;
	if (font == MOSAIC_FONT_LARGE && strlen(letters) + strlen(text) < sizeof(letters)) strcat(letters, text);
	if (font == MOSAIC_FONT_SMALL && strlen(names_drawn) + strlen(text) + 1 < sizeof(names_drawn)) {
		strcat(names_drawn, text);
		strcat(names_drawn, ",");
	}
	return 0;
}
static int fakeFillRoundRect(MosaicSurface* t, const MosaicRect* r, int radius, MosaicColor c) {
	(void)t; (void)r; (void)radius;
	if (c.r == 0x9B && c.g == 0x22 && c.b == 0x57) tiles++;
	return 0;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr) {
	(void)t; (void)sr;
	blits++;
	if (s == &art_image) blits_of_image++;
	if (s == &video) {
		blits_of_video++;
		blit_video = *dr;
	}
	return 0;
}
static int fakeSurfaceSize(const MosaicSurface* s, int* w, int* h) {
	*w = s->w;
	*h = s->h;
	return 0;
}
static MosaicSurface* fakeSurfaceScale(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)id; (void)src; (void)fit;
	scales++;
	if (scale_fail || copy_count >= 8) return NULL;
	MosaicSurface* s = &copies[copy_count++];
	*s = (MosaicSurface){ w, h, 1 };
	return s;
}
static int fakeSurfaceFree(const char* id, MosaicSurface* s) {
	if (!s->owned) return -1;
	s->owned = 0;
	frees++;
	return 0;
}
static int fakeArtGet(const MosaicEntry* e, int kind, const MosaicSurface** out) {
	*out = NULL;
	if (kind == MOSAIC_ART_THUMBNAIL) art_gets++;
	if (kind == MOSAIC_ART_BOX) box_gets++;
	if (!strcmp(e->name, "Apple") && kind == MOSAIC_ART_THUMBNAIL) {
		*out = &art_image;
		return MOSAIC_ART_READY;
	}
	// Date has a box only
	if (!strcmp(e->name, "Date") && kind == MOSAIC_ART_BOX) {
		*out = &art_image;
		return MOSAIC_ART_READY;
	}
	if (!strcmp(e->name, "Cherry")) return MOSAIC_ART_LOADING;
	return MOSAIC_ART_MISSING;
}
static int fakeDecorateCell(MosaicSurface* s, int index, const MosaicRect* r, const MosaicRect* name) {
	(void)s; (void)index;
	decorated++;
	if (r->x < view_area.x || r->y < view_status.y + view_status.h || r->x + r->w > view_area.w ||
		r->y + r->h > view_area.h) {
		decorated_outside++;
	}
	if (name && name->w > 0) decorated_named++;
	return 0;
}

static int fakeArtReplacement(int index, const MosaicSurface** out) {
	replacement_asks++;
	*out = NULL;
	if (index != replaced_index) return 0;
	*out = &video;
	return 1;
}

static MosaicHost fake_host;

static void setUp(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.entry_path = fakeEntryPath;
	fake_host.entry_name = fakeEntryName;
	fake_host.list_get = fakeListGet;
	fake_host.select_index = fakeSelectIndex;
	fake_host.screen_scale = fakeScale;
	fake_host.theme_color = fakeTheme;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTextTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.fill_round_rect = fakeFillRoundRect;
	fake_host.blit = fakeBlit;
	fake_host.surface_size = fakeSurfaceSize;
	fake_host.surface_scale = fakeSurfaceScale;
	fake_host.surface_free = fakeSurfaceFree;
	fake_host.art_get = fakeArtGet;
	fake_host.decorate_cell = fakeDecorateCell;

	static const char* names[] = { "Apple", "Banana", "Cherry", "Date", "elder", "Fig", "Grape", "Hops", "Ivy", "Jam" };
	memset(&fake_list, 0, sizeof(fake_list));
	for (int i = 0; i < 10; i++) {
		snprintf(fake_list.items[i].path, sizeof(fake_list.items[i].path), "/Roms/%s.gb", names[i]);
		snprintf(fake_list.items[i].name, sizeof(fake_list.items[i].name), "%s", names[i]);
	}
	fake_list.count = 10;
}

static MosaicListView makeView(int selected) {
	MosaicListView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.list = &fake_list;
	v.count = fake_list.count;
	v.selected = selected;
	v.depth = 2;
	v.path = "/Roms";
	v.show_names = 1;
	v.show_art = 1;
	v.art = "";
	v.area = view_area;
	v.status = view_status;
	return v;
}

static void resetCounts(void) {
	selects = 0;
	selected_to = -1;
	art_gets = box_gets = 0;
	decorated = decorated_outside = decorated_named = 0;
	tiles = 0;
	letters[0] = '\0';
	names_drawn[0] = '\0';
	blits = blits_of_image = 0;
	scales = 0;
	replacement_asks = blits_of_video = 0;
}

static void testPlugin(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->size == sizeof(MosaicPlugin) && !strcmp(p->id, "layout-grid"));
	CHECK(p->init(&fake_host) == 0);
	CHECK(columns_setting == 0 && rows_setting == 0);
	MosaicListView v = makeView(0);
	CHECK(p->layout_init(&v) == 0);

	// the d-pad, through select_index, taking all four and nothing else
	unsigned int dpad = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN) |
		MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	MosaicInput in = { sizeof(MosaicInput), 0, 0, 0, 0 };
	unsigned int consumed = 0;
	resetCounts();
	v = makeView(1);
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(consumed == dpad);
	CHECK(selects == 1 && selected_to == 5);
	// A and the shoulders stay the launcher's
	resetCounts();
	consumed = 0;
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_R1);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(consumed == dpad && selects == 0);
	// a held repeat stops at the end, a press wraps
	v = makeView(9);
	in.pressed = 0;
	in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 0);
	in.pressed = in.repeated;
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 1 && selected_to == 0);
	// left from the first, by a press
	resetCounts();
	v = makeView(0);
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 1 && selected_to == 9);

	// a frame: 4x2 cells, all decorated inside the area and below the pill,
	// Apple's thumbnail copied, Date's box copied, and letter tiles for
	// Banana (no art), Cherry (loading), elder, Fig, Grape and Hops
	resetCounts();
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 8 && decorated_outside == 0 && decorated_named == 8);
	CHECK(scales == 2 && copy_count == 2);
	CHECK(copies[0].w == 220 && copies[0].h == 193 && copies[1].w == 220); // the tile less the name
	CHECK(tiles == 6);
	CHECK_STR(letters, "BCEFGH");
	CHECK(box_gets == 6); // everything with no thumbnail, not Cherry, still loading
	CHECK(blits == 2 && blits_of_image == 0);

	// the next frame draws the copies without asking for them again
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(scales == 0 && blits == 2);
	CHECK(art_gets == 6); // the letter tiles ask each frame
	CHECK(copy_count == 2);

	// moving down a row past the window shows rows 1 and 2: Ivy and Jam too
	resetCounts();
	v = makeView(9);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(first_row == 1);
	CHECK(decorated == 6);
	CHECK_STR(letters, "EFGHIJ");

	// names off: no name strips; art off: letter tiles only
	resetCounts();
	v = makeView(0);
	v.show_names = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 8 && decorated_named == 0);
	CHECK(scales == 2); // a new size, so new copies
	resetCounts();
	v = makeView(0);
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(tiles == 8 && art_gets == 0 && blits == 0);

	// with no room for a copy, the cache's image is drawn as it is
	resetCounts();
	cacheClear();
	scale_fail = 1;
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_image == 2);
	scale_fail = 0;

	// quit frees the copies left
	copy_count = 0;
	frees = 0;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(copy_count == 2);
	p->quit();
	CHECK(frees == 2);

	// a columns setting
	columns_cfg = "3";
	CHECK(p->init(&fake_host) == 0);
	CHECK(columns_setting == 3);
	resetCounts();
	consumed = 0;
	v = makeView(1);
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selected_to == 4);
	columns_cfg = "auto";

	// a launcher without art_get is declined
	fake_host.art_get = NULL;
	CHECK(p->init(&fake_host) == 1);
}

static void testReplacement(void) {
	setUp();
	fake_host.art_replacement = fakeArtReplacement;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	cacheClear();
	copy_count = 0;

	// Banana, selected, has no art: its video takes the letter tile's place,
	// fitted inside the tile's art, straight from the plugin's surface
	replaced_index = 1;
	resetCounts();
	MosaicListView v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 1 && blit_video.w == 220 && blit_video.h == 146);
	CHECK(tiles == 5 && strchr(letters, 'B') == NULL);
	CHECK(replacement_asks > 0);
	int copies_made = scales;
	// the next frame draws it again, and copies nothing more
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 1 && scales == 0 && copies_made == 2);
	// Apple, selected, has art of its own, which its video stands in for
	replaced_index = 0;
	resetCounts();
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 1 && blits == 2);
	// gone: Banana's letter again
	replaced_index = -1;
	resetCounts();
	v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 0 && tiles == 6);
	// art off: none
	replaced_index = 1;
	resetCounts();
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 0 && tiles == 8);
	replaced_index = -1;
	p->quit();
}

static void testHiddenNames(void) {
	setUp();
	fake_host.art_replacement = fakeArtReplacement;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	cacheClear();
	copy_count = 0;

	// Apple, Banana, Cherry and Date hidden: Apple's thumbnail and Date's
	// box stand in for their names, and Banana (no art) and Cherry
	// (loading) keep theirs on their letter tiles
	unsigned char hidden[10] = { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 };
	resetCounts();
	MosaicListView v = makeView(0);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(names_drawn, "Banana,Cherry,elder,Fig,Grape,Hops,");
	CHECK(decorated == 8 && decorated_named == 6);
	CHECK(tiles == 6 && blits == 2);
	// the art keeps its size, with the strip left blank
	CHECK(copies[0].w == 220 && copies[0].h == 193);

	// selected, the same: Apple's name stays hidden and Banana's shows
	resetCounts();
	v = makeView(1);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(names_drawn, "Banana,Cherry,elder,Fig,Grape,Hops,");

	// a replacement is art: Banana's video stands in for its name
	replaced_index = 1;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 1);
	CHECK_STR(names_drawn, "Cherry,elder,Fig,Grape,Hops,");
	replaced_index = -1;

	// art off: letter tiles everywhere, so every name shows
	resetCounts();
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 8);
	CHECK_STR(names_drawn, "Apple,Banana,Cherry,Date,elder,Fig,Grape,Hops,");

	// names off: none, hidden or not
	resetCounts();
	v = makeView(0);
	v.names_hidden = hidden;
	v.show_names = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 0 && names_drawn[0] == '\0');

	// a view from before names_hidden, too short to have it, hides nothing
	resetCounts();
	v = makeView(0);
	v.names_hidden = hidden;
	v.size = offsetof(MosaicListView, names_hidden);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 8);
	p->quit();
}

static void testCacheLimit(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	cacheClear();
	// a limit of 2: the least recently drawn copy goes for a third
	MosaicRect a = { 0, 0, 10, 10 };
	copy_count = 0;
	frees = 0;
	frame = 1;
	CHECK(drawArt(&screen_surface, &fake_list.items[0], 0, &a, 2) == 1);
	frame = 2;
	MosaicRect b = { 0, 0, 20, 20 };
	CHECK(drawArt(&screen_surface, &fake_list.items[0], 0, &b, 2) == 1);
	frame = 3;
	CHECK(drawArt(&screen_surface, &fake_list.items[0], 0, &a, 2) == 1); // a hit, now the newer
	MosaicRect c = { 0, 0, 30, 30 };
	CHECK(drawArt(&screen_surface, &fake_list.items[0], 0, &c, 2) == 1);
	CHECK(frees == 1 && copy_count == 3);
	CHECK(cacheFind("/Roms/Apple.gb", 10, 10) != NULL);
	CHECK(cacheFind("/Roms/Apple.gb", 20, 20) == NULL);
	CHECK(cacheFind("/Roms/Apple.gb", 30, 30) != NULL);
	// no art, no copy
	CHECK(drawArt(&screen_surface, &fake_list.items[1], 1, &a, 2) == 0);
	cacheClear();
}

int main(void) {
	testCount();
	testAuto();
	testGrid();
	testParts();
	testWindow();
	testMove();
	testLetter();
	testShowName();
	testPlugin();
	testReplacement();
	testHiddenNames();
	testCacheLimit();
	CHECK_DONE();
}
