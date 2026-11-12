// The screensaver plugin: its model on its own, then the plugin against a
// fake host, for when it opens and closes, the buttons it takes, how it
// cycles through a folder's images, how many copies it holds and giving
// way to charging-screen.

#include <stdlib.h>
#include <unistd.h>
#include "check.h"
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
	SsIdle idle;
	SsIdle_init(&idle, 30000);
	// the first look starts the count
	CHECK(!SsIdle_due(&idle, 100000, 1, 0));
	CHECK(!SsIdle_due(&idle, 129999, 1, 0));
	CHECK(SsIdle_due(&idle, 130000, 1, 0));
	// a button starts it again
	SsIdle_reset(&idle, 200000);
	CHECK(!SsIdle_due(&idle, 229999, 1, 0));
	CHECK(SsIdle_due(&idle, 230000, 1, 0));
	// off the list, or giving way, starts it again too
	CHECK(!SsIdle_due(&idle, 240000, 0, 0));
	CHECK(!SsIdle_due(&idle, 269999, 1, 0));
	CHECK(SsIdle_due(&idle, 270000, 1, 0));
	CHECK(!SsIdle_due(&idle, 280000, 1, 1));
	CHECK(!SsIdle_due(&idle, 309999, 1, 0));
	CHECK(SsIdle_due(&idle, 310000, 1, 0));
	// off never opens
	SsIdle_init(&idle, 0);
	CHECK(!SsIdle_due(&idle, 1000, 1, 0));
	CHECK(!SsIdle_due(&idle, 1000000, 1, 0));

	CHECK(SS_idleScreen("gamelist", NULL));
	CHECK(SS_idleScreen("gamelist", "dash.home"));
	CHECK(SS_idleScreen("dash.home", "dash.home"));
	CHECK(!SS_idleScreen("dash.home", NULL));
	CHECK(!SS_idleScreen("dash.home", ""));
	CHECK(!SS_idleScreen("quickmenu", "dash.home"));
	CHECK(!SS_idleScreen("switcher", NULL));
	CHECK(!SS_idleScreen(NULL, NULL));

	// charging-screen comes first only while it's on, with its idle on, and charging
	CHECK(SS_chargingWins(1, "60", 1));
	CHECK(SS_chargingWins(1, "junk", 1)); // its default, 60
	CHECK(SS_chargingWins(1, NULL, 1));
	CHECK(!SS_chargingWins(1, "off", 1));
	CHECK(!SS_chargingWins(1, "0", 1));
	CHECK(!SS_chargingWins(0, "60", 1));
	CHECK(!SS_chargingWins(1, "60", 0));
	CHECK(!SS_chargingWins(1, "60", -1));

	CHECK(SS_pluginOn("charging-screen=on\n", "charging-screen"));
	CHECK(SS_pluginOn("a=off\r\n charging-screen=on \r\n", "charging-screen"));
	CHECK(!SS_pluginOn("charging-screen=on\ncharging-screen=off\n", "charging-screen"));
	CHECK(!SS_pluginOn("charging-screen-plus=on\n", "charging-screen"));
	CHECK(!SS_pluginOn("charging-screen=onn\n", "charging-screen"));
	CHECK(!SS_pluginOn(NULL, "charging-screen"));

	int l = 0, c = 0;
	CHECK(SS_parseFake("73 1", &l, &c) && l == 73 && c == 1);
	CHECK(SS_parseFake(" 40 0\n", &l, &c) && l == 40 && c == 0);
	CHECK(!SS_parseFake("73 2", &l, &c));
	CHECK(!SS_parseFake("73", &l, &c));
	CHECK(!SS_parseFake(NULL, &l, &c));

	SsFilter f = {0};
	int shut = 0;
	// closed: nothing taken
	CHECK(SsFilter_frame(&f, 0, BIT(MOSAIC_BUTTON_A), BIT(MOSAIC_BUTTON_A), POWER, &shut) == 0 && !shut);
	// open: a press closes it, and everything but power is taken until let go
	CHECK(SsFilter_frame(&f, 1, BIT(MOSAIC_BUTTON_A), BIT(MOSAIC_BUTTON_A), POWER, &shut) == ~POWER && shut);
	CHECK(SsFilter_frame(&f, 0, 0, BIT(MOSAIC_BUTTON_A), POWER, &shut) == ~POWER && !shut);
	CHECK(SsFilter_frame(&f, 0, 0, 0, POWER, &shut) == ~POWER); // the release frame
	CHECK(SsFilter_frame(&f, 0, 0, 0, POWER, &shut) == 0);
	// power alone doesn't close it
	CHECK(SsFilter_frame(&f, 1, POWER, POWER, POWER, &shut) == 0 && !shut);
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

	const char* text = "/Roms/A.gb\r\n\n/Roms/B.gb";
	const char* cursor = text;
	char line[64];
	CHECK(SS_nextLine(&cursor, line, sizeof(line)));
	CHECK_STR(line, "/Roms/A.gb");
	CHECK(SS_nextLine(&cursor, line, sizeof(line)));
	CHECK_STR(line, "/Roms/B.gb");
	CHECK(!SS_nextLine(&cursor, line, sizeof(line)));
	CHECK_STR(SS_collectionName("Mine"), "Mine");
	CHECK_STR(SS_collectionName(""), "Favorites");
	CHECK_STR(SS_collectionName("../x"), "Favorites");
	CHECK_STR(SS_collectionName(NULL), "Favorites");
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

