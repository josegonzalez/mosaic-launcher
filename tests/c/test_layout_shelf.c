// The cartridge shelf layout plugin: its model on its own, then the plugin
// against a fake host, for the d-pad, the cartridges it draws and
// decorates, their shapes, letter labels and name tags, the shelves in
// view, the art copies it keeps, and its lift.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/layout-shelf/layout_shelf.c"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static int within(const MosaicRect* a, const MosaicRect* b) {
	return a->x >= b->x && a->y >= b->y && a->x + a->w <= b->x + b->w && a->y + a->h <= b->y + b->h;
}

static void testShapes(void) {
	int w, h;
	LS_aspect(LS_SHAPE_GB, &w, &h);
	CHECK(w == 57 && h == 65);
	LS_aspect(LS_SHAPE_GBA, &w, &h);
	CHECK(w == 57 && h == 35);
	LS_aspect(LS_SHAPE_CASE, &w, &h);
	CHECK(w == 5 && h == 7);
	LS_aspect(99, &w, &h);
	CHECK(w == 5 && h == 7);
	CHECK_STR(LS_shapeName(LS_SHAPE_GB), "GB cartridges");
	CHECK_STR(LS_shapeName(LS_SHAPE_GBA), "GBA cartridges");
	CHECK_STR(LS_shapeName(LS_SHAPE_CASE), "cases");

	CHECK(LS_shapeForTag("GB") == LS_SHAPE_GB);
	CHECK(LS_shapeForTag("gbc") == LS_SHAPE_GB);
	CHECK(LS_shapeForTag("SGB") == LS_SHAPE_GB);
	CHECK(LS_shapeForTag("GBA") == LS_SHAPE_GBA);
	CHECK(LS_shapeForTag("MGBA") == LS_SHAPE_GBA);
	CHECK(LS_shapeForTag("SFC") == LS_SHAPE_CASE);
	CHECK(LS_shapeForTag("") == LS_SHAPE_CASE);
	CHECK(LS_shapeForTag(NULL) == LS_SHAPE_CASE);

	char tag[16];
	LS_systemTag("/mnt/SDCARD/Roms/Game Boy (GB)/Tetris.gb", 0, tag, sizeof(tag));
	CHECK_STR(tag, "GB");
	LS_systemTag("/mnt/SDCARD/Roms/Game Boy Advance (GBA)", 1, tag, sizeof(tag));
	CHECK_STR(tag, "GBA");
	LS_systemTag("/mnt/SDCARD/Roms/Game Boy Advance (GBA)/", 1, tag, sizeof(tag));
	CHECK_STR(tag, "GBA");
	// the outermost folder's, not a subfolder's or the game's
	LS_systemTag("/mnt/SDCARD/Roms/Game Boy (GB)/Pokemon (USA)/Pokemon (USA).gb", 0, tag, sizeof(tag));
	CHECK_STR(tag, "GB");
	LS_systemTag("/mnt/SDCARD/Roms/Hacks/Super Famicom (SFC)/Mario.sfc", 0, tag, sizeof(tag));
	CHECK_STR(tag, "SFC");
	// a game's own name isn't its folder, unless it's a folder
	LS_systemTag("/mnt/SDCARD/Roms/Tetris (GB)", 0, tag, sizeof(tag));
	CHECK_STR(tag, "");
	LS_systemTag("/mnt/SDCARD/Roms/Tetris (GB)", 1, tag, sizeof(tag));
	CHECK_STR(tag, "GB");
	LS_systemTag("/mnt/SDCARD/Roms/Empty ()/x.gb", 0, tag, sizeof(tag));
	CHECK_STR(tag, "");
	LS_systemTag("/mnt/SDCARD/Recently Played", 1, tag, sizeof(tag));
	CHECK_STR(tag, "");
	LS_systemTag("Tetris.gb", 0, tag, sizeof(tag));
	CHECK_STR(tag, "");
	LS_systemTag(NULL, 0, tag, sizeof(tag));
	CHECK_STR(tag, "");
	char small[3];
	LS_systemTag("/Roms/Game Boy Advance (GBA)", 1, small, sizeof(small));
	CHECK_STR(small, "GB");

	CHECK(LS_shapeOf("/Roms/Game Boy (GB)/Tetris.zip", 0) == LS_SHAPE_GB);
	CHECK(LS_shapeOf("/Roms/Game Boy Color (GBC)/Zelda.gbc", 0) == LS_SHAPE_GB);
	CHECK(LS_shapeOf("/Roms/Game Boy Advance (MGBA)/Metroid.gba", 0) == LS_SHAPE_GBA);
	CHECK(LS_shapeOf("/Roms/Game Boy Advance (GBA)", 1) == LS_SHAPE_GBA);
	CHECK(LS_shapeOf("/Roms/Super Nintendo (SFC)/Mario.sfc", 0) == LS_SHAPE_CASE);
	// the tag wins over the extension, and with none the extension says
	CHECK(LS_shapeOf("/Roms/Super Game Boy (SFC)/Tetris.gb", 0) == LS_SHAPE_CASE);
	CHECK(LS_shapeOf("/Peeker/Apple.gb", 0) == LS_SHAPE_GB);
	CHECK(LS_shapeOf("/Peeker/Apple.GBA", 0) == LS_SHAPE_GBA);
	CHECK(LS_shapeOf("/Peeker/Apple.gb.zip", 0) == LS_SHAPE_CASE);
	CHECK(LS_shapeOf("/Peeker.gb/Apple", 0) == LS_SHAPE_CASE);
	CHECK(LS_shapeOf("/Tools/Files.pak", 1) == LS_SHAPE_CASE);
	CHECK(LS_shapeOf("/Peeker/Folder.gb", 1) == LS_SHAPE_CASE);
	CHECK(LS_shapeOf(NULL, 0) == LS_SHAPE_CASE);

	int gb[] = { LS_SHAPE_GB, LS_SHAPE_GB, LS_SHAPE_GB };
	int mixed[] = { LS_SHAPE_GB, LS_SHAPE_GBA };
	CHECK(LS_commonShape(gb, 3) == LS_SHAPE_GB);
	CHECK(LS_commonShape(mixed, 2) == LS_SHAPE_CASE);
	CHECK(LS_commonShape(mixed + 1, 1) == LS_SHAPE_GBA);
	CHECK(LS_commonShape(gb, 0) == LS_SHAPE_CASE);
	CHECK(LS_commonShape(NULL, 2) == LS_SHAPE_CASE);
}

