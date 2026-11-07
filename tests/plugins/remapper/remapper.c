// A plugin that uses the button override, launch replacement and quick
// tile rename host calls, logging what each returned.
//
// In init it swaps A and X, takes Y away from everyone, and relabels the
// Games tile. Once the launcher is up it opens the game named by the "rom"
// setting, and its pre_launch replaces the launch with the "emu" setting's
// program, so the launcher writes that command and quits. On the start
// after, post_return logs that it ran.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "nextui_plugin.h"

#define ID "remapper"

static const NUIHost* host;
static int ticks = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int preLaunch(const NUILaunch* launch) {
	say("pre_launch %s", launch->path);
	const char* args[] = { "--core", "x's.so" };
	say("replace missing %i", host->replace_launch("/nowhere/emu.sh", args, 2));
	say("replace %i", host->replace_launch(host->cfg_get(ID, "emu", ""), args, 2));
	host->cfg_set(ID, "launched", "1");
	return NUI_LAUNCH_REPLACE;
}

static void postReturn(void) {
	say("post_return");
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	if (ticks != 1) return;
	say("override from tick %i", host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_L2, NUI_TRIGGER_PRESS, "gamelist.open"));
	if (strcmp(host->cfg_get(ID, "launched", "0"), "1") == 0) return;
	say("open rom %i", host->open_path(host->cfg_get(ID, "rom", ""), NUI_ENTRY_ROM));
}

static int init(const NUIHost* h) {
	host = h;
	if (!NUI_HOST_HAS(host, override_button) || !NUI_HOST_HAS(host, replace_launch) || !NUI_HOST_HAS(host, rename_quick_tile)) {
		host->log(ID, "the launcher is missing a host call");
		return 1;
	}
	say("swap %i %i",
		host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_A, NUI_TRIGGER_PRESS, "gamelist.resume"),
		host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_X, NUI_TRIGGER_PRESS, "gamelist.open"));
	say("again %i", host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_A, NUI_TRIGGER_PRESS, "gamelist.resume"));
	say("nothing %i", host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_Y, NUI_TRIGGER_PRESS, NULL));
	say("unknown %i, bad screen %i, bad trigger %i",
		host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_L2, NUI_TRIGGER_PRESS, "remapper.missing"),
		host->override_button(NUI_SCREEN_SWITCHER, NUI_BUTTON_L2, NUI_TRIGGER_PRESS, "gamelist.open"),
		host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_L2, 7, "gamelist.open"));
	say("replace from init %i", host->replace_launch(host->cfg_get(ID, "emu", ""), NULL, 0));
	say("rename %i, bottom row %i", host->rename_quick_tile("Games", "Library"), host->rename_quick_tile("Wifi", "Net"));
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.tick = tick,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
