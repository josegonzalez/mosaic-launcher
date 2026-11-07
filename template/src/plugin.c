// An example nextui-launcher plugin: adds an action on Y in the game list
// that shows the selected entry's name. Replace it with your own.
//
// Keep `id` here in step with id= in plugin.txt.

#include <stdio.h>
#include "nextui_plugin.h"

#define ID "example"

static const NUIHost* host;

static int canShow(void* data) {
	(void)data;
	return host->selected_entry() != NULL;
}

static void show(void* data) {
	(void)data;
	const NUIEntry* entry = host->selected_entry();
	if (!entry) return;
	char message[160];
	snprintf(message, sizeof(message), "Selected: %s", host->entry_name(entry));
	if (NUI_HOST_HAS(host, toast)) host->toast(message);
	else host->log(ID, message);
}

static int init(const NUIHost* h) {
	host = h;
	if (host->register_action(ID ".show", "SHOW", canShow, show, NULL) != 0) return 1;
	// Y is free on the game list in stock NextUI; another plugin may want it too
	if (host->bind_button(NUI_SCREEN_GAMELIST, NUI_BUTTON_Y, NUI_TRIGGER_PRESS, ID ".show") != 0) {
		host->log(ID, "couldn't bind Y");
	}
	return 0;
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
