// Charging screen: while the device charges and the game list or home has
// been left alone for the idle setting's time, a full screen with the
// battery level, whether it's charging or charged, and the clock, drawn dim
// on black, and what music plays, while the music plugin has a track.
//
// It's an ambient screen (register_ambient): the launcher keeps the idle
// time and opens it over the game list, home or a later ambient screen,
// such as the screensaver, in the user's order of ambient screens, once
// wants says the device charges and the idle time has passed. It's drawn
// only when what it shows changes, the level, the state, the clock's minute
// or what music plays, never animating.
//
// Any button closes it, back to the list or home, and is taken until it's
// let go, so that press doesn't also act on the screen under it, except X
// while music has a track, which plays or pauses it (music.toggle) and
// stays. The power button is left to the launcher, so it still sleeps;
// waking closes the screen. It closes by itself when the device stops
// charging.
//
// It never holds off sleep. Stock's launcher already never turns the
// screen off or sleeps on its own while charging (PWR_update counts
// charging as input, and its suspend timeout waits too), with or without
// this screen, and its count starts again when the charger comes out.
//
// On a platform without a battery, such as desktop, it loads and does
// nothing. For the tests only, MOSAIC_CHARGING_SCREEN_FAKE names a file
// holding "<level> <charging>", such as "73 1", read in place of the
// battery.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "charging_model.h"

#define ID "charging-screen"
#define SCREEN_ID "charging-screen.main"
#define FAKE_ENV "MOSAIC_CHARGING_SCREEN_FAKE"
#define FAKE_READ_MS 200
#define CHECK_MS 1000 // how often to look at the clock while it shows
#define DIM 110       // alpha of everything drawn over the black
#define LINE_MAX 512  // the now playing line

static const MosaicHost* host;
static int supported = 0;
static int clock_mode = CS_CLOCK_24H;
static unsigned long idle_ms = CS_IDLE_DEFAULT_MS;
static unsigned long opened_idle = 0; // the idle time wants opened it after
static CsFilter filter;
static int showing = 0;
static int close_pressed = 0;  // a button closed it
static int toggle_pressed = 0; // X played or paused music
static int resumed = 0;        // the device woke since the last tick
static unsigned long last_now = 0;
static unsigned long next_check = 0;

// the battery as last read
static int level = -1, charging = -1;
static const char* fake_path = NULL;
static unsigned long next_fake = 0;

// what's on screen, to draw again only when it changes
static char shown_level[8], shown_clock[16];
static const char* shown_state = "";
static char want_level[8], want_clock[16];
static const char* want_state = "";
// the music plugin's now playing line, "" without a track
static char shown_music[LINE_MAX], want_music[LINE_MAX];

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}


static void say(const char* message) {
	host->log(ID, message);
}

static void readBattery(unsigned long now) {
	if (!fake_path) {
		level = host->battery_level();
		charging = host->is_charging();
		return;
	}
	if (now < next_fake) return;
	next_fake = now + FAKE_READ_MS;
	level = charging = -1;
	FILE* f = fopen(fake_path, "r");
	if (!f) return;
	char text[64];
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[n] = '\0';
	if (!CS_parseFake(text, &level, &charging)) level = charging = -1;
}

// What music plays, from the music plugin's music.now, or "".
static void musicLine(char* out, size_t size) {
	out[0] = '\0';
	char reply[MOSAIC_CALL_MAX];
	if (!MOSAIC_HOST_HAS(host, call) || host->call("music.now", NULL, reply, sizeof(reply)) < 0) return;
	if (host->event_value(reply, "line", out, size) < 0) out[0] = '\0';
}

static void wanted(void) {
	CS_formatLevel(level, want_level, sizeof(want_level));
	want_state = tr(CS_stateLabel(level));
	time_t t = time(NULL);
	struct tm local;
	if (clock_mode != CS_CLOCK_OFF && localtime_r(&t, &local)) {
		CS_formatClock(local.tm_hour, local.tm_min, clock_mode, want_clock, sizeof(want_clock));
	}
	else want_clock[0] = '\0';
	musicLine(want_music, sizeof(want_music));
}

static int changed(void) {
	return strcmp(want_level, shown_level) || strcmp(want_state, shown_state) || strcmp(want_clock, shown_clock)
		|| strcmp(want_music, shown_music);
}

