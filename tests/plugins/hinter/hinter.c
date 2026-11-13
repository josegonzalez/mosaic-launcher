// Owns the theme slot to pick a hint style, and draws single buttons in
// the style in effect. Its config's style is the theme's hint_style (0,
// unset, by default). init logs the style before the theme is asked; once
// the list shows, tick logs it again, draws buttons on a surface of its
// own and logs each one's size, then the calls it should refuse, and
// "done":
//
//   style <n>                 host->hint_style
//   <name> <rc> <w>x<h> ink <n> measure <w>
//                             a button drawn at the top left of a new
//                             surface: its rect, the pixels it drew on
//                             and its width with no surface
//   dpad differs <0|1>        whether UP and DOWN drew different pixels
//   refused ...               the calls' results that should be -1

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "hinter"
#define SIZE 160

static const MosaicHost* host;
static MosaicSurface* surface;
static int done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int theme(MosaicTheme* t) {
	t->hint_style = atoi(host->cfg_get("style", "0"));
	return 0;
}

// The pixels of rect with any alpha, adding each one's colour to sum.
static int ink(const MosaicRect* rect, unsigned long* sum) {
	int count = 0;
	for (int y = rect->y; y < rect->y + rect->h; y++) {
		for (int x = rect->x; x < rect->x + rect->w; x++) {
			MosaicColor c;
			if (host->read_pixel(surface, x, y, &c) != 0 || !c.a) continue;
			count++;
			if (sum) *sum += (unsigned long)(x * 7 + y * 13) * (c.r + c.g * 3 + c.b * 5 + c.a * 11);
		}
	}
	return count;
}

static unsigned long button(const char* name, int which) {
	surface = host->surface_new(ID, SIZE, SIZE);
	if (!surface) {
		say("%s no surface", name);
		return 0;
	}
	MosaicRect r = { 0, 0, 0, 0 }, m = { 0, 0, 0, 0 };
	int rc = host->draw_button(surface, which, 0, 0, &r);
	host->draw_button(NULL, which, 0, 0, &m);
	unsigned long sum = 0;
	int n = ink(&r, &sum);
	say("%s %d %dx%d ink %d measure %d", name, rc, r.w, r.h, n, m.w);
	host->surface_free(ID, surface);
	surface = NULL;
	return sum;
}

static void tick(unsigned long now) {
	(void)now;
	if (done || !host->current_list()) return;
	done = 1;
	say("style %d", host->hint_style());
	button("A", MOSAIC_BUTTON_A);
	button("START", MOSAIC_BUTTON_START);
	button("POWER", MOSAIC_BUTTON_POWER);
	unsigned long up = button("UP", MOSAIC_BUTTON_UP);
	unsigned long down = button("DOWN", MOSAIC_BUTTON_DOWN);
	button("LEFT", MOSAIC_BUTTON_LEFT);
	button("RIGHT", MOSAIC_BUTTON_RIGHT);
	button("U/D", MOSAIC_GLYPH_UP_DOWN);
	button("L/R", MOSAIC_GLYPH_LEFT_RIGHT);
	button("DPAD", MOSAIC_GLYPH_DPAD);
	say("dpad differs %d", up != down);
	surface = host->surface_new(ID, SIZE, SIZE);
	MosaicRect r;
	say("refused unknown %d fn %d stale %d far %d", host->draw_button(surface, 99, 0, 0, &r),
		host->draw_button(surface, MOSAIC_BUTTON_FN1, 0, 0, &r),
		host->draw_button((MosaicSurface*)&r, MOSAIC_BUTTON_A, 0, 0, &r),
		host->draw_button(surface, MOSAIC_BUTTON_A, 1 << 30, 0, &r));
	host->surface_free(ID, surface);
	surface = NULL;
	say("done");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_button)) return 1;
	say("init style %d", host->hint_style());
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.theme = theme,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
