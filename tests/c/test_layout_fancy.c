// The fancy layout plugin: its model on its own, then the plugin against a
// fake host and a fixture card, for the rows it draws and decorates, its
// names and pill, the panel's box and save state preview, and the art
// copies it keeps.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/layout-fancy/layout_fancy.c"

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define PS ROMS "/Sony PlayStation (PS)"
#define MINUI CARD "/.userdata/shared/.minui"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void testLayout(void) {
	// desktop: 1024x663 above the hints, the status pill 90 tall at 30
	MosaicRect area = { 0, 0, 1024, 663 };
	MosaicRect status = { 794, 30, 200, 90 };
	MosaicRect none = { 0, 0, 0, 0 };
	FLLayout l;
	CHECK(FL_layout(&area, &status, &none, 3, 1, &l) == 0);
	// stock's rows beside its thumbnail: 563 - 75 + 15 - 60 wide
	CHECK(l.row_x == 15 && l.row_y == 30 && l.row_h == 90 && l.row_w == 443 && l.name_pad == 36);
	// the pill is right of the rows, so the top row keeps its width
	CHECK(l.top_w == 443);
	// 460 wide, ending 45 short of the edge, from below the pill to 30 above the bottom
	CHECK(rectIs(l.panel, 519, 150, 460, 483));
	CHECK(FL_rows(&l, &area) == 7);
	CHECK(rectIs(FL_row(&l, 0), 15, 30, 443, 90));
	CHECK(rectIs(FL_row(&l, 2), 15, 210, 443, 90));
	MosaicRect row = FL_row(&l, 1);
	CHECK(rectIs(FL_name(&l, &row, 100), 15, 120, 172, 90));
	CHECK(rectIs(FL_name(&l, &row, 1000), 15, 120, 443, 90));

	// no panel: the rows take the width, and the top one stops short of the pill
	CHECK(FL_layout(&area, &status, &none, 3, 0, &l) == 0);
	CHECK(l.row_w == 949 && l.panel.w == 0);
	CHECK(l.top_w == 794 - 30 - 15);
	CHECK(FL_row(&l, 0).w == 749 && FL_row(&l, 1).w == 949);
	// header widgets left of the pill, too
	MosaicRect header = { 600, 30, 194, 90 };
	CHECK(FL_layout(&area, &status, &header, 3, 0, &l) == 0);
	CHECK(l.top_w == 600 - 30 - 15);
	// taller header widgets push the panel down
	MosaicRect tall = { 600, 30, 194, 150 };
	CHECK(FL_layout(&area, &status, &tall, 3, 1, &l) == 0);
	CHECK(l.panel.y == 210 && l.panel.h == 423);

	// 640x480 at scale 2: stock's rows end at 352 - 50 + 10 - 40
	MosaicRect small = { 0, 0, 640, 400 };
	CHECK(FL_layout(&small, &none, &none, 2, 1, &l) == 0);
	CHECK(l.row_w == 272 && rectIs(l.panel, 322, 20, 288, 360));
	// too small for a row
	MosaicRect tiny = { 0, 0, 100, 50 };
	CHECK(FL_layout(&tiny, &none, &none, 3, 1, &l) == -1);
}

static void testWindow(void) {
	CHECK(FL_window(0, 0, 10, 7) == 0);
	CHECK(FL_window(0, 6, 10, 7) == 0);
	CHECK(FL_window(0, 7, 10, 7) == 1);
	CHECK(FL_window(3, 9, 10, 7) == 3);
	CHECK(FL_window(5, 2, 10, 7) == 2);
	// never past the last row, and short lists start at the top
	CHECK(FL_window(8, 9, 10, 7) == 3);
	CHECK(FL_window(2, 1, 3, 7) == 0);
	CHECK(FL_window(0, 0, 0, 7) == 0);
}

