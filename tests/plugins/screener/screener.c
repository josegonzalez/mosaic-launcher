// A plugin with screens of its own. It registers four:
//
//   screener.main   input, enter, exit and render; B closes it, A opens a
//                   confirm dialog from its input
//   screener.bare   render only, so B closes it as the launcher's default
//   screener.full   MOSAIC_SCREEN_FULL, with no header or overlays
//   screener.fail   its render (fail=render) or input (fail=input) fails
//
// Each render fills the middle half of the screen blue, and each redraws
// every frame (animating). It binds the
// screener.open action, which opens screener.main, to Y on the game list.
// It owns the transition slot, picking no transition but logging the old
// and new frames' pixel at the probe (where tests/plugins/layerer draws its
// overlay), so a case can see what was on screen, overlays included.
//
// Its steps setting is a comma separated list run from tick, one every 20
// ticks from the 40th: y, a or b presses that button (the desktop build
// reads the keyboard through SDL, so it pushes key events with
// SDL_PushEvent, found with dlsym: a test-only shortcut), open:<id> and
// close:<id> call open_screen and close_screen, and confirm opens a
// confirm dialog. It logs what each returns, what current_screen says
// whenever it changes, and "done" 40 ticks after the last step.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "screener"

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

static int (*pushEvent)(Event* event);

static const MosaicHost* host;
static char steps[512];
static const char* fail = "";
static int ticks = 0;
static int step_count = 0;
static int key_up_at = 0, key_scancode = 0;
static char last_screen[80] = "";
static int done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

// "RRGGBBAA" at x, y on s, or "-"
static const char* pixel(const MosaicSurface* s, int x, int y) {
	static char out[4][12];
	static int next = 0;
	char* text = out[next++ % 4];
	MosaicColor c;
	if (host->read_pixel(s, x, y, &c) != 0) return "-";
	snprintf(text, 12, "%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
	return text;
}

// where layerer draws
static int probeX(void) { return 4; }
static int probeY(void) { return host->screen_height() / 2 + 4; }

static void key(int scancode, int down) {
	if (!pushEvent) return;
	Event event;
	memset(&event, 0, sizeof(event));
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? 1 : 0;
	event.key.scancode = scancode;
	if (pushEvent(&event) != 1) say("can't push a key event");
}

// Each screen's state, as its hooks' data.
typedef struct Screen {
	const char* id;
	int rendered; // since it opened
} Screen;
static Screen main_screen = { "screener.main", 0 };
static Screen bare_screen = { "screener.bare", 0 };
static Screen full_screen = { "screener.full", 0 };
static Screen fail_screen = { "screener.fail", 0 };

static int enter(void* data) {
	Screen* s = data;
	s->rendered = 0;
	say("enter %s over %s", s->id, host->current_screen());
	return 0;
}

static void leave(void* data) {
	Screen* s = data;
	say("exit %s", s->id);
	say("%s drew %s", s->id, s->rendered > 10 ? "every frame" : "once or a few times");
}

// Every frame, so a transition's old frame is the screen however the
// renderer buffers what it shows.
static int animating(void* data) {
	(void)data;
	return 1;
}

static void onYes(void* data) {
	(void)data;
	say("yes, now %s", host->current_screen());
}

static int input(const MosaicInput* in, void* data) {
	Screen* s = data;
	if (in->size != sizeof(MosaicInput)) say("input size %zu", in->size);
	if (in->pressed) say("input %s pressed %x", s->id, in->pressed);
	if (s == &fail_screen && !strcmp(fail, "input") && in->pressed) return -3;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A)) {
		host->confirm("Screener?", "Yes", onYes, NULL);
		return MOSAIC_SCREEN_STAY;
	}
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	return MOSAIC_SCREEN_STAY;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	Screen* s = data;
	int w = 0, h = 0;
	host->surface_size(screen, &w, &h);
	if (!s->rendered++) {
		// the status pill's middle, which the launcher drew first, against the clear top middle
		const char* status = view->status.w > 0
			? pixel(screen, view->status.x + view->status.w / 2, view->status.y + view->status.h / 2) : "-";
		const char* clear = pixel(screen, w / 2, 2);
		say("render %s %ix%i area %ix%i status %i drawn %i current %s open %i select %i", s->id, w, h, view->area.w,
			view->area.h, view->status.w, strcmp(status, "-") && strcmp(status, clear), view->screen,
			host->open_screen("screener.bare"), host->select_index(0));
	}
	MosaicRect middle = { 0, h / 4, w, h / 2 };
	host->fill_rect(screen, &middle, mosaic_color(0x0000FF, 255));
	if (s == &fail_screen && !strcmp(fail, "render")) return -2;
	return 0;
}

