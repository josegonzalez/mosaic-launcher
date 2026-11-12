// A plugin with an overlay: a 16 pixel magenta square at the left edge,
// halfway down, where tests/plugins/screener reads the screen back. The
// first time it draws over each screen it logs which, whether the overlay
// it was lent starts clear there, and its size. With fail=1 in its config
// its third overlay_render fails.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "layerer"

static const MosaicHost* host;
static int fail = 0;
static int calls = 0;
static char seen[16][80];
static int seen_count = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, open_screen)) {
		say("no open_screen");
		return 1;
	}
	fail = atoi(host->cfg_get("fail", "0"));
	return 0;
}

static int firstOver(const char* screen) {
	for (int i = 0; i < seen_count; i++) {
		if (!strcmp(seen[i], screen)) return 0;
	}
	if (seen_count < 16) snprintf(seen[seen_count++], sizeof(seen[0]), "%s", screen);
	return 1;
}

static int overlayRender(MosaicSurface* overlay, const MosaicScreenView* view) {
	calls++;
	if (fail && calls == 3) return -1;
	MosaicRect square = { 0, host->screen_height() / 2, 16, 16 };
	if (firstOver(view->screen)) {
		MosaicColor c = { 1, 1, 1, 1 };
		host->read_pixel(overlay, square.x + 4, square.y + 4, &c);
		int w = 0, h = 0;
		host->surface_size(overlay, &w, &h);
		say("over %s clear %i size %i %i select %i open %i", view->screen, c.a == 0, w == host->screen_width(),
			h == host->screen_height(), host->select_index(0), host->open_screen("screener.main"));
	}
	host->fill_rect(overlay, &square, mosaic_color(0xFF00FF, 255));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.overlay_render = overlayRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
