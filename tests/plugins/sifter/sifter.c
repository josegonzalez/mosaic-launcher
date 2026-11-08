// A second input filter, loaded after filterer, that takes nothing and logs
// what reaches it: each frame with a button held, the presses and held
// buttons it sees, so a case can tell what filterer took before it.

#include <stdio.h>
#include "mosaic_plugin.h"

static const MosaicHost* host;
static unsigned int last_pressed = 0, last_held = 0;

static int init(const MosaicHost* h) {
	host = h;
	return 0;
}

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	(void)consumed;
	if (input->pressed == last_pressed && input->held == last_held) return 0;
	last_pressed = input->pressed;
	last_held = input->held;
	char message[128];
	snprintf(message, sizeof(message), "sees pressed %x held %x", input->pressed, input->held);
	host->log("sifter", message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = "sifter",
	.init = init,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
