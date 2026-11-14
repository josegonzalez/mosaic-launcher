#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charging_model.h"

// English the plugin translates where it draws it, for make pot
#define TR_MARK(text) (text)

static CSWords words; // none set: English

void CS_setWords(const CSWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

///////////////////////////////////////
// Settings

static const char* skipSpaces(const char* s) {
	while (*s && isspace((unsigned char)*s)) s++;
	return s;
}

unsigned long CS_parseIdle(const char* setting) {
	if (!setting) return CS_IDLE_DEFAULT_MS;
	const char* s = skipSpaces(setting);
	if (!strncmp(s, "off", 3) && !*skipSpaces(s + 3)) return 0;
	if (!isdigit((unsigned char)*s)) return CS_IDLE_DEFAULT_MS;
	char* end;
	long seconds = strtol(s, &end, 10);
	if (*skipSpaces(end) || seconds < 0 || seconds > CS_IDLE_MAX_S) return CS_IDLE_DEFAULT_MS;
	return (unsigned long)seconds * 1000UL;
}

int CS_parseClock(const char* setting) {
	if (setting && !strcmp(setting, "off")) return CS_CLOCK_OFF;
	if (setting && !strcmp(setting, "12h")) return CS_CLOCK_12H;
	return CS_CLOCK_24H;
}

///////////////////////////////////////
// When it opens and closes

int CS_wants(unsigned long idle_ms, unsigned long setting_ms, int charging) {
	return setting_ms > 0 && charging == 1 && idle_ms >= setting_ms;
}

unsigned int CsFilter_frame(CsFilter* f, int open, unsigned int pressed, unsigned int held,
	unsigned int released, unsigned int keep, unsigned int toggle, int* close, int* toggled) {
	(void)released;
	*close = 0;
	*toggled = 0;
	if (open && (pressed & ~keep)) {
		if (toggle && (pressed & ~keep) == toggle) *toggled = 1;
		else *close = 1;
		f->swallowing = 1;
	}
	if (!f->swallowing) return 0;
	// this frame's releases are taken too, and then it's done
	if (!(held & ~keep)) f->swallowing = 0;
	return ~keep;
}

///////////////////////////////////////
// What it shows

void CS_formatLevel(int level, char* out, size_t size) {
	if (!size) return;
	if (level < 0 || level > 100) out[0] = '\0';
	else snprintf(out, size, "%d%%", level);
}

const char* CS_stateLabel(int level) {
	return level >= 100 ? TR_MARK("Charged") : TR_MARK("Charging");
}

void CS_formatClock(int hour, int minute, int mode, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (mode == CS_CLOCK_OFF || hour < 0 || hour > 23 || minute < 0 || minute > 59) return;
	if (mode == CS_CLOCK_12H) {
		int h = hour % 12;
		snprintf(out, size, "%d:%02d %s", h ? h : 12, minute, hour < 12 ? tr("AM") : tr("PM"));
	}
	else snprintf(out, size, "%02d:%02d", hour, minute);
}

int CS_fillWidth(int level, int inner) {
	if (inner <= 0 || level <= 0 || level > 100) return 0;
	int w = inner * level / 100;
	return w < 1 ? 1 : w;
}

///////////////////////////////////////
// The test-only fake battery

int CS_parseFake(const char* text, int* level, int* charging) {
	if (!text) return 0;
	int l, c;
	char extra;
	if (sscanf(text, " %d %d %c", &l, &c, &extra) != 2) return 0;
	if (c != 0 && c != 1) return 0;
	*level = l;
	*charging = c;
	return 1;
}
