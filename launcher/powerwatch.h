#ifndef POWERWATCH_H
#define POWERWATCH_H

// Telling stock's power-off from a sleep, with NextUI unmodified. Stock's
// PWR_update runs its before-sleep callback both before a sleep and before
// it powers off for the power button (held for a second, or a power-off
// button), and it powers off from inside its sleep loop after the suspend
// timeout without any callback. PowerWatch follows the power button the way
// PWR_update does, so the callback can tell which one it's for, and knows
// when the device is asleep, so an exit then is a power-off.
//
// When unsure it answers sleep: the plugins then get before_sleep, and
// before_poweroff still runs when the launcher exits while asleep.

#define POWERWATCH_HOLD_MS 1000   // stock powers off when power is held this long
#define POWERWATCH_RESUME_MS 1000 // stock ignores a press this soon after a resume

typedef struct {
	unsigned long frame_at;   // when the current PWR_update started
	unsigned long pressed_at; // when stock started timing a power press, 0 when it isn't
	unsigned long resumed_at; // the last resume, 0 before the first
	int asleep;               // between before_sleep and after_resume
	int woke;                 // a sleep ended during this PWR_update
	int powered_off;          // before_poweroff ran
} PowerWatch;

// Call right before PWR_update, with the time.
void PowerWatch_before(PowerWatch* pw, unsigned long now);

// From the before-sleep callback: 1 when PWR_update is powering off, 0 for a
// sleep. poweroff_released is PAD_justReleased(BTN_POWEROFF).
int PowerWatch_isPowerOff(const PowerWatch* pw, int poweroff_released);

// Call right after PWR_update returns, with the time and whether power was
// just pressed in this frame (read before PWR_update, as it sees it).
void PowerWatch_after(PowerWatch* pw, unsigned long now, int power_pressed);

// Around a sleep.
void PowerWatch_sleeping(PowerWatch* pw);
void PowerWatch_resumed(PowerWatch* pw, unsigned long now);

// before_poweroff ran.
void PowerWatch_poweredOff(PowerWatch* pw);

// At exit: 1 when before_poweroff still needs to run, because stock is
// powering off from its sleep.
int PowerWatch_exitNeedsPoweroff(const PowerWatch* pw);

#endif
