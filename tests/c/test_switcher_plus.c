// The game switcher plus plugin: its model on its own, then the plugin
// against a fake host, for its screen, the buttons, the recents it keeps,
// the hints, what each style draws and the play time captions, which are
// checked against playtime-captions' own model.

#include <stdlib.h>
#include "check.h"
#include "words.h"
#include "../../plugins/switcher-plus/switcher_plus.c"
#include "../../plugins/playtime-captions/captions_model.h"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

// desktop: 1024x768 at scale 3, the status pill 200 wide
static const MosaicRect AREA = { 0, 0, 1024, 768 };
static const MosaicRect STATUS = { 794, 30, 200, 90 };
static const MosaicRect NONE = { 0, 0, 0, 0 };

static void testSettings(void) {
	CHECK(SP_style("full") == SP_STYLE_FULL);
	CHECK(SP_style("strip") == SP_STYLE_STRIP);
	CHECK(SP_style("list") == SP_STYLE_LIST);
	CHECK(SP_style("grid") == SP_STYLE_FULL);
	CHECK(SP_style("") == SP_STYLE_FULL);
	CHECK(SP_style(NULL) == SP_STYLE_FULL);
}

static void testMoves(void) {
	// a press wraps at the ends, a held repeat stops there
	CHECK(SP_move(0, 3, -1, 1) == 2);
	CHECK(SP_move(0, 3, -1, 0) == 0);
	CHECK(SP_move(2, 3, 1, 1) == 0);
	CHECK(SP_move(2, 3, 1, 0) == 2);
	CHECK(SP_move(1, 3, 1, 0) == 2);
	// a page stops at the end before it wraps
	CHECK(SP_move(1, 10, -6, 1) == 0);
	CHECK(SP_move(8, 10, 6, 1) == 9);
	CHECK(SP_move(9, 10, 6, 1) == 0);
	CHECK(SP_move(0, 1, 1, 1) == 0);
	CHECK(SP_move(0, 0, 1, 1) == 0);
	CHECK(SP_move(1, 3, 0, 1) == 1);

	// as stock's: the one before, or the last from the first
	CHECK(SP_afterRemove(1, 2) == 0);
	CHECK(SP_afterRemove(0, 2) == 1);
	CHECK(SP_afterRemove(2, 2) == 1);
	CHECK(SP_afterRemove(0, 0) == 0);

	CHECK(SP_window(0, 7, 10, 6) == 2);
	CHECK(SP_window(5, 3, 10, 6) == 3);
	CHECK(SP_window(2, 4, 10, 6) == 2);
	CHECK(SP_window(8, 9, 10, 6) == 4); // never past the last row
	CHECK(SP_window(3, 1, 4, 6) == 0);
	CHECK(SP_window(3, 0, 0, 6) == 0);
}

