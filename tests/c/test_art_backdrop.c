// Art backdrop: its model, then the plugin against a fake host that
// records what it draws.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/art-backdrop/art_backdrop.c"

static void testSettings(void) {
	CHECK(BD_style("gradient") == BD_STYLE_GRADIENT);
	CHECK(BD_style("full") == BD_STYLE_FULL);
	CHECK(BD_style("Full") == BD_STYLE_GRADIENT);
	CHECK(BD_style("") == BD_STYLE_GRADIENT);
	CHECK(BD_style(NULL) == BD_STYLE_GRADIENT);

	CHECK(BD_dim("0") == 0);
	CHECK(BD_dim("25") == 64);
	CHECK(BD_dim("50") == 128);
	CHECK(BD_dim("75") == 191);
	CHECK(BD_dim("100") == 255);
	// anything else is the default, 50%
	CHECK(BD_dim("101") == 128);
	CHECK(BD_dim("-1") == 128);
	CHECK(BD_dim("50%") == 128);
	CHECK(BD_dim("") == 128);
	CHECK(BD_dim(NULL) == 128);

	CHECK(BD_height("full") == 1);
	CHECK(BD_height("half") == 2);
	CHECK(BD_height("third") == 3);
	CHECK(BD_height("quarter") == 1);
	CHECK(BD_height(NULL) == 1);

	MosaicRect r = BD_area(1024, 768, 1);
	CHECK(r.x == 0 && r.y == 0 && r.w == 1024 && r.h == 768);
	r = BD_area(1024, 768, 2);
	CHECK(r.w == 1024 && r.h == 384);
	r = BD_area(1280, 720, 3);
	CHECK(r.w == 1280 && r.h == 240);
	r = BD_area(640, 480, 0);
	CHECK(r.h == 480);
}

static void testKinds(void) {
	int kinds[BD_MAX_KINDS];
	CHECK(BD_kinds(1, 1, 1, kinds) == 4);
	CHECK(kinds[0] == MOSAIC_ART_BACKDROP && kinds[1] == MOSAIC_ART_SCREENSHOT && kinds[2] == MOSAIC_ART_THUMBNAIL &&
		kinds[3] == MOSAIC_ART_BOX);
	CHECK(BD_kinds(0, 1, 0, kinds) == 2);
	CHECK(kinds[0] == MOSAIC_ART_BACKDROP && kinds[1] == MOSAIC_ART_THUMBNAIL);
	CHECK(BD_kinds(0, 0, 0, kinds) == 1);
	CHECK(kinds[0] == MOSAIC_ART_BACKDROP);
}

static void testCover(void) {
	MosaicRect c;
	// wider than the screen: the sides go
	CHECK(BD_cover(1920, 1080, 1024, 768, &c) == 0);
	CHECK(c.x == 240 && c.y == 0 && c.w == 1440 && c.h == 1080);
	// taller: the top and bottom
	CHECK(BD_cover(600, 1200, 1024, 768, &c) == 0);
	CHECK(c.x == 0 && c.y == 375 && c.w == 600 && c.h == 450);
	// the same shape: all of it
	CHECK(BD_cover(2048, 1536, 1024, 768, &c) == 0);
	CHECK(c.x == 0 && c.y == 0 && c.w == 2048 && c.h == 1536);
	// a third of the screen keeps the middle of the art
	CHECK(BD_cover(1024, 768, 1024, 256, &c) == 0);
	CHECK(c.x == 0 && c.y == 256 && c.w == 1024 && c.h == 256);
	// never empty
	CHECK(BD_cover(1, 1000, 1000, 1, &c) == 0);
	CHECK(c.w == 1 && c.h == 1);
	CHECK(BD_cover(0, 10, 10, 10, &c) == -1);
	CHECK(BD_cover(10, 10, 10, 0, &c) == -1);
}

