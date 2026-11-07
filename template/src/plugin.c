// An example mosaic-launcher plugin: adds an action on Y in the game list
// that shows the selected entry's name. Replace it with your own.
//
// Keep `id` here in step with id= in plugin.txt.

#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "example"

static const MosaicHost* host;

static int canShow(void* data) {
	(void)data;
	return host->selected_entry() != NULL;
}

static void show(void* data) {
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	if (!entry) return;
	char message[160];
	snprintf(message, sizeof(message), "Selected: %s", host->entry_name(entry));
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
	else host->log(ID, message);
}

static int init(const MosaicHost* h) {
	host = h;
	if (host->register_action(ID ".show", "SHOW", canShow, show, NULL) != 0) return 1;
	// Y is free on the game list in stock NextUI; another plugin may want it too
	if (host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, ID ".show") != 0) {
		host->log(ID, "couldn't bind Y");
	}
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
