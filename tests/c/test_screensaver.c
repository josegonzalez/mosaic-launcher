// The screensaver plugin: its model on its own, then the plugin against a
// fake host, for when it opens and closes, the buttons it takes, how it
// cycles through a folder's images, how many copies it holds, what music
// plays, the videos source's clips, the random source and the logo and
// clock styles.

#include <stdlib.h>
#include <unistd.h>
#include "check.h"
#include "fakeloaded.h"
#include "fakeproviders.h"
#include "fakesets.h"
#include "words.h"
#include "../../plugins/screensaver/screensaver.c"

#define BIT MOSAIC_BUTTON_BIT
static const unsigned int POWER = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);

static void testSettings(void) {
	CHECK(SS_parseIdle("30") == 30000);
	CHECK(SS_parseIdle(" 60 ") == 60000);
	CHECK(SS_parseIdle("1") == 1000);
	CHECK(SS_parseIdle("3600") == 3600000);
	CHECK(SS_parseIdle("off") == 0);
	CHECK(SS_parseIdle("0") == 0);
	CHECK(SS_parseIdle("3601") == SS_IDLE_DEFAULT_MS);
	CHECK(SS_parseIdle("-5") == SS_IDLE_DEFAULT_MS);
	CHECK(SS_parseIdle("1m") == SS_IDLE_DEFAULT_MS);
	CHECK(SS_parseIdle("") == SS_IDLE_DEFAULT_MS);
	CHECK(SS_parseIdle(NULL) == SS_IDLE_DEFAULT_MS);

	CHECK(SS_parseInterval("5") == 5000);
	CHECK(SS_parseInterval("1") == 1000);
	CHECK(SS_parseInterval("0") == SS_INTERVAL_DEFAULT_MS);
	CHECK(SS_parseInterval("off") == SS_INTERVAL_DEFAULT_MS);
	CHECK(SS_parseInterval("x") == SS_INTERVAL_DEFAULT_MS);
	CHECK(SS_parseInterval(NULL) == SS_INTERVAL_DEFAULT_MS);

	CHECK(SS_changeMs(10000) == 1000);
	CHECK(SS_changeMs(1000) == 500);
	CHECK(SS_changeMs(2000) == 1000);

	CHECK(SS_parseSource("games") == SS_SOURCE_GAMES);
	CHECK(SS_parseSource("screenshots") == SS_SOURCE_SCREENSHOTS);
	CHECK(SS_parseSource("folder") == SS_SOURCE_FOLDER);
	CHECK(SS_parseSource("videos") == SS_SOURCE_VIDEOS);
	CHECK_STR(SS_sourceName(SS_SOURCE_VIDEOS), "videos");
	CHECK(SS_parseSource("web") == SS_SOURCE_GAMES);
	CHECK(SS_parseSource(NULL) == SS_SOURCE_GAMES);
	CHECK_STR(SS_sourceName(SS_SOURCE_FOLDER), "folder");

	CHECK(SS_parseArt("screenshot") == SS_ART_SCREENSHOT);
	CHECK(SS_parseArt("box") == SS_ART_BOX);
	CHECK(SS_parseArt(NULL) == SS_ART_BOX);
	CHECK(SS_parseEffect("slide") == SS_EFFECT_SLIDE);
	CHECK(SS_parseEffect("fade") == SS_EFFECT_FADE);
	CHECK(SS_parseEffect("spin") == SS_EFFECT_FADE);
	CHECK(SS_parseClock("24h") == SS_CLOCK_24H);
	CHECK(SS_parseClock("12h") == SS_CLOCK_12H);
	CHECK(SS_parseClock("off") == SS_CLOCK_OFF);
	CHECK(SS_parseClock("analog") == SS_CLOCK_OFF);
	CHECK(SS_parseClock(NULL) == SS_CLOCK_OFF);

	CHECK(SS_parseSource("random") == SS_SOURCE_RANDOM);
	CHECK_STR(SS_sourceName(SS_SOURCE_RANDOM), "random");
	CHECK(SS_parseStyle("images") == SS_STYLE_IMAGES);
	CHECK(SS_parseStyle("logo") == SS_STYLE_LOGO);
	CHECK(SS_parseStyle("clock") == SS_STYLE_CLOCK);
	CHECK(SS_parseStyle("spin") == SS_STYLE_IMAGES);
	CHECK(SS_parseStyle(NULL) == SS_STYLE_IMAGES);
	CHECK_STR(SS_styleName(SS_STYLE_LOGO), "logo");
	CHECK_STR(SS_styleName(SS_STYLE_CLOCK), "clock");
	CHECK_STR(SS_styleName(SS_STYLE_IMAGES), "images");
	CHECK(SS_parseFace("over") == SS_FACE_OVER);
	CHECK(SS_parseFace("full") == SS_FACE_FULL);
	CHECK(SS_parseFace("x") == SS_FACE_FULL);
	CHECK(SS_parseFace(NULL) == SS_FACE_FULL);
	CHECK_STR(SS_faceName(SS_FACE_OVER), "over");
	CHECK_STR(SS_faceName(SS_FACE_FULL), "full");

	char out[64];
	SS_formatClock(9, 5, SS_CLOCK_24H, out, sizeof(out));
	CHECK_STR(out, "09:05");
	SS_formatClock(0, 0, SS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "12:00 AM");
	// in the words it's given, then English again
	static const char* const words[] = { "AM", "a. m.", NULL };
	fake_words = words;
	SSWords set = { fakeTr, fakePlural };
	SS_setWords(&set);
	SS_formatClock(0, 0, SS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "12:00 a. m.");
	SS_setWords(NULL);
	SS_formatClock(0, 0, SS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "12:00 AM");
	SS_formatClock(21, 7, SS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "9:07 PM");
	SS_formatClock(9, 5, SS_CLOCK_OFF, out, sizeof(out));
	CHECK_STR(out, "");
	SS_formatClock(24, 0, SS_CLOCK_24H, out, sizeof(out));
	CHECK_STR(out, "");

	CHECK(SS_folderPath("/sd", "Screensaver", out, sizeof(out)) == 0);
	CHECK_STR(out, "/sd/Screensaver");
	CHECK(SS_folderPath("/sd", "/Pictures/Mine/", out, sizeof(out)) == 0);
	CHECK_STR(out, "/sd/Pictures/Mine");
	CHECK(SS_folderPath("/sd", "", out, sizeof(out)) == 0);
	CHECK_STR(out, "/sd/Screensaver");
	CHECK(SS_folderPath("/sd", NULL, out, sizeof(out)) == 0);
	CHECK_STR(out, "/sd/Screensaver");
	CHECK(SS_folderPath("/sd", "a/../../etc", out, sizeof(out)) == -1);
	CHECK(SS_folderPath("/sd", "..", out, sizeof(out)) == -1);
	CHECK(SS_folderPath("/sd", "./x", out, sizeof(out)) == -1);
	CHECK(SS_folderPath("/sd", "a\\b", out, sizeof(out)) == -1);
	CHECK(SS_folderPath("/sd", "..hidden", out, sizeof(out)) == 0); // a name, not a part
	char tiny[8];
	CHECK(SS_folderPath("/sd", "Screensaver", tiny, sizeof(tiny)) == -1);
	CHECK_STR(tiny, "");
}

