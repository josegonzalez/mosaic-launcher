// The metadata band: a game's year, publisher and genre (or the fields
// the fields setting names) from its .info sidecar, in a pill under its
// art. The built-in list asks for it under the selected game's thumbnail;
// a layout gets it where it calls host->decorate_art.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "band_model.h"

#define ID "metadata-band"
#define SEP " \xc2\xb7 " // a middle dot, spaced

static const MosaicHost* host;
static int fields[MB_FIELD_COUNT];
static int field_count = 0;

// the last entry asked about and its tidied values, so entry_info is read
// once each time the selection lands on a game
static struct {
	const MosaicEntry* entry;
	char path[1024];
	char values[MB_FIELD_COUNT][MB_VALUE_SIZE];
	int present;
	// the line last joined for it, at a font and width
	int font, max_width;
	char line[MB_FIELD_COUNT * (MB_VALUE_SIZE + 8)];
} cache;

static void readValues(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	if (!path) path = "";
	if (cache.entry == entry && !strcmp(cache.path, path)) return;
	cache.entry = entry;
	snprintf(cache.path, sizeof(cache.path), "%s", path);
	cache.present = 0;
	cache.font = -1;
	for (int i = 0; i < field_count; i++) {
		char raw[MB_VALUE_SIZE];
		if (host->entry_info(entry, MB_key(fields[i]), raw, sizeof(raw)) != 1) raw[0] = '\0';
		cache.present += MB_clean(fields[i], raw, cache.values[i], sizeof(cache.values[i]));
	}
}

static int measure(const char* text, void* data) {
	return host->text_width(*(const int*)data, text);
}

// The largest font whose band fits height, or -1.
static int fontFor(int height, int pad) {
	static const int fonts[] = { MOSAIC_FONT_SMALL, MOSAIC_FONT_TINY, MOSAIC_FONT_MICRO };
	for (int i = 0; i < 3; i++) {
		int h = host->text_height(fonts[i]);
		if (h > 0 && h + 2 * pad <= height) return fonts[i];
	}
	return -1;
}

static int decorateArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* art, const MosaicRect* below) {
	if (!entry || host->entry_type(entry) != MOSAIC_ENTRY_ROM || !field_count) return 0;
	readValues(entry);
	if (!cache.present) return 0;

	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int gap = 2 * scale;   // between the art and the band
	int pad_y = 2 * scale; // around the text
	int pad_x = 4 * scale;
	int font = fontFor(below->h - gap, pad_y);
	int max_width = below->w - 2 * pad_x;
	if (font < 0 || max_width <= 0) return 0;

	if (cache.font != font || cache.max_width != max_width) {
		const char* values[MB_FIELD_COUNT];
		for (int i = 0; i < field_count; i++) values[i] = cache.values[i];
		if (MB_join(values, field_count, SEP, max_width, measure, &font, cache.line, sizeof(cache.line)) <= 0)
			cache.line[0] = '\0';
		cache.font = font;
		cache.max_width = max_width;
	}
	if (!cache.line[0]) return 0;

	int text_w = host->text_width(font, cache.line);
	if (text_w < 0) return -1;
	if (text_w > max_width) text_w = max_width;
	int text_h = host->text_height(font);
	MosaicRect band = { 0, below->y + gap, text_w + 2 * pad_x, text_h + 2 * pad_y };
	// centred on the art, kept inside below
	band.x = art->x + art->w / 2 - band.w / 2;
	if (band.x + band.w > below->x + below->w) band.x = below->x + below->w - band.w;
	if (band.x < below->x) band.x = below->x;

	int pill = host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	if (host->fill_round_rect(screen, &band, band.h / 2, mosaic_color((unsigned int)pill, 255)) != 0) return -1;
	if (host->draw_text(screen, font, cache.line, band.x + pad_x, band.y + pad_y, mosaic_color((unsigned int)ink, 255),
		max_width) < 0) return -1;
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_art) || !MOSAIC_HOST_HAS(host, entry_info) || !MOSAIC_HOST_HAS(host, draw_text)
		|| !MOSAIC_HOST_HAS(host, theme_color)) {
		host->log(ID, "this launcher can't draw under the art, so there's no band");
		return 1;
	}
	field_count = MB_fields(host->cfg_get(ID, "fields", "year,publisher,genre"), fields);
	memset(&cache, 0, sizeof(cache));
	cache.font = -1;
	char keys[128] = "";
	for (int i = 0; i < field_count; i++) {
		size_t n = strlen(keys);
		snprintf(keys + n, sizeof(keys) - n, "%s%s", i ? "," : "", MB_key(fields[i]));
	}
	char message[160];
	snprintf(message, sizeof(message), "fields %s", field_count ? keys : "none");
	host->log(ID, message);
	return 0;
}

static void quit(void) {
	memset(&cache, 0, sizeof(cache));
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.decorate_art = decorateArt,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
