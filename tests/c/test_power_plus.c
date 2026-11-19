// The power plus plugin's model, and the plugin against a fake host.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/power-plus/power_plus.c"

///////////////////////////////////////
// the model

static void testParseNumber(void) {
	CHECK(Pp_parseNumber("5", 1, 99, 7) == 5);
	CHECK(Pp_parseNumber("5%", 1, 99, 7) == 5);
	CHECK(Pp_parseNumber(" 12 ", 1, 99, 7) == 12);
	CHECK(Pp_parseNumber("", 1, 99, 7) == 7);
	CHECK(Pp_parseNumber(NULL, 1, 99, 7) == 7);
	CHECK(Pp_parseNumber("five", 1, 99, 7) == 7);
	CHECK(Pp_parseNumber("5x", 1, 99, 7) == 7);
	CHECK(Pp_parseNumber("-3", 1, 99, 7) == 7);
	CHECK(Pp_parseNumber("0", 1, 99, 7) == 7);
	CHECK(Pp_parseNumber("100", 1, 99, 7) == 7);
}

// Feeds readings 5 seconds apart from *now, returning the last event.
static int feed(PpGuard* g, unsigned long* now, int level, int charging, int count, int* seconds) {
	int event = PP_GUARD_NONE;
	for (int i = 0; i < count; i++) {
		CHECK(PpGuard_due(g, *now));
		event = PpGuard_sample(g, *now, level, charging, seconds);
		*now += PP_SAMPLE_MS;
	}
	return event;
}

static void testDue(void) {
	PpGuard g;
	PpGuard_init(&g, 5, 2);
	CHECK(PpGuard_due(&g, 0));
	CHECK(!PpGuard_due(&g, 1));
	CHECK(!PpGuard_due(&g, PP_SAMPLE_MS - 1));
	CHECK(PpGuard_due(&g, PP_SAMPLE_MS));
	PpGuard_resumed(&g);
	CHECK(PpGuard_due(&g, PP_SAMPLE_MS + 1));
}

static void testWarning(void) {
	PpGuard g;
	PpGuard_init(&g, 5, 2);
	unsigned long now = 1000;
	CHECK(feed(&g, &now, 50, 0, 3, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 5, 0, 2, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 4, 0, 1, NULL) == PP_GUARD_WARN);
	// once
	CHECK(feed(&g, &now, 4, 0, 5, NULL) == PP_GUARD_NONE);
	// a reading just above doesn't bring it back
	CHECK(feed(&g, &now, 6, 0, 1, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 5, 0, 3, NULL) == PP_GUARD_NONE);
	// well above does
	CHECK(feed(&g, &now, 8, 0, 1, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 5, 0, 3, NULL) == PP_GUARD_WARN);
	// and so does charging
	CHECK(feed(&g, &now, 5, 1, 1, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 5, 0, 3, NULL) == PP_GUARD_WARN);
}

static void testOneBadReading(void) {
	PpGuard g;
	PpGuard_init(&g, 5, 2);
	unsigned long now = 0;
	// a failed read as 0 among good ones
	for (int i = 0; i < 10; i++) {
		CHECK(feed(&g, &now, 0, 0, 1, NULL) == PP_GUARD_NONE);
		CHECK(feed(&g, &now, 0, 0, 1, NULL) == PP_GUARD_NONE);
		CHECK(feed(&g, &now, 60, 0, 1, NULL) == PP_GUARD_NONE);
	}
	// out of range counts for nothing either way
	CHECK(feed(&g, &now, -1, 0, 5, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 101, 0, 5, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 2, 0, 2, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, -1, 0, 1, NULL) == PP_GUARD_NONE);
	int seconds = 0;
	CHECK(feed(&g, &now, 2, 0, 1, &seconds) == PP_GUARD_COUNTDOWN);
	CHECK(seconds == PP_COUNTDOWN_MS / 1000);
}

