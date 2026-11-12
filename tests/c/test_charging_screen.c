// The charging screen plugin: its model on its own, then the plugin
// against a fake host, for when it opens and closes, the buttons it takes
// and what it draws.

#include <stdlib.h>
#include "check.h"
#include "words.h"
#include "../../plugins/charging-screen/charging_screen.c"

#define BIT MOSAIC_BUTTON_BIT
static const unsigned int POWER = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);

static void testSettings(void) {
	CHECK(CS_parseIdle("60") == 60000);
	CHECK(CS_parseIdle(" 30 ") == 30000);
	CHECK(CS_parseIdle("1") == 1000);
	CHECK(CS_parseIdle("3600") == 3600000);
	CHECK(CS_parseIdle("off") == 0);
	CHECK(CS_parseIdle(" off ") == 0);
	CHECK(CS_parseIdle("0") == 0);
	CHECK(CS_parseIdle("3601") == CS_IDLE_DEFAULT_MS);
	CHECK(CS_parseIdle("-5") == CS_IDLE_DEFAULT_MS);
	CHECK(CS_parseIdle("1m") == CS_IDLE_DEFAULT_MS);
	CHECK(CS_parseIdle("offline") == CS_IDLE_DEFAULT_MS);
	CHECK(CS_parseIdle("") == CS_IDLE_DEFAULT_MS);
	CHECK(CS_parseIdle(NULL) == CS_IDLE_DEFAULT_MS);

	CHECK(CS_parseClock("off") == CS_CLOCK_OFF);
	CHECK(CS_parseClock("12h") == CS_CLOCK_12H);
	CHECK(CS_parseClock("24h") == CS_CLOCK_24H);
	CHECK(CS_parseClock("analog") == CS_CLOCK_24H);
	CHECK(CS_parseClock(NULL) == CS_CLOCK_24H);
}

static void testIdle(void) {
	CsIdle idle;
	CsIdle_init(&idle, 30000);
	// the first look starts the count
	CHECK(!CsIdle_due(&idle, 100000, 1, 1));
	CHECK(!CsIdle_due(&idle, 129999, 1, 1));
	CHECK(CsIdle_due(&idle, 130000, 1, 1));
	CHECK(CsIdle_due(&idle, 200000, 1, 1));

	// a button starts it again
	CsIdle_reset(&idle, 200000);
	CHECK(!CsIdle_due(&idle, 220000, 1, 1));
	CHECK(CsIdle_due(&idle, 230000, 1, 1));

	// not charging, or off the list, starts it again too
	CHECK(!CsIdle_due(&idle, 240000, 0, 1));
	CHECK(!CsIdle_due(&idle, 269999, 1, 1));
	CHECK(CsIdle_due(&idle, 270000, 1, 1));
	CHECK(!CsIdle_due(&idle, 280000, 1, 0));
	CHECK(!CsIdle_due(&idle, 300000, 1, 1));
	CHECK(CsIdle_due(&idle, 310000, 1, 1));
	// no battery
	CHECK(!CsIdle_due(&idle, 320000, -1, 1));
	CHECK(!CsIdle_due(&idle, 349999, 1, 1));

	// off never opens
	CsIdle_init(&idle, 0);
	CHECK(!CsIdle_due(&idle, 0, 1, 1));
	CHECK(!CsIdle_due(&idle, 10000000, 1, 1));
}

static void testIdleScreen(void) {
	CHECK(CS_idleScreen("gamelist", NULL));
	CHECK(CS_idleScreen("gamelist", "home.main"));
	CHECK(CS_idleScreen("home.main", "home.main"));
	CHECK(!CS_idleScreen("home.main", NULL)); // no home, or it failed
	CHECK(!CS_idleScreen("home.main", ""));
	CHECK(!CS_idleScreen("home.other", "home.main"));
	CHECK(!CS_idleScreen("quickmenu", "home.main"));
	CHECK(!CS_idleScreen("switcher", NULL));
	CHECK(!CS_idleScreen("dialog", NULL));
	CHECK(!CS_idleScreen(NULL, "home.main"));
}

