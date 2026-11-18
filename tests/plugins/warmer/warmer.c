// Reads the bundled blue-light plugin's tint back off the screen. Its
// overlay is a white 16 pixel square at the left edge, halfway down (the
// probe), over the game list, the quick menu and plugin screens; its
// screen warmer.screen fills the probe white too, and its ambient screen
// warmer.ambient (MOSAIC_SCREEN_FULL, which has no overlays) fills the
// whole screen white. So the probe is white without a tint, and the tint
// itself with one.
//
// Its steps setting is a comma separated list run from tick, one every
// 20 ticks from the 40th: action:<id> calls run_action, quick opens the
// quick menu, b presses B (the desktop build reads the keyboard through
// SDL, so it pushes key events with SDL_PushEvent, found with dlsym: a
// test-only shortcut), screen opens warmer.screen and close closes it,
// ambient lets warmer.ambient open and unambient closes it, time:<seconds>
// writes the time to the file MOSAIC_BLUE_LIGHT_FAKE names, the fake
// clock blue-light reads, probe:<label> logs the probe's pixel as the
// renderer last showed it (read back with SDL_RenderReadPixels, found with
// dlsym like SDL_PushEvent), with what current_screen and tint_shown say,
// capture:<label> logs the probe's pixel in capture_screen's frame, save
// saves a screenshot and logs its path and the probe's place in it, and
// any other word waits. It logs "done" 40 ticks after the last step.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "warmer"
#define SCREEN_ID ID ".screen"
#define AMBIENT_ID ID ".ambient"
#define WHITE 0xFFFFFF
#define SQUARE 16

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
#define SCANCODE_B 4 // A, the desktop build's B
#define PIXELFORMAT_ARGB8888 0x16362004u

typedef struct {
	int x, y, w, h;
} Rect;

static int (*pushEvent)(Event* event);
static void* (*currentWindow)(void);
static void* (*windowRenderer)(void* window);
static int (*readPixels)(void* renderer, const Rect* rect, unsigned int format, void* pixels, int pitch);
static const MosaicHost* host;
static char steps[1024];
static int ticks = 0, step_count = 0, done = 0, key_up_at = 0;
static int ambient_wanted = 0;

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
	event.key.scancode = SCANCODE_B;
	pushEvent(&event);
}

static MosaicRect probe(void) {
	return (MosaicRect){ 0, host->screen_height() / 2, SQUARE, SQUARE };
}

// the point read, inside the square
static int probeX(void) { return 4; }
static int probeY(void) { return host->screen_height() / 2 + 4; }

static const char* shown(void) {
	static char out[16];
	MosaicColor c;
	if (host->tint_shown(&c)) snprintf(out, sizeof(out), "%02X%02X%02X", c.r, c.g, c.b);
	else snprintf(out, sizeof(out), "off");
	return out;
}

static void probeShown(const char* label) {
	unsigned int pixel = 0;
	void* window = currentWindow ? currentWindow() : NULL;
	void* renderer = window && windowRenderer ? windowRenderer(window) : NULL;
	Rect at = { probeX(), probeY(), 1, 1 };
	if (!renderer || !readPixels || readPixels(renderer, &at, PIXELFORMAT_ARGB8888, &pixel, 4) != 0) {
		say("probe %s can't read the renderer", label);
		return;
	}
	say("probe %s on %s %06X shown %s", label, host->current_screen(), pixel & 0xFFFFFF, shown());
}

static void probeCapture(const char* label) {
	MosaicSurface* s = host->capture_screen();
	MosaicColor c = { 0, 0, 0, 0 };
	if (!s || host->read_pixel(s, probeX(), probeY(), &c) != 0) {
		say("capture %s null", label);
		if (s) host->surface_free(ID, s);
		return;
	}
	host->surface_free(ID, s);
	say("capture %s on %s %02X%02X%02X", label, host->current_screen(), c.r, c.g, c.b);
}

static void save(void) {
	char path[512] = "";
	int rc = host->save_screenshot(path, sizeof(path));
	say("save %i at %i %i path %s", rc, probeX(), probeY(), rc == 0 ? path : "-");
}

