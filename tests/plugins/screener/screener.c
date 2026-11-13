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
// overlay), so a case can see what was on screen, overlays included. Its
// home setting, main or fail, marks that screen MOSAIC_SCREEN_HOME, for
// the home slot (slots=home in its plugin.txt), and its switcher setting,
// main, bare or fail, marks that one MOSAIC_SCREEN_GAME_SWITCHER, for the
// switcher slot (slots=switcher). With decline=1 the first enter of the
// switcher setting's screen declines.
//
// Its steps setting is a comma separated list run from tick, one every 20
// ticks from the 40th: y, a, b, select, left, right, up or down presses
// that button (the desktop build reads the keyboard through SDL, so it
// pushes key events with SDL_PushEvent, found with dlsym: a test-only
// shortcut), open:<id> and
// close:<id> call open_screen and close_screen, action:<id> calls
// run_action, pak:<path> opens a pak with open_path, confirm opens a
// confirm dialog, recents logs recents_count and each recent_info,
// play:<index>:<resume> calls recent_open and remove:<index>
// recent_remove. It logs what each
// returns, what current_screen says whenever it changes, what home_screen
// says from init and whenever it changes ("none" for NULL), and
// "done" 40 ticks after the last step.
//
// Its hints setting has every screen draw button hints with draw_hints
// over the blue: switcher draws the stock switcher's ("B BACK" on the
// left, "Y REMOVE  A RESUME" on the right), open "B BACK" and "A OPEN",
// back "B BACK" alone on the right, and names one hint on the left and
// two on the right, a different set each frame, for every button with a
// name. The first render with them logs what draw_hints returned, where
// the pills went, whether that's where stock puts its own, the pill's
// colour against the accent colour, and the calls it refuses; its
// overlay_render and tick log a call too, and each exit how many renders
// drew hints and how many had a group refused.

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
static char last_home[80] = "";
static int done = 0;
static const char* hints = "";
static int hinted = 0;        // renders that drew hints
static int hint_frame = 0;    // for names, which changes them each frame
static int hint_failures = 0; // renders where a group it drew was refused

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

static const char* switcher = "";
static int declines = 0;

static int enter(void* data) {
	Screen* s = data;
	s->rendered = 0;
	if (declines > 0 && !strcmp(s->id + strlen(ID "."), switcher)) {
		declines--;
		say("declined %s over %s", s->id, host->current_screen());
		return 1;
	}
	say("enter %s over %s", s->id, host->current_screen());
	return 0;
}

