#include <stdio.h>
#include <string.h>
#include "defines.h"
#include "api.h"
#include "config.h"
#include "leds.h"

// stock's ambient profile, which api.h doesn't declare
extern LightSettings lightsAmbient[MAX_LIGHTS];

// "static" in the effect ids every platform shares (see api.h), the one
// GFX_setAmbientColor uses
#define EFFECT_STATIC 4

static char holder[64]; // the plugin whose colour is on, "" for none
static int pushed = 0;  // our LIGHT_PROFILE_AMBIENT is on stock's override stack

// The user's own settings, as stock loaded them, in rgb when it's >= 0.
static void fillAmbient(int64_t rgb) {
	int count = LEDS_getCount();
	for (int i = 0; i < count; i++) {
		lightsAmbient[i] = lightsDefault[i];
		if (rgb < 0) continue;
		lightsAmbient[i].color1 = (uint32_t)rgb;
		lightsAmbient[i].effect = EFFECT_STATIC;
	}
}

int Leds_set(const char* who, uint32_t rgb) {
	if (!who || !who[0] || strlen(who) >= sizeof(holder) || rgb > 0xFFFFFF) return -1;
	if (LEDS_getCount() <= 0) return -1;
	if (holder[0] && strcmp(holder, who) != 0) return -1;

	fillAmbient(rgb);
	if (!pushed) {
		if (!LEDS_pushProfileOverride(LIGHT_PROFILE_AMBIENT)) {
			fillAmbient(-1);
			return -1;
		}
		pushed = 1;
	}
	else {
		LEDS_applyRules(); // at once, rather than on the next frame
	}
	snprintf(holder, sizeof(holder), "%s", who);
	return 0;
}

int Leds_clear(const char* who) {
	if (!who || !holder[0] || strcmp(holder, who) != 0) return -1;
	holder[0] = '\0';
	// in case something else is above it on the stack, such as sleep while
	// stock powers off from it: then it stays, as the user's own settings
	fillAmbient(-1);
	if (pushed && LEDS_popProfileOverride(LIGHT_PROFILE_AMBIENT)) pushed = 0;
	else if (pushed) LEDS_applyRules();
	return 0;
}

const char* Leds_holder(void) {
	return holder[0] ? holder : NULL;
}

int Leds_themeColor(int index) {
	uint32_t color;
	switch (index) {
		case 1: color = THEME_COLOR1_255; break;
		case 2: color = THEME_COLOR2_255; break;
		case 3: color = THEME_COLOR3_255; break;
		case 4: color = THEME_COLOR4_255; break;
		case 5: color = THEME_COLOR5_255; break;
		case 6: color = THEME_COLOR6_255; break;
		case 7: color = THEME_COLOR7_255; break;
		default: return -1;
	}
	// stock packs them as 0xRRGGBBAA
	return (int)(color >> 8);
}
