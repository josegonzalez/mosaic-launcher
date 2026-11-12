// The carousel layout plugin: its model on its own, then the plugin
// against a fake host, for the d-pad, the covers it draws and decorates,
// its letter cards and names, the art copies it keeps, and its slide.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/layout-carousel/layout_carousel.c"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void testStrip(void) {
	// desktop: 1024x663 above the hints, the status pill 90 tall at 30, names 60 tall
	MosaicRect area = { 0, 0, 1024, 663 };
	MosaicRect status = { 794, 30, 200, 90 };
	MosaicRect none = { 0, 0, 0, 0 };
	LCStrip s;
	CHECK(LC_strip(&area, &status, &none, 3, 60, 48, &s) == 0);
	// below the pill and a gap: 1000 by 519, the selected cover 3/10 of its width
	CHECK(rectIs(s.room, 12, 132, 1000, 519));
	CHECK(s.size[0] == 300 && s.size[1] == 180 && s.size[2] == 108);
	CHECK(s.sides == 2); // a third 64 wide would end at 1050, past the room
	CHECK(s.centre[1] == 252 && s.centre[2] == 408);
	CHECK(s.cx == 512 && s.cy == 355);
	CHECK(s.gap == 12);
	// the name a gap under the selected cover, across the room
	CHECK(rectIs(s.name, 12, 517, 1000, 60));
	CHECK(s.name.y + s.name.h <= s.room.y + s.room.h);

	MosaicRect r = LC_slot(&s, 0);
	CHECK(rectIs(r, 362, 205, 300, 300));
	r = LC_slot(&s, LC_UNIT);
	CHECK(rectIs(r, 674, 265, 180, 180));
	r = LC_slot(&s, -2 * LC_UNIT);
	CHECK(rectIs(r, 50, 301, 108, 108));
	CHECK(r.x >= s.room.x);
	// halfway between the selected cover and the next, in size and place
	r = LC_slot(&s, LC_UNIT / 2);
	CHECK(rectIs(r, 518, 235, 240, 240));
	r = LC_slot(&s, -LC_UNIT / 2);
	CHECK(rectIs(r, 266, 235, 240, 240));
	// past the last side: the outermost size, a step further out
	r = LC_slot(&s, 3 * LC_UNIT);
	CHECK(rectIs(r, 986, 301, 108, 108));
	CHECK(r.x + r.w > s.room.x + s.room.w);

	// no names: the cover takes the room's height, up to 3/10 of its width;
	// covers and name centred together
	MosaicRect small = { 0, 0, 640, 400 };
	CHECK(LC_strip(&small, &none, &none, 2, 0, 32, &s) == 0);
	CHECK(rectIs(s.room, 8, 8, 624, 384));
	CHECK(s.size[0] == 187 && s.size[1] == 112 && s.size[2] == 67 && s.sides == 2);
	CHECK(s.name.h == 0);
	CHECK(s.cx == 320 && s.cy == 199);

	// header widgets taller than the pill push it down
	MosaicRect header = { 600, 30, 180, 120 };
	CHECK(LC_strip(&area, &status, &header, 3, 60, 48, &s) == 0);
	CHECK(s.room.y == 162);

	// a short room: the cover goes by the height, and the sides by min_size
	MosaicRect wide = { 0, 0, 1024, 200 };
	CHECK(LC_strip(&wide, &none, &none, 3, 60, 48, &s) == 0);
	CHECK(s.size[0] == 104 && s.size[1] == 62 && s.sides == 1); // 37 is under 48

	// too small for even the selected cover
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(LC_strip(&tiny, &none, &none, 3, 0, 48, &s) == -1);
	MosaicRect gone = { 0, 0, 20, 20 };
	CHECK(LC_strip(&gone, &none, &none, 3, 0, 1, &s) == -1);
	LCStrip empty;
	memset(&empty, 0, sizeof(empty));
	CHECK(LC_slot(&empty, 0).w == 0);
}

