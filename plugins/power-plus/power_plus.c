// Power plus: a low battery guard, and power-off after a period asleep.
//
// The guard reads the battery every 5 seconds while the launcher is on
// screen. When it has been at or below the warn_at level for 3 readings in
// a row it warns once. At or below off_at for 3 readings in a row it counts
// down 30 seconds, saying so, and then powers off through the host, so
// every plugin's before_poweroff runs. Charging, or a reading above off_at,
// stops the countdown, and a failed reading counts for nothing, so one bad
// reading can't power the device off. It never powers off while charging.
//
// Nothing in the launcher runs while the device sleeps: stock's sleep loop
// holds the main thread, and in deep sleep the whole device is suspended.
// So before a sleep it sets the RTC's wake alarm for the period, and when
// the device wakes at that alarm, which it can tell by how long it slept on
// the boot clock, it says so and powers off. A wake by the user, sooner or
// later than the alarm, resumes as usual, and the alarm is cleared. It
// doesn't power off while charging, or while /tmp/stay_awake or
// /tmp/stay_alive exists. Without a writable wake alarm this part is off.
// It's off by default too, until the wake alarm has been checked on a
// device.
//
// Stock's own screen and suspend timeouts still decide when the device
// sleeps and goes into deep sleep. On a platform without a battery, such as
// desktop, it loads and does nothing.

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "power_model.h"

#define ID "power-plus"

#ifndef PP_WAKEALARM_PATH
#define PP_WAKEALARM_PATH "/sys/class/rtc/rtc0/wakealarm"
#endif
#ifndef PP_STAY_AWAKE_PATH
#define PP_STAY_AWAKE_PATH "/tmp/stay_awake"
#endif
#ifndef PP_STAY_ALIVE_PATH
#define PP_STAY_ALIVE_PATH "/tmp/stay_alive"
#endif

// as in plugin.txt
#define PP_WARN_DEFAULT 5
#define PP_OFF_DEFAULT 2
#define PP_HOURS_DEFAULT 4

#if defined(__linux__) && !defined(CLOCK_BOOTTIME)
#define CLOCK_BOOTTIME 7
#endif

static const MosaicHost* host;
static int supported = 0;
static int guard_on = 0;
static PpGuard guard;
static long sleep_period = 0;  // seconds, 0 when power-off after sleep is off
static int alarm_set = 0;
static long asleep_at = 0;     // the boot clock at before_sleep
static int sleep_off_pending = 0;
static unsigned long sleep_off_at = 0;

// Seconds on a clock that keeps counting while the device is suspended.
static long bootSeconds(void) {
	struct timespec ts;
#ifdef CLOCK_BOOTTIME
	if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) return (long)ts.tv_sec;
#endif
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec;
}
static long (*boot_seconds)(void) = bootSeconds;

static void say(const char* message) {
	host->log(ID, message);
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int isOn(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(ID, key, fallback), "off") != 0;
}

static int charging(void) {
	return host->is_charging() == 1;
}

static int stayingAlive(void) {
	return access(PP_STAY_AWAKE_PATH, F_OK) == 0 || access(PP_STAY_ALIVE_PATH, F_OK) == 0;
}

///////////////////////////////////////
// the wake alarm

static int writeAlarm(const char* value) {
	FILE* f = fopen(PP_WAKEALARM_PATH, "w");
	if (!f) return 0;
	int ok = fputs(value, f) >= 0;
	// sysfs reports a refused value when the write is flushed
	if (fclose(f) != 0) ok = 0;
	return ok;
}

static void clearAlarm(void) {
	if (!alarm_set) return;
	alarm_set = 0;
	if (!writeAlarm("0")) say("couldn't clear the wake alarm");
}

static void setAlarm(void) {
	char value[32];
	if (!PpSleep_alarmValue(sleep_period, value, sizeof(value))) return;
	// a set alarm has to be cleared before another is set
	writeAlarm("0");
	if (!writeAlarm(value)) {
		say("couldn't set the wake alarm, so this sleep won't power off");
		return;
	}
	alarm_set = 1;
}