static void testHints(void) {
	MosaicHint left[2], right[2];
	int l = -1, r = -1;
	SP_hints(0, 0, left, &l, right, &r);
	CHECK(l == 0 && r == 1);
	CHECK(right[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(right[0].label, "BACK");

	SP_hints(3, 1, left, &l, right, &r);
	CHECK(l == 1 && r == 2);
	CHECK(left[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(left[0].label, "BACK");
	CHECK(right[0].button == MOSAIC_BUTTON_Y && right[1].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "REMOVE");
	CHECK_STR(right[1].label, "RESUME");

	SP_hints(3, 0, left, &l, right, &r);
	CHECK(l == 1 && r == 1);
	CHECK(right[0].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "OPEN");
}

///////////////////////////////////////
// recents for the model and the fake host

static const char* fake_paths[SP_MAX_RECENTS + 2];
static MosaicRecent fake_infos[SP_MAX_RECENTS + 2];
static int fake_count = 0;
static int info_asks = 0;
static int info_fails = -1; // the index recent_info fails for

static const char* modelPath(int index, void* data) {
	(void)data;
	return index >= 0 && index < fake_count ? fake_paths[index] : NULL;
}

static int modelInfo(int index, MosaicRecent* out, void* data) {
	(void)data;
	info_asks++;
	if (index < 0 || index >= fake_count || index == info_fails) return -1;
	size_t size = out->size;
	*out = fake_infos[index];
	out->size = size;
	return 0;
}

static void fakeRecent(int index, const char* path, const char* name, int can_resume, const char* preview) {
	fake_paths[index] = path;
	memset(&fake_infos[index], 0, sizeof(fake_infos[index]));
	snprintf(fake_infos[index].name, sizeof(fake_infos[index].name), "%s", name);
	fake_infos[index].type = MOSAIC_ENTRY_ROM;
	fake_infos[index].available = 1;
	fake_infos[index].can_resume = can_resume;
	snprintf(fake_infos[index].preview, sizeof(fake_infos[index].preview), "%s", preview ? preview : "");
}

static void threeRecents(void) {
	fake_count = 3;
	fakeRecent(0, "/sd/Roms/GB/Apple.gb", "Apple", 1, "/sd/.minui/GB/Apple.gb.0.bmp");
	fakeRecent(1, "/sd/Roms/GB/Banana.gb", "Banana", 0, NULL);
	fakeRecent(2, "/sd/Roms/GB/Cherry.gb", "Cherry", 1, "/sd/.minui/GB/Cherry.gb.1.bmp");
}

static int skip_asks = 0;
static int skipBanana(const char* path, void* data) {
	(void)data;
	skip_asks++;
	return strstr(path, "Banana") != NULL;
}

static void testRecents(void) {
	static SPRecents r;
	memset(&r, 0, sizeof(r));
	threeRecents();
	CHECK(SP_sync(&r, 3, modelPath, NULL, NULL) == 1);
	CHECK(r.count == 3);
	CHECK(SP_sync(&r, 3, modelPath, NULL, NULL) == 0);

	// asked once until they change
	info_asks = 0;
	const MosaicRecent* i = SP_info(&r, 1, modelInfo, NULL);
	CHECK(i && strcmp(i->name, "Banana") == 0 && i->can_resume == 0 && i->size == sizeof(MosaicRecent));
	CHECK(SP_info(&r, 1, modelInfo, NULL) == i);
	CHECK(info_asks == 1);
	CHECK(SP_info(&r, 3, modelInfo, NULL) == NULL);
	CHECK(SP_info(&r, -1, modelInfo, NULL) == NULL);
	CHECK(info_asks == 1);

	// a path that changes in place
	fake_paths[2] = "/sd/Roms/GB/Date.gb";
	CHECK(SP_sync(&r, 3, modelPath, NULL, NULL) == 1);
	CHECK_STR(r.path[2], "/sd/Roms/GB/Date.gb");
	SP_info(&r, 1, modelInfo, NULL);
	CHECK(info_asks == 2);
	// one fewer
	CHECK(SP_sync(&r, 2, modelPath, NULL, NULL) == 1);
	CHECK(r.count == 2);
	// a failed ask isn't asked again until they change
	info_fails = 0;
	CHECK(SP_info(&r, 0, modelInfo, NULL) == NULL);
	CHECK(SP_info(&r, 0, modelInfo, NULL) == NULL);
	CHECK(info_asks == 3);
	info_fails = -1;
	// none, and more than it keeps
	CHECK(SP_sync(&r, 0, modelPath, NULL, NULL) == 1 && r.count == 0);
	CHECK(SP_sync(&r, 0, modelPath, NULL, NULL) == 0);
	for (int k = 0; k < SP_MAX_RECENTS + 2; k++) fake_paths[k] = "/sd/Roms/GB/Same.gb";
	fake_count = SP_MAX_RECENTS + 2;
	CHECK(SP_sync(&r, fake_count, modelPath, NULL, NULL) == 1 && r.count == SP_MAX_RECENTS);
	CHECK(SP_sync(&r, fake_count, modelPath, NULL, NULL) == 0);

	// a skipped recent isn't a row, and a row's info is asked by its index
	// in the host's recents
	threeRecents();
	skip_asks = 0;
	CHECK(SP_sync(&r, 3, modelPath, skipBanana, NULL) == 1);
	CHECK(r.count == 2 && r.index[0] == 0 && r.index[1] == 2);
	CHECK_STR(r.path[1], "/sd/Roms/GB/Cherry.gb");
	const MosaicRecent* c = SP_info(&r, 1, modelInfo, NULL);
	CHECK(c && !strcmp(c->name, "Cherry"));
	CHECK(skip_asks == 3);
	// asked again only when the recents change, or count is -1
	CHECK(SP_sync(&r, 3, modelPath, skipBanana, NULL) == 0);
	CHECK(skip_asks == 3);
	r.count = -1;
	CHECK(SP_sync(&r, 3, modelPath, skipBanana, NULL) == 1);
	CHECK(skip_asks == 6 && r.count == 2);
	fake_count = 0;
}

static void testGeometry(void) {
	MosaicRect parts[4];
	CHECK(SP_avoid(&AREA, &STATUS, parts) == 4);
	CHECK(rectIs(parts[0], 0, 0, 1024, 30));
	CHECK(rectIs(parts[1], 0, 30, 794, 90));
	CHECK(rectIs(parts[2], 994, 30, 30, 90));
	CHECK(rectIs(parts[3], 0, 120, 1024, 648));
	MosaicRect away = { 0, 300, 100, 100 };
	CHECK(SP_avoid(&away, &STATUS, parts) == 1 && rectIs(parts[0], 0, 300, 100, 100));
	CHECK(SP_avoid(&away, &NONE, parts) == 1);
	CHECK(SP_avoid(&NONE, &STATUS, parts) == 0);
	// a hole at the corner leaves two
	MosaicRect corner = { 900, 0, 200, 50 };
	CHECK(SP_avoid(&AREA, &corner, parts) == 2);
	CHECK(rectIs(parts[0], 0, 0, 900, 50));
	CHECK(rectIs(parts[1], 0, 50, 1024, 718));

	MosaicRect header = { 600, 30, 194, 90 };
	CHECK(rectIs(SP_hole(&STATUS, &header), 600, 30, 394, 90));
	MosaicRect tall = { 600, 20, 194, 120 };
	CHECK(rectIs(SP_hole(&STATUS, &tall), 600, 20, 394, 120));
	CHECK(rectIs(SP_hole(&STATUS, &NONE), 794, 30, 200, 90));
	CHECK(rectIs(SP_hole(&NONE, &header), 600, 30, 194, 90));
	CHECK(SP_hole(&NONE, NULL).w == 0);

	CHECK(rectIs(SP_meet(&AREA, &STATUS), 794, 30, 200, 90));
	CHECK(SP_meet(&away, &STATUS).w == 0);

	MosaicRect dst = { 100, 100, 200, 100 }, part = { 150, 100, 50, 50 };
	CHECK(rectIs(SP_source(&dst, &part, 400, 200), 100, 0, 100, 100));
	CHECK(rectIs(SP_source(&dst, &dst, 200, 100), 0, 0, 200, 100));

	CHECK(rectIs(SP_fit(640, 480, &AREA), 0, 0, 1024, 768));
	MosaicRect box = { 0, 0, 100, 100 };
	CHECK(rectIs(SP_fit(300, 100, &box), 0, 33, 100, 33));
	CHECK(rectIs(SP_fit(100, 200, &box), 25, 0, 50, 100));
	CHECK(SP_fit(0, 100, &box).w == 0);

	// stock's title: PADDING in, up to the status pill
	CHECK(rectIs(SP_title(&AREA, &STATUS, 3, 200), 30, 30, 272, 90));
	CHECK(rectIs(SP_title(&AREA, &STATUS, 3, 1000), 30, 30, 764, 90));
	CHECK(rectIs(SP_title(&AREA, &NONE, 3, 1000), 30, 30, 964, 90));
	MosaicRect wide = { 40, 30, 984, 90 };
	CHECK(SP_title(&AREA, &wide, 3, 100).w == 0);

	// below the status pill, above the hints
	CHECK(rectIs(SP_room(&AREA, &STATUS, 3), 30, 150, 964, 468));
	CHECK(rectIs(SP_room(&AREA, &NONE, 3), 30, 30, 964, 588));
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(SP_room(&tiny, &NONE, 3).w == 0);
}

static void testStrip(void) {
	MosaicRect room = SP_room(&AREA, &STATUS, 3);
	SPStrip s;
	CHECK(SP_strip(&room, 1024, 768, 3, 90, 96, &s) == 0);
	// two fifths of the room's width, at the screen's shape, one either side
	CHECK(s.w[0] == 385 && s.h[0] == 288);
	CHECK(s.sides == 1 && s.w[1] == 231 && s.h[1] == 172);
	CHECK(s.cx == 512 && s.cy == 324);
	CHECK(rectIs(s.name, 30, 498, 964, 90));
	CHECK(s.name.y + s.name.h <= room.y + room.h);
	CHECK(rectIs(SP_stripCard(&s, 0), 320, 180, 385, 288));
	CHECK(rectIs(SP_stripCard(&s, 1), 734, 238, 231, 172));
	CHECK(rectIs(SP_stripCard(&s, -1), 60, 238, 231, 172));
	CHECK(SP_stripCard(&s, 2).w == 0);
	CHECK(SP_stripCard(&s, -2).w == 0);

	// a short room: the height decides, and more fit
	MosaicRect low = { 0, 0, 1000, 200 };
	CHECK(SP_strip(&low, 4, 3, 1, 0, 20, &s) == 0);
	CHECK(s.h[0] == 200 && s.w[0] == 266);
	CHECK(s.sides == 3);
	CHECK(s.name.h == 0);
	MosaicRect r = SP_stripCard(&s, -s.sides);
	CHECK(r.x >= low.x);
	r = SP_stripCard(&s, s.sides);
	CHECK(r.x + r.w <= low.x + low.w);

	CHECK(SP_strip(&room, 1024, 768, 3, 90, 400, &s) == -1);
	CHECK(SP_strip(&NONE, 1024, 768, 3, 90, 1, &s) == -1);
}

static void testList(void) {
	SPList l;
	CHECK(SP_list(&AREA, &STATUS, 3, &l) == 0);
	CHECK(rectIs(l.rows, 30, 30, 379, 540));
	CHECK(l.row_h == 90 && l.count == 6);
	CHECK(rectIs(l.panel, 439, 150, 555, 468));
	// header widgets reaching over the column push the rows down
	MosaicRect hole = { 300, 30, 694, 90 };
	CHECK(SP_list(&AREA, &hole, 3, &l) == 0);
	CHECK(l.rows.y == 150 && l.count == 5);
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(SP_list(&tiny, &NONE, 3, &l) == -1);
}

// 6 pixels a byte, as playtime-captions' tests measure
static int bytes(const char* text, void* ctx) {
	(void)ctx;
	return 6 * (int)strlen(text);
}

// The copy of playtime-captions' formatter writes what it writes, for
// every part, format and never setting over a spread of histories.
static void testCaptionsCopy(void) {
	SPCaptions c = SP_captions("on", "on", "off", "relative", "off", "off");
	CHECK(c.parts == (SP_PART_TIME | SP_PART_LAST) && c.format == SP_FORMAT_RELATIVE && c.never == 0);
	c = SP_captions("off", "off", "on", "compact", "on", "on");
	CHECK(c.parts == (SP_PART_SESSIONS | SP_PART_ACHIEVEMENTS) && c.format == SP_FORMAT_COMPACT && c.never == 1);
	CHECK(SP_captions(NULL, NULL, NULL, "date", NULL, NULL).format == SP_FORMAT_DATE);
	CHECK(SP_captions(NULL, NULL, NULL, "fancy", NULL, NULL).format == SP_FORMAT_RELATIVE);
	CHECK(SP_captions("yes", NULL, NULL, NULL, NULL, NULL).parts == 0);
	CHECK((int)SP_PART_TIME == (int)PC_PART_TIME && (int)SP_PART_LAST == (int)PC_PART_LAST &&
		(int)SP_PART_SESSIONS == (int)PC_PART_SESSIONS && (int)SP_PART_ACHIEVEMENTS == (int)PC_PART_ACHIEVEMENTS);
	CHECK((int)SP_FORMAT_RELATIVE == (int)PC_FORMAT_RELATIVE && (int)SP_FORMAT_COMPACT == (int)PC_FORMAT_COMPACT &&
		(int)SP_FORMAT_DATE == (int)PC_FORMAT_DATE);
	CHECK(SP_mute(0xFFFFFF, 0x000000, 40) == PC_mute(0xFFFFFF, 0x000000, 40));
	CHECK(SP_mute(0x123456, 0xABCDEF, 75) == PC_mute(0x123456, 0xABCDEF, 75));
	CHECK(SP_mute(0x123456, 0xABCDEF, 150) == 0xABCDEF && SP_mute(0x123456, 0xABCDEF, -5) == 0x123456);

	static const char* zones[] = { "UTC0", "TST-11" };
	static const long long times[] = { -1, 0, 30, 60, 3600, 8100, 360000 };
	static const long long agos[] = { -2, -1, 0, 3600, 86400, 3 * 86400, 10 * 86400, 40 * 86400, 400 * 86400 };
	static const int counts[] = { -1, 0, 1, 2 };
	static const int cheevos[][2] = { { 0, 0 }, { 1, 3 }, { -1, 3 } };
	static const int widths[] = { 0, 30, 100, 1000 };
	int builds = 0, wrong = 0;
	for (size_t z = 0; z < sizeof(zones) / sizeof(zones[0]); z++) {
		setenv("TZ", zones[z], 1);
		tzset();
		long long now = 1762344000; // 2025-11-05 12:00 UTC
		for (size_t t = 0; t < sizeof(times) / sizeof(times[0]); t++)
		for (size_t a = 0; a < sizeof(agos) / sizeof(agos[0]); a++)
		for (size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); n++)
		for (size_t r = 0; r < sizeof(cheevos) / sizeof(cheevos[0]); r++) {
			// -2 for a never-set last played (0), -1 for unknown
			long long last = agos[a] == -2 ? 0 : agos[a] == -1 ? -1 : now - agos[a];
			SPPlay play = { times[t], last, counts[n], cheevos[r][0], cheevos[r][1] };
			PCStats stats = { times[t], last, counts[n], cheevos[r][0], cheevos[r][1] };
			for (int parts = 0; parts < 16; parts++)
			for (int format = 0; format < 3; format++)
			for (int never = 0; never < 2; never++) {
				char mine[160], theirs[160];
				for (int max = 1; max <= 4; max += 3) {
					int got = SP_captionBuild(&play, now, parts, format, never, max, mine, sizeof(mine));
					int want = PC_build(&stats, now, parts, format, never, max, theirs, sizeof(theirs));
					builds++;
					if (got != want || strcmp(mine, theirs) != 0) {
						if (!wrong) fprintf(stderr, "build: got %i \"%s\", want %i \"%s\"\n", got, mine, want, theirs);
						wrong++;
					}
				}
				SPCaptions mine_c = { parts, format, never };
				PCOptions theirs_c = { parts, format, never };
				for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
					int got = SP_captionFit(&play, now, &mine_c, widths[w], bytes, NULL, mine, sizeof(mine));
					int want = PC_fit(&stats, now, &theirs_c, widths[w], bytes, NULL, theirs, sizeof(theirs));
					if (got != want || strcmp(mine, theirs) != 0) {
						if (!wrong) fprintf(stderr, "fit: got %i \"%s\", want %i \"%s\"\n", got, mine, want, theirs);
						wrong++;
					}
				}
			}
		}
	}
	unsetenv("TZ");
	tzset();
	CHECK(builds > 100000);
	CHECK(wrong == 0);

	// a few by hand, so a change to both shows here too
	setenv("TZ", "UTC0", 1);
	tzset();
	long long now = 1762344000;
	SPPlay tetris = { 8100, now - 3 * 86400, 2, 0, 0 };
	SPCaptions both = SP_captions("on", "on", "off", "relative", "off", "off");
	char out[160];
	CHECK(SP_captionFit(&tetris, now, &both, 1000, bytes, NULL, out, sizeof(out)) == 120);
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago");
	CHECK(SP_captionFit(&tetris, now, &both, 60, bytes, NULL, out, sizeof(out)) == 48);
	CHECK_STR(out, "2h \xC2\xB7 3d");

	// both copies take the same words
	static const char* const words[] = { "%lldh %lldm", "%lld h %lld min", "Nov", "nov", NULL };
	static const char* const plurals[] = { "%i day ago", "hace %i d\xc3\xad" "a", "hace %i d\xc3\xad" "as",
		"%i session", "%i sesi\xc3\xb3n", "%i sesiones", NULL };
	fake_words = words;
	fake_plurals = plurals;
	SPWords sp_words = { fakeTr, fakePlural };
	PCWords pc_words = { fakeTr, fakePlural };
	SP_setWords(&sp_words);
	PC_setWords(&pc_words);
	PCStats tetris_pc = { 8100, now - 3 * 86400, 2, 0, 0 };
	char theirs[256];
	for (int format = 0; format < 3; format++) {
		SP_captionBuild(&tetris, now, 7, format, 0, 3, out, sizeof(out));
		PC_build(&tetris_pc, now, 7, format, 0, 3, theirs, sizeof(theirs));
		CHECK_STR(out, theirs);
	}
	SP_captionBuild(&tetris, now, 7, SP_FORMAT_RELATIVE, 0, 3, out, sizeof(out));
	CHECK_STR(out, "2 h 15 min \xC2\xB7 hace 3 d\xc3\xad" "as \xC2\xB7 2 sesiones");
	SP_setWords(NULL);
	PC_setWords(NULL);
	SP_captionBuild(&tetris, now, 7, SP_FORMAT_RELATIVE, 0, 3, out, sizeof(out));
	CHECK_STR(out, "2h 15m \xC2\xB7 3 days ago \xC2\xB7 2 sessions");
	CHECK(SP_captionFit(&tetris, now, &both, 10, bytes, NULL, out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	SPPlay unseen = { 0, 0, 0, 0, 0 };
	SPCaptions never = SP_captions("on", "on", "off", "relative", "on", "off");
	CHECK(SP_captionFit(&unseen, now, &never, 1000, bytes, NULL, out, sizeof(out)) > 0);
	CHECK_STR(out, "Never played");
	unsetenv("TZ");
	tzset();
}

static void testCaptionPlaces(void) {
	// under the title pill, below the status pill: the whole width
	MosaicRect title = SP_title(&AREA, &STATUS, 3, 100);
	CHECK(rectIs(title, 30, 30, 172, 90));
	CHECK(rectIs(SP_titleCaption(&AREA, &STATUS, &title, 3, 40), 30, 135, 964, 60));
	// a taller small font sets the height
	CHECK(rectIs(SP_titleCaption(&AREA, &STATUS, &title, 3, 70), 30, 135, 964, 70));
	// level with header widgets, up to them
	MosaicRect deep = { 794, 30, 200, 150 };
	CHECK(rectIs(SP_titleCaption(&AREA, &deep, &title, 3, 40), 30, 135, 764, 60));
	CHECK(SP_titleCaption(&AREA, &STATUS, &NONE, 3, 40).w == 0);
	// no room above the hints
	MosaicRect small = { 0, 0, 200, 290 };
	MosaicRect small_title = SP_title(&small, &NONE, 3, 40);
	CHECK(small_title.w > 0);
	CHECK(SP_titleCaption(&small, &NONE, &small_title, 3, 40).w == 0);

	// the strip's line under the name pill
	SPStrip s;
	memset(&s, 0, sizeof(s));
	s.name = (MosaicRect){ 30, 470, 964, 145 };
	CHECK(rectIs(SP_stripCaption(&s, 90, 3, 40), 30, 575, 964, 40));
	CHECK(SP_stripCaption(&s, 90, 3, 41).w == 0);
	CHECK(SP_stripCaption(&s, 90, 3, 0).w == 0);
	s.name = NONE;
	CHECK(SP_stripCaption(&s, 90, 3, 40).w == 0);

	// along the bottom of the list's panel, the preview above it
	SPList l;
	CHECK(SP_list(&AREA, &STATUS, 3, &l) == 0);
	MosaicRect preview;
	CHECK(rectIs(SP_panelCaption(&l.panel, 3, 40, &preview), 439, 578, 555, 40));
	CHECK(rectIs(preview, 439, 150, 555, 413));
	// not when the preview would be left less than half
	MosaicRect low = { 0, 0, 100, 100 };
	CHECK(SP_panelCaption(&low, 3, 40, &preview).w == 0);
	CHECK(rectIs(preview, 0, 0, 100, 100));
	CHECK(SP_panelCaption(&l.panel, 3, 0, &preview).w == 0);
	CHECK(SP_panelCaption(&NONE, 3, 40, &preview).w == 0);
}

///////////////////////////////////////
// the fake host

struct MosaicSurface {
	int w, h;
	int owned;
	const MosaicSurface* from; // what it's a copy of
};

static MosaicSurface screen_surface = { 1024, 768, 0, NULL };
static MosaicSurface preview_apple = { 640, 480, 0, NULL };
static MosaicSurface preview_cherry = { 1024, 768, 0, NULL };

static MosaicHost fake_host;
static MosaicScreenHooks registered;
static char registered_id[64];
static int register_result = 0;
static char last_log[512];
static int logs = 0;
static int opened = -1, opened_resume = -1, open_result = 1;
static int removed = -1;
static int redraws = 0;
static int scales = 0, frees = 0, live = 0;
static int blits = 0, blits_into_hole = 0, fills_into_hole = 0;
static int art_state = MOSAIC_ART_READY;
static char loaded[512];
static MosaicRect hole_now; // what the view leaves clear, checked against every draw
static MosaicHint hints_left[2], hints_right[2];
static int hints_left_count = 0, hints_right_count = 0;
static char texts[16][256];
static int text_x[16], text_y[16], text_font[16];
static int text_count = 0;
static const char* style_setting = "full";
static const char* captions_setting = "off";
// playtime-captions' cfg, which only answers while it's loaded
static int captions_loaded = 0;
static const char* captions_cfg[8][2];
static int captions_cfg_count = 0;
static char logged[16][512];
static int logged_count = 0;

static void fakeLog(const char* id, const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
	if (logged_count < 16) snprintf(logged[logged_count++], sizeof(logged[0]), "%s", message);
	logs++;
}

static int wasLogged(const char* message) {
	for (int i = 0; i < logged_count; i++) {
		if (strcmp(logged[i], message) == 0) return 1;
	}
	return 0;
}

static const char* fakeCfgRead(const char* id, const char* key, const char* fallback) {
	if (strcmp(id, "playtime-captions") != 0 || !captions_loaded) return fallback;
	for (int i = captions_cfg_count - 1; i >= 0; i--) {
		if (strcmp(captions_cfg[i][0], key) == 0) return captions_cfg[i][1];
	}
	return fallback;
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (strcmp(key, "style") == 0) return style_setting;
	if (strcmp(key, "captions") == 0) return captions_setting;
	return fallback;
}

static void captionsCfg(const char* key, const char* value) {
	captions_cfg[captions_cfg_count][0] = key;
	captions_cfg[captions_cfg_count][1] = value;
	captions_cfg_count++;
}

// the play history: Apple played 2h 15m over two sessions three days ago,
// Banana never, and Cherry's history unknown; achievements for Apple and
// Cherry
static long long play_now = 0;
static int play_asks = 0;

static int playOf(const char* path) {
	play_asks++;
	if (strstr(path, "Apple")) return 0;
	if (strstr(path, "Banana")) return 1;
	return 2;
}

static long long fakePlayTime(const char* path) {
	static const long long t[] = { 8100, 0, -1 };
	return t[playOf(path)];
}

static long long fakeLastPlayed(const char* path) {
	int i = playOf(path);
	return i == 0 ? play_now - 3 * 86400 : i == 1 ? 0 : -1;
}

static int fakePlayCount(const char* path) {
	static const int c[] = { 2, 0, -1 };
	return c[playOf(path)];
}

static int fakeAchievements(const char* path, MosaicAchievements* out) {
	int i = playOf(path);
	if (i == 1) return 0;
	out->unlocked = i == 0 ? 1 : 2;
	out->total = i == 0 ? 3 : 5;
	return 1;
}

static void fakeRedraw(void) {
	redraws++;
}

static int fakeCount(void) {
	return fake_count;
}

static const char* fakePath(int index) {
	return modelPath(index, NULL);
}

static int fakeInfo(int index, MosaicRecent* out) {
	return modelInfo(index, out, NULL);
}

static int fakeRemove(int index) {
	if (index < 0 || index >= fake_count) return -1;
	removed = index;
	for (int i = index; i < fake_count - 1; i++) {
		fake_paths[i] = fake_paths[i + 1];
		fake_infos[i] = fake_infos[i + 1];
	}
	fake_count--;
	return 0;
}

static int fakeOpen(int index, int resume) {
	opened = index;
	opened_resume = resume;
	return open_result;
}

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(registered_id, sizeof(registered_id), "%s", id);
	registered = *hooks;
	return register_result;
}

static int fakeThemeColor(int index) {
	return 0x101010 * index;
}

static int fakeScreenWidth(void) {
	return 1024;
}
static int fakeScreenHeight(void) {
	return 768;
}
static int fakeScale(void) {
	return 3;
}

static MosaicSurface* fakeScaleSurface(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)fit;
	scales++;
	live++;
	MosaicSurface* s = calloc(1, sizeof(*s));
	s->w = w;
	s->h = h;
	s->owned = 1;
	s->from = src;
	return s;
}

static int fakeFree(const char* id, MosaicSurface* s) {
	if (!s || !s->owned) return -1;
	frees++;
	live--;
	free(s);
	return 0;
}

static int fakeSize(const MosaicSurface* s, int* w, int* h) {
	if (!s) return -1;
	*w = s->w;
	*h = s->h;
	return 0;
}

static int meetsHole(const MosaicRect* r) {
	return r && hole_now.w > 0 && SP_meet(r, &hole_now).w > 0;
}

static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)target;
	(void)color;
	if (!rect || meetsHole(rect)) fills_into_hole++;
	return 0;
}

