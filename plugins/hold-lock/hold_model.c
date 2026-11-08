#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "hold_model.h"

typedef struct {
	const char* name;
	int button;
} HlName;

static const HlName names[] = {
	{ "a", MOSAIC_BUTTON_A },
	{ "b", MOSAIC_BUTTON_B },
	{ "x", MOSAIC_BUTTON_X },
	{ "y", MOSAIC_BUTTON_Y },
	{ "l1", MOSAIC_BUTTON_L1 },
	{ "r1", MOSAIC_BUTTON_R1 },
	{ "l2", MOSAIC_BUTTON_L2 },
	{ "r2", MOSAIC_BUTTON_R2 },
	{ "start", MOSAIC_BUTTON_START },
	{ "select", MOSAIC_BUTTON_SELECT },
	{ "menu", MOSAIC_BUTTON_MENU },
	{ "up", MOSAIC_BUTTON_UP },
	{ "down", MOSAIC_BUTTON_DOWN },
	{ "left", MOSAIC_BUTTON_LEFT },
	{ "right", MOSAIC_BUTTON_RIGHT },
};
#define NAMES (int)(sizeof(names) / sizeof(names[0]))

static int named(const char* text, size_t len) {
	for (int i = 0; i < NAMES; i++) {
		if (strlen(names[i].name) == len && !strncasecmp(names[i].name, text, len)) return names[i].button;
	}
	return -1;
}

int HL_parseChord(const char* text, unsigned int* mask) {
	if (!text || !mask) return 0;
	if (!strcasecmp(text, "off")) {
		*mask = 0;
		return 1;
	}
	unsigned int bits = 0;
	const char* p = text;
	for (;;) {
		const char* end = strchr(p, '+');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		int button = named(p, len);
		if (button < 0) return 0;
		bits |= MOSAIC_BUTTON_BIT(button);
		if (!end) break;
		p = end + 1;
	}
	*mask = bits;
	return 1;
}

unsigned long HL_parseHoldMs(const char* text, unsigned long fallback) {
	if (!text || !isdigit((unsigned char)text[0])) return fallback;
	char* end = NULL;
	long seconds = strtol(text, &end, 10);
	if (*end || seconds < 1 || seconds > HL_HOLD_SECONDS_MAX) return fallback;
	return (unsigned long)seconds * 1000;
}

void HL_init(HoldLock* lock, unsigned int chord, unsigned long hold_ms, int use_switch) {
	memset(lock, 0, sizeof(*lock));
	lock->chord = chord;
	lock->hold_ms = hold_ms;
	lock->use_switch = use_switch;
}

int HL_locked(const HoldLock* lock) {
	return lock->chord_locked || lock->switch_locked;
}

int HL_frame(HoldLock* lock, unsigned int held, int switch_pos, unsigned long now, unsigned int* consumed) {
	int change = HL_SAME;

	int on = lock->use_switch && switch_pos == 1;
	if (on != lock->switch_locked) {
		lock->switch_locked = on;
		change = on ? HL_LOCKED_SWITCH : HL_UNLOCKED_SWITCH;
		if (!HL_locked(lock)) lock->draining = 1;
	}

	int whole = lock->chord && (held & lock->chord) == lock->chord;
	if (!whole) {
		lock->holding = 0;
		lock->fired = 0;
	} else if (!lock->holding) {
		lock->holding = 1;
		lock->since = now;
	}
	if (whole && !lock->fired && now - lock->since >= lock->hold_ms) {
		lock->fired = 1;
		if (lock->switch_locked) {
			// the switch holds the lock; the chord waits for it
		} else if (lock->chord_locked) {
			lock->chord_locked = 0;
			lock->draining = 1;
			change = HL_UNLOCKED_CHORD;
		} else {
			lock->chord_locked = 1;
			change = HL_LOCKED_CHORD;
		}
	}

	*consumed = HL_locked(lock) || lock->draining ? ~0u : 0;
	// the frame everything comes up still takes its releases
	if (lock->draining && !held) lock->draining = 0;
	return change;
}

void HL_padlock(const MosaicRect* rect, HlPadlock* out) {
	memset(out, 0, sizeof(*out));
	int size = (rect->w < rect->h ? rect->w : rect->h) * 3 / 5;
	if (size < 6) size = 6;
	int cx = rect->x + rect->w / 2;
	int top = rect->y + (rect->h - size) / 2;
	int t = size / 9 > 2 ? size / 9 : 2;

	int bw = size * 3 / 4, bh = size * 11 / 20;
	out->body = (MosaicRect){ cx - bw / 2, top + size - bh, bw, bh };
	out->body_radius = size / 12 > 1 ? size / 12 : 1;

	int sw = bw * 5 / 8;
	out->shackle = (MosaicRect){ cx - sw / 2, top, sw, out->body.y - top + bh / 2 };
	out->shackle_radius = sw / 2;
	int hw = sw - 2 * t > 1 ? sw - 2 * t : 1;
	out->hole = (MosaicRect){ cx - hw / 2, top + t, hw, out->shackle.h - t };
	out->hole_radius = hw / 2;

	int kw = size / 10 > 2 ? size / 10 : 2, kh = bh * 2 / 5 > 2 ? bh * 2 / 5 : 2;
	out->keyhole = (MosaicRect){ cx - kw / 2, out->body.y + (bh - kh) / 2, kw, kh };
}
