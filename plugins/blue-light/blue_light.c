// Blue light tint: a warm tint over every screen of the launcher, for
// evenings. Games aren't tinted.
//
// The launcher multiplies every frame it shows by the tint this plugin
// gives it (screen_tint), as the very last step, over dialogs, toasts,
// other plugins' overlays and screens, ambient screens included, so
// screenshots of the launcher have it too. White turns into the colour
// setting's colour at the strength setting's percent, and black stays
// black.
//
// A Warm tint tile on the quick menu (the "tile" setting) turns it on and
// off, as does the blue-light.toggle action, which the "button" setting
// puts on a game list button and an FN button can run. With the
// "schedule" setting on, it comes on at the "from" time and goes off at
// the "to" time, crossing midnight when "to" is earlier, without a button
// press; the toggle then flips it only until the next of those times,
// after which the schedule has it again. That forcing is kept in the
// plugin's cfg ("forced" and "until"), so it lasts through games and
// restarts. For the tests only, MOSAIC_BLUE_LIGHT_FAKE names a file
// holding the time as seconds since 1970, read in place of the clock.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "blue_light_model.h"

#define ID "blue-light"
#define TOGGLE_ID ID ".toggle"
#define FAKE_ENV "MOSAIC_BLUE_LIGHT_FAKE"
#define CHECK_MS 1000     // how often to look at the clock
#define FAKE_CHECK_MS 200 // and at the fake clock's file

static const MosaicHost* host;
static BLState state;
static MosaicColor tint;
static int active = 0;
static const char* fake_path = NULL;
static unsigned long next_check = 0;

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

// The time now, from the clock or the fake clock's file. 0 when there's
// none, as when the fake's file is missing.
static int readClock(BLClock* clock) {
	time_t t = time(NULL);
	if (fake_path) {
		FILE* f = fopen(fake_path, "r");
		if (!f) return 0;
		char text[32];
		size_t n = fread(text, 1, sizeof(text) - 1, f);
		fclose(f);
		text[n] = '\0';
		char* end = NULL;
		long long seconds = strtoll(text, &end, 10);
		if (end == text || seconds < 0) return 0;
		t = (time_t)seconds;
	}
	struct tm local;
	if (!localtime_r(&t, &local)) return 0;
	clock->now = (long long)t;
	clock->minute = local.tm_hour * 60 + local.tm_min;
	clock->second = local.tm_sec;
	return 1;
}

static void saveForced(void) {
	char until[32];
	snprintf(until, sizeof(until), "%lld", state.forced >= 0 ? state.until : 0);
	host->cfg_set("forced", state.forced < 0 ? "" : state.forced ? "on" : "off");
	host->cfg_set("until", until);
}

// The tint's state at clock: a forcing that ended is forgotten, and a
// change is drawn.
static void update(const BLClock* clock) {
	if (state.forced >= 0 && BL_forcedEnded(&state, clock)) {
		state.forced = -1;
		saveForced();
		say("the toggle's forcing ended at the schedule's edge");
	}
	int now_active = BL_active(&state, clock);
	if (now_active == active) return;
	active = now_active;
	char message[64];
	if (active) snprintf(message, sizeof(message), "tint on %02X%02X%02X", tint.r, tint.g, tint.b);
	else snprintf(message, sizeof(message), "tint off");
	say(message);
	host->request_redraw();
}

///////////////////////////////////////
// the toggle

static int always(const char* path, void* data) {
	(void)path;
	(void)data;
	return 1;
}

static int isOn(void* data) {
	(void)data;
	return active;
}

static void toggle(const char* path, void* data) {
	(void)path;
	(void)data;
	BLClock clock = { 0, 0, 0 };
	if (!readClock(&clock)) {
		// no clock: only the toggle's own state means anything
		state.scheduled = 0;
	}
	BL_toggle(&state, &clock);
	host->cfg_set("on", state.manual ? "on" : "off");
	saveForced();
	update(&clock);
	char message[96];
	if (state.forced == 0) {
		int edge = 0;
		char until[8];
		BL_toNextEdge(clock.minute, state.from, state.to, &edge);
		BL_formatTime(edge, until, sizeof(until));
		snprintf(message, sizeof(message), tr("Warm tint off until %s"), until);
	}
	else snprintf(message, sizeof(message), "%s", active ? tr("Warm tint on") : tr("Warm tint off"));
	if (state.forced >= 0) say(state.forced ? "forced on until the schedule's next edge" : "forced off until the schedule's next edge");
	toast(message);
}

