#include "launcher.h"
#include "powerwatch.h"

static PowerWatch watch;

void Power_beforePoweroff(void) {
	if (watch.powered_off) return;
	PowerWatch_poweredOff(&watch);
	Plugins_started(); // powering off before the first frame isn't a crash
	Plugins_beforePoweroff();
}

// What PWR_update calls before a sleep, and before it powers off for the
// power button.
static void beforeSleep(void) {
	if (PowerWatch_isPowerOff(&watch, PAD_justReleased(BTN_POWEROFF))) {
		Power_beforePoweroff();
		return;
	}
	PowerWatch_sleeping(&watch);
	Plugins_beforeSleep();
}

static void afterResume(void) {
	PowerWatch_resumed(&watch, SDL_GetTicks());
	Plugins_afterResume();
}

void Power_atExit(void) {
	if (!Plugins_count() || !PowerWatch_exitNeedsPoweroff(&watch)) return;
	Power_beforePoweroff();
	sync(); // stock synced before the hooks ran
}

void Power_update(int* dirty, int* show_setting) {
	static int registered = 0;
	if (!registered) {
		atexit(Power_atExit);
		registered = 1;
	}
	int power_pressed = PAD_justPressed(BTN_POWER); // before a sleep resets the pad
	PowerWatch_before(&watch, SDL_GetTicks());
	PWR_update(dirty, show_setting, beforeSleep, afterResume);
	PowerWatch_after(&watch, SDL_GetTicks(), power_pressed);
}

void Power_sleep(void) {
	PowerWatch_sleeping(&watch);
	Plugins_beforeSleep();
	PWR_sleep();
	afterResume();
}
