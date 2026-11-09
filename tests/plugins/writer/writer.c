// A plugin that writes RGBA8888 pixels into surfaces of its own with
// surface_write, reading them back with read_pixel and blit, and has the
// launcher refuse writes into surfaces that aren't its own: one lent to its
// row decorator, an art cache image (art=<path> in its config), and one
// written under another loaded plugin's id (other=<id>). It times copying
// a 720x480 frame, against a plain memcpy of the same bytes.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "writer"
#define FRAME_W 720
#define FRAME_H 480
#define FRAME_RUNS 200

static const MosaicHost* host;
static char folder[512];
static MosaicSurface* mine = NULL;
static int lent_done = 0, art_done = 0, other_done = 0, said_done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
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

static void put(unsigned char* p, unsigned int rgba) {
	p[0] = (unsigned char)(rgba >> 24);
	p[1] = (unsigned char)(rgba >> 16);
	p[2] = (unsigned char)(rgba >> 8);
	p[3] = (unsigned char)rgba;
}

static double now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void timeFrame(void) {
	MosaicSurface* frame = host->surface_new(ID, FRAME_W, FRAME_H);
	size_t bytes = (size_t)FRAME_W * FRAME_H * 4;
	unsigned char* pixels = malloc(bytes);
	unsigned char* copy = malloc(bytes);
	if (!frame || !pixels || !copy) {
		say("frame not timed");
		free(pixels);
		free(copy);
		return;
	}
	for (size_t i = 0; i < bytes; i++) pixels[i] = (unsigned char)(i * 7);
	int failed = 0;
	double start = now_ms();
	for (int i = 0; i < FRAME_RUNS; i++) failed |= host->surface_write(ID, frame, NULL, pixels, FRAME_W * 4);
	double written = (now_ms() - start) / FRAME_RUNS;
	start = now_ms();
	for (int i = 0; i < FRAME_RUNS; i++) {
		pixels[i] ^= 1;
		memcpy(copy, pixels, bytes);
	}
	double copied = (now_ms() - start) / FRAME_RUNS;
	say("frame %ix%i written %i, %.3f ms a frame, memcpy %.3f ms (%u)", FRAME_W, FRAME_H, failed, written, copied,
		copy[FRAME_RUNS]);
	host->surface_free(ID, frame);
	free(pixels);
	free(copy);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, surface_write)) return 1;
	snprintf(folder, sizeof(folder), "%s/Writer", host->sdcard_path);
	MosaicColor red = mosaic_color(0xFF0000, 255);

	// two of a 4x2 surface's columns, from rows padded to 3 pixels
	mine = host->surface_new(ID, 4, 2);
	host->fill_rect(mine, NULL, red);
	unsigned char block[2 * 12];
	memset(block, 0xEE, sizeof(block));
	put(block + 0, 0x00FF00FF);
	put(block + 4, 0x0000FF80);
	put(block + 12, 0x12345678);
	put(block + 16, 0x00000000);
	int wrote = host->surface_write(ID, mine, &(MosaicRect){ 1, 0, 2, 2 }, block, 12);
	say("write %i, %s %s %s %s, %s %s %s %s", wrote, pixel(mine, 0, 0), pixel(mine, 1, 0), pixel(mine, 2, 0),
		pixel(mine, 3, 0), pixel(mine, 0, 1), pixel(mine, 1, 1), pixel(mine, 2, 1), pixel(mine, 3, 1));

	// all of it with a NULL rect
	MosaicSurface* all = host->surface_new(ID, 4, 2);
	unsigned char whole[4 * 2 * 4];
	for (int i = 0; i < 8; i++) put(whole + i * 4, 0x11223344);
	int wrote_all = host->surface_write(ID, all, NULL, whole, 16);
	say("whole %i, %s %s", wrote_all, pixel(all, 0, 0), pixel(all, 3, 1));

	// past the top left edge: the first row and column are left out
	unsigned char three[3 * 3 * 4];
	for (int i = 0; i < 9; i++) put(three + i * 4, 0x10101000u * (unsigned)(i + 1) | 0xFF);
	int clipped = host->surface_write(ID, all, &(MosaicRect){ -1, -1, 3, 3 }, three, 12);
	say("clipped %i, %s %s %s, off %i, %s", clipped, pixel(all, 0, 0), pixel(all, 1, 1), pixel(all, 2, 0),
		host->surface_write(ID, all, &(MosaicRect){ 10, 10, 2, 2 }, three, 12), pixel(all, 3, 1));

	// what it wrote is what blit draws
	MosaicSurface* canvas = host->surface_new(ID, 4, 2);
	host->blit(canvas, mine, NULL, NULL);
	say("blit %s %s", pixel(canvas, 1, 0), pixel(canvas, 3, 1));

	// bad arguments
	say("null pixels %i, null surface %i, unknown %i, short pitch %i, negative %i, not loaded %i, no id %i",
		host->surface_write(ID, mine, NULL, NULL, 16), host->surface_write(ID, NULL, NULL, whole, 16),
		host->surface_write(ID, (MosaicSurface*)whole, NULL, whole, 16),
		host->surface_write(ID, mine, &(MosaicRect){ 0, 0, 2, 2 }, whole, 7),
		host->surface_write(ID, mine, &(MosaicRect){ 0, 0, -2, 2 }, whole, 16),
		host->surface_write("nobody", mine, NULL, whole, 16), host->surface_write(NULL, mine, NULL, whole, 16));

	// a loaded image is its own too, and a freed surface isn't
	MosaicSurface* img = host->image_load(ID, host->cfg_get(ID, "art", ""));
	int img_wrote = host->surface_write(ID, img, &(MosaicRect){ 0, 0, 1, 1 }, whole, 4);
	say("image %i %s", img_wrote, pixel(img, 0, 0));
	host->surface_free(ID, canvas);
	say("freed %i", host->surface_write(ID, canvas, NULL, whole, 16));

	timeFrame();
	return 0;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Writer"));
}

// the screen it's lent is the launcher's
static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	if (lent_done || strcmp(host->entry_path(entry), folder) != 0) return 0;
	unsigned char px[4] = { 0, 0, 0, 255 };
	lent_done = 1;
	say("lent %i", host->surface_write(ID, screen, &(MosaicRect){ row->rect.x, row->rect.y, 1, 1 }, px, 4));
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	unsigned char px[4] = { 0, 0, 0, 255 };
	if (!other_done) {
		// another loaded plugin's id can't write into this one's surface
		other_done = 1;
		const char* other = host->cfg_get(ID, "other", "");
		say("other %i, own %i", host->surface_write(other, mine, &(MosaicRect){ 0, 0, 1, 1 }, px, 4),
			host->surface_write(ID, mine, &(MosaicRect){ 0, 0, 1, 1 }, px, 4));
	}
	if (!art_done) {
		const MosaicSurface* art = NULL;
		int got = host->art_load(host->cfg_get(ID, "art", ""), &art);
		if (got == MOSAIC_ART_READY) {
			art_done = 1;
			say("art %i %s", host->surface_write(ID, (MosaicSurface*)art, &(MosaicRect){ 0, 0, 1, 1 }, px, 4),
				pixel(art, 0, 0));
		}
		else if (got == MOSAIC_ART_MISSING) {
			art_done = 1;
			say("art missing");
		}
	}
	if (lent_done && art_done && other_done && !said_done) {
		said_done = 1;
		say("done");
	}
	if (!said_done) host->request_redraw();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.tick = tick,
	.decorate_row = decorateRow,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