///////////////////////////////////////
// hooks

static void tick(unsigned long now) {
	if (now < next_check) return;
	next_check = now + (fake_path ? FAKE_CHECK_MS : CHECK_MS);
	BLClock clock;
	if (readClock(&clock)) update(&clock);
}

static int screenTint(MosaicColor* out) {
	if (!active) return 0;
	*out = tint;
	return 1;
}

// A setting's time, or -1 after logging why it's not used.
static int timeSetting(const char* key, const char* fallback) {
	const char* text = host->cfg_get(key, fallback);
	int minutes = BL_parseTime(text);
	if (minutes < 0) {
		char message[160];
		snprintf(message, sizeof(message), "%s is %.20s, not a time such as 21:00, so the schedule is off", key, text);
		say(message);
	}
	return minutes;
}

static void readSettings(void) {
	char message[160];
	const char* name = host->cfg_get("colour", "amber");
	int colour = BL_colour(name);
	if (colour < 0) {
		snprintf(message, sizeof(message), "colour %.20s isn't amber, orange or red, so it's amber", name);
		say(message);
		colour = BL_AMBER;
	}
	const char* text = host->cfg_get("strength", "50");
	int strength = BL_strength(text);
	if (strength < 0) {
		snprintf(message, sizeof(message), "strength %.20s isn't 10 to 100 in steps of 10, so it's %d", text,
			BL_STRENGTH_DEFAULT);
		say(message);
		strength = BL_STRENGTH_DEFAULT;
	}
	tint = BL_tint(colour, strength);

	state.manual = !strcmp(host->cfg_get("on", "off"), "on");
	state.scheduled = !strcmp(host->cfg_get("schedule", "off"), "on");
	state.from = state.to = 0;
	if (state.scheduled) {
		state.from = timeSetting("from", "21:00");
		state.to = timeSetting("to", "07:00");
		if (state.from < 0 || state.to < 0) state.scheduled = 0;
		else if (state.from == state.to) {
			say("the schedule turns on and off at the same time, so it's off");
			state.scheduled = 0;
		}
	}
	const char* forced = host->cfg_get("forced", "");
	state.forced = !strcmp(forced, "on") ? 1 : !strcmp(forced, "off") ? 0 : -1;
	state.until = strtoll(host->cfg_get("until", "0"), NULL, 10);
	if (!state.scheduled) state.forced = -1;
}

static int init(const MosaicHost* h) {
	host = h;
	active = 0;
	next_check = 0;
	if (!MOSAIC_HOST_HAS(host, tint_shown)) {
		say("unsupported: this launcher has no screen tint");
		return 1;
	}
	fake_path = getenv(FAKE_ENV);
	if (fake_path && !fake_path[0]) fake_path = NULL;
	readSettings();

	if (host->register_action(TOGGLE_ID, "Warm tint", 0, always, toggle, NULL) != 0) {
		say("couldn't register " TOGGLE_ID);
		return 1;
	}
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(TOGGLE_ID, MOSAIC_QUICK_TOP, NULL, NULL, isOn) != 0)) {
		say("couldn't add the Warm tint tile");
	}
	int button = BL_button(host->cfg_get("button", "none"));
	if (button >= 0 && host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, TOGGLE_ID) != 0) {
		say("couldn't bind the Warm tint button");
	}
	BLClock clock;
	if (readClock(&clock)) update(&clock);
	char message[96];
	snprintf(message, sizeof(message), "ready, tint %02X%02X%02X, %s", tint.r, tint.g, tint.b,
		state.scheduled ? "scheduled" : "not scheduled");
	say(message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.screen_tint = screenTint,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