///////////////////////////////////////
// the screen

static int screenEnter(void* data) {
	(void)data;
	showing = 1;
	close_pressed = toggle_pressed = 0;
	shown_level[0] = shown_clock[0] = shown_music[0] = '\0';
	shown_state = "";
	wanted();
	next_check = last_now + CHECK_MS;
	char message[96];
	snprintf(message, sizeof(message), "opened after %lus idle: %s, %s", opened_idle / 1000,
		want_level[0] ? want_level : "no level", want_state);
	say(message);
	return 0;
}

static void screenExit(void* data) {
	(void)data;
	showing = 0;
	close_pressed = toggle_pressed = 0;
}

// buttons are handled in input_filter, so they never reach the screen
static int screenInput(const MosaicInput* input, void* data) {
	(void)input;
	(void)data;
	return MOSAIC_SCREEN_STAY;
}

static MosaicColor dim(int index) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? 0xFFFFFF : (unsigned int)rgb, DIM);
}

static int screenRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	const MosaicRect area = view->area;
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	host->fill_rect(screen, &area, mosaic_color(0x000000, 255));

	// the battery, a quarter of the screen wide and half as tall, with its nub
	int body_w = area.w / 4, body_h = body_w / 2;
	int nub_w = body_w / 16, nub_h = body_h / 3;
	int line = 3 * scale;
	int large = host->text_height(MOSAIC_FONT_LARGE);
	int small = host->text_height(MOSAIC_FONT_SMALL);
	int gap = 4 * scale;
	int clock_h = want_clock[0] ? large + gap * 2 : 0;
	int total = clock_h + body_h + gap * 2 + large + small;
	int y = area.y + (area.h - total) / 2;

	MosaicColor fg = dim(MOSAIC_COLOR_MAIN);
	MosaicColor hint = dim(MOSAIC_COLOR_HINT);
	if (want_clock[0]) {
		int w = host->text_width(MOSAIC_FONT_LARGE, want_clock);
		host->draw_text(screen, MOSAIC_FONT_LARGE, want_clock, area.x + (area.w - w) / 2, y, fg, 0);
		y += clock_h;
	}

	MosaicRect body = { area.x + (area.w - body_w - nub_w) / 2, y, body_w, body_h };
	MosaicRect nub = { body.x + body.w, body.y + (body_h - nub_h) / 2, nub_w, nub_h };
	MosaicRect inner = { body.x + line, body.y + line, body.w - 2 * line, body.h - 2 * line };
	host->fill_round_rect(screen, &body, 4 * scale, fg);
	host->fill_rect(screen, &nub, fg);
	host->fill_round_rect(screen, &inner, 2 * scale, mosaic_color(0x000000, 255));
	MosaicRect bar = { inner.x + line, inner.y + line, 0, inner.h - 2 * line };
	bar.w = CS_fillWidth(level, inner.w - 2 * line);
	if (bar.w > 0) host->fill_round_rect(screen, &bar, scale, dim(MOSAIC_COLOR_ACCENT));
	y += body_h + gap * 2;

	if (want_level[0]) {
		int w = host->text_width(MOSAIC_FONT_LARGE, want_level);
		host->draw_text(screen, MOSAIC_FONT_LARGE, want_level, area.x + (area.w - w) / 2, y, fg, 0);
	}
	y += large;
	int w = host->text_width(MOSAIC_FONT_SMALL, want_state);
	host->draw_text(screen, MOSAIC_FONT_SMALL, want_state, area.x + (area.w - w) / 2, y, hint, 0);

	// what music plays, at the bottom
	if (want_music[0]) {
		int margin = 10 * scale;
		char cut[LINE_MAX];
		int mw = host->text_truncate(MOSAIC_FONT_SMALL, want_music, area.w - 2 * margin, cut, sizeof(cut));
		if (mw > 0) {
			host->draw_text(screen, MOSAIC_FONT_SMALL, cut, area.x + (area.w - mw) / 2, area.y + area.h - margin - small,
				hint, 0);
		}
	}
	if (strcmp(want_music, shown_music)) {
		char message[LINE_MAX + 16];
		snprintf(message, sizeof(message), "music: %s", want_music[0] ? want_music : "none");
		say(message);
	}

	if (strcmp(want_level, shown_level) || strcmp(want_state, shown_state)) {
		char message[96];
		snprintf(message, sizeof(message), "drew %s, %s", want_level[0] ? want_level : "no level", want_state);
		say(message);
	}
	snprintf(shown_level, sizeof(shown_level), "%s", want_level);
	snprintf(shown_clock, sizeof(shown_clock), "%s", want_clock);
	snprintf(shown_music, sizeof(shown_music), "%s", want_music);
	shown_state = want_state;
	return 0;
}