static void testLayout(void) {
	CHECK(LS_count("3", 1, 4) == 3);
	CHECK(LS_count("auto", 1, 4) == 0);
	CHECK(LS_count("5", 1, 4) == 0);
	CHECK(LS_count("", 1, 4) == 0);
	CHECK(LS_count(NULL, 1, 4) == 0);
	CHECK(LS_autoShelves(519, 3) == 2);
	CHECK(LS_autoShelves(340, 2) == 2);
	CHECK(LS_autoShelves(100, 3) == 1);
	CHECK(LS_autoShelves(2000, 2) == LS_MAX_SHELVES);

	// desktop: 1024x663 above the hints, the status pill 90 tall at 30, names 36 tall
	MosaicRect area = { 0, 0, 1024, 663 };
	MosaicRect status = { 794, 30, 200, 90 };
	MosaicRect none = { 0, 0, 0, 0 };
	LSShelves s;
	CHECK(LS_layout(&area, &status, &none, 3, 0, 36, LS_SHAPE_GB, 72, &s) == 0);
	CHECK(rectIs(s.room, 12, 132, 1000, 519));
	// two shelves 259 tall: a lift of 21, cartridges 190 tall, the plank 36
	CHECK(s.shelves == 2 && s.shelf_h == 259);
	CHECK(s.lift == 21 && s.cart_h == 190 && s.plank_h == 36 && s.gap == 12);
	// GB cartridges 166 wide, five to a shelf
	CHECK(s.cols == 5 && s.col_w == 200 && s.col_x == 12 && s.shape == LS_SHAPE_GB);

	MosaicRect r = LS_cart(&s, 0, LS_SHAPE_GB, 0);
	CHECK(rectIs(r, 29, 153, 166, 190));
	// lifted all the way, into the room left above it
	r = LS_cart(&s, 0, LS_SHAPE_GB, LS_UNIT);
	CHECK(rectIs(r, 29, 132, 166, 190));
	CHECK(r.y >= s.room.y);
	r = LS_cart(&s, 0, LS_SHAPE_GB, LS_UNIT / 2);
	CHECK(r.y == 143);
	r = LS_cart(&s, 0, LS_SHAPE_GB, 5 * LS_UNIT);
	CHECK(r.y == 132);
	// the second shelf, second column
	r = LS_cart(&s, 6, LS_SHAPE_GB, 0);
	CHECK(rectIs(r, 229, 412, 166, 190));
	// a GBA cartridge in a GB column: as wide as the column less a gap, on the plank
	r = LS_cart(&s, 0, LS_SHAPE_GBA, 0);
	CHECK(rectIs(r, 18, 228, 188, 115));
	CHECK(r.y + r.h == LS_plank(&s, 0).y);
	// out of view
	CHECK(LS_cart(&s, 10, LS_SHAPE_GB, 0).w == 0);
	CHECK(LS_cart(&s, -1, LS_SHAPE_GB, 0).w == 0);

	MosaicRect plank = LS_plank(&s, 1);
	CHECK(rectIs(plank, 12, 602, 1000, 36));
	CHECK(plank.y + plank.h + s.gap <= s.room.y + s.room.h);
	CHECK(LS_plank(&s, 2).w == 0);
	MosaicRect tag = LS_tag(&s, 6, 36);
	CHECK(rectIs(tag, 218, 602, 188, 36));
	CHECK(LS_tag(&s, 6, 0).h == 0);
	CHECK(LS_tag(&s, 10, 36).h == 0);

	// labels, inside their cartridges and over their middles
	r = LS_cart(&s, 0, LS_SHAPE_GB, 0);
	MosaicRect label = LS_label(LS_SHAPE_GB, &r);
	CHECK(rectIs(label, 48, 194, 126, 114));
	for (int shape = 0; shape < LS_SHAPE_COUNT; shape++) {
		r = LS_cart(&s, 0, shape, 0);
		label = LS_label(shape, &r);
		CHECK(within(&label, &r));
		CHECK(label.x < r.x + r.w / 2 && label.x + label.w > r.x + r.w / 2);
		// the middle of the cartridge and its lift together, as decorators see it
		int mid = r.y + (r.h + s.lift) / 2;
		CHECK(label.y < r.y + r.h / 2 && label.y + label.h > mid);
	}

	// cases: six to a shelf; GBA cartridges: three
	CHECK(LS_layout(&area, &status, &none, 3, 0, 36, LS_SHAPE_CASE, 72, &s) == 0);
	CHECK(s.cols == 6 && s.col_w == 166 && s.col_x == 14);
	CHECK(rectIs(LS_cart(&s, 0, LS_SHAPE_CASE, 0), 29, 153, 135, 190));
	CHECK(LS_layout(&area, &status, &none, 3, 0, 36, LS_SHAPE_GBA, 72, &s) == 0);
	CHECK(s.cols == 3 && s.col_w == 333 && s.col_x == 12);
	CHECK(rectIs(LS_cart(&s, 0, LS_SHAPE_GBA, 0), 24, 153, 309, 190));
	// a bad shape is a case
	CHECK(LS_layout(&area, &status, &none, 3, 0, 36, 7, 72, &s) == 0);
	CHECK(s.shape == LS_SHAPE_CASE);

	// no names: a thin plank and taller cartridges
	CHECK(LS_layout(&area, &status, &none, 3, 0, 0, LS_SHAPE_GB, 72, &s) == 0);
	CHECK(s.plank_h == 12 && s.lift == 23 && s.cart_h == 212);

	// four shelves when they fit, fewer when the cartridges would be too short
	CHECK(LS_layout(&area, &status, &none, 3, 4, 36, LS_SHAPE_GB, 72, &s) == 0);
	CHECK(s.shelves == 4 && s.cart_h == 73 && s.lift == 8);
	CHECK(LS_layout(&area, &status, &none, 3, 4, 36, LS_SHAPE_GB, 80, &s) == 0);
	CHECK(s.shelves == 3 && s.cart_h == 113);
	CHECK(LS_layout(&area, &status, &none, 3, 9, 36, LS_SHAPE_GB, 72, &s) == 0);
	CHECK(s.shelves == 4);
	// many narrow columns stop at the most
	MosaicRect wide = { 0, 0, 4000, 300 };
	CHECK(LS_layout(&wide, &none, &none, 1, 1, 0, LS_SHAPE_GB, 8, &s) == 0);
	CHECK(s.cols == LS_MAX_COLUMNS);

	// header widgets taller than the pill push the shelves down
	MosaicRect header = { 600, 30, 180, 120 };
	CHECK(LS_layout(&area, &status, &header, 3, 0, 36, LS_SHAPE_GB, 72, &s) == 0);
	CHECK(s.room.y == 162);

	// too small for one shelf
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(LS_layout(&tiny, &none, &none, 3, 0, 36, LS_SHAPE_GB, 72, &s) == -1);
	MosaicRect gone = { 0, 0, 20, 20 };
	CHECK(LS_layout(&gone, &none, &none, 3, 0, 0, LS_SHAPE_GB, 1, &s) == -1);
	LSShelves empty;
	memset(&empty, 0, sizeof(empty));
	CHECK(LS_cart(&empty, 0, LS_SHAPE_GB, 0).w == 0);
	CHECK(LS_tag(&empty, 0, 36).w == 0);
}

