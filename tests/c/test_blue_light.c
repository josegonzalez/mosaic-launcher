// The blue light tint plugin against a fake host, without the launcher:
// its colours, its schedule across midnight, the toggle's forcing until
// the next edge, and the plugin's settings, tile, button and tint, with a
// fake clock.

#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/blue-light/blue_light_model.c"
#include "../../plugins/blue-light/blue_light.c"

#define HOUR 3600LL
#define DAY (24 * HOUR)

static const char* settings[16][2];
static int setting_count = 0;
static char saved[8][2][32];
static int saved_count = 0;
static char logged[4096];
static char toasted[128];
static int redraws = 0;
static char action_ids[4][64], action_labels[4][64];
static void (*action_run)(const char*, void*);
static int action_count = 0, tile_count = 0, bound_button = -1;
static int (*tile_on)(void*);
static char clock_path[256];

static void fakeLog(const char* id, const char* message) {
	(void)id;
	size_t used = strlen(logged);
	snprintf(logged + used, sizeof(logged) - used, "%s\n", message);
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	for (int i = saved_count - 1; i >= 0; i--) {
		if (!strcmp(saved[i][0], key)) return saved[i][1];
	}
	for (int i = 0; i < setting_count; i++) {
		if (!strcmp(settings[i][0], key)) return settings[i][1];
	}
	return fallback;
}

static void fakeCfgSet(const char* key, const char* value) {
	for (int i = 0; i < saved_count; i++) {
		if (!strcmp(saved[i][0], key)) {
			snprintf(saved[i][1], sizeof(saved[i][1]), "%s", value);
			return;
		}
	}
	snprintf(saved[saved_count][0], sizeof(saved[0][0]), "%s", key);
	snprintf(saved[saved_count][1], sizeof(saved[0][1]), "%s", value);
	saved_count++;
}

static void set(const char* key, const char* value) {
	settings[setting_count][0] = key;
	settings[setting_count][1] = value;
	setting_count++;
}

static int fakeRegisterAction(const char* id, const char* label, int flags, int (*available)(const char*, void*),
		void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)available;
	(void)data;
	snprintf(action_ids[action_count], sizeof(action_ids[0]), "%s", id);
	snprintf(action_labels[action_count], sizeof(action_labels[0]), "%s", label);
	action_run = run;
	action_count++;
	return 0;
}

static int fakeQuickTile(const char* action, int row, const char* icon, const char* icon_off, int (*on)(void*)) {
	(void)icon;
	(void)icon_off;
	CHECK_STR(action, "blue-light.toggle");
	CHECK(row == MOSAIC_QUICK_TOP);
	tile_on = on;
	tile_count++;
	return 0;
}

static int fakeBindButton(int screen, int button, int trigger, const char* action) {
	(void)screen;
	(void)trigger;
	CHECK_STR(action, "blue-light.toggle");
	bound_button = button;
	return 0;
}

static void fakeToast(const char* message) { snprintf(toasted, sizeof(toasted), "%s", message); }
static void fakeRedraw(void) { redraws++; }
static int fakeTintShown(MosaicColor* out) {
	(void)out;
	return 0;
}

static MosaicHost fake;

static void reset(void) {
	memset(&fake, 0, sizeof(fake));
	fake.size = sizeof(fake);
	fake.log = fakeLog;
	fake.cfg_get = fakeCfgGet;
	fake.cfg_set = fakeCfgSet;
	fake.register_action = fakeRegisterAction;
	fake.bind_button = fakeBindButton;
	fake.request_redraw = fakeRedraw;
	fake.toast = fakeToast;
	fake.quick_tile = fakeQuickTile;
	fake.tint_shown = fakeTintShown;
	logged[0] = toasted[0] = '\0';
	setting_count = saved_count = action_count = tile_count = redraws = 0;
	bound_button = -1;
	action_run = NULL;
	tile_on = NULL;
}

// sets the fake clock to day d (from 1970-01-01) at h:m, UTC
static long long at(int d, int h, int m) {
	long long t = d * DAY + h * HOUR + m * 60LL;
	FILE* f = fopen(clock_path, "w");
	fprintf(f, "%lld\n", t);
	fclose(f);
	return t;
}

static BLClock clockAt(int d, int h, int m, int s) {
	BLClock c = { d * DAY + h * HOUR + m * 60LL + s, h * 60 + m, s };
	return c;
}

static unsigned long ms = 0;

// a tick past the fake clock's wait
static void later(void) {
	ms += 1000;
	plugin.tick(ms);
}