static void testFilter(void) {
	CsFilter f = {0};
	int shut = 0;
	unsigned int a = BIT(MOSAIC_BUTTON_A), x = BIT(MOSAIC_BUTTON_X);

	// closed: nothing is taken
	CHECK(CsFilter_frame(&f, 0, a, a, 0, POWER, &shut) == 0);
	CHECK(!shut);
	CHECK(CsFilter_frame(&f, 0, 0, 0, a, POWER, &shut) == 0);

	// open: a press closes it and is taken until it's let go
	CHECK(CsFilter_frame(&f, 1, 0, 0, 0, POWER, &shut) == 0);
	CHECK(!shut);
	CHECK(CsFilter_frame(&f, 1, x, x, 0, POWER, &shut) == ~POWER);
	CHECK(shut);
	CHECK(CsFilter_frame(&f, 0, 0, x, 0, POWER, &shut) == ~POWER);
	CHECK(!shut);
	CHECK(CsFilter_frame(&f, 0, 0, 0, x, POWER, &shut) == ~POWER); // its release too
	CHECK(CsFilter_frame(&f, 0, 0, 0, 0, POWER, &shut) == 0);

	// a press and release in one frame
	CHECK(CsFilter_frame(&f, 1, a, 0, a, POWER, &shut) == ~POWER);
	CHECK(shut);
	CHECK(CsFilter_frame(&f, 0, a, a, 0, POWER, &shut) == 0);

	// the power button is the launcher's
	CHECK(CsFilter_frame(&f, 1, POWER, POWER, 0, POWER, &shut) == 0);
	CHECK(!shut);
	// a button held over the power press is still let go of first
	CHECK(CsFilter_frame(&f, 1, a, a | POWER, 0, POWER, &shut) == ~POWER);
	CHECK(CsFilter_frame(&f, 0, 0, POWER, a, POWER, &shut) == ~POWER);
	CHECK(CsFilter_frame(&f, 0, 0, POWER, 0, POWER, &shut) == 0);
}

static void testFormat(void) {
	char out[16];
	CS_formatLevel(73, out, sizeof(out));
	CHECK_STR(out, "73%");
	CS_formatLevel(0, out, sizeof(out));
	CHECK_STR(out, "0%");
	CS_formatLevel(100, out, sizeof(out));
	CHECK_STR(out, "100%");
	CS_formatLevel(-1, out, sizeof(out));
	CHECK_STR(out, "");
	CS_formatLevel(101, out, sizeof(out));
	CHECK_STR(out, "");

	CHECK_STR(CS_stateLabel(73), "Charging");
	CHECK_STR(CS_stateLabel(-1), "Charging");
	CHECK_STR(CS_stateLabel(100), "Charged");

	CS_formatClock(9, 5, CS_CLOCK_24H, out, sizeof(out));
	CHECK_STR(out, "09:05");
	CS_formatClock(23, 59, CS_CLOCK_24H, out, sizeof(out));
	CHECK_STR(out, "23:59");
	// in the words it's given, then English again
	static const char* const words[] = { "PM", "p. m.", NULL };
	fake_words = words;
	CSWords set = { fakeTr, fakePlural };
	CS_setWords(&set);
	CS_formatClock(23, 59, CS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "11:59 p. m.");
	CS_setWords(NULL);
	CS_formatClock(23, 59, CS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "11:59 PM");
	CS_formatClock(0, 0, CS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "12:00 AM");
	CS_formatClock(12, 30, CS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "12:30 PM");
	CS_formatClock(21, 7, CS_CLOCK_12H, out, sizeof(out));
	CHECK_STR(out, "9:07 PM");
	CS_formatClock(9, 5, CS_CLOCK_OFF, out, sizeof(out));
	CHECK_STR(out, "");
	CS_formatClock(24, 0, CS_CLOCK_24H, out, sizeof(out));
	CHECK_STR(out, "");
	CS_formatClock(1, 60, CS_CLOCK_24H, out, sizeof(out));
	CHECK_STR(out, "");

	CHECK(CS_fillWidth(50, 200) == 100);
	CHECK(CS_fillWidth(100, 200) == 200);
	CHECK(CS_fillWidth(1, 50) == 1); // never lost
	CHECK(CS_fillWidth(0, 200) == 0);
	CHECK(CS_fillWidth(-1, 200) == 0);
	CHECK(CS_fillWidth(101, 200) == 0);
	CHECK(CS_fillWidth(50, 0) == 0);

	int l = 0, c = 0;
	CHECK(CS_parseFake("73 1", &l, &c) && l == 73 && c == 1);
	CHECK(CS_parseFake(" 40 0\n", &l, &c) && l == 40 && c == 0);
	CHECK(!CS_parseFake("73", &l, &c));
	CHECK(!CS_parseFake("73 2", &l, &c));
	CHECK(!CS_parseFake("73 1 x", &l, &c));
	CHECK(!CS_parseFake("", &l, &c));
	CHECK(!CS_parseFake(NULL, &l, &c));
}

///////////////////////////////////////
// a fake host

static char last_log[256];
static char cfg_idle[16] = "30";
static int fake_level = 73, fake_charging = 1;
static MosaicScreenHooks screen;
static int registered = 0, is_open = 0, redraws = 0, opens = 0, open_result = 0;
static const char* top = "gamelist";
static const char* home = NULL;

