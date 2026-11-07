// Telling a power-off from a sleep: the launcher's power glue against a
// fake PWR_update that follows stock's power button and sleep order, with
// the pad, the clock and the plugin hooks faked. Each case runs in its own
// process, since stock's and the launcher's power state are static.

#include <setjmp.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include "launcher.h"
#include "powerwatch.h"
#include "check.h"

// the clock and the pad
static uint32_t ticks = 5000;
static int pressed, just_pressed, just_released;

Uint32 SDL_GetTicks(void) { return ticks; }
int PAD_justPressed(int btn) { return (just_pressed & btn) != 0; }
int PAD_justReleased(int btn) { return (just_released & btn) != 0; }

// the plugin hooks
static int plugin_count = 1;
static int slept, resumed, powered_off;
static int started;
static char order[64];

static void note(char c) {
	size_t n = strlen(order);
	if (n + 1 < sizeof(order)) order[n] = c;
}
void Plugins_beforeSleep(void) { slept++; note('s'); }
void Plugins_afterResume(void) { resumed++; note('r'); }
void Plugins_beforePoweroff(void) { powered_off++; note('p'); }
void Plugins_started(void) { started++; }
int Plugins_count(void) { return plugin_count; }

// stock, reduced to its power button and sleep handling, in its order
static jmp_buf exit_jmp;
static int exited;
static int autosleep, sleep_times_out;
static uint32_t resume_tick;

static void stockExit(void) { // PLAT_powerOff ends in exit(0)
	Power_atExit();
	longjmp(exit_jmp, 1);
}

void PWR_sleep(void) {
	just_pressed = just_released = 0; // PAD_reset
	if (sleep_times_out) stockExit();
	ticks += 60000;
	resume_tick = ticks;
}

void PWR_update(int* dirty, int* show_setting, PWR_callback_t before_sleep, PWR_callback_t after_sleep) {
	(void)dirty;
	(void)show_setting;
	static uint32_t power_pressed_at = 0;
	uint32_t now = SDL_GetTicks();

	if (PAD_justReleased(BTN_POWEROFF) || (power_pressed_at && now - power_pressed_at >= 1000)) {
		if (before_sleep) before_sleep();
		stockExit(); // PWR_powerOff
	}
	if (PAD_justPressed(BTN_POWER)) {
		power_pressed_at = now - resume_tick < 1000 ? 0 : now;
	}
	if (autosleep || (PAD_justReleased(BTN_SLEEP) && power_pressed_at)) {
		autosleep = 0;
		if (before_sleep) before_sleep();
		PWR_sleep();
		if (after_sleep) after_sleep();
		power_pressed_at = 0;
	}
}

// one launcher frame, 16ms long
static void frame(void) {
	int dirty = 0, show_setting = 0;
	if (setjmp(exit_jmp)) {
		exited = 1;
		return;
	}
	Power_update(&dirty, &show_setting);
	just_pressed = just_released = 0;
	ticks += 16;
}

static void press(void) { pressed |= BTN_POWER; just_pressed |= BTN_POWER; }
static void release(void) { pressed &= ~BTN_POWER; just_released |= BTN_POWER; }

// frames until ms have passed, or the launcher exits
static void run(uint32_t ms) {
	uint32_t until = ticks + ms;
	while (!exited && ticks < until) frame();
}

static void holdToPowerOff(void) {
	run(100);
	press();
	run(1500);
	CHECK(exited);
	CHECK_STR(order, "p");
	CHECK(started == 1);
}

static void tapToSleep(void) {
	run(100);
	press();
	run(300);
	release();
	run(100);
	CHECK(!exited);
	CHECK_STR(order, "sr");
}

static void releaseAsTheHoldEnds(void) {
	run(100);
	press();
	// the frame where stock's hold reaches a second, released on that frame
	uint32_t pressed_at = ticks;
	run(1000 - 16);
	while (ticks - pressed_at < 1000) frame();
	release();
	frame();
	CHECK(exited);
	CHECK_STR(order, "p");
}

