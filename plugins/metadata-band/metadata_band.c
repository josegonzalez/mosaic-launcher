// The metadata band: a game's year, publisher and genre (or the fields
// the fields setting names) from its .info sidecar, in a pill under its
// art. The built-in list asks for it under the selected game's thumbnail;
// a layout gets it where it calls host->decorate_art.
//
// Other plugins format a game's .info fields the same way with the
// info.format service: path=<game>, optionally fields=<keys> in place of
// the fields setting's, and width=<pixels> in font=<small, tiny or micro>
// to fit the line, give count=<fields with a value>, <key>=<tidied value>
// for each of those, and line=<them joined with " · ">, the last left out
// until it fits the width. The sidecar is read from the card as the
// launcher reads it for entry_info.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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

// A sidecar's text, read as the launcher reads them: a regular file of up
// to MB_INFO_FILE_MAX bytes, opened without blocking. Returns its length,
// or -1 for none.
static int readSidecar(const char* path, char* text, size_t size) {
	char file[1100];
	MB_infoPath(path, file, sizeof(file));
	int fd = open(file, O_RDONLY | O_NONBLOCK);
	if (fd < 0) return -1;
	struct stat st;
	size_t got = 0;
	if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
		while (got < size) {
			ssize_t n = read(fd, text + got, size - got);
			if (n <= 0) break;
			got += (size_t)n;
		}
	}
	close(fd);
	return got > MB_INFO_FILE_MAX ? -1 : (int)got;
}

// info.format: path=<game>, and optionally fields=<keys, as the fields
// setting lists them> in place of the setting's, and width=<pixels> in
// font=<small, tiny or micro> to fit the line.
static int provideInfo(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char path[1024], list[128], number[16], font_name[16];
	int len = host->event_value(args, "path", path, sizeof(path));
	if (len <= 0 || len >= (int)sizeof(path)) return -1;
	int asked[MB_FIELD_COUNT];
	int count = field_count;
	memcpy(asked, fields, sizeof(asked));
	if (host->event_value(args, "fields", list, sizeof(list)) >= 0) count = MB_fields(list, asked);
	int max_width = -1, font = MOSAIC_FONT_SMALL;
	if (host->event_value(args, "width", number, sizeof(number)) >= 0) {
		char* end;
		long w = strtol(number, &end, 10);
		if (end == number || *end || w < 0 || w > 100000) return -1;
		max_width = (int)w;
	}
	if (host->event_value(args, "font", font_name, sizeof(font_name)) >= 0) {
		if (!strcmp(font_name, "tiny")) font = MOSAIC_FONT_TINY;
		else if (!strcmp(font_name, "micro")) font = MOSAIC_FONT_MICRO;
		else if (strcmp(font_name, "small") != 0) return -1;
	}
	static char text[MB_INFO_FILE_MAX + 1];
	int got = readSidecar(path, text, sizeof(text));
	char raw[MB_FIELD_COUNT][MB_VALUE_SIZE];
	const char* values[MB_FIELD_COUNT];
	for (int i = 0; i < count; i++) {
		if (got < 0 || !MB_infoValue(text, (size_t)got, MB_key(asked[i]), raw[i], sizeof(raw[i]))) raw[i][0] = '\0';
		values[i] = raw[i];
	}
	return MB_reply(asked, count, values, SEP, max_width, measure, &font, out, size);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_art) || !MOSAIC_HOST_HAS(host, entry_info) || !MOSAIC_HOST_HAS(host, draw_text)
		|| !MOSAIC_HOST_HAS(host, theme_color)) {
		host->log(ID, "this launcher can't draw under the art, so there's no band");
		return 1;
	}
	field_count = MB_fields(host->cfg_get("fields", "year,publisher,genre"), fields);
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
	if (MOSAIC_HOST_HAS(host, call) && host->provide("info.format", provideInfo) != 0) host->log(ID, "couldn't provide info.format");
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