static int tinted(MosaicColor* out) {
	MosaicColor c = { 1, 2, 3, 4 };
	int rc = plugin.screen_tint(&c);
	if (out) *out = c;
	return rc;
}

static void model(void) {
	CHECK(BL_colour("amber") == BL_AMBER);
	CHECK(BL_colour("orange") == BL_ORANGE);
	CHECK(BL_colour("red") == BL_RED);
	CHECK(BL_colour("blue") == -1);
	CHECK(BL_colour(NULL) == -1);

	CHECK(BL_strength("10") == 10);
	CHECK(BL_strength("50") == 50);
	CHECK(BL_strength("100") == 100);
	const char* bad[] = { "0", "5", "55", "110", "1000", "", "abc", "-10", " 50", "50%" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(BL_strength(bad[i]) == -1);
	CHECK(BL_strength(NULL) == -1);

	// white turns into the colour at 100%, and halfway there at 50%
	MosaicColor c = BL_tint(BL_AMBER, 100);
	CHECK(c.r == 255 && c.g == 191 && c.b == 0 && c.a == 255);
	c = BL_tint(BL_AMBER, 50);
	CHECK(c.r == 255 && c.g == 223 && c.b == 127);
	c = BL_tint(BL_ORANGE, 50);
	CHECK(c.r == 255 && c.g == 191 && c.b == 127);
	c = BL_tint(BL_RED, 10);
	CHECK(c.r == 255 && c.g == 229 && c.b == 229);
	c = BL_tint(BL_RED, 100);
	CHECK(c.r == 255 && c.g == 0 && c.b == 0);
	c = BL_tint(BL_ORANGE, 0);
	CHECK(c.r == 255 && c.g == 255 && c.b == 255);
	// every step makes it stronger
	for (int s = BL_STRENGTH_STEP; s <= 100; s += BL_STRENGTH_STEP) {
		CHECK(BL_tint(BL_AMBER, s).b < BL_tint(BL_AMBER, s - BL_STRENGTH_STEP).b);
	}

	CHECK(BL_parseTime("21:00") == 21 * 60);
	CHECK(BL_parseTime("07:00") == 7 * 60);
	CHECK(BL_parseTime("00:00") == 0);
	CHECK(BL_parseTime("23:59") == 23 * 60 + 59);
	const char* times[] = { "24:00", "7:00", "21:60", "21-00", "21:000", "", "ab:cd" };
	for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); i++) CHECK(BL_parseTime(times[i]) == -1);
	CHECK(BL_parseTime(NULL) == -1);
	char text[8];
	BL_formatTime(7 * 60 + 5, text, sizeof(text));
	CHECK_STR(text, "07:05");
	BL_formatTime(BL_DAY + 60, text, sizeof(text));
	CHECK_STR(text, "01:00");

	// across midnight
	int from = 21 * 60, to = 7 * 60;
	CHECK(!BL_inRange(20 * 60 + 59, from, to));
	CHECK(BL_inRange(21 * 60, from, to));
	CHECK(BL_inRange(23 * 60 + 59, from, to));
	CHECK(BL_inRange(0, from, to));
	CHECK(BL_inRange(6 * 60 + 59, from, to));
	CHECK(!BL_inRange(7 * 60, from, to));
	CHECK(!BL_inRange(12 * 60, from, to));
	// in the day
	CHECK(BL_inRange(9 * 60, 9 * 60, 17 * 60));
	CHECK(!BL_inRange(17 * 60, 9 * 60, 17 * 60));
	CHECK(!BL_inRange(8 * 60, 9 * 60, 17 * 60));
	// empty
	CHECK(!BL_inRange(9 * 60, 9 * 60, 9 * 60));

	int edge = -1;
	CHECK(BL_toNextEdge(20 * 60, from, to, &edge) == 60 && edge == from);
	CHECK(BL_toNextEdge(21 * 60, from, to, &edge) == 600 && edge == to);
	CHECK(BL_toNextEdge(23 * 60, from, to, &edge) == 480 && edge == to);
	CHECK(BL_toNextEdge(6 * 60 + 59, from, to, &edge) == 1 && edge == to);
	CHECK(BL_toNextEdge(7 * 60, from, to, &edge) == 840 && edge == from);
	CHECK(BL_toNextEdge(9 * 60, 9 * 60, 9 * 60, &edge) == -1);

	CHECK(BL_button("Y") == MOSAIC_BUTTON_Y);
	CHECK(BL_button("L2") == MOSAIC_BUTTON_L2);
	CHECK(BL_button("R2") == MOSAIC_BUTTON_R2);
	CHECK(BL_button("SELECT") == MOSAIC_BUTTON_SELECT);
	CHECK(BL_button("none") == -1);
	CHECK(BL_button(NULL) == -1);

	// without a schedule, the toggle is all there is
	BLState s = { 0, 0, from, to, -1, 0 };
	BLClock noon = clockAt(1, 12, 0, 0);
	CHECK(!BL_active(&s, &noon));
	BL_toggle(&s, &noon);
	CHECK(s.manual == 1 && BL_active(&s, &noon) && s.forced == -1);
	BL_toggle(&s, &noon);
	CHECK(s.manual == 0 && !BL_active(&s, &noon));

	// scheduled: on from 21:00 to 07:00 the next day, whatever manual is
	s = (BLState){ 1, 1, from, to, -1, 0 };
	CHECK(!BL_active(&s, &noon));
	BLClock t = clockAt(1, 20, 59, 59);
	CHECK(!BL_active(&s, &t));
	t = clockAt(1, 21, 0, 0);
	CHECK(BL_active(&s, &t));
	t = clockAt(2, 0, 30, 0);
	CHECK(BL_active(&s, &t));
	t = clockAt(2, 7, 0, 0);
	CHECK(!BL_active(&s, &t));

	// the toggle in the range forces it off until 07:00, then the schedule has it again
	BLClock late = clockAt(1, 22, 15, 30);
	BL_toggle(&s, &late);
	CHECK(s.forced == 0 && s.until == 2 * DAY + 7 * HOUR);
	CHECK(s.manual == 1);
	CHECK(!BL_active(&s, &late));
	t = clockAt(2, 6, 59, 59);
	CHECK(!BL_active(&s, &t) && !BL_forcedEnded(&s, &t));
	t = clockAt(2, 7, 0, 0);
	CHECK(BL_forcedEnded(&s, &t) && !BL_active(&s, &t));
	t = clockAt(2, 21, 0, 0);
	CHECK(BL_active(&s, &t));

	// outside it, forced on until 21:00, when the schedule keeps it on
	s.forced = -1;
	BL_toggle(&s, &noon);
	CHECK(s.forced == 1 && s.until == 1 * DAY + 21 * HOUR);
	CHECK(BL_active(&s, &noon));
	t = clockAt(1, 21, 0, 0);
	CHECK(BL_forcedEnded(&s, &t) && BL_active(&s, &t));
	t = clockAt(2, 7, 0, 0);
	CHECK(!BL_active(&s, &t));

	// toggling back to what the schedule wants stops forcing
	s.forced = -1;
	BL_toggle(&s, &noon);
	BL_toggle(&s, &noon);
	CHECK(s.forced == -1 && !BL_active(&s, &noon));

	// a forcing from long ago has ended
	s.forced = 0;
	s.until = 1 * DAY;
	t = clockAt(5, 23, 0, 0);
	CHECK(BL_forcedEnded(&s, &t) && BL_active(&s, &t));

	// an empty range is no schedule
	s = (BLState){ 0, 1, 9 * 60, 9 * 60, -1, 0 };
	t = clockAt(1, 9, 0, 0);
	CHECK(!BL_active(&s, &t));
	BL_toggle(&s, &t);
	CHECK(s.manual == 1 && s.forced == -1 && BL_active(&s, &t));
}

