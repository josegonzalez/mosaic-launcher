#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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
