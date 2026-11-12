// A plugin loaded after remapper that wants the same things: A, a binding
// on the Y remapper took, and the launch remapper replaced.

#include <stdarg.h>
#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "rival"

static const MosaicHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void runY(void* data) {
	(void)data;
	say("y ran");
}

static int preLaunch(const MosaicLaunch* launch) {
	(void)launch;
	say("replace %i", host->replace_launch(host->cfg_get("emu", ""), NULL, 0));
	return MOSAIC_LAUNCH_REPLACE;
}

static int init(const MosaicHost* h) {
	host = h;
	say("override A %i, B %i",
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_RELEASE, "gamelist.open"),
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_B, MOSAIC_TRIGGER_PRESS, "gamelist.back"));
	host->register_action("rival.y", "RIVAL", NULL, runY, NULL);
	say("bind Y %i", host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "rival.y"));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
