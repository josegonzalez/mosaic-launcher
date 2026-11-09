// A plugin that tries every drawing call on surfaces of its own, reading
// pixels back to see what was drawn, has its bad arguments refused, and
// leaves some surfaces for the launcher to free. With decline=1 in its
// config it declines afterwards, and with pak=<path> it opens that pak on
// its first tick, which quits the launcher to launch it.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "drawer"

static const MosaicHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static const char* made(const MosaicSurface* s) {
	return s ? "ok" : "NULL";
}

// "RRGGBB AA" at x, y, or "-" when it can't be read
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[8][16]; // enough for one say's worth
	static int next = 0;
	char* text = out[next++ % 8];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 16, "%02X%02X%02X %02X", c.r, c.g, c.b, c.a);
	return text;
}

static int near(int value, int want) {
	return value >= want - 8 && value <= want + 8;
}

static int left = 0; // surfaces made and not freed

static MosaicSurface* keep(MosaicSurface* s) {
	if (s) left++;
	return s;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_text)) return 1;
	say("screen %ix%i scale %i", host->screen_width(), host->screen_height(), host->screen_scale());

	// making surfaces
	MosaicSurface* s = keep(host->surface_new(ID, 40, 20));
	say("new %s, zero %s, too wide %s, not loaded %s, no id %s", made(s),
		made(host->surface_new(ID, 0, 5)), made(host->surface_new(ID, 5000, 5)),
		made(host->surface_new("nobody", 4, 4)), made(host->surface_new(NULL, 4, 4)));
	int w = 0, ht = 0;
	int sized = host->surface_size(s, &w, &ht);
	say("size %i %ix%i, null %i, unknown %i, no out %i", sized, w, ht,
		host->surface_size(NULL, &w, &ht), host->surface_size((MosaicSurface*)&w, &w, &ht),
		host->surface_size(s, NULL, &ht));
	say("new pixel %s, outside %s %s", pixel(s, 0, 0), pixel(s, 40, 0), pixel(s, 0, -1));

	// the budget: 16 MiB fits, another doesn't
	MosaicSurface* big = host->surface_new(ID, 2048, 2048);
	MosaicSurface* bigger = host->surface_new(ID, 2048, 2048);
	say("budget first %s, second %s, freed %i", made(big), made(bigger), host->surface_free(ID, big));

	// rects, opaque and blended
	MosaicColor red = mosaic_color(0xFF0000, 255);
	int filled = host->fill_rect(s, NULL, red);
	say("fill %i, pixel %s", filled, pixel(s, 0, 0));
	MosaicColor c;
	int blended = host->fill_rect(s, &(MosaicRect){ 10, 5, 10, 10 }, mosaic_color(0x0000FF, 128));
	host->read_pixel(s, 15, 10, &c);
	// SDL2 blends the alpha to 254 here, SDL3 (sdl2-compat) to 255
	say("blend %i, half %i, outside %s", blended, near(c.r, 0x7F) && near(c.b, 0x80) && c.a >= 254, pixel(s, 9, 5));
	say("fill bad rect %i, null %i, unknown %i, clear %i",
		host->fill_rect(s, &(MosaicRect){ 0, 0, -1, 5 }, red), host->fill_rect(NULL, NULL, red),
		host->fill_rect((MosaicSurface*)&c, NULL, red), host->fill_rect(s, NULL, mosaic_color(0, 0)));
	host->fill_rect(s, &(MosaicRect){ 30, 10, 10, 10 }, mosaic_color(0x00FF00, 255));
	int edge = host->fill_rect(s, &(MosaicRect){ -10, -10, 1000, 25 }, red);
	say("off the edge %i, huge %i, pixel %s %s", edge, host->fill_rect(s, &(MosaicRect){ 0, 0, 1 << 30, 5 }, red),
		pixel(s, 39, 14), pixel(s, 39, 15));

	// rounded rects
	MosaicSurface* round = keep(host->surface_new(ID, 40, 20));
	int rounded = host->fill_round_rect(round, NULL, 10, mosaic_color(0x00FF00, 255));
	say("round %i, corner %s, edge %s, middle %s, negative %i", rounded, pixel(round, 0, 0), pixel(round, 0, 10),
		pixel(round, 20, 10), host->fill_round_rect(round, NULL, -1, red));

	// theme colours
	int accent = host->theme_color(MOSAIC_COLOR_ACCENT);
	host->fill_rect(round, &(MosaicRect){ 0, 0, 1, 1 }, mosaic_color((unsigned)accent, 255));
	host->read_pixel(round, 0, 0, &c);
	say("theme fill %i", accent >= 0 && c.r == ((accent >> 16) & 0xFF) && c.g == ((accent >> 8) & 0xFF) && c.b == (accent & 0xFF));

	// images
	const char* art = host->cfg_get(ID, "art", "");
	MosaicSurface* img = keep(host->image_load(ID, art));
	host->surface_size(img, &w, &ht);
	say("image %s %ix%i, left %s, right %s", made(img), w, ht, pixel(img, 0, 0), pixel(img, 3, 1));
	say("image dir %s, missing %s, not an image %s, fifo %s, null %s, not loaded %s",
		made(host->image_load(ID, host->cfg_get(ID, "dir", ""))), made(host->image_load(ID, "/nonexistent/art.png")),
		made(host->image_load(ID, host->cfg_get(ID, "text", ""))), made(host->image_load(ID, host->cfg_get(ID, "fifo", ""))),
		made(host->image_load(ID, NULL)), made(host->image_load("nobody", art)));

	// scaling
	MosaicSurface* contain = keep(host->surface_scale(ID, img, 8, 8, MOSAIC_FIT_CONTAIN));
	say("contain %s, bar %s, left %s, right %s", made(contain), pixel(contain, 0, 0), pixel(contain, 0, 3), pixel(contain, 7, 3));
	MosaicSurface* cover = keep(host->surface_scale(ID, img, 2, 2, MOSAIC_FIT_COVER));
	say("cover %s, left %s, right %s", made(cover), pixel(cover, 0, 0), pixel(cover, 1, 1));
	MosaicSurface* stretch = keep(host->surface_scale(ID, img, 8, 2, MOSAIC_FIT_STRETCH));
	say("stretch %s, %s %s", made(stretch), pixel(stretch, 3, 0), pixel(stretch, 4, 0));
	say("scale bad fit %s, zero %s, null %s", made(host->surface_scale(ID, img, 2, 2, 9)),
		made(host->surface_scale(ID, img, 0, 2, MOSAIC_FIT_COVER)), made(host->surface_scale(ID, NULL, 2, 2, MOSAIC_FIT_COVER)));

	// blits
	MosaicSurface* canvas = keep(host->surface_new(ID, 40, 20));
	int blit = host->blit(canvas, img, NULL, NULL);
	int stretched = host->blit(canvas, img, NULL, &(MosaicRect){ 20, 0, 20, 10 });
	int part = host->blit(canvas, img, &(MosaicRect){ 2, 0, 2, 2 }, &(MosaicRect){ 0, 10, 2, 2 });
	say("blit %i %s %s, stretched %i %s %s, part %i %s, outside %s", blit, pixel(canvas, 0, 0), pixel(canvas, 3, 1),
		stretched, pixel(canvas, 21, 1), pixel(canvas, 39, 9), part, pixel(canvas, 0, 10), pixel(canvas, 10, 15));
	say("blit onto itself %i, null %i, bad rect %i", host->blit(canvas, canvas, NULL, NULL), host->blit(canvas, NULL, NULL, NULL),
		host->blit(canvas, img, &(MosaicRect){ 0, 0, 4, -2 }, NULL));

	// text
	int width = host->text_width(MOSAIC_FONT_LARGE, "Hello");
	int height = host->text_height(MOSAIC_FONT_LARGE);
	say("text width %i, height %i, smaller %i, two lines %i, bad font %i %i, null %i", width > 0, height > 0,
		host->text_width(MOSAIC_FONT_MICRO, "Hello") < width, host->text_width(MOSAIC_FONT_LARGE, "Hello\nworld") == width,
		host->text_width(99, "Hello"), host->text_height(-1), host->text_width(MOSAIC_FONT_LARGE, NULL));
	char out[64];
	const char* name = "A very long game name that won't fit";
	int most = host->text_width(MOSAIC_FONT_LARGE, "A very");
	int cut = host->text_truncate(MOSAIC_FONT_LARGE, name, most, out, sizeof(out));
	size_t len = strlen(out);
	say("truncate fits %i, dots %i, whole %i, small out %i", cut > 0 && cut <= most, len > 3 && !strcmp(out + len - 3, "..."),
		host->text_truncate(MOSAIC_FONT_LARGE, "Hi", 1000, out, sizeof(out)) == host->text_width(MOSAIC_FONT_LARGE, "Hi"),
		host->text_truncate(MOSAIC_FONT_LARGE, name, 1000, out, 8) > 0 && strlen(out) == 7);
	say("truncate no room %i, negative %i, null %i", host->text_truncate(MOSAIC_FONT_LARGE, name, 1000, out, 0),
		host->text_truncate(MOSAIC_FONT_LARGE, name, -1, out, sizeof(out)), host->text_truncate(MOSAIC_FONT_LARGE, name, 10, NULL, 4));

	MosaicSurface* text = keep(host->surface_new(ID, width + 4, height));
	int drawn = host->draw_text(text, MOSAIC_FONT_LARGE, "Hello", 0, 0, mosaic_color(0xFFFFFF, 255), 0);
	int inked = 0;
	for (int y = 0; y < height && !inked; y++) {
		for (int x = 0; x < width && !inked; x++) inked = host->read_pixel(text, x, y, &c) == 0 && c.a > 0;
	}
	int shortened = host->draw_text(text, MOSAIC_FONT_LARGE, name, 0, 0, mosaic_color(0xFFFFFF, 255), width);
	say("draw text %i, inked %i, truncated %i, null %i, bad font %i, negative %i", drawn == width, inked,
		shortened > 0 && shortened <= width, host->draw_text(NULL, MOSAIC_FONT_LARGE, "Hi", 0, 0, red, 0),
		host->draw_text(text, 9, "Hi", 0, 0, red, 0), host->draw_text(text, MOSAIC_FONT_LARGE, "Hi", 0, 0, red, -1));

	// freeing, after which the surface is refused
	int freed = host->surface_free(ID, round);
	if (freed == 0) left--;
	say("free %i, again %i, not mine %i, null %i, then fill %i, then blit %i", freed,
		host->surface_free(ID, round), host->surface_free("nobody", s), host->surface_free(ID, NULL),
		host->fill_rect(round, NULL, red), host->blit(canvas, round, NULL, NULL));

	say("leaving %i surfaces", left);
	if (!strcmp(host->cfg_get(ID, "decline", "0"), "1")) {
		say("declining");
		return 1;
	}
	return 0;
}

static void tick(unsigned long now) {
	static int done = 0;
	(void)now;
	const char* pak = host->cfg_get(ID, "pak", "");
	if (done || !pak[0]) return;
	done = 1;
	say("open pak %i", host->open_path(pak, MOSAIC_ENTRY_PAK));
}

static void quit(void) {
	say("quit");
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