static void testWindow(void) {
	// 5 to a shelf, 2 in view, 23 entries on 5 shelves
	CHECK(LS_window(0, 0, 23, 5, 2) == 0);
	CHECK(LS_window(0, 9, 23, 5, 2) == 0);
	CHECK(LS_window(0, 10, 23, 5, 2) == 1);
	CHECK(LS_window(1, 4, 23, 5, 2) == 0);
	CHECK(LS_window(0, 22, 23, 5, 2) == 3);
	// back up only as far as it takes
	CHECK(LS_window(3, 12, 23, 5, 2) == 2);
	CHECK(LS_window(3, 17, 23, 5, 2) == 3);
	// never past the last shelf
	CHECK(LS_window(9, 22, 23, 5, 2) == 3);
	CHECK(LS_window(2, 3, 4, 5, 2) == 0);
	CHECK(LS_window(0, 0, 0, 5, 2) == 0);
	CHECK(LS_window(0, 99, 23, 5, 2) == 3);
}

static void testMove(void) {
	// along a shelf and on to the next
	CHECK(LS_move(3, 23, 5, MOSAIC_BUTTON_RIGHT, 1) == 4);
	CHECK(LS_move(4, 23, 5, MOSAIC_BUTTON_RIGHT, 0) == 5);
	CHECK(LS_move(5, 23, 5, MOSAIC_BUTTON_LEFT, 0) == 4);
	// between shelves, keeping the column
	CHECK(LS_move(3, 23, 5, MOSAIC_BUTTON_DOWN, 0) == 8);
	CHECK(LS_move(8, 23, 5, MOSAIC_BUTTON_UP, 0) == 3);
	// past the ends: a press wraps, a repeat stays
	CHECK(LS_move(0, 23, 5, MOSAIC_BUTTON_LEFT, 1) == 22);
	CHECK(LS_move(0, 23, 5, MOSAIC_BUTTON_LEFT, 0) == 0);
	CHECK(LS_move(22, 23, 5, MOSAIC_BUTTON_RIGHT, 1) == 0);
	CHECK(LS_move(22, 23, 5, MOSAIC_BUTTON_RIGHT, 0) == 22);
	CHECK(LS_move(1, 23, 5, MOSAIC_BUTTON_UP, 1) == 21);
	CHECK(LS_move(3, 23, 5, MOSAIC_BUTTON_UP, 1) == 18); // the last shelf has nothing in column 3
	CHECK(LS_move(3, 23, 5, MOSAIC_BUTTON_UP, 0) == 3);
	CHECK(LS_move(21, 23, 5, MOSAIC_BUTTON_DOWN, 1) == 1);
	CHECK(LS_move(21, 23, 5, MOSAIC_BUTTON_DOWN, 0) == 21);
	// down to a short last shelf with nothing below: the last entry
	CHECK(LS_move(18, 23, 5, MOSAIC_BUTTON_DOWN, 0) == 22);
	// nowhere to go
	CHECK(LS_move(0, 1, 5, MOSAIC_BUTTON_RIGHT, 1) == 0);
	CHECK(LS_move(12, 10, 5, MOSAIC_BUTTON_LEFT, 1) == 12);
	CHECK(LS_move(3, 10, 5, MOSAIC_BUTTON_A, 1) == 3);
	CHECK(LS_move(3, 10, 0, MOSAIC_BUTTON_LEFT, 1) == 3);
}

