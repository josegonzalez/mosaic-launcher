// Opens what the bundled ui-sounds plays a sound for, for seeker to run
// as actions: chimer.toast shows a toast, chimer.confirm a dialog and
// chimer.pin the PIN keyboard. It logs each of the launcher's events it
// hears, with its payload, so the case can check the launcher publishes
// them in order.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "chimer"

static const MosaicHost* host;

static void onEvent(const char* name, const char* payload, void* data) {
	(void)data;
	char message[256];
	snprintf(message, sizeof(message), "event %s%s%s", name, payload[0] ? " " : "", payload);
	host->log(ID, message);
}

static void toast(const char* path, void* data) {
	(void)path;
	(void)data;
	host->toast("Chimed");
}

static void confirmed(void* data) {
	(void)data;
	host->log(ID, "confirmed");
}

static void confirm(const char* path, void* data) {
	(void)path;
	(void)data;
	host->confirm("Chime?", "Chime", confirmed, NULL);
}

static void pinDone(int result, void* data) {
	(void)data;
	char message[64];
	snprintf(message, sizeof(message), "pin %s", result == MOSAIC_PIN_WRONG ? "wrong" : result == MOSAIC_PIN_RIGHT ? "right" : "cancelled");
	host->log(ID, message);
}

static void pin(const char* path, void* data) {
	(void)path;
	(void)data;
	host->ask_pin("Chime PIN", pinDone, NULL);
}

static int init(const MosaicHost* h) {
	host = h;
	static const char* const events[] = { "launcher.menu_open", "launcher.menu_close", "launcher.toast", "launcher.error" };
	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) host->subscribe(events[i], onEvent, NULL);
	host->register_action(ID ".toast", "Toast", 0, NULL, toast, NULL);
	host->register_action(ID ".confirm", "Confirm", 0, NULL, confirm, NULL);
	host->register_action(ID ".pin", "PIN", 0, NULL, pin, NULL);
	return 0;
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