static int screenAnimating(void* data) {
	(void)data;
	return 0;
}

///////////////////////////////////////
// hooks

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	if (!supported) return 0;
	int shut = 0, toggled = 0;
	unsigned int toggle = showing && want_music[0] ? MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X) : 0;
	*consumed = CsFilter_frame(&filter, showing, input->pressed, input->held, input->released,
		MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER), toggle, &shut, &toggled);
	if (shut) close_pressed = 1;
	if (toggled) toggle_pressed = 1;
	return 0;
}

static int wantsToShow(unsigned long idle, void* data) {
	(void)data;
	if (!CS_wants(idle, idle_ms, charging)) return 0;
	opened_idle = idle;
	return 1;
}

static void closeScreen(const char* why) {
	if (host->close_screen(SCREEN_ID) == 0) say(why);
}

// X: plays or pauses music, staying open.
static void toggleMusic(void) {
	char reply[MOSAIC_CALL_MAX], state[16] = "";
	if (host->call("music.toggle", NULL, reply, sizeof(reply)) >= 0) host->event_value(reply, "state", state, sizeof(state));
	char message[64];
	snprintf(message, sizeof(message), "X: music %s", state[0] ? state : "didn't answer");
	say(message);
	wanted();
	if (changed()) host->request_redraw();
}

static void tick(unsigned long now) {
	if (!supported) return;
	last_now = now;
	readBattery(now);
	if (showing) {
		if (resumed) closeScreen("closed: the device woke");
		else if (close_pressed) closeScreen("closed by a button");
		else if (charging != 1) closeScreen("closed: not charging");
		else if (toggle_pressed) {
			toggle_pressed = 0;
			toggleMusic();
		}
		else if (now >= next_check) {
			next_check = now + CHECK_MS;
			wanted();
			if (changed()) host->request_redraw();
		}
	}
	resumed = 0;
}

static void afterResume(void) {
	resumed = 1;
}

static int init(const MosaicHost* h) {
	host = h;
	static const CSWords words = { tr, NULL };
	CS_setWords(&words);
	supported = 0;
	showing = close_pressed = toggle_pressed = resumed = 0;
	want_music[0] = shown_music[0] = '\0';
	filter = (CsFilter){0};
	if (!MOSAIC_HOST_HAS(host, register_ambient)) {
		say("unsupported: this launcher has no ambient screens");
		return 0;
	}
	fake_path = getenv(FAKE_ENV);
	if (fake_path && !fake_path[0]) fake_path = NULL;
	if (!fake_path && host->is_charging() < 0) {
		say("unsupported on desktop: there's no battery, so the screen never opens");
		return 0;
	}

	idle_ms = CS_parseIdle(host->cfg_get("idle", "60"));
	clock_mode = CS_parseClock(host->cfg_get("clock", "24h"));
	if (!idle_ms) {
		say("idle is off, so the screen never opens");
		return 0;
	}

	MosaicScreenHooks hooks = {
		.size = sizeof(hooks),
		.flags = MOSAIC_SCREEN_FULL,
		.enter = screenEnter,
		.exit = screenExit,
		.input = screenInput,
		.render = screenRender,
		.animating = screenAnimating,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0 || host->register_ambient(SCREEN_ID, wantsToShow, NULL) != 0) {
		say("couldn't register the screen");
		return 0;
	}
	supported = 1;
	char message[128];
	snprintf(message, sizeof(message), "opens after %lus idle on the list or home while charging, clock %s%s", idle_ms / 1000,
		clock_mode == CS_CLOCK_OFF ? "off" : clock_mode == CS_CLOCK_12H ? "12h" : "24h",
		fake_path ? ", with the test battery" : "");
	say(message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.after_resume = afterResume,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