///////////////////////////////////////
// a fake host

typedef struct FakeSurface {
	int w, h, live, cached;
} FakeSurface;
static FakeSurface fakes[256];
static int fake_count = 0;
static char last_log[600];
static char logs[4096];
static char cfg_source[16] = "folder", cfg_folder[64], cfg_charging_idle[8] = "60";
static char userdata[64], sdcard[64];
static int fake_charging = 0;
static MosaicScreenHooks screen;
static int is_open = 0, redraws = 0, opens = 0;
static int loading = 0; // art_load says it's still decoding
static const char* top = "gamelist";
static const char* home = NULL;
static int blits = 0, alpha_blits = 0;

static void fakeLog(const char* id, const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
	size_t len = strlen(logs);
	snprintf(logs + len, sizeof(logs) - len, "%s\n", message);
}
static const char* fakeCfgRead(const char* id, const char* key, const char* fallback) {
	return !strcmp(id, "charging-screen") && !strcmp(key, "idle") ? cfg_charging_idle : fallback;
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "idle")) return "30";
	if (!strcmp(key, "source")) return cfg_source;
	if (!strcmp(key, "folder")) return cfg_folder;
	if (!strcmp(key, "interval")) return "10";
	if (!strcmp(key, "clock")) return "24h";
	return fallback;
}
static int fakeCharging(void) { return fake_charging; }
static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	CHECK_STR(id, SCREEN_ID);
	CHECK(hooks->flags & MOSAIC_SCREEN_FULL);
	screen = *hooks;
	return 0;
}
static int fakeOpen(const char* id) {
	CHECK_STR(id, SCREEN_ID);
	if (is_open || !SS_idleScreen(top, home)) return -1;
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
static const char* fakeCurrent(void) { return is_open ? SCREEN_ID : top; }
static const char* fakeHome(void) { return home; }
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
	return newFake(w, h, 0);
}
static int fakeFree(const char* id, MosaicSurface* s) {
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
static int clock_draws = 0;
static int fakeDraw(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max) {
	(void)s; (void)font; (void)text; (void)x; (void)y; (void)max;
	CHECK(c.a == CLOCK_ALPHA);
	clock_draws++;
	return 0;
}
static int fakeRecents(void) { return 0; }

static MosaicHost fakeHost(void) {
	return (MosaicHost){
		.size = sizeof(MosaicHost),
		.api_version = MOSAIC_API_VERSION,
		.sdcard_path = sdcard,
		.userdata_path = userdata,
		.log = fakeLog,
		.cfg_get = fakeCfgGet,
		.cfg_read = fakeCfgRead,
		.request_redraw = fakeRedraw,
		.recents_count = fakeRecents,
		.is_charging = fakeCharging,
		.theme_color = fakeTheme,
		.screen_width = fakeWidthPx,
		.screen_height = fakeHeightPx,
		.screen_scale = fakeScale,
		.surface_scale = fakeScaleSurface,
		.surface_free = fakeFree,
		.surface_size = fakeSize,
		.fill_rect = fakeFill,
		.blit = fakeBlit,
		.text_width = fakeWidth,
		.text_height = fakeHeight,
		.draw_text = fakeDraw,
		.art_get = fakeArtGet,
		.art_load = fakeArtLoad,
		.blit_alpha = fakeBlitAlpha,
		.hold_switch = fakeSwitch,
		.register_screen = fakeRegister,
		.open_screen = fakeOpen,
		.close_screen = fakeClose,
		.current_screen = fakeCurrent,
		.home_screen = fakeHome,
	};
}

static MosaicInput buttons(unsigned int pressed, unsigned int held, unsigned int released) {
	return (MosaicInput){ sizeof(MosaicInput), pressed, pressed, released, held };
}

// one frame: the filter, tick, then a render when it's open
static unsigned int frame(unsigned long now, MosaicInput input) {
	unsigned int consumed = 0;
	CHECK(plugin.input_filter(&input, &consumed) == 0);
	plugin.tick(now);
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

	MosaicHost host_table = fakeHost();
	CHECK(plugin.init(&host_table) == 0);
	CHECK(supported);
	CHECK(strstr(logs, "opens after 30s idle on the list or home: folder, every 10s, fade, clock 24h\n") != NULL);

	// a button down keeps it shut
	frame(1000, none);
	frame(20000, buttons(BIT(MOSAIC_BUTTON_UP), BIT(MOSAIC_BUTTON_UP), 0));
	frame(49999, none);
	CHECK(!is_open);
	frame(50000, none);
	CHECK(is_open && opens == 1);
	CHECK(strstr(logs, "opened after 30s idle on the list: 4 images from folder\n") != NULL);
	// broken.png is first by name: it's skipped
	CHECK(strstr(logs, "couldn't load broken.png\n") != NULL);
	CHECK(strstr(logs, "showing 2 of 4: one.png\n") != NULL);
	CHECK(live() == 2); // the one showing and the next
	CHECK(clock_draws > 0);

	// after the interval it fades to the next, drawing both with alpha
	frame(55000, none);
	CHECK(!screenAnimating(NULL));
	frame(60000, none);
	CHECK(strstr(logs, "fading to 3 of 4\n") != NULL);
	CHECK(screenAnimating(NULL));
	alpha_blits = 0;
	frame(60500, none);
	CHECK(alpha_blits == 2);
	frame(61000, none);
	CHECK(strstr(logs, "showing 3 of 4: three.png\n") != NULL);
	CHECK(!screenAnimating(NULL));
	frame(61016, none);
	CHECK(live() == 2);
	// round to two.png, past broken.png
	frame(71016, none);
	frame(72016, none);
	CHECK(strstr(logs, "showing 4 of 4: two.png\n") != NULL);
	frame(72032, none);
	CHECK(live() == 2);

	// a press closes it, and is taken until let go
	unsigned int took = frame(73000, buttons(BIT(MOSAIC_BUTTON_A), BIT(MOSAIC_BUTTON_A), 0));
	CHECK(took == ~POWER);
	CHECK(!is_open);
	CHECK_STR(last_log, "closed by a button");
	CHECK(live() == 0); // freed
	CHECK(frame(73016, buttons(0, 0, BIT(MOSAIC_BUTTON_A))) == ~POWER);
	CHECK(frame(73032, none) == 0);

	// it starts again after the image it last showed
	frame(103032, none);
	CHECK(is_open && opens == 2);
	CHECK_STR(last_shown + strlen(last_shown) - strlen("one.png"), "one.png");

	// waking closes it
	plugin.after_resume();
	frame(104000, none);
	CHECK(!is_open);
	CHECK_STR(last_log, "closed: the device woke");

	// not over another screen, or a plugin screen that isn't home
	top = "quickmenu";
	frame(200000, none);
	CHECK(!is_open);
	top = "dash.home";
	frame(230000, none);
	frame(235000, none);
	CHECK(!is_open);
	// over home once it's home
	home = "dash.home";
	frame(265000, none);
	CHECK(is_open);
	CHECK(strstr(logs, "opened after 30s idle on home: 4 images from folder\n") != NULL);
	plugin.after_resume();
	frame(265016, none);
	CHECK(!is_open);
	home = NULL;
	top = "gamelist";

	// while it loads it shows black, then the image
	loading = 1;
	frame(296000, none);
	CHECK(is_open);
	CHECK(live() == 0);
	loading = 0;
	frame(296016, none);
	CHECK(live() == 2);
	plugin.after_resume();
	frame(296032, none);
	CHECK(!is_open);
	plugin.quit();

	// with charging-screen on and charging, it gives way
	snprintf(path, sizeof(path), "%s/launcher/plugins.txt", userdata);
	FILE* f = fopen(path, "w");
	if (f) {
		fputs("charging-screen=on\n", f);
		fclose(f);
	}
	logs[0] = '\0';
	CHECK(plugin.init(&host_table) == 0);
	CHECK(strstr(logs, ", after charging-screen while charging\n") != NULL);
	fake_charging = 1;
	frame(300000, none);
	CHECK_STR(last_log, "waiting: charging-screen's screen comes first while charging");
	frame(400000, none);
	CHECK(!is_open);
	// charging-screen's idle off: it opens
	snprintf(cfg_charging_idle, sizeof(cfg_charging_idle), "off");
	frame(400016, none);
	frame(430016, none);
	CHECK(is_open);
	plugin.after_resume();
	frame(430032, none);
	snprintf(cfg_charging_idle, sizeof(cfg_charging_idle), "60");
	// unplugged, the count starts from there
	frame(440000, none);
	frame(450000, none);
	fake_charging = 0;
	frame(450016, none);
	frame(479999, none);
	CHECK(!is_open);
	frame(480000, none);
	CHECK(is_open);
	plugin.after_resume();
	frame(480016, none);
	plugin.quit();
	unlink(path);

	// an empty folder still opens, black with the clock
	snprintf(cfg_folder, sizeof(cfg_folder), "Empty");
	logs[0] = '\0';
	CHECK(plugin.init(&host_table) == 0);
	frame(500000, none);
	frame(530000, none);
	CHECK(is_open);
	CHECK(strstr(logs, "opened after 30s idle on the list: no images from folder\n") != NULL);
	plugin.after_resume();
	frame(530016, none);
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

int main(void) {
	testSettings();
	testIdle();
	testPool();
	testCycle();
	testPlugin();
	CHECK_DONE();
}
