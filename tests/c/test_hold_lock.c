// The hold lock plugin's model, and the plugin against a fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/hold-lock/hold_lock.c"

#define BIT MOSAIC_BUTTON_BIT
#define CHORD (BIT(MOSAIC_BUTTON_SELECT) | BIT(MOSAIC_BUTTON_START))

///////////////////////////////////////
// the model

static void testParseChord(void) {
	unsigned int mask = 7;
	CHECK(HL_parseChord("select+start", &mask) && mask == CHORD);
	CHECK(HL_parseChord("L1+R1", &mask) && mask == (BIT(MOSAIC_BUTTON_L1) | BIT(MOSAIC_BUTTON_R1)));
	CHECK(HL_parseChord("menu+down", &mask) && mask == (BIT(MOSAIC_BUTTON_MENU) | BIT(MOSAIC_BUTTON_DOWN)));
	CHECK(HL_parseChord("y", &mask) && mask == BIT(MOSAIC_BUTTON_Y));
	CHECK(HL_parseChord("off", &mask) && mask == 0);
	mask = 7;
	CHECK(!HL_parseChord("select+", &mask) && mask == 7);
	CHECK(!HL_parseChord("+start", &mask));
	CHECK(!HL_parseChord("select+power", &mask));
	CHECK(!HL_parseChord("", &mask));
	CHECK(!HL_parseChord(NULL, &mask));
	CHECK(mask == 7);
}

static void testParseHold(void) {
	CHECK(HL_parseHoldMs("1", 9) == 1000);
	CHECK(HL_parseHoldMs("3", 9) == 3000);
	CHECK(HL_parseHoldMs("5", 9) == 5000);
	CHECK(HL_parseHoldMs("6", 9) == 9);
	CHECK(HL_parseHoldMs("0", 9) == 9);
	CHECK(HL_parseHoldMs("-1", 9) == 9);
	CHECK(HL_parseHoldMs("1s", 9) == 9);
	CHECK(HL_parseHoldMs("", 9) == 9);
	CHECK(HL_parseHoldMs(NULL, 9) == 9);
}

static void testChord(void) {
	HoldLock lock;
	unsigned int taken = 1;
	HL_init(&lock, CHORD, 1000, 0);
	CHECK(HL_frame(&lock, 0, -1, 100, &taken) == HL_SAME && taken == 0);
	// one of the two isn't enough
	CHECK(HL_frame(&lock, BIT(MOSAIC_BUTTON_SELECT), -1, 200, &taken) == HL_SAME && taken == 0);
	CHECK(HL_frame(&lock, BIT(MOSAIC_BUTTON_SELECT), -1, 2000, &taken) == HL_SAME && taken == 0);
	// both, with other buttons too, for a second
	unsigned int both = CHORD | BIT(MOSAIC_BUTTON_A);
	CHECK(HL_frame(&lock, both, -1, 3000, &taken) == HL_SAME && taken == 0);
	CHECK(HL_frame(&lock, both, -1, 3999, &taken) == HL_SAME && taken == 0);
	CHECK(HL_frame(&lock, both, -1, 4000, &taken) == HL_LOCKED_CHORD && taken == ~0u);
	CHECK(HL_locked(&lock));
	// still held: nothing more until it's let go
	CHECK(HL_frame(&lock, both, -1, 9000, &taken) == HL_SAME && taken == ~0u);
	CHECK(HL_frame(&lock, 0, -1, 9100, &taken) == HL_SAME && taken == ~0u);
	CHECK(HL_frame(&lock, BIT(MOSAIC_BUTTON_DOWN), -1, 9200, &taken) == HL_SAME && taken == ~0u);
	// a short hold doesn't unlock
	CHECK(HL_frame(&lock, CHORD, -1, 10000, &taken) == HL_SAME);
	CHECK(HL_frame(&lock, 0, -1, 10500, &taken) == HL_SAME && HL_locked(&lock));
	// a full one does, then takes everything until it's all let go
	CHECK(HL_frame(&lock, CHORD, -1, 11000, &taken) == HL_SAME);
	CHECK(HL_frame(&lock, CHORD, -1, 12000, &taken) == HL_UNLOCKED_CHORD && taken == ~0u);
	CHECK(!HL_locked(&lock));
	CHECK(HL_frame(&lock, BIT(MOSAIC_BUTTON_START), -1, 12100, &taken) == HL_SAME && taken == ~0u);
	CHECK(HL_frame(&lock, 0, -1, 12200, &taken) == HL_SAME && taken == ~0u);
	CHECK(HL_frame(&lock, 0, -1, 12300, &taken) == HL_SAME && taken == 0);
	CHECK(HL_frame(&lock, BIT(MOSAIC_BUTTON_DOWN), -1, 12400, &taken) == HL_SAME && taken == 0);
	// the clock wrapping past zero still counts the hold
	HL_init(&lock, CHORD, 1000, 0);
	CHECK(HL_frame(&lock, CHORD, -1, (unsigned long)-500, &taken) == HL_SAME);
	CHECK(HL_frame(&lock, CHORD, -1, 499, &taken) == HL_SAME);
	CHECK(HL_frame(&lock, CHORD, -1, 500, &taken) == HL_LOCKED_CHORD);
	// no chord never locks
	HL_init(&lock, 0, 1000, 0);
	CHECK(HL_frame(&lock, 0, -1, 0, &taken) == HL_SAME);
	CHECK(HL_frame(&lock, ~0u, -1, 5000, &taken) == HL_SAME && taken == 0);
}