static int fakeFillRound(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	(void)radius;
	return fakeFill(target, rect, color);
}

static int fakeBlit(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst) {
	(void)target;
	(void)src;
	(void)src_rect;
	blits++;
	if (!dst || meetsHole(dst)) blits_into_hole++;
	return 0;
}

static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return 20 * (int)strlen(text);
}

static int fakeTextHeight(int font) {
	(void)font;
	return 40;
}

static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	snprintf(out, size, "%s", text);
	int w = fakeTextWidth(font, out);
	return w < max_width ? w : max_width;
}

static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)target;
	(void)color;
	if (text_count < 16) {
		text_x[text_count] = x;
		text_y[text_count] = y;
		text_font[text_count] = font;
		snprintf(texts[text_count++], sizeof(texts[0]), "%s", text);
	}
	int w = fakeTextWidth(font, text);
	return w < max_width ? w : max_width;
}

static int fakeArtLoad(const char* path, const MosaicSurface** out) {
	snprintf(loaded, sizeof(loaded), "%s", path);
	if (art_state != MOSAIC_ART_READY) return art_state;
	*out = strstr(path, "Apple") ? &preview_apple : &preview_cherry;
	return MOSAIC_ART_READY;
}

static int fakeArtGet(const MosaicEntry* entry, int kind, const MosaicSurface** out) {
	(void)entry;
	(void)kind;
	(void)out;
	return MOSAIC_ART_MISSING;
}

