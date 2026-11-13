// The hero and card rail layout plugin: its model on its own, then the
// plugin against a fake host, for the d-pad, the hero, logo and cards it
// draws, the logo's fade, hidden names, the art it asks for ahead, and the
// copies it keeps.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/layout-hero-rail/layout_hero_rail.c"

static void testSettings(void) {
	CHECK(HR_cardPercent("small") == 22);
	CHECK(HR_cardPercent("medium") == 28);
	CHECK(HR_cardPercent("large") == 34);
	CHECK(HR_cardPercent("huge") == 28);
	CHECK(HR_cardPercent(NULL) == 28);
	// theme, the default, takes the theme's layout-hero-rail.cards; the user's own wins
	CHECK_STR(HR_themed("theme", "large"), "large");
	CHECK_STR(HR_themed(NULL, "large"), "large");
	CHECK(HR_themed("theme", NULL) == NULL);
	CHECK_STR(HR_themed("small", "large"), "small");
	CHECK(HR_cardPercent(HR_themed("theme", NULL)) == 28);
	CHECK(HR_cardPercent("theme") == 28);

	CHECK(HR_tint("0") == 0);
	CHECK(HR_tint("20") == 51);
	CHECK(HR_tint("40") == 102);
	CHECK(HR_tint("100") == 255);
	CHECK(HR_tint("101") == 51);
	CHECK(HR_tint("x") == 51);
	CHECK(HR_tint(NULL) == 51);

	CHECK(HR_fadeMs("0") == 0);
	CHECK(HR_fadeMs("600") == 600);
	CHECK(HR_fadeMs("2001") == HR_FADE_DEFAULT);
	CHECK(HR_fadeMs("-1") == HR_FADE_DEFAULT);
	CHECK(HR_fadeMs("") == HR_FADE_DEFAULT);
}

static void testLayout(void) {
	// desktop: 1024x663 above the hints, the status pill 90 tall at 30
	MosaicRect area = { 0, 0, 1024, 663 };
	MosaicRect status = { 794, 30, 200, 90 };
	HRLayout l;
	CHECK(HR_layout(&area, &status, NULL, 3, 28, 30, &l) == 0);
	CHECK(l.gap == 12 && l.frame == 6 && l.name_h == 30);
	CHECK(l.card_w == 138 && l.card_h == 185 && l.raise == 23);
	CHECK(l.cards == 6 && l.card_x == 68 && l.card_y == 436);
	// the hero to the middle of the cards, fading from half a card above the raised one
	CHECK(l.hero.x == 0 && l.hero.y == 0 && l.hero.w == 1024 && l.hero.h == 528);
	CHECK(l.fade_y == 315);
	CHECK(l.below.y == 528 && l.below.h == 135 && l.below.w == 1024);
	CHECK(l.logo.x == 68 && l.logo.y == 184 && l.logo.w == 409 && l.logo.h == 211);
	CHECK(l.hole.x == 794 && l.hole.y == 30 && l.hole.w == 200 && l.hole.h == 90);

	// the cards end inside the area, the raised one above the rest
	MosaicRect last = HR_card(&l, 5, 0);
	CHECK(last.x == 68 + 5 * 150 && last.x + last.w <= 1024 - 68);
	MosaicRect strip = HR_nameStrip(&l, &last);
	CHECK(strip.y == 621 && strip.h == 30 && strip.y + strip.h == 663 - 12);
	MosaicRect raised = HR_card(&l, 0, 1);
	CHECK(raised.y == 413 && raised.h == 185);
	CHECK(HR_card(&l, 6, 0).w == 0 && HR_card(&l, -1, 0).w == 0);

	// header widgets join the pill in the hole
	MosaicRect header = { 600, 30, 194, 100 };
	CHECK(HR_layout(&area, &status, &header, 3, 28, 30, &l) == 0);
	CHECK(l.hole.x == 600 && l.hole.w == 394 && l.hole.h == 100);
	// a pill over the logo's left pushes the logo down
	MosaicRect left = { 10, 30, 400, 250 };
	CHECK(HR_layout(&area, &left, NULL, 3, 28, 30, &l) == 0);
	CHECK(l.logo.y == 292 && l.logo.h == 103);

	// without names the cards sit lower; large cards are fewer
	CHECK(HR_layout(&area, &status, NULL, 3, 28, 0, &l) == 0);
	CHECK(l.card_y == 466 && l.name_h == 0);
	CHECK(HR_layout(&area, &status, NULL, 3, 34, 30, &l) == 0);
	CHECK(l.card_h == 225 && l.card_w == 168 && l.cards == 5);
	// 640x480 at scale 2, 412 above the hints
	MosaicRect small = { 0, 0, 640, 412 };
	CHECK(HR_layout(&small, NULL, NULL, 2, 22, 20, &l) == 0);
	CHECK(l.card_h == 90 && l.card_w == 67 && l.cards == 8);
	// too small for a card
	MosaicRect tiny = { 0, 0, 100, 60 };
	CHECK(HR_layout(&tiny, NULL, NULL, 3, 28, 30, &l) == -1);
}