static void testEaseAndLetter(void) {
	CHECK(LS_ease(0, 120) == 0);
	CHECK(LS_ease(60, 120) == 750);
	CHECK(LS_ease(120, 120) == LS_UNIT);
	CHECK(LS_ease(500, 120) == LS_UNIT);
	CHECK(LS_ease(10, 0) == LS_UNIT);
	int last = -1;
	for (unsigned long t = 0; t <= 120; t += 10) {
		int e = LS_ease(t, 120);
		CHECK(e >= last);
		last = e;
	}

	char out[8];
	LS_letter("super mario", out, sizeof(out));
	CHECK_STR(out, "S");
	LS_letter("'Splosion Man", out, sizeof(out));
	CHECK_STR(out, "S");
	LS_letter("007 GoldenEye", out, sizeof(out));
	CHECK_STR(out, "0");
	LS_letter("\xC3\xA9mile", out, sizeof(out));
	CHECK_STR(out, "\xC3\xA9");
	LS_letter("  !!", out, sizeof(out));
	CHECK_STR(out, "!");
	LS_letter("   ", out, sizeof(out));
	CHECK_STR(out, "");
	LS_letter(NULL, out, sizeof(out));
	CHECK_STR(out, "");
	LS_letter("\xC3\xA9", out, 2);
	CHECK_STR(out, "");
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicEntry {
	char path[64];
	char name[64];
	int type;
};
struct MosaicList {
	MosaicEntry items[32];
	int count;
};
struct MosaicSurface {
	int w, h;
	int owned; // made by the plugin
	const MosaicSurface* from; // what it's a copy of
};

static MosaicList fake_list;
static MosaicSurface screen_surface = { 1024, 768, 0, NULL };
static MosaicSurface thumb_image = { 400, 300, 0, NULL };
static MosaicSurface box_image = { 300, 400, 0, NULL };
static MosaicSurface copies[128];
static int copy_count = 0, frees = 0, scales = 0, scale_fail = 0, last_fit = -1;
static int selects = 0, selected_to = -1;
static int decorated = 0, decorated_outside = 0, decorated_named = 0;
static int decorated_index[32];
static MosaicRect decorated_rect[32];
static MosaicRect decorated_name[32];
static MosaicRect view_area = { 0, 0, 1024, 663 };
static MosaicRect view_status = { 794, 30, 200, 90 };
static int labels = 0, pills = 0, gb_bodies = 0, gba_bodies = 0, cases = 0, planks = 0;
static char letters[32];
static char named[128];
static int blits = 0, blits_of_image = 0, blits_of_box = 0;
static MosaicRect last_blit, last_blit_src;
static int redraws = 0;
static const char* animate_cfg = "on";
static const char* shelves_cfg = "auto";
static char logged[256];

static void fakeLog(const char* id, const char* m) {
	(void)id;
	snprintf(logged, sizeof(logged), "%s", m);
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "animate")) return animate_cfg;
	if (!strcmp(key, "shelves")) return shelves_cfg;
	return fallback;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) {
	return i >= 0 && i < l->count ? (MosaicEntry*)&l->items[i] : NULL;
}
static int fakeSelectIndex(int i) {
	selects++;
	selected_to = i;
	return 0;
}
static void fakeRequestRedraw(void) { redraws++; }
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
	(void)t; (void)x; (void)y; (void)max_width;
	// letters in the large font, names in the small one, the selected name in black
	char* to = font == MOSAIC_FONT_LARGE ? letters : named;
	size_t size = font == MOSAIC_FONT_LARGE ? sizeof(letters) : sizeof(named);
	if (strlen(to) + strlen(text) + 2 < size) {
		if (font != MOSAIC_FONT_LARGE && c.r == 0) strcat(to, "*");
		strcat(to, text);
		if (font != MOSAIC_FONT_LARGE) strcat(to, " ");
	}
	return 0;
}
static int colorIs(MosaicColor c, unsigned int rgb) {
	return c.r == (rgb >> 16 & 0xFF) && c.g == (rgb >> 8 & 0xFF) && c.b == (rgb & 0xFF);
}
static void countBody(MosaicColor c) {
	if (colorIs(c, 0xB4B4BC)) gb_bodies++;
	if (colorIs(c, 0x5A5A66)) gba_bodies++;
	if (colorIs(c, 0x26262C)) cases++;
}
static int fakeFillRect(MosaicSurface* t, const MosaicRect* r, MosaicColor c) {
	(void)t; (void)r;
	if (colorIs(c, 0x7A5230)) planks++;
	return 0;
}
static int fakeFillRoundRect(MosaicSurface* t, const MosaicRect* r, int radius, MosaicColor c) {
	(void)t; (void)r; (void)radius;
	if (colorIs(c, 0x9B2257)) labels++;
	if (colorIs(c, 0xFFFFFF)) pills++;
	countBody(c);
	return 0;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr) {
	(void)t;
	blits++;
	if (s == &thumb_image || s == &box_image) blits_of_image++;
	if (s == &box_image || s->from == &box_image) blits_of_box++;
	if (dr) last_blit = *dr;
	last_blit_src = sr ? *sr : (MosaicRect){ 0, 0, 0, 0 };
	return 0;
}
static int fakeSurfaceSize(const MosaicSurface* s, int* w, int* h) {
	*w = s->w;
	*h = s->h;
	return 0;
}
static MosaicSurface* fakeSurfaceScale(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)id;
	scales++;
	last_fit = fit;
	if (scale_fail || copy_count >= 128) return NULL;
	MosaicSurface* s = &copies[copy_count++];
	*s = (MosaicSurface){ w, h, 1, src };
	return s;
}
static int fakeSurfaceFree(const char* id, MosaicSurface* s) {
	(void)id;
	if (!s->owned) return -1;
	s->owned = 0;
	frees++;
	return 0;
}
static int fakeArtGet(const MosaicEntry* e, int kind, const MosaicSurface** out) {
	*out = NULL;
	// Apple has a thumbnail only, Banana both, Date a box only, Cherry is loading
	if (kind == MOSAIC_ART_THUMBNAIL && (!strcmp(e->name, "Apple") || !strcmp(e->name, "Banana"))) {
		*out = &thumb_image;
		return MOSAIC_ART_READY;
	}
	if (kind == MOSAIC_ART_BOX && (!strcmp(e->name, "Banana") || !strcmp(e->name, "Date"))) {
		*out = &box_image;
		return MOSAIC_ART_READY;
	}
	if (!strcmp(e->name, "Cherry")) return MOSAIC_ART_LOADING;
	return MOSAIC_ART_MISSING;
}
static int fakeDecorateCell(MosaicSurface* s, int index, const MosaicRect* r, const MosaicRect* name) {
	(void)s;
	if (decorated < 32) {
		decorated_index[decorated] = index;
		decorated_rect[decorated] = *r;
		decorated_name[decorated] = name ? *name : (MosaicRect){ 0, 0, 0, 0 };
	}
	decorated++;
	if (r->x < view_area.x || r->y < view_status.y + view_status.h || r->x + r->w > view_area.w ||
		r->y + r->h > view_area.h) {
		decorated_outside++;
	}
	if (name && name->w > 0) {
		decorated_named++;
		if (!within(name, r)) decorated_outside++;
	}
	return 0;
}