static int fakeDrawHints(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	(void)screen;
	(void)drawn;
	MosaicHint* to = group == MOSAIC_HINTS_LEFT ? hints_left : hints_right;
	for (int i = 0; i < count && i < 2; i++) to[i] = hints[i];
	if (group == MOSAIC_HINTS_LEFT) hints_left_count = count;
	else hints_right_count = count;
	return 0;
}

static const char* filtered_name = ""; // is_filtered hides the path with it in
static int fakeFiltered(const char* path) { return filtered_name[0] && strstr(path, filtered_name) != NULL; }
static int fakeNoSets(void (*each)(const char*, void*), void* data) {
	(void)each;
	(void)data;
	return 0;
}
static void (*set_changed_fn)(const char*, const char*, void*);
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	if (!strcmp(name, "launcher.set_changed")) set_changed_fn = on_event;
	return 0;
}

static void setupHost(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.cfg_read = fakeCfgRead;
	fake_host.request_redraw = fakeRedraw;
	fake_host.recents_count = fakeCount;
	fake_host.recent_path = fakePath;
	fake_host.recent_remove = fakeRemove;
	fake_host.recent_info = fakeInfo;
	fake_host.recent_open = fakeOpen;
	fake_host.register_screen = fakeRegister;
	fake_host.theme_color = fakeThemeColor;
	fake_host.screen_width = fakeScreenWidth;
	fake_host.screen_height = fakeScreenHeight;
	fake_host.screen_scale = fakeScale;
	fake_host.surface_scale = fakeScaleSurface;
	fake_host.surface_free = fakeFree;
	fake_host.surface_size = fakeSize;
	fake_host.fill_rect = fakeFill;
	fake_host.fill_round_rect = fakeFillRound;
	fake_host.blit = fakeBlit;
	fake_host.text_width = fakeTextWidth;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.art_get = fakeArtGet;
	fake_host.art_load = fakeArtLoad;
	fake_host.draw_hints = fakeDrawHints;
	fake_host.play_time = fakePlayTime;
	fake_host.last_played = fakeLastPlayed;
	fake_host.play_count = fakePlayCount;
	fake_host.achievements = fakeAchievements;
	fake_host.is_filtered = fakeFiltered;
	fake_host.set_names = fakeNoSets;
	fake_host.subscribe = fakeSubscribe;
}

