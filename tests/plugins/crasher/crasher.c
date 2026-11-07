// A plugin that crashes the launcher while it starts, for checking that the
// next start pauses plugins instead of crashing again.

#include <stdlib.h>
#include "mosaic_plugin.h"

static int init(const MosaicHost* host) {
	host->log("crasher", "about to crash");
	abort();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = "crasher",
	.init = init,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* host) {
	(void)host;
	return &plugin;
}