static void testBands(void) {
	int y, h, alpha;
	// 768 tall: the top half at full alpha, then 64 bands of 6
	CHECK(BD_bands(768) == 65);
	CHECK(BD_band(768, 0, &y, &h, &alpha) == 0);
	CHECK(y == 0 && h == 384 && alpha == 255);
	CHECK(BD_band(768, 1, &y, &h, &alpha) == 0);
	CHECK(y == 384 && h == 6 && alpha == 253);
	CHECK(BD_band(768, 64, &y, &h, &alpha) == 0);
	CHECK(y == 762 && h == 6 && alpha == 2);
	CHECK(BD_band(768, 65, &y, &h, &alpha) == -1);
	CHECK(BD_band(768, -1, &y, &h, &alpha) == -1);
	// short areas get fewer bands, at least 2 tall
	CHECK(BD_bands(40) == 11);
	CHECK(BD_bands(3) == 2);
	CHECK(BD_bands(1) == 1);
	CHECK(BD_band(1, 0, &y, &h, &alpha) == 0);
	CHECK(y == 0 && h == 1 && alpha == 128);
	CHECK(BD_bands(0) == 0);
	CHECK(BD_bands(-5) == 0);

	// the bands tile the area, top to bottom, and only ever fade
	static const int heights[] = { 2, 7, 100, 240, 256, 384, 480, 720, 768, 1080 };
	for (size_t k = 0; k < sizeof(heights) / sizeof(heights[0]); k++) {
		int area = heights[k], next = 0, last = 256;
		for (int i = 0; i < BD_bands(area); i++) {
			CHECK(BD_band(area, i, &y, &h, &alpha) == 0);
			CHECK(y == next && h > 0);
			CHECK(alpha <= last && alpha > 0 && alpha <= 255);
			next = y + h;
			last = alpha;
		}
		CHECK(next == area);
	}
}

static void testSrcBand(void) {
	MosaicRect crop = { 240, 0, 1440, 1080 };
	MosaicRect s = BD_srcBand(&crop, 768, 0, 384);
	CHECK(s.x == 240 && s.y == 0 && s.w == 1440 && s.h == 540);
	s = BD_srcBand(&crop, 768, 384, 6);
	CHECK(s.y == 540 && s.h == 8);
	s = BD_srcBand(&crop, 768, 762, 6);
	CHECK(s.y + s.h == 1080);
	// a small image stretched over many rows still gives a row of it
	MosaicRect tiny = { 0, 0, 4, 2 };
	s = BD_srcBand(&tiny, 768, 400, 6);
	CHECK(s.y == 1 && s.h == 1);
	s = BD_srcBand(&tiny, 768, 762, 6);
	CHECK(s.y == 1 && s.h == 1);
	// a copy the area's size maps row for row
	MosaicRect same = { 0, 0, 1024, 768 };
	s = BD_srcBand(&same, 768, 390, 6);
	CHECK(s.y == 390 && s.h == 6);

	CHECK(BD_mulAlpha(128, 255) == 128);
	CHECK(BD_mulAlpha(128, 128) == 64);
	CHECK(BD_mulAlpha(255, 0) == 0);
	CHECK(BD_mulAlpha(255, 255) == 255);
}

///////////////////////////////////////
// the plugin, against a fake host

struct MosaicEntry {
	const char* path;
	int type;
	unsigned kinds; // bits of the kinds it has art files for
	int state;      // what art_get returns for them
};

struct MosaicSurface {
	int w, h;
	int mine; // the plugin made it
	int freed;
	int dimmed; // alpha of the last fill over all of it
};

static MosaicSurface pool[16];
static int pool_count = 0;
static MosaicSurface screen = { 1024, 768, 0, 0, 0 };
static MosaicSurface art = { 1920, 1080, 0, 0, 0 };
static int scales = 0, scale_fail = 0, frees = 0, finds = 0, art_gets = 0;
static int blits = 0, alpha_blits = 0, fills = 0;
static int last_get_kind = -1, first_alpha = -1, last_fill_alpha = -1, clear = 0;
static unsigned theme_fill = 0x1000000;
static MosaicRect first_blit, last_dst;
static const MosaicSurface* blit_src = NULL;
static const char* cfg[][2] = {
	{ "style", NULL }, { "dim", NULL }, { "height", NULL }, { "screenshot", NULL }, { "thumbnail", NULL }, { "box", NULL },
};

