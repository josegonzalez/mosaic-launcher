// A plugin that asks for the audio device while speaker has it, and is
// refused.

#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "hearer"

static const MosaicHost* host;
static int ticks = 0;
static short frames[20];

static int init(const MosaicHost* h) {
	host = h;
	return MOSAIC_HOST_HAS(host, audio_open) ? 0 : 1;
}

static void tick(unsigned long now) {
	(void)now;
	// speaker opens on its first tick and keeps it for a second
	if (++ticks != 5) return;
	char message[128];
	snprintf(message, sizeof(message), "open while taken %i, queue %i, queued %i, close %i", host->audio_open(ID, 44100, 2),
		host->audio_queue(ID, frames, 10), host->audio_queued(ID), host->audio_close(ID));
	host->log(ID, message);
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
