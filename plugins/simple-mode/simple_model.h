#ifndef SIMPLE_MODEL_H
#define SIMPLE_MODEL_H

#include <stddef.h>

// What simple mode hides and how it's left. No SDL and no host here, so
// the host tests can check it on its own.

#define SIMPLE_PIN_LEN 64
#define SIMPLE_TILES_MAX 8

// 1 unless the "on" setting is "off". NULL counts as on, the default.
int Simple_isOn(const char* setting);

// 1 for <sdcard>/Tools/<platform> and anything inside it, such as a pak.
int Simple_isTools(const char* path, const char* sdcard, const char* platform);

// The built-in quick menu tiles simple mode hides, by name: Tools and Pak
// Store, and with hide_settings also Settings, Wifi, Bluetooth and Reboot.
// out holds SIMPLE_TILES_MAX. Returns the count.
int Simple_tiles(int hide_settings, const char** out);

// The PIN with spaces, tabs and line breaks trimmed from both ends. Longer
// than SIMPLE_PIN_LEN - 1 is cut. NULL is empty.
void Simple_cleanPin(const char* in, char out[SIMPLE_PIN_LEN]);

// What leaving takes: the PIN when one is set, otherwise just a confirm.
enum SimpleLeave {
	SIMPLE_LEAVE_CONFIRM,
	SIMPLE_LEAVE_PIN,
};
int Simple_leaveStep(const char* pin);

// 1 when typed is the PIN, both trimmed. An empty PIN never matches, and
// a NULL typed (the keyboard was cancelled) never does either.
int Simple_pinMatches(const char* pin, const char* typed);

#endif
