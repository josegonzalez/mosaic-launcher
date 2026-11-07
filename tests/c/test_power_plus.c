// The power plus plugin's model, and the plugin against a fake host.

#include <stdlib.h>
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

///////////////////////////////////////
// the plugin against a fake host

static int battery = 50, is_charging_now = 0;
static int power_offs = 0;
static char toasted[128];
static int toasts = 0;
static long clock_now = 1000;
static const char* sleep_off_setting = NULL;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); toasts++; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (sleep_off_setting && !strcmp(key, "sleep_off")) return sleep_off_setting;
	return fallback;
}
static int fakeBattery(void) { return battery; }
static int fakeCharging(void) { return battery < 0 ? -1 : is_charging_now; }
static void fakePowerOff(int reboot) { CHECK(reboot == 0); power_offs++; }
static long fakeClock(void) { return clock_now; }

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.toast = fakeToast,
	.battery_level = fakeBattery,
	.is_charging = fakeCharging,
	.power_off = fakePowerOff,
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
}

static void testSleepOffUnavailable(void) {
	battery = 50;
	// turned off
	writeFile(PP_WAKEALARM_PATH, "");
	sleep_off_setting = "off";
	start();
	CHECK(sleep_period == 0);
	beforeSleep();
	char alarm[32];
	readFile(PP_WAKEALARM_PATH, alarm, sizeof(alarm));
	CHECK_STR(alarm, "");
	sleep_off_setting = NULL;

	// no wake alarm to set
	unlink(PP_WAKEALARM_PATH);
	start();
	CHECK(sleep_period == 0);
	beforeSleep();
	CHECK(access(PP_WAKEALARM_PATH, F_OK) != 0);
}

int main(void) {
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
	CHECK_DONE();
}
