#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "power_model.h"

///////////////////////////////////////
// The low battery guard

void PpGuard_init(PpGuard* g, int warn_at, int off_at) {
	*g = (PpGuard){0};
	g->warn_at = warn_at;
	g->off_at = off_at;
}

int PpGuard_due(PpGuard* g, unsigned long now) {
	if (g->done || now < g->next_sample) return 0;
	g->next_sample = now + PP_SAMPLE_MS;
	return 1;
}

static int cancel(PpGuard* g) {
	g->low_count = 0;
	if (!g->counting) return PP_GUARD_NONE;
	g->counting = 0;
	return PP_GUARD_CANCEL;
}

int PpGuard_sample(PpGuard* g, unsigned long now, int level, int charging, int* seconds_left) {
	if (g->done) return PP_GUARD_NONE;
	if (charging == 1) {
		g->warn_count = 0;
		g->warned = 0;
		return cancel(g);
	}
	// a failed read, which counts neither way: a countdown waits for a good one
	if (level < 0 || level > 100) return PP_GUARD_NONE;

	if (level > g->off_at) {
		int event = cancel(g);
		if (level > g->warn_at) {
			g->warn_count = 0;
			if (level > g->warn_at + PP_REARM_MARGIN) g->warned = 0;
			return event;
		}
		if (event != PP_GUARD_NONE) return event;
		if (++g->warn_count >= PP_SAMPLES && !g->warned) {
			g->warned = 1;
			return PP_GUARD_WARN;
		}
		return PP_GUARD_NONE;
	}

	// at or below the power-off level
	g->warn_count++;
	if (!g->counting) {
		if (++g->low_count < PP_SAMPLES) return PP_GUARD_NONE;
		g->counting = 1;
		g->warned = 1; // the countdown is the warning
		g->off_at_ms = now + PP_COUNTDOWN_MS;
	}
	else if (now >= g->off_at_ms) {
		g->counting = 0;
		g->done = 1;
		return PP_GUARD_POWER_OFF;
	}
	if (seconds_left) *seconds_left = (int)((g->off_at_ms - now + 999) / 1000);
	return PP_GUARD_COUNTDOWN;
}

void PpGuard_resumed(PpGuard* g) {
	g->warn_count = 0;
	g->low_count = 0;
	g->counting = 0;
	g->next_sample = 0;
}

///////////////////////////////////////
// Power-off after a period asleep

int PpSleep_wokeByAlarm(long slept, long period) {
	if (period <= 0) return 0;
	return slept >= period - PP_WAKE_EARLY_S && slept <= period + PP_WAKE_LATE_S;
}

int PpSleep_alarmValue(long seconds, char* out, size_t size) {
	if (seconds <= 0 || !out) return 0;
	// a leading + sets it relative to now
	int n = snprintf(out, size, "+%ld", seconds);
	return n > 0 && (size_t)n < size;
}

///////////////////////////////////////
// The battery from its voltage

// X-Powers' default open-circuit voltage curve for its AXP fuel gauges, as
// Allwinner's kernel sets it (drivers/power/axp_power/axp-cfg.h, OCVREG0
// to OCVREG1F), from 3.55 V, the last point at 0%. Each device NextUI
// runs on reads its battery from the PMIC's axp2202-battery supply.
static const PpCurvePoint axp_default[] = {
	{3550, 0}, {3570, 4}, {3590, 5}, {3610, 6}, {3630, 7}, {3640, 10}, {3660, 13},
	{3700, 26}, {3730, 36}, {3770, 41}, {3780, 46}, {3800, 50}, {3840, 53}, {3850, 57},
	{3870, 61}, {3910, 67}, {3940, 73}, {3980, 79}, {4010, 84}, {4050, 88}, {4080, 92},
	{4100, 94}, {4120, 96}, {4140, 98}, {4150, 100},
};

#define CURVE(name, points) { name, points, (int)(sizeof(points) / sizeof(points[0])) }

static const struct {
	const char* platform;
	PpCurve curve;
} curves[] = {
	{ "tg5040", CURVE("axp-default", axp_default) },  // TrimUI Smart Pro and Brick
	{ "tg5050", CURVE("axp-default", axp_default) },  // TrimUI Smart Pro S
	{ "h700", CURVE("axp-default", axp_default) },    // Anbernic's H700 devices
	{ "desktop", CURVE("axp-default", axp_default) }, // for its test voltage
};

