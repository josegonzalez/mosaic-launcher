// LED status: the LEDs show the battery level or a theme colour.
//
// In battery mode it reads the battery every 5 seconds while the launcher
// is on screen, and lights the LEDs red at or below low_at, yellow at or
// below mid_at, and green above. A failed reading, 0 or out of range,
// keeps the colour it had, and a band is left upwards only 2 points past
// its threshold, so readings around one don't flicker. In theme mode the
// LEDs take one of stock's theme colours, looked at again every 5 seconds.
// In auto mode, the default, they take the theme slot owner's
// led-status.colour key (a colour, a theme colour's name or battery), or
// show the battery without one; the key is read again on
// launcher.theme_changed, and the LEDs change straight away.
//
// It sets the colour through the host's led_set_color, only when it
// changes, since stock sends it to the LEDs every frame while it's on.
// Stock's charging, critical and low battery, mute and sleep lights still
// come first. The launcher puts the user's LED settings back after quit
// and before a power-off, and a game sets the LEDs its own way, so init
// starts again when the launcher comes back. Without LEDs, such as on
// desktop, or on a launcher without LED colours, it loads and does
// nothing.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "led_model.h"

#define ID "led-status"

// as in plugin.txt
#define LS_LOW_DEFAULT 20
#define LS_MID_DEFAULT 50

static const MosaicHost* host;
static int supported = 0;
static int mode = LS_MODE_BATTERY;
static int theme_index = MOSAIC_COLOR_ACCENT;
static LsBattery battery;
static LsApplied applied;
static unsigned long next_sample = 0;
static int following = 0; // 1 once the settings are read, so the theme's key is followed
static int resolved = 0;  // 0 until the theme's key is read, and again when the theme changes
static LsSource source;
static char shown[96];    // what the log last said the LEDs show

static void say(const char* message) {
	host->log(ID, message);
}

static void resolve(void) {
	const char* colour = MOSAIC_HOST_HAS(host, theme_value) ? host->theme_value(ID ".colour", NULL) : NULL;
	source = Ls_source(mode, theme_index, colour);
	resolved = 1;
	char now[96];
	Ls_describe(&source, now, sizeof(now));
	if (!strcmp(now, shown)) return;
	snprintf(shown, sizeof(shown), "%s", now);
	char line[128];
	snprintf(line, sizeof(line), "the LEDs show %s", now);
	say(line);
}

// the theme's key may have changed
static void themeChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	next_sample = 0;
	resolve();
}

static int wantedColor(void) {
	if (source.show == LS_SHOW_RGB) return source.value;
	if (source.show == LS_SHOW_THEME) return host->theme_color(source.value);
	return LsBattery_sample(&battery, host->battery_level());
}

static void update(void) {
	int rgb = wantedColor();
	if (!LsApplied_wanted(&applied, rgb)) return;
	int result = host->led_set_color(ID, (unsigned int)rgb);
	if (result != 0 && applied.refused < 0) say("the launcher refused the LED colour, another plugin's may be on");
	LsApplied_result(&applied, rgb, result);
}

static void tick(unsigned long now) {
	// the theme is first put in place after every plugin's init
	if (following && !resolved) resolve();
	if (!supported || !Ls_due(&next_sample, now)) return;
	update();
}

// the battery may have changed while the device slept
static void afterResume(void) {
	next_sample = 0;
}

static int init(const MosaicHost* h) {
	host = h;
	supported = 0;
	following = 0;
	resolved = 0;
	shown[0] = '\0';
	next_sample = 0;
	LsApplied_init(&applied);
	if (!MOSAIC_HOST_HAS(host, led_set_color) || !MOSAIC_HOST_HAS(host, theme_color) || !MOSAIC_HOST_HAS(host, battery_level)) {
		say("unsupported: this launcher can't set LED colours");
		return 0;
	}
	// without LEDs it still says what they'd show, and follows the theme's key
	supported = host->led_count() > 0;
	if (!supported) say("unsupported on this platform: it has no LEDs to set");

	mode = Ls_parseMode(host->cfg_get("mode", "auto"));
	theme_index = Ls_parseThemeColor(host->cfg_get("theme_color", "accent"));
	int low_at = Ls_parseNumber(host->cfg_get("low_at", "20"), 1, 99, LS_LOW_DEFAULT);
	int mid_at = Ls_parseNumber(host->cfg_get("mid_at", "50"), 1, 99, LS_MID_DEFAULT);
	if (supported && mode != LS_MODE_THEME && low_at >= mid_at) say("low_at isn't below mid_at, so there's no yellow");
	LsBattery_init(&battery, low_at, mid_at);
	following = 1;
	if (MOSAIC_HOST_HAS(host, subscribe) && host->subscribe("launcher.theme_changed", themeChanged, NULL) != 0) {
		say("couldn't follow theme changes");
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.after_resume = afterResume,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
