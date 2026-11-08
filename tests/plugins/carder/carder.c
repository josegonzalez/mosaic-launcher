// A plugin that looks at the bundled art-placeholder plugin's cards. Once
// the lists are up it opens <sd>/Placeholder, where Bravo has a real
// thumbnail and Alpha and Charlie have no art, and logs what art_get gives
// for them: each card's size, whether its edge, panel and middle are in
// the theme's colours, a signature of its pixels to compare across starts,
// and how long art_get took to make it. Its own art_surface, loaded before
// art-placeholder, logs each time the launcher asks for generated art and
// makes none.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "carder"

static const MosaicHost* host;
static char folder[512];
static int ticks = 0;
static int step = 0;
static int polls = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static unsigned int rgbAt(const MosaicSurface* s, int x, int y) {
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return 0xFFFFFFFFu;
	return (unsigned int)c.r << 16 | (unsigned int)c.g << 8 | c.b;
}

// FNV-1a over a 16 by 16 grid of the surface's pixels
static unsigned int signature(const MosaicSurface* s, int w, int h) {
	unsigned int hash = 2166136261u;
	for (int j = 0; j < 16; j++) {
		for (int i = 0; i < 16; i++) {
			MosaicColor c = { 0, 0, 0, 0 };
			host->read_pixel(s, i * w / 16 + w / 32, j * h / 16 + h / 32, &c);
			unsigned char bytes[4] = { c.r, c.g, c.b, c.a };
			for (int k = 0; k < 4; k++) {
				hash ^= bytes[k];
				hash *= 16777619u;
			}
		}
	}
	return hash;
}

static const MosaicEntry* game(const char* name) {
	const MosaicList* list = host->current_list();
	for (int i = 0; list && i < host->list_count(list); i++) {
		const MosaicEntry* entry = host->list_get(list, i);
		if (!strcmp(host->entry_name(entry), name)) return entry;
	}
	return NULL;
}

static long micros(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}

static void card(const char* name, int kind) {
	const MosaicSurface* art = NULL;
	long start = micros();
	int state = host->art_get(game(name), kind, &art);
	long took = micros() - start;
	int w = 0, h = 0;
	host->surface_size(art, &w, &h);
	if (state != MOSAIC_ART_READY) {
		say("card %s %i %i", name, kind, state);
		return;
	}
	unsigned int accent = (unsigned int)host->theme_color(MOSAIC_COLOR_ACCENT) & 0xFFFFFF;
	unsigned int accent2 = (unsigned int)host->theme_color(MOSAIC_COLOR_ACCENT2) & 0xFFFFFF;
	int margin = (w < h ? w : h) / 16;
	int text = 0;
	for (int x = margin * 2; x < w - margin * 2; x++) text += rgbAt(art, x, h / 2) != accent;
	say("card %s %i %i %ix%i edge %i panel %i text %i sig %08X", name, kind, state, w, h, rgbAt(art, 2, 2) == accent2,
		rgbAt(art, w / 2, margin + 2) == accent, text > 0, signature(art, w, h));
	say("time %s %i %ld us", name, kind, took);
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	const char* path = host->current_path();
	if (ticks < 4 || !path || strcmp(path, folder) != 0 || step > 1) return;
	host->request_redraw();
	if (step == 0) {
		card("Alpha", MOSAIC_ART_THUMBNAIL);
		card("Alpha", MOSAIC_ART_BOX);
		card("Charlie", MOSAIC_ART_THUMBNAIL);
		card("Charlie", MOSAIC_ART_BOX);
		// Bravo has a thumbnail, so no box is made for it either
		card("Bravo", MOSAIC_ART_BOX);
		step++;
		return;
	}
	const MosaicSurface* art = NULL;
	int state = host->art_get(game("Bravo"), MOSAIC_ART_THUMBNAIL, &art);
	if (state == MOSAIC_ART_LOADING && polls++ < 600) return;
	say("real Bravo %i %06X", state, rgbAt(art, 0, 0));
	say("done");
	step++;
}

static MosaicSurface* artSurface(const MosaicEntry* entry, int kind) {
	const char* path = host->entry_path(entry);
	size_t len = strlen(folder);
	if (path && !strncmp(path, folder, len) && path[len] == '/') say("asked %s %i", host->entry_name(entry), kind);
	return NULL;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, plugin_art)) return 1;
	snprintf(folder, sizeof(folder), "%s/Placeholder", host->sdcard_path);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.art_surface = artSurface,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
