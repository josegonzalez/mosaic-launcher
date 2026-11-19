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

static void testPositions(void) {
	CHECK(FL_position("right") == FL_ART_RIGHT && FL_position("left") == FL_ART_LEFT && FL_position("top") == FL_ART_TOP);
	CHECK(FL_position("") == FL_ART_RIGHT && FL_position(NULL) == FL_ART_RIGHT && FL_position("Left") == FL_ART_RIGHT);
	CHECK_STR(FL_positionName(FL_ART_LEFT), "left");
	CHECK_STR(FL_positionName(FL_ART_TOP), "top");
	CHECK_STR(FL_positionName(FL_ART_RIGHT), "right");

	MosaicRect area = { 0, 0, 1024, 663 };
	MosaicRect status = { 794, 30, 200, 90 };
	MosaicRect none = { 0, 0, 0, 0 };
	FLLayout a, b;
	// right is FL_layout
	CHECK(FL_layoutAt(&area, &status, &none, 3, 1, FL_ART_RIGHT, &a) == 0);
	CHECK(FL_layout(&area, &status, &none, 3, 1, &b) == 0);
	CHECK(!memcmp(&a, &b, sizeof(a)));

	// left: mirrored, the panel from the top as the pill isn't above it, and
	// the top row stops short of the pill
	CHECK(FL_layoutAt(&area, &status, &none, 3, 1, FL_ART_LEFT, &a) == 0);
	CHECK(rectIs(a.panel, 45, 30, 460, 603));
	CHECK(a.row_x == 566 && a.row_y == 30 && a.row_w == 443 && a.row_h == 90);
	CHECK(a.top_w == 794 - 30 - 566);
	CHECK(rectIs(FL_row(&a, 1), 566, 120, 443, 90));
	CHECK(FL_rows(&a, &area) == 7);
	// header widgets above the panel push it down
	MosaicRect header = { 300, 30, 494, 90 };
	CHECK(FL_layoutAt(&area, &status, &header, 3, 1, FL_ART_LEFT, &a) == 0);
	CHECK(rectIs(a.panel, 45, 150, 460, 483));
	CHECK(a.top_w == 0);
	// no panel: as FL_layout's
	CHECK(FL_layoutAt(&area, &status, &none, 3, 0, FL_ART_LEFT, &a) == 0);
	CHECK(FL_layout(&area, &status, &none, 3, 0, &b) == 0);
	CHECK(!memcmp(&a, &b, sizeof(a)));

	// top: the top half, centred and clear of the pill, the rows under it as wide as without art
	CHECK(FL_layoutAt(&area, &status, &none, 3, 1, FL_ART_TOP, &a) == 0);
	CHECK(rectIs(a.panel, 260, 30, 504, 271));
	CHECK(a.row_x == 15 && a.row_y == 331 && a.row_w == 949 && a.top_w == 949);
	CHECK(FL_rows(&a, &area) == 3);
	CHECK(FL_layoutAt(&area, &none, &none, 3, 1, FL_ART_TOP, &a) == 0);
	CHECK(rectIs(a.panel, 45, 30, 934, 271));
	// header widgets reaching past the middle: across, below them
	CHECK(FL_layoutAt(&area, &status, &header, 3, 1, FL_ART_TOP, &a) == 0);
	CHECK(rectIs(a.panel, 45, 150, 934, 151));
	// something on the left keeps it clear there too
	MosaicRect left = { 0, 30, 100, 90 };
	CHECK(FL_layoutAt(&area, &left, &none, 3, 1, FL_ART_TOP, &a) == 0);
	CHECK(rectIs(a.panel, 130, 30, 764, 271));
	CHECK(FL_layoutAt(&area, &status, &none, 3, 0, FL_ART_TOP, &a) == 0);
	CHECK(a.panel.w == 0 && a.row_y == 30);

	// 640x480 at scale 2
	MosaicRect small = { 0, 0, 640, 400 };
	CHECK(FL_layoutAt(&small, &none, &none, 2, 1, FL_ART_LEFT, &a) == 0);
	CHECK(a.row_x == 358 && a.row_w == 272 && rectIs(a.panel, 30, 20, 288, 360));
	CHECK(FL_layoutAt(&small, &none, &none, 2, 1, FL_ART_TOP, &a) == 0);
	CHECK(rectIs(a.panel, 30, 20, 580, 160) && a.row_y == 200 && FL_rows(&a, &small) == 3);
}