static void testMove(void) {
	// left and right by one; a press past an end wraps, a repeat stays
	CHECK(LC_move(3, 10, MOSAIC_BUTTON_LEFT, 1, 5) == 2);
	CHECK(LC_move(3, 10, MOSAIC_BUTTON_RIGHT, 0, 5) == 4);
	CHECK(LC_move(0, 10, MOSAIC_BUTTON_LEFT, 1, 5) == 9);
	CHECK(LC_move(0, 10, MOSAIC_BUTTON_LEFT, 0, 5) == 0);
	CHECK(LC_move(9, 10, MOSAIC_BUTTON_RIGHT, 1, 5) == 0);
	CHECK(LC_move(9, 10, MOSAIC_BUTTON_RIGHT, 0, 5) == 9);
	// up and down by a page, stopping at the ends before wrapping
	CHECK(LC_move(3, 20, MOSAIC_BUTTON_DOWN, 1, 5) == 8);
	CHECK(LC_move(8, 20, MOSAIC_BUTTON_UP, 0, 5) == 3);
	CHECK(LC_move(3, 20, MOSAIC_BUTTON_UP, 0, 5) == 0);
	CHECK(LC_move(17, 20, MOSAIC_BUTTON_DOWN, 0, 5) == 19);
	CHECK(LC_move(19, 20, MOSAIC_BUTTON_DOWN, 0, 5) == 19);
	CHECK(LC_move(19, 20, MOSAIC_BUTTON_DOWN, 1, 5) == 0);
	CHECK(LC_move(0, 20, MOSAIC_BUTTON_UP, 1, 5) == 19);
	CHECK(LC_move(0, 20, MOSAIC_BUTTON_UP, 0, 5) == 0);
	// a page of 0 is one
	CHECK(LC_move(3, 20, MOSAIC_BUTTON_DOWN, 1, 0) == 4);
	// nowhere to go
	CHECK(LC_move(0, 1, MOSAIC_BUTTON_RIGHT, 1, 5) == 0);
	CHECK(LC_move(0, 0, MOSAIC_BUTTON_RIGHT, 1, 5) == 0);
	CHECK(LC_move(12, 10, MOSAIC_BUTTON_LEFT, 1, 5) == 12);
	CHECK(LC_move(3, 10, MOSAIC_BUTTON_A, 1, 5) == 3);
}

static void testSlide(void) {
	CHECK(LC_ease(0, 150) == 0);
	CHECK(LC_ease(75, 150) == 750); // eased out: three quarters there at half time
	CHECK(LC_ease(150, 150) == LC_UNIT);
	CHECK(LC_ease(500, 150) == LC_UNIT);
	CHECK(LC_ease(10, 0) == LC_UNIT);
	int last = -1;
	for (unsigned long t = 0; t <= 150; t += 10) {
		int e = LC_ease(t, 150);
		CHECK(e >= last);
		last = e;
	}

	// moving right by one: the covers start a step to the right of their places
	CHECK(LC_slideFrom(0, 1, 2) == LC_UNIT);
	CHECK(LC_slideFrom(0, -2, 2) == -2 * LC_UNIT);
	// a move partway through a slide starts from where the covers are
	CHECK(LC_slideFrom(400, 1, 2) == 1400);
	CHECK(LC_slideFrom(-600, 1, 2) == 400);
	// too far, such as a page or a wrap, doesn't slide
	CHECK(LC_slideFrom(0, 3, 2) == 0);
	CHECK(LC_slideFrom(0, -9, 2) == 0);
	CHECK(LC_slideFrom(1500, 1, 2) == 0);
	CHECK(LC_slideFrom(0, 1, 0) == 0);
}

static void testArtAbove(void) {
	// the cover down to its name, for art decorators to go under
	MosaicRect cover = { 362, 205, 300, 300 }, name = { 12, 517, 1000, 60 };
	CHECK(rectIs(LC_artAbove(&cover, &name), 362, 205, 300, 372));
	// no name strip, or one that isn't under the cover: just the cover
	MosaicRect none = { 12, 517, 1000, 0 }, above = { 12, 100, 1000, 60 };
	CHECK(rectIs(LC_artAbove(&cover, &none), 362, 205, 300, 300));
	CHECK(rectIs(LC_artAbove(&cover, &above), 362, 205, 300, 300));
}

static void testLetter(void) {
	char out[8];
	LC_letter("super mario", out, sizeof(out));
	CHECK_STR(out, "S");
	LC_letter("'Splosion Man", out, sizeof(out));
	CHECK_STR(out, "S");
	LC_letter("007 GoldenEye", out, sizeof(out));
	CHECK_STR(out, "0");
	LC_letter("\xC3\xA9mile", out, sizeof(out));
	CHECK_STR(out, "\xC3\xA9");
	LC_letter("  !!", out, sizeof(out));
	CHECK_STR(out, "!");
	LC_letter("   ", out, sizeof(out));
	CHECK_STR(out, "");
	LC_letter(NULL, out, sizeof(out));
	CHECK_STR(out, "");
	LC_letter("\xC3\xA9", out, 2);
	CHECK_STR(out, "");
	LC_letter("\xA9z", out, sizeof(out));
	CHECK_STR(out, "Z");
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
	const MosaicSurface* from; // what it's a copy of
};