static void testWindow(void) {
	// one card either side of the selection stays in view
	CHECK(HR_window(0, 0, 20, 6) == 0);
	CHECK(HR_window(0, 4, 20, 6) == 0);
	CHECK(HR_window(0, 5, 20, 6) == 1);
	CHECK(HR_window(1, 2, 20, 6) == 1);
	CHECK(HR_window(1, 1, 20, 6) == 0);
	CHECK(HR_window(3, 19, 20, 6) == 14); // never past the last card
	CHECK(HR_window(14, 18, 20, 6) == 14);
	CHECK(HR_window(9, 0, 20, 6) == 0);
	CHECK(HR_window(5, 2, 3, 6) == 0); // fewer than fit
	// two in view: no margin
	CHECK(HR_window(0, 1, 5, 2) == 0);
	CHECK(HR_window(0, 2, 5, 2) == 1);
	CHECK(HR_window(0, 0, 0, 6) == 0);
	CHECK(HR_window(0, 99, 20, 6) == 14);
}

static void testMove(void) {
	int l = MOSAIC_BUTTON_LEFT, r = MOSAIC_BUTTON_RIGHT, u = MOSAIC_BUTTON_UP, d = MOSAIC_BUTTON_DOWN;
	CHECK(HR_move(3, 20, 6, l, 1) == 2);
	CHECK(HR_move(3, 20, 6, r, 0) == 4);
	// a press wraps, a repeat stays
	CHECK(HR_move(0, 20, 6, l, 1) == 19);
	CHECK(HR_move(0, 20, 6, l, 0) == 0);
	CHECK(HR_move(19, 20, 6, r, 1) == 0);
	CHECK(HR_move(19, 20, 6, r, 0) == 19);
	// a rail's worth, stopping at the ends, then wrapping from them
	CHECK(HR_move(10, 20, 6, u, 1) == 4);
	CHECK(HR_move(4, 20, 6, u, 0) == 0);
	CHECK(HR_move(0, 20, 6, u, 1) == 19);
	CHECK(HR_move(0, 20, 6, u, 0) == 0);
	CHECK(HR_move(10, 20, 6, d, 0) == 16);
	CHECK(HR_move(16, 20, 6, d, 0) == 19);
	CHECK(HR_move(19, 20, 6, d, 1) == 0);
	CHECK(HR_move(19, 20, 6, d, 0) == 19);
	// one entry, an unknown direction, a bad selection
	CHECK(HR_move(0, 1, 6, r, 1) == 0);
	CHECK(HR_move(3, 20, 6, MOSAIC_BUTTON_A, 1) == 3);
	CHECK(HR_move(-1, 20, 6, r, 1) == -1);
}

static void testFade(void) {
	CHECK(HR_fade(0, 300) == 0);
	CHECK(HR_fade(-5, 300) == 0);
	CHECK(HR_fade(150, 300) == 191); // easing out: three quarters at halfway
	CHECK(HR_fade(30, 300) == 48);
	CHECK(HR_fade(299, 300) == 255);
	CHECK(HR_fade(300, 300) == 255);
	CHECK(HR_fade(5000, 300) == 255);
	CHECK(HR_fade(0, 0) == 255);
	// it only rises
	int last = 0, rising = 1;
	for (long t = 0; t <= 600; t += 7) {
		int a = HR_fade(t, 600);
		if (a < last) rising = 0;
		last = a;
	}
	CHECK(rising && last == 255);
}