static void testDescParts(void) {
	MosaicRect panel = { 519, 150, 460, 483 }, art, desc;
	FL_descParts(&panel, FL_ART_RIGHT, 30, &art, &desc);
	CHECK(rectIs(art, 519, 150, 460, 226) && rectIs(desc, 519, 407, 460, 226));
	FL_descParts(&panel, FL_ART_LEFT, 30, &art, &desc);
	CHECK(rectIs(art, 519, 150, 460, 226) && rectIs(desc, 519, 407, 460, 226));
	MosaicRect wide = { 45, 150, 934, 151 };
	FL_descParts(&wide, FL_ART_TOP, 30, &art, &desc);
	CHECK(rectIs(art, 45, 150, 452, 151) && rectIs(desc, 527, 150, 452, 151));

	// the description fills what's under (beside) the art drawn
	MosaicRect drawn = { 664, 150, 169, 226 }, nothing = { 519, 150, 0, 0 };
	CHECK(rectIs(FL_descRest(&panel, &drawn, FL_ART_RIGHT, 30), 519, 406, 460, 227));
	CHECK(rectIs(FL_descRest(&panel, &nothing, FL_ART_RIGHT, 30), 519, 150, 460, 483));
	CHECK(rectIs(FL_descRest(&panel, NULL, FL_ART_LEFT, 30), 519, 150, 460, 483));
	MosaicRect beside = { 45, 150, 113, 151 };
	CHECK(rectIs(FL_descRest(&wide, &beside, FL_ART_TOP, 30), 188, 150, 791, 151));
	// art filling the panel leaves nothing
	CHECK(FL_descRest(&panel, &panel, FL_ART_RIGHT, 30).h == 0);

	// the art goes to its place's top, or left at the top
	MosaicRect fitted = { 600, 200, 100, 50 };
	CHECK(rectIs(FL_pin(&fitted, &art, FL_ART_RIGHT), 600, 150, 100, 50));
	CHECK(rectIs(FL_pin(&fitted, &art, FL_ART_TOP), 45, 200, 100, 50));
}

// each byte 10 wide
static int tenEach(const char* text, int len, void* ctx) {
	(void)text;
	(*(int*)ctx)++;
	return len * 10;
}

static int lineIs(const char* text, FLLine line, const char* want) {
	return (int)strlen(want) == line.len && !strncmp(text + line.start, want, (size_t)line.len);
}

