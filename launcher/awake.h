#ifndef AWAKE_H
#define AWAKE_H

// Keeping the device from auto-sleeping while something needs it awake: a
// pak that touches /tmp/stay_awake or /tmp/stay_alive (eg. while syncing),
// or a plugin that asks. The power button still sleeps the device.
//
// Stock's only automatic power-off comes after a period asleep, so holding
// off auto-sleep is also what keeps the device alive.

#define STAY_AWAKE_PATH "/tmp/stay_awake"
#define STAY_ALIVE_PATH "/tmp/stay_alive"

// A plugin holding the device awake (on=1) or letting it go (on=0).
void Awake_set(const char* who, int on);

// Call every frame. Applies changes only when the answer flips, so it never
// fights anything else that turns auto-sleep on or off.
void Awake_tick(unsigned long now);

#endif