static void testAvoid(void) {
	MosaicRect r = { 0, 0, 1024, 528 };
	MosaicRect hole = { 794, 30, 200, 90 };
	MosaicRect out[4];
	CHECK(HR_avoid(&r, &hole, out) == 4);
	CHECK(out[0].x == 0 && out[0].y == 0 && out[0].w == 1024 && out[0].h == 30);
	CHECK(out[1].x == 0 && out[1].y == 30 && out[1].w == 794 && out[1].h == 90);
	CHECK(out[2].x == 994 && out[2].y == 30 && out[2].w == 30 && out[2].h == 90);
	CHECK(out[3].x == 0 && out[3].y == 120 && out[3].w == 1024 && out[3].h == 408);
	// the pieces cover all but the hole
	long area = 0;
	for (int i = 0; i < 4; i++) area += (long)out[i].w * out[i].h;
	CHECK(area == 1024L * 528 - 200L * 90);

	// in the corner, only two
	MosaicRect corner = { 900, 0, 200, 50 };
	CHECK(HR_avoid(&r, &corner, out) == 2);
	CHECK(out[0].w == 900 && out[0].h == 50 && out[1].y == 50 && out[1].h == 478);
	// apart, or no hole: all of it
	MosaicRect away = { 0, 600, 100, 10 };
	CHECK(HR_avoid(&r, &away, out) == 1 && out[0].h == 528);
	MosaicRect none = { 0, 0, 0, 0 };
	CHECK(HR_avoid(&r, &none, out) == 1);
	CHECK(HR_avoid(&r, NULL, out) == 1);
	// all of it covered
	MosaicRect all = { -10, -10, 2000, 2000 };
	CHECK(HR_avoid(&r, &all, out) == 0);
}

static void testFit(void) {
	MosaicRect crop;
	// a 16:9 backdrop over a 1024x528 hero loses a little off its top and bottom
	CHECK(HR_cover(1920, 1080, 1024, 528, &crop) == 0);
	CHECK(crop.x == 0 && crop.w == 1920 && crop.h == 990 && crop.y == 45);
	// a wider one off its sides
	CHECK(HR_cover(2400, 900, 1024, 528, &crop) == 0);
	CHECK(crop.y == 0 && crop.h == 900 && crop.w == 1745 && crop.x == 327);
	// a square thumbnail loses its top and bottom
	CHECK(HR_cover(400, 400, 1024, 528, &crop) == 0);
	CHECK(crop.x == 0 && crop.w == 400 && crop.h == 206 && crop.y == 97);
	CHECK(HR_cover(0, 10, 10, 10, &crop) == -1);

	// a wide logo across the box's bottom, a tall one at its left
	MosaicRect box = { 68, 184, 409, 211 };
	MosaicRect r = HR_fit(800, 200, &box);
	CHECK(r.x == 68 && r.w == 409 && r.h == 102 && r.y == 184 + 211 - 102);
	r = HR_fit(100, 200, &box);
	CHECK(r.x == 68 && r.y == 184 && r.h == 211 && r.w == 105);
	r = HR_fit(0, 0, &box);
	CHECK(r.w == 0);
}

static void testBands(void) {
	CHECK(HR_bands(315, 528) == 64);
	CHECK(HR_bands(500, 528) == 14);
	CHECK(HR_bands(528, 528) == 0);
	CHECK(HR_bands(527, 528) == 1);
	// the bands cover the fade, rising from nearly nothing to nearly full
	int y = 0, h = 0, a = 0, next = 315, last = -1, rising = 1;
	for (int i = 0; i < 64; i++) {
		CHECK(HR_band(315, 528, i, &y, &h, &a) == 0);
		if (y != next || h < 2 || a <= last) rising = 0;
		next = y + h;
		last = a;
	}
	CHECK(rising && next == 528);
	CHECK(HR_band(315, 528, 0, &y, &h, &a) == 0 && a == 2);
	CHECK(HR_band(315, 528, 63, &y, &h, &a) == 0 && a == 253);
	CHECK(HR_band(315, 528, 64, &y, &h, &a) == -1);
	CHECK(HR_band(315, 528, -1, &y, &h, &a) == -1);
}