static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
		if (!strcmp(cfg[i][0], key)) return cfg[i][1] ? cfg[i][1] : fallback;
	}
	return fallback;
}
static void setCfg(const char* key, const char* value) {
	for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
		if (!strcmp(cfg[i][0], key)) cfg[i][1] = value;
	}
}
static void fakeLog(const char* id, const char* message) { (void)id; (void)message; }
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static int fakeTheme(int index) { return index == MOSAIC_COLOR_BACKGROUND ? 0x102030 : -1; }
static int fakeSize(const MosaicSurface* s, int* w, int* h) {
	*w = s->w;
	*h = s->h;
	return 0;
}
static int fakeReadPixel(const MosaicSurface* s, int x, int y, MosaicColor* out) {
	CHECK(s == &screen && x >= 0 && y >= 0 && x < s->w && y < s->h);
	*out = (MosaicColor){ 0, 0, 0, clear ? 0 : 255 };
	return 0;
}
static MosaicSurface* fakeScale(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)id;
	CHECK(src == &art && fit == MOSAIC_FIT_COVER);
	scales++;
	if (scale_fail || pool_count >= 16) return NULL;
	MosaicSurface* s = &pool[pool_count++];
	*s = (MosaicSurface){ w, h, 1, 0, -1 };
	return s;
}
static int fakeFree(const char* id, MosaicSurface* s) {
	(void)id;
	CHECK(s->mine && !s->freed);
	s->freed = 1;
	frees++;
	return 0;
}
static int fakeFillRect(MosaicSurface* s, const MosaicRect* rect, MosaicColor color) {
	if (s != &screen) {
		CHECK(s->mine && !s->freed && !rect);
		s->dimmed = color.a;
		return 0;
	}
	CHECK(rect && rect->x >= 0 && rect->y >= 0 && rect->x + rect->w <= s->w && rect->y + rect->h <= s->h);
	if (color.a == 255 && color.r == 0x10) {
		// the theme's colour under clear pixels, before any art
		CHECK(blits == 0 && alpha_blits == 0);
		theme_fill = (unsigned)color.r << 16 | (unsigned)color.g << 8 | color.b;
		return 0;
	}
	CHECK(color.r == 0 && color.g == 0 && color.b == 0);
	last_fill_alpha = color.a;
	fills++;
	return 0;
}
static void sawBlit(const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst) {
	CHECK(src && !src->freed && src_rect && dst);
	CHECK(src_rect->x >= 0 && src_rect->y >= 0 && src_rect->x + src_rect->w <= src->w &&
		src_rect->y + src_rect->h <= src->h && src_rect->h > 0);
	CHECK(dst->x == 0 && dst->w == screen.w && dst->y >= 0 && dst->y + dst->h <= screen.h);
	if (!blits && !alpha_blits) first_blit = *dst;
	blit_src = src;
	last_dst = *dst;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst) {
	CHECK(t == &screen);
	sawBlit(src, src_rect, dst);
	blits++;
	return 0;
}
static int fakeBlitAlpha(MosaicSurface* t, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst,
		int alpha) {
	CHECK(t == &screen && alpha > 0 && alpha < 255);
	sawBlit(src, src_rect, dst);
	if (!alpha_blits) first_alpha = alpha;
	alpha_blits++;
	return 0;
}
static int fakeArtFind(const MosaicEntry* e, int kind, char* out, size_t size) {
	finds++;
	int found = (e->kinds >> kind) & 1;
	snprintf(out, size, "%s", found ? "/r/GB/.media/x.png" : "");
	return found;
}
static int fakeArtGet(const MosaicEntry* e, int kind, const MosaicSurface** out) {
	art_gets++;
	last_get_kind = kind;
	*out = e->state == MOSAIC_ART_READY ? &art : NULL;
	return e->state;
}

static MosaicHost fakeHost(void) {
	MosaicHost h;
	memset(&h, 0, sizeof(h));
	h.size = sizeof(h);
	h.api_version = MOSAIC_API_VERSION;
	h.log = fakeLog;
	h.cfg_get = fakeCfgGet;
	h.entry_path = fakeEntryPath;
	h.entry_type = fakeEntryType;
	h.theme_color = fakeTheme;
	h.surface_size = fakeSize;
	h.read_pixel = fakeReadPixel;
	h.surface_scale = fakeScale;
	h.surface_free = fakeFree;
	h.fill_rect = fakeFillRect;
	h.blit = fakeBlit;
	h.blit_alpha = fakeBlitAlpha;
	h.art_find = fakeArtFind;
	h.art_get = fakeArtGet;
	h.plugin_art = 1;
	return h;
}