static void testWrap(void) {
	FLLine lines[8];
	int calls = 0;
	const char* t = "the quick brown fox";
	CHECK(FL_wrap(t, 100, tenEach, &calls, lines, 8) == 2);
	CHECK(lineIs(t, lines[0], "the quick") && lineIs(t, lines[1], "brown fox"));
	CHECK(calls > 0);
	// spaces at the start and between lines go, inner ones stay
	t = "  a   b  ";
	CHECK(FL_wrap(t, 30, tenEach, &calls, lines, 8) == 2);
	CHECK(lineIs(t, lines[0], "a") && lineIs(t, lines[1], "b"));
	CHECK(FL_wrap(t, 50, tenEach, &calls, lines, 8) == 1);
	CHECK(lineIs(t, lines[0], "a   b"));
	// a word wider than a line is cut
	t = "abcdefghijklmnop qr";
	CHECK(FL_wrap(t, 50, tenEach, &calls, lines, 8) == 4);
	CHECK(lineIs(t, lines[0], "abcde") && lineIs(t, lines[2], "klmno") && lineIs(t, lines[3], "p qr"));
	// between UTF-8 characters, at least one a line
	t = "\xc3\xa9\xc3\xa9\xc3\xa9";
	CHECK(FL_wrap(t, 30, tenEach, &calls, lines, 8) == 3);
	CHECK(lines[0].len == 2 && lines[1].start == 2 && lines[2].len == 2);
	CHECK(FL_wrap(t, 5, tenEach, &calls, lines, 8) == 3);
	// up to max lines
	CHECK(FL_wrap("a b c d e f g h i j", 10, tenEach, &calls, lines, 4) == 4);
	CHECK(FL_wrap("", 100, tenEach, &calls, lines, 8) == 0);
	CHECK(FL_wrap("   ", 100, tenEach, &calls, lines, 8) == 0);
	CHECK(FL_wrap(NULL, 100, tenEach, &calls, lines, 8) == 0);
	CHECK(FL_wrap("a", 0, tenEach, &calls, lines, 8) == 0);
}

static void testScroll(void) {
	// still for the first two seconds, then 30 a second, looping each 300 + 48
	CHECK(FL_scrollOffset(0, 300, 200, 48, 30) == 0);
	CHECK(FL_scrollOffset(1999, 300, 200, 48, 30) == 0);
	CHECK(FL_scrollOffset(2000, 300, 200, 48, 30) == 0);
	CHECK(FL_scrollOffset(3000, 300, 200, 48, 30) == 30);
	CHECK(FL_scrollOffset(2000 + 11600, 300, 200, 48, 30) == 0);
	CHECK(FL_scrollOffset(14000, 300, 200, 48, 30) == 12);
	// a description that fits never moves
	CHECK(FL_scrollOffset(10000, 200, 200, 48, 30) == 0);
	CHECK(FL_scrollOffset(10000, 100, 200, 48, 30) == 0);
	CHECK(FL_scrollOffset(10000, 300, 200, 48, 0) == 0);

	MosaicRect view = { 519, 406, 460, 227 }, src[2], dst[2];
	CHECK(FL_scrollParts(&view, 300, 48, 0, src, dst) == 1);
	CHECK(rectIs(src[0], 0, 0, 460, 227) && rectIs(dst[0], 519, 406, 460, 227));
	CHECK(FL_scrollParts(&view, 300, 48, 100, src, dst) == 1);
	CHECK(rectIs(src[0], 0, 100, 460, 200) && rectIs(dst[0], 519, 406, 460, 200));
	// its end, the gap, and its start again
	CHECK(FL_scrollParts(&view, 300, 48, 200, src, dst) == 2);
	CHECK(rectIs(src[0], 0, 200, 460, 100) && rectIs(dst[0], 519, 406, 460, 100));
	CHECK(rectIs(src[1], 0, 0, 460, 79) && rectIs(dst[1], 519, 554, 460, 79));
	// inside the gap, only the start again
	CHECK(FL_scrollParts(&view, 300, 48, 320, src, dst) == 1);
	CHECK(rectIs(src[0], 0, 0, 460, 199) && rectIs(dst[0], 519, 434, 460, 199));
	// one that fits: once, at the top
	CHECK(FL_scrollParts(&view, 100, 48, 0, src, dst) == 1);
	CHECK(rectIs(src[0], 0, 0, 460, 100) && rectIs(dst[0], 519, 406, 460, 100));
	CHECK(FL_scrollParts(&view, 0, 48, 0, src, dst) == 0);
	MosaicRect empty = { 0, 0, 0, 0 };
	CHECK(FL_scrollParts(&empty, 300, 48, 0, src, dst) == 0);
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
	// a plugin's replacement takes the box's place, and the preview never hides it
	CHECK(FL_replacedArt(0, 1, 1) == 0);
	CHECK(FL_replacedArt(1, 1, 1) == FL_SHOW_BOX);
	CHECK(FL_replacedArt(1, 0, 1) == (FL_SHOW_BOX | FL_SHOW_PREVIEW));
	CHECK(FL_replacedArt(1, 0, 0) == FL_SHOW_BOX);
	CHECK(FL_replacedArt(1, 1, 0) == FL_SHOW_BOX);

	// what art decorators go under: everything the panel drew
	MosaicRect top = { 519, 150, 460, 226 }, bottom = { 600, 407, 300, 226 }, none = { 519, 150, 0, 0 };
	CHECK(rectIs(FL_around(&top, &bottom), 519, 150, 460, 483));
	CHECK(rectIs(FL_around(&top, &none), 519, 150, 460, 226));
	CHECK(rectIs(FL_around(&none, &bottom), 600, 407, 300, 226));
	MosaicRect both = FL_around(&none, &none);
	CHECK(both.w == 0 && both.h == 0);

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
static const char* position_cfg = "right";
static const char* desc_cfg = NULL; // NULL for unset
// the description's surface, and what of it was drawn last
static const MosaicSurface desc_origin = { 0, 0, 0, NULL };
static int blits_desc = 0, desc_surfaces = 0, infos = 0, redraws = 0;
static MosaicRect desc_src, desc_dst;
static MosaicSurface video = { 720, 480, 0, NULL }; // another plugin's art_replace surface
static int replaced_index = -1; // the entry it's for, or -1 for none
static int blits_video = 0;
static MosaicRect blit_video;
static int art_decorated = 0, art_decorated_index = -1;
static MosaicRect art_decorated_rect;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "prefer_preview")) return prefer_cfg;
	if (!strcmp(key, "position")) return position_cfg;
	if (!strcmp(key, "description") && desc_cfg) return desc_cfg;
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
	if (o == &desc_origin) {
		blits_desc++;
		desc_src = sr ? *sr : (MosaicRect){ 0, 0, s->w, s->h };
		desc_dst = *dr;
	}
	if (s == &video) {
		blits_video++;
		blit_video = *dr;
	}
	return 0;
}
static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return (int)strlen(text) * 24;
}
static MosaicSurface* fakeSurfaceNew(const char* id, int w, int h) {
	(void)id;
	if (copy_count >= 64) return NULL;
	desc_surfaces++;
	MosaicSurface* s = &copies[copy_count++];
	*s = (MosaicSurface){ w, h, 1, &desc_origin };
	return s;
}
// Mario's description is long, Tetris's short, and the rest have none
static const char* mario_desc = "Mario runs and jumps through the Mushroom Kingdom to save the princess from "
	"Bowser, across land, sea and sky, with mushrooms, flowers and stars to help him on his way.";
