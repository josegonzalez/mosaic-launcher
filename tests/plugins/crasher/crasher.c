// A plugin that crashes the launcher while it starts, for checking that the
// next start pauses plugins instead of crashing again.

#include <stdlib.h>
#include "nextui_plugin.h"

static int init(const NUIHost* host) {
	host->log("crasher", "about to crash");
	abort();
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = "crasher",
	.init = init,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* host) {
	(void)host;
	return &plugin;
}
