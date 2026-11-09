// Unplugs charging-screen's test battery: the second time its screen
// opens, it writes "<level> 0" to the file MOSAIC_CHARGING_SCREEN_FAKE
// names, keeping the level, and logs "unplugged".

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "unplugger"

static const MosaicHost* host;
static int was_open = 0, opened = 0, done = 0;

static void tick(unsigned long now) {
	(void)now;
	int is_open = !strcmp(host->current_screen(), "charging-screen.main");
	if (is_open && !was_open) opened++;
	was_open = is_open;
	if (done || opened < 2) return;
	done = 1;
	const char* path = getenv("MOSAIC_CHARGING_SCREEN_FAKE");
	FILE* f = path ? fopen(path, "r+") : NULL;
	int level = 0, charging = 0;
	if (!f || fscanf(f, "%d %d", &level, &charging) != 2) {
		host->log(ID, "couldn't read the test battery");
		if (f) fclose(f);
		return;
	}
	fclose(f);
	f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "%d 0\n", level);
	fclose(f);
	host->log(ID, "unplugged");
}

static int init(const MosaicHost* h) {
	host = h;
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
