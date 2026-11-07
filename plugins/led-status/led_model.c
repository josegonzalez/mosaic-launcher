#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "led_model.h"

///////////////////////////////////////
// The battery bands

void LsBattery_init(LsBattery* b, int low_at, int mid_at) {
	*b = (LsBattery){0};
	b->low_at = low_at;
	b->mid_at = mid_at;
	b->band = LS_BAND_NONE;
}

static int bandFor(const LsBattery* b, int level) {
	if (level <= b->low_at) return LS_BAND_RED;
	if (level <= b->mid_at) return LS_BAND_YELLOW;
	return LS_BAND_GREEN;
}

static int bandColor(int band) {
	switch (band) {
		case LS_BAND_RED: return LS_RED;
		case LS_BAND_YELLOW: return LS_YELLOW;
		case LS_BAND_GREEN: return LS_GREEN;
	}
	return -1;
}

int LsBattery_sample(LsBattery* b, int level) {
	// a failed read: keep the colour rather than flicker
	if (level <= 0 || level > 100) return bandColor(b->band);

	if (b->band == LS_BAND_NONE) {
		b->band = bandFor(b, level);
	}
	else {
		// down at once, up only once clear of the threshold
		int up = bandFor(b, level - LS_MARGIN);
		int band = bandFor(b, level);
		if (up > b->band) b->band = up;
		else if (band < b->band) b->band = band;
	}
	return bandColor(b->band);
}

///////////////////////////////////////
// Setting the colour

void LsApplied_init(LsApplied* a) {
	a->rgb = -1;
	a->refused = -1;
}

int LsApplied_wanted(const LsApplied* a, int rgb) {
	return rgb >= 0 && rgb != a->rgb && rgb != a->refused;
}

void LsApplied_result(LsApplied* a, int rgb, int result) {
	if (result == 0) {
		a->rgb = rgb;
		a->refused = -1;
	}
	else {
		a->refused = rgb;
	}
}

int Ls_due(unsigned long* next, unsigned long now) {
	if (now < *next) return 0;
	*next = now + LS_SAMPLE_MS;
	return 1;
}

///////////////////////////////////////
// Settings

static const char* skipSpace(const char* s) {
	while (isspace((unsigned char)*s)) s++;
	return s;
}

// s, without the spaces around it, is name
static int is(const char* s, const char* name) {
	s = skipSpace(s);
	size_t n = strlen(name);
	return strncmp(s, name, n) == 0 && *skipSpace(s + n) == '\0';
}

int Ls_parseMode(const char* setting) {
	if (setting && is(setting, "theme")) return LS_MODE_THEME;
	return LS_MODE_BATTERY;
}

int Ls_parseThemeColor(const char* setting) {
	static const struct { const char* name; int index; } names[] = {
		{"main", MOSAIC_COLOR_MAIN},
		{"accent", MOSAIC_COLOR_ACCENT},
		{"accent2", MOSAIC_COLOR_ACCENT2},
		{"list_text", MOSAIC_COLOR_LIST_TEXT},
		{"list_text_selected", MOSAIC_COLOR_LIST_TEXT_SELECTED},
		{"hint", MOSAIC_COLOR_HINT},
		{"background", MOSAIC_COLOR_BACKGROUND},
	};
	if (setting) {
		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			if (is(setting, names[i].name)) return names[i].index;
		}
	}
	return MOSAIC_COLOR_ACCENT;
}

int Ls_parseNumber(const char* setting, int min, int max, int fallback) {
	if (!setting) return fallback;
	setting = skipSpace(setting);
	if (!isdigit((unsigned char)*setting)) return fallback;
	char* end;
	long value = strtol(setting, &end, 10);
	if (*end == '%') end++;
	if (*skipSpace(end) || value < min || value > max) return fallback;
	return (int)value;
}
