// A plugin loaded after remapper that wants the same things: A, a binding
// on the Y remapper took, and the launch remapper replaced.

#include <stdarg.h>
#include <stdio.h>
#include "nextui_plugin.h"

#define ID "rival"

static const NUIHost* host;

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

static int preLaunch(const NUILaunch* launch) {
	(void)launch;
	say("replace %i", host->replace_launch(host->cfg_get(ID, "emu", ""), NULL, 0));
	return NUI_LAUNCH_REPLACE;
}

static int init(const NUIHost* h) {
	host = h;
	say("override A %i, B %i",
		host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_A, NUI_TRIGGER_RELEASE, "gamelist.open"),
		host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_B, NUI_TRIGGER_PRESS, "gamelist.back"));
	host->register_action("rival.y", "RIVAL", NULL, runY, NULL);
	say("bind Y %i", host->bind_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_Y, NUI_TRIGGER_PRESS, "rival.y"));
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