static int fakeEntryInfo(const MosaicEntry* e, const char* key, char* out, size_t size) {
	infos++;
	const char* value = NULL;
	if (!strcmp(key, "description") && !strcmp(e->name, "Mario")) value = mario_desc;
	if (!strcmp(key, "description") && !strcmp(e->name, "Tetris")) value = "Blocks fall.";
	snprintf(out, size, "%s", value ? value : "");
	return value ? 1 : 0;
}
static void fakeRequestRedraw(void) {
	redraws++;
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

static int fakeArtReplacement(int index, const MosaicSurface** out) {
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
	blits_video = 0;
	art_decorated = 0;
	art_decorated_index = -1;
	blits_desc = 0;
	desc_surfaces = 0;
	infos = 0;
	redraws = 0;
}

static void testPositionRender(void) {
	setUp();
	makeCard();
	fake_host.decorate_art = fakeDecorateArt;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);

	// left: Mario's box down the panel on the left, the rows on the right
	position_cfg = "left";
	CHECK(p->init(&fake_host) == 0);
	resetCounts();
	MosaicListView v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_box == 1 && rectIs(blit_box, 49, 30, 452, 603));
	CHECK(decorated == 7 && rectIs(decorated_rect[0], 566, 30, 198, 90) && rectIs(decorated_rect[1], 566, 120, 443, 90));
	CHECK(art_decorated == 1 && rectIs(art_decorated_rect, 49, 30, 452, 603));
	p->quit();

	// top: the box across the top half, three rows under it, the window following the selection
	position_cfg = "top";
	CHECK(p->init(&fake_host) == 0);
	resetCounts();
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_box == 1 && rectIs(blit_box, 410, 30, 203, 271));
	CHECK(decorated == 3 && rectIs(decorated_rect[0], 15, 331, 949, 90) && rectIs(decorated_rect[2], 15, 511, 949, 90));
	resetCounts();
	v = makeView(5);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(decorated == 3 && decorated_index[0] == 3 && decorated_index[2] == 5);
	p->quit();
	position_cfg = "right";
	CHECK(system("rm -rf '" CARD "'") == 0);
}