static void leave(void* data) {
	Screen* s = data;
	say("exit %s", s->id);
	say("%s drew %s", s->id, s->rendered > 10 ? "every frame" : "once or a few times");
	if (hints[0]) say("hints drawn %i refused %i", hinted, hint_failures);
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

static MosaicSurface* own = NULL;
static MosaicSurface* lent = NULL; // the last screen a render was lent, kept to try after

static const MosaicHint switcher_left[] = { { MOSAIC_BUTTON_B, "BACK" } };
static const MosaicHint switcher_right[] = { { MOSAIC_BUTTON_Y, "REMOVE" }, { MOSAIC_BUTTON_A, "RESUME" } };
static const MosaicHint open_right[] = { { MOSAIC_BUTTON_A, "OPEN" } };
// a hint on the left and two on the right, a set a frame
#define NAME_SETS 4
static const MosaicHint names[NAME_SETS][3] = {
	{ { MOSAIC_BUTTON_MENU, "INFO" }, { MOSAIC_BUTTON_START, "DONE" }, { MOSAIC_BUTTON_SELECT, "MORE" } },
	{ { MOSAIC_BUTTON_POWER, "SLEEP" }, { MOSAIC_BUTTON_L1, "PREV" }, { MOSAIC_BUTTON_R1, "NEXT" } },
	{ { MOSAIC_BUTTON_X, "SORT" }, { MOSAIC_BUTTON_L2, "UP" }, { MOSAIC_BUTTON_R2, "DOWN" } },
	{ { MOSAIC_BUTTON_Y, "FIND" }, { MOSAIC_BUTTON_B, "BACK" }, { MOSAIC_BUTTON_A, "PICK" } },
};
// a group wider than any screen
static const MosaicHint wide[] = {
	{ MOSAIC_BUTTON_A, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW" },
	{ MOSAIC_BUTTON_B, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW" },
};

// "x,y wxh"
static const char* rect(const MosaicRect* r) {
	static char out[2][40];
	static int next = 0;
	char* text = out[next++ % 2];
	snprintf(text, 40, "%i,%i %ix%i", r->x, r->y, r->w, r->h);
	return text;
}

// Draws the hints setting's, and the first time logs how it went.
static void drawHints(MosaicSurface* screen, const MosaicScreenView* view, int w, int h) {
	MosaicRect left = { 0, 0, 0, 0 }, right = { 0, 0, 0, 0 };
	int left_rc = 1, right_rc = 1; // 1 for a group not drawn
	// the background, in the bottom left corner no pill reaches
	char background[12];
	snprintf(background, sizeof(background), "%s", pixel(screen, 1, h - 2));
	if (!strcmp(hints, "switcher")) {
		left_rc = host->draw_hints(screen, MOSAIC_HINTS_LEFT, switcher_left, 1, &left);
		right_rc = host->draw_hints(screen, MOSAIC_HINTS_RIGHT, switcher_right, 2, &right);
	}
	else if (!strcmp(hints, "open")) {
		left_rc = host->draw_hints(screen, MOSAIC_HINTS_LEFT, switcher_left, 1, &left);
		right_rc = host->draw_hints(screen, MOSAIC_HINTS_RIGHT, open_right, 1, &right);
	}
	else if (!strcmp(hints, "back")) right_rc = host->draw_hints(screen, MOSAIC_HINTS_RIGHT, switcher_left, 1, &right);
	else if (!strcmp(hints, "names")) {
		const MosaicHint* set = names[hint_frame++ % NAME_SETS];
		left_rc = host->draw_hints(screen, MOSAIC_HINTS_LEFT, set, 1, &left);
		right_rc = host->draw_hints(screen, MOSAIC_HINTS_RIGHT, set + 1, 2, &right);
	}
	if (left_rc < 0 || right_rc < 0) hint_failures++;
	if (hinted++) return;

	// Where stock's GFX_blitButtonGroup puts them: in from the bottom
	// corners by stock's padding, as tall as its pills. The status pill,
	// when it shows, is in from the top right corner by the same padding.
	const MosaicRect* pill = left_rc == 0 ? &left : &right;
	int pad = h - pill->y - pill->h;
	int stock = pill->w > 0 && pad > 0
		&& (view->status.w == 0 || (view->status.y == pad && view->status.h == pill->h
			&& view->status.x + view->status.w == w - pad))
		&& (left_rc != 0 || (left.x == pad && left.y == pill->y && left.h == pill->h))
		&& (right_rc != 0 || (right.x + right.w == w - pad && right.y == pill->y && right.h == pill->h));
	// just inside its round left end, and just left of it
	char in[12];
	snprintf(in, sizeof(in), "%s", pixel(screen, pill->x + pill->h / 12, pill->y + pill->h / 2));
	const char* out = pixel(screen, pill->x - 2, pill->y + pill->h / 2);
	MosaicColor accent = mosaic_color(host->theme_color(MOSAIC_COLOR_ACCENT), 255);
	say("hints %s on %s left %i %s right %i %s stock %i pill %s accent %02X%02X%02X%02X outside clear %i", hints,
		view->screen, left_rc, rect(&left), right_rc, rect(&right), stock, in, accent.r, accent.g, accent.b, accent.a,
		!strcmp(out, background));

	// what it refuses
	MosaicHint unknown[] = { { 99, "UNKNOWN" } };
	MosaicHint fn[] = { { MOSAIC_BUTTON_FN1, "FN" } };
	MosaicHint unlabelled[] = { { MOSAIC_BUTTON_A, NULL } };
	MosaicHint empty[] = { { MOSAIC_BUTTON_A, "" } };
	MosaicHint three[] = { { MOSAIC_BUTTON_A, "A" }, { MOSAIC_BUTTON_B, "B" }, { MOSAIC_BUTTON_X, "X" } };
	if (!own) own = host->surface_new(ID, 64, 64);
	say("hints refused again %i wide %i own %i group %i none %i three %i unknown %i fn %i null %i empty %i",
		host->draw_hints(screen, pill == &left ? MOSAIC_HINTS_LEFT : MOSAIC_HINTS_RIGHT, switcher_left, 1, NULL),
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, wide, 2, NULL),
		host->draw_hints(own, MOSAIC_HINTS_LEFT, switcher_left, 1, NULL),
		host->draw_hints(screen, 2, switcher_left, 1, NULL),
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, switcher_left, 0, NULL),
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, three, 3, NULL),
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, unknown, 1, NULL),
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, fn, 1, NULL),
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, unlabelled, 1, NULL),
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, empty, 1, NULL));
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
	if (hints[0] && MOSAIC_HOST_HAS(host, draw_hints)) drawHints(screen, view, w, h);
	lent = screen;
	if (s == &fail_screen && !strcmp(fail, "render")) return -2;
	return 0;
}

