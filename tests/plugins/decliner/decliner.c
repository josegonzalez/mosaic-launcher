// A plugin that registers everything it can from init and then declines to
// start. The loader unloads it, so nothing it registered may stay behind:
// its action's available would be called in unloaded code on the first
// frame.

#include "mosaic_plugin.h"

#define ID "decliner"

static int available(void* data) {
	(void)data;
	return 1;
}

static void run(void* data) {
	(void)data;
}

static int isOn(void* data) {
	(void)data;
	return 1;
}

static int has(const char* name, const char* path) {
	(void)name, (void)path;
	return 1;
}

static void list(const char* name, void (*add)(const char* path, void* ctx), void* ctx) {
	(void)name, (void)add, (void)ctx;
}

static void heard(const char* name, const char* payload, void* data) {
	(void)name, (void)payload, (void)data;
}

static int init(const MosaicHost* host) {
	host->register_action(ID ".act", "DECLINED", available, run, NULL);
	// takes over an action another plugin registered first
	host->register_action("powerer.act", "TAKEN", available, run, NULL);
	host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, ID ".act");
	host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_L2, MOSAIC_TRIGGER_PRESS, ID ".act");
	host->quick_tile(ID ".act", MOSAIC_QUICK_TOP, NULL, NULL, isOn);
	host->quick_tile(ID ".act", MOSAIC_QUICK_BOTTOM, NULL, NULL, isOn);
	host->hide_quick_tile("Tools", 1);
	host->rename_quick_tile("Games", "Declined");
	host->inhibit_sleep(ID, 1);
	// a set's name, which the next plugin to register it then gets
	host->set_register("favorites", has, list);
	// a subscription, which would be called in unloaded code if it stayed
	host->subscribe("launcher.set_changed", heard, NULL);
	host->power_off(0);
	host->log(ID, "declining");
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
