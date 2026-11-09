// The game switcher plus plugin: its model on its own, then the plugin
// against a fake host, for its screen, the buttons, the recents it keeps,
// the hints and what each style draws.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/switcher-plus/switcher_plus.c"

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

static void testRecents(void) {
	static SPRecents r;
	memset(&r, 0, sizeof(r));
	threeRecents();
	CHECK(SP_sync(&r, 3, modelPath, NULL) == 1);
	CHECK(r.count == 3);
	CHECK(SP_sync(&r, 3, modelPath, NULL) == 0);

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
	CHECK(SP_sync(&r, 3, modelPath, NULL) == 1);
	CHECK_STR(r.path[2], "/sd/Roms/GB/Date.gb");
	SP_info(&r, 1, modelInfo, NULL);
	CHECK(info_asks == 2);
	// one fewer
	CHECK(SP_sync(&r, 2, modelPath, NULL) == 1);
	CHECK(r.count == 2);
	// a failed ask isn't asked again until they change
	info_fails = 0;
	CHECK(SP_info(&r, 0, modelInfo, NULL) == NULL);
	CHECK(SP_info(&r, 0, modelInfo, NULL) == NULL);
	CHECK(info_asks == 3);
	info_fails = -1;
	// none, and more than it keeps
	CHECK(SP_sync(&r, 0, modelPath, NULL) == 1 && r.count == 0);
	CHECK(SP_sync(&r, 0, modelPath, NULL) == 0);
	for (int k = 0; k < SP_MAX_RECENTS + 2; k++) fake_paths[k] = "/sd/Roms/GB/Same.gb";
	fake_count = SP_MAX_RECENTS + 2;
	CHECK(SP_sync(&r, fake_count, modelPath, NULL) == 1 && r.count == SP_MAX_RECENTS);
	CHECK(SP_sync(&r, fake_count, modelPath, NULL) == 0);
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
static int text_count = 0;
static const char* style_setting = "full";

static void fakeLog(const char* id, const char* message) {
	(void)id;
	snprintf(last_log, sizeof(last_log), "%s", message);
	logs++;
}

static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (strcmp(key, "style") == 0) return style_setting;
	return fallback;
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
	(void)id;
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
	(void)id;
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
	(void)x;
	(void)y;
	if (text_count < 16) snprintf(texts[text_count++], sizeof(texts[0]), "%s", text);
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

static void setupHost(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
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

int main(void) {
	testSettings();
	testMoves();
	testHints();
	testRecents();
	testGeometry();
	testStrip();
	testList();
	testInit();
	testButtons();
	testStyleMoves();
	testRender();
	CHECK_DONE();
}
