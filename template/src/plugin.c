// An example mosaic-launcher plugin: adds an action on Y in the game list
// that shows the selected entry's name, and starts an example helper
// program (helper.c and helper/) that answers a ping. Replace it with your
// own.
//
// Its text is translated from res/lang/<code>.po, made from po/example.pot
// (see README.md).
//
// Keep `id` here in step with id= in plugin.txt.

#include <stdio.h>
#include "mosaic_plugin.h"
#include "helper.h" // helper

#define ID "example"
#define HELPER ID "-helper" // helper, as the makefile names it

static const MosaicHost* host;

// Fixed text, translated by the strings slot's owner from this plugin's
// own language packs.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static int canShow(void* data) {
	(void)data;
	return host->selected_entry() != NULL;
}

static void show(void* data) {
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	if (!entry) return;
	char message[160];
	snprintf(message, sizeof(message), tr("Selected: %s"), host->entry_name(entry));
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
	else host->log(ID, message);
}

// Starts the helper and checks that it answers. The plugin works without
// it, as it does when the pak was built without helper/.
static void startHelper(void) {
	char reply[32];
	if (helper_start(host, ID, HELPER) != 0) {
		host->log(ID, "no helper to start");
		return;
	}
	char message[96];
	if (helper_ask("ping\n", reply, sizeof(reply), 1000) == 0) {
		snprintf(message, sizeof(message), "helper %i answered %s", helper_pid(), reply);
	} else {
		snprintf(message, sizeof(message), "helper %i didn't answer", helper_pid());
	}
	host->log(ID, message);
}

static int init(const MosaicHost* h) {
	host = h;
	if (host->register_action(ID ".show", "SHOW", canShow, show, NULL) != 0) return 1;
	// Y is free on the game list in stock NextUI; another plugin may want it too
	if (host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, ID ".show") != 0) {
		host->log(ID, "couldn't bind Y");
	}
	startHelper(); // helper
	return 0;
}

static void quit(void) {
	helper_stop(); // helper
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit, // helper
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
