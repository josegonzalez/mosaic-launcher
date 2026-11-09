// A plugin that filters input, and presses buttons itself to see what the
// filter does to them. The desktop build reads the keyboard through SDL, so
// it pushes key events with SDL_PushEvent, found in the launcher with dlsym:
// a test-only shortcut, as plugins otherwise never touch SDL.
//
// At init it logs hold_switch, the new buttons' values, and what
// bind_button and override_button say to them. Each frame it checks its
// input_filter ran once before its tick. It adds two folders to the root,
// so the game list has rows to move through.
//
// Settings (in its config):
//   take=<buttons>  what input_filter consumes, comma separated: a, b, down,
//                   menu, power, or all; nothing by default
//   press=<button>  a button to press once things settle (a, b, down, menu
//                   or power), and log what came of it
//   hold=<ms>       how long to hold it before letting go; 0 holds it
//   confirm=1       open a confirm dialog before the press
//   fail=1          input_filter returns an error on its third call
//   wake=<ms>       first tap power, taking nothing, so the launcher sleeps,
//                   wake it from a thread, and press this long after the wake
//
// It logs "done: ..." a second and a half after the release (or the press,
// held), with the selection before and after, the rows the game list drew
// after the press, whether the dialog's yes ran, and its filter's counts.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "filterer"
#define STOCK_WAKE_MS 1000 // stock ignores power presses this soon after it starts or wakes

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

typedef struct {
	const char* name;
	int button;   // an enum MosaicButton
	int scancode; // the desktop build's key for it
} Button;
static const Button buttons[] = {
	{ "a", MOSAIC_BUTTON_A, 22 },       // S
	{ "b", MOSAIC_BUTTON_B, 4 },        // A
	{ "down", MOSAIC_BUTTON_DOWN, 81 }, // down arrow
	{ "menu", MOSAIC_BUTTON_MENU, 44 }, // space
	{ "power", MOSAIC_BUTTON_POWER, 42 }, // backspace
};
#define BUTTONS (int)(sizeof(buttons) / sizeof(buttons[0]))

static const MosaicHost* host;
static char folder_a[512], folder_b[512];
static unsigned int take = 0;
static const Button* press = NULL;
static unsigned long hold = 0;
static int confirm = 0, fail = 0;

static int calls = 0;    // input_filter calls
static int ticks = 0;
static int filtered = 0; // input_filter ran this frame
static int missed = 0, doubled = 0;
static int pressed_at_tick = 0;
static unsigned long pressed_at = 0, released_at = 0;
static int index_before = -1;
static int rows = 0, yes = 0, sleeps = 0;
static int done = 0;
static int settled = 0; // the press, if any, went in
static unsigned long last = 0; // the last tick's time

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static const Button* named(const char* name, int len) {
	for (int i = 0; i < BUTTONS; i++) {
		if ((int)strlen(buttons[i].name) == len && !strncmp(buttons[i].name, name, len)) return &buttons[i];
	}
	return NULL;
}

static unsigned int parseTake(const char* list) {
	if (!strcmp(list, "all")) return ~0u;
	unsigned int bits = 0;
	while (*list) {
		const char* end = strchr(list, ',');
		int len = end ? (int)(end - list) : (int)strlen(list);
		const Button* b = named(list, len);
		if (b) bits |= MOSAIC_BUTTON_BIT(b->button);
		list += len;
		if (*list == ',') list++;
	}
	return bits;
}

static int pushKey(int scancode, int down) {
	Event event;
	memset(&event, 0, sizeof(event));
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? 1 : 0;
	event.key.scancode = scancode;
	return pushEvent(&event) == 1;
}

static void key(int down) {
	if (!pushKey(press->scancode, down)) say("can't push a key event");
}

///////////////////////////////////////
// wake=<ms>: a power tap that sleeps, and the press after the wake

static long wake_ms = -1;
static int wake_ticks = 0, tap_up = 0, woken = 0, handed = 0;
static unsigned long tapped_at = 0, woke_at = 0;
static pthread_t waker;
static int waking = 0;