static void testPanel(void) {
	CHECK(FL_panelArt(0, 1, 1, 1) == 0);
	CHECK(FL_panelArt(1, 1, 1, 1) == FL_SHOW_PREVIEW);
	CHECK(FL_panelArt(1, 0, 1, 1) == (FL_SHOW_BOX | FL_SHOW_PREVIEW));
	CHECK(FL_panelArt(1, 1, 1, 0) == FL_SHOW_BOX);
	CHECK(FL_panelArt(1, 0, 0, 1) == FL_SHOW_PREVIEW);
	CHECK(FL_panelArt(1, 1, 0, 0) == 0);

	MosaicRect panel = { 519, 150, 460, 483 }, box, shot;
	FL_panelParts(&panel, FL_SHOW_BOX | FL_SHOW_PREVIEW, 30, &box, &shot);
	CHECK(rectIs(box, 519, 150, 460, 226));
	CHECK(rectIs(shot, 519, 407, 460, 226));
	FL_panelParts(&panel, FL_SHOW_PREVIEW, 30, &box, &shot);
	CHECK(box.w == 0 && rectIs(shot, 519, 150, 460, 483));
	FL_panelParts(&panel, FL_SHOW_BOX, 30, &box, &shot);
	CHECK(rectIs(box, 519, 150, 460, 483) && shot.w == 0);
	FL_panelParts(&panel, 0, 30, &box, &shot);
	CHECK(box.w == 0 && shot.w == 0);

	// a 4:3 preview across the panel, a tall box down it
	CHECK(rectIs(FL_contain(&panel, 640, 480), 519, 219, 460, 345));
	CHECK(rectIs(FL_contain(&panel, 300, 600), 628, 150, 241, 483));
	CHECK(rectIs(FL_contain(&panel, 0, 0), 519, 150, 460, 483));

	// names as the built-in list shows them
	CHECK(FL_nameShown(0, 0, 1, 1, 0) == 0);
	CHECK(FL_nameShown(1, 0, 1, 0, 1) == 1);
	CHECK(FL_nameShown(1, 1, 0, 0, 1) == 1);
	CHECK(FL_nameShown(1, 1, 1, 0, 0) == 0);
	CHECK(FL_nameShown(1, 1, 1, 1, 1) == 0);
	CHECK(FL_nameShown(1, 1, 1, 1, 0) == 1);
}

// files the fake card has, for FL_resumeFile
static const char* present[8];
static int presentExists(const char* path, void* ctx) {
	(void)ctx;
	for (int i = 0; i < 8 && present[i]; i++) {
		if (!strcmp(present[i], path)) return 1;
	}
	return 0;
}

