// A plugin that provides dropper.gone and then declines to start, which
// releases it, so no one can call it.

#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "dropper"

static const MosaicHost* host;

static int gone(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "here=1");
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	char message[64];
	snprintf(message, sizeof(message), "provide %i, declining", MOSAIC_HOST_HAS(host, call) ? host->provide("dropper.gone", gone) : -9);
	host->log(ID, message);
	return 1;
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
