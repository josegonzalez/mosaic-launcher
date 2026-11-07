// A plugin's LED colour and the theme colours, against a fake of stock's
// LED profiles: its override stack and LEDS_applyRules' priorities, with
// the LEDs that were last written kept to check.

#include <stdbool.h>
#include <stdint.h>
#include "defines.h"
#include "api.h"
#include "config.h"
#include "leds.h"
#include "check.h"

uint32_t THEME_COLOR1_255, THEME_COLOR2_255, THEME_COLOR3_255, THEME_COLOR4_255;
uint32_t THEME_COLOR5_255, THEME_COLOR6_255, THEME_COLOR7_255;

// stock's profiles, as LEDS_initLeds derives them from the user's settings
LightSettings lightsDefault[MAX_LIGHTS];
LightSettings lightsAmbient[MAX_LIGHTS];
static LightSettings lightsCharging[MAX_LIGHTS];
static LightSettings lightsSleep[MAX_LIGHTS];

static int led_count = 3;
int LEDS_getCount(void) { return led_count; }

// stock's override stack
#define STACK_SIZE 4
static int stack[STACK_SIZE];
static int top = -1;
static int charging = 0;

// what the LEDs show, and how many times they were written
static LightSettings shown[MAX_LIGHTS];
static int writes = 0;

static void show(const LightSettings* lights) {
	for (int i = 0; i < led_count; i++) shown[i] = lights[i];
	writes++;
}

void LEDS_applyRules(void) {
	int profile = top >= 0 ? stack[top] : LIGHT_PROFILE_DEFAULT;
	if (charging) show(lightsCharging);
	else if (profile == LIGHT_PROFILE_AMBIENT) show(lightsAmbient);
	else if (profile == LIGHT_PROFILE_SLEEP) show(lightsSleep);
	else show(lightsDefault);
}

bool LEDS_pushProfileOverride(int profile) {
	if (top == STACK_SIZE - 1) return false;
	stack[++top] = profile;
	LEDS_applyRules();
	return true;
}

bool LEDS_popProfileOverride(int profile) {
	if (top < 0 || stack[top] != profile) return false;
	top--;
	LEDS_applyRules();
	return true;
}

static void userSettings(void) {
	for (int i = 0; i < MAX_LIGHTS; i++) {
		lightsDefault[i] = (LightSettings){0};
		snprintf(lightsDefault[i].filename, sizeof(lightsDefault[i].filename), "led%i", i);
		lightsDefault[i].effect = 2; // breathe
		lightsDefault[i].speed = 1500;
		lightsDefault[i].brightness = 40 + i;
		lightsDefault[i].color1 = 0x112233;
		lightsDefault[i].color2 = 0x445566;
		lightsDefault[i].inbrightness = 60;
		lightsDefault[i].cycles = -1;
		lightsCharging[i] = lightsDefault[i];
		lightsCharging[i].color1 = 0x00FF00;
		lightsSleep[i] = lightsDefault[i];
		lightsSleep[i].cycles = 5;
	}
}

// every LED shows the user's settings in rgb, static
static int showsColor(uint32_t rgb) {
	for (int i = 0; i < led_count; i++) {
		LightSettings want = lightsDefault[i];
		want.color1 = rgb;
		want.effect = 4;
		if (memcmp(&shown[i], &want, sizeof(want)) != 0) return 0;
	}
	return 1;
}

static int showsUser(void) {
	return memcmp(shown, lightsDefault, sizeof(LightSettings) * led_count) == 0;
}

