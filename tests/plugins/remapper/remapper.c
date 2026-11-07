// A plugin that uses the button override, launch replacement and quick
// tile rename host calls, logging what each returned.
//
// In init it swaps A and X, takes Y away from everyone, puts the letter
// jump back on L1 with repeat, and relabels the Games tile. Once the
// launcher is up it opens the game named by the "rom" setting, and its pre_launch replaces the launch with the "emu" setting's
// program, so the launcher writes that command and quits. On the start
// after, post_return logs that it ran.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "remapper"

static const MosaicHost* host;
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

static int preLaunch(const MosaicLaunch* launch) {
	say("pre_launch %s", launch->path);
	const char* args[] = { "--core", "x's.so" };
	say("replace missing %i", host->replace_launch("/nowhere/emu.sh", args, 2));
	say("replace %i", host->replace_launch(host->cfg_get(ID, "emu", ""), args, 2));
	host->cfg_set(ID, "launched", "1");
	return MOSAIC_LAUNCH_REPLACE;
}

static void postReturn(void) {
	say("post_return");
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	if (ticks != 1) return;
	say("override from tick %i", host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L2, MOSAIC_TRIGGER_PRESS, "gamelist.open"));
	if (strcmp(host->cfg_get(ID, "launched", "0"), "1") == 0) return;
	say("open rom %i", host->open_path(host->cfg_get(ID, "rom", ""), MOSAIC_ENTRY_ROM));
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, override_button) || !MOSAIC_HOST_HAS(host, replace_launch) || !MOSAIC_HOST_HAS(host, rename_quick_tile)) {
		host->log(ID, "the launcher is missing a host call");
		return 1;
	}
	say("swap %i %i",
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.resume"),
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_X, MOSAIC_TRIGGER_PRESS, "gamelist.open"));
	say("again %i", host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.resume"));
	say("nothing %i", host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, NULL));
	say("unknown %i, bad screen %i, bad trigger %i",
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L2, MOSAIC_TRIGGER_PRESS, "remapper.missing"),
		host->override_button(MOSAIC_SCREEN_SWITCHER, MOSAIC_BUTTON_L2, MOSAIC_TRIGGER_PRESS, "gamelist.open"),
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L2, 7, "gamelist.open"));
	say("repeat %i %i, bind repeat %i", MOSAIC_HOST_HAS(host, override_repeat),
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L1, MOSAIC_TRIGGER_REPEAT, "gamelist.prev_letter"),
		host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L2, MOSAIC_TRIGGER_REPEAT, "gamelist.open"));
	say("replace from init %i", host->replace_launch(host->cfg_get(ID, "emu", ""), NULL, 0));
	say("rename %i, bottom row %i", host->rename_quick_tile("Games", "Library"), host->rename_quick_tile("Wifi", "Net"));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
