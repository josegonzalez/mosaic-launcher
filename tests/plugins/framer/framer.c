// A second art provider beside artist: it answers MOSAIC_ART_BOX for every
// game in <sd>/Artist with <dir>/framer-<game>.png, so whichever of the two
// loads first wins the chain.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "framer"

static const MosaicHost* host;
static char folder[512];
static const char* dir = "";

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	const char* path = host->entry_path(entry);
	size_t len = strlen(folder);
	if (kind != MOSAIC_ART_BOX || !path || strncmp(path, folder, len) != 0 || path[len] != '/') return 0;
	snprintf(out, size, "%s/framer-%s.png", dir, host->entry_name(entry));
	return 1;
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder, sizeof(folder), "%s/Artist", host->sdcard_path);
	dir = host->cfg_get("dir", "");
	host->log(ID, "framing");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.art_path = artPath,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