static void testStatePaths(void) {
	char out[1024], slot[1024], stem[1024];
	CHECK(FL_tagOf("/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, "GB");
	CHECK(FL_tagOf("/sd/Roms", "/sd/Roms/Pico (P8) (beta)/Celeste.p8", out, sizeof(out)) == 1);
	CHECK_STR(out, "beta");
	CHECK(FL_tagOf("/sd/Roms", "/sd/Roms/Arcade/Puzzle/Bust.zip", out, sizeof(out)) == 1);
	CHECK_STR(out, "Arcade");
	CHECK(FL_tagOf("/sd/Roms", "/sd/Roms/Arcade", out, sizeof(out)) == 0);
	CHECK(FL_tagOf("/sd/Roms", "/sd/Peeker/Apple.gb", out, sizeof(out)) == 0);
	CHECK(FL_tagOf("/sd/Roms", "/sd/RomsX/GB/Tetris.gb", out, sizeof(out)) == 0);

	memset(present, 0, sizeof(present));
	// a plain game is its own file
	CHECK(FL_resumeFile("/sd/Roms/GB/Tetris.gb", 0, presentExists, NULL, out, sizeof(out)) == 1);
	CHECK_STR(out, "/sd/Roms/GB/Tetris.gb");
	// a disc goes by its game's .m3u
	present[0] = "/sd/Roms/PS (PS)/FF7/FF7.m3u";
	CHECK(FL_resumeFile("/sd/Roms/PS (PS)/FF7/FF7 (Disc 2).cue", 0, presentExists, NULL, out, sizeof(out)) == 1);
	CHECK_STR(out, "/sd/Roms/PS (PS)/FF7/FF7.m3u");
	CHECK(FL_resumeFile("/sd/Roms/PS (PS)/FF7/FF7.m3u", 0, presentExists, NULL, out, sizeof(out)) == 1);
	CHECK_STR(out, "/sd/Roms/PS (PS)/FF7/FF7.m3u");
	// a folder by its .cue, and that by the folder's .m3u
	CHECK(FL_resumeFile("/sd/Roms/PS (PS)/FF7", 1, presentExists, NULL, out, sizeof(out)) == 1);
	CHECK_STR(out, "/sd/Roms/PS (PS)/FF7/FF7.m3u");
	present[0] = "/sd/Roms/PS (PS)/Crash/Crash.cue";
	CHECK(FL_resumeFile("/sd/Roms/PS (PS)/Crash", 1, presentExists, NULL, out, sizeof(out)) == 1);
	CHECK_STR(out, "/sd/Roms/PS (PS)/Crash/Crash.cue");
	// a folder with neither has nothing to resume
	CHECK(FL_resumeFile("/sd/Roms/PS (PS)", 1, presentExists, NULL, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(FL_resumeFile("", 0, presentExists, NULL, out, sizeof(out)) == 0);

	CHECK(FL_slotPaths("/sd", "/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris.gb", slot, stem, sizeof(slot)) == 1);
	CHECK_STR(slot, "/sd/.userdata/shared/.minui/GB/Tetris.gb.txt");
	CHECK_STR(stem, "/sd/.userdata/shared/.minui/GB/Tetris.gb");
	CHECK(FL_slotPaths("/sd", "/sd/Roms", "/sd/Tools/Clock.pak", slot, stem, sizeof(slot)) == 0);
	CHECK_STR(slot, "");
	CHECK(FL_slotPaths("/sd", "/sd/Roms", "/sd/Roms/GB/Tetris.gb", slot, stem, 20) == 0);

	CHECK(FL_slot("1") == 1);
	CHECK(FL_slot(" 4\n") == 4);
	CHECK(FL_slot("+9") == 9);
	CHECK(FL_slot("12abc") == 12);
	CHECK(FL_slot("") == 0);
	CHECK(FL_slot("x") == 0);
	CHECK(FL_slot("-0") == 0);
	CHECK(FL_slot("-1") == -1);
	CHECK(FL_slot("1000") == -1);
	CHECK(FL_previewPath("/m/GB/Tetris.gb", 3, out, sizeof(out)) == 1);
	CHECK_STR(out, "/m/GB/Tetris.gb.3.bmp");
	CHECK(FL_previewPath("/m/GB/Tetris.gb", -1, out, sizeof(out)) == 0);
	CHECK(FL_previewPath("/m/GB/Tetris.gb", 3, out, 10) == 0);
	CHECK_STR(out, "");
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicEntry {
	char path[512];
	char name[64];
	int type;
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
static MosaicSurface box_image = { 300, 400, 0, NULL };
static MosaicSurface thumb_image = { 400, 300, 0, NULL };
static MosaicSurface shot_image = { 640, 480, 0, NULL };
static MosaicSurface copies[64];
static int copy_count = 0, frees = 0, scales = 0;
static int decorated = 0, decorated_named = 0;
static int decorated_index[16];
static MosaicRect decorated_rect[16];
static MosaicRect decorated_name[16];
static MosaicRect view_area = { 0, 0, 1024, 663 };
static MosaicRect view_status = { 794, 30, 200, 90 };
static int pills = 0;
static MosaicRect last_pill;
static char named[128];
static int blits_box = 0, blits_thumb = 0, blits_shot = 0;
static MosaicRect blit_box, blit_shot;
static int loads = 0;
static char loaded[1024];
static const char* prefer_cfg = "on";

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "prefer_preview")) return prefer_cfg;
	return fallback;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) {
	return i >= 0 && i < l->count ? (MosaicEntry*)&l->items[i] : NULL;
}
static int fakeScale(void) { return 3; }
static int fakeTheme(int index) {
	switch (index) {
	case MOSAIC_COLOR_MAIN: return 0xFFFFFF;
	case MOSAIC_COLOR_LIST_TEXT: return 0xFFFFFF;
	case MOSAIC_COLOR_LIST_TEXT_SELECTED: return 0x000000;
	}
	return -1;
}
static int fakeTextHeight(int font) { return font == MOSAIC_FONT_LARGE ? 48 : 24; }
static int fakeTextTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	(void)font;
	snprintf(out, size, "%s", text);
	int w = (int)strlen(text) * 24;
	return w > max_width ? max_width : w;
}
static int fakeDrawText(MosaicSurface* t, int font, const char* text, int x, int y, MosaicColor c, int max_width) {
	(void)t; (void)font; (void)x; (void)y; (void)max_width;
	// the selected name in the selected text colour, as "[name]"
	char add[80];
	snprintf(add, sizeof(add), c.r == 0 ? "[%s]" : "%s,", text);
	if (strlen(named) + strlen(add) < sizeof(named)) strcat(named, add);
	return 0;
}
static int fakeFillRoundRect(MosaicSurface* t, const MosaicRect* r, int radius, MosaicColor c) {
	(void)t; (void)radius;
	if (c.r == 0xFF && c.g == 0xFF && c.b == 0xFF) {
		pills++;
		last_pill = *r;
	}
	return 0;
}
static const MosaicSurface* origin(const MosaicSurface* s) {
	return s->from ? s->from : s;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* sr, const MosaicRect* dr) {
	(void)t; (void)sr;
	const MosaicSurface* o = origin(s);
	if (o == &box_image) {
		blits_box++;
		blit_box = *dr;
	}
	if (o == &thumb_image) blits_thumb++;
	if (o == &shot_image) {
		blits_shot++;
		blit_shot = *dr;
	}
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
	if (copy_count >= 64) return NULL;
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
	// Tetris and Mario have boxes, Zelda only a thumbnail
	if (kind == MOSAIC_ART_BOX && (strstr(e->name, "Tetris") || strstr(e->name, "Mario"))) {
		*out = &box_image;
		return MOSAIC_ART_READY;
	}
	if (kind == MOSAIC_ART_THUMBNAIL && strstr(e->name, "Zelda")) {
		*out = &thumb_image;
		return MOSAIC_ART_READY;
	}
	return MOSAIC_ART_MISSING;
}
static int fakeArtLoad(const char* path, const MosaicSurface** out) {
	loads++;
	snprintf(loaded, sizeof(loaded), "%s", path);
	*out = &shot_image;
	return MOSAIC_ART_READY;
}
static int fakeDecorateCell(MosaicSurface* s, int index, const MosaicRect* r, const MosaicRect* name) {
	(void)s;
	if (decorated < 16) {
		decorated_index[decorated] = index;
		decorated_rect[decorated] = *r;
		decorated_name[decorated] = name ? *name : (MosaicRect){ 0, 0, 0, 0 };
	}
	decorated++;
	if (name && name->w > 0) decorated_named++;
	return 0;
}

static MosaicHost fake_host;

static void addEntry(const char* path, const char* name, int type) {
	MosaicEntry* e = &fake_list.items[fake_list.count++];
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name);
	e->type = type;
}

static void setUp(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.sdcard_path = CARD;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.entry_path = fakeEntryPath;
	fake_host.entry_name = fakeEntryName;
	fake_host.entry_type = fakeEntryType;
	fake_host.list_get = fakeListGet;
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
	fake_host.art_load = fakeArtLoad;
	fake_host.decorate_cell = fakeDecorateCell;

	memset(&fake_list, 0, sizeof(fake_list));
	addEntry(GB "/Mario.gb", "Mario", MOSAIC_ENTRY_ROM);
	addEntry(GB "/Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM);
	addEntry(GB "/Zelda.gb", "Zelda", MOSAIC_ENTRY_ROM);
	addEntry(PS "/FF7", "FF7", MOSAIC_ENTRY_DIR);
	for (int i = 0; i < 6; i++) {
		char path[512], name[16];
		snprintf(name, sizeof(name), "Game%i", i);
		snprintf(path, sizeof(path), GB "/%s.gb", name);
		addEntry(path, name, MOSAIC_ENTRY_ROM);
	}
}

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" GB "' '" PS "/FF7' '" MINUI "/GB' '" MINUI "/PS'") == 0);
	// Tetris was last saved in slot 2, which has a preview; Mario's slot 1 has none
	CHECK(system("printf '2' >'" MINUI "/GB/Tetris.gb.txt' && : >'" MINUI "/GB/Tetris.gb.2.bmp' && "
		": >'" MINUI "/GB/Tetris.gb.0.bmp' && printf '1\\n' >'" MINUI "/GB/Mario.gb.txt' && "
		": >'" MINUI "/GB/Mario.gb.0.bmp'") == 0);
	// FF7 resumes by its .m3u
	CHECK(system(": >'" PS "/FF7/FF7.cue' && : >'" PS "/FF7/FF7.m3u' && printf '0' >'" MINUI "/PS/FF7.m3u.txt' && "
		": >'" MINUI "/PS/FF7.m3u.0.bmp'") == 0);
}

static MosaicListView makeView(int selected) {
	MosaicListView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.list = &fake_list;
	v.count = fake_list.count;
	v.selected = selected;
	v.start = 0;
	v.end = 7;
	v.rows = 7;
	v.depth = 2;
	v.path = GB;
	v.show_names = 1;
	v.show_art = 1;
	v.art = "";
	v.area = view_area;
	v.status = view_status;
	return v;
}

static void resetCounts(void) {
	decorated = decorated_named = 0;
	pills = 0;
	named[0] = '\0';
	blits_box = blits_thumb = blits_shot = 0;
	loads = 0;
	loaded[0] = '\0';
	scales = 0;
}

static void testInput(void) {
	setUp();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->size == sizeof(MosaicPlugin) && !strcmp(p->id, "layout-fancy"));
	CHECK(p->init(&fake_host) == 0);
	MosaicListView v = makeView(0);
	CHECK(p->layout_init(&v) == 0);
	// every button stays the launcher's
	MosaicInput in = { sizeof(MosaicInput), 0, 0, 0, 0 };
	in.pressed = in.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A);
	unsigned int consumed = 0;
	CHECK(p->layout_input(&in, &v, &consumed) == 0);
	CHECK(consumed == 0);

	// a launcher without art is declined, and so is a screen too small
	fake_host.art_get = NULL;
	CHECK(p->init(&fake_host) == 1);
	fake_host.art_get = fakeArtGet;
	CHECK(p->init(&fake_host) == 0);
	v.area = (MosaicRect){ 0, 0, 100, 50 };
	v.status = (MosaicRect){ 0, 0, 0, 0 };
	CHECK(p->layout_init(&v) == 1);
	CHECK(p->layout_render(&screen_surface, &v) == -1);
	p->quit();
}

