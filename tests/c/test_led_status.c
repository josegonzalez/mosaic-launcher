// The LED status plugin's model, and the plugin against a fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/led-status/led_status.c"

///////////////////////////////////////
// the model

static void testParse(void) {
	CHECK(Ls_parseMode("battery") == LS_MODE_BATTERY);
	CHECK(Ls_parseMode(" theme ") == LS_MODE_THEME);
	CHECK(Ls_parseMode("themes") == LS_MODE_BATTERY);
	CHECK(Ls_parseMode("") == LS_MODE_BATTERY);
	CHECK(Ls_parseMode(NULL) == LS_MODE_BATTERY);

	CHECK(Ls_parseThemeColor("main") == MOSAIC_COLOR_MAIN);
	CHECK(Ls_parseThemeColor("accent") == MOSAIC_COLOR_ACCENT);
	CHECK(Ls_parseThemeColor("accent2") == MOSAIC_COLOR_ACCENT2);
	CHECK(Ls_parseThemeColor("list_text") == MOSAIC_COLOR_LIST_TEXT);
	CHECK(Ls_parseThemeColor("list_text_selected") == MOSAIC_COLOR_LIST_TEXT_SELECTED);
	CHECK(Ls_parseThemeColor("hint") == MOSAIC_COLOR_HINT);
	CHECK(Ls_parseThemeColor(" background ") == MOSAIC_COLOR_BACKGROUND);
	CHECK(Ls_parseThemeColor("list") == MOSAIC_COLOR_ACCENT);
	CHECK(Ls_parseThemeColor("accent3") == MOSAIC_COLOR_ACCENT);
	CHECK(Ls_parseThemeColor(NULL) == MOSAIC_COLOR_ACCENT);

	CHECK(Ls_parseNumber("20", 1, 99, 7) == 20);
	CHECK(Ls_parseNumber("20%", 1, 99, 7) == 20);
	CHECK(Ls_parseNumber(" 20 ", 1, 99, 7) == 20);
	CHECK(Ls_parseNumber("", 1, 99, 7) == 7);
	CHECK(Ls_parseNumber(NULL, 1, 99, 7) == 7);
	CHECK(Ls_parseNumber("2x", 1, 99, 7) == 7);
	CHECK(Ls_parseNumber("0", 1, 99, 7) == 7);
	CHECK(Ls_parseNumber("100", 1, 99, 7) == 7);
}

static void testBands(void) {
	LsBattery b;
	LsBattery_init(&b, 20, 50);
	CHECK(LsBattery_sample(&b, 80) == LS_GREEN);
	CHECK(LsBattery_sample(&b, 51) == LS_GREEN);
	CHECK(LsBattery_sample(&b, 50) == LS_YELLOW);
	CHECK(LsBattery_sample(&b, 21) == LS_YELLOW);
	CHECK(LsBattery_sample(&b, 20) == LS_RED);
	CHECK(LsBattery_sample(&b, 1) == LS_RED);

	// the first good reading picks its band straight away
	LsBattery_init(&b, 20, 50);
	CHECK(LsBattery_sample(&b, 21) == LS_YELLOW);
	LsBattery_init(&b, 20, 50);
	CHECK(LsBattery_sample(&b, 100) == LS_GREEN);
}

static void testNoFlicker(void) {
	LsBattery b;
	LsBattery_init(&b, 20, 50);
	CHECK(LsBattery_sample(&b, 20) == LS_RED);
	// just above the threshold isn't enough to go back up
	CHECK(LsBattery_sample(&b, 21) == LS_RED);
	CHECK(LsBattery_sample(&b, 22) == LS_RED);
	CHECK(LsBattery_sample(&b, 20) == LS_RED);
	CHECK(LsBattery_sample(&b, 23) == LS_YELLOW);
	CHECK(LsBattery_sample(&b, 21) == LS_YELLOW);
	CHECK(LsBattery_sample(&b, 52) == LS_YELLOW);
	CHECK(LsBattery_sample(&b, 53) == LS_GREEN);
	CHECK(LsBattery_sample(&b, 51) == LS_GREEN);
	// down goes at once
	CHECK(LsBattery_sample(&b, 50) == LS_YELLOW);
	// from red straight past yellow
	LsBattery_init(&b, 20, 50);
	CHECK(LsBattery_sample(&b, 10) == LS_RED);
	CHECK(LsBattery_sample(&b, 90) == LS_GREEN);
}