static void testLetter(void) {
	char out[8];
	HR_letter("apple", out, sizeof(out));
	CHECK_STR(out, "A");
	HR_letter("  (2) Zelda", out, sizeof(out));
	CHECK_STR(out, "2");
	HR_letter("\xc3\xa9t\xc3\xa9", out, sizeof(out));
	CHECK_STR(out, "\xc3\xa9");
	HR_letter("!!", out, sizeof(out));
	CHECK_STR(out, "!");
	HR_letter("   ", out, sizeof(out));
	CHECK_STR(out, "");
	HR_letter(NULL, out, sizeof(out));
	CHECK_STR(out, "");
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicEntry {
	char path[64];
	char name[64];
};
struct MosaicList {
	MosaicEntry items[24];
	int count;
};
struct MosaicSurface {
	int w, h;
	int owned; // made by the plugin
};

static MosaicList fake_list;
static MosaicSurface screen_surface = { 1024, 768, 0 };
static MosaicSurface backdrop_image = { 1920, 1080, 0 }; // Apple's backdrop
static MosaicSurface thumb_image = { 400, 400, 0 };      // Apple's and Cherry's thumbnails
static MosaicSurface logo_image = { 800, 200, 0 };       // Apple's logo
static MosaicSurface copies[64];
static int copy_count = 0, frees = 0, scales = 0, news = 0, make_fail = 0;
static int selects = 0, selected_to = -1;
static int finds = 0, redraws = 0;
static int decorated = 0, decorated_outside = 0, decorated_named = 0;
static MosaicRect view_area = { 0, 0, 1024, 663 };
static MosaicRect view_status = { 794, 30, 200, 90 };
static int tiles = 0; // no-art tiles: rounded fills in the accent colour
static char large_text[64], small_text[256];
static int large_alpha = -1;
static int blits = 0, blits_onto_screen_of_cache = 0, blits_in_hole = 0, hero_blits = 0;
static int faded = -1; // the last blit_alpha's alpha
static int tinted = 0, banded = 0, belows = 0;
static char asked[512]; // "<name>:<kind> " for each art_get
static const char* cards_cfg = "medium";
static const char* theme_cards = NULL; // the theme's layout-hero-rail.cards
static void (*onThemeChanged)(const char*, const char*, void*);
static const char* fakeThemeValue(const char* key, const char* fallback) {
	return !strcmp(key, "layout-hero-rail.cards") && theme_cards ? theme_cards : fallback;
}
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	CHECK(!strcmp(name, "launcher.theme_changed"));
	onThemeChanged = on_event;
	return 0;
}
static const char* fade_cfg = "300";
static MosaicSurface video = { 720, 480, 0 }; // another plugin's art_replace surface
static int replaced_index = -1; // the entry it's for, or -1 for none
static int blits_of_video = 0, screen_tinted = 0, screen_banded = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "cards")) return cards_cfg;
	if (!strcmp(key, "fade")) return fade_cfg;
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
static void fakeRedraw(void) { redraws++; }
static int fakeScale(void) { return 3; }
static int fakeTheme(int index) {
	switch (index) {
	case MOSAIC_COLOR_MAIN: return 0xFFFFFF;
	case MOSAIC_COLOR_ACCENT: return 0x9B2257;
	case MOSAIC_COLOR_LIST_TEXT: return 0xFFFFFF;
	case MOSAIC_COLOR_BACKGROUND: return 0x101010;
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
	// the shadow is black; only the text itself counts
	if (c.r == 0 && c.g == 0 && c.b == 0) return 0;
	char* into = font == MOSAIC_FONT_LARGE ? large_text : small_text;
	size_t size = font == MOSAIC_FONT_LARGE ? sizeof(large_text) : sizeof(small_text);
	if (strlen(into) + strlen(text) + 2 < size) {
		strcat(into, text);
		strcat(into, " ");
	}
	if (font == MOSAIC_FONT_LARGE) large_alpha = c.a;
	return 0;
}
static int inHole(const MosaicRect* r) {
	return r->x < view_status.x + view_status.w && view_status.x < r->x + r->w && r->y < view_status.y + view_status.h &&
		view_status.y < r->y + r->h;
}
static int fakeFillRect(MosaicSurface* t, const MosaicRect* r, MosaicColor c) {
	if (t == &screen_surface) {
		if (r && inHole(r)) blits_in_hole++;
		if (c.r == 0x10 && c.a == 255) belows++;
		if (c.r == 0x9B && c.a == 51) screen_tinted++;
		if (c.r == 0x10 && c.a < 255) screen_banded++;
		return 0;
	}
	if (c.r == 0x9B && c.a == 51 && !r) tinted++;
	if (c.r == 0x10 && c.a < 255 && r) banded++;
	return 0;
}
static int fakeFillRoundRect(MosaicSurface* t, const MosaicRect* r, int radius, MosaicColor c) {
	(void)t; (void)r; (void)radius;
	if (c.r == 0x9B && c.g == 0x22 && c.b == 0x57) tiles++;
	return 0;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr) {
	(void)sr;
	if (t != &screen_surface) return 0;
	blits++;
	if (!s->owned) blits_onto_screen_of_cache++;
	if (dr && inHole(dr)) blits_in_hole++;
	if (s->w == 1024 && s->h == 528) hero_blits++;
	if (s == &video) blits_of_video++;
	return 0;
}
static int fakeBlitAlpha(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr, int a) {
	(void)t; (void)s; (void)sr; (void)dr;
	faded = a;
	return 0;
}
static int fakeSurfaceSize(const MosaicSurface* s, int* w, int* h) {
	*w = s->w;
	*h = s->h;
	return 0;
}
static MosaicSurface* fakeMake(int w, int h) {
	if (make_fail || copy_count >= 64) return NULL;
	MosaicSurface* s = &copies[copy_count++];
	*s = (MosaicSurface){ w, h, 1 };
	return s;
}
static MosaicSurface* fakeSurfaceNew(const char* id, int w, int h) {
	news++;
	return fakeMake(w, h);
}
static MosaicSurface* fakeSurfaceScale(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)id; (void)src; (void)fit;
	scales++;
	return fakeMake(w, h);
}
static int fakeSurfaceFree(const char* id, MosaicSurface* s) {
	if (!s->owned) return -1;
	s->owned = 0;
	frees++;
	return 0;
}
// Apple has a backdrop, a thumbnail and a logo; Banana nothing; Cherry a
// thumbnail, still loading; Date a box only; the rest nothing
static int fakeArtFind(const MosaicEntry* e, int kind, char* out, size_t size) {
	finds++;
	snprintf(out, size, "%s", "");
	if (!strcmp(e->name, "Apple") && (kind == MOSAIC_ART_BACKDROP || kind == MOSAIC_ART_THUMBNAIL)) return 1;
	if (!strcmp(e->name, "Cherry") && kind == MOSAIC_ART_THUMBNAIL) return 1;
	if (!strcmp(e->name, "Date") && kind == MOSAIC_ART_BOX) return 1;
	return 0;
}
static int fakeArtGet(const MosaicEntry* e, int kind, const MosaicSurface** out) {
	*out = NULL;
	char ask[96];
	snprintf(ask, sizeof(ask), "%s:%i ", e->name, kind);
	if (strlen(asked) + strlen(ask) < sizeof(asked)) strcat(asked, ask);
	if (!strcmp(e->name, "Apple")) {
		if (kind == MOSAIC_ART_BACKDROP) *out = &backdrop_image;
		if (kind == MOSAIC_ART_THUMBNAIL) *out = &thumb_image;
		if (kind == MOSAIC_ART_LOGO) *out = &logo_image;
		return *out ? MOSAIC_ART_READY : MOSAIC_ART_MISSING;
	}
	if (!strcmp(e->name, "Cherry") && kind == MOSAIC_ART_THUMBNAIL) return MOSAIC_ART_LOADING;
	if (!strcmp(e->name, "Date") && kind == MOSAIC_ART_BOX) {
		*out = &thumb_image;
		return MOSAIC_ART_READY;
	}
	return MOSAIC_ART_MISSING;
}
static int fakeDecorateCell(MosaicSurface* s, int index, const MosaicRect* r, const MosaicRect* name) {
	(void)s; (void)index;
	decorated++;
	if (r->x < view_area.x || r->y < view_area.y || r->x + r->w > view_area.w || r->y + r->h > view_area.h) {
		decorated_outside++;
	}
	if (name && name->w > 0) decorated_named++;
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
	fake_host.request_redraw = fakeRedraw;
	fake_host.select_index = fakeSelectIndex;
	fake_host.screen_scale = fakeScale;
	fake_host.theme_color = fakeTheme;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTextTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.fill_rect = fakeFillRect;
	fake_host.fill_round_rect = fakeFillRoundRect;
	fake_host.blit = fakeBlit;
	fake_host.blit_alpha = fakeBlitAlpha;
	fake_host.surface_size = fakeSurfaceSize;
	fake_host.surface_new = fakeSurfaceNew;
	fake_host.surface_scale = fakeSurfaceScale;
	fake_host.surface_free = fakeSurfaceFree;
	fake_host.art_find = fakeArtFind;
	fake_host.art_get = fakeArtGet;
	fake_host.decorate_cell = fakeDecorateCell;
	fake_host.selection_settle_ms = 250;

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
	finds = redraws = 0;
	decorated = decorated_outside = decorated_named = 0;
	tiles = 0;
	large_text[0] = small_text[0] = '\0';
	large_alpha = -1;
	blits = blits_onto_screen_of_cache = blits_in_hole = hero_blits = 0;
	faded = -1;
	tinted = banded = belows = 0;
	asked[0] = '\0';
	scales = news = 0;
	blits_of_video = screen_tinted = screen_banded = 0;
}

