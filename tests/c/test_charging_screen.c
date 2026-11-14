// The charging screen plugin: its model on its own, then the plugin
// against a fake host, for when it opens and closes, the buttons it takes
// and what it draws.

#include <stdlib.h>
#include "check.h"
#include "words.h"
#include "fakeproviders.h"
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

static void testWants(void) {
	CHECK(!CS_wants(29999, 30000, 1));
	CHECK(CS_wants(30000, 30000, 1));
	CHECK(CS_wants(100000, 30000, 1));
	CHECK(!CS_wants(100000, 30000, 0));  // not charging
	CHECK(!CS_wants(100000, 30000, -1)); // no battery
	CHECK(!CS_wants(100000, 0, 1));      // off
}

static void testFilter(void) {
	CsFilter f = {0};
	int shut = 0, toggled = 0;
	unsigned int a = BIT(MOSAIC_BUTTON_A), x = BIT(MOSAIC_BUTTON_X);

	// closed: nothing is taken
	CHECK(CsFilter_frame(&f, 0, a, a, 0, POWER, 0, &shut, &toggled) == 0);
	CHECK(!shut);
	CHECK(CsFilter_frame(&f, 0, 0, 0, a, POWER, 0, &shut, &toggled) == 0);

	// open: a press closes it and is taken until it's let go
	CHECK(CsFilter_frame(&f, 1, 0, 0, 0, POWER, 0, &shut, &toggled) == 0);
	CHECK(!shut);
	CHECK(CsFilter_frame(&f, 1, x, x, 0, POWER, 0, &shut, &toggled) == ~POWER);
	CHECK(shut && !toggled);
	CHECK(CsFilter_frame(&f, 0, 0, x, 0, POWER, 0, &shut, &toggled) == ~POWER);
	CHECK(!shut);
	CHECK(CsFilter_frame(&f, 0, 0, 0, x, POWER, 0, &shut, &toggled) == ~POWER); // its release too
	CHECK(CsFilter_frame(&f, 0, 0, 0, 0, POWER, 0, &shut, &toggled) == 0);

	// a press and release in one frame
	CHECK(CsFilter_frame(&f, 1, a, 0, a, POWER, 0, &shut, &toggled) == ~POWER);
	CHECK(shut);
	CHECK(CsFilter_frame(&f, 0, a, a, 0, POWER, 0, &shut, &toggled) == 0);

	// the power button is the launcher's
	CHECK(CsFilter_frame(&f, 1, POWER, POWER, 0, POWER, 0, &shut, &toggled) == 0);
	CHECK(!shut);
	// a button held over the power press is still let go of first
	CHECK(CsFilter_frame(&f, 1, a, a | POWER, 0, POWER, 0, &shut, &toggled) == ~POWER);
	CHECK(CsFilter_frame(&f, 0, 0, POWER, a, POWER, 0, &shut, &toggled) == ~POWER);
	CHECK(CsFilter_frame(&f, 0, 0, POWER, 0, POWER, 0, &shut, &toggled) == 0);

	// with music's toggle, X alone plays or pauses and stays open, taken
	// until it's let go; X with another button still closes
	CHECK(CsFilter_frame(&f, 1, x, x, 0, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(toggled && !shut);
	CHECK(CsFilter_frame(&f, 1, 0, x, 0, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(!toggled && !shut);
	CHECK(CsFilter_frame(&f, 1, 0, 0, x, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(CsFilter_frame(&f, 1, 0, 0, 0, POWER, x, &shut, &toggled) == 0);
	CHECK(CsFilter_frame(&f, 1, x | a, x | a, 0, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(shut && !toggled);
	CHECK(CsFilter_frame(&f, 0, 0, 0, x | a, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(CsFilter_frame(&f, 1, a, a, 0, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(shut && !toggled);
	CHECK(CsFilter_frame(&f, 0, 0, 0, a, POWER, x, &shut, &toggled) == ~POWER);
	// X with power held is X alone
	CHECK(CsFilter_frame(&f, 1, x, x | POWER, 0, POWER, x, &shut, &toggled) == ~POWER);
	CHECK(toggled && !shut);
	CHECK(CsFilter_frame(&f, 1, 0, 0, x, POWER, x, &shut, &toggled) == ~POWER);
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
static char logs[4096];
static char cfg_idle[16] = "30";
static int fake_level = 73, fake_charging = 1;
static MosaicScreenHooks screen;
static int registered = 0, is_open = 0, redraws = 0, opens = 0;
static int (*ambient_wants)(unsigned long idle_ms, void* data) = NULL;

static void fakeLog(const char* id, const char* message) {
	(void)id;
	snprintf(last_log, sizeof(last_log), "%s", message);
	size_t used = strlen(logs);
	snprintf(logs + used, sizeof(logs) - used, "%s\n", message);
}
static int logged(const char* message) {
	char line[300];
	snprintf(line, sizeof(line), "%s\n", message);
	return strstr(logs, line) != NULL;
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
static int fakeRegisterAmbient(const char* id, int (*wants)(unsigned long idle_ms, void* data), void* data) {
	(void)data;
	CHECK_STR(id, SCREEN_ID);
	CHECK(registered);
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
static int fills = 0, bar_w = -1;
static int fakeFill(MosaicSurface* s, const MosaicRect* r, MosaicColor c) { (void)s; (void)r; (void)c; fills++; return 0; }
static int fakeRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s; (void)radius;
	if (c.a == DIM && r->h < 768 / 8) bar_w = r->w; // the dim bar inside the battery
	return 0;
}
static int fakeWidth(int font, const char* text) { (void)font; return 10 * (int)strlen(text); }
static int fakeHeight(int font) { (void)font; return 30; }
static int fakeTruncate(int font, const char* text, int max, char* out, size_t size) {
	(void)font; (void)max;
	snprintf(out, size, "%s", text);
	return 10 * (int)strlen(out);
}
static char drawn[4][64];
static int drawn_count = 0;
static int fakeDraw(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max) {
	(void)s; (void)font; (void)x; (void)y; (void)max;
	CHECK(c.a == DIM);
	if (drawn_count < 4) snprintf(drawn[drawn_count++], sizeof(drawn[0]), "%s", text);
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
		.text_truncate = fakeTruncate,
		.draw_text = fakeDraw,
		.register_screen = fakeRegister,
		.open_screen = fakeOpen,
		.close_screen = fakeClose,
		.current_screen = fakeCurrent,
		.hold_switch = fakeSwitch,
		.event_value = fakeEventValue,
		.call = fakeCall,
		.register_ambient = fakeRegisterAmbient,
	};
}

static MosaicInput buttons(unsigned int pressed, unsigned int held, unsigned int released) {
	return (MosaicInput){ sizeof(MosaicInput), pressed, pressed, released, held };
}

// one frame: the filter, then tick, then the launcher's ambient check
// with idle_ms
static unsigned int frame(unsigned long now, unsigned long idle_ms, MosaicInput input) {
	unsigned int consumed = 0;
	CHECK(plugin.input_filter(&input, &consumed) == 0);
	plugin.tick(now);
	if (!is_open && ambient_wants && ambient_wants(idle_ms, NULL)) fakeOpen(SCREEN_ID);
	return consumed;
}

static void testPlugin(void) {
	MosaicHost host_table = fakeHost();

	// no battery: inert
	fake_charging = -1;
	CHECK(plugin.init(&host_table) == 0);
	CHECK_STR(last_log, "unsupported on desktop: there's no battery, so the screen never opens");
	CHECK(!registered && !ambient_wants);

	// off: inert too
	fake_charging = 1;
	snprintf(cfg_idle, sizeof(cfg_idle), "off");
	CHECK(plugin.init(&host_table) == 0);
	CHECK_STR(last_log, "idle is off, so the screen never opens");
	CHECK(!registered && !ambient_wants);

	// a launcher without ambient screens
	snprintf(cfg_idle, sizeof(cfg_idle), "30");
	host_table.size = offsetof(MosaicHost, register_ambient);
	CHECK(plugin.init(&host_table) == 0);
	CHECK_STR(last_log, "unsupported: this launcher has no ambient screens");
	CHECK(!registered);
	host_table.size = sizeof(MosaicHost);

	CHECK(plugin.init(&host_table) == 0);
	CHECK(registered && ambient_wants);
	CHECK_STR(last_log, "opens after 30s idle on the list or home while charging, clock off");

	// the launcher's idle time, while charging
	frame(1000, 1000, buttons(0, 0, 0));
	frame(29000, 29999, buttons(0, 0, 0));
	CHECK(opens == 0);
	frame(30000, 30000, buttons(0, 0, 0));
	CHECK(opens == 1);
	CHECK_STR(last_log, "opened after 30s idle: 73%, Charging");

	// what it draws, without music
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
	frame(32000, 0, buttons(0, 0, 0));
	CHECK(redraws == 0);
	fake_level = 100;
	frame(33000, 0, buttons(0, 0, 0));
	frame(34000, 0, buttons(0, 0, 0));
	CHECK(redraws >= 1);
	drawn_count = 0;
	CHECK(screen.render(NULL, &view, screen.data) == 0);
	CHECK_STR(drawn[0], "100%");
	CHECK_STR(drawn[1], "Charged");
	CHECK(bar_w > bar_73);
	redraws = 0;
	frame(36000, 0, buttons(0, 0, 0));
	CHECK(redraws == 0);

	// a press closes it and is taken until it's let go: X too, with the
	// music plugin off
	unsigned int a = BIT(MOSAIC_BUTTON_A), x = BIT(MOSAIC_BUTTON_X);
	CHECK(frame(37000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(!is_open);
	CHECK_STR(last_log, "closed by a button");
	CHECK(frame(37050, 0, buttons(0, x, 0)) == ~POWER);
	CHECK(frame(37100, 0, buttons(0, 0, x)) == ~POWER);
	CHECK(frame(37150, 0, buttons(0, 0, 0)) == 0);
	CHECK(frame(37200, 0, buttons(a, a, 0)) == 0); // the list's again

	// closes when unplugged, and isn't wanted unplugged
	frame(70000, 30000, buttons(0, 0, 0));
	CHECK(opens == 2);
	fake_charging = 0;
	frame(71000, 0, buttons(0, 0, 0));
	CHECK(!is_open);
	CHECK_STR(last_log, "closed: not charging");
	frame(200000, 100000, buttons(0, 0, 0));
	CHECK(opens == 2);
	fake_charging = 1;

	// the power button is left to the launcher, and waking closes it
	frame(201000, 30000, buttons(0, 0, 0));
	CHECK(opens == 3);
	CHECK(frame(202000, 0, buttons(POWER, POWER, 0)) == 0);
	CHECK(is_open);
	plugin.after_resume();
	frame(202100, 0, buttons(0, 0, 0));
	CHECK(!is_open);
	CHECK_STR(last_log, "closed: the device woke");

	// with the music plugin on but nothing playing, X still closes it
	fakeProvide("music.now", musicNow);
	fakeProvide("music.toggle", musicToggle);
	frame(300000, 30000, buttons(0, 0, 0));
	CHECK(opens == 4);
	CHECK(frame(301000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(!is_open);
	CHECK(toggles == 0);
	frame(301100, 0, buttons(0, 0, x));

	// with a track, the now playing line shows at the bottom, and X plays
	// or pauses it without closing
	music_state = "playing";
	frame(400000, 30000, buttons(0, 0, 0));
	CHECK(opens == 5);
	drawn_count = 0;
	CHECK(screen.render(NULL, &view, screen.data) == 0);
	CHECK(drawn_count == 3);
	CHECK_STR(drawn[2], "Playing: Song");
	CHECK(logged("music: Playing: Song"));
	redraws = 0;
	CHECK(frame(401000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(is_open);
	CHECK(toggles == 1);
	CHECK_STR(last_log, "X: music paused");
	CHECK(redraws == 1);
	CHECK(frame(401050, 0, buttons(0, x, 0)) == ~POWER);
	CHECK(frame(401100, 0, buttons(0, 0, x)) == ~POWER);
	CHECK(frame(401150, 0, buttons(0, 0, 0)) == 0);
	drawn_count = 0;
	CHECK(screen.render(NULL, &view, screen.data) == 0);
	CHECK_STR(drawn[2], "Paused: Song");
	CHECK(frame(402000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(is_open);
	CHECK_STR(last_log, "X: music playing");
	frame(402100, 0, buttons(0, 0, x));
	// a change from elsewhere is picked up within a second
	music_state = "stopped";
	redraws = 0;
	frame(403500, 0, buttons(0, 0, 0));
	CHECK(redraws == 1);
	drawn_count = 0;
	CHECK(screen.render(NULL, &view, screen.data) == 0);
	CHECK(drawn_count == 2);
	CHECK(logged("music: none"));
	// and then X closes it again
	CHECK(frame(404000, 0, buttons(x, x, 0)) == ~POWER);
	CHECK(!is_open);
	CHECK(toggles == 2);
}

int main(void) {
	testSettings();
	testWants();
	testFilter();
	testFormat();
	testPlugin();
	CHECK_DONE();
}
