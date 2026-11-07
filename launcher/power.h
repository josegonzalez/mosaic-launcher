#ifndef POWER_H
#define POWER_H

// Stock's sleep and power-off with the plugins' power hooks. A power-off
// from the power button, or stock's own after the suspend timeout, runs
// before_poweroff rather than before_sleep (see powerwatch.h).

// In place of PWR_update in the main loop.
void Power_update(int* dirty, int* show_setting);

// The quick menu's Sleep: PWR_sleep between before_sleep and after_resume.
void Power_sleep(void);

// Every plugin's before_poweroff, once, before a power-off or reboot.
void Power_beforePoweroff(void);

// Power_update registers this with atexit: when stock powers off while
// asleep, it runs before_poweroff. Public for the tests.
void Power_atExit(void);

#endif
