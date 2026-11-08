// Hold lock: a button chord, held for a moment, locks every button the
// launcher sees, and the same chord unlocks it; with the switch setting,
// the hold switch on the tg5040 and tg5050 locks while it's on. A padlock
// shows in the header while it's locked.
//
// The lock is an input filter that takes every button while locked, on
// every screen. It watches the buttons held, which filters can't take, for
// the chord. A long power press still powers off, as the launcher always
// lets it through, but a short one doesn't sleep. The volume keys, and
// brightness with MENU, are stock's keymon's and can't be locked.
//
// The switch is stock's mute switch, so it's used only with the switch
// setting on, and it keeps muting as stock's Mute settings say. On devices
// without it, such as the h700 and desktop, the plugin logs that once and
// only the chord locks.

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "hold_model.h"

#define ID "hold-lock"
#define DEFAULT_CHORD "select+start"

static const MosaicHost* host;
static HoldLock lock;
static int has_switch = 0;
static MosaicRect logged_rect;
static int logged = 0;

static void say(const char* message) {
	host->log(ID, message);
}

static unsigned long nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000UL + (unsigned long)(ts.tv_nsec / 1000000);
}

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	int position = lock.use_switch && has_switch ? host->hold_switch() : -1;
	int change = HL_frame(&lock, input->held, position, nowMs(), consumed);
	switch (change) {
	case HL_LOCKED_CHORD: say("locked by the chord"); break;
	case HL_UNLOCKED_CHORD: say("unlocked by the chord"); break;
	case HL_LOCKED_SWITCH: say("locked by the switch"); break;
	case HL_UNLOCKED_SWITCH: say("unlocked by the switch"); break;
	}
	if (change != HL_SAME) {
		if (!HL_locked(&lock)) logged = 0;
		host->request_redraw();
	}
	return 0;
}

static int headerWidth(int max_width, int height) {
	if (!HL_locked(&lock) || height > max_width) return 0;
	return height;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	int pill = host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	MosaicColor pill_color = mosaic_color((unsigned int)pill, 255);
	MosaicColor ink_color = mosaic_color((unsigned int)ink, 255);
	HlPadlock p;
	HL_padlock(rect, &p);
	host->fill_round_rect(screen, rect, rect->h / 2, pill_color);
	host->fill_round_rect(screen, &p.shackle, p.shackle_radius, ink_color);
	host->fill_round_rect(screen, &p.hole, p.hole_radius, pill_color);
	host->fill_round_rect(screen, &p.body, p.body_radius, ink_color);
	host->fill_rect(screen, &p.keyhole, pill_color);

	if (!logged || memcmp(rect, &logged_rect, sizeof(*rect))) {
		char message[96];
		snprintf(message, sizeof(message), "padlock at %i,%i %ix%i", rect->x, rect->y, rect->w, rect->h);
		say(message);
		logged_rect = *rect;
		logged = 1;
	}
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, hold_switch) || !MOSAIC_HOST_HAS(host, decorate_cell)
		|| !MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, theme_color)) {
		say("unsupported: this launcher has no input filters or header widgets");
		return 1;
	}
	unsigned int chord = 0;
	const char* setting = host->cfg_get(ID, "chord", DEFAULT_CHORD);
	if (!HL_parseChord(setting, &chord)) {
		char message[160];
		snprintf(message, sizeof(message), "unknown chord \"%.64s\", using " DEFAULT_CHORD, setting);
		say(message);
		HL_parseChord(DEFAULT_CHORD, &chord);
	}
	unsigned long hold_ms = HL_parseHoldMs(host->cfg_get(ID, "hold", "1"), HL_HOLD_MS_DEFAULT);
	const char* use = host->cfg_get(ID, "switch", "off");
	int use_switch = !strcmp(use, "on") || !strcmp(use, "1");
	HL_init(&lock, chord, hold_ms, use_switch);
	logged = 0;

	has_switch = host->hold_switch() >= 0;
	if (!has_switch) {
		char message[160];
		snprintf(message, sizeof(message), "unsupported on %s: no hold switch, so only the chord locks", host->platform);
		say(message);
	}
	if (!chord && !(use_switch && has_switch)) say("nothing locks: the chord is off and the switch isn't used");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.header_width = headerWidth,
	.header_widget = headerWidget,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
