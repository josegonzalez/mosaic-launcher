// A plugin that shows surfaces of its own in place of the built-in list's
// thumbnail with art_replace, and reads the screen back to see them there.
// It adds a <sd>/Swapper folder to the root holding Apple and Next, Next
// holding Banana, Cherry and another Next, and that one holding Dog and an
// empty Empty folder, and opens Swapper. run.sh gives Apple, Cherry and Dog
// a wide red thumbnail, and Banana none. As an art decorator it logs each
// new art rect it's handed for a game.
//
// From tick, once Apple's thumbnail is in, it has art_replace refuse bad
// calls, shows a green 720x480 surface, then writes it blue and calls
// art_replace again each frame for a while without asking for a redraw,
// counting the list's redraws, and opens Next. In Next it shows the green
// surface for Banana, which has no art, then selects Cherry and opens the
// next Next. There it shows a small surface for Dog and has taker (a second
// plugin, which watches this one's hold setting) be refused, frees it so
// taker's shows and is refused itself, then has taker clear its own and
// opens Empty, where art_replace is refused. As the transition slot's
// owner (slots=transition in its plugin.txt) it reads each push's old
// frame, where the layers are drawn together: the art an eighth of the way
// in (the left of run.sh's art is red) and its top left corner, and draws
// the push with no transition.
//
// With once=1 in its config it opens Swapper, waits and logs one call
// instead, for a launcher with Show game art off or a plugin layout.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "swapper"
#define FRAME_W 720
#define FRAME_H 480
#define FRAMES 30

static const MosaicHost* host;
static char folder[512], next1[600], next2[700], empty[800];
static int once = 0;
static MosaicSurface* frame = NULL;
static MosaicSurface* small = NULL;
static int ticks = 0, step = 0, step_at = 0;
static int redraws = 0;     // decorate_art calls, one each time the list draws
static int frames_failed = 0;
static int pushes = 0;
static MosaicRect last_art; // the latest art rect decorate_art was handed
static char last_game[32] = "";
static int in_hook_done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Swapper"));
}

static void addGame(MosaicList* list, const char* dir, const char* name) {
	char file[900];
	snprintf(file, sizeof(file), "%s/%s.gb", dir, name);
	host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, name));
}

static MosaicList* virtualDir(const char* path) {
	MosaicList* list = NULL;
	if (!strcmp(path, folder)) {
		list = host->list_new();
		addGame(list, folder, "Apple");
		if (!once) host->list_push(list, host->entry_new(next1, MOSAIC_ENTRY_DIR, "Next"));
	}
	else if (!strcmp(path, next1)) {
		list = host->list_new();
		addGame(list, next1, "Banana");
		addGame(list, next1, "Cherry");
		host->list_push(list, host->entry_new(next2, MOSAIC_ENTRY_DIR, "Next"));
	}
	else if (!strcmp(path, next2)) {
		list = host->list_new();
		addGame(list, next2, "Dog");
		host->list_push(list, host->entry_new(empty, MOSAIC_ENTRY_DIR, "Empty"));
	}
	else if (!strcmp(path, empty)) list = host->list_new();
	return list;
}

// "RRGGBB" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[4][8];
	static int next = 0;
	char* text = out[next++ % 4];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 8, "%02X%02X%02X", c.r, c.g, c.b);
	return text;
}

static const char* selectedName(void) {
	const MosaicEntry* entry = host->selected_entry();
	return entry ? host->entry_name(entry) : "";
}

static int decorateArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* art, const MosaicRect* below) {
	(void)below;
	redraws++;
	const char* name = host->entry_name(entry);
	if (!in_hook_done) {
		// a hook that only draws can't change the art
		in_hook_done = 1;
		say("in decorate_art %i", host->art_replace(ID, frame));
	}
	if (strcmp(name, last_game) != 0 || memcmp(art, &last_art, sizeof(*art)) != 0) {
		say("art %s rect %i,%i %ix%i", name, art->x, art->y, art->w, art->h);
		snprintf(last_game, sizeof(last_game), "%s", name);
		last_art = *art;
	}
	(void)screen;
	return 0;
}

static int transitionBegin(MosaicTransition* t) {
	if (t->kind != MOSAIC_TRANSITION_PUSH || step < 2) return MOSAIC_STYLE_OFF;
	pushes++;
	say("push %i %s left %s corner %s", pushes, last_game, pixel(t->old_frame, last_art.x + last_art.w / 8,
		last_art.y + last_art.h / 2), pixel(t->old_frame, last_art.x, last_art.y));
	return MOSAIC_STYLE_OFF;
}