static void testCountdown(void) {
	PpGuard g;
	PpGuard_init(&g, 5, 2);
	unsigned long now = 0;
	int seconds = 0;
	CHECK(feed(&g, &now, 2, 0, 2, &seconds) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 2, 0, 1, &seconds) == PP_GUARD_COUNTDOWN);
	CHECK(seconds == 30);
	CHECK(feed(&g, &now, 1, 0, 1, &seconds) == PP_GUARD_COUNTDOWN);
	CHECK(seconds == 25);
	CHECK(feed(&g, &now, 2, 0, 3, &seconds) == PP_GUARD_COUNTDOWN);
	CHECK(seconds == 10);
	CHECK(feed(&g, &now, 2, 0, 1, &seconds) == PP_GUARD_COUNTDOWN);
	CHECK(seconds == 5);
	// a failed read when it runs out waits for a good one
	CHECK(feed(&g, &now, -1, 0, 1, &seconds) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 2, 0, 1, &seconds) == PP_GUARD_POWER_OFF);
	// once
	CHECK(!PpGuard_due(&g, now));
	CHECK(PpGuard_sample(&g, now, 2, 0, &seconds) == PP_GUARD_NONE);
}

static void testCountdownCancelled(void) {
	PpGuard g;
	PpGuard_init(&g, 5, 2);
	unsigned long now = 0;
	CHECK(feed(&g, &now, 2, 0, 3, NULL) == PP_GUARD_COUNTDOWN);
	CHECK(feed(&g, &now, 2, 1, 1, NULL) == PP_GUARD_CANCEL);
	// charging the whole way never powers off
	CHECK(feed(&g, &now, 1, 1, 20, NULL) == PP_GUARD_NONE);
	// unplugged, it takes 3 readings again, then a whole countdown
	CHECK(feed(&g, &now, 2, 0, 2, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 2, 0, 1, NULL) == PP_GUARD_COUNTDOWN);
	// a good reading above the power-off level stops it too
	CHECK(feed(&g, &now, 3, 0, 1, NULL) == PP_GUARD_CANCEL);
	CHECK(feed(&g, &now, 3, 0, 1, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 2, 0, 2, NULL) == PP_GUARD_NONE);
	CHECK(feed(&g, &now, 2, 0, 1, NULL) == PP_GUARD_COUNTDOWN);
	// a sleep starts it over
	PpGuard_resumed(&g);
	CHECK(feed(&g, &now, 2, 0, 2, NULL) == PP_GUARD_NONE);
	int seconds = 0;
	CHECK(feed(&g, &now, 2, 0, 1, &seconds) == PP_GUARD_COUNTDOWN);
	CHECK(seconds == 30);
}

static void testOffAboveWarn(void) {
	PpGuard g;
	PpGuard_init(&g, 3, 5);
	unsigned long now = 0;
	CHECK(feed(&g, &now, 4, 0, 3, NULL) == PP_GUARD_COUNTDOWN);
}

static void testSleepModel(void) {
	long period = 4 * 3600;
	CHECK(PpSleep_wokeByAlarm(period, period));
	CHECK(PpSleep_wokeByAlarm(period - PP_WAKE_EARLY_S, period));
	CHECK(PpSleep_wokeByAlarm(period + PP_WAKE_LATE_S, period));
	CHECK(!PpSleep_wokeByAlarm(period - PP_WAKE_EARLY_S - 1, period));
	CHECK(!PpSleep_wokeByAlarm(period + PP_WAKE_LATE_S + 1, period));
	CHECK(!PpSleep_wokeByAlarm(60, period));
	CHECK(!PpSleep_wokeByAlarm(0, 0));

	char value[16];
	CHECK(PpSleep_alarmValue(14400, value, sizeof(value)));
	CHECK_STR(value, "+14400");
	CHECK(!PpSleep_alarmValue(0, value, sizeof(value)));
	CHECK(!PpSleep_alarmValue(14400, value, 4));
}

