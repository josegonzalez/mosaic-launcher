// A plugin that reads the bundled transitions plugin's frames back. It adds
// a <sd>/Wiper folder to the root holding Red and an Inner folder holding
// Blue, and owns the background slot, filling the screen red in Wiper and
// blue in Inner, so each frame of the push from Wiper into Inner, and of
// the pop back out, says where the old and new frames are.
//
// It owns the transition slot too, and hands each transition to the
// transitions plugin, loaded alongside it without the slot: lib in its
// config is that plugin's library, which the launcher has already loaded
// and started, so it's the same copy. After each of its frames it logs
// the pixels at the left, middle and right of the screen, 5/8 of the way
// down, below the rows, for the first frame, the first at progress 0.5 or
// more, and the last. With no lib, or none loaded, it turns transitions
// off.
//
// From tick it opens Wiper, then Inner, and then presses B to back out of
// Inner into Wiper again, a pop. The desktop build reads the keyboard
// through SDL, so it pushes B's key events with SDL_PushEvent, found in the
// launcher with dlsym, as filterer does: a test-only shortcut.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "wiper"

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
#define B_SCANCODE 4 // the desktop build's key for B, A

static int (*pushEvent)(Event* event);

static const MosaicHost* host;
static const MosaicPlugin* inner_plugin;
static char folder[512];
static char inner[600];
static char inner_prefix[610];
static const char* lib = "";
static const char* to = "";
static const char* kind = "";
static int frames = 0, mid_logged = 0;
static int step = 0;
static long step_at = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
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

// "RRGGBBAA" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[8][12];
	static int next = 0;
	char* text = out[next++ % 8];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 12, "%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
	return text;
}

// the left, middle and right of s, 5/8 of the way down
static const char* probes(const MosaicSurface* s) {
	static char out[2][40];
	static int next = 0;
	char* text = out[next++ % 2];
	int w = 0, h = 0;
	host->surface_size(s, &w, &h);
	int y = h * 5 / 8;
	snprintf(text, 40, "%s %s %s", pixel(s, 2, y), pixel(s, w / 2, y), pixel(s, w - 3, y));
	return text;
}

static const char* kindName(int kind) {
	switch (kind) {
		case MOSAIC_TRANSITION_PUSH: return "push";
		case MOSAIC_TRANSITION_POP: return "pop";
		case MOSAIC_TRANSITION_SCREEN: return "screen";
		case MOSAIC_TRANSITION_LAUNCH: return "launch";
		default: return "?";
	}
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Wiper"));
}

static MosaicList* virtualDir(const char* path) {
	char file[700];
	MosaicList* list;
	if (!strcmp(path, folder)) {
		list = host->list_new();
		snprintf(file, sizeof(file), "%s/Red.gb", folder);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, "Red"));
		host->list_push(list, host->entry_new(inner, MOSAIC_ENTRY_DIR, "Inner"));
		return list;
	}
	if (!strcmp(path, inner)) {
		list = host->list_new();
		snprintf(file, sizeof(file), "%s/Blue.gb", inner);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, "Blue"));
		return list;
	}
	return NULL;
}

static int backgroundRender(MosaicSurface* screen, const MosaicEntry* selected) {
	const char* path = selected ? host->entry_path(selected) : NULL;
	if (!path) return 0;
	if (!strncmp(path, inner_prefix, strlen(inner_prefix))) return host->fill_rect(screen, NULL, mosaic_color(0x0000FF, 255));
	if (!strncmp(path, folder, strlen(folder)) && path[strlen(folder)] == '/')
		return host->fill_rect(screen, NULL, mosaic_color(0xFF0000, 255));
	return 0;
}

static int transitionBegin(MosaicTransition* t) {
	if (!inner_plugin && lib[0]) {
		void* handle = dlopen(lib, RTLD_NOW | RTLD_NOLOAD);
		const MosaicPlugin* (*get)(const MosaicHost*) =
			handle ? (const MosaicPlugin* (*)(const MosaicHost*))dlsym(handle, "mosaic_plugin_get") : NULL;
		if (get) inner_plugin = get(host);
	}
	if (!inner_plugin) {
		say("no transitions plugin loaded from \"%s\"", lib);
		return MOSAIC_STYLE_OFF;
	}
	const char* path = host->current_path();
	const char* slash = path ? strrchr(path, '/') : NULL;
	to = slash ? slash + 1 : "?";
	kind = kindName(t->kind);
	frames = 0;
	mid_logged = 0;
	int style = inner_plugin->transition_begin(t);
	say("begin %s to %s: style %i duration %i ease %i", kind, to, style, t->duration_ms, t->ease);
	say("frames %s to %s old %s new %s", kind, to, probes(t->old_frame), probes(t->new_frame));
	return style;
}

static int transitionFrame(MosaicSurface* screen, const MosaicTransition* t, float progress) {
	int rc = inner_plugin->transition_frame(screen, t, progress);
	frames++;
	if (frames == 1) say("first %s to %s progress %.3f: %s", kind, to, progress, probes(screen));
	if (!mid_logged && progress >= 0.5f && progress < 1.0f) {
		mid_logged = 1;
		say("mid %s to %s progress %.3f: %s", kind, to, progress, probes(screen));
	}
	if (progress >= 1.0f) say("last %s to %s progress %.3f frames %i: %s", kind, to, progress, frames, probes(screen));
	return rc;
}

static void key(int down) {
	Event event;
	memset(&event, 0, sizeof(event));
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? 1 : 0;
	event.key.scancode = B_SCANCODE;
	if (pushEvent(&event) != 1) say("can't push a key event");
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	host->request_redraw();
	long at = ms();
	if (step == 0) step_at = at;
	else if (step == 1 && at >= step_at + 300) say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
	// long enough for Wiper's red background to be on screen
	else if (step == 2 && at >= step_at + 300) say("open inner %i", host->open_path(inner, MOSAIC_ENTRY_DIR));
	else if (step == 3 && at >= step_at + 300) {
		if (pushEvent) key(1);
		else say("can't press B");
	}
	else if (step == 4 && at >= step_at + 100) {
		if (pushEvent) key(0);
	}
	else if (step == 5 && at >= step_at + 300) say("done at %s", host->current_path());
	else return;
	step++;
	step_at = at;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, plugin_art)) return 1;
	snprintf(folder, sizeof(folder), "%s/Wiper", host->sdcard_path);
	snprintf(inner, sizeof(inner), "%s/Inner", folder);
	snprintf(inner_prefix, sizeof(inner_prefix), "%s/", inner);
	lib = host->cfg_get(ID, "lib", "");
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
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
	.background_render = backgroundRender,
	.transition_begin = transitionBegin,
	.transition_frame = transitionFrame,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
