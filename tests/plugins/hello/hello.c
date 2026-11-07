// A plugin that only says hello, for checking the loader end to end.

#include <stdio.h>
#include "nextui_plugin.h"

static const NUIHost* host;

static int init(const NUIHost* h) {
	host = h;
	char message[256];
	const char* greeting = host->cfg_get("hello", "greeting", "hello");
	snprintf(message, sizeof(message), "%s from %s", greeting, host->platform);
	host->log("hello", message);
	host->cfg_set("hello", "started", "yes");
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = "hello",
	.init = init,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