static MosaicList fake_list;
static MosaicSurface screen_surface = { 1024, 768, 0, NULL };
static MosaicSurface thumb_image = { 400, 300, 0, NULL };
static MosaicSurface box_image = { 300, 400, 0, NULL };
static MosaicSurface copies[64];
static int copy_count = 0, frees = 0, scales = 0, scale_fail = 0;
static int selects = 0, selected_to = -1;
static int thumb_gets = 0, box_gets = 0;
static int decorated = 0, decorated_outside = 0, decorated_named = 0;
static int decorated_index[16];
static MosaicRect decorated_rect[16];
static MosaicRect view_area = { 0, 0, 1024, 663 };
static MosaicRect view_status = { 794, 30, 200, 90 };
static int cards = 0, pills = 0;
static char letters[16];
static char named[64];
static int blits = 0, blits_of_image = 0, blits_of_box = 0;
static MosaicRect last_blit;
static int redraws = 0;
static const char* slide_cfg = "on";
static MosaicSurface video = { 720, 480, 0, NULL }; // another plugin's art_replace surface
static int replaced_index = -1; // the entry it's for, or -1 for none
static int replacement_asks = 0, blits_of_video = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "slide")) return slide_cfg;
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
	(void)t; (void)font; (void)x; (void)y; (void)max_width;
	// letters in the list text colour, names in the selected text colour
	if (c.r == 0xFF) {
		if (strlen(letters) + strlen(text) < sizeof(letters)) strcat(letters, text);
	} else if (strlen(named) + strlen(text) < sizeof(named)) {
		strcat(named, text);
	}
	return 0;
}
static int fakeFillRoundRect(MosaicSurface* t, const MosaicRect* r, int radius, MosaicColor c) {
	(void)t; (void)r; (void)radius;
	if (c.r == 0x9B && c.g == 0x22 && c.b == 0x57) cards++;
	if (c.r == 0xFF && c.g == 0xFF && c.b == 0xFF) pills++;
	return 0;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr) {
	(void)t; (void)sr;
	blits++;
	if (s == &video) blits_of_video++;
	if (s == &thumb_image || s == &box_image) blits_of_image++;
	if (s == &box_image || s->from == &box_image) blits_of_box++;
	if (dr) last_blit = *dr;
	return 0;
}
static int fakeSurfaceSize(const MosaicSurface* s, int* w, int* h) {
	*w = s->w;
	*h = s->h;
	return 0;
}
static MosaicSurface* fakeSurfaceScale(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)id; (void)fit;
	scales++;
	if (scale_fail || copy_count >= 64) return NULL;
	MosaicSurface* s = &copies[copy_count++];
	*s = (MosaicSurface){ w, h, 1, src };
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
	if (kind == MOSAIC_ART_THUMBNAIL) thumb_gets++;
	if (kind == MOSAIC_ART_BOX) box_gets++;
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
	if (decorated < 16) {
		decorated_index[decorated] = index;
		decorated_rect[decorated] = *r;
	}
	decorated++;
	if (r->x < view_area.x || r->y < view_status.y + view_status.h || r->x + r->w > view_area.w ||
		r->y + r->h > view_area.h) {
		decorated_outside++;
	}
	if (name && name->w > 0) {
		decorated_named++;
		if (name->x < r->x || name->y < r->y || name->x + name->w > r->x + r->w || name->y + name->h > r->y + r->h) {
			decorated_outside++;
		}
	}
	return 0;
}

static int art_decorated = 0, art_decorated_index = -1;
static MosaicRect art_decorated_rect;
static int fakeArtReplacement(int index, const MosaicSurface** out) {
	replacement_asks++;
	*out = NULL;
	if (index != replaced_index) return 0;
	*out = &video;
	return 1;
}
static int fakeDecorateArt(MosaicSurface* s, int index, const MosaicRect* art) {
	(void)s;
	art_decorated++;
	art_decorated_index = index;
	art_decorated_rect = *art;
	return 0;
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
	fake_host.request_redraw = fakeRequestRedraw;
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
	thumb_gets = box_gets = 0;
	decorated = decorated_outside = decorated_named = 0;
	cards = pills = 0;
	letters[0] = '\0';
	named[0] = '\0';
	blits = blits_of_image = blits_of_box = 0;
	scales = 0;
	redraws = 0;
	replacement_asks = blits_of_video = 0;
	art_decorated = 0;
	art_decorated_index = -1;
}

