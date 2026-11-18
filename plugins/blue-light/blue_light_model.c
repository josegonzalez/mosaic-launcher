#include <stdio.h>
#include <string.h>
#include "blue_light_model.h"

static const char* const colour_names[BL_COLOURS] = { "amber", "orange", "red" };
static const MosaicColor colours[BL_COLOURS] = {
	{ 255, 191, 0, 255 }, // amber
	{ 255, 128, 0, 255 }, // orange
	{ 255, 0, 0, 255 },   // red
};

int BL_colour(const char* name) {
	for (int i = 0; name && i < BL_COLOURS; i++) {
		if (!strcmp(name, colour_names[i])) return i;
	}
	return -1;
}

MosaicColor BL_colourAt100(int colour) {
	if (colour < 0 || colour >= BL_COLOURS) colour = BL_AMBER;
	return colours[colour];
}

int BL_strength(const char* text) {
	if (!text) return -1;
	int value = 0, digits = 0;
	for (const char* p = text; *p; p++) {
		if (*p < '0' || *p > '9' || ++digits > 3) return -1;
		value = value * 10 + (*p - '0');
	}
	if (!digits || value < BL_STRENGTH_STEP || value > 100 || value % BL_STRENGTH_STEP) return -1;
	return value;
}

static unsigned char toward(unsigned char full, int strength) {
	return (unsigned char)(255 - ((255 - full) * strength + 50) / 100);
}

MosaicColor BL_tint(int colour, int strength) {
	MosaicColor full = BL_colourAt100(colour);
	if (strength < 0) strength = 0;
	if (strength > 100) strength = 100;
	return (MosaicColor){ toward(full.r, strength), toward(full.g, strength), toward(full.b, strength), 255 };
}

int BL_parseTime(const char* text) {
	if (!text || strlen(text) != 5 || text[2] != ':') return -1;
	for (int i = 0; i < 5; i++) {
		if (i != 2 && (text[i] < '0' || text[i] > '9')) return -1;
	}
	int hours = (text[0] - '0') * 10 + (text[1] - '0');
	int minutes = (text[3] - '0') * 10 + (text[4] - '0');
	if (hours > 23 || minutes > 59) return -1;
	return hours * 60 + minutes;
}

void BL_formatTime(int minutes, char* out, int size) {
	minutes = ((minutes % BL_DAY) + BL_DAY) % BL_DAY;
	snprintf(out, (size_t)size, "%02d:%02d", minutes / 60, minutes % 60);
}

int BL_inRange(int minute, int from, int to) {
	if (from == to) return 0;
	if (from < to) return minute >= from && minute < to;
	return minute >= from || minute < to;
}

// minutes from minute to the edge, 1 to BL_DAY: an edge at minute now is
// a day away
static int ahead(int minute, int edge) {
	int wait = ((edge - minute) % BL_DAY + BL_DAY) % BL_DAY;
	return wait ? wait : BL_DAY;
}

int BL_toNextEdge(int minute, int from, int to, int* edge) {
	if (from == to) return -1;
	int to_from = ahead(minute, from), to_to = ahead(minute, to);
	if (edge) *edge = to_from < to_to ? from : to;
	return to_from < to_to ? to_from : to_to;
}

int BL_button(const char* name) {
	if (!name) return -1;
	if (!strcmp(name, "Y")) return MOSAIC_BUTTON_Y;
	if (!strcmp(name, "L2")) return MOSAIC_BUTTON_L2;
	if (!strcmp(name, "R2")) return MOSAIC_BUTTON_R2;
	if (!strcmp(name, "SELECT")) return MOSAIC_BUTTON_SELECT;
	return -1;
}

// a schedule with an empty range is no schedule
static int scheduled(const BLState* s) {
	return s->scheduled && s->from != s->to;
}

int BL_forcedEnded(const BLState* s, const BLClock* clock) {
	return s->forced >= 0 && clock->now >= s->until;
}

int BL_active(const BLState* s, const BLClock* clock) {
	if (!scheduled(s)) return s->manual ? 1 : 0;
	if (s->forced >= 0 && !BL_forcedEnded(s, clock)) return s->forced ? 1 : 0;
	return BL_inRange(clock->minute, s->from, s->to);
}

void BL_toggle(BLState* s, const BLClock* clock) {
	if (!scheduled(s)) {
		s->manual = !s->manual;
		return;
	}
	int want = !BL_active(s, clock);
	if (want == BL_inRange(clock->minute, s->from, s->to)) {
		s->forced = -1;
		s->until = 0;
		return;
	}
	s->forced = want;
	s->until = clock->now - clock->second + (long long)BL_toNextEdge(clock->minute, s->from, s->to, NULL) * 60;
}
