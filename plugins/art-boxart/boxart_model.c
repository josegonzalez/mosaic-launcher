#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "boxart_model.h"
#include "mosaic_plugin.h"

void BA_defaults(BAConfig* config) {
	config->thumbnail = 0;
	config->media = 1;
	config->imgs = 1;
	config->res = 1;
	config->images = 1;
	config->hide = 1;
	config->cover = 1;
	config->rom_art = 1;
}

int BA_toggle(const char* value, int fallback) {
	if (!value) return fallback;
	if (!strcmp(value, "on")) return 1;
	if (!strcmp(value, "off")) return 0;
	return fallback;
}

static const char* fileName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : NULL;
}

int BA_artName(const char* path, int is_dir, char* out, size_t size) {
	if (size) out[0] = '\0';
	const char* name = path ? fileName(path) : NULL;
	if (!name || !name[0] || !size) return 0;
	size_t len = strlen(name);
	if (!is_dir) {
		const char* dot = strrchr(name, '.');
		if (dot) len = (size_t)(dot - name);
	}
	if (len == 0 || len >= size) return 0;
	memcpy(out, name, len);
	out[len] = '\0';
	return 1;
}

int BA_isCart(const char* path) {
	static const char suffix[] = ".p8.png";
	size_t len = path ? strlen(path) : 0, n = sizeof(suffix) - 1;
	return len > n && !strcasecmp(path + len - n, suffix) && path[len - n - 1] != '/';
}

// Writes <dir>/<folder>/<name>.png, where dir is the folder path is in.
// Returns 0 when it doesn't fit.
static int besidePath(const char* path, const char* folder, const char* name, char* out, size_t size) {
	const char* slash = strrchr(path, '/');
	int dir_len = (int)(slash - path);
	int n = snprintf(out, size, "%.*s/%s/%s.png", dir_len, path, folder, name);
	return n > 0 && (size_t)n < size;
}

// Tries a candidate: 1 when it was built and exists, leaving it in out.
static int try(int built, const char* out, BAExists exists, void* data) {
	return built && exists(out, data);
}

static int findBox(const BAConfig* config, const char* path, int is_dir, const char* name, BAExists exists, void* data,
		char* out, size_t size) {
	if (!is_dir && config->rom_art && BA_isCart(path)) {
		int n = snprintf(out, size, "%s", path);
		if (try(n > 0 && (size_t)n < size, out, exists, data)) return 1;
	}
	if (is_dir && config->cover) {
		int n = snprintf(out, size, "%s/cover.png", path);
		if (try(n > 0 && (size_t)n < size, out, exists, data)) return 1;
	}
	if (config->media && try(besidePath(path, ".media/box", name, out, size), out, exists, data)) return 1;
	if (config->imgs && try(besidePath(path, "Imgs", name, out, size), out, exists, data)) return 1;
	if (config->res && try(besidePath(path, ".res", name, out, size), out, exists, data)) return 1;
	if (config->images && try(besidePath(path, "Images", name, out, size), out, exists, data)) return 1;
	return 0;
}

int BA_find(const BAConfig* config, const char* path, int type, int kind, BAExists exists, void* data,
		char* out, size_t size) {
	if (!size) return 0;
	out[0] = '\0';
	if (type != MOSAIC_ENTRY_ROM && type != MOSAIC_ENTRY_DIR) return 0;
	int is_dir = type == MOSAIC_ENTRY_DIR;
	char name[BA_PATH_LEN];
	if (!BA_artName(path, is_dir, name, sizeof(name))) return 0;

	int found = 0;
	switch (kind) {
		case MOSAIC_ART_THUMBNAIL:
			found = config->thumbnail && findBox(config, path, is_dir, name, exists, data, out, size);
			break;
		case MOSAIC_ART_BOX:
			found = findBox(config, path, is_dir, name, exists, data, out, size);
			break;
		case MOSAIC_ART_BOX_3D:
			found = config->media && try(besidePath(path, ".media/3d", name, out, size), out, exists, data);
			break;
		case MOSAIC_ART_LOGO:
			found = config->media && try(besidePath(path, ".media/wheel", name, out, size), out, exists, data);
			break;
		default:
			break;
	}
	if (!found) out[0] = '\0';
	return found;
}

int BA_hides(const BAConfig* config, const char* path, int type) {
	if (!config->hide || type != MOSAIC_ENTRY_DIR || !path) return 0;
	const char* name = fileName(path);
	if (!name) return 0;
	return (config->imgs && !strcasecmp(name, "Imgs")) || (config->images && !strcasecmp(name, "Images"));
}