static void registerScreen(Screen* s, int flags, int with_input) {
	MosaicScreenHooks hooks = { sizeof(hooks), s, flags, enter, leave, with_input ? input : NULL, render, animating };
	if (host->register_screen(s->id, &hooks) != 0) say("can't register %s", s->id);
}

static int canOpen(void* data) { (void)data; return 1; }
static void openMain(void* data) {
	(void)data;
	say("action open %i", host->open_screen("screener.main"));
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, open_screen)) {
		say("no open_screen");
		return 1;
	}
	snprintf(steps, sizeof(steps), "%s", host->cfg_get(ID, "steps", ""));
	for (const char* p = steps; *p; p++) {
		if (*p == ',' || p == steps) step_count++;
	}
	fail = host->cfg_get(ID, "fail", "");
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	if (!pushEvent) say("no SDL_PushEvent");

	registerScreen(&main_screen, 0, 1);
	registerScreen(&bare_screen, 0, 0);
	registerScreen(&full_screen, MOSAIC_SCREEN_FULL, 1);
	registerScreen(&fail_screen, 0, 1);
	MosaicScreenHooks good = { sizeof(good), NULL, 0, NULL, NULL, NULL, render, NULL };
	MosaicScreenHooks none = { sizeof(none), NULL, 0, NULL, NULL, NULL, NULL, NULL };
	say("refused dup %i builtin %i empty %i no render %i open %i close %i",
		host->register_screen("screener.main", &good), host->register_screen("gamelist", &good),
		host->register_screen("", &good), host->register_screen("screener.none", &none),
		host->open_screen("screener.main"), host->close_screen("screener.main"));
	host->register_action("screener.open", "Open", canOpen, openMain, NULL);
	host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "screener.open");
	return 0;
}

static int scancode(const char* name) {
	if (!strcmp(name, "a")) return 22; // S
	if (!strcmp(name, "b")) return 4;  // A
	if (!strcmp(name, "y")) return 20; // Q
	return 0;
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

static void runStep(const char* step) {
	if (!strncmp(step, "open:", 5)) say("open %s %i", step + 5, host->open_screen(step + 5));
	else if (!strncmp(step, "close:", 6)) say("close %s %i", step + 6, host->close_screen(step + 6));
	else if (!strcmp(step, "confirm")) host->confirm("Screener?", "Yes", onYes, NULL);
	else if (!strcmp(step, "register")) {
		MosaicScreenHooks good = { sizeof(good), NULL, 0, NULL, NULL, NULL, render, NULL };
		say("register from tick %i", host->register_screen("screener.late", &good));
	}
	else if (scancode(step)) {
		key_scancode = scancode(step);
		key(key_scancode, 1);
		key_up_at = ticks + 3;
	}
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	const char* current = host->current_screen();
	if (strcmp(current, last_screen)) {
		snprintf(last_screen, sizeof(last_screen), "%s", current);
		say("now %s", current);
	}
	if (key_up_at && ticks >= key_up_at) {
		key(key_scancode, 0);
		key_up_at = 0;
	}
	if (done || ticks < 40 || (ticks - 40) % 20) return;
	int at = (ticks - 40) / 20;
	char step[80];
	if (at < step_count && stepAt(at, step, sizeof(step))) runStep(step);
	else if (at >= step_count + 2) {
		done = 1;
		say("done");
	}
}

static int transitionBegin(MosaicTransition* t) {
	say("transition %s old %s new %s", t->kind == MOSAIC_TRANSITION_PUSH ? "push" : t->kind == MOSAIC_TRANSITION_POP ? "pop" : "other",
		pixel(t->old_frame, probeX(), probeY()), pixel(t->new_frame, probeX(), probeY()));
	return MOSAIC_STYLE_OFF;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.transition_begin = transitionBegin,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
