// Charging screen: while the device charges and the game list has been
// left alone for the idle setting's time, a full screen with the battery
// level, whether it's charging or charged, and the clock, drawn dim on
// black.
//
// It keeps its own idle time, from input_filter, which sees every button
// on every screen: any button down, another screen showing, or the device
// waking starts the count again. It opens only over the game list. The
// home screen is next once the launcher can say which screen is home
// (#220). It's drawn only when what it shows changes, the level, the state
// or the clock's minute, never animating.
//
// Any button closes it, back to the list, and is taken until it's let go,
// so that press doesn't also act on the list. The power button is left to
// the launcher, so it still sleeps; waking closes the screen. It closes by
// itself when the device stops charging.
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

static const MosaicHost* host;
static int supported = 0;
static int clock_mode = CS_CLOCK_24H;
static CsIdle idle;
static CsFilter filter;
static int showing = 0;
static int activity = 0;       // a button was down since the last tick
static int close_pressed = 0;  // a button closed it
static int resumed = 0;        // the device woke since the last tick
static int refused = 0;        // open_screen was refused; logged once
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

static void wanted(void) {
	CS_formatLevel(level, want_level, sizeof(want_level));
	want_state = CS_stateLabel(level);
	time_t t = time(NULL);
	struct tm local;
	if (clock_mode != CS_CLOCK_OFF && localtime_r(&t, &local)) {
		CS_formatClock(local.tm_hour, local.tm_min, clock_mode, want_clock, sizeof(want_clock));
	}
	else want_clock[0] = '\0';
}

static int changed(void) {
	return strcmp(want_level, shown_level) || strcmp(want_state, shown_state) || strcmp(want_clock, shown_clock);
}

///////////////////////////////////////
// the screen

static int screenEnter(void* data) {
	(void)data;
	showing = 1;
	close_pressed = 0;
	shown_level[0] = shown_clock[0] = '\0';
	shown_state = "";
	return 0;
}

static void screenExit(void* data) {
	(void)data;
	showing = 0;
	close_pressed = 0;
	CsIdle_reset(&idle, last_now);
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

	if (strcmp(want_level, shown_level) || strcmp(want_state, shown_state)) {
		char message[96];
		snprintf(message, sizeof(message), "drew %s, %s", want_level[0] ? want_level : "no level", want_state);
		say(message);
	}
	snprintf(shown_level, sizeof(shown_level), "%s", want_level);
	snprintf(shown_clock, sizeof(shown_clock), "%s", want_clock);
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
	if (input->pressed | input->held | input->released) activity = 1;
	int shut = 0;
	*consumed = CsFilter_frame(&filter, showing, input->pressed, input->held, input->released,
		MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER), &shut);
	if (shut) close_pressed = 1;
	return 0;
}

// Where it opens over. The home screen joins the game list once the
// launcher can say which screen is home (#220).
static int onIdleScreen(void) {
	return !strcmp(host->current_screen(), "gamelist");
}

static void closeScreen(const char* why) {
	if (host->close_screen(SCREEN_ID) == 0) say(why);
}

static void tick(unsigned long now) {
	if (!supported) return;
	last_now = now;
	readBattery(now);
	if (activity || resumed) {
		activity = 0;
		CsIdle_reset(&idle, now);
	}

	if (showing) {
		if (resumed) closeScreen("closed: the device woke");
		else if (close_pressed) closeScreen("closed by a button");
		else if (charging != 1) closeScreen("closed: not charging");
		else if (now >= next_check) {
			next_check = now + CHECK_MS;
			wanted();
			if (changed()) host->request_redraw();
		}
		resumed = 0;
		return;
	}
	resumed = 0;

	if (!CsIdle_due(&idle, now, charging, onIdleScreen())) return;
	unsigned long waited = now - idle.last_input;
	wanted();
	next_check = now + CHECK_MS;
	if (host->open_screen(SCREEN_ID) != 0) {
		if (!refused++) say("couldn't open the screen; trying again after the next idle time");
		CsIdle_reset(&idle, now);
		return;
	}
	char message[96];
	snprintf(message, sizeof(message), "opened after %lus idle on the list: %s, %s", waited / 1000,
		want_level[0] ? want_level : "no level", want_state);
	say(message);
}

static void afterResume(void) {
	resumed = 1;
}

static int init(const MosaicHost* h) {
	host = h;
	supported = 0;
	showing = activity = close_pressed = resumed = refused = 0;
	filter = (CsFilter){0};
	if (!MOSAIC_HOST_HAS(host, open_screen) || !MOSAIC_HOST_HAS(host, hold_switch) || !MOSAIC_HOST_HAS(host, draw_text)
		|| !MOSAIC_HOST_HAS(host, is_charging)) {
		say("unsupported: this launcher has no plugin screens or input filters");
		return 0;
	}
	fake_path = getenv(FAKE_ENV);
	if (fake_path && !fake_path[0]) fake_path = NULL;
	if (!fake_path && host->is_charging() < 0) {
		say("unsupported on desktop: there's no battery, so the screen never opens");
		return 0;
	}

	unsigned long idle_ms = CS_parseIdle(host->cfg_get(ID, "idle", "60"));
	clock_mode = CS_parseClock(host->cfg_get(ID, "clock", "24h"));
	CsIdle_init(&idle, idle_ms);
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
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("couldn't register the screen");
		return 0;
	}
	supported = 1;
	char message[128];
	snprintf(message, sizeof(message), "opens after %lus idle on the list while charging, clock %s%s", idle_ms / 1000,
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