static int wasDecorated(int index) {
	for (int i = 0; i < decorated && i < 16; i++) {
		if (decorated_index[i] == index) return 1;
	}
	return 0;
}

static void testInput(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->size == sizeof(MosaicPlugin) && !strcmp(p->id, "layout-carousel"));
	CHECK(p->init(&fake_host) == 0);
	MosaicListView v = makeView(0);
	CHECK(p->layout_init(&v) == 0);

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
	// down a page: the 5 covers in view
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
	v = makeView(9);
	in.pressed = 0;
	in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 0);
	in.pressed = in.repeated;
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 1 && selected_to == 0);
	resetCounts();
	v = makeView(0);
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selects == 1 && selected_to == 9);

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
	frees = 0;

	// at the first entry: Apple in the middle with its thumbnail, Banana's
	// box (it has a thumbnail too) and Cherry's card to its right, nothing
	// to its left
	resetCounts();
	MosaicListView v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 3 && decorated_outside == 0 && decorated_named == 1);
	CHECK(wasDecorated(0) && wasDecorated(1) && wasDecorated(2));
	CHECK(decorated_index[2] == 0); // the selected one last
	// its cell takes in its name, under the cover
	CHECK(decorated_rect[2].x == 362 && decorated_rect[2].y == 205 && decorated_rect[2].w == 300 &&
		decorated_rect[2].h == 372);
	CHECK(scales == 2 && copy_count == 2);
	CHECK(copies[0].from == &box_image && copies[0].w == 180); // Banana's box, a side cover
	CHECK(copies[1].from == &thumb_image && copies[1].w == 300); // Apple's thumbnail, the middle
	CHECK(cards == 1);
	CHECK_STR(letters, "C");
	CHECK(thumb_gets == 1); // Apple's; Cherry's box is loading and Banana's is there
	CHECK(pills == 1);
	CHECK_STR(named, "Apple");
	CHECK(blits == 2 && blits_of_image == 0 && blits_of_box == 1);

	// the next frame draws the copies without asking for them again
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(scales == 0 && blits == 2);
	CHECK(box_gets == 1 && thumb_gets == 0); // only Cherry's card asks each frame

	// in the middle of the list: five covers
	slide_setting = 0;
	resetCounts();
	v = makeView(5);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 5 && decorated_outside == 0);
	CHECK(wasDecorated(3) && wasDecorated(7) && !wasDecorated(2) && !wasDecorated(8));
	CHECK_STR(named, "Fig");
	CHECK_STR(letters, "HEGF"); // outside in; Date has a box

	// names off: no name or pill; art off: cards only
	resetCounts();
	v = makeView(0);
	v.show_names = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 3 && decorated_named == 0 && pills == 0 && named[0] == '\0');
	resetCounts();
	v = makeView(0);
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(cards == 3 && thumb_gets == 0 && box_gets == 0 && blits == 0);
	CHECK_STR(named, "Apple");

	// a hidden name: Apple's art stands in for it, Cherry, with none, keeps it
	static const unsigned char hidden[10] = { 1, 0, 1 };
	resetCounts();
	v = makeView(0);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 0 && pills == 0 && named[0] == '\0');
	resetCounts();
	v = makeView(2);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 1);
	CHECK_STR(named, "Cherry");
	// art off: every name shows
	resetCounts();
	v = makeView(0);
	v.names_hidden = hidden;
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(named, "Apple");
	// an older launcher's view, without names_hidden, isn't read past its end
	resetCounts();
	v = makeView(0);
	v.size = offsetof(MosaicListView, names_hidden);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(named, "Apple");

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
	slide_setting = 1;
}

static void testReplacement(void) {
	setUp();
	fake_host.art_replacement = fakeArtReplacement;
	fake_host.decorate_art = fakeDecorateArt;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	slide_setting = 0;
	copy_count = 0;

	// none showing: the covers as before, and art decorators under Apple's name
	resetCounts();
	MosaicListView v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 0 && replacement_asks > 0);
	CHECK(art_decorated == 1 && art_decorated_index == 0 && rectIs(art_decorated_rect, 362, 205, 300, 372));

	// Apple's video in the middle cover, fitted inside it, straight from the
	// plugin's surface, and never copied
	replaced_index = 0;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 1 && rectIs(last_blit, 362, 255, 300, 200));
	CHECK(scales == 0);
	CHECK(art_decorated == 1 && art_decorated_index == 0);
	CHECK_STR(named, "Apple");
	// Cherry has none of its own, but the video stands in for its card
	replaced_index = 2;
	resetCounts();
	v = makeView(2);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 1 && art_decorated == 1 && art_decorated_index == 2);
	CHECK(strchr(letters, 'C') == NULL);
	// a card with no art and no video has no art decorators
	replaced_index = -1;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 0 && art_decorated == 0);
	// art off: no video either
	replaced_index = 0;
	resetCounts();
	v = makeView(0);
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 0 && art_decorated == 0);
	replaced_index = -1;
	p->quit();
	slide_setting = 1;
}

