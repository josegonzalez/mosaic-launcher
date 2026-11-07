#ifndef POWER_MODEL_H
#define POWER_MODEL_H

#include <stddef.h>

// What power plus decides, apart from the host, the clock and the card, so
// the host tests can check it on its own.

///////////////////////////////////////
// The low battery guard

#define PP_SAMPLE_MS 5000       // how often to read the battery; the launcher refreshes it every 5 seconds
#define PP_SAMPLES 3            // readings in a row it takes to warn or start the countdown
#define PP_COUNTDOWN_MS 30000   // from the power-off warning to the power-off
#define PP_REARM_MARGIN 2       // the warning comes back once the battery is this far above it, or charging

enum PpGuardEvent {
	PP_GUARD_NONE,
	PP_GUARD_WARN,      // the battery is low: warn once
	PP_GUARD_COUNTDOWN, // the battery is at the power-off level: say when it powers off
	PP_GUARD_CANCEL,    // charging, or a reading above the power-off level, stopped the countdown
	PP_GUARD_POWER_OFF, // the countdown ran out on a low reading: power off now
};

typedef struct PpGuard {
	int warn_at;  // percent
	int off_at;   // percent
	unsigned long next_sample;
	int warn_count; // readings in a row at or below warn_at
	int low_count;  // readings in a row at or below off_at
	int warned;
	int counting;   // the countdown is running
	unsigned long off_at_ms; // when the countdown runs out
	int done;       // asked to power off
} PpGuard;

void PpGuard_init(PpGuard* g, int warn_at, int off_at);

// 1 when it's time to read the battery again. Read it, then call
// PpGuard_sample.
int PpGuard_due(PpGuard* g, unsigned long now);

// One battery reading: level from 0 to 100 and charging 1 or 0, as the host
// returns them. A level outside 0 to 100 is a bad reading and counts for
// nothing. Returns an enum PpGuardEvent; *seconds_left is the time to the
// power-off for PP_GUARD_COUNTDOWN.
int PpGuard_sample(PpGuard* g, unsigned long now, int level, int charging, int* seconds_left);

// After a sleep: start counting readings again, as the battery may have
// changed while the device slept. A countdown starts over too.
void PpGuard_resumed(PpGuard* g);

///////////////////////////////////////
// Power-off after a period asleep

#define PP_WAKE_EARLY_S 60  // an alarm wake may come this much before the period by the boot clock
#define PP_WAKE_LATE_S 120  // or this much after it, while the device resumes
#define PP_SLEEP_WARN_MS 3000 // from the "powering off" message to the power-off

// 1 when a sleep of slept seconds ended at the wake alarm set for period
// seconds, so the device woke on its own and should power off. A wake well
// before or after it was the user's.
int PpSleep_wokeByAlarm(long slept, long period);

// What to write to the RTC's wakealarm for an alarm in seconds from now.
// Returns 1, or 0 when it doesn't fit.
int PpSleep_alarmValue(long seconds, char* out, size_t size);

///////////////////////////////////////
// Settings

// A whole number from a setting such as "5", "5%" or " 12 ", or fallback
// when it isn't one or is outside min to max.
int Pp_parseNumber(const char* setting, int min, int max, int fallback);

#endif
