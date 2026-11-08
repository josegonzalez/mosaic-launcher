// A second row decorator and header widget, to load beside badger: it logs
// whether badger's dot was already on each of badger's rows when it got
// them, and whether badger's widget is on its right, so the order the
// launcher calls them in shows. Its widget is a green box 40 wide.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "captioner"

static const MosaicHost* host;
static char folder[512];
static unsigned int seen = 0;
static int widgets = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int isColor(const MosaicSurface* s, int x, int y, unsigned int rgb) {
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return 0;
	return c.r == (unsigned char)(rgb >> 16) && c.g == (unsigned char)(rgb >> 8) && c.b == (unsigned char)rgb;
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	const char* path = host->entry_path(entry);
	size_t n = strlen(folder);
	if (!path || strncmp(path, folder, n) != 0 || path[n] != '/' || row->index < 0 || row->index >= 32) return 0;
	unsigned int bit = 1u << row->index;
	if (seen & bit) return 0;
	seen |= bit;
	const MosaicRect* r = &row->rect;
	say("row %s after badger %i", host->entry_name(entry), isColor(screen, r->x + r->w - 11, r->y + r->h / 2 - 2, 0xFF0000));
	return 0;
}

static int headerWidth(int max_width, int height) {
	(void)max_width;
	(void)height;
	return 40;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	host->fill_rect(screen, rect, mosaic_color(0x00FF00, 255));
	if (++widgets == 1) {
		int right = rect->x + rect->w + 20;
		say("widget rect %i,%i %ix%i, badger on the right %i", rect->x, rect->y, rect->w, rect->h,
			isColor(screen, right, rect->y + rect->h / 2, 0x0000FF));
	}
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_cell)) return 1;
	snprintf(folder, sizeof(folder), "%s/Badger", host->sdcard_path);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.decorate_row = decorateRow,
	.header_width = headerWidth,
	.header_widget = headerWidget,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
