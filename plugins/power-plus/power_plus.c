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
// sleeps and goes into deep sleep.
//
// With its volts setting on, it also reads the battery's voltage on the
// same 5 second timer, smooths it, and shows it as a header widget beside
// stock's percentage: as a percentage along the device's curve, or as the
// voltage itself. The guard can go by that percentage in place of stock's,
// with guard_volts on.
//
// On a platform without a battery, such as desktop, it loads and does
// nothing. For the tests and screenshots only, on desktop,
// MOSAIC_POWER_PLUS_FAKE_VOLTS names a file holding a voltage in mV or uV,
// such as "3850", read in place of the battery's.

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
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

#ifndef PP_VOLTAGE_PATH
#define PP_VOLTAGE_PATH "/sys/class/power_supply/axp2202-battery/voltage_now"
#endif
#ifndef PP_POWER_SUPPLY_DIR
#define PP_POWER_SUPPLY_DIR "/sys/class/power_supply"
#endif
#define PP_FAKE_ENV "MOSAIC_POWER_PLUS_FAKE_VOLTS"
#define FONT MOSAIC_FONT_SMALL

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
static int volts_mode = PP_VOLTS_OFF;
static int guard_volts = 0;    // the guard goes by the voltage's percentage
static int volts_ready = 0;    // there's a voltage to read
static char volts_path[512];
static PpVolts volts;
static char shown_text[32];
static MosaicRect logged_rect;
static char logged_text[32];

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

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int isOn(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(key, fallback), "off") != 0;
}