static void testDescription(void) {
	setUp();
	makeCard();
	fake_host.decorate_art = fakeDecorateArt;
	fake_host.entry_info = fakeEntryInfo;
	fake_host.text_width = fakeTextWidth;
	fake_host.surface_new = fakeSurfaceNew;
	fake_host.request_redraw = fakeRequestRedraw;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);

	// off by default: Mario as before, its description never asked for, and no frames asked for
	desc_cfg = NULL;
	CHECK(p->init(&fake_host) == 0);
	copy_count = frees = 0;
	resetCounts();
	p->tick(1000);
	MosaicListView v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(rectIs(blit_box, 568, 150, 362, 483) && art_decorated == 1);
	CHECK(infos == 0 && blits_desc == 0);
	p->tick(5000);
	CHECK(redraws == 0);
	p->quit();

	// on: Mario's box in the top half, pinned to its top, and its description,
	// 19 characters to a line, under it with no art decorators
	desc_cfg = "on";
	CHECK(p->init(&fake_host) == 0);
	copy_count = frees = 0;
	resetCounts();
	p->tick(1000);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_box == 1 && rectIs(blit_box, 664, 150, 169, 226));
	CHECK(art_decorated == 0);
	CHECK(desc_surfaces == 1 && blits_desc == 1);
	CHECK(copies[copy_count - 1].w == 460 && copies[copy_count - 1].h == 240);
	CHECK(rectIs(desc_src, 0, 0, 460, 227) && rectIs(desc_dst, 519, 406, 460, 227));
	// still for two seconds: no frames asked for
	p->tick(1500);
	p->tick(2999);
	CHECK(redraws == 0);
	// then a frame each frame while it scrolls, and only once each is drawn
	p->tick(3000);
	CHECK(redraws == 1);
	p->tick(3016);
	CHECK(redraws == 1);
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(desc_surfaces == 0 && infos == 0 && blits_desc == 1 && rectIs(desc_src, 0, 0, 460, 227));
	p->tick(4000);
	CHECK(redraws == 1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	// 30 a second for a second, of its 10 lines, 240 tall
	CHECK(rectIs(desc_src, 0, 30, 460, 210) && rectIs(desc_dst, 519, 406, 460, 210));

	// Tetris's fits under its preview: it never moves
	resetCounts();
	p->tick(5000);
	v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_shot == 1 && blits_desc == 1 && desc_src.h == 24);
	CHECK(rectIs(blit_shot, 598, 150, 301, 226));
	redraws = 0;
	p->tick(5100);
	p->tick(9000);
	CHECK(redraws == 0);

	// Zelda has none: its thumbnail as before, with art decorators
	resetCounts();
	v = makeView(2);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_thumb == 1 && blits_desc == 0 && art_decorated == 1);
	p->tick(20000);
	CHECK(redraws == 0);

	// back on Mario, the wait starts again
	resetCounts();
	p->tick(30000);
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(desc_surfaces == 1);
	p->tick(31000);
	CHECK(redraws == 0);
	p->tick(32000);
	CHECK(redraws == 1);
	// art off: no panel, no description, no frames
	resetCounts();
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_desc == 0);
	p->tick(40000);
	CHECK(redraws == 0);
	p->quit();
	CHECK(frees == copy_count);

	// at the top: the box in the panel's left half, pinned left, the description beside it
	position_cfg = "top";
	CHECK(p->init(&fake_host) == 0);
	copy_count = frees = 0;
	resetCounts();
	v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(rectIs(blit_box, 260, 30, 203, 271));
	CHECK(desc_dst.x == 493 && desc_dst.y == 30 && desc_dst.w == 271 && desc_dst.h <= 271);
	p->quit();
	position_cfg = "right";
	desc_cfg = NULL;
	CHECK(system("rm -rf '" CARD "'") == 0);
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

