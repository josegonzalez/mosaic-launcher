// Captures and saves the screen. From init, and from a thread of its own,
// both are refused. Its overlay is a 16 pixel green square at the left
// edge, halfway down, which the launcher draws over the game list but not
// over dialogs; each capture logs the pixel there, so a case can tell the
// game list's frame from a dialog's. From tick, on the game list, it
// captures and saves, then opens a choose dialog, captures while it shows,
// and presses A, whose pick callback captures and saves again, all of
// which should be the game list's frame. Then it fills its surface budget
// and captures once more, which is refused, and logs "done". With quick=1
// it only saves from the game list before "done".

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "snapper"
#define GREEN 0x12AB34

// SDL2's keyboard event, as SDL_Event lays it out
typedef struct {
	unsigned int type, timestamp, window;
	unsigned char state, repeat, pad2, pad3;
	int scancode, sym;
	unsigned short mod;
	unsigned int unused;
} KeyEvent;
typedef union {
	unsigned int type;
	KeyEvent key;
	unsigned char bytes[56];
} Event;
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SCANCODE_A 22 // S, the desktop build's A

static int (*pushEvent)(Event* event);
static const MosaicHost* host;
static int quick = 0;
static int ticks = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void key(int down) {
	if (!pushEvent) return;
	Event event;
	memset(&event, 0, sizeof(event));
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? 1 : 0;
	event.key.scancode = SCANCODE_A;
	if (pushEvent(&event) != 1) say("can't push a key event");
}

// a capture's size against the screen's and its pixel under the overlay,
// or "null"; it's freed
static const char* captured(MosaicSurface* s) {
	static char out[64];
	if (!s) return "null";
	int w = 0, h = 0;
	MosaicColor c = { 0, 0, 0, 0 };
	host->surface_size(s, &w, &h);
	host->read_pixel(s, 4, host->screen_height() / 2 + 4, &c);
	snprintf(out, sizeof(out), "full %i probe %02X%02X%02X%02X", w == host->screen_width() && h == host->screen_height(),
		c.r, c.g, c.b, c.a);
	host->surface_free(ID, s);
	return out;
}

// the saved file's name, or "-"
static const char* saved(int rc, const char* path) {
	const char* slash = strrchr(path, '/');
	return rc == 0 && slash ? slash + 1 : "-";
}

static void save(const char* when) {
	char path[512] = "";
	int rc = host->save_screenshot(path, sizeof(path));
	say("%s save %i %s", when, rc, saved(rc, path));
	if (rc == 0) say("%s path %s", when, path);
}

static void* fromThread(void* data) {
	(void)data;
	MosaicSurface* s = host->capture_screen();
	char path[512] = "";
	int rc = host->save_screenshot(path, sizeof(path));
	say("thread capture %s save %i", captured(s), rc);
	return NULL;
}

static void picked(int index, void* data) {
	(void)data;
	say("picked %i on %s capture %s", index, host->current_screen(), captured(host->capture_screen()));
	save("pick");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, save_screenshot)) {
		say("no save_screenshot");
		return 1;
	}
	quick = atoi(host->cfg_get("quick", "0"));
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	if (!pushEvent) say("no SDL_PushEvent");
	char path[512] = "";
	say("init capture %s save %i", captured(host->capture_screen()), host->save_screenshot(path, sizeof(path)));
	pthread_t thread;
	if (pthread_create(&thread, NULL, fromThread, NULL) == 0) pthread_join(thread, NULL);
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	if (ticks == 40) {
		say("list on %s capture %s", host->current_screen(), captured(host->capture_screen()));
		save("list");
		if (quick) say("done");
	}
	if (quick) return;
	if (ticks == 60) {
		static const char* const choices[] = { "Snap", "Skip" };
		say("choose %i", host->choose("Snapper", choices, 2, 0, picked, NULL));
	}
	if (ticks == 80) say("menu on %s capture %s", host->current_screen(), captured(host->capture_screen()));
	if (ticks == 90) key(1);
	if (ticks == 93) key(0);
	if (ticks == 120) {
		say("after on %s capture %s", host->current_screen(), captured(host->capture_screen()));
		// nearly all of its 32 MiB, leaving less than a frame
		MosaicSurface* filler = host->surface_new(ID, 4096, 2000);
		say("filled %i over budget capture %s", filler != NULL, captured(host->capture_screen()));
		if (filler) host->surface_free(ID, filler);
		say("done");
	}
}

static int overlayRender(MosaicSurface* overlay, const MosaicScreenView* view) {
	(void)view;
	MosaicRect square = { 0, host->screen_height() / 2, 16, 16 };
	host->fill_rect(overlay, &square, mosaic_color(GREEN, 255));
	// refused while drawing
	static int tried = 0;
	if (!tried++) say("overlay capture %s", captured(host->capture_screen()));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.overlay_render = overlayRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