static const MosaicPlugin* start(const char* setting) {
	style_setting = setting;
	setupHost();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	return p;
}

static MosaicScreenView viewOf(void) {
	MosaicScreenView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.area = AREA;
	v.status = STATUS;
	v.screen = "switcher-plus.switcher";
	hole_now = STATUS;
	return v;
}

static int draw(void) {
	MosaicScreenView v = viewOf();
	text_count = 0;
	hints_left_count = hints_right_count = 0;
	return registered.render(&screen_surface, &v, registered.data);
}

static int press(int button) {
	MosaicInput in = { sizeof(MosaicInput), MOSAIC_BUTTON_BIT(button), MOSAIC_BUTTON_BIT(button), 0,
		MOSAIC_BUTTON_BIT(button) };
	return registered.input(&in, registered.data);
}

static int hold(int button) {
	MosaicInput in = { sizeof(MosaicInput), 0, MOSAIC_BUTTON_BIT(button), 0, MOSAIC_BUTTON_BIT(button) };
	return registered.input(&in, registered.data);
}

static int release(int button) {
	MosaicInput in = { sizeof(MosaicInput), 0, 0, MOSAIC_BUTTON_BIT(button), 0 };
	return registered.input(&in, registered.data);
}

static int tap(int button) {
	int rc = press(button);
	if (rc != MOSAIC_SCREEN_STAY) return rc;
	return release(button);
}

