// A plugin that adds one folder to the root, for checking whether a
// replaced root takes other plugins' entries.

#include <stdio.h>
#include "mosaic_plugin.h"

static const MosaicHost* host;

static void rootEntries(MosaicList* root) {
	char path[512];
	snprintf(path, sizeof(path), "%s/Added", host->sdcard_path);
	host->list_push(root, host->entry_new(path, MOSAIC_ENTRY_DIR, "Added"));
}

static int init(const MosaicHost* h) {
	host = h;
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = "adder",
	.init = init,
	.root_entries = rootEntries,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
