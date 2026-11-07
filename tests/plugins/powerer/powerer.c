// A plugin that reads the battery, LED and show-recents fields, checks
// that decliner (loaded after it) left nothing behind, and powers off or
// reboots from its first tick when its power setting asks.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "nextui_plugin.h"

#define ID "powerer"

static const NUIHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void run(void* data) {
	(void)data;
}

static void tick(unsigned long now) {
	(void)now;
	static int ticked = 0;
	if (ticked++) return;
	say("bind declined %i, tile declined %i, tile own %i",
		host->bind_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_Y, NUI_TRIGGER_PRESS, "decliner.act"),
		host->quick_tile("decliner.act", NUI_QUICK_TOP, NULL, NULL, NULL),
		host->quick_tile(ID ".act", NUI_QUICK_BOTTOM, NULL, NULL, NULL));
	say("show_recents %i", host->show_recents());
	const char* power = host->cfg_get(ID, "power", "");
	if (!strcmp(power, "off")) host->power_off(0);
	else if (!strcmp(power, "reboot")) host->power_off(1);
}

static void beforePoweroff(void) {
	say("before_poweroff");
}

static int init(const NUIHost* h) {
	host = h;
	say("battery %i, charging %i, leds %i, show_recents %i",
		host->battery_level(), host->is_charging(), host->led_count(), host->show_recents());
	host->register_action(ID ".act", "Power", NULL, run, NULL);
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.before_poweroff = beforePoweroff,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