static void testInit(void) {
	start("full");
	CHECK_STR(registered_id, "switcher-plus.switcher");
	CHECK(registered.flags == MOSAIC_SCREEN_GAME_SWITCHER);
	CHECK(registered.enter && registered.exit && registered.input && registered.render);
	CHECK_STR(last_log, "style full");
	start("list");
	CHECK_STR(last_log, "style list");

	// an older launcher
	setupHost();
	fake_host.size = offsetof(MosaicHost, draw_hints);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) != 0);
	setupHost();
	register_result = -1;
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) != 0);
	register_result = 0;
}

static void testButtons(void) {
	start("full");
	threeRecents();
	CHECK(registered.enter(registered.data) == 0);

	// a release of a press from before it opened does nothing
	opened = -1;
	CHECK(release(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK(opened == -1);
	CHECK(release(MOSAIC_BUTTON_SELECT) == MOSAIC_SCREEN_STAY);

	// stock's left and right wrap, on presses only
	redraws = 0;
	CHECK(press(MOSAIC_BUTTON_LEFT) == MOSAIC_SCREEN_STAY);
	CHECK(selected == 2 && redraws == 1);
	CHECK(hold(MOSAIC_BUTTON_LEFT) == MOSAIC_SCREEN_STAY);
	CHECK(selected == 2);
	CHECK(press(MOSAIC_BUTTON_RIGHT) == MOSAIC_SCREEN_STAY);
	CHECK(selected == 0);
	CHECK(press(MOSAIC_BUTTON_DOWN) == MOSAIC_SCREEN_STAY);
	CHECK(selected == 0);

	// A opens it on its release, to resume
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK(opened == -1);
	CHECK(release(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK(opened == 0 && opened_resume == 1);
	CHECK_STR(last_log, "opening 0: Apple");
	open_result = 0;
	press(MOSAIC_BUTTON_RIGHT);
	tap(MOSAIC_BUTTON_A);
	CHECK(opened == 1);
	CHECK_STR(last_log, "can't open 1: Banana");
	open_result = 1;

	// Y removes it, and the selection goes to the one before
	CHECK(tap(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_STAY);
	CHECK(removed == 1 && fake_count == 2 && selected == 0);
	CHECK_STR(last_log, "removed 1, 2 left");
	CHECK(tap(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_STAY);
	CHECK(removed == 0 && fake_count == 1 && selected == 0);
	CHECK_STR(fake_paths[0], "/sd/Roms/GB/Cherry.gb");
	tap(MOSAIC_BUTTON_Y);
	CHECK(fake_count == 0 && selected == 0);
	// nothing to open or remove
	opened = removed = -1;
	tap(MOSAIC_BUTTON_A);
	tap(MOSAIC_BUTTON_Y);
	CHECK(opened == -1 && removed == -1);

	// B closes it, and so does a quick tap of SELECT, but not a long one
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	const MosaicPlugin* p = start("full");
	threeRecents();
	registered.enter(registered.data);
	p->tick(1000);
	press(MOSAIC_BUTTON_SELECT);
	p->tick(1400);
	CHECK(release(MOSAIC_BUTTON_SELECT) == MOSAIC_SCREEN_STAY);
	press(MOSAIC_BUTTON_SELECT);
	p->tick(1500);
	CHECK(release(MOSAIC_BUTTON_SELECT) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
}

// A recent a filter hides isn't shown, and A and Y act on the one that is.
static void testFiltered(void) {
	start("full");
	threeRecents();
	filtered_name = "Banana";
	CHECK(set_changed_fn != NULL);
	CHECK(registered.enter(registered.data) == 0);
	CHECK(recents.count == 2);
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(selected == 1);
	opened = -1;
	tap(MOSAIC_BUTTON_A);
	CHECK(opened == 2);
	CHECK_STR(last_log, "opening 1: Cherry");
	removed = -1;
	tap(MOSAIC_BUTTON_Y);
	CHECK(removed == 2 && fake_count == 2);
	CHECK(recents.count == 1 && selected == 0);

	// a set's change asks again
	filtered_name = "";
	redraws = 0;
	set_changed_fn("launcher.set_changed", "name=hidden", NULL);
	CHECK(redraws == 1);
	draw();
	CHECK(recents.count == 2);
	registered.exit(registered.data);
	fake_count = 0;
}

static void testStyleMoves(void) {
	start("strip");
	threeRecents();
	registered.enter(registered.data);
	// held, it stops at the end; pressed, it wraps
	CHECK(hold(MOSAIC_BUTTON_RIGHT) == MOSAIC_SCREEN_STAY && selected == 1);
	hold(MOSAIC_BUTTON_RIGHT);
	hold(MOSAIC_BUTTON_RIGHT);
	CHECK(selected == 2);
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(selected == 0);
	press(MOSAIC_BUTTON_UP);
	CHECK(selected == 0);
	registered.exit(registered.data);

	start("list");
	fake_count = 10;
	for (int i = 3; i < 10; i++) fakeRecent(i, "/sd/Roms/GB/More.gb", "More", 0, NULL);
	threeRecents();
	fake_count = 10;
	registered.enter(registered.data);
	CHECK(draw() == 0); // works out the page
	press(MOSAIC_BUTTON_DOWN);
	CHECK(selected == 1);
	press(MOSAIC_BUTTON_UP);
	press(MOSAIC_BUTTON_UP);
	CHECK(selected == 9);
	press(MOSAIC_BUTTON_LEFT);
	CHECK(selected == 3); // a page of 6 rows
	hold(MOSAIC_BUTTON_LEFT);
	CHECK(selected == 0);
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(selected == 6);
	CHECK(draw() == 0);
	CHECK(first == 1);
	registered.exit(registered.data);
	fake_count = 0;
}

static void testRender(void) {
	start("full");
	threeRecents();
	registered.enter(registered.data);
	info_asks = 0;
	scales = frees = live = 0;
	blits = blits_into_hole = fills_into_hole = 0;

	// the selected preview, fitted to the screen, around the status pill
	CHECK(draw() == 0);
	CHECK_STR(loaded, "/sd/.minui/GB/Apple.gb.0.bmp");
	CHECK(scales == 1 && live == 1); // 640x480 to the screen's 1024x768
	CHECK(blits == 4 && blits_into_hole == 0 && fills_into_hole == 0);
	CHECK_STR(texts[0], "Apple");
	CHECK(hints_left_count == 1 && hints_left[0].button == MOSAIC_BUTTON_B);
	CHECK(hints_right_count == 2 && hints_right[0].button == MOSAIC_BUTTON_Y && hints_right[1].button == MOSAIC_BUTTON_A);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview drawn, resume, style full");
	// drawn again: no new copy, no new ask, no new log
	int before = logs;
	CHECK(draw() == 0);
	CHECK(scales == 1 && logs == before);

	// no preview: stock's message and A OPEN alone
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK_STR(texts[0], "No Preview");
	CHECK_STR(texts[1], "Banana");
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_A);
	CHECK_STR(last_log, "showing 1 of 3: Banana, preview none, open, style full");
	// a screen-sized preview isn't copied
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK(scales == 1);
	CHECK(blits_into_hole == 0 && fills_into_hole == 0);
	CHECK(info_asks == 3);

	// still loading
	art_state = MOSAIC_ART_LOADING;
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview loading, resume, style full");
	art_state = MOSAIC_ART_MISSING;
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview none, resume, style full");
	art_state = MOSAIC_ART_READY;

	// recents that change are asked again
	fake_paths[1] = "/sd/Roms/GB/Date.gb";
	snprintf(fake_infos[1].name, sizeof(fake_infos[1].name), "Date");
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK_STR(texts[1], "Date");
	CHECK(info_asks == 4);
	// and each time it opens
	registered.exit(registered.data);
	CHECK(live == 0);
	registered.enter(registered.data);
	CHECK(draw() == 0);
	CHECK(info_asks == 5);

	// no recents
	fake_count = 0;
	CHECK(draw() == 0);
	CHECK_STR(texts[0], "No Recents");
	CHECK(hints_left_count == 0 && hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(last_log, "showing no recents, style full");
	registered.exit(registered.data);

	// strip: the selected one and one either side, wrapping no further
	start("strip");
	threeRecents();
	registered.enter(registered.data);
	blits_into_hole = fills_into_hole = 0;
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 1 of 3: Banana, preview none, open, style strip");
	CHECK(blits == 0 || blits_into_hole == 0);
	CHECK(fills_into_hole == 0);
	CHECK(live == 2); // Apple's and Cherry's at their cards' size
	int t = 0;
	for (int i = 0; i < text_count; i++) t += strcmp(texts[i], "No Preview") == 0;
	CHECK(t == 1);
	CHECK_STR(texts[text_count - 1], "Banana");
	registered.exit(registered.data);
	CHECK(live == 0);

	// list: a row for each, the selected one's preview in the panel
	start("list");
	threeRecents();
	registered.enter(registered.data);
	blits_into_hole = fills_into_hole = 0;
	CHECK(draw() == 0);
	CHECK(text_count == 3);
	CHECK_STR(texts[0], "Apple");
	CHECK_STR(texts[1], "Banana");
	CHECK_STR(texts[2], "Cherry");
	CHECK(live == 1 && blits_into_hole == 0 && fills_into_hole == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview drawn, resume, style list");
	// a recent whose info can't be read shows its file's name
	info_fails = 1;
	registered.exit(registered.data);
	registered.enter(registered.data);
	CHECK(draw() == 0);
	CHECK_STR(texts[1], "Banana.gb");
	info_fails = -1;
	registered.exit(registered.data);

	// too small for a strip: drawn full
	start("strip");
	threeRecents();
	registered.enter(registered.data);
	MosaicScreenView v = viewOf();
	v.area = (MosaicRect){ 0, 0, 200, 290 };
	v.status = NONE;
	hole_now = NONE;
	CHECK(registered.render(&screen_surface, &v, registered.data) == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview drawn, resume, style strip (too small, drawn full)");
	registered.exit(registered.data);
	fake_count = 0;
}

static int findText(const char* text) {
	for (int i = 0; i < text_count; i++) {
		if (strcmp(texts[i], text) == 0) return i;
	}
	return -1;
}

static void testCaptions(void) {
	setenv("TZ", "UTC0", 1);
	tzset();
	play_now = (long long)time(NULL);
	const char* caption = "2h 15m \xC2\xB7 3 days ago";

	// off by default: nothing asked, nothing drawn
	captions_setting = "off";
	start("full");
	threeRecents();
	registered.enter(registered.data);
	play_asks = 0;
	CHECK(draw() == 0);
	CHECK(play_asks == 0);
	CHECK(findText(caption) < 0);
	registered.exit(registered.data);

	// full: on a pill under the title, in playtime-captions' defaults while
	// it isn't loaded
	captions_setting = "on";
	logged_count = 0;
	start("full");
	CHECK_STR(last_log, "style full, captions on");
	threeRecents();
	registered.enter(registered.data);
	CHECK(wasLogged("captions time on, last on, sessions off, format relative, never off, achievements off"));
	fills_into_hole = blits_into_hole = 0;
	CHECK(draw() == 0);
	int i = findText(caption);
	CHECK(i >= 0 && text_font[i] == MOSAIC_FONT_SMALL && text_x[i] == 66 && text_y[i] == 145);
	CHECK(fills_into_hole == 0 && blits_into_hole == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview drawn, resume, style full, caption 2h 15m \xC2\xB7 3 days ago");
	// never played and unknown history: none
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 1 of 3: Banana, preview none, open, style full, no caption");
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 2 of 3: Cherry, preview drawn, resume, style full, no caption");
	registered.exit(registered.data);

	// playtime-captions' settings while it's loaded, read when it opens
	captions_loaded = 1;
	captions_cfg_count = 0;
	captionsCfg("format", "compact");
	captionsCfg("sessions", "on");
	captionsCfg("never", "on");
	logged_count = 0;
	registered.enter(registered.data);
	CHECK(wasLogged("captions time on, last on, sessions on, format compact, never on, achievements off"));
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview drawn, resume, style full, caption 2h \xC2\xB7 3d \xC2\xB7 2x");
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 1 of 3: Banana, preview none, open, style full, caption Never played");
	registered.exit(registered.data);
	// the same settings aren't logged again
	logged_count = 0;
	registered.enter(registered.data);
	CHECK(!wasLogged("captions time on, last on, sessions on, format compact, never on, achievements off"));
	registered.exit(registered.data);

	// achievements last, also for a game with unknown history
	captions_cfg_count = 0;
	captionsCfg("achievements", "on");
	registered.enter(registered.data);
	CHECK(draw() == 0);
	CHECK_STR(last_log,
		"showing 0 of 3: Apple, preview drawn, resume, style full, caption 2h 15m \xC2\xB7 3 days ago \xC2\xB7 1/3 achievements");
	press(MOSAIC_BUTTON_LEFT);
	CHECK(draw() == 0);
	CHECK_STR(last_log, "showing 2 of 3: Cherry, preview drawn, resume, style full, caption 2/5 achievements");
	registered.exit(registered.data);
	captions_cfg_count = 0;
	captions_loaded = 0;

	// strip: centred under the name pill
	start("strip");
	threeRecents();
	registered.enter(registered.data);
	fills_into_hole = 0;
	CHECK(draw() == 0);
	i = findText(caption);
	CHECK(i >= 0 && text_font[i] == MOSAIC_FONT_SMALL && text_x[i] == 312 && text_y[i] == 575);
	CHECK(fills_into_hole == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview drawn, resume, style strip, caption 2h 15m \xC2\xB7 3 days ago");
	registered.exit(registered.data);

	// list: the selected recent's, centred under its preview in the panel
	start("list");
	threeRecents();
	registered.enter(registered.data);
	blits_into_hole = fills_into_hole = 0;
	CHECK(draw() == 0);
	i = findText(caption);
	CHECK(i >= 0 && text_font[i] == MOSAIC_FONT_SMALL && text_x[i] == 516 && text_y[i] == 578);
	CHECK(text_count == 4); // the three names and Apple's caption
	CHECK(blits_into_hole == 0 && fills_into_hole == 0);
	CHECK_STR(last_log, "showing 0 of 3: Apple, preview drawn, resume, style list, caption 2h 15m \xC2\xB7 3 days ago");
	press(MOSAIC_BUTTON_DOWN);
	CHECK(draw() == 0);
	CHECK(text_count == 4); // the names and "No Preview", but no caption for Banana
	CHECK_STR(last_log, "showing 1 of 3: Banana, preview none, open, style list, no caption");
	registered.exit(registered.data);

	captions_setting = "off";
	fake_count = 0;
	unsetenv("TZ");
	tzset();
}

int main(void) {
	testSettings();
	testMoves();
	testHints();
	testRecents();
	testGeometry();
	testStrip();
	testList();
	testCaptionsCopy();
	testCaptionPlaces();
	testInit();
	testButtons();
	testFiltered();
	testStyleMoves();
	testRender();
	testCaptions();
	CHECK_DONE();
}