static void testReplacement(void) {
	setUp();
	makeCard();
	fake_host.art_replacement = fakeArtReplacement;
	fake_host.decorate_art = fakeDecorateArt;
	prefer_cfg = "on";
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	copy_count = frees = 0;

	// none showing: Mario's box, with art decorators under it
	resetCounts();
	MosaicListView v = makeView(0);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_box == 1 && blits_video == 0);
	CHECK(art_decorated == 1 && art_decorated_index == 0 && rectIs(art_decorated_rect, 568, 150, 362, 483));

	// Mario's video takes its box's place, fitted across the panel, straight
	// from the plugin's surface
	replaced_index = 0;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_box == 0 && blits_video == 1 && scales == 0);
	CHECK(rectIs(blit_video, 519, 238, 460, 306));
	CHECK(art_decorated == 1 && rectIs(art_decorated_rect, 519, 238, 460, 306));
	// Tetris's state preview doesn't stand in for its video
	replaced_index = 1;
	resetCounts();
	v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_video == 1 && blits_shot == 0 && rectIs(blit_video, 519, 238, 460, 306));
	// another entry's isn't drawn for Zelda
	replaced_index = 1;
	resetCounts();
	v = makeView(2);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_video == 0 && blits_thumb == 1);
	// Game0 has no art: its video gives the panel art, so a hidden name goes
	unsigned char hidden[10] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
	replaced_index = 4;
	resetCounts();
	v = makeView(4);
	v.names_hidden = hidden;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_video == 1 && pills == 0 && art_decorated == 1);
	// and without it, it has no art and no art decorators
	replaced_index = -1;
	resetCounts();
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_video == 0 && pills == 1 && art_decorated == 0);
	// art off: no video, no art decorators
	replaced_index = 0;
	resetCounts();
	v = makeView(0);
	v.show_art = 0;
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_video == 0 && art_decorated == 0);
	p->quit();

	// with prefer_preview off, Tetris's video goes on top and its preview under it,
	// and art decorators under both
	prefer_cfg = "off";
	CHECK(p->init(&fake_host) == 0);
	replaced_index = 1;
	resetCounts();
	v = makeView(1);
	CHECK(p->layout_render(&screen_surface, &v) == 0);
	CHECK(blits_video == 1 && blits_shot == 1 && blits_box == 0);
	CHECK(blit_video.y >= 150 && blit_video.y + blit_video.h <= 150 + 226);
	CHECK(art_decorated == 1 && art_decorated_rect.y == blit_video.y &&
		art_decorated_rect.y + art_decorated_rect.h == blit_shot.y + blit_shot.h);
	p->quit();
	prefer_cfg = "on";
	replaced_index = -1;
	CHECK(system("rm -rf '" CARD "'") == 0);
}

int main(void) {
	testLayout();
	testPositions();
	testDescParts();
	testWrap();
	testScroll();
	testWindow();
	testPanel();
	testStatePaths();
	testInput();
	testRender();
	testReplacement();
	testPositionRender();
	testDescription();
	CHECK_DONE();
}