static void fakeLog(const char* id, const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "idle")) return cfg_idle;
	if (!strcmp(key, "clock")) return "off";
	return fallback;
}
static int fakeLevel(void) { return fake_level; }
static int fakeCharging(void) { return fake_charging; }
static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	CHECK_STR(id, SCREEN_ID);
	CHECK(hooks->flags & MOSAIC_SCREEN_FULL);
	screen = *hooks;
	registered = 1;
	return 0;
}
static int fakeOpen(const char* id) {
	CHECK_STR(id, SCREEN_ID);
	if (open_result || is_open) return -1;
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
static int fills = 0, bar_w = -1;
static int fakeFill(MosaicSurface* s, const MosaicRect* r, MosaicColor c) { (void)s; (void)r; (void)c; fills++; return 0; }
static int fakeRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s; (void)radius;
	if (c.a == DIM && r->h < 768 / 8) bar_w = r->w; // the dim bar inside the battery
	return 0;
}
static int fakeWidth(int font, const char* text) { (void)font; return 10 * (int)strlen(text); }
static int fakeHeight(int font) { (void)font; return 30; }
static char drawn[4][32];
static int drawn_count = 0;
static int fakeDraw(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max) {
	(void)s; (void)font; (void)x; (void)y; (void)max;
	CHECK(c.a == DIM);
	if (drawn_count < 4) snprintf(drawn[drawn_count++], sizeof(drawn[0]), "%s", text);
	return 0;
}

static MosaicHost fakeHost(void) {
	return (MosaicHost){
		.size = sizeof(MosaicHost),
		.api_version = MOSAIC_API_VERSION,
		.log = fakeLog,
		.cfg_get = fakeCfgGet,
		.request_redraw = fakeRedraw,
		.battery_level = fakeLevel,
		.is_charging = fakeCharging,
		.theme_color = fakeTheme,
		.screen_scale = fakeScale,
		.fill_rect = fakeFill,
		.fill_round_rect = fakeRound,
		.text_width = fakeWidth,
		.text_height = fakeHeight,
		.draw_text = fakeDraw,
		.register_screen = fakeRegister,
		.open_screen = fakeOpen,
		.close_screen = fakeClose,
		.current_screen = fakeCurrent,
		.hold_switch = fakeSwitch,
		.home_screen = fakeHome,
	};
}

static MosaicInput buttons(unsigned int pressed, unsigned int held, unsigned int released) {
	return (MosaicInput){ sizeof(MosaicInput), pressed, pressed, released, held };
}

// one frame: the filter, then tick, as the launcher calls them
static unsigned int frame(unsigned long now, MosaicInput input) {
	unsigned int consumed = 0;
	CHECK(plugin.input_filter(&input, &consumed) == 0);
	plugin.tick(now);
	return consumed;
}