static void registerScreen(Screen* s, int flags, int with_input) {
	MosaicScreenHooks hooks = { sizeof(hooks), s, flags, enter, leave, with_input ? input : NULL, render, animating };
	if (host->register_screen(s->id, &hooks) != 0) say("can't register %s", s->id);
}

static const char* homeName(void) {
	const char* home = host->home_screen();
	return home ? home : "none";
}

static int canOpen(const char* path, void* data) { (void)path; (void)data; return 1; }
static void openMain(const char* path, void* data) {
	(void)path;
	(void)data;
	say("action open %i", host->open_screen("screener.main"));
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, open_screen)) {
		say("no open_screen");
		return 1;
	}
	snprintf(steps, sizeof(steps), "%s", host->cfg_get("steps", ""));
	for (const char* p = steps; *p; p++) {
		if (*p == ',' || p == steps) step_count++;
	}
	fail = host->cfg_get("fail", "");
	hints = host->cfg_get("hints", "");
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	if (!pushEvent) say("no SDL_PushEvent");

	const char* home = host->cfg_get("home", "");
	switcher = host->cfg_get("switcher", "");
	declines = atoi(host->cfg_get("decline", "0"));
	int game_switcher = MOSAIC_SCREEN_GAME_SWITCHER;
	registerScreen(&main_screen, (!strcmp(home, "main") ? MOSAIC_SCREEN_HOME : 0) | (!strcmp(switcher, "main") ? game_switcher : 0), 1);
	registerScreen(&bare_screen, !strcmp(switcher, "bare") ? game_switcher : 0, 0);
	registerScreen(&full_screen, MOSAIC_SCREEN_FULL, 1);
	registerScreen(&fail_screen, (!strcmp(home, "fail") ? MOSAIC_SCREEN_HOME : 0) | (!strcmp(switcher, "fail") ? game_switcher : 0), 1);
	if (MOSAIC_HOST_HAS(host, recent_open)) {
		MosaicRecent recent = { .size = sizeof(recent) };
		say("recents from init info %i null %i open %i", host->recent_info(0, &recent), host->recent_info(0, NULL),
			host->recent_open(0, 1));
	}
	MosaicScreenHooks good = { sizeof(good), NULL, 0, NULL, NULL, NULL, render, NULL };
	MosaicScreenHooks none = { sizeof(none), NULL, 0, NULL, NULL, NULL, NULL, NULL };
	say("refused dup %i builtin %i empty %i no render %i open %i close %i",
		host->register_screen("screener.main", &good), host->register_screen("gamelist", &good),
		host->register_screen("", &good), host->register_screen("screener.none", &none),
		host->open_screen("screener.main"), host->close_screen("screener.main"));
	say("run from init %i", host->run_action("gamelist.quick_menu", NULL));
	if (MOSAIC_HOST_HAS(host, home_screen)) say("home_screen from init %s", homeName());
	host->register_action("screener.open", "Open", 0, canOpen, openMain, NULL);
	host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "screener.open");
	return 0;
}

