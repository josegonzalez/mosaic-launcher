// A plugin that adds one folder to the root, for checking whether a
// replaced root takes other plugins' entries.

#include <stdio.h>
#include "nextui_plugin.h"

static const NUIHost* host;

static void rootEntries(NUIList* root) {
	char path[512];
	snprintf(path, sizeof(path), "%s/Added", host->sdcard_path);
	host->list_push(root, host->entry_new(path, NUI_ENTRY_DIR, "Added"));
}

static int init(const NUIHost* h) {
	host = h;
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = "adder",
	.init = init,
	.root_entries = rootEntries,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
