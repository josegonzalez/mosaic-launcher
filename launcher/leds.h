#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

// A plugin's LED colour and the theme colours, for led_set_color,
// led_clear and theme_color in mosaic_plugin.h.
//
// A colour is stock's LIGHT_PROFILE_AMBIENT override: the user's own LED
// settings with every LED static in that colour. It goes on stock's
// override stack, so charging, critical and low battery, mute and sleep
// still take priority, and clearing pops it, which puts the user's
// settings back. Nothing here writes the LED settings files. While it's on,
// stock sends it to the LEDs every frame, as it does for a game's ambient
// mode.
//
// One plugin holds the colour at a time: until it clears it, any other
// plugin's set is refused.

// Lights every LED in rgb (0xRRGGBB) for plugin who, or changes the colour
// who already set. Returns 0, or -1 with no LEDs, for another plugin while
// who's colour is on, or for a bad argument.
int Leds_set(const char* who, uint32_t rgb);

// Puts the user's LED settings back if who holds the colour. Returns 0, or
// -1 when who doesn't hold it.
int Leds_clear(const char* who);

// The plugin whose colour is on, or NULL.
const char* Leds_holder(void);

// Stock's THEME_COLORn_255 for n from 1 to 7 as 0xRRGGBB, or -1 for any
// other n.
int Leds_themeColor(int index);

#endif