///////////////////////////////////////
// hooks

static void tick(unsigned long now) {
	if (!supported) return;

	if (sleep_off_pending) {
		if (!sleep_off_at) sleep_off_at = now + PP_SLEEP_WARN_MS;
		if (now >= sleep_off_at) {
			sleep_off_pending = 0;
			if (charging()) say("charging, so not powering off after the sleep");
			else host->power_off(0);
		}
	}

	if (!guard_on || !PpGuard_due(&guard, now)) return;
	int level = host->battery_level();
	int seconds = 0;
	char message[128];
	switch (PpGuard_sample(&guard, now, level, charging(), &seconds)) {
		case PP_GUARD_WARN:
			snprintf(message, sizeof(message), "Battery at %d%%, charge soon", level);
			toast(message);
			say(message);
			break;
		case PP_GUARD_COUNTDOWN:
			snprintf(message, sizeof(message), "Battery at %d%%, powering off in %ds", level, seconds);
			toast(message);
			break;
		case PP_GUARD_CANCEL:
			toast("Power off cancelled");
			say("low battery power off cancelled");
			break;
		case PP_GUARD_POWER_OFF:
			snprintf(message, sizeof(message), "battery at %d%%, powering off", level);
			say(message);
			host->power_off(0);
			break;
	}
}

static void beforeSleep(void) {
	if (!supported || !sleep_period) return;
	if (charging() || stayingAlive()) return;
	asleep_at = boot_seconds();
	setAlarm();
}

static void afterResume(void) {
	if (!supported) return;
	PpGuard_resumed(&guard);
	if (!alarm_set) return;
	clearAlarm();
	long slept = boot_seconds() - asleep_at;
	if (!PpSleep_wokeByAlarm(slept, sleep_period)) return;
	if (charging() || stayingAlive()) {
		say("woke at the wake alarm, but charging or held awake, so not powering off");
		return;
	}
	char message[128];
	snprintf(message, sizeof(message), "Asleep for %ld min, powering off", slept / 60);
	toast(message);
	say(message);
	sleep_off_pending = 1;
	sleep_off_at = 0;
}

// An alarm left set could wake a device that's off.
static void beforePoweroff(void) {
	clearAlarm();
}

static void quit(void) {
	clearAlarm();
}

static int init(const MosaicHost* h) {
	host = h;
	supported = 0;
	if (!MOSAIC_HOST_HAS(host, battery_level) || !MOSAIC_HOST_HAS(host, is_charging) || !MOSAIC_HOST_HAS(host, power_off)) {
		say("unsupported: this launcher can't read the battery or power off");
		return 0;
	}
	if (host->battery_level() < 0) {
		say("unsupported on this platform: it has no battery");
		return 0;
	}
	supported = 1;

	guard_on = isOn("guard", "on");
	int warn_at = Pp_parseNumber(host->cfg_get(ID, "warn_at", "5"), 1, 99, PP_WARN_DEFAULT);
	int off_at = Pp_parseNumber(host->cfg_get(ID, "off_at", "2"), 1, 99, PP_OFF_DEFAULT);
	if (off_at >= warn_at) say("off_at isn't below warn_at, so the power off countdown is the only warning");
	PpGuard_init(&guard, warn_at, off_at);

	sleep_period = 0;
	if (isOn("sleep_off", "off")) {
		if (access(PP_WAKEALARM_PATH, W_OK) == 0) {
			sleep_period = 3600L * Pp_parseNumber(host->cfg_get(ID, "sleep_hours", "4"), 1, 72, PP_HOURS_DEFAULT);
		}
		else {
			say("no writable wake alarm, so power off after sleep is off");
		}
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.before_sleep = beforeSleep,
	.after_resume = afterResume,
	.before_poweroff = beforePoweroff,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
