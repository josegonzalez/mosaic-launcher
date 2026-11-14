// The screensaver plugin: its model on its own, then the plugin against a
// fake host, for when it opens and closes, the buttons it takes, how it
// cycles through a folder's images, how many copies it holds, what music
// plays and the videos source's clips.

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
static char logs[8192];
static char cfg_source[16] = "folder", cfg_folder[64];
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
static int fakeRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s; (void)r; (void)radius; (void)c;
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
static char music_drawn[128];
static int fakeDraw(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max) {
	(void)s; (void)x; (void)y; (void)max;
	CHECK(c.a == (font == MOSAIC_FONT_LARGE ? CLOCK_ALPHA : LINE_ALPHA));
	if (font == MOSAIC_FONT_LARGE) clock_draws++;
	else snprintf(music_drawn, sizeof(music_drawn), "%s", text);
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

int main(void) {
	testSettings();
	testIdle();
	testPool();
	testCycle();
	testPlugin();
	testGames();
	CHECK_DONE();
}
