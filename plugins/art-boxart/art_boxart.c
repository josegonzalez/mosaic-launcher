// Box art provider: answers art_path from the art folder conventions of
// several forks, chained with other providers, and hides the visible art
// folders from the list. boxart_model.h has the conventions and their
// order. Each setting turns one convention or mode on or off; the thumbnail
// setting (off by default) also answers the thumbnail kind, so stock's
// .media thumbnails stay the default and this art fills in as the box the
// built-in list falls back to.

#include <sys/stat.h>
#include "boxart_model.h"
#include "mosaic_plugin.h"

#define ID "art-boxart"

static const MosaicHost* host;
static BAConfig config;

static int setting(const char* key, int fallback) {
	return BA_toggle(host->cfg_get(ID, key, fallback ? "on" : "off"), fallback);
}

static int init(const MosaicHost* h) {
	host = h;
	BA_defaults(&config);
	config.thumbnail = setting("thumbnail", config.thumbnail);
	config.media = setting("media", config.media);
	config.imgs = setting("imgs", config.imgs);
	config.res = setting("res", config.res);
	config.images = setting("images", config.images);
	config.hide = setting("hide", config.hide);
	config.cover = setting("cover", config.cover);
	config.rom_art = setting("rom_art", config.rom_art);
	return 0;
}

static int isFile(const char* path, void* data) {
	(void)data;
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	const char* path = host->entry_path(entry);
	return path && BA_find(&config, path, host->entry_type(entry), kind, isFile, NULL, out, size);
}

static int filter(const MosaicEntry* entry, const char* dir_path) {
	(void)dir_path;
	return !BA_hides(&config, host->entry_path(entry), host->entry_type(entry));
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.filter = filter,
	.art_path = artPath,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