static void testPlugin(void) {
	MosaicHost host_table = fakeHost();

	// no battery: inert
	fake_charging = -1;
	CHECK(plugin.init(&host_table) == 0);
	CHECK_STR(last_log, "unsupported on desktop: there's no battery, so the screen never opens");
	CHECK(!registered);
	frame(0, buttons(0, 0, 0));
	frame(100000, buttons(0, 0, 0));
	CHECK(opens == 0);

	// off: inert too
	fake_charging = 1;
	snprintf(cfg_idle, sizeof(cfg_idle), "off");
	CHECK(plugin.init(&host_table) == 0);
	CHECK_STR(last_log, "idle is off, so the screen never opens");
	CHECK(!registered);

	snprintf(cfg_idle, sizeof(cfg_idle), "30");
	CHECK(plugin.init(&host_table) == 0);
	CHECK(registered);
	CHECK_STR(last_log, "opens after 30s idle on the list or home while charging, clock off");

	unsigned int a = BIT(MOSAIC_BUTTON_A);
	frame(1000, buttons(0, 0, 0));
	frame(20000, buttons(a, a, 0)); // a press starts the count again
	frame(20100, buttons(0, 0, a));
	frame(50000, buttons(0, 0, 0));
	CHECK(opens == 0);
	frame(50100, buttons(0, 0, 0));
	CHECK(opens == 1);
	CHECK_STR(last_log, "opened after 30s idle on the list: 73%, Charging");

	// what it draws
	MosaicScreenView view = { sizeof(view), { 0, 0, 1024, 768 }, { 0 }, { 0 }, SCREEN_ID };
	CHECK(screen.render(NULL, &view, screen.data) == 0);
	CHECK(drawn_count == 2);
	CHECK_STR(drawn[0], "73%");
	CHECK_STR(drawn[1], "Charging");
	CHECK(bar_w > 0);
	int bar_73 = bar_w;
	CHECK_STR(last_log, "drew 73%, Charging");
	CHECK(!screen.animating(screen.data));

	// drawn again only when it changes
	redraws = 0;
	frame(52000, buttons(0, 0, 0));
	CHECK(redraws == 0);
	fake_level = 100;
	frame(53000, buttons(0, 0, 0));
	frame(54000, buttons(0, 0, 0));
	CHECK(redraws >= 1);
	drawn_count = 0;
	CHECK(screen.render(NULL, &view, screen.data) == 0);
	CHECK_STR(drawn[0], "100%");
	CHECK_STR(drawn[1], "Charged");
	CHECK(bar_w > bar_73);
	redraws = 0;
	frame(56000, buttons(0, 0, 0));
	CHECK(redraws == 0);

	// a press closes it and is taken until it's let go
	unsigned int x = BIT(MOSAIC_BUTTON_X);
	CHECK(frame(57000, buttons(x, x, 0)) == ~POWER);
	CHECK(!is_open);
	CHECK_STR(last_log, "closed by a button");
	CHECK(frame(57050, buttons(0, x, 0)) == ~POWER);
	CHECK(frame(57100, buttons(0, 0, x)) == ~POWER);
	CHECK(frame(57150, buttons(0, 0, 0)) == 0);
	CHECK(frame(57200, buttons(a, a, 0)) == 0); // the list's again

	// opens again after the idle time, and closes when unplugged
	frame(87100, buttons(0, 0, 0));
	CHECK(opens == 1);
	frame(87200, buttons(0, 0, 0));
	CHECK(opens == 2);
	fake_charging = 0;
	frame(88000, buttons(0, 0, 0));
	CHECK(!is_open);
	CHECK_STR(last_log, "closed: not charging");
	fake_charging = 1;
	frame(100000, buttons(0, 0, 0));
	CHECK(opens == 2); // the count started when it closed
	frame(118100, buttons(0, 0, 0));
	CHECK(opens == 3);

	// the power button is left to the launcher, and waking closes it
	CHECK(frame(119000, buttons(POWER, POWER, 0)) == 0);
	CHECK(is_open);
	plugin.after_resume();
	frame(119100, buttons(0, 0, 0));
	CHECK(!is_open);
	CHECK_STR(last_log, "closed: the device woke");

	// only over the game list
	top = "quickmenu";
	frame(200000, buttons(0, 0, 0));
	CHECK(opens == 3);
	top = "gamelist";
	frame(200100, buttons(0, 0, 0));
	frame(229999, buttons(0, 0, 0));
	CHECK(opens == 3);
	frame(230000, buttons(0, 0, 0));
	CHECK(opens == 4);
	fakeClose(SCREEN_ID); // closed some other way, such as the quick menu over it
	frame(230200, buttons(0, 0, 0));
	CHECK(opens == 4);

	// a refused open is logged once and waits another idle time
	open_result = -1;
	frame(260200, buttons(0, 0, 0));
	CHECK_STR(last_log, "couldn't open the screen; trying again after the next idle time");
	last_log[0] = '\0';
	frame(290200, buttons(0, 0, 0));
	CHECK_STR(last_log, "");
	open_result = 0;
	frame(320200, buttons(0, 0, 0));
	CHECK(opens == 5);
	fakeClose(SCREEN_ID);

	// over home too, but not over another plugin's screen
	top = "home.main";
	frame(400000, buttons(0, 0, 0)); // home isn't the list, so the count starts
	frame(500000, buttons(0, 0, 0));
	CHECK(opens == 5);
	home = "home.main";
	frame(500100, buttons(0, 0, 0));
	frame(529999, buttons(0, 0, 0));
	CHECK(opens == 5);
	frame(530000, buttons(0, 0, 0));
	CHECK(opens == 6);
	CHECK_STR(last_log, "opened after 30s idle on home: 100%, Charged");
	CHECK(frame(531000, buttons(a, a, 0)) == ~POWER); // home doesn't get it
	CHECK(!is_open);
	top = "other.screen";
	frame(600000, buttons(0, 0, 0));
	frame(700000, buttons(0, 0, 0));
	CHECK(opens == 6);
	// an older launcher without home_screen: the list only
	top = "home.main";
	host_table.size = offsetof(MosaicHost, home_screen);
	frame(800000, buttons(0, 0, 0));
	frame(900000, buttons(0, 0, 0));
	CHECK(opens == 6);
	top = "gamelist";
	home = NULL;
}

int main(void) {
	testSettings();
	testIdle();
	testIdleScreen();
	testFilter();
	testFormat();
	testPlugin();
	CHECK_DONE();
}
