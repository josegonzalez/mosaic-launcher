// Bluetooth reconnect: reconnects the last Bluetooth device at start and
// after the device wakes.
//
// Stock doesn't record which device was last connected, so while the
// launcher is on screen it saves the connected device's address in the
// last setting whenever it changes. At start (init, which runs again each
// time the launcher comes back from a game) and after a resume, with
// Bluetooth on and a saved device, it waits for the launcher's first look,
// and if nothing is connected by then asks the host to connect the saved
// device, up to 3 times with a longer wait before each retry. A device
// connecting, or Bluetooth turning off, ends it. The host runs the
// connect; this plugin never runs a program. Without Bluetooth, such as on
// desktop, or on a launcher without the Bluetooth calls, it loads and does
// nothing.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "bt_model.h"

#define ID "bt-reconnect"

static const MosaicHost* host;
static int supported = 0;
static int on_resume = 0;
static char saved[BR_ADDRESS_SIZE]; // the device to reconnect, or ""
static BrReconnect reconnect;

static void say(const char* message) {
	host->log(ID, message);
}

static int isOn(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(key, fallback), "off") != 0;
}

static void begin(void) {
	if (saved[0]) Br_start(&reconnect);
}

// remember the device while it's connected
static void remember(const char* address) {
	if (!strcmp(address, saved)) return;
	snprintf(saved, sizeof(saved), "%s", address);
	host->cfg_set("last", saved);
	char message[64];
	snprintf(message, sizeof(message), "remembering %s", saved);
	say(message);
}

static void askToConnect(unsigned long now) {
	int result = host->bt_connect(saved);
	Br_connectResult(&reconnect, now, result);
	char message[96];
	if (result == 0) snprintf(message, sizeof(message), "reconnecting %s, try %i of %i", saved, reconnect.tries, BR_TRIES);
	else snprintf(message, sizeof(message), "the launcher refused to reconnect %s, try %i of %i", saved, reconnect.tries, BR_TRIES);
	say(message);
}

static void tick(unsigned long now) {
	if (!supported) return;
	char address[BR_ADDRESS_SIZE];
	int connected = host->bt_connected_address(address, sizeof(address));
	if (connected) remember(address);

	int tried = reconnect.tries;
	char message[96];
	switch (Br_step(&reconnect, now, host->bt_enabled(), connected)) {
		case BR_CONNECT:
			askToConnect(now);
			break;
		case BR_CONNECTED:
			if (tried) {
				snprintf(message, sizeof(message), "%s connected", address);
				say(message);
			}
			break;
		case BR_OFF:
			say("Bluetooth is off, so not reconnecting");
			break;
		case BR_GAVE_UP:
			snprintf(message, sizeof(message), "%s didn't connect after %i tries, giving up", saved, BR_TRIES);
			say(message);
			break;
	}
}

static void beforeSleep(void) {
	Br_stop(&reconnect);
}

static void afterResume(void) {
	if (supported && on_resume) begin();
}

static int init(const MosaicHost* h) {
	host = h;
	supported = 0;
	saved[0] = '\0';
	Br_init(&reconnect);
	if (!MOSAIC_HOST_HAS(host, bt_supported) || !MOSAIC_HOST_HAS(host, bt_enabled)
		|| !MOSAIC_HOST_HAS(host, bt_connected_address) || !MOSAIC_HOST_HAS(host, bt_connect)) {
		say("unsupported: this launcher has no Bluetooth calls");
		return 0;
	}
	if (!host->bt_supported()) {
		say("unsupported on this platform: it has no Bluetooth");
		return 0;
	}
	int on_start = isOn("on_start", "on");
	on_resume = isOn("on_resume", "on");
	if (!on_start && !on_resume) {
		say("reconnecting at start and after sleep are both off");
		return 0;
	}
	supported = 1;

	const char* last = host->cfg_get("last", "");
	if (last[0] && !Br_parseAddress(last, saved)) {
		say("the last setting isn't a Bluetooth address, so nothing to reconnect until a device connects");
	}
	if (on_start) begin();
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.before_sleep = beforeSleep,
	.after_resume = afterResume,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