static MosaicHost fake_host;

static void setFolder(const char* folder, int n) {
	static const char* names[] = { "Apple", "Banana", "Cherry", "Date", "elder", "Fig", "Grape", "Hops", "Ivy", "Jam",
		"Kiwi", "Lime", "Mango", "Nut", "Olive", "Pear", "Quince", "Rye", "Sage", "Tea", "Ugli", "Vine", "Wheat" };
	memset(&fake_list, 0, sizeof(fake_list));
	for (int i = 0; i < n && i < 23; i++) {
		snprintf(fake_list.items[i].path, sizeof(fake_list.items[i].path), "%s/%s.gb", folder, names[i]);
		snprintf(fake_list.items[i].name, sizeof(fake_list.items[i].name), "%s", names[i]);
		fake_list.items[i].type = MOSAIC_ENTRY_ROM;
	}
	fake_list.count = n;
}

static void setUp(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.entry_path = fakeEntryPath;
	fake_host.entry_name = fakeEntryName;
	fake_host.entry_type = fakeEntryType;
	fake_host.list_get = fakeListGet;
	fake_host.select_index = fakeSelectIndex;
	fake_host.request_redraw = fakeRequestRedraw;
	fake_host.screen_scale = fakeScale;
	fake_host.theme_color = fakeTheme;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTextTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.fill_rect = fakeFillRect;
	fake_host.fill_round_rect = fakeFillRoundRect;
	fake_host.blit = fakeBlit;
	fake_host.surface_size = fakeSurfaceSize;
	fake_host.surface_scale = fakeSurfaceScale;
	fake_host.surface_free = fakeSurfaceFree;
	fake_host.art_get = fakeArtGet;
	fake_host.decorate_cell = fakeDecorateCell;
	setFolder("/Roms/Game Boy (GB)", 12);
	animate_cfg = "on";
	shelves_cfg = "auto";
}