static void powerOffButton(void) {
	run(100);
	just_released |= BTN_POWEROFF;
	frame();
	CHECK(exited);
	CHECK_STR(order, "p");
}

static void autosleepTimesOut(void) {
	run(100);
	autosleep = 1;
	sleep_times_out = 1;
	frame();
	CHECK(exited);
	CHECK_STR(order, "sp");
}

static void autosleepAndWake(void) {
	run(100);
	autosleep = 1;
	run(100);
	CHECK(!exited);
	CHECK_STR(order, "sr");
	// stock ignores a press just after waking, so holding it doesn't power off
	press();
	run(1500);
	CHECK(!exited);
	autosleep = 1;
	run(100);
	CHECK(!exited);
	CHECK_STR(order, "srsr");
}

static void quickMenuSleepTimesOut(void) {
	run(100);
	sleep_times_out = 1;
	if (setjmp(exit_jmp)) exited = 1;
	else Power_sleep();
	CHECK(exited);
	CHECK_STR(order, "sp");
}

static void quickMenuSleepAndWake(void) {
	run(100);
	Power_sleep();
	run(100);
	CHECK_STR(order, "sr");
}

static void quickMenuPowerOff(void) {
	run(100);
	Power_beforePoweroff();
	Power_beforePoweroff();
	Power_atExit();
	CHECK_STR(order, "p");
}

static void noPluginsTimesOut(void) {
	plugin_count = 0;
	run(100);
	autosleep = 1;
	sleep_times_out = 1;
	frame();
	CHECK(exited);
	CHECK_STR(order, "s");
	CHECK(started == 0);
}

static void cleanExit(void) {
	run(100);
	autosleep = 1;
	run(100);
	Power_atExit();
	CHECK_STR(order, "sr");
}

// the model on its own: a press is never timed longer than stock's
static void modelBorderline(void) {
	PowerWatch pw = {0};
	PowerWatch_before(&pw, 10000);
	PowerWatch_after(&pw, 10001, 1);
	PowerWatch_before(&pw, 11000);
	CHECK(!PowerWatch_isPowerOff(&pw, 0));
	PowerWatch_before(&pw, 11001);
	CHECK(PowerWatch_isPowerOff(&pw, 0));
	CHECK(PowerWatch_isPowerOff(&pw, 1));
	// stock didn't power off, so it isn't timing the press
	PowerWatch_after(&pw, 11002, 0);
	PowerWatch_before(&pw, 12000);
	CHECK(!PowerWatch_isPowerOff(&pw, 0));
	CHECK(!PowerWatch_exitNeedsPoweroff(&pw));
	PowerWatch_sleeping(&pw);
	CHECK(PowerWatch_exitNeedsPoweroff(&pw));
	PowerWatch_poweredOff(&pw);
	CHECK(!PowerWatch_exitNeedsPoweroff(&pw));
}

static int failed = 0;

static void scenario(const char* name, void (*fn)(void)) {
	pid_t pid = fork();
	if (pid == 0) {
		fn();
		_exit(check_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, "%s failed\n", name);
		failed++;
	}
}

int main(void) {
	scenario("holdToPowerOff", holdToPowerOff);
	scenario("tapToSleep", tapToSleep);
	scenario("releaseAsTheHoldEnds", releaseAsTheHoldEnds);
	scenario("powerOffButton", powerOffButton);
	scenario("autosleepTimesOut", autosleepTimesOut);
	scenario("autosleepAndWake", autosleepAndWake);
	scenario("quickMenuSleepTimesOut", quickMenuSleepTimesOut);
	scenario("quickMenuSleepAndWake", quickMenuSleepAndWake);
	scenario("quickMenuPowerOff", quickMenuPowerOff);
	scenario("noPluginsTimesOut", noPluginsTimesOut);
	scenario("cleanExit", cleanExit);
	scenario("modelBorderline", modelBorderline);
	check_failures += failed;
	CHECK_DONE();
}
