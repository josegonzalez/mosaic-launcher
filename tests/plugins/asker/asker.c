// A plugin that asks which other plugins are loaded and what they provide,
// from init, where only the plugins that loaded before it count, and again
// on its first tick, once every plugin has loaded. It names hello and
// decliner in optional=, so it loads after them. On that tick it also logs
// each plugin's description from plugin_info, and checks that a
// MosaicPluginInfo from before description was added keeps it untouched.

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "asker"

static const MosaicHost* host;
static int ticked = 0;

static void ask(const char* when) {
	char message[256];
	snprintf(message, sizeof(message), "%s hello %i decliner %i asker %i adder %i missing %i cap %i none %i empty %i",
		when, host->plugin_loaded("hello"), host->plugin_loaded("decliner"), host->plugin_loaded(ID),
		host->plugin_loaded("adder"), host->plugin_loaded("missing"), host->has_capability("test.greets"),
		host->has_capability("test.nothing"), host->has_capability(""));
	host->log(ID, message);
}

static void tick(unsigned long now) {
	(void)now;
	if (ticked++) return;
	ask("tick");
	for (int i = 0; i < host->plugins_count(); i++) {
		MosaicPluginInfo info = { .size = sizeof(info) };
		if (host->plugin_info(i, &info) != 0) break;
		char message[400];
		snprintf(message, sizeof(message), "info %s [%s]", info.id, info.description);
		host->log(ID, message);
	}
	MosaicPluginInfo old;
	memset(&old, 'x', sizeof(old));
	old.size = offsetof(MosaicPluginInfo, description);
	int rc = host->plugin_info(0, &old);
	host->log(ID, rc == 0 && old.description[0] == 'x' ? "old info kept" : "old info written");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, has_capability)) {
		host->log(ID, "no capability query");
		return -1;
	}
	ask("init");
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