// the whole of s in one colour, with surface_write
static int paint(MosaicSurface* s, int w, int h, unsigned int rgb) {
	unsigned char* px = malloc((size_t)w * h * 4);
	if (!px) return -1;
	for (int i = 0; i < w * h; i++) {
		px[i * 4] = (unsigned char)(rgb >> 16);
		px[i * 4 + 1] = (unsigned char)(rgb >> 8);
		px[i * 4 + 2] = (unsigned char)rgb;
		px[i * 4 + 3] = 255;
	}
	int rc = host->surface_write(ID, s, NULL, px, w * 4);
	free(px);
	return rc;
}

static void go(int next) {
	step = next;
	step_at = ticks;
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	int waited = ticks - step_at;
	const char* path = host->current_path();
	const char* game = selectedName();
	if (once) {
		if (step == 0 && path && !strcmp(path, folder) && waited > 40) {
			say("once %i", host->art_replace(ID, frame));
			say("done");
			go(1);
		}
		return;
	}
	switch (step) {
	case 0:
		// Apple's own thumbnail is in
		if (!path || strcmp(path, folder) != 0 || strcmp(last_game, "Apple") != 0) break;
		{
			const MosaicSurface* cached = NULL;
			if (host->art_get(host->selected_entry(), MOSAIC_ART_THUMBNAIL, &cached) != MOSAIC_ART_READY) break;
			int unknown = 0;
			say("null id %i, not loaded %i, unknown %i, other's id %i, art cache %i, clear none %i",
				host->art_replace(NULL, frame), host->art_replace("nobody", frame),
				host->art_replace(ID, (MosaicSurface*)&unknown), host->art_replace("hello", frame),
				host->art_replace(ID, (MosaicSurface*)cached), host->art_replace(ID, NULL));
		}
		say("show %i", host->art_replace(ID, frame));
		go(1);
		break;
	case 1:
		// the list draws again for its decorators, at the frame's size
		if (last_art.h != 306 && waited < 120) break;
		paint(frame, FRAME_W, FRAME_H, 0x0000FF);
		redraws = 0;
		go(2);
		break;
	case 2:
		// new frames of the same surface only redraw the thumbnail
		frames_failed |= host->art_replace(ID, frame);
		if (waited < FRAMES) break;
		say("frames %i failed %i, list redraws %i", FRAMES, frames_failed, redraws);
		say("open next %i", host->open_path(next1, MOSAIC_ENTRY_DIR));
		go(3);
		break;
	case 3:
		// Banana has no art of its own
		if (!path || strcmp(path, next1) != 0 || strcmp(game, "Banana") != 0 || waited < 20) break;
		paint(frame, FRAME_W, FRAME_H, 0x00FF00);
		say("show Banana %i", host->art_replace(ID, frame));
		go(4);
		break;
	case 4:
		if (strcmp(last_game, "Banana") != 0 && waited < 120) break;
		say("select Cherry %i", host->select_index(1));
		go(5);
		break;
	case 5:
		// the move took it away, so Cherry's own thumbnail shows
		if ((strcmp(last_game, "Cherry") != 0 && waited < 120) || waited < 20) break;
		say("open next %i", host->open_path(next2, MOSAIC_ENTRY_DIR));
		go(6);
		break;
	case 6:
		if (!path || strcmp(path, next2) != 0 || strcmp(last_game, "Dog") != 0 || waited < 10) break;
		small = host->surface_new(ID, 90, 60);
		paint(small, 90, 60, 0x0000FF);
		say("show small %i", host->art_replace(ID, small));
		host->cfg_set(ID, "hold", "1");
		go(7);
		break;
	case 7:
		// taker is refused while this one's shows
		if (waited < 10) break;
		say("free small %i", host->surface_free(ID, small));
		host->cfg_set(ID, "hold", "2");
		go(8);
		break;
	case 8:
		// taker's shows once this one's surface is gone
		if (waited < 10) break;
		say("while taker's shows %i", host->art_replace(ID, frame));
		host->cfg_set(ID, "hold", "3");
		go(9);
		break;
	case 9:
		if (waited < 20) break;
		say("open empty %i", host->open_path(empty, MOSAIC_ENTRY_DIR));
		go(10);
		break;
	case 10:
		if (!path || strcmp(path, empty) != 0 || waited < 10) break;
		say("empty %i", host->art_replace(ID, frame));
		say("done");
		go(11);
		break;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, art_replace)) return 1;
	snprintf(folder, sizeof(folder), "%s/Swapper", host->sdcard_path);
	snprintf(next1, sizeof(next1), "%s/Next", folder);
	snprintf(next2, sizeof(next2), "%s/Next", next1);
	snprintf(empty, sizeof(empty), "%s/Empty", next2);
	once = atoi(host->cfg_get(ID, "once", "0"));
	host->cfg_set(ID, "hold", "0");
	frame = host->surface_new(ID, FRAME_W, FRAME_H);
	paint(frame, FRAME_W, FRAME_H, 0x00FF00);
	say("init %i", host->art_replace(ID, frame));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.transition_begin = transitionBegin,
	.decorate_art = decorateArt,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