static void plugin_(void) {
	// the defaults: off, amber at 50%, a tile and no button
	reset();
	at(1, 12, 0);
	CHECK(plugin.init(&fake) == 0);
	CHECK(action_count == 1);
	CHECK_STR(action_ids[0], "blue-light.toggle");
	CHECK_STR(action_labels[0], "Warm tint");
	CHECK(tile_count == 1 && bound_button == -1);
	CHECK(strstr(logged, "ready, tint FFDF7F, not scheduled") != NULL);
	CHECK(!tinted(NULL) && !tile_on(NULL));

	// the toggle turns it on and off, at once, and remembers it
	redraws = 0;
	action_run(NULL, NULL);
	MosaicColor c;
	CHECK(tinted(&c) == 1 && c.r == 255 && c.g == 223 && c.b == 127 && c.a == 255);
	CHECK(tile_on(NULL));
	CHECK(redraws == 1);
	CHECK_STR(toasted, "Warm tint on");
	CHECK_STR(fakeCfgGet("on", ""), "on");
	CHECK(strstr(logged, "tint on FFDF7F") != NULL);
	action_run(NULL, NULL);
	CHECK(!tinted(NULL) && redraws == 2);
	CHECK_STR(toasted, "Warm tint off");
	CHECK_STR(fakeCfgGet("on", ""), "off");

	// on from the setting, red at 100%, on SELECT, without a tile
	reset();
	set("on", "on");
	set("colour", "red");
	set("strength", "100");
	set("button", "SELECT");
	set("tile", "off");
	CHECK(plugin.init(&fake) == 0);
	CHECK(tinted(&c) == 1 && c.r == 255 && c.g == 0 && c.b == 0);
	CHECK(tile_count == 0 && bound_button == MOSAIC_BUTTON_SELECT);

	// settings it doesn't know, said so and left at their defaults
	reset();
	set("on", "on");
	set("colour", "blue");
	set("strength", "55");
	CHECK(plugin.init(&fake) == 0);
	CHECK(strstr(logged, "colour blue isn't amber, orange or red, so it's amber") != NULL);
	CHECK(strstr(logged, "strength 55 isn't 10 to 100 in steps of 10, so it's 50") != NULL);
	CHECK(tinted(&c) == 1 && c.g == 223 && c.b == 127);

	// a schedule with a bad time, or the same time twice, is off
	reset();
	set("schedule", "on");
	set("from", "9pm");
	CHECK(plugin.init(&fake) == 0);
	CHECK(strstr(logged, "from is 9pm, not a time such as 21:00, so the schedule is off") != NULL);
	CHECK(strstr(logged, "not scheduled") != NULL);
	reset();
	set("schedule", "on");
	set("from", "08:00");
	set("to", "08:00");
	CHECK(plugin.init(&fake) == 0);
	CHECK(strstr(logged, "the schedule turns on and off at the same time, so it's off") != NULL);

	// scheduled from 23:00 to 01:00: it follows the clock across midnight
	// on its own, with a redraw at each edge
	reset();
	set("schedule", "on");
	set("from", "23:00");
	set("to", "01:00");
	at(1, 22, 59);
	CHECK(plugin.init(&fake) == 0);
	CHECK(strstr(logged, "scheduled") != NULL);
	CHECK(!tinted(NULL));
	redraws = 0;
	at(1, 23, 0);
	later();
	CHECK(tinted(NULL) && redraws == 1);
	at(2, 0, 30);
	later();
	CHECK(tinted(NULL) && redraws == 1);
	at(2, 1, 0);
	later();
	CHECK(!tinted(NULL) && redraws == 2);

	// the toggle in the range: off until 01:00, then the schedule's again
	at(2, 23, 30);
	later();
	CHECK(tinted(NULL));
	action_run(NULL, NULL);
	CHECK(!tinted(NULL));
	CHECK_STR(toasted, "Warm tint off until 01:00");
	CHECK_STR(fakeCfgGet("forced", ""), "off");
	char until[32];
	snprintf(until, sizeof(until), "%lld", 3 * DAY + 1 * HOUR);
	CHECK_STR(fakeCfgGet("until", ""), until);
	// it lasts through a restart
	logged[0] = '\0';
	CHECK(plugin.init(&fake) == 0);
	CHECK(!tinted(NULL));
	at(3, 0, 59);
	later();
	CHECK(!tinted(NULL));
	at(3, 1, 0);
	later();
	CHECK(!tinted(NULL));
	CHECK(strstr(logged, "the toggle's forcing ended at the schedule's edge") != NULL);
	CHECK_STR(fakeCfgGet("forced", "x"), "");
	at(3, 23, 0);
	later();
	CHECK(tinted(NULL));

	// outside it: on until 23:00, when the schedule keeps it on until 01:00
	at(4, 12, 0);
	later();
	CHECK(!tinted(NULL));
	action_run(NULL, NULL);
	CHECK(tinted(NULL));
	CHECK_STR(toasted, "Warm tint on");
	CHECK(strstr(logged, "forced on until the schedule's next edge") != NULL);
	at(4, 23, 0);
	later();
	CHECK(tinted(NULL));
	at(5, 1, 0);
	later();
	CHECK(!tinted(NULL));

	// no screen tint in the launcher: it says so and declines
	reset();
	fake.size = offsetof(MosaicHost, tint_shown);
	CHECK(plugin.init(&fake) == 1);
	CHECK(strstr(logged, "unsupported: this launcher has no screen tint") != NULL);
}

int main(void) {
	setenv("TZ", "UTC0", 1);
	tzset();
	snprintf(clock_path, sizeof(clock_path), "%s/blue-light-clock-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp",
		(int)getpid());
	setenv("MOSAIC_BLUE_LIGHT_FAKE", clock_path, 1);
	model();
	plugin_();
	remove(clock_path);
	CHECK_DONE();
}