static void testBadReadings(void) {
	LsBattery b;
	LsBattery_init(&b, 20, 50);
	// nothing to show before a good reading
	CHECK(LsBattery_sample(&b, 0) == -1);
	CHECK(LsBattery_sample(&b, -1) == -1);
	CHECK(LsBattery_sample(&b, 101) == -1);
	CHECK(LsBattery_sample(&b, 80) == LS_GREEN);
	// a failed read keeps the colour
	CHECK(LsBattery_sample(&b, 0) == LS_GREEN);
	CHECK(LsBattery_sample(&b, -1) == LS_GREEN);
	CHECK(LsBattery_sample(&b, 255) == LS_GREEN);
	CHECK(LsBattery_sample(&b, 79) == LS_GREEN);
}

static void testNoYellow(void) {
	LsBattery b;
	LsBattery_init(&b, 30, 30);
	CHECK(LsBattery_sample(&b, 31) == LS_GREEN);
	CHECK(LsBattery_sample(&b, 30) == LS_RED);
	CHECK(LsBattery_sample(&b, 33) == LS_GREEN);
	LsBattery_init(&b, 40, 30);
	CHECK(LsBattery_sample(&b, 35) == LS_RED);
	CHECK(LsBattery_sample(&b, 41) == LS_RED);
	CHECK(LsBattery_sample(&b, 43) == LS_GREEN);
}

static void testApplied(void) {
	LsApplied a;
	LsApplied_init(&a);
	CHECK(!LsApplied_wanted(&a, -1));
	CHECK(LsApplied_wanted(&a, 0));
	CHECK(LsApplied_wanted(&a, LS_GREEN));
	LsApplied_result(&a, LS_GREEN, 0);
	CHECK(!LsApplied_wanted(&a, LS_GREEN));
	CHECK(LsApplied_wanted(&a, LS_RED));
	// a refusal isn't retried until the colour changes
	LsApplied_result(&a, LS_RED, -1);
	CHECK(!LsApplied_wanted(&a, LS_RED));
	CHECK(LsApplied_wanted(&a, LS_YELLOW));
	LsApplied_result(&a, LS_YELLOW, 0);
	CHECK(LsApplied_wanted(&a, LS_RED));
}

static void testDue(void) {
	unsigned long next = 0;
	CHECK(Ls_due(&next, 0));
	CHECK(!Ls_due(&next, 1));
	CHECK(!Ls_due(&next, LS_SAMPLE_MS - 1));
	CHECK(Ls_due(&next, LS_SAMPLE_MS));
}

///////////////////////////////////////
// the plugin against a fake host

static int battery_now = 80;
static int leds = 4;
static int theme[8];
static int sets = 0, refuse = 0;
static int lit = -1;
static int logs = 0;
static const char* settings[8][2];

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; logs++; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	CHECK(strcmp(id, ID) == 0);
	for (int i = 0; i < 8 && settings[i][0]; i++) {
		if (!strcmp(settings[i][0], key)) return settings[i][1];
	}
	return fallback;
}
static int fakeBattery(void) { return battery_now; }
static int fakeCharging(void) { return 0; }
static int fakeLedCount(void) { return leds; }
static int fakeSet(const char* id, unsigned int rgb) {
	CHECK(strcmp(id, ID) == 0);
	CHECK(rgb <= 0xFFFFFF);
	sets++;
	if (refuse) return -1;
	lit = (int)rgb;
	return 0;
}
static int fakeClear(const char* id) { (void)id; lit = -1; return 0; }
static int fakeTheme(int index) { return index >= 1 && index <= 7 ? theme[index] : -1; }

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.battery_level = fakeBattery,
	.is_charging = fakeCharging,
	.led_count = fakeLedCount,
	.led_set_color = fakeSet,
	.led_clear = fakeClear,
	.theme_color = fakeTheme,
};

static void setting(const char* key, const char* value) {
	int i = 0;
	while (i < 7 && settings[i][0] && strcmp(settings[i][0], key)) i++;
	settings[i][0] = key;
	settings[i][1] = value;
}