static MosaicListView makeView(int selected) {
	MosaicListView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.list = &fake_list;
	v.count = fake_list.count;
	v.selected = selected;
	v.depth = 2;
	v.path = "/Roms/Game Boy (GB)";
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
	decorated = decorated_outside = decorated_named = 0;
	labels = pills = gb_bodies = gba_bodies = cases = planks = 0;
	letters[0] = '\0';
	named[0] = '\0';
	blits = blits_of_image = blits_of_box = 0;
	scales = 0;
	redraws = 0;
}

static int decoratedAt(int index) {
	for (int i = 0; i < decorated && i < 32; i++) {
		if (decorated_index[i] == index) return i;
	}
	return -1;
}

static void testInput(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->size == sizeof(MosaicPlugin) && !strcmp(p->id, "layout-shelf"));
	CHECK(p->init(&fake_host) == 0);
	MosaicListView v = makeView(0);
	CHECK(p->layout_init(&v) == 0);
	CHECK_STR(logged, "2 shelves of 5 GB cartridges, 166 by 190");

	unsigned int dpad = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN) |
		MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	MosaicInput in = { sizeof(MosaicInput), 0, 0, 0, 0 };
	unsigned int consumed = 0;
	resetCounts();
	v = makeView(1);
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(consumed == dpad);
	CHECK(selects == 1 && selected_to == 2);
	// down a shelf, five along
	resetCounts();
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 1 && selected_to == 6);
	// A and the shoulders stay the launcher's
	resetCounts();
	consumed = 0;
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_R1);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(consumed == dpad && selects == 0);
	// a held repeat stops at the end, a press wraps
	v = makeView(11);
	in.pressed = 0;
	in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 0);
	in.pressed = in.repeated;
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 1 && selected_to == 0);
	// up from the top shelf: the column's lowest entry
	resetCounts();
	v = makeView(1);
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 1 && selected_to == 11);

	// a launcher without art_get is declined, and so is a screen too small
	fake_host.art_get = NULL;
	CHECK(p->init(&fake_host) == 1);
	fake_host.art_get = fakeArtGet;
	CHECK(p->init(&fake_host) == 0);
	v = makeView(0);
	v.area = (MosaicRect){ 0, 0, 100, 100 };
	v.status = (MosaicRect){ 0, 0, 0, 0 };
	CHECK(p->layout_init(&v) == 1);
	CHECK(p->layout_render(&screen_surface, &v) == -1);
	p->quit();
}

