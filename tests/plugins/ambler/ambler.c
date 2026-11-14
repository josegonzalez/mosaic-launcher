// Two ambient screens, ambler.first and ambler.second, that both want to
// show after a second idle, and lines and an X button on the launcher's
// lock screen. With the user's order putting ambler.second first, it
// waits for ambler.second to open, closes it and asks the launcher to
// lock, waits for the lock screen, presses X there (its ambler.ping
// action logs "pinged"), and waits for ambler.second to open over the lock
// screen once it's been idle a second. Like locker, it pushes key events
// with SDL_PushEvent, found in the launcher with dlsym: a test-only
// shortcut, as plugins otherwise never touch SDL.
//
// It logs "refusals: ..." from init, the calls it expects refused, each
// -1; "entered <screen>" as each of its screens opens; "lock <rc>, locked
// <n>" after asking to lock; and "done: <what happened>" at the end, or
// "stuck: ..." after 15 seconds.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "ambler"
#define FIRST ID ".first"
#define SECOND ID ".second"
#define THIRD ID ".third"

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
#define KEY_X 26 // the desktop build's X, W

enum Step { WAIT_SECOND, WAIT_LOCK, PRESS, RELEASE, WAIT_AGAIN, DONE };

static int (*pushEvent)(Event* event);
static const MosaicHost* host;
static int step = WAIT_SECOND;
static unsigned long at = 0, started = 0;
static int first_entered = 0, second_entered = 0, pinged = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[400];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void key(int scancode, int down) {
	Event event;
	memset(&event, 0, sizeof(event));
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? 1 : 0;
	event.key.scancode = scancode;
	if (pushEvent(&event) != 1) say("can't push a key event");
}

static int wants(unsigned long idle_ms, void* data) {
	(void)data;
	return idle_ms >= 1000;
}

static int enter(void* data) {
	const char* id = data;
	say("entered %s", id);
	if (!strcmp(id, FIRST)) first_entered++;
	if (!strcmp(id, SECOND)) second_entered++;
	return 0;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	return host->fill_rect(screen, &view->area, mosaic_color(0x000000, 255));
}

static int lockText(char* out, size_t size) {
	snprintf(out, size, "Ambler says hi\nsecond line");
	return 0;
}

static int always(const char* path, void* data) {
	(void)path;
	(void)data;
	return 1;
}

static void ping(const char* path, void* data) {
	(void)path;
	(void)data;
	pinged++;
	say("pinged on %s, locked %i", host->current_screen(), host->is_locked());
}

static int addScreen(const char* id, int flags) {
	MosaicScreenHooks hooks = {
		.size = sizeof(hooks),
		.data = (void*)id,
		.flags = flags,
		.enter = enter,
		.render = render,
	};
	return host->register_screen(id, &hooks);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, register_ambient)) {
		say("no ambient screens");
		return 1;
	}
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	if (!pushEvent) {
		say("no SDL_PushEvent");
		return 1;
	}
	if (addScreen(FIRST, MOSAIC_SCREEN_FULL) || addScreen(SECOND, MOSAIC_SCREEN_FULL) || addScreen(THIRD, 0)) {
		say("couldn't register the screens");
		return 1;
	}
	if (host->register_ambient(FIRST, wants, NULL) || host->register_ambient(SECOND, wants, NULL)) {
		say("couldn't make the screens ambient");
		return 1;
	}
	if (host->register_action(ID ".ping", "PING", 0, always, ping, NULL) || host->lock_button(MOSAIC_BUTTON_X, ID ".ping")) {
		say("couldn't put ping on the lock screen");
		return 1;
	}
	say("refusals: %i %i %i %i %i %i", host->register_ambient(THIRD, wants, NULL), host->register_ambient(SECOND, wants, NULL),
		host->register_ambient("lock", wants, NULL), host->lock_button(MOSAIC_BUTTON_SELECT, ID ".ping"),
		host->lock_button(MOSAIC_BUTTON_Y, ID ".missing"), host->lock_button(MOSAIC_BUTTON_X, ID ".ping"));
	return 0;
}

static void next(int to, unsigned long now) {
	step = to;
	at = now;
}

static void tick(unsigned long now) {
	if (!started) started = now;
	const char* screen = host->current_screen();
	unsigned long since = now - at;
	switch (step) {
	case WAIT_SECOND:
		if (strcmp(screen, SECOND)) break;
		if (host->close_screen(SECOND) != 0) say("couldn't close " SECOND);
		say("lock %i, locked %i", host->lock(), host->is_locked());
		next(WAIT_LOCK, now);
		break;
	case WAIT_LOCK:
		if (strcmp(screen, "lock") || since < 300) break;
		key(KEY_X, 1);
		next(RELEASE, now);
		break;
	case RELEASE:
		if (since < 100) break;
		key(KEY_X, 0);
		next(WAIT_AGAIN, now);
		break;
	case WAIT_AGAIN:
		if (strcmp(screen, SECOND)) break;
		say("done: %s over the lock, locked %i, pinged %i, entered first %i second %i", screen, host->is_locked(), pinged,
			first_entered, second_entered);
		next(DONE, now);
		break;
	default:
		break;
	}
	if (step != DONE && now - started > 15000) {
		say("stuck: step %i on %s, pinged %i, entered first %i second %i", step, screen, pinged, first_entered, second_entered);
		next(DONE, now);
	}
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.lock_text = lockText,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