static void testCurve(void) {
	const PpCurve* c = PpCurve_forPlatform("tg5040");
	CHECK(c != NULL);
	CHECK(PpCurve_forPlatform("tg5050") == c || PpCurve_forPlatform("tg5050") != NULL);
	CHECK(PpCurve_forPlatform("h700") != NULL);
	CHECK(PpCurve_forPlatform("desktop") != NULL);
	CHECK(PpCurve_forPlatform("nope") == NULL);
	CHECK(PpCurve_forPlatform(NULL) == NULL);
	// the points rise
	for (int i = 1; i < c->count; i++) {
		CHECK(c->points[i].mv > c->points[i - 1].mv);
		CHECK(c->points[i].percent >= c->points[i - 1].percent);
	}
	// held at its ends
	CHECK(PpCurve_tenths(c, 3000) == 0);
	CHECK(PpCurve_tenths(c, 3550) == 0);
	CHECK(PpCurve_tenths(c, 4150) == 1000);
	CHECK(PpCurve_tenths(c, 4300) == 1000);
	// on a point, and straight between two
	CHECK(PpCurve_tenths(c, 3800) == 500);
	CHECK(PpCurve_tenths(c, 3850) == 570);
	CHECK(PpCurve_tenths(c, 3820) == 515); // halfway from 50% to 53%
	CHECK(PpCurve_tenths(c, 3680) == 195); // halfway from 13% to 26%
	CHECK(PpCurve_tenths(NULL, 3800) == 0);
}

static void testMillivolts(void) {
	CHECK(PpVolts_millivolts(3850000) == 3850);
	CHECK(PpVolts_millivolts(3849600) == 3850);
	CHECK(PpVolts_millivolts(3850) == 3850);
	CHECK(PpVolts_millivolts(0) == 0);
	CHECK(PpVolts_millivolts(-1) == 0);
	CHECK(PpVolts_millivolts(385) == 0);
	CHECK(PpVolts_millivolts(38500000) == 0);
}

static void testVoltsMode(void) {
	CHECK(PpVolts_mode("off") == PP_VOLTS_OFF);
	CHECK(PpVolts_mode("percent") == PP_VOLTS_PERCENT);
	CHECK(PpVolts_mode("volts") == PP_VOLTS_VOLTS);
	CHECK(PpVolts_mode("") == PP_VOLTS_OFF);
	CHECK(PpVolts_mode(NULL) == PP_VOLTS_OFF);
}

static void testSmoothing(void) {
	PpVolts v;
	PpVolts_init(&v, PpCurve_forPlatform("tg5040"));
	char text[32];
	CHECK(PpVolts_percent(&v) == -1);
	PpVolts_text(&v, PP_VOLTS_VOLTS, text, sizeof(text));
	CHECK_STR(text, "");

	// due every 5 seconds
	CHECK(PpVolts_due(&v, 0));
	CHECK(!PpVolts_due(&v, PP_SAMPLE_MS - 1));
	CHECK(PpVolts_due(&v, PP_SAMPLE_MS));

	// a failed reading counts for nothing
	CHECK(!PpVolts_sample(&v, 0, 0));
	CHECK(PpVolts_percent(&v) == -1);

	// the first reading shows as it is
	CHECK(PpVolts_sample(&v, 3850, 0));
	CHECK(PpVolts_percent(&v) == 57);
	PpVolts_text(&v, PP_VOLTS_VOLTS, text, sizeof(text));
	CHECK_STR(text, "3.85 V");
	PpVolts_text(&v, PP_VOLTS_PERCENT, text, sizeof(text));
	CHECK_STR(text, "V 57%");
	PpVolts_text(&v, PP_VOLTS_OFF, text, sizeof(text));
	CHECK_STR(text, "");

	// one reading off by a lot moves it only an eighth of the way
	CHECK(PpVolts_sample(&v, 3770, 0));
	CHECK(v.shown_mv == 3840);
	// readings that jump about either side don't move it
	PpVolts_init(&v, PpCurve_forPlatform("tg5040"));
	PpVolts_sample(&v, 3850, 0);
	for (int i = 0; i < 20; i++) {
		CHECK(!PpVolts_sample(&v, i % 2 ? 3862 : 3838, 0));
		CHECK(v.shown_mv == 3850);
		CHECK(PpVolts_percent(&v) == 57);
	}
	// a steady fall comes through
	for (int i = 0; i < 60; i++) PpVolts_sample(&v, 3700, 0);
	CHECK(v.shown_mv == 3700);
	CHECK(PpVolts_percent(&v) == 26);

	// the average starts again when charging starts, and after a sleep
	CHECK(PpVolts_sample(&v, 4000, 1));
	CHECK(v.shown_mv == 4000);
	PpVolts_resumed(&v);
	CHECK(PpVolts_percent(&v) == -1);
	CHECK(PpVolts_due(&v, 0));
	CHECK(PpVolts_sample(&v, 3800, 1));
	CHECK(PpVolts_percent(&v) == 50);
}