static void testIdle(void) {
	CHECK(!SS_wants(29999, 30000));
	CHECK(SS_wants(30000, 30000));
	CHECK(SS_wants(1000000, 30000));
	CHECK(!SS_wants(1000000, 0)); // off

	MosaicHost h = { .size = sizeof(MosaicHost), .plugin_loaded = fakePluginLoaded };
	fake_loaded = "favorites,charging-screen";
	CHECK(SS_pluginLoaded(&h, "favorites"));
	CHECK(!SS_pluginLoaded(&h, "favorites-plus"));
	CHECK(!SS_pluginLoaded(&h, NULL));
	CHECK(!SS_pluginLoaded(NULL, "favorites"));
	// an older launcher's table, without plugin_loaded
	h.size = FAKE_HOST_BEFORE_LOADED;
	CHECK(!SS_pluginLoaded(&h, "favorites"));
	fake_loaded = "";

	SsFilter f = {0};
	int shut = 0, toggled = 0;
	unsigned int a = BIT(MOSAIC_BUTTON_A), x = BIT(MOSAIC_BUTTON_X);
	// closed: nothing taken
	CHECK(SsFilter_frame(&f, 0, a, a, POWER, 0, &shut, &toggled) == 0 && !shut);
	// open: a press closes it, and everything but power is taken until let go
	CHECK(SsFilter_frame(&f, 1, a, a, POWER, 0, &shut, &toggled) == ~POWER && shut && !toggled);
	CHECK(SsFilter_frame(&f, 0, 0, a, POWER, 0, &shut, &toggled) == ~POWER && !shut);
	CHECK(SsFilter_frame(&f, 0, 0, 0, POWER, 0, &shut, &toggled) == ~POWER); // the release frame
	CHECK(SsFilter_frame(&f, 0, 0, 0, POWER, 0, &shut, &toggled) == 0);
	// power alone doesn't close it
	CHECK(SsFilter_frame(&f, 1, POWER, POWER, POWER, 0, &shut, &toggled) == 0 && !shut);
	// X without music's toggle closes it too
	CHECK(SsFilter_frame(&f, 1, x, x, POWER, 0, &shut, &toggled) == ~POWER && shut && !toggled);
	CHECK(SsFilter_frame(&f, 1, 0, 0, POWER, 0, &shut, &toggled) == ~POWER);
	// with it, X alone plays or pauses, taken until let go, and stays open
	CHECK(SsFilter_frame(&f, 1, x, x, POWER, x, &shut, &toggled) == ~POWER && toggled && !shut);
	CHECK(SsFilter_frame(&f, 1, 0, x, POWER, x, &shut, &toggled) == ~POWER && !toggled && !shut);
	CHECK(SsFilter_frame(&f, 1, 0, 0, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(SsFilter_frame(&f, 1, 0, 0, POWER, x, &shut, &toggled) == 0);
	// X with another button, or another button, still closes it
	CHECK(SsFilter_frame(&f, 1, x | a, x | a, POWER, x, &shut, &toggled) == ~POWER && shut && !toggled);
	CHECK(SsFilter_frame(&f, 0, 0, 0, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(SsFilter_frame(&f, 1, a, a, POWER, x, &shut, &toggled) == ~POWER && shut && !toggled);
	CHECK(SsFilter_frame(&f, 0, 0, 0, POWER, x, &shut, &toggled) == ~POWER);

	// A alone launches the random source's game, power aside
	CHECK(SS_pressedAlone(a, POWER, a));
	CHECK(SS_pressedAlone(a | POWER, POWER, a));
	CHECK(!SS_pressedAlone(a | x, POWER, a));
	CHECK(!SS_pressedAlone(x, POWER, a));
	CHECK(!SS_pressedAlone(0, POWER, 0));
}

static void touch(const char* path) {
	FILE* fh = fopen(path, "w");
	if (fh) fclose(fh);
}

static void testPool(void) {
	static SsPool pool;
	SS_poolClear(&pool);
	CHECK(SS_poolAdd(&pool, "/a.png"));
	CHECK(!SS_poolAdd(&pool, "/a.png"));
	CHECK(!SS_poolAdd(&pool, ""));
	CHECK(!SS_poolAdd(&pool, NULL));
	CHECK(SS_poolAdd(&pool, "/b.png"));
	CHECK(pool.count == 2);
	CHECK(SS_poolFind(&pool, "/b.png") == 1);
	CHECK(SS_poolFind(&pool, "/c.png") == -1);
	for (int i = 0; i < SS_POOL_MAX + 10; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/%d.png", i);
		SS_poolAdd(&pool, path);
	}
	CHECK(pool.count == SS_POOL_MAX);

	// names, and a name in place of an image
	SS_poolClear(&pool);
	CHECK(SS_poolAddNamed(&pool, "/R/.media/wheel/Tetris.png", "/R/Tetris.gb", "Tetris", 0));
	CHECK(SS_poolAddNamed(&pool, "/R/Zelda.nes", "/R/Zelda.nes", "Zelda", 1));
	CHECK(!SS_poolAddNamed(&pool, "/R/Zelda.nes", "/R/Zelda.nes", "Zelda", 1));
	CHECK(pool.count == 2 && !pool.text[0] && pool.text[1]);
	CHECK_STR(pool.names[0], "Tetris");
	CHECK_STR(pool.games[1], "/R/Zelda.nes");
	SS_poolAdd(&pool, "/c.png");
	CHECK_STR(pool.names[2], "");
	CHECK(!pool.text[2]);
	// the random source's two places
	SS_poolClear(&pool);
	CHECK(SS_poolPut(&pool, 1, "/b.png", "/b.gb", "B", 0));
	CHECK(pool.count == 2);
	CHECK(SS_poolPut(&pool, 0, "/a.gb", "/a.gb", "A", 1));
	CHECK(pool.count == 2 && pool.text[0] && !pool.text[1]);
	pool.bad[1] = 1;
	CHECK(SS_poolPut(&pool, 1, "/c.png", "/c.gb", "C", 0));
	CHECK(!pool.bad[1]);
	CHECK_STR(pool.paths[1], "/c.png");
	CHECK(!SS_poolPut(&pool, -1, "/x", NULL, NULL, 0));
	CHECK(!SS_poolPut(&pool, SS_POOL_MAX, "/x", NULL, NULL, 0));
	char longname[SS_NAME_MAX + 20];
	memset(longname, 'n', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = '\0';
	CHECK(SS_poolPut(&pool, 0, "/x", "/x", longname, 1));
	CHECK(strlen(pool.names[0]) == SS_NAME_MAX - 1);

	char name[64], media[128];
	SS_gameName("/R/GB/Tetris (World).gb", 0, name, sizeof(name));
	CHECK_STR(name, "Tetris (World)");
	SS_gameName("/R/PS/Final Fantasy VII", 1, name, sizeof(name));
	CHECK_STR(name, "Final Fantasy VII");
	SS_gameName("/R/PS/Disc.Game", 1, name, sizeof(name));
	CHECK_STR(name, "Disc.Game");
	SS_gameName("/R/GB/.gb", 0, name, sizeof(name));
	CHECK_STR(name, ".gb");
	CHECK(SS_mediaPath("/R/GB/Tetris.gb", 0, "wheel", 0, media, sizeof(media)) == 0);
	CHECK_STR(media, "/R/GB/.media/wheel/Tetris.png");
	CHECK(SS_mediaPath("/R/GB/Tetris.gb", 0, "wheel", 2, media, sizeof(media)) == 0);
	CHECK_STR(media, "/R/GB/.media/wheel/Tetris.jpeg");
	CHECK(SS_mediaPath("/R/PS/FF7", 1, "wheel", 1, media, sizeof(media)) == 0);
	CHECK_STR(media, "/R/PS/.media/wheel/FF7.jpg");
	CHECK(SS_mediaPath("/R/GB/Tetris.gb", 0, "", 0, media, sizeof(media)) == 0);
	CHECK_STR(media, "/R/GB/.media/Tetris.png");
	CHECK(SS_mediaPath("/R/GB/Tetris.gb", 0, "wheel", 3, media, sizeof(media)) == -1);
	CHECK(SS_mediaPath("Tetris.gb", 0, "wheel", 0, media, sizeof(media)) == -1);
	CHECK(SS_mediaPath(NULL, 0, "wheel", 0, media, sizeof(media)) == -1);
	CHECK(SS_mediaPath("/R/GB/Tetris.gb", 0, "wheel", 0, media, 12) == -1);
	CHECK_STR(media, "");

	CHECK_STR(SS_baseName("/x/y/z.png"), "z.png");
	CHECK_STR(SS_baseName("z.png"), "z.png");
	CHECK(SS_isImage("a.png"));
	CHECK(SS_isImage("A.JPG"));
	CHECK(SS_isImage("a.jpeg"));
	CHECK(SS_isImage("a.bmp"));
	CHECK(!SS_isImage("a.gif"));
	CHECK(!SS_isImage(".png"));
	CHECK(!SS_isImage("._a.png"));
	CHECK(!SS_isImage("a"));
	CHECK(!SS_isImage(""));

	char dir[] = "/tmp/screensaver-test-XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	char path[256];
	const char* files[] = { "c.png", "a.jpg", "b.PNG", "._b.png", "notes.txt" };
	for (int i = 0; i < 5; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
		touch(path);
	}
	snprintf(path, sizeof(path), "%s/d.png", dir);
	mkdir(path, 0755); // a folder named like an image
	char linked[300];
	snprintf(path, sizeof(path), "%s/c.png", dir);
	snprintf(linked, sizeof(linked), "%s/e.png", dir);
	CHECK(symlink(path, linked) == 0); // a link to an image counts
	SS_poolClear(&pool);
	CHECK(SS_scanFolder(dir, &pool) == 4);
	CHECK(pool.count == 4);
	CHECK_STR(SS_baseName(pool.paths[0]), "a.jpg");
	CHECK_STR(SS_baseName(pool.paths[1]), "b.PNG");
	CHECK_STR(SS_baseName(pool.paths[2]), "c.png");
	CHECK_STR(SS_baseName(pool.paths[3]), "e.png");
	CHECK(SS_scanFolder("/nonexistent-screensaver", &pool) == -1);
	unlink(linked);
	snprintf(path, sizeof(path), "%s/d.png", dir);
	rmdir(path);
	for (int i = 0; i < 5; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
		unlink(path);
	}
	rmdir(dir);
}

static void testCycle(void) {
	static SsPool pool;
	SS_poolClear(&pool);
	CHECK(SS_following(&pool, -1) == -1);
	CHECK(SS_startIndex(&pool, NULL) == -1);
	SS_poolAdd(&pool, "/a.png");
	CHECK(SS_following(&pool, -1) == 0);
	CHECK(SS_following(&pool, 0) == -1); // only the one
	CHECK(SS_startIndex(&pool, "/a.png") == 0);
	SS_poolAdd(&pool, "/b.png");
	SS_poolAdd(&pool, "/c.png");
	CHECK(SS_following(&pool, 0) == 1);
	CHECK(SS_following(&pool, 2) == 0);
	pool.bad[1] = 1;
	CHECK(SS_following(&pool, 0) == 2);
	CHECK(SS_startIndex(&pool, "/a.png") == 2);
	CHECK(SS_startIndex(&pool, "/c.png") == 0);
	CHECK(SS_startIndex(&pool, "/gone.png") == 0);
	CHECK(SS_startIndex(&pool, NULL) == 0);
	pool.bad[0] = pool.bad[2] = 1;
	CHECK(SS_following(&pool, -1) == -1);
	CHECK(SS_startIndex(&pool, "/a.png") == -1);

	SsCycle c;
	SsCycle_init(&c, 10000, 1000);
	CHECK(c.current == -1 && c.next == -1);
	CHECK(SsCycle_step(&c, 50000, 1) == SS_STEP_NONE); // nothing showing
	c.current = 0;
	c.shown_at = 1000;
	c.next = 1;
	CHECK(SsCycle_step(&c, 10999, 1) == SS_STEP_NONE); // not yet
	CHECK(SsCycle_step(&c, 11000, 0) == SS_STEP_NONE); // next not ready
	CHECK(SsCycle_step(&c, 12000, 1) == SS_STEP_START);
	CHECK(c.changing);
	CHECK(SsCycle_progress(&c, 12000) == 0);
	CHECK(SsCycle_progress(&c, 12500) == 500);
	CHECK(SsCycle_progress(&c, 12100) < 100); // eased in
	CHECK(SsCycle_progress(&c, 12900) > 900); // and out
	CHECK(SsCycle_progress(&c, 14000) == 1000);
	CHECK(SsCycle_step(&c, 12500, 1) == SS_STEP_MOVING);
	CHECK(SsCycle_step(&c, 13000, 1) == SS_STEP_DONE);
	CHECK(c.current == 1 && c.next == -1 && !c.changing && c.shown_at == 13000);
	CHECK(SsCycle_progress(&c, 13000) == 0);
	CHECK(SsCycle_step(&c, 30000, 1) == SS_STEP_NONE); // no next yet

	int out, in;
	SS_fadeAlphas(0, &out, &in);
	CHECK(out == 255 && in == 0);
	SS_fadeAlphas(1000, &out, &in);
	CHECK(out == 0 && in == 255);
	SS_fadeAlphas(500, &out, &in);
	CHECK(out + in == 255 && in == 127);
	SS_fadeAlphas(2000, &out, &in);
	CHECK(in == 255);
	CHECK(SS_slideOffset(1024, 0) == 0);
	CHECK(SS_slideOffset(1024, 500) == 512);
	CHECK(SS_slideOffset(1024, 1000) == 1024);
	CHECK(SS_slideOffset(0, 500) == 0);

	MosaicRect box = { 0, 0, 1024, 768 };
	MosaicRect r = SS_fit(500, 500, &box);
	CHECK(r.x == 128 && r.y == 0 && r.w == 768 && r.h == 768);
	r = SS_fit(2048, 768, &box);
	CHECK(r.x == 0 && r.w == 1024 && r.h == 384 && r.y == 192);
	r = SS_fit(0, 10, &box);
	CHECK(r.w == 0);

	MosaicRect src, to;
	MosaicRect at = { -100, 0, 400, 300 };
	CHECK(SS_clip(&at, &box, &src, &to));
	CHECK(src.x == 100 && src.w == 300 && to.x == 0 && to.w == 300 && to.h == 300);
	at = (MosaicRect){ 900, 10, 400, 300 };
	CHECK(SS_clip(&at, &box, &src, &to));
	CHECK(src.x == 0 && src.w == 124 && to.x == 900 && to.w == 124 && to.y == 10);
	at = (MosaicRect){ 1024, 0, 400, 300 };
	CHECK(!SS_clip(&at, &box, &src, &to));
}

// a random source that plays back a list, then 0s
static const unsigned long* script;
static int script_at, script_len;
static unsigned long scripted(unsigned long below, void* data) {
	(void)data;
	unsigned long n = script_at < script_len ? script[script_at] : 0;
	script_at++;
	return n % below;
}
static void play(const unsigned long* list, int len) {
	script = list;
	script_len = len;
	script_at = 0;
}
// games 0 to 9: odd ones can show, those over 5 have art, 7 is hidden
static int asked;
static int fit(int index, void* data) {
	(void)data;
	asked++;
	if (index % 2 == 0 || index == 7) return 0;
	return index > 5 ? 2 : 1;
}
static int none_fit(int index, void* data) {
	(void)index;
	(void)data;
	return 0;
}

static void testRandom(void) {
	// the first with art among the tries
	static const unsigned long a[] = { 2, 3, 9, 5 };
	play(a, 4);
	CHECK(SS_randomPick(10, -1, 16, scripted, NULL, fit, NULL) == 9);
	// none with art: the first that can show
	static const unsigned long b[] = { 2, 3, 5, 4 };
	play(b, 4);
	CHECK(SS_randomPick(10, -1, 4, scripted, NULL, fit, NULL) == 3);
	// the one showing is left out
	static const unsigned long c[] = { 9, 9, 3 };
	play(c, 3);
	CHECK(SS_randomPick(10, 9, 3, scripted, NULL, fit, NULL) == 3);
	// none in the tries: going round from a random index
	static const unsigned long d[] = { 0, 0, 4 };
	play(d, 3);
	CHECK(SS_randomPick(10, -1, 2, scripted, NULL, fit, NULL) == 5);
	static const unsigned long e[] = { 8 };
	play(e, 1);
	CHECK(SS_randomPick(10, -1, 0, scripted, NULL, fit, NULL) == 9);
	// the one showing when it's the only one, or the only one that fits
	play(NULL, 0);
	CHECK(SS_randomPick(1, 0, 4, scripted, NULL, fit, NULL) == -1); // index 0 doesn't fit
	play(NULL, 0);
	CHECK(SS_randomPick(2, 1, 4, scripted, NULL, fit, NULL) == 1); // 0 doesn't fit, 1 is showing
	// nothing fits, or nothing to pick
	asked = 0;
	CHECK(SS_randomPick(10, -1, 4, scripted, NULL, none_fit, NULL) == -1);
	CHECK(SS_randomPick(0, -1, 4, scripted, NULL, fit, NULL) == -1);
	CHECK(SS_randomPick(10, -1, 4, NULL, NULL, fit, NULL) == -1);

	unsigned long long state = 0;
	unsigned long long first = SS_xorshift(&state);
	CHECK(first != 0 && state == first);
	CHECK(SS_xorshift(&state) != first);
	unsigned long long again = 0;
	CHECK(SS_xorshift(&again) == first); // the same seed, the same numbers
}

static void testBounce(void) {
	SsBounce b;
	SsBounce_init(&b, 10, 20, 1, -1);
	CHECK(SsBounce_x(&b) == 10 && SsBounce_y(&b) == 20 && b.dx == 1 && b.dy == -1);
	// 100 pixels a second for 100ms: 10 pixels each way
	CHECK(SsBounce_step(&b, 100, 100, 1000, 1000) == 0);
	CHECK(SsBounce_x(&b) == 20 && SsBounce_y(&b) == 10);
	// over the edge and back
	CHECK(SsBounce_step(&b, 150, 100, 30, 1000) == 2);
	CHECK(SsBounce_x(&b) == 25 && b.dx == -1);
	CHECK(SsBounce_y(&b) == 5 && b.dy == 1);
	// a part of a pixel at a time adds up
	SsBounce_init(&b, 0, 0, 1, 1);
	for (int i = 0; i < 100; i++) SsBounce_step(&b, 16, 2, 1000, 1000);
	CHECK(SsBounce_x(&b) == 3 && SsBounce_y(&b) == 3);
	// a long time at once: whole trips there and back
	SsBounce_init(&b, 0, 0, 1, 1);
	// 1000 pixels: two trips of 400 and 200 more, to the far edge; one of
	// 600 and 400 more, back from it to 200
	CHECK(SsBounce_step(&b, 10000, 100, 200, 300) == 4 + 3);
	CHECK(SsBounce_x(&b) == 200 && b.dx == 1);
	CHECK(SsBounce_y(&b) == 200 && b.dy == -1);
	// a smaller box moves it inside, and no room holds it at 0
	SsBounce_init(&b, 500, 500, 1, 1);
	SsBounce_step(&b, 0, 100, 100, 0);
	CHECK(SsBounce_x(&b) == 100 && SsBounce_y(&b) == 0);
	SsBounce_step(&b, 1000, 100, -5, -5);
	CHECK(SsBounce_x(&b) == 0 && SsBounce_y(&b) == 0);
	// standing still
	SsBounce_init(&b, 7, 7, -1, -1);
	CHECK(SsBounce_step(&b, 1000, 0, 100, 100) == 0);
	CHECK(SsBounce_x(&b) == 7 && SsBounce_y(&b) == 7);
}

static void testClockFace(void) {
	int h, m, s;
	SS_handAngles(0, 0, 0, &h, &m, &s);
	CHECK(h == 0 && m == 0 && s == 0);
	SS_handAngles(3, 0, 0, &h, &m, &s);
	CHECK(h == 90);
	SS_handAngles(15, 30, 45, &h, &m, &s);
	CHECK(h == 105 && m == 184 && s == 270);
	SS_handAngles(23, 59, 59, &h, &m, &s);
	CHECK(h == 359 && m == 359 && s == 354);
	SS_handAngles(12, 0, 30, &h, &m, &s);
	CHECK(h == 0 && m == 3 && s == 180);

	int dx, dy;
	SS_handPoint(0, 100, &dx, &dy);
	CHECK(dx == 0 && dy == -100); // 12 is up
	SS_handPoint(90, 100, &dx, &dy);
	CHECK(dx == 100 && dy == 0);
	SS_handPoint(180, 100, &dx, &dy);
	CHECK(dx == 0 && dy == 100);
	SS_handPoint(270, 100, &dx, &dy);
	CHECK(dx == -100 && dy == 0);
	SS_handPoint(30, 100, &dx, &dy);
	CHECK(dx == 50 && dy == -87);
	SS_handPoint(225, 100, &dx, &dy);
	CHECK(dx == -71 && dy == 71);
	SS_handPoint(-90, 100, &dx, &dy);
	CHECK(dx == -100 && dy == 0);
	SS_handPoint(450, 10, &dx, &dy);
	CHECK(dx == 10 && dy == 0);
	SS_handPoint(45, 0, &dx, &dy);
	CHECK(dx == 0 && dy == 0);

	CHECK(SS_faceRadius(1024, 768, SS_FACE_FULL) == 307);
	CHECK(SS_faceRadius(1024, 768, SS_FACE_OVER) == 345);
	CHECK(SS_faceRadius(480, 640, SS_FACE_FULL) == 192);
	CHECK(SS_faceRadius(0, 640, SS_FACE_FULL) == 0);
}

///////////////////////////////////////
// a fake host

typedef struct FakeSurface {
	int w, h, live, cached;
} FakeSurface;
static FakeSurface fakes[256];
static int fake_count = 0;
static char last_log[600];
static char logs[8192];
static char cfg_source[16] = "folder", cfg_folder[64], cfg_style[16] = "images", cfg_face[16] = "full";
static char userdata[64], sdcard[64];
static MosaicScreenHooks screen;
static int is_open = 0, redraws = 0, opens = 0;
static int loading = 0; // art_load says it's still decoding
static int blits = 0, alpha_blits = 0;
static int (*ambient_wants)(unsigned long idle_ms, void* data) = NULL;

static void fakeLog(const char* id, const char* message) {
	(void)id;
	snprintf(last_log, sizeof(last_log), "%s", message);
	size_t len = strlen(logs);
	snprintf(logs + len, sizeof(logs) - len, "%s\n", message);
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "idle")) return "30";
	if (!strcmp(key, "source")) return cfg_source;
	if (!strcmp(key, "folder")) return cfg_folder;
	if (!strcmp(key, "interval")) return "10";
	if (!strcmp(key, "clock")) return "24h";
	if (!strcmp(key, "style")) return cfg_style;
	if (!strcmp(key, "face")) return cfg_face;
	return fallback;
}
static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	CHECK_STR(id, SCREEN_ID);
	CHECK(hooks->flags & MOSAIC_SCREEN_FULL);
	screen = *hooks;
	return 0;
}
static int fakeRegisterAmbient(const char* id, int (*wants)(unsigned long idle_ms, void* data), void* data) {
	(void)data;
	CHECK_STR(id, SCREEN_ID);
	ambient_wants = wants;
	return 0;
}
static int fakeOpen(const char* id) {
	CHECK_STR(id, SCREEN_ID);
	if (is_open) return -1;
	if (screen.enter(screen.data) != 0) return -1;
	is_open = 1;
	opens++;
	return 0;
}
static int fakeClose(const char* id) {
	CHECK_STR(id, SCREEN_ID);
	if (!is_open) return -1;
	is_open = 0;
	screen.exit(screen.data);
	return 0;
}
static const char* fakeCurrent(void) { return is_open ? SCREEN_ID : "gamelist"; }
static void fakeRedraw(void) { redraws++; }
static int fakeScale(void) { return 3; }
static int fakeSwitch(void) { return -1; }
static int fakeTheme(int index) { (void)index; return 0xFFFFFF; }
static int fakeWidthPx(void) { return 1024; }
static int fakeHeightPx(void) { return 768; }
static int live(void) {
	int n = 0;
	for (int i = 0; i < fake_count; i++) n += fakes[i].live && !fakes[i].cached;
	return n;
}
static MosaicSurface* newFake(int w, int h, int cached) {
	if (fake_count >= 256) return NULL;
	fakes[fake_count] = (FakeSurface){ w, h, 1, cached };
	return (MosaicSurface*)&fakes[fake_count++];
}
static int fakeArtLoad(const char* path, const MosaicSurface** out) {
	*out = NULL;
	if (loading) return MOSAIC_ART_LOADING;
	if (strstr(path, "broken")) return MOSAIC_ART_MISSING;
	*out = newFake(500, 500, 1);
	return MOSAIC_ART_READY;
}
static int fakeArtGet(const MosaicEntry* e, int kind, const MosaicSurface** out) {
	(void)e; (void)kind;
	*out = NULL;
	return MOSAIC_ART_MISSING;
}
static MosaicSurface* fakeScaleSurface(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)id; (void)src; (void)fit;
	CHECK(w <= 1024 && h <= 768);
	if (style == SS_STYLE_LOGO) CHECK(w <= 1024 * 2 / 5 && h <= 768 / 3);
	return newFake(w, h, 0);
}
static int fakeFree(const char* id, MosaicSurface* s) {
	(void)id;
	FakeSurface* f = (FakeSurface*)s;
	CHECK(f->live && !f->cached);
	f->live = 0;
	return 0;
}
static int fakeSize(const MosaicSurface* s, int* w, int* h) {
	const FakeSurface* f = (const FakeSurface*)s;
	*w = f->w;
	*h = f->h;
	return 0;
}
static int fakeFill(MosaicSurface* s, const MosaicRect* r, MosaicColor c) { (void)s; (void)r; (void)c; return 0; }
static int rounds = 0;
static int fakeRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s; (void)r; (void)radius; (void)c;
	rounds++;
	return 0;
}
static int fakeBlit(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* a, const MosaicRect* b) {
	(void)t; (void)a; (void)b;
	CHECK(((const FakeSurface*)s)->live);
	blits++;
	return 0;
}
static int fakeBlitAlpha(MosaicSurface* t, const MosaicSurface* s, const MosaicRect* a, const MosaicRect* b, int alpha) {
	(void)t; (void)a; (void)b;
	CHECK(((const FakeSurface*)s)->live);
	CHECK(alpha > 0 && alpha < 255);
	alpha_blits++;
	return 0;
}
static int fakeWidth(int font, const char* text) { (void)font; return 10 * (int)strlen(text); }
static int fakeHeight(int font) { (void)font; return 30; }
static int fakeTruncate(int font, const char* text, int max, char* out, size_t size) {
	(void)font; (void)max;
	snprintf(out, size, "%s", text);
	return 10 * (int)strlen(out);
}
static int clock_draws = 0;
static char music_drawn[128], large_drawn[128];
static int large_x = -1, large_y = -1;
static int fakeDraw(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max) {
	(void)s; (void)max;
	// the clock dim, a name in place of an image whole, the rest as the now playing line
	if (font == MOSAIC_FONT_LARGE && c.a == CLOCK_ALPHA) clock_draws++;
	else if (font == MOSAIC_FONT_LARGE) {
		CHECK(c.a == 255);
		snprintf(large_drawn, sizeof(large_drawn), "%s", text);
		large_x = x;
		large_y = y;
	}
	else {
		CHECK(c.a == LINE_ALPHA);
		snprintf(music_drawn, sizeof(music_drawn), "%s", text);
	}
	return 0;
}
static int fakeRecents(void) { return 0; }
static int fakeHasCapability(const char* name) {
	for (int i = 0; i < fake_provider_count; i++) {
		if (!strcmp(fake_provider_names[i], name)) return 1;
	}
	return 0;
}

