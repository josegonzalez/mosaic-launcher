// A plugin that reads back the band metadata-band, earlier in load order,
// drew under the game's art. It opens <sd>/<folder> (from folder= in its
// config) and walks its games from tick, one a second. In decorate_art it
// finds the pixels in the band's colour, the theme's accent colour, in
// below, and logs, for each game the first time it's decorated, "none", or
// whether the band starts just under the art, is centred on it, holds
// text in the theme's hint colour, and is as wide as the text its config
// gives for the game's name (eg. Apple=1994 · Nintendo) plus the band's
// padding, in any of the small fonts. After the last game it logs done.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "dater"
#define MAX_GAMES 16

static const MosaicHost* host;
static char folder[512];
static int ticks = 0;
static int next = 0;
static int logged[MAX_GAMES];

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int rgbAt(const MosaicSurface* s, int x, int y) {
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return -1;
	return c.r << 16 | c.g << 8 | c.b;
}

// 1 when width is the band's for text in one of the small fonts
static int fits(const char* text, int width) {
	static const int fonts[] = { MOSAIC_FONT_SMALL, MOSAIC_FONT_TINY, MOSAIC_FONT_MICRO };
	int pad = 2 * 4 * host->screen_scale();
	for (int i = 0; i < 3; i++) {
		int want = host->text_width(fonts[i], text) + pad;
		if (width >= want - 3 && width <= want + 3) return 1;
	}
	return 0;
}

static int decorateArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* art, const MosaicRect* below) {
	const char* path = host->current_path();
	if (!path || strcmp(path, folder) != 0) return 0;
	int index = host->selected_index();
	if (index < 0 || index >= MAX_GAMES || logged[index]) return 0;
	logged[index] = 1;
	const char* name = host->entry_name(entry);
	int accent = host->theme_color(MOSAIC_COLOR_ACCENT);
	int hint = host->theme_color(MOSAIC_COLOR_HINT);
	int left = -1, right = -1, top = -1, bottom = -1, ink = 0;
	for (int y = below->y; y < below->y + below->h; y++) {
		for (int x = below->x; x < below->x + below->w; x++) {
			if (rgbAt(screen, x, y) != accent) continue;
			if (left < 0 || x < left) left = x;
			if (x > right) right = x;
			if (top < 0) top = y;
			bottom = y;
		}
	}
	if (left < 0) {
		say("band %s none", name);
		return 0;
	}
	for (int y = top; y <= bottom; y++)
		for (int x = left; x <= right; x++) ink += rgbAt(screen, x, y) == hint;
	int scale = host->screen_scale();
	int art_bottom = art->y + art->h;
	int centre = (left + right + 1) / 2 - (art->x + art->w / 2);
	const char* text = host->cfg_get(name, "");
	say("band %s under art %i, centred %i, text %i, as wide as \"%s\" %i", name,
		top >= art_bottom && top <= art_bottom + 3 * scale, centre >= -2 && centre <= 2, ink > 0, text,
		text[0] && fits(text, right - left + 1));
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	const MosaicList* list = host->current_list();
	if (!list) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	host->request_redraw();
	if (ticks < 10 || ticks % 30) return;
	const char* path = host->current_path();
	if (!path || strcmp(path, folder) != 0) return;
	int count = host->list_count(list);
	if (next < count && next < MAX_GAMES) host->select_index(next++);
	else if (next == count) {
		next++;
		say("done");
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, decorate_art)) return 1;
	snprintf(folder, sizeof(folder), "%s/%s", host->sdcard_path, host->cfg_get("folder", "Dater"));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.decorate_art = decorateArt,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
