// A plugin that tries to show a surface of its own with art_replace while
// swapper's shows, for the first one's keeping the place. It watches
// swapper's hold setting, which the case's plugin.txt marks public: at 1
// it tries once and logs being refused, at 2 it shows its surface, and at
// 3 it clears it. At init it logs what it can read of swapper's other
// settings, that its cfg_set writes its own, and that cfg_get and cfg_set
// from a thread of its own are refused.

#include <pthread.h>
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
	sscanf(host->cfg_read("swapper", "hold", "0"), "%i", &hold);
	if (hold == seen || hold < 1 || hold > 3) return;
	seen = hold;
	char message[64];
	if (hold == 1) snprintf(message, sizeof(message), "refused %i", host->art_replace(ID, mine));
	else if (hold == 2) snprintf(message, sizeof(message), "shown %i", host->art_replace(ID, mine));
	else snprintf(message, sizeof(message), "cleared %i", host->art_replace(ID, NULL));
	host->log(ID, message);
}

static char from_thread[64];

static void* threadCfg(void* data) {
	(void)data;
	snprintf(from_thread, sizeof(from_thread), "%s", host->cfg_get("hold", "refused"));
	host->cfg_set("thread", "yes");
	return NULL;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, art_replace) || !MOSAIC_HOST_HAS(host, cfg_read)) return 1;
	char message[256];
	snprintf(message, sizeof(message), "reads hold %s, note %s, code %s, extra %s, nobody's %s",
		host->cfg_read("swapper", "hold", "unset"), host->cfg_read("swapper", "note", "private"),
		host->cfg_read("swapper", "code", "private"), host->cfg_read("swapper", "extra", "private"),
		host->cfg_read("nobody", "hold", "none"));
	host->log(ID, message);
	// its own hold, never swapper's
	host->cfg_set("hold", "9");
	pthread_t thread;
	snprintf(from_thread, sizeof(from_thread), "no thread");
	if (pthread_create(&thread, NULL, threadCfg, NULL) == 0) pthread_join(thread, NULL);
	snprintf(message, sizeof(message), "own hold %s, swapper's %s, from a thread %s", host->cfg_get("hold", "unset"),
		host->cfg_read("swapper", "hold", "unset"), from_thread);
	host->log(ID, message);
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