// the music plugin's services
static const char* music_state = "stopped";
static int toggles = 0;
static int musicNow(const char* name, const char* args, char* out, size_t size) {
	(void)name; (void)args;
	if (!strcmp(music_state, "stopped")) snprintf(out, size, "state=stopped\ntitle=\nartist=\nline=\n");
	else {
		snprintf(out, size, "state=%s\ntitle=Song\nartist=\nline=%s: Song\n", music_state,
			!strcmp(music_state, "playing") ? "Playing" : "Paused");
	}
	return 0;
}
static int musicToggle(const char* name, const char* args, char* out, size_t size) {
	(void)name; (void)args;
	toggles++;
	if (!strcmp(music_state, "playing")) music_state = "paused";
	else if (!strcmp(music_state, "paused")) music_state = "playing";
	snprintf(out, size, "state=%s\n", music_state);
	return 0;
}

static MosaicHost fakeHost(void) {
	return (MosaicHost){
		.size = sizeof(MosaicHost),
		.api_version = MOSAIC_API_VERSION,
		.sdcard_path = sdcard,
		.userdata_path = userdata,
		.log = fakeLog,
		.cfg_get = fakeCfgGet,
		.plugin_loaded = fakePluginLoaded,
		.has_capability = fakeHasCapability,
		.request_redraw = fakeRedraw,
		.recents_count = fakeRecents,
		.theme_color = fakeTheme,
		.screen_width = fakeWidthPx,
		.screen_height = fakeHeightPx,
		.screen_scale = fakeScale,
		.surface_scale = fakeScaleSurface,
		.surface_free = fakeFree,
		.surface_size = fakeSize,
		.fill_rect = fakeFill,
		.fill_round_rect = fakeRound,
		.blit = fakeBlit,
		.text_width = fakeWidth,
		.text_height = fakeHeight,
		.text_truncate = fakeTruncate,
		.draw_text = fakeDraw,
		.art_get = fakeArtGet,
		.art_load = fakeArtLoad,
		.blit_alpha = fakeBlitAlpha,
		.hold_switch = fakeSwitch,
		.register_screen = fakeRegister,
		.open_screen = fakeOpen,
		.close_screen = fakeClose,
		.current_screen = fakeCurrent,
		.event_value = fakeEventValue,
		.call = fakeCall,
		.register_ambient = fakeRegisterAmbient,
	};
}

