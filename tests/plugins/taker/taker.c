// A plugin that tries to show a surface of its own with art_replace while
// swapper's shows, for the first one's keeping the place. It watches
// swapper's hold setting: at 1 it tries once and logs being refused, at 2
// it shows its surface, and at 3 it clears it.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "taker"

static const MosaicHost* host;
static MosaicSurface* mine = NULL;
static int seen = 0; // the last hold it acted on

static void tick(unsigned long now) {
	(void)now;
	int hold = 0;
	sscanf(host->cfg_get("swapper", "hold", "0"), "%i", &hold);
	if (hold == seen || hold < 1 || hold > 3) return;
	seen = hold;
	char message[64];
	if (hold == 1) snprintf(message, sizeof(message), "refused %i", host->art_replace(ID, mine));
	else if (hold == 2) snprintf(message, sizeof(message), "shown %i", host->art_replace(ID, mine));
	else snprintf(message, sizeof(message), "cleared %i", host->art_replace(ID, NULL));
	host->log(ID, message);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, art_replace)) return 1;
	mine = host->surface_new(ID, 80, 80);
	host->fill_rect(mine, NULL, mosaic_color(0xFFFF00, 255));
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
