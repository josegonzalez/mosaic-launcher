// A header widget that reads back the header to its right, where the
// widgets before it in load order and the status pill are, to check a
// bundled header widget, such as header indicators, draws where it says.
// Its own widget is a 20 pixel wide box. Each time what it sees changes,
// up to 20 times, it logs the stretches of the middle line right of it
// that aren't the background (the colour just past its right edge), as
// "runs a-b c-d ..." with b past the last pixel, and how many pixels of
// the first stretch, top to bottom, are in the theme's accent and hint
// colours.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "spotter"
#define WIDTH 20
#define MAX_RUNS 4

static const MosaicHost* host;
static char last[256];
static int logged = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[400];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int same(const MosaicColor* a, const MosaicColor* b) {
	return a->r == b->r && a->g == b->g && a->b == b->b;
}

static int isRgb(const MosaicColor* c, int rgb) {
	return rgb >= 0 && c->r == ((rgb >> 16) & 0xFF) && c->g == ((rgb >> 8) & 0xFF) && c->b == (rgb & 0xFF);
}

static int headerWidth(int max_width, int height) {
	(void)max_width;
	(void)height;
	return WIDTH;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	host->fill_rect(screen, rect, mosaic_color(0x00FF00, 255));
	if (logged >= 20) return 0;

	int w = 0, h = 0;
	if (host->surface_size(screen, &w, &h) != 0) return 0;
	int y = rect->y + rect->h / 2;
	int x = rect->x + rect->w;
	MosaicColor bg, c;
	if (host->read_pixel(screen, x, y, &bg) != 0) return 0;

	int starts[MAX_RUNS], ends[MAX_RUNS], runs = 0, in = 0;
	for (; x < w && runs < MAX_RUNS; x++) {
		if (host->read_pixel(screen, x, y, &c) != 0) break;
		int other = !same(&c, &bg);
		if (other && !in) starts[runs] = x;
		if (!other && in) ends[runs++] = x;
		in = other;
	}
	if (in && runs < MAX_RUNS) ends[runs++] = x;

	char text[sizeof(last)];
	int n = snprintf(text, sizeof(text), "runs");
	for (int i = 0; i < runs; i++) n += snprintf(text + n, sizeof(text) - (size_t)n, " %i-%i", starts[i], ends[i]);
	if (runs) {
		int accent = host->theme_color(MOSAIC_COLOR_ACCENT), hint = host->theme_color(MOSAIC_COLOR_HINT);
		int accents = 0, hints = 0;
		for (int py = rect->y; py < rect->y + rect->h; py++) {
			for (int px = starts[0]; px < ends[0]; px++) {
				if (host->read_pixel(screen, px, py, &c) != 0) continue;
				accents += isRgb(&c, accent);
				hints += isRgb(&c, hint);
			}
		}
		snprintf(text + n, sizeof(text) - (size_t)n, ", accent %i hint %i", accents > 0, hints > 0);
	}
	if (!strcmp(text, last)) return 0;
	snprintf(last, sizeof(last), "%s", text);
	logged++;
	say("%s", text);
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	return MOSAIC_HOST_HAS(host, decorate_cell) ? 0 : 1;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.header_width = headerWidth,
	.header_widget = headerWidget,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