int main(void) {
	userSettings();
	LightSettings saved[MAX_LIGHTS];
	memcpy(saved, lightsDefault, sizeof(saved));
	LEDS_applyRules();
	CHECK(showsUser());

	// no LEDs: refused, and nothing changes
	led_count = 0;
	CHECK(Leds_set("status", 0xFF0000) == -1);
	CHECK(top == -1);
	CHECK(Leds_holder() == NULL);
	led_count = 3;

	// bad arguments
	CHECK(Leds_set(NULL, 0xFF0000) == -1);
	CHECK(Leds_set("", 0xFF0000) == -1);
	CHECK(Leds_set("status", 0x1000000) == -1);
	CHECK(Leds_set("an-id-far-too-long-for-the-holder-to-keep-without-cutting-it-off", 0xFF0000) == -1);
	CHECK(top == -1);
	CHECK(Leds_clear("status") == -1);

	// a colour: the ambient override, from the user's settings
	CHECK(Leds_set("status", 0xFF0000) == 0);
	CHECK_STR(Leds_holder(), "status");
	CHECK(top == 0 && stack[0] == LIGHT_PROFILE_AMBIENT);
	CHECK(showsColor(0xFF0000));
	// only the LEDs the device has
	CHECK(lightsAmbient[3].color1 == 0);

	// another plugin is refused while it's on
	CHECK(Leds_set("other", 0x0000FF) == -1);
	CHECK(Leds_clear("other") == -1);
	CHECK_STR(Leds_holder(), "status");
	CHECK(showsColor(0xFF0000));

	// the same plugin changes it at once, without pushing again
	int before = writes;
	CHECK(Leds_set("status", 0x00FFFF) == 0);
	CHECK(top == 0);
	CHECK(writes > before);
	CHECK(showsColor(0x00FFFF));

	// stock's rules still come first, and the colour comes back after
	charging = 1;
	LEDS_applyRules();
	CHECK(shown[0].color1 == 0x00FF00);
	charging = 0;
	LEDS_applyRules();
	CHECK(showsColor(0x00FFFF));

	// and sleep goes over it
	LEDS_pushProfileOverride(LIGHT_PROFILE_SLEEP);
	CHECK(shown[0].cycles == 5);
	LEDS_popProfileOverride(LIGHT_PROFILE_SLEEP);
	CHECK(showsColor(0x00FFFF));

	// clearing pops it and shows the user's settings, untouched
	CHECK(Leds_clear("status") == 0);
	CHECK(Leds_holder() == NULL);
	CHECK(top == -1);
	CHECK(showsUser());
	CHECK(memcmp(saved, lightsDefault, sizeof(saved)) == 0);
	CHECK(Leds_clear("status") == -1);

	// then another plugin can set one
	CHECK(Leds_set("other", 0x0000FF) == 0);
	CHECK(showsColor(0x0000FF));

	// cleared while sleep is above it, as when stock powers off from sleep:
	// it stays on the stack as the user's own settings, and goes next time
	LEDS_pushProfileOverride(LIGHT_PROFILE_SLEEP);
	CHECK(Leds_clear("other") == 0);
	CHECK(top == 1);
	LEDS_popProfileOverride(LIGHT_PROFILE_SLEEP);
	CHECK(showsUser());
	CHECK(Leds_set("status", 0xFFFF00) == 0);
	CHECK(top == 0);
	CHECK(showsColor(0xFFFF00));
	CHECK(Leds_clear("status") == 0);
	CHECK(top == -1);
	CHECK(showsUser());

	// a full override stack refuses it
	for (int i = 0; i < STACK_SIZE; i++) LEDS_pushProfileOverride(LIGHT_PROFILE_SLEEP);
	CHECK(Leds_set("status", 0xFF0000) == -1);
	CHECK(Leds_holder() == NULL);
	top = -1;

	// the theme colours, as 0xRRGGBB
	THEME_COLOR1_255 = 0xFFFFFFFF;
	THEME_COLOR2_255 = 0x9B2257FF;
	THEME_COLOR3_255 = 0x1E232980;
	THEME_COLOR7_255 = 0x000000FF;
	CHECK(Leds_themeColor(1) == 0xFFFFFF);
	CHECK(Leds_themeColor(2) == 0x9B2257);
	CHECK(Leds_themeColor(3) == 0x1E2329);
	CHECK(Leds_themeColor(7) == 0x000000);
	CHECK(Leds_themeColor(0) == -1);
	CHECK(Leds_themeColor(8) == -1);
	CHECK(Leds_themeColor(-1) == -1);

	CHECK_DONE();
}
