#ifndef AWAKE_H
#define AWAKE_H

// Keeping the device from auto-sleeping while something needs it awake: a
// pak that touches /tmp/stay_awake (eg. while syncing), or a plugin that
// asks. The power button still sleeps the device.

#define STAY_AWAKE_PATH "/tmp/stay_awake"

// A plugin holding the device awake (on=1) or letting it go (on=0).
void Awake_set(const char* who, int on);

// Call every frame. Applies changes only when the answer flips, so it never
// fights anything else that turns auto-sleep on or off.
void Awake_tick(unsigned long now);

#endif
