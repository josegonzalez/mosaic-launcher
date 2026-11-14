#ifndef CHARGING_MODEL_H
#define CHARGING_MODEL_H

#include <stddef.h>

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until CS_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct CSWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} CSWords;
void CS_setWords(const CSWords* words);

// What the charging screen decides, apart from the host, the clock and the
// card, so the host tests can check it on its own.

///////////////////////////////////////
// Settings

#define CS_IDLE_DEFAULT_MS 60000UL // as in plugin.txt
#define CS_IDLE_MAX_S 3600

// The idle setting in ms: whole seconds from 1 to CS_IDLE_MAX_S, such as
// "60" or " 30 ", or 0 for "off" or "0". Anything else is the default.
unsigned long CS_parseIdle(const char* setting);

enum CsClock {
	CS_CLOCK_OFF,
	CS_CLOCK_24H,
	CS_CLOCK_12H,
};

// The clock setting: "off", "24h" or "12h", or 24h for anything else.
int CS_parseClock(const char* setting);

///////////////////////////////////////
// When it opens and closes

// The ambient screen's wants: 1 when the launcher should open it, with the
// idle setting on (setting_ms above 0), the device charging (charging 1, as
// is_charging returns it) and idle_ms, the launcher's idle time, at least
// the setting. The launcher asks only over the game list, home or a later
// ambient screen.
int CS_wants(unsigned long idle_ms, unsigned long setting_ms, int charging);

// Which buttons the screen takes while it shows and until they're let go
// after it closes, so the press that closes it doesn't also act on the
// screen under it.
typedef struct CsFilter {
	int swallowing; // a press took the screen's buttons and they aren't all up yet
} CsFilter;

// One frame's buttons, as MOSAIC_BUTTON_BIT masks. keep is what's never
// taken (the power button, so sleep still works), and toggle the button
// that plays or pauses music in place of closing the screen (X while music
// has a track), or 0. Sets *close to 1 when a press should close the open
// screen, or *toggled to 1 when it was toggle alone, and returns the
// buttons to take.
unsigned int CsFilter_frame(CsFilter* f, int open, unsigned int pressed, unsigned int held,
	unsigned int released, unsigned int keep, unsigned int toggle, int* close, int* toggled);

///////////////////////////////////////
// What it shows

// "73%" for a level from 0 to 100, or "" for anything else.
void CS_formatLevel(int level, char* out, size_t size);

// "Charged" at 100, "Charging" otherwise.
const char* CS_stateLabel(int level);

// "09:05" in 24h, "9:05 AM" in 12h, or "" with the clock off or a bad time.
void CS_formatClock(int hour, int minute, int mode, char* out, size_t size);

// How much of a bar inner wide to fill for level: none for a bad level,
// at least 1 pixel for any level above 0.
int CS_fillWidth(int level, int inner);

///////////////////////////////////////
// The test-only fake battery

// Parses "<level> <charging>", such as "73 1", as the fake file holds it.
// Returns 1, or 0 when it isn't that.
int CS_parseFake(const char* text, int* level, int* charging);

#endif