static int charging(void) {
	return MOSAIC_HOST_HAS(host, is_charging) && host->is_charging() == 1;
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
// the voltage

static long readLong(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return -1;
	long value = -1;
	if (fscanf(f, "%ld", &value) != 1) value = -1;
	fclose(f);
	return value;
}

static int readable(const char* path) {
	return PpVolts_millivolts(readLong(path)) > 0;
}

// The battery's voltage_now: the AXP2202's, as stock reads its charge from
// there, else the first power supply of type Battery that has one.
static int findVoltage(void) {
	volts_path[0] = '\0';
	if (host->platform && !strcmp(host->platform, "desktop")) {
		const char* fake = getenv(PP_FAKE_ENV);
		if (!fake || !*fake) return 0;
		snprintf(volts_path, sizeof(volts_path), "%s", fake);
		return 1;
	}
	if (readable(PP_VOLTAGE_PATH)) {
		snprintf(volts_path, sizeof(volts_path), "%s", PP_VOLTAGE_PATH);
		return 1;
	}
	DIR* dir = opendir(PP_POWER_SUPPLY_DIR);
	if (!dir) return 0;
	struct dirent* e;
	while ((e = readdir(dir))) {
		if (e->d_name[0] == '.') continue;
		char path[512], type[32] = "";
		snprintf(path, sizeof(path), "%s/%s/type", PP_POWER_SUPPLY_DIR, e->d_name);
		FILE* f = fopen(path, "r");
		if (!f) continue;
		if (!fgets(type, sizeof(type), f)) type[0] = '\0';
		fclose(f);
		if (strncmp(type, "Battery", 7) != 0) continue;
		snprintf(path, sizeof(path), "%s/%s/voltage_now", PP_POWER_SUPPLY_DIR, e->d_name);
		if (!readable(path)) continue;
		snprintf(volts_path, sizeof(volts_path), "%s", path);
		closedir(dir);
		return 1;
	}
	closedir(dir);
	return 0;
}

static void sampleVolts(void) {
	int mv = PpVolts_millivolts(readLong(volts_path));
	if (!PpVolts_sample(&volts, mv, charging())) return;
	char text[sizeof(shown_text)];
	PpVolts_text(&volts, volts_mode, text, sizeof(text));
	char message[128];
	snprintf(message, sizeof(message), "voltage %d.%02d V, %d%%", volts.shown_mv / 1000, volts.shown_mv % 1000 / 10, volts.shown_percent);
	say(message);
	if (strcmp(text, shown_text) == 0) return;
	snprintf(shown_text, sizeof(shown_text), "%s", text);
	if (MOSAIC_HOST_HAS(host, request_redraw)) host->request_redraw();
}

static int scale(void) {
	int s = MOSAIC_HOST_HAS(host, screen_scale) ? host->screen_scale() : 1;
	return s > 0 ? s : 1;
}

static int pad(void) {
	return 12 * scale(); // stock's BUTTON_PADDING, as its pills use
}

// From what the last reading left, never reading the battery here.
static int headerWidth(int max_width, int height) {
	(void)height;
	if (!shown_text[0] || !MOSAIC_HOST_HAS(host, text_width)) return 0;
	int text = host->text_width(FONT, shown_text);
	if (text < 0) return -1;
	int width = text + pad() * 2;
	return width <= max_width ? width : 0;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	int pill = host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	host->fill_round_rect(screen, rect, rect->h / 2, mosaic_color((unsigned int)pill, 255));
	int text_y = rect->y + (rect->h - host->text_height(FONT)) / 2;
	host->draw_text(screen, FONT, shown_text, rect->x + pad(), text_y, mosaic_color((unsigned int)ink, 255), rect->w - pad() * 2);

	if (memcmp(rect, &logged_rect, sizeof(*rect)) != 0 || strcmp(shown_text, logged_text) != 0) {
		char message[128];
		snprintf(message, sizeof(message), "widget at %i,%i %ix%i: %s", rect->x, rect->y, rect->w, rect->h, shown_text);
		say(message);
		logged_rect = *rect;
		snprintf(logged_text, sizeof(logged_text), "%s", shown_text);
	}
	return 0;
}

///////////////////////////////////////
// hooks

static void tick(unsigned long now) {
	if (volts_ready && PpVolts_due(&volts, now)) sampleVolts();

	if (supported && sleep_off_pending) {
		if (!sleep_off_at) sleep_off_at = now + PP_SLEEP_WARN_MS;
		if (now >= sleep_off_at) {
			sleep_off_pending = 0;
			if (charging()) say("charging, so not powering off after the sleep");
			else host->power_off(0);
		}
	}

	if (!guard_on || !PpGuard_due(&guard, now)) return;
	int level = guard_volts ? PpVolts_percent(&volts) : host->battery_level();
	int seconds = 0;
	char message[256];
	switch (PpGuard_sample(&guard, now, level, charging(), &seconds)) {
		case PP_GUARD_WARN:
			snprintf(message, sizeof(message), tr("Battery at %d%%, charge soon"), level);
			toast(message);
			snprintf(message, sizeof(message), "Battery at %d%%, charge soon", level);
			say(message);
			break;
		case PP_GUARD_COUNTDOWN:
			snprintf(message, sizeof(message), tr("Battery at %d%%, powering off in %ds"), level, seconds);
			toast(message);
			break;
		case PP_GUARD_CANCEL:
			toast(tr("Power off cancelled"));
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
	PpVolts_resumed(&volts);
	PpGuard_resumed(&guard);
	if (!supported) return;
	if (!alarm_set) return;
	clearAlarm();
	long slept = boot_seconds() - asleep_at;
	if (!PpSleep_wokeByAlarm(slept, sleep_period)) return;
	if (charging() || stayingAlive()) {
		say("woke at the wake alarm, but charging or held awake, so not powering off");
		return;
	}
	char message[256];
	snprintf(message, sizeof(message), tr("Asleep for %ld min, powering off"), slept / 60);
	toast(message);
	snprintf(message, sizeof(message), "Asleep for %ld min, powering off", slept / 60);
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
	guard_on = 0;
	shown_text[0] = '\0';
	logged_text[0] = '\0';
	logged_rect = (MosaicRect){0};

	volts_mode = PpVolts_mode(host->cfg_get("volts", "off"));
	guard_volts = isOn("guard_volts", "off");
	volts_ready = 0;
	const PpCurve* curve = PpCurve_forPlatform(host->platform);
	if (volts_mode != PP_VOLTS_OFF || guard_volts) {
		if (!curve) say("no voltage curve for this platform, so the voltage readout is off");
		else if (!findVoltage()) say("can't read the battery's voltage, so the voltage readout is off");
		else volts_ready = 1;
	}
	PpVolts_init(&volts, curve);
	if (volts_ready) {
		char message[600];
		snprintf(message, sizeof(message), "voltage from %s, curve %s", volts_path, curve->name);
		say(message);
	}
	if (guard_volts && !volts_ready) {
		say("the guard goes by stock's percentage, as there's no voltage to read");
		guard_volts = 0;
	}

	if (!MOSAIC_HOST_HAS(host, battery_level) || !MOSAIC_HOST_HAS(host, is_charging) || !MOSAIC_HOST_HAS(host, power_off)) {
		say("unsupported: this launcher can't read the battery or power off");
		guard_volts = 0;
		return 0;
	}
	if (host->battery_level() < 0) {
		say("unsupported on this platform: it has no battery");
		// the desktop's test voltage still shows, and its guard still runs
		if (!guard_volts) return 0;
	}
	else {
		supported = 1;
	}

	guard_on = isOn("guard", "on");
	int warn_at = Pp_parseNumber(host->cfg_get("warn_at", "5"), 1, 99, PP_WARN_DEFAULT);
	int off_at = Pp_parseNumber(host->cfg_get("off_at", "2"), 1, 99, PP_OFF_DEFAULT);
	if (off_at >= warn_at) say("off_at isn't below warn_at, so the power off countdown is the only warning");
	PpGuard_init(&guard, warn_at, off_at);

	sleep_period = 0;
	if (supported && isOn("sleep_off", "off")) {
		if (access(PP_WAKEALARM_PATH, W_OK) == 0) {
			sleep_period = 3600L * Pp_parseNumber(host->cfg_get("sleep_hours", "4"), 1, 72, PP_HOURS_DEFAULT);
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
	.header_width = headerWidth,
	.header_widget = headerWidget,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