static void start(const MosaicHost* h) {
	sets = 0;
	refuse = 0;
	lit = -1;
	logs = 0;
	const MosaicPlugin* p = mosaic_plugin_get(h);
	CHECK(p->init(h) == 0);
}

static void ticks(unsigned long* now, unsigned long until) {
	for (; *now <= until; *now += 100) tick(*now);
}

static void testBattery(void) {
	memset(settings, 0, sizeof(settings));
	battery_now = 80;
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 0);
	CHECK(lit == LS_GREEN);
	CHECK(sets == 1);
	// every frame, but only sent once
	ticks(&now, 60000);
	CHECK(sets == 1);
	battery_now = 45;
	ticks(&now, 64000);
	CHECK(lit == LS_GREEN);
	ticks(&now, 66000);
	CHECK(lit == LS_YELLOW);
	CHECK(sets == 2);
	// a bad reading changes nothing
	battery_now = 0;
	ticks(&now, 90000);
	CHECK(lit == LS_YELLOW);
	CHECK(sets == 2);
	battery_now = 15;
	ticks(&now, 100000);
	CHECK(lit == LS_RED);
	CHECK(sets == 3);
}

static void testBatterySettings(void) {
	memset(settings, 0, sizeof(settings));
	setting("low_at", "30");
	setting("mid_at", "70");
	battery_now = 60;
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 0);
	CHECK(lit == LS_YELLOW);
	battery_now = 25;
	ticks(&now, 10000);
	CHECK(lit == LS_RED);
}

static void testFirstReadingBad(void) {
	memset(settings, 0, sizeof(settings));
	battery_now = 0;
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 20000);
	CHECK(sets == 0);
	battery_now = 55;
	ticks(&now, 30000);
	CHECK(lit == LS_GREEN);
}

static void testTheme(void) {
	memset(settings, 0, sizeof(settings));
	setting("mode", "theme");
	for (int i = 1; i <= 7; i++) theme[i] = 0x100000 * i + i;
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 0);
	CHECK(lit == theme[MOSAIC_COLOR_ACCENT]);
	ticks(&now, 30000);
	CHECK(sets == 1);
	// a new theme shows within a few seconds
	theme[MOSAIC_COLOR_ACCENT] = 0x123456;
	ticks(&now, 36000);
	CHECK(lit == 0x123456);
	CHECK(sets == 2);

	setting("theme_color", "background");
	theme[MOSAIC_COLOR_BACKGROUND] = 0x000000;
	start(&fake);
	now = 0;
	ticks(&now, 0);
	CHECK(lit == 0x000000);
	CHECK(sets == 1);
}

static void testRefused(void) {
	memset(settings, 0, sizeof(settings));
	battery_now = 80;
	start(&fake);
	refuse = 1;
	unsigned long now = 0;
	ticks(&now, 60000);
	// tried once, logged once
	CHECK(sets == 1);
	CHECK(logs == 1);
	CHECK(lit == -1);
	refuse = 0;
	battery_now = 40;
	ticks(&now, 70000);
	CHECK(lit == LS_YELLOW);
	CHECK(sets == 2);
}

static void testResume(void) {
	memset(settings, 0, sizeof(settings));
	battery_now = 80;
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 1000);
	battery_now = 10;
	afterResume();
	tick(now);
	CHECK(lit == LS_RED);
}

static void testUnsupported(void) {
	memset(settings, 0, sizeof(settings));
	// desktop: no LEDs
	leds = 0;
	start(&fake);
	CHECK(logs == 1);
	unsigned long now = 0;
	ticks(&now, 60000);
	afterResume();
	tick(now);
	CHECK(sets == 0);
	leds = 4;

	// an older launcher, without LED colours
	MosaicHost old = fake;
	old.size = offsetof(MosaicHost, led_set_color);
	start(&old);
	CHECK(logs == 1);
	now = 0;
	ticks(&now, 60000);
	CHECK(sets == 0);
}

int main(void) {
	testParse();
	testBands();
	testNoFlicker();
	testBadReadings();
	testNoYellow();
	testApplied();
	testDue();
	testBattery();
	testBatterySettings();
	testFirstReadingBad();
	testTheme();
	testRefused();
	testResume();
	testUnsupported();
	CHECK_DONE();
}
