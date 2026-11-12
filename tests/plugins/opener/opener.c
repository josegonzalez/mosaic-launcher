// A plugin that launches a game, to check a plugin's pre_launch. Its cfg
// names the game (rom, a full path). Once the lists are up it opens it
// with open_path and logs what that returned; a launch that goes ahead
// quits the launcher, and one that's refused leaves it running, so it
// logs that it's done a little later.

#include <stdarg.h>
#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "opener"

static const MosaicHost* host;
static int ticks = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[600];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open rom %i", host->open_path(host->cfg_get("rom", ""), MOSAIC_ENTRY_ROM));
	else if (ticks == 20) say("done");
}

static int init(const MosaicHost* h) {
	host = h;
	return MOSAIC_HOST_HAS(host, open_path) ? 0 : 1;
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
