#include <ctype.h>
#include <stdio.h>
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
	if (setting && is(setting, "battery")) return LS_MODE_BATTERY;
	return LS_MODE_AUTO;
}

static const struct { const char* name; int index; } colorNames[] = {
	{"main", MOSAIC_COLOR_MAIN},
	{"accent", MOSAIC_COLOR_ACCENT},
	{"accent2", MOSAIC_COLOR_ACCENT2},
	{"list_text", MOSAIC_COLOR_LIST_TEXT},
	{"list_text_selected", MOSAIC_COLOR_LIST_TEXT_SELECTED},
	{"hint", MOSAIC_COLOR_HINT},
	{"background", MOSAIC_COLOR_BACKGROUND},
};
#define COLOR_NAMES (sizeof(colorNames) / sizeof(colorNames[0]))

// a theme colour's name as an enum MosaicThemeColor, or -1
static int colorIndex(const char* name) {
	for (size_t i = 0; name && i < COLOR_NAMES; i++) {
		if (is(name, colorNames[i].name)) return colorNames[i].index;
	}
	return -1;
}

int Ls_parseThemeColor(const char* setting) {
	int index = colorIndex(setting);
	return index < 0 ? MOSAIC_COLOR_ACCENT : index;
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

int Ls_parseColour(const char* value, LsSource* out) {
	if (!value) return -1;
	LsSource source = { LS_SHOW_BATTERY, 0, 1 };
	const char* p = skipSpace(value);
	int index = colorIndex(p);
	if (is(p, "battery")) {
		// the battery's own colours
	}
	else if (index >= 0) {
		source.show = LS_SHOW_THEME;
		source.value = index;
	}
	else {
		if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
		else if (p[0] == '#') p++;
		else return -1;
		int digits = 0;
		while (isxdigit((unsigned char)p[digits])) digits++;
		if (digits != 6 || *skipSpace(p + digits)) return -1;
		source.show = LS_SHOW_RGB;
		source.value = (int)strtol(p, NULL, 16);
	}
	*out = source;
	return 0;
}

LsSource Ls_source(int mode, int theme_index, const char* theme_colour) {
	LsSource source = { LS_SHOW_BATTERY, 0, 0 };
	if (mode == LS_MODE_THEME) {
		source.show = LS_SHOW_THEME;
		source.value = theme_index;
	}
	else if (mode == LS_MODE_AUTO) {
		Ls_parseColour(theme_colour, &source);
	}
	return source;
}

void Ls_describe(const LsSource* source, char* out, size_t size) {
	const char* from = source->from_theme ? ", from the theme" : "";
	if (source->show == LS_SHOW_RGB) {
		snprintf(out, size, "0x%06X%s", (unsigned)source->value, from);
		return;
	}
	if (source->show == LS_SHOW_THEME) {
		const char* name = "?";
		for (size_t i = 0; i < COLOR_NAMES; i++) {
			if (colorNames[i].index == source->value) name = colorNames[i].name;
		}
		snprintf(out, size, "the theme colour %s%s", name, from);
		return;
	}
	snprintf(out, size, "the battery level%s", from);
}