///////////////////////////////////////
// the plugin against a fake host

static int battery = 50, is_charging_now = 0;
static int power_offs = 0;
static char toasted[128];
static int toasts = 0;
static long clock_now = 1000;
static const char* sleep_off_setting = NULL;
static const char* volts_setting = NULL;
static const char* guard_volts_setting = NULL;
static int redraws = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); toasts++; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (sleep_off_setting && !strcmp(key, "sleep_off")) return sleep_off_setting;
	if (volts_setting && !strcmp(key, "volts")) return volts_setting;
	if (guard_volts_setting && !strcmp(key, "guard_volts")) return guard_volts_setting;
	return fallback;
}
static int fakeBattery(void) { return battery; }
static int fakeCharging(void) { return battery < 0 ? -1 : is_charging_now; }
static void fakePowerOff(int reboot) { CHECK(reboot == 0); power_offs++; }
static long fakeClock(void) { return clock_now; }
static void fakeRedraw(void) { redraws++; }
static int fakeTextWidth(int font, const char* text) { (void)font; return (int)strlen(text) * 10; }

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.toast = fakeToast,
	.battery_level = fakeBattery,
	.is_charging = fakeCharging,
	.power_off = fakePowerOff,
	.request_redraw = fakeRedraw,
	.text_width = fakeTextWidth,
};