static void pause_ms(int ms) {
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

// presses and releases power while the launcher sleeps
static void* wake(void* data) {
	(void)data;
	pause_ms(300);
	pushKey(named("power", 5)->scancode, 1);
	pause_ms(100);
	pushKey(named("power", 5)->scancode, 0);
	return NULL;
}

// Runs tick's frames until wake_ms after the wake, then hands them back
// at the frame that presses.
static int beforeWake(unsigned long now) {
	if (wake_ms < 0 || handed || !press || !pushEvent) return 0;
	// the tick in the frame that slept has the time from before the sleep,
	// so the wake's time is the next frame's
	if (woken == 2) {
		woken = 3;
		woke_at = now;
	}
	if (woken == 3 && now - woke_at >= (unsigned long)wake_ms) {
		say("pressing %lu ms after the wake", now - woke_at);
		handed = 1;
		ticks = 39;
		return 0;
	}
	filtered = 0;
	if (woken == 1) woken = 2;
	if (woken) return 1;
	wake_ticks++;
	// stock ignores a power press in its first second
	if (!tapped_at && wake_ticks >= 40 && now >= 1500) {
		tapped_at = now;
		if (!pushKey(named("power", 5)->scancode, 1)) say("can't push a key event");
	}
	else if (tapped_at && !tap_up && now - tapped_at >= 100) {
		tap_up = 1;
		if (!pushKey(named("power", 5)->scancode, 0)) say("can't push a key event");
	}
	return 1;
}

static int noopAvailable(void* data) { (void)data; return 1; }
static void noopRun(void* data) { (void)data; }

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, hold_switch)) {
		say("no hold_switch");
		return 1;
	}
	snprintf(folder_a, sizeof(folder_a), "%s/Filterer A", host->sdcard_path);
	snprintf(folder_b, sizeof(folder_b), "%s/Filterer B", host->sdcard_path);
	take = parseTake(host->cfg_get(ID, "take", ""));
	const char* p = host->cfg_get(ID, "press", "");
	press = named(p, (int)strlen(p));
	hold = strtoul(host->cfg_get(ID, "hold", "0"), NULL, 10);
	confirm = atoi(host->cfg_get(ID, "confirm", "0"));
	fail = atoi(host->cfg_get(ID, "fail", "0"));
	const char* w = host->cfg_get(ID, "wake", "");
	if (*w) wake_ms = strtol(w, NULL, 10);
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	if (press && !pushEvent) say("no SDL_PushEvent");

	say("hold_switch %i", host->hold_switch());
	say("buttons MENU %i POWER %i FN1 %i FN2 %i FN3 %i", MOSAIC_BUTTON_MENU, MOSAIC_BUTTON_POWER,
		MOSAIC_BUTTON_FN1, MOSAIC_BUTTON_FN2, MOSAIC_BUTTON_FN3);
	host->register_action("filterer.noop", "Noop", noopAvailable, noopRun, NULL);
	say("bind MENU %i POWER %i FN1 %i FN3 %i UP %i",
		host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_MENU, MOSAIC_TRIGGER_PRESS, "filterer.noop"),
		host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_POWER, MOSAIC_TRIGGER_PRESS, "filterer.noop"),
		host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_FN1, MOSAIC_TRIGGER_PRESS, "filterer.noop"),
		host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_FN3, MOSAIC_TRIGGER_RELEASE, "filterer.noop"),
		host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_UP, MOSAIC_TRIGGER_PRESS, "filterer.noop"));
	say("override MENU %i POWER %i FN2 %i",
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_MENU, MOSAIC_TRIGGER_PRESS, "filterer.noop"),
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_POWER, MOSAIC_TRIGGER_PRESS, NULL),
		host->override_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_FN2, MOSAIC_TRIGGER_REPEAT, "filterer.noop"));
	return 0;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder_a, MOSAIC_ENTRY_DIR, "Filterer A"));
	host->list_push(root, host->entry_new(folder_b, MOSAIC_ENTRY_DIR, "Filterer B"));
}

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	calls++;
	if (filtered) doubled++;
	filtered = 1;
	if (input->size != sizeof(MosaicInput)) say("input size %zu", input->size);
	if (input->pressed) say("filter pressed %x held %x", input->pressed, input->held);
	if (input->released) say("filter released %x", input->released);
	if (fail && calls == 3) return -1;
	*consumed = wake_ms >= 0 && !handed ? 0 : take;
	return 0;
}

static void onYes(void* data) {
	(void)data;
	yes = 1;
}

static void tick(unsigned long now) {
	if (beforeWake(now)) return;
	ticks++;
	last = now;
	if (!filtered) missed++;
	filtered = 0;
	if (done) return;
	if (ticks == 30 && confirm) host->confirm("Filter?", "Yes", onYes, NULL);
	// things have settled by frame 40, and stock ignores a power press in
	// its first second, as the press that woke it, so the press also waits
	// for that to pass however quickly the frames came
	if (!settled && ticks >= 40 && now >= STOCK_WAKE_MS) {
		settled = 1;
		index_before = host->selected_index();
		if (press && pushEvent) {
			pressed_at_tick = ticks;
			pressed_at = now ? now : 1;
			key(1);
		}
	}
	if (pressed_at && !released_at && hold && now - pressed_at >= hold) {
		released_at = now ? now : 1;
		key(0);
	}
	unsigned long from = released_at ? released_at : pressed_at;
	int finished = press ? from && now - from >= 1500 : ticks >= 100;
	if (!finished) return;
	done = 1;
	say("done: index %i to %i, rows %i, yes %i, calls %i, missed %i, doubled %i, sleeps %i",
		index_before, host->selected_index(), rows, yes, calls, missed, doubled, sleeps);
}

static int decorateRow(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row) {
	(void)screen;
	(void)entry;
	(void)row;
	if (pressed_at_tick && !done) rows++;
	return 0;
}

static void beforeSleep(void) {
	sleeps++;
	say("before_sleep");
	if (wake_ms >= 0 && !woken && !waking) waking = pthread_create(&waker, NULL, wake, NULL) == 0;
}

static void afterResume(void) {
	if (waking) pthread_join(waker, NULL);
	waking = 0;
	if (wake_ms >= 0 && !woken) woken = 1;
}

static void beforePoweroff(void) {
	// a second, give or take a frame
	unsigned long held = pressed_at && !released_at ? last - pressed_at : 0;
	say("before_poweroff, held a second %i", held >= 950 && held < 2000);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.tick = tick,
	.before_sleep = beforeSleep,
	.after_resume = afterResume,
	.before_poweroff = beforePoweroff,
	.decorate_row = decorateRow,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