static void setTime(const char* seconds) {
	const char* path = getenv("MOSAIC_BLUE_LIGHT_FAKE");
	FILE* f = path ? fopen(path, "w") : NULL;
	if (!f) {
		say("can't write the fake clock");
		return;
	}
	fprintf(f, "%s\n", seconds);
	fclose(f);
	say("time %s", seconds);
}

// The i-th step, from 0, into out; 0 past the last.
static int stepAt(int i, char* out, size_t size) {
	const char* p = steps;
	for (; i > 0 && p; i--) {
		p = strchr(p, ',');
		if (p) p++;
	}
	if (!p || !*p) return 0;
	size_t len = strcspn(p, ",");
	if (len >= size) len = size - 1;
	memcpy(out, p, len);
	out[len] = '\0';
	return 1;
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	if (key_up_at && ticks >= key_up_at) {
		key(0);
		key_up_at = 0;
	}
	if (done || ticks < 40 || (ticks - 40) % 20) return;
	int at = (ticks - 40) / 20;
	char step[128];
	if (at < step_count && stepAt(at, step, sizeof(step))) {
		if (!strncmp(step, "action:", 7)) say("run %s %i", step + 7, host->run_action(step + 7, NULL));
		else if (!strcmp(step, "quick")) say("quick %i", host->run_action("gamelist.quick_menu", NULL));
		else if (!strcmp(step, "b")) {
			key(1);
			key_up_at = ticks + 3;
		}
		else if (!strcmp(step, "screen")) say("open %i", host->open_screen(SCREEN_ID));
		else if (!strcmp(step, "close")) say("close %i", host->close_screen(SCREEN_ID));
		else if (!strcmp(step, "ambient")) ambient_wanted = 1;
		else if (!strcmp(step, "unambient")) {
			ambient_wanted = 0;
			say("close ambient %i", host->close_screen(AMBIENT_ID));
		}
		else if (!strncmp(step, "time:", 5)) setTime(step + 5);
		else if (!strncmp(step, "probe:", 6)) probeShown(step + 6);
		else if (!strncmp(step, "capture:", 8)) probeCapture(step + 8);
		else if (!strcmp(step, "save")) save();
	}
	else if (at >= step_count + 2) {
		done = 1;
		say("done");
	}
}

static int overlayRender(MosaicSurface* overlay, const MosaicScreenView* view) {
	(void)view;
	MosaicRect square = probe();
	return host->fill_rect(overlay, &square, mosaic_color(WHITE, 255)) == 0 ? 0 : 1;
}

static int screenRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)view;
	(void)data;
	MosaicRect square = probe();
	host->fill_rect(screen, &square, mosaic_color(WHITE, 255));
	return 0;
}

static int ambientRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)view;
	(void)data;
	host->fill_rect(screen, NULL, mosaic_color(WHITE, 255));
	return 0;
}

static int wants(unsigned long idle_ms, void* data) {
	(void)idle_ms;
	(void)data;
	return ambient_wanted;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, tint_shown)) {
		say("no tint_shown");
		return 1;
	}
	snprintf(steps, sizeof(steps), "%s", host->cfg_get("steps", ""));
	step_count = steps[0] ? 1 : 0;
	for (const char* p = steps; *p; p++) {
		if (*p == ',') step_count++;
	}
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	currentWindow = (void* (*)(void))dlsym(RTLD_DEFAULT, "SDL_GL_GetCurrentWindow");
	windowRenderer = (void* (*)(void*))dlsym(RTLD_DEFAULT, "SDL_GetRenderer");
	readPixels = (int (*)(void*, const Rect*, unsigned int, void*, int))dlsym(RTLD_DEFAULT, "SDL_RenderReadPixels");
	if (!pushEvent || !currentWindow || !windowRenderer || !readPixels) say("missing an SDL call");

	MosaicScreenHooks hooks = { .size = sizeof(hooks), .render = screenRender };
	MosaicScreenHooks full = { .size = sizeof(full), .flags = MOSAIC_SCREEN_FULL, .render = ambientRender };
	if (host->register_screen(SCREEN_ID, &hooks) != 0 || host->register_screen(AMBIENT_ID, &full) != 0
			|| host->register_ambient(AMBIENT_ID, wants, NULL) != 0) {
		say("couldn't register the screens");
		return 1;
	}
	say("ready");
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