static void readFile(const char* path, char* out, size_t size) {
	out[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return;
	size_t n = fread(out, 1, size - 1, f);
	out[n] = '\0';
	fclose(f);
}

static void writeFile(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

static void start(void) {
	power_offs = 0;
	toasts = 0;
	toasted[0] = '\0';
	alarm_set = 0;
	sleep_off_pending = 0;
	boot_seconds = fakeClock;
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
}

static void ticks(unsigned long* now, unsigned long until) {
	for (; *now <= until; *now += 100) tick(*now);
}

static void testDesktop(void) {
	battery = -1;
	writeFile(PP_WAKEALARM_PATH, "");
	start();
	CHECK(!supported);
	unsigned long now = 0;
	ticks(&now, 120000);
	beforeSleep();
	afterResume();
	char alarm[32];
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "");
	CHECK(power_offs == 0);
	CHECK(toasts == 0);
	battery = 50;
}

static void testLowBattery(void) {
	battery = 50;
	is_charging_now = 0;
	start();
	unsigned long now = 0;
	ticks(&now, 20000);
	CHECK(toasts == 0);
	battery = 4;
	ticks(&now, 40000);
	CHECK_STR(toasted, "Battery at 4%, charge soon");
	battery = 2;
	ticks(&now, 60000);
	CHECK(strstr(toasted, "Battery at 2%, powering off in") != NULL);
	CHECK(power_offs == 0);
	ticks(&now, 100000);
	CHECK(power_offs == 1);
	ticks(&now, 200000);
	CHECK(power_offs == 1);
}

static void testLowBatteryCharging(void) {
	battery = 1;
	is_charging_now = 1;
	start();
	unsigned long now = 0;
	ticks(&now, 200000);
	CHECK(power_offs == 0);
	CHECK(toasts == 0);
	is_charging_now = 0;
}

static void testSleepOff(void) {
	battery = 50;
	is_charging_now = 0;
	unlink(PP_STAY_ALIVE_PATH);
	writeFile(PP_WAKEALARM_PATH, "");
	sleep_off_setting = "on";
	start();
	CHECK(sleep_period == 4 * 3600);
	char alarm[32];

	// the user wakes it before the alarm
	clock_now = 1000;
	beforeSleep();
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "+14400");
	clock_now += 600;
	afterResume();
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "0");
	unsigned long now = 0;
	ticks(&now, 10000);
	CHECK(power_offs == 0);

	// or long after it, when the alarm didn't wake it
	beforeSleep();
	clock_now += 14400 + 3600;
	afterResume();
	ticks(&now, 20000);
	CHECK(power_offs == 0);

	// the alarm wakes it
	beforeSleep();
	clock_now += 14400 + 5;
	afterResume();
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "0");
	CHECK_STR(toasted, "Asleep for 240 min, powering off");
	tick(now);
	CHECK(power_offs == 0);
	ticks(&now, now + PP_SLEEP_WARN_MS);
	CHECK(power_offs == 1);

	// plugged in while asleep: the alarm wakes it, but it stays on
	start();
	beforeSleep();
	clock_now += 14400;
	is_charging_now = 1;
	afterResume();
	ticks(&now, now + 10000);
	CHECK(power_offs == 0);

	// charging when it sleeps: no alarm
	writeFile(PP_WAKEALARM_PATH, "");
	beforeSleep();
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "");
	is_charging_now = 0;

	// held awake: no alarm
	writeFile(PP_STAY_ALIVE_PATH, "");
	beforeSleep();
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "");
	unlink(PP_STAY_ALIVE_PATH);

	// powering off another way clears a set alarm
	beforeSleep();
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "+14400");
	beforePoweroff();
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "0");
	sleep_off_setting = NULL;
}

static void testSleepOffUnavailable(void) {
	battery = 50;
	// off by default, and turned off
	const char* off[] = { NULL, "off" };
	for (int i = 0; i < 2; i++) {
		writeFile(PP_WAKEALARM_PATH, "");
		sleep_off_setting = off[i];
		start();
		CHECK(sleep_period == 0);
		beforeSleep();
		char alarm[32];
		readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
		CHECK_STR(alarm, "");
	}

	// no wake alarm to set
	unlink(PP_WAKEALARM_PATH);
	sleep_off_setting = "on";
	start();
	CHECK(sleep_period == 0);
	beforeSleep();
	CHECK(access(PP_WAKEALARM_PATH, F_OK) != 0);
	sleep_off_setting = NULL;
}

#define FAKE_VOLTS BUILD_DIR "/pp-fake-volts"

