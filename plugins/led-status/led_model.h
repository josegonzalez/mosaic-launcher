#ifndef LED_MODEL_H
#define LED_MODEL_H

// What LED status decides, apart from the host, so the host tests can check
// it on its own.

#define LS_SAMPLE_MS 5000 // how often to look again; the launcher refreshes the battery every 5 seconds
#define LS_MARGIN 2       // a band is left upwards only this far above its threshold, so readings around it don't flicker

#define LS_RED 0xFF0000
#define LS_YELLOW 0xFFFF00
#define LS_GREEN 0x00FF00

enum LsMode {
	LS_MODE_BATTERY, // red, yellow or green by the battery level
	LS_MODE_THEME,   // one of stock's theme colours
};

enum LsBand {
	LS_BAND_NONE,   // no good reading yet
	LS_BAND_RED,    // at or below low_at
	LS_BAND_YELLOW, // at or below mid_at
	LS_BAND_GREEN,
};

typedef struct LsBattery {
	int low_at; // percent
	int mid_at; // percent; at or below low_at there's no yellow band
	int band;   // an enum LsBand
} LsBattery;

void LsBattery_init(LsBattery* b, int low_at, int mid_at);

// One battery reading, from 0 to 100 as the host returns it. A level of 0
// or outside 0 to 100 is a bad reading and keeps the band it had. Returns
// the band's colour as 0xRRGGBB, or -1 before the first good reading.
int LsBattery_sample(LsBattery* b, int level);

// The colour last given to the LEDs, so it's set only when it changes:
// while a plugin's colour is on, stock sends it to the LEDs every frame.
typedef struct LsApplied {
	int rgb;     // the colour that's on, or -1
	int refused; // the colour the host last refused, or -1
} LsApplied;

void LsApplied_init(LsApplied* a);

// 1 when rgb (0xRRGGBB, or -1 for none) should be sent to the host: it's a
// colour, and neither the one that's on nor the one last refused, so a
// refusal isn't retried every few seconds.
int LsApplied_wanted(const LsApplied* a, int rgb);

// What the host said to setting rgb: 0 for set, anything else for refused.
void LsApplied_result(LsApplied* a, int rgb, int result);

// When it's time to look again: 1 at now >= *next, which moves on.
int Ls_due(unsigned long* next, unsigned long now);

///////////////////////////////////////
// Settings

// "battery" or "theme"; anything else is battery.
int Ls_parseMode(const char* setting);

// A theme colour's name in the theme_color setting, as an enum
// MosaicThemeColor index: main, accent, accent2, list_text,
// list_text_selected, hint or background. Anything else is accent.
int Ls_parseThemeColor(const char* setting);

// A whole number from a setting such as "20", "20%" or " 20 ", or fallback
// when it isn't one or is outside min to max.
int Ls_parseNumber(const char* setting, int min, int max, int fallback);

#endif