static void testSwitch(void) {
	HoldLock lock;
	unsigned int taken = 1;
	// unused, the switch does nothing
	HL_init(&lock, CHORD, 1000, 0);
	CHECK(HL_frame(&lock, 0, 1, 0, &taken) == HL_SAME && taken == 0);
	// used, on locks and off unlocks
	HL_init(&lock, CHORD, 1000, 1);
	CHECK(HL_frame(&lock, 0, 0, 0, &taken) == HL_SAME && taken == 0);
	CHECK(HL_frame(&lock, 0, 1, 100, &taken) == HL_LOCKED_SWITCH && taken == ~0u);
	// the chord can't unlock it while it's on
	CHECK(HL_frame(&lock, CHORD, 1, 200, &taken) == HL_SAME);
	CHECK(HL_frame(&lock, CHORD, 1, 1300, &taken) == HL_SAME && HL_locked(&lock));
	CHECK(HL_frame(&lock, 0, 1, 1400, &taken) == HL_SAME && taken == ~0u);
	// switched off with a button down: taken until it's let go
	CHECK(HL_frame(&lock, BIT(MOSAIC_BUTTON_A), 0, 1500, &taken) == HL_UNLOCKED_SWITCH && taken == ~0u);
	CHECK(!HL_locked(&lock));
	CHECK(HL_frame(&lock, 0, 0, 1600, &taken) == HL_SAME && taken == ~0u);
	CHECK(HL_frame(&lock, 0, 0, 1700, &taken) == HL_SAME && taken == 0);
	// a chord lock outlasts the switch
	CHECK(HL_frame(&lock, CHORD, 0, 2000, &taken) == HL_SAME);
	CHECK(HL_frame(&lock, CHORD, 0, 3000, &taken) == HL_LOCKED_CHORD);
	CHECK(HL_frame(&lock, 0, 1, 3100, &taken) == HL_LOCKED_SWITCH);
	CHECK(HL_frame(&lock, 0, 0, 3200, &taken) == HL_UNLOCKED_SWITCH && taken == ~0u && HL_locked(&lock));
	CHECK(HL_frame(&lock, 0, 0, 3300, &taken) == HL_SAME && taken == ~0u);
	// no switch on the device
	HL_init(&lock, CHORD, 1000, 1);
	CHECK(HL_frame(&lock, 0, -1, 0, &taken) == HL_SAME && taken == 0);
}

static int inside(const MosaicRect* in, const MosaicRect* out) {
	return in->w > 0 && in->h > 0 && in->x >= out->x && in->y >= out->y
		&& in->x + in->w <= out->x + out->w && in->y + in->h <= out->y + out->h;
}

static void testPadlock(void) {
	int heights[] = { 30, 60, 90 };
	for (int i = 0; i < 3; i++) {
		MosaicRect rect = { 500, 15, heights[i], heights[i] };
		HlPadlock p;
		HL_padlock(&rect, &p);
		CHECK(inside(&p.shackle, &rect));
		CHECK(inside(&p.body, &rect));
		CHECK(inside(&p.hole, &p.shackle));
		CHECK(inside(&p.keyhole, &p.body));
		// the shackle's legs run into the body, and the body is wider
		CHECK(p.shackle.y + p.shackle.h > p.body.y);
		CHECK(p.hole.y + p.hole.h > p.body.y);
		CHECK(p.body.w > p.shackle.w);
		CHECK(p.hole.y > p.shackle.y && p.hole.x > p.shackle.x);
		// centred
		int mid = rect.x + rect.w / 2;
		CHECK(abs(p.body.x + p.body.w / 2 - mid) <= 1);
		CHECK(abs(p.shackle.x + p.shackle.w / 2 - mid) <= 1);
	}
}

///////////////////////////////////////
// the plugin against a fake host

static char last_log[256];
static int logs = 0, redraws = 0, fills = 0;
static int switch_pos = -1;
static const char* cfg_chord = NULL;
static const char* cfg_switch = NULL;