static int scancode(const char* name) {
	if (!strcmp(name, "a")) return 22; // S
	if (!strcmp(name, "b")) return 4;  // A
	if (!strcmp(name, "y")) return 20; // Q
	if (!strcmp(name, "select")) return 53; // `
	if (!strcmp(name, "right")) return 79;
	if (!strcmp(name, "left")) return 80;
	if (!strcmp(name, "down")) return 81;
	if (!strcmp(name, "up")) return 82;
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
	else if (!strncmp(step, "action:", 7)) say("run %s %i", step + 7, host->run_action(step + 7, NULL));
	else if (!strncmp(step, "pak:", 4)) say("open pak %i", host->open_path(step + 4, MOSAIC_ENTRY_PAK));
	else if (!strcmp(step, "confirm")) host->confirm("Screener?", "Yes", onYes, NULL);
	else if (!strcmp(step, "recents")) {
		int count = host->recents_count();
		say("recents %i", count);
		for (int i = 0; i <= count; i++) {
			MosaicRecent recent = { .size = sizeof(recent) };
			int rc = host->recent_info(i, &recent);
			const char* preview = strrchr(recent.preview, '/');
			say("recent %i %i name %s type %i available %i resume %i preview %s", i, rc, recent.name, recent.type,
				recent.available, recent.can_resume, preview ? preview + 1 : "-");
		}
		// an older plugin's smaller struct gets only what fits
		struct { size_t size; char name[256]; int type; } old = { sizeof(old), "", -1 };
		host->recent_info(0, (MosaicRecent*)&old);
		say("old size %zu name %s type %i", old.size, old.name, old.type);
	}
	else if (!strncmp(step, "play:", 5)) {
		int index = atoi(step + 5);
		const char* colon = strchr(step + 5, ':');
		say("play %i %i", index, host->recent_open(index, colon ? atoi(colon + 1) : 1));
	}
	else if (!strncmp(step, "remove:", 7)) say("remove %s %i", step + 7, host->recent_remove(atoi(step + 7)));
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
	static int tried = 0;
	if (hints[0] && lent && !tried++) say("hints from tick %i", host->draw_hints(lent, MOSAIC_HINTS_LEFT, switcher_left, 1, NULL));
	const char* current = host->current_screen();
	if (strcmp(current, last_screen)) {
		snprintf(last_screen, sizeof(last_screen), "%s", current);
		say("now %s", current);
	}
	if (MOSAIC_HOST_HAS(host, home_screen) && strcmp(homeName(), last_home)) {
		snprintf(last_home, sizeof(last_home), "%s", homeName());
		say("home %s", last_home);
	}
	if (key_up_at && ticks >= key_up_at) {
		key(key_scancode, 0);
		key_up_at = 0;
	}
	if (done || ticks < 40 || (ticks - 40) % 20) return;
	int at = (ticks - 40) / 20;
	char step[512]; // as long as a pak's path
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

static int overlayRender(MosaicSurface* overlay, const MosaicScreenView* view) {
	static int tried = 0;
	if (hints[0] && !tried++) {
		say("hints on the overlay over %s %i", view->screen, host->draw_hints(overlay, MOSAIC_HINTS_LEFT, switcher_left, 1, NULL));
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.transition_begin = transitionBegin,
	.overlay_render = overlayRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