static void testRender(void) {
	setUp();
	makeCard();
	prefer_cfg = "on";
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	copy_count = frees = 0;

	// Mario, selected: its slot has no preview, so its box fills the panel,
	// and seven rows, its name on the pill
	resetCounts();
	MosaicListView v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(loads == 0);
	CHECK(blits_box == 1 && blits_shot == 0);
	CHECK(rectIs(blit_box, 568, 150, 362, 483));
	CHECK(decorated == 7 && decorated_named == 7);
	CHECK(decorated_index[0] == 0 && decorated_index[6] == 6);
	CHECK(rectIs(decorated_rect[0], 15, 30, 443, 90) && rectIs(decorated_rect[6], 15, 570, 443, 90));
	CHECK(pills == 1 && rectIs(last_pill, 15, 30, 120 + 72, 90));
	CHECK(rectIs(decorated_name[0], 15, 30, 192, 90));
	CHECK_STR(named, "[Mario]Tetris,Zelda,FF7,Game0,Game1,Game2,");

	// Tetris: its slot 2's preview stands in for its box
	resetCounts();
	v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(loads == 1);
	CHECK_STR(loaded, MINUI "/GB/Tetris.gb.2.bmp");
	CHECK(blits_box == 0 && blits_shot == 1);
	CHECK(rectIs(blit_shot, 519, 219, 460, 345));
	int made = scales;
	// drawn again from its copy, without looking for the state again
	CHECK(system("rm -f '" MINUI "/GB/Tetris.gb.2.bmp'") == 0);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(loads == 1 && blits_shot == 1 && scales == 0);
	CHECK(made == 1);
	// back from a game it looks again, and the preview's gone
	p->post_return();
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(loads == 0 && blits_shot == 0 && blits_box == 1);
	CHECK(system(": >'" MINUI "/GB/Tetris.gb.2.bmp'") == 0);
	p->post_return();

	// Zelda: no state, its thumbnail stands in for a box
	resetCounts();
	v = makeView(2);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(loads == 0 && blits_thumb == 1);

	// the FF7 folder resumes by its .m3u
	resetCounts();
	v = makeView(3);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK_STR(loaded, MINUI "/PS/FF7.m3u.0.bmp");
	CHECK(blits_shot == 1);

	// further down, the window follows the selection
	resetCounts();
	v = makeView(9);
	v.start = 3;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 7 && decorated_index[0] == 3 && decorated_index[6] == 9);
	CHECK(pills == 1 && rectIs(last_pill, 15, 570, 192, 90));
	p->quit();
	CHECK(frees == copy_count);

	// with prefer_preview off, Tetris's box goes on top and its preview under it
	prefer_cfg = "off";
	CHECK(p->init(&fake_host) == 0);
	copy_count = frees = 0;
	resetCounts();
	v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_box == 1 && blits_shot == 1);
	CHECK(blit_box.y == 150 && blit_box.y + blit_box.h <= 150 + 226);
	CHECK(blit_shot.y >= 407 && blit_shot.y + blit_shot.h <= 633);
	p->quit();
	prefer_cfg = "on";

	// art off: no panel, the rows take the width, and hidden names show
	CHECK(p->init(&fake_host) == 0);
	resetCounts();
	v = makeView(1);
	v.show_art = 0;
	unsigned char hidden[10] = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_box == 0 && blits_shot == 0 && loads == 0);
	CHECK(decorated_rect[0].w == 749 && decorated_rect[1].w == 949);
	CHECK(decorated_named == 7);
	// art on: a hidden name goes while there's art, the selected one too
	resetCounts();
	v.show_art = 1;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated_named == 5 && pills == 0);
	CHECK(decorated_name[0].w == 0 && decorated_name[1].w == 0);
	// a selected hidden name with no art in the panel shows
	hidden[4] = 1;
	resetCounts();
	v.selected = 4;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(pills == 1 && decorated_named == 5);
	// names off: none
	resetCounts();
	v.show_names = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 7 && decorated_named == 0 && pills == 0);
	p->quit();

	CHECK(system("rm -rf '" CARD "'") == 0);
}

int main(void) {
	testLayout();
	testWindow();
	testPanel();
	testStatePaths();
	testInput();
	testRender();
	CHECK_DONE();
}