static void testRender(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	copy_count = 0;
	MosaicListView v = makeView(0);
	CHECK(p->layout_init(&v) == 0);

	// the first two shelves of five, ten cartridges, all GB
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 10 && decorated_outside == 0 && decorated_named == 10);
	CHECK(planks == 2 && gb_bodies == 10 && gba_bodies == 0 && cases == 0);
	CHECK(decoratedAt(10) < 0);
	// the selected one stands lifted, its cell from its top to its tag
	int at = decoratedAt(0);
	CHECK(at >= 0 && rectIs(decorated_rect[at], 29, 132, 166, 247));
	at = decoratedAt(1);
	CHECK(at >= 0 && decorated_rect[at].y == 153);
	// Apple's thumbnail and Banana's and Date's boxes, copied once each to
	// fill their labels; Cherry's loading and the rest have none, so letters
	CHECK(scales == 3 && last_fit == MOSAIC_FIT_COVER);
	CHECK(blits == 3 && blits_of_box == 2 && rectIs(last_blit, 648, 194, 126, 114));
	CHECK_STR(letters, "CEFGHIJ");
	CHECK(labels == 7);
	// names on the planks, the selected one on a pill
	CHECK_STR(named, "*Apple Banana Cherry Date elder Fig Grape Hops Ivy Jam ");
	CHECK(pills == 2); // the selected cartridge's edge and its name's pill

	// drawn again: the copies are kept
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(scales == 0 && blits == 3);

	// the shelves follow the selection, a shelf at a time
	resetCounts();
	v = makeView(11);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 7 && decoratedAt(5) >= 0 && decoratedAt(11) >= 0 && decoratedAt(4) < 0);
	CHECK(planks == 2);
	resetCounts();
	v = makeView(6);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decoratedAt(5) >= 0 && decoratedAt(0) < 0);
	resetCounts();
	v = makeView(2);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decoratedAt(0) >= 0 && decoratedAt(10) < 0);

	// art off: letters on every label
	resetCounts();
	v = makeView(0);
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits == 0 && labels == 10);

	// names off: thin planks, no tags, taller cartridges
	resetCounts();
	v = makeView(0);
	v.show_names = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 10 && decorated_named == 0 && named[0] == '\0' && pills == 1);
	CHECK_STR(logged, "2 shelves of 5 GB cartridges, 185 by 212");

	// a hidden name stays hidden over art, and shows on a letter label
	resetCounts();
	unsigned char hidden[12] = { 1, 0, 1 };
	v = makeView(0);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 9);
	CHECK(decorated_name[decoratedAt(0)].w == 0 && decorated_name[decoratedAt(2)].w > 0);
	// an older launcher's view without names_hidden
	resetCounts();
	v.size = offsetof(MosaicListView, names_hidden);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 10);

	// no copy can be made: straight from the cache's image, its middle filling the label
	cacheClear();
	resetCounts();
	scale_fail = 1;
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_image == 3);
	CHECK(rectIs(last_blit_src, 0, 64, 300, 271)); // Date's box, cut to the label's shape
	scale_fail = 0;
	p->quit();
	CHECK(copy_count > 0);
}