const PpCurve* PpCurve_forPlatform(const char* platform) {
	if (!platform) return NULL;
	for (size_t i = 0; i < sizeof(curves) / sizeof(curves[0]); i++) {
		if (!strcmp(curves[i].platform, platform)) return &curves[i].curve;
	}
	return NULL;
}

int PpCurve_tenths(const PpCurve* curve, int mv) {
	if (!curve || curve->count < 1) return 0;
	const PpCurvePoint* p = curve->points;
	if (mv <= p[0].mv) return p[0].percent * 10;
	for (int i = 1; i < curve->count; i++) {
		if (mv > p[i].mv) continue;
		int span = p[i].mv - p[i - 1].mv;
		int rise = (p[i].percent - p[i - 1].percent) * 10;
		int base = p[i - 1].percent * 10;
		if (span <= 0) return p[i].percent * 10;
		return base + (rise * (mv - p[i - 1].mv) + span / 2) / span;
	}
	return p[curve->count - 1].percent * 10;
}

int PpVolts_millivolts(long raw) {
	// a single cell reads between about 2.5 V and 4.5 V
	if (raw >= 1000000 && raw <= 6000000) return (int)((raw + 500) / 1000);
	if (raw >= 1000 && raw <= 6000) return (int)raw;
	return 0;
}

void PpVolts_init(PpVolts* v, const PpCurve* curve) {
	*v = (PpVolts){0};
	v->curve = curve;
}

int PpVolts_due(PpVolts* v, unsigned long now) {
	if (now < v->next_sample) return 0;
	v->next_sample = now + PP_SAMPLE_MS;
	return 1;
}

static int roundToStep(int mv) {
	return (mv + PP_VOLTS_STEP_MV / 2) / PP_VOLTS_STEP_MV * PP_VOLTS_STEP_MV;
}

int PpVolts_sample(PpVolts* v, int mv, int charging) {
	if (mv <= 0) return 0;
	charging = charging == 1;
	int restart = !v->have || charging != v->charging;
	v->charging = charging;
	if (restart) v->avg = (long)mv * 16;
	else v->avg += ((long)mv * 16 - v->avg) / PP_VOLTS_SMOOTH;

	int avg_mv = (int)((v->avg + 8) / 16);
	int tenths = PpCurve_tenths(v->curve, avg_mv);
	int changed = 0;
	if (restart || abs(avg_mv - v->shown_mv) >= PP_VOLTS_STEP_MV) {
		int next = roundToStep(avg_mv);
		changed |= next != v->shown_mv;
		v->shown_mv = next;
	}
	if (restart || abs(tenths - v->shown_percent * 10) >= PP_PERCENT_STEP) {
		int next = (tenths + 5) / 10;
		changed |= next != v->shown_percent;
		v->shown_percent = next;
	}
	if (!v->have) changed = 1;
	v->have = 1;
	return changed;
}

void PpVolts_resumed(PpVolts* v) {
	v->have = 0;
	v->next_sample = 0;
}

int PpVolts_percent(const PpVolts* v) {
	return v->have ? v->shown_percent : -1;
}

void PpVolts_text(const PpVolts* v, int mode, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!v->have) return;
	if (mode == PP_VOLTS_PERCENT) snprintf(out, size, "V %d%%", v->shown_percent);
	else if (mode == PP_VOLTS_VOLTS) snprintf(out, size, "%d.%02d V", v->shown_mv / 1000, v->shown_mv % 1000 / 10);
}

int PpVolts_mode(const char* setting) {
	if (setting && !strcmp(setting, "percent")) return PP_VOLTS_PERCENT;
	if (setting && !strcmp(setting, "volts")) return PP_VOLTS_VOLTS;
	return PP_VOLTS_OFF;
}

///////////////////////////////////////
// Settings

int Pp_parseNumber(const char* setting, int min, int max, int fallback) {
	if (!setting) return fallback;
	while (isspace((unsigned char)*setting)) setting++;
	if (!isdigit((unsigned char)*setting)) return fallback;
	char* end;
	long value = strtol(setting, &end, 10);
	if (*end == '%') end++;
	while (isspace((unsigned char)*end)) end++;
	if (*end || value < min || value > max) return fallback;
	return (int)value;
}
