// A plugin that reads stock toggles, has a name off the list refused, and
// flips Show clock from its first tick.

#include <stdarg.h>
#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "toggler"

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

static void tick(unsigned long now) {
	(void)now;
	static int ticked = 0;
	if (ticked++) return;
	int clock = host->stock_toggle_get(MOSAIC_TOGGLE_SHOW_CLOCK);
	say("set clock %i, set wifi %i, set clock to 2 %i",
		host->stock_toggle_set(MOSAIC_TOGGLE_SHOW_CLOCK, !clock),
		host->stock_toggle_set("wifi", 1),
		host->stock_toggle_set(MOSAIC_TOGGLE_SHOW_CLOCK, 2));
	say("clock now %i", host->stock_toggle_get(MOSAIC_TOGGLE_SHOW_CLOCK));
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, stock_toggle_set)) return 1;
	say("clock %i, haptics %i, wifi %i",
		host->stock_toggle_get(MOSAIC_TOGGLE_SHOW_CLOCK),
		host->stock_toggle_get(MOSAIC_TOGGLE_HAPTICS),
		host->stock_toggle_get("wifi"));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