static void testShapesPlugin(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	copy_count = 0;

	// a GBA folder: three wide cartridges a shelf
	setFolder("/Roms/Game Boy Advance (GBA)", 8);
	MosaicListView v = makeView(0);
	v.path = "/Roms/Game Boy Advance (GBA)";
	CHECK(p->layout_init(&v) == 0);
	CHECK_STR(logged, "2 shelves of 3 GBA cartridges, 309 by 190");
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(gba_bodies == 6 && gb_bodies == 0);

	// any other system's folder: cases
	setFolder("/Roms/Super Nintendo (SFC)", 8);
	v = makeView(0);
	v.path = "/Roms/Super Nintendo (SFC)";
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(logged, "2 shelves of 6 cases, 135 by 190");
	CHECK(cases == 8 && gb_bodies == 0);

	// a folder without a tag goes by its games: all .gb, so GB columns
	setFolder("/Peeker", 3);
	v = makeView(0);
	v.path = "/Peeker";
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(logged, "2 shelves of 5 GB cartridges, 166 by 190");
	CHECK(gb_bodies == 3);

	// mixed, as in Recently Played: case columns, each game its own shape
	setFolder("/Recently Played", 4);
	snprintf(fake_list.items[1].path, sizeof(fake_list.items[1].path), "/Roms/Game Boy Advance (GBA)/Banana.gba");
	snprintf(fake_list.items[2].path, sizeof(fake_list.items[2].path), "/Roms/Tools/Cherry");
	fake_list.items[2].type = MOSAIC_ENTRY_DIR;
	v = makeView(0);
	v.path = "/Recently Played";
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(logged, "2 shelves of 6 cases, 135 by 190");
	CHECK(gb_bodies == 2 && gba_bodies == 1 && cases == 1);
	CHECK(decorated == 4 && decorated_outside == 0);

	// the shelves setting
	shelves_cfg = "3";
	CHECK(p->init(&fake_host) == 0);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(!strncmp(logged, "3 shelves of ", 13));
	shelves_cfg = "1";
	CHECK(p->init(&fake_host) == 0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(!strncmp(logged, "1 shelf of ", 11));
	p->quit();
}

static void testLift(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	copy_count = 0;
	// a new folder starts with the selection lifted, at rest
	setFolder("/Roms/Game Boy Color (GBC)", 12);
	MosaicListView v = makeView(0);
	v.path = "/Roms/Game Boy Color (GBC)";
	p->tick(1000);
	CHECK(p->layout_init(&v) == 0);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_rect[decoratedAt(0)].y == 132);
	p->tick(1016);
	CHECK(redraws == 0);

	// a move lifts the new one as the old one drops back
	v.selected = 1;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_rect[decoratedAt(1)].y == 153 && decorated_rect[decoratedAt(0)].y == 132);
	p->tick(1076); // halfway: three quarters of the way up, eased
	CHECK(redraws == 1);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_rect[decoratedAt(1)].y == 138 && decorated_rect[decoratedAt(0)].y == 148);
	CHECK(scales == 0); // the label copies don't change size on the way
	p->tick(1200);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_rect[decoratedAt(1)].y == 132 && decorated_rect[decoratedAt(0)].y == 153);
	p->tick(1216);
	CHECK(redraws == 0);

	// a lift nobody draws stops asking for redraws
	v.selected = 2;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	resetCounts();
	p->tick(1300);
	p->tick(1500);
	p->tick(1600);
	CHECK(redraws == 2);

	// with animate off, the selection is lifted at once
	animate_cfg = "off";
	CHECK(p->init(&fake_host) == 0);
	v.selected = 3;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_rect[decoratedAt(3)].y == 132 && decorated_rect[decoratedAt(2)].y == 153);
	p->tick(1700);
	CHECK(redraws == 0);
	p->quit();
}

static void testCache(void) {
	setUp();
	CHECK(fake_host.surface_free != NULL);
	host = &fake_host;
	copy_count = 0;
	frees = 0;
	MosaicRect a = { 0, 0, 20, 20 };
	// a limit of two: a third copy frees the least recently drawn
	frame = 1;
	CHECK(drawArt(&screen_surface, &fake_list.items[0], &a, 2) == 1);
	frame = 2;
	CHECK(drawArt(&screen_surface, &fake_list.items[1], &a, 2) == 1);
	frame = 3;
	CHECK(drawArt(&screen_surface, &fake_list.items[3], &a, 2) == 1);
	CHECK(frees == 1);
	CHECK(cacheFind(fake_list.items[0].path, 20, 20) == NULL);
	CHECK(cacheFind(fake_list.items[3].path, 20, 20) != NULL);
	// another size is another copy
	CHECK(cacheFind(fake_list.items[3].path, 30, 30) == NULL);
	// no art, no copy
	CHECK(drawArt(&screen_surface, &fake_list.items[4], &a, 2) == 0);
	cacheClear();
	CHECK(frees == 3);
}

int main(void) {
	testShapes();
	testLayout();
	testWindow();
	testMove();
	testEaseAndLetter();
	testInput();
	testRender();
	testShapesPlugin();
	testLift();
	testCache();
	CHECK_DONE();
}