static int fakeArtReplacement(int index, const MosaicSurface** out) {
	*out = NULL;
	if (index != replaced_index) return 0;
	*out = &video;
	return 1;
}

static int askedFor(const char* name, int kind) {
	char ask[96];
	snprintf(ask, sizeof(ask), "%s:%i ", name, kind);
	return strstr(asked, ask) != NULL;
}

static void testPlugin(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->size == sizeof(MosaicPlugin) && !strcmp(p->id, "layout-hero-rail"));
	CHECK(p->init(&fake_host) == 0);
	CHECK(percent == 28 && tint == 51 && fade_ms == 300 && settles == 1);
	MosaicListView v = makeView(0);
	CHECK(p->layout_init(&v) == 0);

	// the d-pad, through select_index, taking all four and nothing else
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
	// down a rail's worth, 6 cards
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selected_to == 7);
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

	// a frame on Apple before the selection settles: its backdrop as the
	// hero, copied once with the tint and fade baked in and drawn around
	// the pill, the rest of the area filled, and six cards, all decorated
	// inside the area; Apple's and Date's art copied, letter tiles for
	// the rest; no logo yet
	resetCounts();
	tick(1000);
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(news == 1 && copies[0].w == 1024 && copies[0].h == 528);
	CHECK(tinted == 1 && banded == 64);
	CHECK(hero_blits == 4 && blits_in_hole == 0);
	CHECK(belows >= 1);
	CHECK(decorated == 6 && decorated_outside == 0 && decorated_named == 6);
	CHECK(tiles == 4);
	CHECK_STR(large_text, "B C E F ");
	CHECK_STR(small_text, "Apple Banana Cherry Date elder Fig ");
	CHECK(faded == -1); // the logo at alpha 0 isn't drawn
	CHECK(blits_onto_screen_of_cache == 0);
	// the logo's asked for, and so are the neighbour's hero (Banana has
	// none) and the card just past the rail
	CHECK(askedFor("Apple", MOSAIC_ART_LOGO));
	CHECK(askedFor("Banana", MOSAIC_ART_LOGO));
	CHECK(askedFor("Grape", MOSAIC_ART_THUMBNAIL) && askedFor("Grape", MOSAIC_ART_BOX));
	CHECK(!askedFor("Apple", MOSAIC_ART_SCREENSHOT)); // the backdrop came first

	// it settles: the logo fades in, redrawing from tick until it's done
	resetCounts();
	CHECK(p->selection_changed(&fake_list.items[0]) == 0);
	CHECK(redraws == 1 && fading);
	tick(1150);
	CHECK(redraws == 2);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(faded == 191);
	CHECK(news == 0); // the hero's copy is kept
	tick(1300);
	CHECK(redraws == 3 && !fading);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(faded == -1 && blits == 4 + 2 + 1); // the hero's four parts, two cards and the logo at full strength
	CHECK(finds == 0); // the hero's kinds are known
	tick(1400);
	CHECK(redraws == 0);

	// moving on: Banana has no hero, so stock's background shows, and its
	// name stands in for a logo once it settles
	resetCounts();
	v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(hero_blits == 0 && belows == 0);
	CHECK_STR(large_text, "B C E F "); // the cards' letters only
	// Apple's hero and logo copies are kept, so its art isn't asked for again
	CHECK(!askedFor("Apple", MOSAIC_ART_BACKDROP) && !askedFor("Apple", MOSAIC_ART_LOGO));
	CHECK(askedFor("Cherry", MOSAIC_ART_THUMBNAIL));
	CHECK(p->selection_changed(&fake_list.items[1]) == 0);
	tick(2000 + 300);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(large_text, "Banana B C E F ");
	CHECK(large_alpha == 255);

	// with their names hidden, Apple's card, which has art, has no name
	// strip, and Banana, which has none, still shows its name on its card
	// and over the hero's place
	unsigned char hidden[10] = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
	resetCounts();
	v = makeView(1);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(strstr(small_text, "Apple") == NULL && strstr(small_text, "Banana") != NULL);
	CHECK(strstr(large_text, "Banana") != NULL);
	CHECK(decorated == 6 && decorated_named == 5);

	// rail window: Hops selected shows Cherry to Hops, one to its right
	resetCounts();
	v = makeView(7);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(first == 3);
	CHECK(decorated == 6);
	CHECK(strstr(small_text, "Date") == small_text);

	// names off: no strips, the names on the no-art tiles instead of letters
	resetCounts();
	v = makeView(0);
	v.show_names = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 6 && decorated_named == 0);
	CHECK_STR(small_text, "Banana Cherry elder Fig ");
	// the cards keep their size and copies; the hero and logo boxes are taller
	CHECK(news == 1 && scales == 1);
	// art off: tiles only, no hero, no logo, Apple's name over the hero's place
	resetCounts();
	v = makeView(0);
	v.show_art = 0;
	tick(2400);
	p->selection_changed(&fake_list.items[0]);
	tick(5000);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(tiles == 6 && hero_blits == 0 && blits == 0 && asked[0] == '\0');
	CHECK(strstr(large_text, "Apple") == large_text);

	// with no room for copies, the cache's images are drawn as they are
	resetCounts();
	quit();
	make_fail = 1;
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_onto_screen_of_cache == 4 + 2 + 1);
	CHECK(blits_in_hole == 0);
	make_fail = 0;

	// quit frees the copies left
	copy_count = 0;
	frees = 0;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(copy_count == 4); // the hero, the logo and two cards
	p->quit();
	CHECK(frees == 4);

	// settings; the fade off shows the logo once it settles
	cards_cfg = "large";
	fade_cfg = "0";
	CHECK(p->init(&fake_host) == 0);
	CHECK(percent == 34 && fade_ms == 0);
	resetCounts();
	consumed = 0;
	v = makeView(0);
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(selected_to == 5);
	CHECK(percent == 34);
	p->selection_changed(&fake_list.items[0]);
	tick(6000);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(faded == -1 && blits >= 1);
	p->quit();
	cards_cfg = "medium";
	fade_cfg = "300";

	// a launcher without selection events shows the logo at once
	fake_host.selection_settle_ms = 0;
	CHECK(p->init(&fake_host) == 0);
	CHECK(settles == 0);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(logoAlpha(&fake_list.items[0]) == 255);
	p->quit();

	// a launcher without art_get is declined
	fake_host.art_get = NULL;
	CHECK(p->init(&fake_host) == 1);
}