static void reset(void) {
	scales = frees = finds = art_gets = blits = alpha_blits = fills = 0;
	last_get_kind = first_alpha = last_fill_alpha = -1;
	theme_fill = 0x1000000;
	blit_src = NULL;
}

#define BIT(kind) (1u << (kind))

static void testPlugin(void) {
	MosaicHost h = fakeHost();
	const MosaicPlugin* p = mosaic_plugin_get(&h);
	CHECK(p->background_render == backgroundRender);
	CHECK(p->init(&h) == 0);
	MosaicEntry backdrop = { "/r/GB/Backdrop.gb", MOSAIC_ENTRY_ROM, BIT(MOSAIC_ART_BACKDROP) | BIT(MOSAIC_ART_BOX), MOSAIC_ART_READY };
	MosaicEntry shot = { "/r/GB/Shot.gb", MOSAIC_ENTRY_ROM, BIT(MOSAIC_ART_SCREENSHOT) | BIT(MOSAIC_ART_BOX), MOSAIC_ART_READY };
	MosaicEntry boxed = { "/r/GB/Boxed.gb", MOSAIC_ENTRY_ROM, BIT(MOSAIC_ART_BOX), MOSAIC_ART_READY };
	MosaicEntry bare = { "/r/GB/Bare.gb", MOSAIC_ENTRY_ROM, 0, MOSAIC_ART_MISSING };
	MosaicEntry slow = { "/r/GB/Slow.gb", MOSAIC_ENTRY_ROM, BIT(MOSAIC_ART_BACKDROP), MOSAIC_ART_LOADING };
	MosaicEntry folder = { "/r/GB/Hacks", MOSAIC_ENTRY_DIR, BIT(MOSAIC_ART_BOX), MOSAIC_ART_READY };
	MosaicEntry pak = { "/r/Tools/Clock.pak", MOSAIC_ENTRY_PAK, BIT(MOSAIC_ART_BOX), MOSAIC_ART_READY };

	// the defaults: a gradient over the whole screen, dimmed by half on the
	// copy, which is scaled once
	reset();
	CHECK(p->background_render(&screen, &backdrop) == 0);
	CHECK(last_get_kind == MOSAIC_ART_BACKDROP && art_gets == 1);
	CHECK(scales == 1 && pool[0].w == 1024 && pool[0].h == 768 && pool[0].dimmed == 128);
	CHECK(blits == 1 && alpha_blits == 64 && fills == 0);
	CHECK(blit_src == &pool[0]);
	CHECK(first_blit.y == 0 && first_blit.h == 384 && first_alpha == 253);
	CHECK(last_dst.y + last_dst.h == 768);
	CHECK(theme_fill == 0x1000000);
	reset();
	CHECK(p->background_render(&screen, &backdrop) == 0);
	CHECK(scales == 0 && art_gets == 0 && finds == 0);
	CHECK(blits == 1 && alpha_blits == 64);

	// over stock's clear background, the theme's colour goes under it first
	clear = 1;
	reset();
	CHECK(p->background_render(&screen, &backdrop) == 0);
	CHECK(theme_fill == 0x102030 && blits == 1);
	clear = 0;

	// the fallbacks, in order: the screenshot before the box
	reset();
	CHECK(p->background_render(&screen, &shot) == 0);
	CHECK(last_get_kind == MOSAIC_ART_SCREENSHOT && scales == 1);
	reset();
	CHECK(p->background_render(&screen, &boxed) == 0);
	CHECK(last_get_kind == MOSAIC_ART_BOX && scales == 1);

	// three copies at most: a fourth game frees the least recently drawn
	reset();
	CHECK(p->background_render(&screen, &backdrop) == 0);
	CHECK(scales == 0);
	MosaicEntry fourth = { "/r/GB/Fourth.gb", MOSAIC_ENTRY_ROM, BIT(MOSAIC_ART_THUMBNAIL), MOSAIC_ART_READY };
	CHECK(p->background_render(&screen, &fourth) == 0);
	CHECK(last_get_kind == MOSAIC_ART_THUMBNAIL && scales == 1 && frees == 1 && pool[1].freed);

	// stock's background, with nothing drawn: no art, a folder, a pak, an
	// empty folder; and no art is only looked for once in a row
	reset();
	CHECK(p->background_render(&screen, &bare) == 0);
	CHECK(finds == 4 && art_gets == 0);
	CHECK(p->background_render(&screen, &bare) == 0);
	CHECK(finds == 4);
	CHECK(p->background_render(&screen, &folder) == 0);
	CHECK(p->background_render(&screen, &pak) == 0);
	CHECK(p->background_render(&screen, NULL) == 0);
	CHECK(blits == 0 && alpha_blits == 0 && fills == 0 && scales == 0);

	// while it loads, stock's, and then the art
	reset();
	CHECK(p->background_render(&screen, &slow) == 0);
	CHECK(art_gets == 1 && scales == 0 && blits == 0);
	slow.state = MOSAIC_ART_READY;
	CHECK(p->background_render(&screen, &slow) == 0);
	CHECK(art_gets == 2 && scales == 1 && blits == 1);

	// without a copy, straight from the cache's image, cropped to cover and
	// dimmed band by band
	scale_fail = 1;
	MosaicEntry fifth = { "/r/GB/Fifth.gb", MOSAIC_ENTRY_ROM, BIT(MOSAIC_ART_BACKDROP), MOSAIC_ART_READY };
	reset();
	CHECK(p->background_render(&screen, &fifth) == 0);
	CHECK(scales == 1 && blit_src == &art && blits == 1 && alpha_blits == 64 && fills == 65);
	CHECK(last_fill_alpha == BD_mulAlpha(128, 2));
	scale_fail = 0;
	p->quit();

	// the fallbacks off: only the backdrop
	setCfg("screenshot", "off");
	setCfg("thumbnail", "off");
	setCfg("box", "off");
	CHECK(p->init(&h) == 0);
	reset();
	CHECK(p->background_render(&screen, &shot) == 0);
	CHECK(finds == 1 && art_gets == 0 && blits == 0);
	setCfg("screenshot", NULL);
	setCfg("thumbnail", NULL);
	setCfg("box", NULL);

	// full-bleed over a third, dimmed by three quarters: one blit
	setCfg("style", "full");
	setCfg("height", "third");
	setCfg("dim", "75");
	CHECK(p->init(&h) == 0);
	reset();
	int before = pool_count;
	CHECK(p->background_render(&screen, &backdrop) == 0);
	CHECK(scales == 1 && pool[before].w == 1024 && pool[before].h == 256 && pool[before].dimmed == 191);
	CHECK(blits == 1 && alpha_blits == 0 && fills == 0);
	CHECK(first_blit.y == 0 && first_blit.h == 256);
	// and from the cache's image, one fill for the dimming
	scale_fail = 1;
	reset();
	CHECK(p->background_render(&screen, &fifth) == 0);
	CHECK(blits == 1 && fills == 1 && last_fill_alpha == 191);
	scale_fail = 0;

	// undimmed: no fills at all
	setCfg("dim", "0");
	CHECK(p->init(&h) == 0);
	reset();
	MosaicEntry sixth = { "/r/GB/Sixth.gb", MOSAIC_ENTRY_ROM, BIT(MOSAIC_ART_BACKDROP), MOSAIC_ART_READY };
	before = pool_count;
	CHECK(p->background_render(&screen, &sixth) == 0);
	CHECK(pool[before].dimmed == -1 && fills == 0);
	setCfg("style", NULL);
	setCfg("height", NULL);
	setCfg("dim", NULL);

	// quit frees every copy
	p->quit();
	for (int i = 0; i < pool_count; i++) CHECK(pool[i].freed);

	// a launcher without the background slot declines it
	MosaicHost old = fakeHost();
	old.plugin_art = 0;
	CHECK(p->init(&old) == 1);
	old.size = offsetof(MosaicHost, plugin_art);
	old.plugin_art = 1;
	CHECK(p->init(&old) == 1);
	CHECK(p->init(&h) == 0);
}

int main(void) {
	testSettings();
	testKinds();
	testCover();
	testBands();
	testSrcBand();
	testPlugin();
	CHECK_DONE();
}