static void fakeLog(const char* id, const char* m) {
	CHECK(!strcmp(id, ID));
	snprintf(last_log, sizeof(last_log), "%s", m);
	logs++;
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	CHECK(!strcmp(id, ID));
	if (!strcmp(key, "chord") && cfg_chord) return cfg_chord;
	if (!strcmp(key, "switch") && cfg_switch) return cfg_switch;
	return fallback;
}
static void fakeRedraw(void) { redraws++; }
static int fakeSwitch(void) { return switch_pos; }
static int fakeTheme(int index) { return index == MOSAIC_COLOR_ACCENT ? 0x112233 : index == MOSAIC_COLOR_HINT ? 0xEEEEEE : 0; }
static int fakeFillRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s; (void)r; (void)radius; (void)c;
	fills++;
	return 0;
}
static int fakeFill(MosaicSurface* s, const MosaicRect* r, MosaicColor c) {
	(void)s; (void)r; (void)c;
	fills++;
	return 0;
}
static int fakeDrawText(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max_width) {
	(void)s; (void)font; (void)text; (void)x; (void)y; (void)c; (void)max_width;
	return 0;
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) {
	(void)s; (void)i; (void)r; (void)n;
	return 0;
}

static MosaicHost fake;

static void setUp(void) {
	memset(&fake, 0, sizeof(fake));
	fake.size = sizeof(fake);
	fake.api_version = MOSAIC_API_VERSION;
	fake.platform = "desktop";
	fake.log = fakeLog;
	fake.cfg_get = fakeCfgGet;
	fake.request_redraw = fakeRedraw;
	fake.theme_color = fakeTheme;
	fake.fill_round_rect = fakeFillRound;
	fake.fill_rect = fakeFill;
	fake.draw_text = fakeDrawText;
	fake.decorate_cell = fakeDecorateCell;
	fake.hold_switch = fakeSwitch;
	logs = redraws = fills = 0;
	last_log[0] = '\0';
	switch_pos = -1;
	cfg_chord = cfg_switch = NULL;
}

static void testPlugin(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->input_filter && p->header_width && p->header_widget);

	// an older launcher has no filters
	setUp();
	fake.size = offsetof(MosaicHost, hold_switch);
	CHECK(p->init(&fake) != 0);

	// desktop: no switch, said once; the chord works
	setUp();
	cfg_switch = "on";
	CHECK(p->init(&fake) == 0);
	CHECK(logs == 1);
	CHECK_STR(last_log, "unsupported on desktop: no hold switch, so only the chord locks");
	CHECK(lock.chord == CHORD && lock.hold_ms == 1000 && lock.use_switch);
	CHECK(p->header_width(400, 90) == 0);
	MosaicInput input = { sizeof(MosaicInput), 0, 0, 0, CHORD };
	unsigned int taken = 0;
	lock.holding = 1;
	lock.since = nowMs() - 2000;
	CHECK(p->input_filter(&input, &taken) == 0);
	CHECK(taken == ~0u && redraws == 1);
	CHECK_STR(last_log, "locked by the chord");
	CHECK(p->header_width(400, 90) == 90);
	CHECK(p->header_width(60, 90) == 0);
	MosaicRect rect = { 700, 15, 90, 90 };
	CHECK(p->header_widget(NULL, &rect) == 0);
	CHECK(fills == 5);
	CHECK_STR(last_log, "padlock at 700,15 90x90");
	int before = logs;
	CHECK(p->header_widget(NULL, &rect) == 0);
	CHECK(logs == before);

	// a device with the switch, used
	setUp();
	fake.platform = "tg5040";
	cfg_switch = "on";
	switch_pos = 0;
	CHECK(p->init(&fake) == 0);
	CHECK(logs == 0);
	input.held = 0;
	CHECK(p->input_filter(&input, &taken) == 0 && taken == 0);
	switch_pos = 1;
	CHECK(p->input_filter(&input, &taken) == 0 && taken == ~0u);
	CHECK_STR(last_log, "locked by the switch");
	switch_pos = 0;
	CHECK(p->input_filter(&input, &taken) == 0);
	CHECK_STR(last_log, "unlocked by the switch");
	CHECK(redraws == 2);

	// the switch off by default
	setUp();
	fake.platform = "tg5040";
	switch_pos = 1;
	CHECK(p->init(&fake) == 0);
	CHECK(p->input_filter(&input, &taken) == 0 && taken == 0);

	// a chord it doesn't know falls back
	setUp();
	fake.platform = "tg5040";
	switch_pos = 0;
	cfg_chord = "select+power";
	CHECK(p->init(&fake) == 0);
	CHECK(lock.chord == CHORD);
	CHECK_STR(last_log, "unknown chord \"select+power\", using select+start");

	// nothing to lock with
	setUp();
	cfg_chord = "off";
	CHECK(p->init(&fake) == 0);
	CHECK_STR(last_log, "nothing locks: the chord is off and the switch isn't used");
}

int main(void) {
	testParseChord();
	testParseHold();
	testChord();
	testSwitch();
	testPadlock();
	testPlugin();
	CHECK_DONE();
}