static void testPool(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	quit();
	copy_count = 0;
	frees = 0;
	// a limit of 2: the least recently drawn goes for a third
	frame = 1;
	CHECK(poolPut(cards, CARD_COPIES, 2, "/a", 1, 1, fakeMake(1, 1)) != NULL);
	frame = 2;
	CHECK(poolPut(cards, CARD_COPIES, 2, "/b", 1, 1, fakeMake(1, 1)) != NULL);
	frame = 3;
	poolFind(cards, CARD_COPIES, "/a", 1, 1)->used = frame;
	CHECK(poolPut(cards, CARD_COPIES, 2, "/c", 1, 1, fakeMake(1, 1)) != NULL);
	CHECK(frees == 1);
	CHECK(poolFind(cards, CARD_COPIES, "/a", 1, 1) != NULL);
	CHECK(poolFind(cards, CARD_COPIES, "/b", 1, 1) == NULL);
	CHECK(poolFind(cards, CARD_COPIES, "/c", 1, 1) != NULL);
	CHECK(poolFind(cards, CARD_COPIES, "/c", 2, 1) == NULL);
	// a full pool of three heroes swaps out the oldest
	for (int i = 0; i < 4; i++) {
		char path[8];
		snprintf(path, sizeof(path), "/h%i", i);
		frame = 10 + i;
		CHECK(poolPut(heroes, HERO_COPIES, HERO_COPIES, path, 5, 5, fakeMake(5, 5)) != NULL);
	}
	CHECK(poolFind(heroes, HERO_COPIES, "/h0", 5, 5) == NULL && poolFind(heroes, HERO_COPIES, "/h3", 5, 5) != NULL);
	quit();
	CHECK(frees == 7); // one each for /b and /h0, and the five left
}