// The desktop, with its test voltage.
static void testVoltsWidget(void) {
	battery = -1;
	fake.platform = "desktop";
	writeFile(FAKE_VOLTS, "3850000\n");
	setenv(PP_FAKE_ENV, FAKE_VOLTS, 1);

	// off by default: nothing shows
	start();
	CHECK(!volts_ready);
	unsigned long now = 0;
	ticks(&now, 10000);
	CHECK(headerWidth(500, 30) == 0);

	volts_setting = "volts";
	redraws = 0;
	start();
	CHECK(volts_ready);
	CHECK(!supported);
	CHECK(headerWidth(500, 30) == 0); // before the first reading
	now = 0;
	tick(now);
	CHECK_STR(shown_text, "3.85 V");
	CHECK(redraws == 1);
	CHECK(headerWidth(500, 30) == 60 + 24);
	CHECK(headerWidth(50, 30) == 0);
	// read every 5 seconds, not every tick
	writeFile(FAKE_VOLTS, "3700000\n");
	ticks(&now, 4900);
	CHECK_STR(shown_text, "3.85 V");
	ticks(&now, 5000);
	CHECK_STR(shown_text, "3.83 V"); // an eighth of the way

	volts_setting = "percent";
	writeFile(FAKE_VOLTS, "3800\n");
	start();
	now = 0;
	tick(now);
	CHECK_STR(shown_text, "V 50%");

	// only on desktop: a device reads its power supply
	fake.platform = "tg5040";
	unlink(PP_VOLTAGE_PATH);
	unlink(PP_POWER_SUPPLY_DIR "/usb/type");
	unlink(PP_POWER_SUPPLY_DIR "/usb/voltage_now");
	unlink(PP_POWER_SUPPLY_DIR "/battery/type");
	unlink(PP_POWER_SUPPLY_DIR "/battery/voltage_now");
	start();
	CHECK(!volts_ready);
	mkdir(PP_POWER_SUPPLY_DIR, 0755);
	mkdir(PP_POWER_SUPPLY_DIR "/usb", 0755);
	writeFile(PP_POWER_SUPPLY_DIR "/usb/type", "USB\n");
	writeFile(PP_POWER_SUPPLY_DIR "/usb/voltage_now", "5000000\n");
	mkdir(PP_POWER_SUPPLY_DIR "/battery", 0755);
	writeFile(PP_POWER_SUPPLY_DIR "/battery/type", "Battery\n");
	writeFile(PP_POWER_SUPPLY_DIR "/battery/voltage_now", "3900000\n");
	start();
	CHECK(volts_ready);
	CHECK_STR(volts_path, PP_POWER_SUPPLY_DIR "/battery/voltage_now");
	// the AXP2202's own first
	writeFile(PP_VOLTAGE_PATH, "4000000\n");
	start();
	CHECK_STR(volts_path, PP_VOLTAGE_PATH);
	now = 0;
	tick(now);
	CHECK_STR(shown_text, "V 82%"); // 82.3% between 3.98 V at 79% and 4.01 V at 84%
	unlink(PP_VOLTAGE_PATH);
	fake.platform = "desktop";

	// and with no file named
	unsetenv(PP_FAKE_ENV);
	start();
	CHECK(!volts_ready);
	volts_setting = NULL;
	fake.platform = NULL;
	battery = 50;
}

// The guard going by the voltage's percentage.
static void testGuardVolts(void) {
	battery = 50;
	is_charging_now = 0;
	fake.platform = "desktop";
	setenv(PP_FAKE_ENV, FAKE_VOLTS, 1);
	writeFile(FAKE_VOLTS, "3640\n"); // 10%
	guard_volts_setting = "on";
	start();
	CHECK(guard_volts);
	unsigned long now = 0;
	ticks(&now, 20000);
	CHECK(toasts == 0); // stock's 50% doesn't matter, nor does 10%
	writeFile(FAKE_VOLTS, "3590\n"); // 5%
	ticks(&now, 300000);
	CHECK_STR(toasted, "Battery at 5%, charge soon");

	// without a voltage it goes by stock's
	unsetenv(PP_FAKE_ENV);
	battery = 4;
	start();
	CHECK(!guard_volts);
	now = 0;
	ticks(&now, 20000);
	CHECK_STR(toasted, "Battery at 4%, charge soon");
	guard_volts_setting = NULL;
	fake.platform = NULL;
	battery = 50;
}

int main(void) {
	testCurve();
	testMillivolts();
	testVoltsMode();
	testSmoothing();
	testParseNumber();
	testDue();
	testWarning();
	testOneBadReading();
	testCountdown();
	testCountdownCancelled();
	testOffAboveWarn();
	testSleepModel();
	testDesktop();
	testLowBattery();
	testLowBatteryCharging();
	testSleepOff();
	testSleepOffUnavailable();
	testVoltsWidget();
	testGuardVolts();
	CHECK_DONE();
}