static void testSlidePlugin(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	slide_cfg = "on";
	CHECK(p->init(&fake_host) == 0);
	CHECK(slide_setting == 1);
	copy_count = 0;
	frees = 0;

	p->tick(1000);
	MosaicListView v = makeView(3);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(slide_from == 0);
	p->tick(1016);
	CHECK(redraws == 0); // at rest, no redraws

	// right by one: Date slides from the middle towards the left
	v = makeView(4);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(slide_from == LC_UNIT);
	int made = copy_count;
	p->tick(1016 + SLIDE_MS / 2);
	CHECK(redraws == 1);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(scales == 0 && copy_count == made); // no copies partway
	// Date's box, three quarters of the way from the middle to the left
	CHECK(slideShift() == 250);
	// at the end it comes to rest and stops redrawing
	p->tick(1016 + SLIDE_MS);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(slide_from == 0);
	p->tick(1100 + SLIDE_MS);
	CHECK(redraws == 0);

	// a slide the list isn't drawn for stops asking for redraws
	v = makeView(5);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(slide_from == LC_UNIT);
	redraws = 0;
	p->tick(1600);
	CHECK(redraws == 1 && slide_from == 0);
	p->tick(1700);
	CHECK(redraws == 1);

	// a page doesn't slide, and a new folder starts at rest
	v = makeView(9);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(slide_from == 0);
	v = makeView(8);
	v.path = "/Other";
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(slide_from == 0);
	p->quit();

	// slide off
	slide_cfg = "off";
	CHECK(p->init(&fake_host) == 0);
	CHECK(slide_setting == 0);
	v = makeView(3);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	v = makeView(4);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(slide_from == 0);
	p->quit();
	slide_cfg = "on";
}

static void testCache(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	cacheClear();
	// a limit of 2: the least recently drawn copy goes for a third
	MosaicRect a = { 0, 0, 10, 10 };
	MosaicRect b = { 0, 0, 20, 20 };
	MosaicRect c = { 0, 0, 30, 30 };
	copy_count = 0;
	frees = 0;
	frame = 1;
	CHECK(drawArt(&screen_surface, &fake_list.items[0], &a, 1, 2) == 1);
	frame = 2;
	CHECK(drawArt(&screen_surface, &fake_list.items[0], &b, 1, 2) == 1);
	frame = 3;
	CHECK(drawArt(&screen_surface, &fake_list.items[0], &a, 1, 2) == 1); // a hit, now the newer
	CHECK(drawArt(&screen_surface, &fake_list.items[0], &c, 1, 2) == 1);
	CHECK(frees == 1 && copy_count == 3);
	CHECK(cacheFind("/Roms/Apple.gb", 10, 10, 1) != NULL);
	CHECK(cacheFind("/Roms/Apple.gb", 20, 20, 1) == NULL);
	CHECK(cacheFind("/Roms/Apple.gb", 30, 30, 1) != NULL);
	// partway through a slide: the closest copy, a bigger one first, stretched
	resetCounts();
	MosaicRect d = { 5, 5, 25, 25 };
	CHECK(drawArt(&screen_surface, &fake_list.items[0], &d, 0, 2) == 1);
	CHECK(scales == 0 && blits == 1 && blits_of_image == 0 && rectIs(last_blit, 5, 5, 25, 25));
	CHECK(cacheFind("/Roms/Apple.gb", 25, 25, 0)->w == 30);
	CHECK(cacheFind("/Roms/Apple.gb", 40, 40, 0)->w == 30);
	CHECK(cacheFind("/Roms/Apple.gb", 5, 5, 0)->w == 10);
	// with no copy, the cache's image, fitted
	resetCounts();
	CHECK(drawArt(&screen_surface, &fake_list.items[3], &d, 0, 2) == 1);
	CHECK(scales == 0 && blits_of_image == 1);
	// no art, no copy
	CHECK(drawArt(&screen_surface, &fake_list.items[4], &a, 1, 2) == 0);
	cacheClear();
}

int main(void) {
	testStrip();
	testMove();
	testSlide();
	testArtAbove();
	testLetter();
	testInput();
	testRender();
	testReplacement();
	testSlidePlugin();
	testCache();
	CHECK_DONE();
}