static void testReplacement(void) {
	setUp();
	fake_host.art_replacement = fakeArtReplacement;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	copy_count = 0;

	// Banana has no hero of its own: its video is the hero, cover-fitted
	// around the pill, with the tint and fade drawn over it on the screen
	// rather than baked into a copy
	replaced_index = 1;
	resetCounts();
	MosaicListView v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 4 && blits_in_hole == 0 && hero_blits == 0);
	CHECK(news == 0 && scales <= 2);
	CHECK(screen_tinted == 4 && screen_banded >= 64);
	CHECK(belows >= 2); // under the video and the rest of the area
	// Apple's video stands in for its backdrop, which isn't asked for
	replaced_index = 0;
	resetCounts();
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 4 && hero_blits == 0 && !askedFor("Apple", MOSAIC_ART_BACKDROP));
	// gone: Apple's own hero
	replaced_index = -1;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 0 && hero_blits == 4);
	// art off: none
	replaced_index = 0;
	resetCounts();
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_of_video == 0);
	replaced_index = -1;
	p->quit();
}

// the theme's layout-hero-rail.cards while the setting is at theme,
// followed on launcher.theme_changed, and the user's own over it
static void testThemeCards(void) {
	setUp();
	fake_host.theme_value = fakeThemeValue;
	fake_host.subscribe = fakeSubscribe;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	MosaicInput in;
	memset(&in, 0, sizeof(in));
	unsigned int consumed = 0;
	MosaicListView v = makeView(0);
	cards_cfg = "theme";
	theme_cards = "small";
	onThemeChanged = NULL;
	CHECK(p->init(&fake_host) == 0);
	CHECK(onThemeChanged != NULL);
	CHECK(percent == 28); // no theme yet in init
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(percent == 22);
	theme_cards = "large";
	onThemeChanged("launcher.theme_changed", NULL, NULL);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(percent == 34);
	theme_cards = NULL;
	onThemeChanged("launcher.theme_changed", NULL, NULL);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(percent == 28);
	theme_cards = "large";
	cards_cfg = "small";
	CHECK(p->init(&fake_host) == 0);
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(percent == 22);
	p->quit();
	theme_cards = NULL;
	cards_cfg = "medium";
}

int main(void) {
	testSettings();
	testLayout();
	testWindow();
	testMove();
	testFade();
	testAvoid();
	testFit();
	testBands();
	testLetter();
	testPlugin();
	testReplacement();
	testPool();
	testThemeCards();
	CHECK_DONE();
}
