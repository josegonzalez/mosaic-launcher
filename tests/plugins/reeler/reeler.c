// A plugin that shows a surface of its own in place of the selected game's
// art with art_replace under a plugin layout, as a video preview would,
// and reads the layout's screen back to see it there. It adds a <sd>/Reeler
// folder holding Apple and Banana to the root and opens it; run.sh gives
// both a thumbnail that's green in the middle. As a row decorator, which a
// layout runs on each cell it draws, it reads the middle of the selected
// cell's art (the cell above its name), and as an art decorator, which a
// layout runs under the art it passes to host->decorate_art, the middle of
// that art. It logs each change in what it reads, and counts the times the
// layout draws the selected cell. It goes by the cells, or with read=art in
// its config by the art, for a layout whose art is outside its cells.
//
// From tick, once Apple's own art shows, it shows a red 720x480 surface,
// then writes it blue and calls art_replace again each frame for frames
// frames (30 by default, frames=N in its config) without asking for a
// redraw, counting the layout's redraws, and logs how long the frames took.
// Then it takes the surface away, shows it again and selects Banana, which
// takes it away by itself. With plain=1 in its config, for a layout that
// doesn't draw it, it doesn't wait to see any art and stops after the
// frames.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "reeler"
#define FRAME_W 720
#define FRAME_H 480

static const MosaicHost* host;
static char folder[512];
static MosaicSurface* frame = NULL;
static int frames = 30;
static int by_art = 0; // read=art
static int plain = 0;
static int ticks = 0, step = 0, step_at = 0;
static int draws = 0; // the selected cell or art drawn
static int failed = 0;
static unsigned int seen = 0x1000000; // the selected art's middle, last read
static char seen_name[32] = "";
static char cell_line[128] = "", art_line[160] = "";
static double frames_start = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static double nowMs(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Reeler"));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	static const char* names[] = { "Apple", "Banana" };
	for (int i = 0; i < 2; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
}

static int ours(const MosaicEntry* entry) {
	const char* path = host->entry_path(entry);
	return path && !strncmp(path, folder, strlen(folder)) && path[strlen(folder)] == '/';
}

static unsigned int pixel(const MosaicSurface* s, int x, int y) {
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return 0x1000000;
	return (unsigned int)c.r << 16 | (unsigned int)c.g << 8 | c.b;
}

static void saw(const MosaicEntry* entry, unsigned int colour) {
	draws++;
	seen = colour;
	snprintf(seen_name, sizeof(seen_name), "%s", host->entry_name(entry));
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	if (!row->selected || !ours(entry)) return 0;
	const MosaicRect* r = &row->rect;
	int art_h = row->name.w > 0 ? row->name.y - r->y : r->h;
	unsigned int middle = pixel(screen, r->x + r->w / 2, r->y + art_h / 2);
	if (!by_art) saw(entry, middle);
	char line[128];
	snprintf(line, sizeof(line), "cell %s %06X", host->entry_name(entry), middle);
	if (strcmp(line, cell_line) != 0) {
		snprintf(cell_line, sizeof(cell_line), "%s", line);
		say("%s", line);
	}
	return 0;
}

static int decorateArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* art, const MosaicRect* below) {
	(void)below;
	if (!ours(entry)) return 0;
	unsigned int middle = pixel(screen, art->x + art->w / 2, art->y + art->h / 2);
	if (by_art) saw(entry, middle);
	char line[160];
	snprintf(line, sizeof(line), "art %s %06X rect %i,%i %ix%i", host->entry_name(entry), middle, art->x, art->y, art->w,
		art->h);
	if (strcmp(line, art_line) != 0) {
		snprintf(art_line, sizeof(art_line), "%s", line);
		say("%s", line);
	}
	return 0;
}

// the whole of the frame in one colour, with surface_write
static int paint(unsigned int rgb) {
	unsigned char* px = malloc((size_t)FRAME_W * FRAME_H * 4);
	if (!px) return -1;
	for (int i = 0; i < FRAME_W * FRAME_H; i++) {
		px[i * 4] = (unsigned char)(rgb >> 16);
		px[i * 4 + 1] = (unsigned char)(rgb >> 8);
		px[i * 4 + 2] = (unsigned char)rgb;
		px[i * 4 + 3] = 255;
	}
	int rc = host->surface_write(ID, frame, NULL, px, FRAME_W * 4);
	free(px);
	return rc;
}

static void go(int next) {
	step = next;
	step_at = ticks;
}

static int showing(const char* name, unsigned int colour) {
	return !strcmp(seen_name, name) && seen == colour;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	// after a layout plugin's own folder opens
	if (ticks == 5) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	int waited = ticks - step_at;
	const char* path = host->current_path();
	if (!path || strcmp(path, folder) != 0) return;
	switch (step) {
	case 0:
		// Apple's own art, and past the frames a layout under test asks for at the start
		if ((!plain && !showing("Apple", 0x00FF00)) || ticks < 80) break;
		paint(0xFF0000);
		say("show %i", host->art_replace(ID, frame));
		go(1);
		break;
	case 1:
		if (!plain && !showing("Apple", 0xFF0000) && waited < 120) break;
		say("shown %06X", seen);
		paint(0x0000FF);
		draws = 0;
		frames_start = nowMs();
		go(2);
		break;
	case 2:
		// new frames of the same surface, with no request_redraw
		failed |= host->art_replace(ID, frame);
		if (waited < frames) break;
		say("frames %i failed %i, redraws %i, now %06X, %.2f ms a frame", frames, failed, draws, seen,
			(nowMs() - frames_start) / frames);
		if (plain) {
			say("done");
			go(6);
			break;
		}
		say("clear %i", host->art_replace(ID, NULL));
		go(3);
		break;
	case 3:
		// its own art again
		if (!showing("Apple", 0x00FF00) && waited < 120) break;
		say("cleared %06X", seen);
		paint(0xFF0000);
		say("show again %i", host->art_replace(ID, frame));
		go(4);
		break;
	case 4:
		if (!showing("Apple", 0xFF0000) && waited < 120) break;
		say("select Banana %i", host->select_index(1));
		go(5);
		break;
	case 5:
		// the move took it away
		if (!showing("Banana", 0x00FF00) && waited < 120) break;
		say("moved %s %06X", seen_name, seen);
		say("done");
		go(6);
		break;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, art_replacement) || !MOSAIC_HOST_HAS(host, surface_write)) return 1;
	snprintf(folder, sizeof(folder), "%s/Reeler", host->sdcard_path);
	frames = atoi(host->cfg_get("frames", "30"));
	if (frames < 1) frames = 1;
	by_art = !strcmp(host->cfg_get("read", "cell"), "art");
	plain = atoi(host->cfg_get("plain", "0"));
	frame = host->surface_new(ID, FRAME_W, FRAME_H);
	return frame ? 0 : 1;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.decorate_row = decorateRow,
	.decorate_art = decorateArt,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