static MosaicInput buttons(unsigned int pressed, unsigned int held, unsigned int released) {
	return (MosaicInput){ sizeof(MosaicInput), pressed, pressed, released, held };
}

// one frame: the filter, tick, the launcher's ambient check with idle_ms,
// then a render when it's open
static unsigned int frame(unsigned long now, unsigned long idle_ms, MosaicInput input) {
	unsigned int consumed = 0;
	CHECK(plugin.input_filter(&input, &consumed) == 0);
	plugin.tick(now);
	if (!is_open && ambient_wants && ambient_wants(idle_ms, NULL)) fakeOpen(SCREEN_ID);
	if (is_open) {
		MosaicScreenView view = { sizeof(view), { 0, 0, 1024, 768 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, SCREEN_ID };
		CHECK(screen.render((MosaicSurface*)&fakes[0], &view, screen.data) == 0);
	}
	return consumed;
}
static const MosaicInput none = { sizeof(MosaicInput), 0, 0, 0, 0 };

static void testPlugin(void) {
	char base[] = "/tmp/screensaver-card-XXXXXX";
	CHECK(mkdtemp(base) != NULL);
	snprintf(sdcard, sizeof(sdcard), "%s", base);
	snprintf(userdata, sizeof(userdata), "%s/ud", base);
	char path[256];
	snprintf(path, sizeof(path), "%s/launcher", userdata);
	mkdir(userdata, 0755);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/Mine", base);
	mkdir(path, 0755);
	const char* files[] = { "one.png", "two.png", "broken.png", "three.png" };
	for (int i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), "%s/Mine/%s", base, files[i]);
		touch(path);
	}
	snprintf(cfg_folder, sizeof(cfg_folder), "Mine");
	newFake(1024, 768, 1); // fakes[0] is the screen

	// a launcher without ambient screens
	MosaicHost host_table = fakeHost();
	host_table.size = offsetof(MosaicHost, register_ambient);
	CHECK(plugin.init(&host_table) == 0);
	CHECK(!supported);
	CHECK_STR(last_log, "unsupported: this launcher has no ambient screens");
	host_table.size = sizeof(MosaicHost);

	CHECK(plugin.init(&host_table) == 0);
	CHECK(supported && ambient_wants);
	CHECK(strstr(logs, "opens after 30s idle on the list or home: folder, every 10s, fade, clock 24h\n") != NULL);

	// the launcher's idle time
	frame(1000, 1000, none);
	frame(49999, 29999, none);
	CHECK(!is_open);
	frame(50000, 30000, none);
	CHECK(is_open && opens == 1);
	CHECK(strstr(logs, "opened after 30s idle: 4 images from folder\n") != NULL);
	// broken.png is first by name: it's skipped
	CHECK(strstr(logs, "couldn't load broken.png\n") != NULL);
	CHECK(strstr(logs, "showing 2 of 4: one.png\n") != NULL);
	CHECK(live() == 2); // the one showing and the next
	CHECK(clock_draws > 0);
	CHECK_STR(music_drawn, ""); // no music plugin

	// after the interval it fades to the next, drawing both with alpha
	frame(55000, 0, none);
	CHECK(!screenAnimating(NULL));
	frame(60000, 0, none);
	CHECK(strstr(logs, "fading to 3 of 4\n") != NULL);
	CHECK(screenAnimating(NULL));
	alpha_blits = 0;
	frame(60500, 0, none);
	CHECK(alpha_blits == 2);
	frame(61000, 0, none);
	CHECK(strstr(logs, "showing 3 of 4: three.png\n") != NULL);
	CHECK(!screenAnimating(NULL));
	frame(61016, 0, none);
	CHECK(live() == 2);
	// round to two.png, past broken.png
	frame(71016, 0, none);
	frame(72016, 0, none);
	CHECK(strstr(logs, "showing 4 of 4: two.png\n") != NULL);
	frame(72032, 0, none);
	CHECK(live() == 2);

	// a press closes it, and is taken until let go: X too, without music
	unsigned int x = BIT(MOSAIC_BUTTON_X);
	unsigned int took = frame(73000, 0, buttons(x, x, 0));
	CHECK(took == ~POWER);
	CHECK(!is_open);
	CHECK_STR(last_log, "closed by a button");
	CHECK(live() == 0); // freed
	CHECK(frame(73016, 0, buttons(0, 0, x)) == ~POWER);
	CHECK(frame(73032, 0, none) == 0);

	// it starts again after the image it last showed
	frame(103032, 30000, none);
	CHECK(is_open && opens == 2);
	CHECK_STR(last_shown + strlen(last_shown) - strlen("one.png"), "one.png");

	// waking closes it
	plugin.after_resume();
	frame(104000, 0, none);
	CHECK(!is_open);
	CHECK_STR(last_log, "closed: the device woke");

	// while it loads it shows black, then the image
	loading = 1;
	frame(296000, 30000, none);
	CHECK(is_open);
	CHECK(live() == 0);
	loading = 0;
	frame(296016, 0, none);
	CHECK(live() == 2);
	plugin.after_resume();
	frame(296032, 0, none);
	CHECK(!is_open);

	// with the music plugin on but nothing playing, X still closes it
	fake_provider_count = 0;
	fakeProvide("music.now", musicNow);
	fakeProvide("music.toggle", musicToggle);
	frame(300000, 30000, none);
	CHECK(is_open);
	CHECK(frame(301000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(!is_open && toggles == 0);
	frame(301016, 0, buttons(0, 0, x));

	// with a track, what plays shows, and X plays or pauses it, staying open
	music_state = "playing";
	frame(400000, 30000, none);
	CHECK(is_open);
	CHECK_STR(music_drawn, "Playing: Song");
	CHECK(strstr(logs, "music: Playing: Song\n") != NULL);
	redraws = 0;
	CHECK(frame(401000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(is_open && toggles == 1);
	CHECK(strstr(logs, "X: music paused\n") != NULL);
	CHECK(redraws >= 1);
	CHECK_STR(music_drawn, "Paused: Song");
	CHECK(frame(401016, 0, buttons(0, 0, x)) == ~POWER);
	CHECK(frame(401032, 0, none) == 0);
	CHECK(frame(402000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(is_open && toggles == 2);
	CHECK(strstr(logs, "X: music playing\n") != NULL);
	frame(402016, 0, buttons(0, 0, x));
	// another button still closes it
	CHECK(frame(403000, 0, buttons(BIT(MOSAIC_BUTTON_A), BIT(MOSAIC_BUTTON_A), 0)) == ~POWER);
	CHECK(!is_open && toggles == 2);
	frame(403016, 0, buttons(0, 0, BIT(MOSAIC_BUTTON_A)));
	music_state = "stopped";
	fake_provider_count = 0;
	music_drawn[0] = '\0';
	plugin.quit();

	// an empty folder still opens, black with the clock
	snprintf(cfg_folder, sizeof(cfg_folder), "Empty");
	logs[0] = '\0';
	CHECK(plugin.init(&host_table) == 0);
	frame(500000, 0, none);
	frame(530000, 30000, none);
	CHECK(is_open);
	CHECK(strstr(logs, "opened after 30s idle: no images from folder\n") != NULL);
	plugin.after_resume();
	frame(530016, 0, none);
	plugin.quit();

	// a folder off the card has no images
	snprintf(cfg_folder, sizeof(cfg_folder), "../etc");
	logs[0] = '\0';
	CHECK(plugin.init(&host_table) == 0);
	CHECK(strstr(logs, "the folder setting isn't a folder on the card, so there are no images\n") != NULL);
	plugin.quit();

	for (int i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), "%s/Mine/%s", base, files[i]);
		unlink(path);
	}
	snprintf(path, sizeof(path), "%s/Mine", base);
	rmdir(path);
	snprintf(path, sizeof(path), "%s/launcher", userdata);
	rmdir(path);
	rmdir(userdata);
	rmdir(base);
}

// The games source: recents, then favorites, each game's box art, and
// nothing a filter hides.
static const char* game_recents[2];
static int gameRecents(void) { return 2; }
static const char* gameRecentPath(int i) { return i >= 0 && i < 2 ? game_recents[i] : NULL; }
static struct MosaicEntry* gameEntryNew(const char* path, int type, const char* name) {
	(void)type;
	(void)name;
	return (struct MosaicEntry*)strdup(path); // only art_find reads it, here
}
static int gameArtFind(const MosaicEntry* e, int kind, char* out, size_t size) {
	if (kind != MOSAIC_ART_BOX) return 0;
	snprintf(out, size, "%s.png", (const char*)e);
	return 1;
}
static int gameFiltered(const char* path) { return strstr(path, "Hidden") != NULL; }
static void gameFavorites(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add("/tmp/screensaver-games/Roms/NES/Zelda.nes", ctx);
	add("/tmp/screensaver-games/Roms/NES/Hidden.nes", ctx);
}
static int gameHas(const char* name, const char* path) {
	(void)name;
	(void)path;
	return 0;
}
static int gameSetList(const char* name, void (*each)(const char*, void*), void* data) {
	int i = fakeSetFind(name);
	if (i < 0) return -1;
	if (each) fake_set_list[i](name, each, data);
	return 0;
}

// video-previews' video.play: a clip for Tetris only
static char video_args[600];
static int video_calls = 0;
static int videoPlay(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	video_calls++;
	snprintf(video_args, sizeof(video_args), "%s", args);
	snprintf(out, size, "playing=%i\n", strstr(args, "Tetris") != NULL);
	return 0;
}

static void testGames(void) {
	CHECK(system("mkdir -p /tmp/screensaver-games/Roms/GB /tmp/screensaver-games/Roms/NES") == 0);
	MosaicHost h = fakeHost();
	h.sdcard_path = "/tmp/screensaver-games";
	h.recents_count = gameRecents;
	h.recent_path = gameRecentPath;
	h.entry_new = gameEntryNew;
	h.art_find = gameArtFind;
	h.is_filtered = gameFiltered;
	h.set_list = gameSetList;
	host = &h;
	game_recents[0] = "/tmp/screensaver-games/Roms/GB/Tetris.gb";
	game_recents[1] = "/tmp/screensaver-games/Roms/GB/Hidden.gb";
	touch(game_recents[0]);
	touch(game_recents[1]);
	touch("/tmp/screensaver-games/Roms/NES/Zelda.nes");
	touch("/tmp/screensaver-games/Roms/NES/Hidden.nes");
	fake_set_count = 0;
	fakeSetRegister("favorites", gameHas, gameFavorites);

	source = SS_SOURCE_GAMES;
	art = SS_ART_BOX;
	favorites_on = 1;
	entry_count = 0;
	buildPool();
	CHECK(pool.count == 2);
	if (pool.count == 2) {
		CHECK_STR(pool.paths[0], "/tmp/screensaver-games/Roms/GB/Tetris.gb.png");
		CHECK_STR(pool.paths[1], "/tmp/screensaver-games/Roms/NES/Zelda.nes.png");
	}
	// favorites off: recents alone
	favorites_on = 0;
	buildPool();
	CHECK(pool.count == 1);
	// and with favorites on but nobody registering the set
	favorites_on = 1;
	fake_set_count = 0;
	buildPool();
	CHECK(pool.count == 1);

	// the videos source: the same art, with each image's game for its clip,
	// which video-previews plays while it's there
	source = SS_SOURCE_VIDEOS;
	fakeSetRegister("favorites", gameHas, gameFavorites);
	buildPool();
	CHECK(pool.count == 2);
	if (pool.count == 2) {
		CHECK_STR(pool.games[0], "/tmp/screensaver-games/Roms/GB/Tetris.gb");
		CHECK_STR(pool.games[1], "/tmp/screensaver-games/Roms/NES/Zelda.nes");
	}
	// without video-previews, nothing's asked
	h.call = fakeCall;
	h.event_value = fakeEventValue;
	h.has_capability = fakeHasCapability;
	fake_provider_count = 0;
	clock_mode = SS_CLOCK_12H;
	clip_playing = 0;
	cycle.current = 0;
	showClip();
	CHECK(!clip_playing);
	fakeProvide("video.play", videoPlay);
	showClip();
	CHECK(clip_playing);
	CHECK_STR(video_args, "path=/tmp/screensaver-games/Roms/GB/Tetris.gb\nclock=12h\n");
	CHECK(strstr(logs, "playing the clip of Tetris.gb\n") != NULL);
	// a game without a clip stops it, and an image with no game asks to
	// stop only while one plays
	cycle.current = 1;
	showClip();
	CHECK(!clip_playing);
	CHECK(strstr(logs, "no clip for this image\n") != NULL);
	SS_poolClear(&pool);
	SS_poolAdd(&pool, "/tmp/screensaver-games/a.png");
	cycle.current = 0;
	video_calls = 0;
	showClip();
	CHECK(video_calls == 0);
	fake_provider_count = 0;
	clock_mode = SS_CLOCK_OFF;
	source = SS_SOURCE_GAMES;

	for (int i = 0; i < entry_count; i++) free(entries[i].entry);
	entry_count = 0;
	SS_poolClear(&pool);
	(void)system("rm -rf /tmp/screensaver-games");
}

// The random source and the logo and clock styles, over a card index of
// a system, Tetris with box art and a logo, a hidden game, Zelda with no
// art but a wheel beside it and Mario with neither.
#define RS_ROMS "/tmp/screensaver-random/Roms/GB"
static const char* const index_paths[] = { RS_ROMS, RS_ROMS "/Hidden.gb", RS_ROMS "/Mario.gb", RS_ROMS "/Tetris.gb",
	RS_ROMS "/Zelda.gb" };
static const char* const index_names[] = { "GB", "Hidden", "Mario", "Tetris", "Zelda" };
static int index_ready = 1, entries_live = 0;
static char opened_path[256];
static int opened_type = -1;
static int indexCount(const char* system) {
	(void)system;
	return index_ready ? 5 : MOSAIC_INDEX_NOT_READY;
}
static int indexEntry(const char* system, int i, MosaicIndexEntry* out) {
	(void)system;
	if (!index_ready || i < 0 || i >= 5) return -1;
	*out = (MosaicIndexEntry){ .size = sizeof(*out), .kind = i ? MOSAIC_INDEX_GAME : MOSAIC_INDEX_SYSTEM,
		.type = i ? MOSAIC_ENTRY_ROM : MOSAIC_ENTRY_DIR, .depth = i ? 0 : -1, .emulator = 1, .path = index_paths[i],
		.launch = index_paths[i], .system = "Game Boy (GB)", .tag = "GB", .name = index_names[i], .year = "",
		.publisher = "", .genre = "", .players = "" };
	return 0;
}
static int indexFind(const char* path, MosaicIndexEntry* out) {
	for (int i = 0; i < 5; i++) {
		if (!strcmp(path, index_paths[i])) return indexEntry(NULL, i, out) == 0 ? i : -1;
	}
	return -1;
}
static struct MosaicEntry* randomEntryNew(const char* path, int type, const char* name) {
	(void)type;
	(void)name;
	entries_live++;
	return (struct MosaicEntry*)strdup(path);
}
static void randomEntryFree(MosaicEntry* e) {
	if (!e) return;
	entries_live--;
	free(e);
}
static int randomArtFind(const MosaicEntry* e, int kind, char* out, size_t size) {
	const char* path = (const char*)e;
	out[0] = '\0';
	if (!strstr(path, "Tetris")) return 0;
	if (kind == MOSAIC_ART_BOX) snprintf(out, size, "%s.png", path);
	else if (kind == MOSAIC_ART_LOGO) snprintf(out, size, "%s.logo.png", path);
	else return 0;
	return 1;
}
static int randomOpenPath(const char* path, int type) {
	snprintf(opened_path, sizeof(opened_path), "%s", path);
	opened_type = type;
	return 1;
}
static int hiddenFiltered(const char* path) { return strstr(path, "Hidden") != NULL; }

static MosaicHost randomHost(void) {
	MosaicHost h = fakeHost();
	h.recents_count = fakeRecents;
	h.entry_new = randomEntryNew;
	h.entry_free = randomEntryFree;
	h.art_find = randomArtFind;
	h.is_filtered = hiddenFiltered;
	h.open_path = randomOpenPath;
	h.index_count = indexCount;
	h.index_entry = indexEntry;
	h.index_find = indexFind;
	return h;
}

static void close_by_waking(unsigned long now) {
	plugin.after_resume();
	frame(now, 0, none);
	CHECK(!is_open);
}

static void testStyles(void) {
	CHECK(system("mkdir -p '" RS_ROMS "/.media/wheel'") == 0);
	for (int i = 1; i < 5; i++) touch(index_paths[i]);
	touch(RS_ROMS "/.media/wheel/Zelda.png");
	snprintf(sdcard, sizeof(sdcard), "/tmp/screensaver-random");
	MosaicHost h = randomHost();
	unsigned int a = BIT(MOSAIC_BUTTON_A), b = BIT(MOSAIC_BUTTON_B);

	// the random source: Tetris first, the one with art, with its name
	snprintf(cfg_source, sizeof(cfg_source), "random");
	logs[0] = '\0';
	is_open = 0;
	CHECK(plugin.init(&h) == 0 && supported);
	seed = 1;
	CHECK(strstr(logs, "opens after 30s idle on the list or home: random (box art), every 10s, fade, clock 24h\n") != NULL);
	frame(1000, 0, none);
	frame(31000, 30000, none);
	CHECK(is_open);
	CHECK(strstr(logs, "opened after 30s idle: random games\n") != NULL);
	frame(31016, 0, none);
	CHECK(strstr(logs, "picked " RS_ROMS "/Tetris.gb\n") != NULL);
	CHECK(strstr(logs, "showing 1 of 1: Tetris.gb.png\n") != NULL);
	CHECK_STR(music_drawn, "Tetris"); // its name, on its pill
	CHECK(entries_live == 0);         // each entry freed
	CHECK(!strstr(logs, "Hidden"));
	// then the other one that can show, Zelda, by its name without art
	frame(31032, 0, none);
	CHECK(strstr(logs, "picked " RS_ROMS "/Mario.gb (its name)\n") || strstr(logs, "picked " RS_ROMS "/Zelda.gb (its name)\n"));
	CHECK(live() == 1); // Tetris' copy; a name needs none
	frame(41032, 0, none);
	frame(42100, 0, none);
	CHECK(!strcmp(large_drawn, "Mario") || !strcmp(large_drawn, "Zelda"));
	CHECK(live() == 0);
	char launched[256];
	snprintf(launched, sizeof(launched), RS_ROMS "/%s.gb", large_drawn);
	// A launches the game showing as it closes, taken until let go
	CHECK(frame(43000, 0, buttons(a, a, 0)) == ~POWER);
	CHECK(!is_open);
	CHECK_STR(opened_path, launched);
	CHECK(opened_type == MOSAIC_ENTRY_ROM);
	CHECK(strstr(logs, "closed by A: launching ") != NULL);
	CHECK(frame(43016, 0, buttons(0, 0, a)) == ~POWER);
	CHECK(frame(43032, 0, none) == 0);
	// another button only closes it, and so does A with another
	opened_path[0] = '\0';
	frame(80000, 30000, none);
	frame(80016, 0, none);
	CHECK(is_open);
	frame(81000, 0, buttons(b, b, 0));
	CHECK(!is_open && !opened_path[0]);
	frame(81016, 0, buttons(0, 0, b));
	frame(120000, 30000, none);
	frame(120016, 0, none);
	frame(121000, 0, buttons(a | b, a | b, 0));
	CHECK(!is_open && !opened_path[0]);
	frame(121016, 0, buttons(0, 0, a | b));
	CHECK(entries_live == 0);
	// before the card index is ready, black until it is
	index_ready = 0;
	frame(160000, 30000, none);
	frame(160016, 0, none);
	CHECK(is_open && cycle.current < 0);
	index_ready = 1;
	frame(161100, 0, none);
	CHECK(cycle.current >= 0);
	close_by_waking(161200);
	// a launcher without entry_free keeps an entry a game
	plugin.quit();
	h.size = offsetof(MosaicHost, entry_free);
	CHECK(plugin.init(&h) == 0);
	entry_count = 0;
	frame(200000, 30000, none);
	frame(200016, 0, none);
	CHECK(entries_live > 0 && entries_live == entry_count);
	close_by_waking(200032);
	plugin.quit();
	for (int i = 0; i < entry_count; i++) randomEntryFree(entries[i].entry);
	entry_count = 0;
	h.size = sizeof(MosaicHost);

	// the logo style: Tetris' logo, Zelda's wheel and Mario's name,
	// bouncing every frame and inside the screen
	snprintf(cfg_style, sizeof(cfg_style), "logo");
	logs[0] = '\0';
	CHECK(plugin.init(&h) == 0);
	CHECK(strstr(logs, "style logo\n") != NULL);
	seed = 1;
	frame(300000, 30000, none);
	frame(300016, 0, none);
	CHECK(strstr(logs, "picked " RS_ROMS "/Tetris.gb\n") != NULL);
	CHECK(strstr(logs, "Tetris.gb.logo.png") != NULL);
	CHECK(screenAnimating(NULL));
	int x0 = SsBounce_x(&bounce), y0 = SsBounce_y(&bounce);
	frame(301016, 0, none);
	CHECK(SsBounce_x(&bounce) == x0 + 30 * 3 && SsBounce_y(&bounce) == y0 + 30 * 3);
	for (unsigned long t = 301016; t < 340000; t += 16) {
		frame(t, 0, none);
		CHECK(SsBounce_x(&bounce) >= 0 && SsBounce_y(&bounce) >= 0);
		CHECK(SsBounce_x(&bounce) <= 1024 && SsBounce_y(&bounce) <= 768);
		CHECK(live() <= 2);
	}
	// Zelda has its wheel beside it, and Mario shows his name
	CHECK(strstr(logs, "picked " RS_ROMS "/Zelda.gb\n") || strstr(logs, "picked " RS_ROMS "/Mario.gb (its name)\n"));
	CHECK(strstr(logs, "changing to ") != NULL);
	close_by_waking(340016);
	plugin.quit();

	// the logo style with the folder source: the images, small
	snprintf(cfg_source, sizeof(cfg_source), "folder");
	snprintf(cfg_folder, sizeof(cfg_folder), "Pics");
	CHECK(system("mkdir -p /tmp/screensaver-random/Pics") == 0);
	touch("/tmp/screensaver-random/Pics/one.png");
	touch("/tmp/screensaver-random/Pics/two.png");
	CHECK(plugin.init(&h) == 0);
	frame(400000, 30000, none);
	frame(400016, 0, none);
	CHECK(live() == 2 && screenAnimating(NULL));
	close_by_waking(400032);
	plugin.quit();

	// the full clock face: no images, drawn once a second, drifting
	snprintf(cfg_style, sizeof(cfg_style), "clock");
	logs[0] = '\0';
	CHECK(plugin.init(&h) == 0);
	CHECK(strstr(logs, "style clock, face full\n") != NULL);
	frame(500000, 30000, none);
	CHECK(strstr(logs, "opened after 30s idle: the clock face\n") != NULL);
	rounds = 0;
	redraws = 0;
	frame(500016, 0, none);
	CHECK(live() == 0 && !screenAnimating(NULL));
	CHECK(rounds > 60); // the marks and the hands
	CHECK(redraws == 1); // its first second
	frame(500032, 0, none);
	CHECK(redraws == 1); // the same second
	CHECK(SsBounce_x(&bounce) >= 0 && SsBounce_x(&bounce) <= 1024 - 2 * SS_faceRadius(1024, 768, SS_FACE_FULL));
	CHECK(SsBounce_y(&bounce) <= 768 - 2 * SS_faceRadius(1024, 768, SS_FACE_FULL));
	// A only closes it, with no game showing
	CHECK(frame(501000, 0, buttons(a, a, 0)) == ~POWER && !is_open);
	frame(501016, 0, buttons(0, 0, a));
	plugin.quit();

	// the face over the dimmed images, which cycle as before
	snprintf(cfg_face, sizeof(cfg_face), "over");
	logs[0] = '\0';
	CHECK(plugin.init(&h) == 0);
	CHECK(strstr(logs, "style clock, face over\n") != NULL);
	frame(600000, 30000, none);
	rounds = 0;
	frame(600016, 0, none);
	CHECK(live() == 2 && rounds > 60 && !screenAnimating(NULL));
	CHECK(strstr(logs, "opened after 30s idle: 2 images from folder\n") != NULL);
	frame(610016, 0, none);
	CHECK(screenAnimating(NULL)); // the fade
	close_by_waking(610032);
	plugin.quit();

	snprintf(cfg_style, sizeof(cfg_style), "images");
	snprintf(cfg_face, sizeof(cfg_face), "full");
	(void)system("rm -rf /tmp/screensaver-random");
}

int main(void) {
	testSettings();
	testIdle();
	testPool();
	testCycle();
	testRandom();
	testBounce();
	testClockFace();
	testPlugin();
	testGames();
	testStyles();
	CHECK_DONE();
}
