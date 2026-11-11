// Simple mode: a launcher for kids or a shared device. While it's on, the
// Tools folder is gone from every list (the root, Recently Played and
// collections), Tools paks don't launch from the game switcher or an FN
// button, and the quick menu loses its Tools and Pak Store tiles, plus
// Settings, Wifi, Bluetooth and Reboot with the "settings" setting (on by
// default).
//
// A Simple mode tile on the quick menu turns it on, and off again: with
// the launcher's PIN set (on the Launcher pak's Plugins page), only after
// typing it, and otherwise after a confirm. The host keeps the PIN and
// checks it; this plugin never sees it. On a launcher without ask_pin, the
// old "pin" key in this plugin's cfg is used instead. Turning the plugin
// or its "on" setting off in the Launcher pak, or editing simple-mode.cfg
// on the card, leaves it too.

#include <stdio.h>
#include <string.h>
#include "simple_model.h"
#include "mosaic_plugin.h"

#define ID "simple-mode"
#define TOGGLE_ID "simple-mode.toggle"

static const MosaicHost* host;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static int isOn(void* data) {
	(void)data;
	return Simple_isOn(host->cfg_get(ID, "on", "on"));
}

static int always(void* data) {
	(void)data;
	return 1;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

// Hides the tiles for the "settings" setting, or shows every tile simple
// mode might have hidden.
static void hideTiles(int hide) {
	if (!MOSAIC_HOST_HAS(host, hide_quick_tile)) return;
	const char* names[SIMPLE_TILES_MAX];
	int hide_settings = strcmp(host->cfg_get(ID, "settings", "on"), "off") != 0;
	int count = Simple_tiles(hide ? hide_settings : 1, names);
	for (int i = 0; i < count; i++) host->hide_quick_tile(names[i], hide);
}

static void setOn(int on) {
	host->cfg_set(ID, "on", on ? "on" : "off");
	hideTiles(on);
	toast(on ? tr("Simple mode on") : tr("Simple mode off"));
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

static void leave(void* data) {
	(void)data;
	setOn(0);
}

static void checkedPin(int result, void* data) {
	(void)data;
	if (result == MOSAIC_PIN_RIGHT) setOn(0);
	else if (result == MOSAIC_PIN_WRONG) toast(tr("Wrong PIN"));
}

static void typedPin(const char* text, void* data) {
	(void)data;
	if (!text) return; // cancelled
	if (Simple_pinMatches(host->cfg_get(ID, "pin", ""), text)) setOn(0);
	else toast(tr("Wrong PIN"));
}

static void toggle(void* data) {
	if (!isOn(data)) {
		setOn(1);
		return;
	}
	if (MOSAIC_HOST_HAS(host, ask_pin)) {
		if (host->has_pin()) host->ask_pin(tr("PIN to leave simple mode"), checkedPin, NULL);
		else if (MOSAIC_HOST_HAS(host, confirm)) host->confirm(tr("Leave simple mode?"), tr("LEAVE"), leave, NULL);
		else setOn(0);
	}
	else if (Simple_leaveStep(host->cfg_get(ID, "pin", "")) == SIMPLE_LEAVE_PIN) {
		if (MOSAIC_HOST_HAS(host, keyboard)) host->keyboard(tr("PIN to leave simple mode"), "", typedPin, NULL);
		else host->log(ID, "this launcher has no keyboard to type the PIN on; turn simple mode off from the Launcher pak");
	}
	else if (MOSAIC_HOST_HAS(host, confirm)) host->confirm(tr("Leave simple mode?"), tr("LEAVE"), leave, NULL);
	else setOn(0);
}

static int filter(const MosaicEntry* entry, const char* dir_path) {
	(void)dir_path;
	if (!isOn(NULL)) return 1;
	return !Simple_isTools(host->entry_path(entry), host->sdcard_path, host->platform);
}

static int preLaunch(const MosaicLaunch* launch) {
	if (!isOn(NULL) || !Simple_isTools(launch->path, host->sdcard_path, host->platform)) return MOSAIC_LAUNCH_CONTINUE;
	toast(tr("Tools are off in simple mode"));
	return MOSAIC_LAUNCH_CANCEL;
}

static int init(const MosaicHost* h) {
	host = h;
	if (host->register_action(TOGGLE_ID, "Simple mode", always, toggle, NULL) != 0
			|| !MOSAIC_HOST_HAS(host, quick_tile)
			|| host->quick_tile(TOGGLE_ID, MOSAIC_QUICK_BOTTOM, NULL, NULL, isOn) != 0) {
		host->log(ID, "couldn't add the Simple mode tile; turn simple mode off from the Launcher pak");
	}
	if (isOn(NULL)) hideTiles(1);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.filter = filter,
	.pre_launch = preLaunch,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
