// A plugin that asks about Bluetooth from init and tick, and has its
// connects refused, since desktop has no Bluetooth.

#include <stdarg.h>
#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "bluer"

static const MosaicHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void report(const char* when) {
	char address[18] = "x";
	int found = host->bt_connected_address(address, sizeof(address));
	say("%s: supported %i, enabled %i, connected %i \"%s\", connect %i, bad %i", when,
		host->bt_supported(), host->bt_enabled(), found, address,
		host->bt_connect("AA:BB:CC:DD:EE:FF"), host->bt_connect("AA:BB:CC:DD:EE:FF; reboot"));
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, bt_connect)) return 1;
	report("init");
	return 0;
}

static void tick(unsigned long now) {
	static int done = 0;
	(void)now;
	if (done) return;
	done = 1;
	report("tick");
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
