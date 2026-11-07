#include "powerwatch.h"

// The times bracket PWR_update's own: frame_at is taken before it reads the
// time, and a press is timed from after it returns, so a held press here is
// never longer than stock's. That keeps a borderline press on the sleep side.

static int held(const PowerWatch* pw) {
	return pw->pressed_at && pw->frame_at >= pw->pressed_at &&
		pw->frame_at - pw->pressed_at >= POWERWATCH_HOLD_MS;
}

void PowerWatch_before(PowerWatch* pw, unsigned long now) {
	pw->frame_at = now;
	pw->woke = 0;
}

int PowerWatch_isPowerOff(const PowerWatch* pw, int poweroff_released) {
	return poweroff_released || held(pw);
}

void PowerWatch_after(PowerWatch* pw, unsigned long now, int power_pressed) {
	// held this long and stock didn't power off, so it isn't timing a press
	if (held(pw)) pw->pressed_at = 0;
	// a sleep in this update clears stock's press after reading it
	if (pw->woke || !power_pressed) return;
	if (pw->frame_at < pw->resumed_at || pw->frame_at - pw->resumed_at < POWERWATCH_RESUME_MS) {
		pw->pressed_at = 0;
	}
	else {
		pw->pressed_at = now ? now : 1;
	}
}

void PowerWatch_sleeping(PowerWatch* pw) {
	pw->asleep = 1;
}

void PowerWatch_resumed(PowerWatch* pw, unsigned long now) {
	pw->asleep = 0;
	pw->woke = 1;
	pw->pressed_at = 0;
	pw->resumed_at = now;
}

void PowerWatch_poweredOff(PowerWatch* pw) {
	pw->powered_off = 1;
}

int PowerWatch_exitNeedsPoweroff(const PowerWatch* pw) {
	return pw->asleep && !pw->powered_off;
}
