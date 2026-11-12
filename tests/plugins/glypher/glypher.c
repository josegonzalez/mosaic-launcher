// A plugin that owns the strings and theme slots, to supply fallback
// fonts: its config's theme_font is its theme's fallback_font and
// strings_font what its fallback_font hook returns ("" for none, which the
// hook returns as NULL). It translates nothing.
//
// From tick, once the list shows, it draws text on a surface of its own
// with draw_text and logs what it reads back, in the large font:
//
//   ascii <width> <hash>     "Hello, World" and a hash of its pixels
//   hangul <width> <alpha>   "한" and the alpha halfway down its middle
//   mixed <same|differs>     whether "A한" is as wide as "A" and "한"
//   truncated <width> <ok|cut>  ten "한" truncated to three and a half
//                            of them: whether it was cut between characters
//
// With reload=1 it then empties strings_font, reloads the lists and logs
// the same again after "reloaded". Then it logs "done".

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "glypher"
#define HANGUL "\xED\x95\x9C" // 한

static const MosaicHost* host;
static char strings_font[512];
static int reload = 0;
static int step = 0;
static long step_at = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[900];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static long ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static const char* translate(const char* plugin_id, const char* context, const char* text) {
	(void)plugin_id;
	(void)context;
	(void)text;
	return NULL;
}

static const char* fallbackFont(void) {
	return strings_font[0] ? strings_font : NULL;
}

static int theme(MosaicTheme* t) {
	if (MOSAIC_THEME_HAS(t, fallback_font))
		snprintf(t->fallback_font, sizeof(t->fallback_font), "%s", host->cfg_get("theme_font", ""));
	return 0;
}

static const MosaicColor white = { 255, 255, 255, 255 };

// text drawn on a new surface, or NULL
static MosaicSurface* drawn(const char* text, int* w, int* h) {
	*w = host->text_width(MOSAIC_FONT_LARGE, text);
	*h = host->text_height(MOSAIC_FONT_LARGE);
	if (*w <= 0 || *h <= 0) return NULL;
	MosaicSurface* s = host->surface_new(ID, *w, *h);
	if (!s) return NULL;
	host->draw_text(s, MOSAIC_FONT_LARGE, text, 0, 0, white, 0);
	return s;
}

static void report(void) {
	int w, h;
	MosaicSurface* s = drawn("Hello, World", &w, &h);
	unsigned long hash = 2166136261UL;
	for (int y = 0; s && y < h; y++)
		for (int x = 0; x < w; x++) {
			MosaicColor c = { 0, 0, 0, 0 };
			host->read_pixel(s, x, y, &c);
			unsigned char bytes[4] = { c.r, c.g, c.b, c.a };
			for (int i = 0; i < 4; i++) hash = ((hash ^ bytes[i]) * 16777619UL) & 0xFFFFFFFFUL;
		}
	say("ascii %d %08lX", w, hash);
	if (s) host->surface_free(ID, s);

	s = drawn(HANGUL, &w, &h);
	MosaicColor c = { 0, 0, 0, 0 };
	if (s) host->read_pixel(s, w / 2, h / 2, &c);
	say("hangul %d %d", w, c.a);
	if (s) host->surface_free(ID, s);
	int hangul = w;

	int a = host->text_width(MOSAIC_FONT_LARGE, "A");
	int both = host->text_width(MOSAIC_FONT_LARGE, "A" HANGUL);
	say("mixed %s", both == a + hangul ? "same" : "differs");

	char out[64];
	int width = host->text_truncate(MOSAIC_FONT_LARGE, HANGUL HANGUL HANGUL HANGUL HANGUL HANGUL HANGUL HANGUL HANGUL HANGUL,
		hangul * 7 / 2, out, sizeof(out));
	// whole characters and then "..."
	size_t len = strlen(out);
	int whole = len > 3 && !strcmp(out + len - 3, "...") && (len - 3) % 3 == 0;
	for (size_t i = 0; whole && i + 3 < len; i += 3) whole = !strncmp(out + i, HANGUL, 3);
	say("truncated %d %s", width, whole ? "ok" : "cut");
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	host->request_redraw();
	long at = ms();
	if (step == 0) {
		step_at = at;
		step = 1;
	}
	else if (step == 1 && at >= step_at + 300) {
		report();
		if (!reload) {
			say("done");
			step = 4;
			return;
		}
		strings_font[0] = '\0';
		host->reload_lists();
		step = 2;
		step_at = at;
	}
	else if (step == 2 && at >= step_at + 300) {
		say("reloaded");
		report();
		say("done");
		step = 4;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, ui_size)) return 1;
	snprintf(strings_font, sizeof(strings_font), "%s", host->cfg_get("strings_font", ""));
	reload = !strcmp(host->cfg_get("reload", ""), "1");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.translate = translate,
	.theme = theme,
	.fallback_font = fallbackFont,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
