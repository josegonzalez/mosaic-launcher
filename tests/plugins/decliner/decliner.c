// A plugin that registers everything it can from init and then declines to
// start. The loader unloads it, so nothing it registered may stay behind:
// its action's available would be called in unloaded code on the first
// frame.

#include "nextui_plugin.h"

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

static int init(const NUIHost* host) {
	host->register_action(ID ".act", "DECLINED", available, run, NULL);
	// takes over an action another plugin registered first
	host->register_action("powerer.act", "TAKEN", available, run, NULL);
	host->bind_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_Y, NUI_TRIGGER_PRESS, ID ".act");
	host->override_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_L2, NUI_TRIGGER_PRESS, ID ".act");
	host->quick_tile(ID ".act", NUI_QUICK_TOP, NULL, NULL, isOn);
	host->quick_tile(ID ".act", NUI_QUICK_BOTTOM, NULL, NULL, isOn);
	host->hide_quick_tile("Tools", 1);
	host->rename_quick_tile("Games", "Declined");
	host->inhibit_sleep(ID, 1);
	host->power_off(0);
	host->log(ID, "declining");
	return 1;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
