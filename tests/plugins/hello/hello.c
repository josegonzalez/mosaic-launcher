// A plugin that only says hello, for checking the loader end to end.

#include <stdio.h>
#include "mosaic_plugin.h"

static const MosaicHost* host;

static int init(const MosaicHost* h) {
	host = h;
	char message[256];
	const char* greeting = host->cfg_get("greeting", "hello");
	snprintf(message, sizeof(message), "%s from %s", greeting, host->platform);
	host->log("hello", message);
	host->cfg_set("started", "yes");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = "hello",
	.init = init,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
