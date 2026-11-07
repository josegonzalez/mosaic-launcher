// A plugin that reads the theme colours and has its LED colour refused,
// since desktop has no LEDs.

#include <stdarg.h>
#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "lighter"

static const MosaicHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, theme_color)) return 1;
	say("main %06X, accent %06X, background %06X, 0 %i, 8 %i",
		(unsigned)host->theme_color(MOSAIC_COLOR_MAIN),
		(unsigned)host->theme_color(MOSAIC_COLOR_ACCENT),
		(unsigned)host->theme_color(MOSAIC_COLOR_BACKGROUND),
		host->theme_color(0), host->theme_color(8));
	say("leds %i, set %i, clear %i", host->led_count(),
		host->led_set_color(ID, 0xFF0000), host->led_clear(ID));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
