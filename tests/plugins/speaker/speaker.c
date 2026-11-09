// A plugin that plays audio through the host's audio calls, run with SDL's
// dummy audio driver. Each step logs what the calls returned:
//
//   - before opening: every call is refused, and SDL's audio isn't started
//   - opening: bad formats refused, the same format again kept
//   - a thread of its own queues 0.8 s of a quiet square wave, then is
//     refused a NULL and more than the queue's cap
//   - the queue falls as it plays, and holds while paused
//   - closing: SDL's audio is quit again, and calls after it are refused
//   - audio_open, audio_pause and audio_close from its thread are refused
//   - opening in another format reopens the device
//   - a thread queues and counts while the main thread opens and closes
//   - with 0.6 s queued it taps the power button, so the launcher sleeps,
//     closing the device once that has played; a thread wakes it
//   - with pak=<path> it opens again and launches that pak, which closes
//     the device; it opens it again, and the launcher's quit closes it
//
// SDL_PushEvent (for the power button) and SDL_WasInit are found in the
// launcher with dlsym: a test-only shortcut, as plugins otherwise never
// touch SDL.
//
// With mode=decline it opens the device in init and declines to start;
// with mode=poweroff it opens it and powers off; with mode=launch it opens
// it and launches pak=<path> straight away, opening it again after, for
// drivers that open only one device, which stock's own takes after a sleep.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"

#define ID "speaker"
#define RATE 48000
#define CHUNK 1024
#define SDL_INIT_AUDIO 0x10u

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
#define POWER_SCANCODE 42 // backspace, the desktop build's power button

static int (*pushEvent)(Event* event);
static unsigned int (*wasInit)(unsigned int flags);

static const MosaicHost* host;
static const char* mode = "";
static const char* pak = "";
static short tone[CHUNK * 2];
static int stage = 0;
static unsigned long stage_at = 0;
static int q1 = 0, q3 = 0;
static int cycles = 0;
static int woke = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int sdlAudio(void) {
	return wasInit && (wasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) ? 1 : 0;
}

static void key(int down) {
	Event event;
	memset(&event, 0, sizeof(event));
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? 1 : 0;
	event.key.scancode = POWER_SCANCODE;
	if (pushEvent(&event) != 1) say("can't push a key event");
}

static void pause_ms(int ms) {
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

static void next(int to, unsigned long now) {
	stage = to;
	stage_at = now;
}

///////////////////////////////////////
// its own threads

// queues 0.8 s in chunks, then a NULL and past the cap
static pthread_t filler;
static int filled = 0, chunks = 0, null_result = 0, over_result = 0, filler_done = 0;
static void* fill(void* data) {
	(void)data;
	int want = RATE * 8 / 10 / CHUNK;
	for (int i = 0; i < want; i++) {
		if (host->audio_queue(ID, tone, CHUNK) == 0) filled++;
	}
	chunks = want;
	null_result = host->audio_queue(ID, NULL, 10);
	// half a second more, in chunks, goes past the cap
	for (int i = 0; i <= RATE / 2 / CHUNK && over_result == 0; i++) over_result = host->audio_queue(ID, tone, CHUNK);
	__atomic_store_n(&filler_done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

// the main-thread-only calls, from here
static pthread_t stranger;
static int off_open = 0, off_pause = 0, off_close = 0;
static void* offMain(void* data) {
	(void)data;
	off_open = host->audio_open(ID, RATE, 2);
	off_pause = host->audio_pause(ID, 1);
	off_close = host->audio_close(ID);
	return NULL;
}

// queues and counts for 400 ms while the main thread opens and closes
static pthread_t racer;
static int race_calls = 0, race_ok = 0, race_refused = 0, racer_done = 0;
static void* race(void* data) {
	(void)data;
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		int queued = host->audio_queued(ID);
		int result = queued >= 0 && queued < 22050 / 4 ? host->audio_queue(ID, tone, CHUNK / 2) : -1;
		race_calls++;
		if (result == 0) race_ok++;
		else race_refused++;
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000 < 400);
	__atomic_store_n(&racer_done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

// presses and releases power while the launcher sleeps
static pthread_t waker;
static int waking = 0;
static void* wake(void* data) {
	(void)data;
	pause_ms(700);
	key(1);
	pause_ms(100);
	key(0);
	return NULL;
}

///////////////////////////////////////
// hooks

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, audio_open)) {
		say("no audio_open");
		return 1;
	}
	mode = host->cfg_get(ID, "mode", "");
	pak = host->cfg_get(ID, "pak", "");
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	wasInit = (unsigned int (*)(unsigned int))dlsym(RTLD_DEFAULT, "SDL_WasInit");
	for (int i = 0; i < CHUNK; i++) {
		short v = (i / 50) % 2 ? 800 : -800; // 480 Hz, quiet
		tone[i * 2] = v;
		tone[i * 2 + 1] = v;
	}
	if (!strcmp(mode, "decline")) {
		say("open in init %i, declining", host->audio_open(ID, RATE, 2));
		return 1;
	}
	return 0;
}

static void tick(unsigned long now) {
	if (stage == 0) {
		if (!strcmp(mode, "poweroff")) {
			say("open %i, powering off", host->audio_open(ID, RATE, 2));
			host->power_off(0);
			stage = 100;
			return;
		}
		if (!strcmp(mode, "launch")) {
			int opened = host->audio_open(ID, RATE, 2);
			int launched = host->open_path(pak, MOSAIC_ENTRY_PAK);
			int queued = host->audio_queued(ID);
			say("open %i, open pak %i, after launch queued %i", opened, launched, queued);
			say("open after launch %i", host->audio_open(ID, RATE, 2));
			stage = 100;
			return;
		}
		say("sdl audio before open %i", sdlAudio());
		say("before open: queue %i, queued %i, pause %i, close %i", host->audio_queue(ID, tone, 10),
			host->audio_queued(ID), host->audio_pause(ID, 1), host->audio_close(ID));
		say("bad rate %i, bad channels %i, unknown id %i, max ms %i", host->audio_open(ID, 1000, 2),
			host->audio_open(ID, RATE, 3), host->audio_open("nobody", RATE, 2), host->audio_queue_max_ms);
		say("open %i", host->audio_open(ID, RATE, 2));
		say("open again %i, queued %i, empty queue %i, sdl audio %i", host->audio_open(ID, RATE, 2),
			host->audio_queued(ID), host->audio_queue(ID, NULL, 0), sdlAudio());
		pthread_create(&filler, NULL, fill, NULL);
		next(1, now);
	}
	else if (stage == 1) {
		if (!__atomic_load_n(&filler_done, __ATOMIC_SEQ_CST)) return;
		pthread_join(filler, NULL);
		q1 = host->audio_queued(ID);
		say("thread queued %i of %i chunks, null %i, over %i, queued %i", filled, chunks, null_result, over_result,
			q1 >= RATE * 7 / 10 ? 1 : 0);
		next(2, now);
	}
	else if (stage == 2) {
		if (now - stage_at < 300) return;
		int q2 = host->audio_queued(ID);
		say("queued fell %i", q2 < q1);
		say("pause %i", host->audio_pause(ID, 1));
		pause_ms(50); // let the device's buffer settle
		q3 = host->audio_queued(ID);
		next(3, now);
	}
	else if (stage == 3) {
		if (now - stage_at < 300) return;
		int q4 = host->audio_queued(ID);
		say("paused held %i", q3 > 0 && q4 == q3);
		say("unpause %i", host->audio_pause(ID, 0));
		next(4, now);
	}
	else if (stage == 4) {
		if (now - stage_at < 100) return;
		int q5 = host->audio_queued(ID);
		say("playing again %i", q5 < q3);
		int closed = host->audio_close(ID);
		say("close %i, again %i, queued after %i, queue after %i, sdl audio %i", closed, host->audio_close(ID),
			host->audio_queued(ID), host->audio_queue(ID, tone, 10), sdlAudio());
		pthread_create(&stranger, NULL, offMain, NULL);
		pthread_join(stranger, NULL);
		say("off main open %i, pause %i, close %i, queued %i", off_open, off_pause, off_close, host->audio_queued(ID));
		int a = host->audio_open(ID, 44100, 2);
		int b = host->audio_open(ID, 22050, 1);
		say("open %i, other format %i", a, b);
		pthread_create(&racer, NULL, race, NULL);
		next(5, now);
	}
	else if (stage == 5) {
		// closes and opens every frame while the racer runs
		if (!__atomic_load_n(&racer_done, __ATOMIC_SEQ_CST)) {
			if (cycles++ % 2 == 0) host->audio_close(ID);
			else host->audio_open(ID, 22050, 1);
			return;
		}
		pthread_join(racer, NULL);
		say("race cycles %i, calls %i, ok %i, refused %i", cycles > 4, race_calls > 0, race_ok > 0, race_refused > 0);
		host->audio_open(ID, RATE, 2);
		// 0.6 s, most of which has to play out before the sleep
		int queued = 0;
		for (int i = 0; i < RATE * 6 / 10 / CHUNK; i++) queued |= host->audio_queue(ID, tone, CHUNK);
		say("queued for sleep %i", queued);
		if (!pushEvent) {
			say("no SDL_PushEvent");
			next(100, now);
			return;
		}
		key(1);
		next(6, now);
	}
	else if (stage == 6) {
		if (now - stage_at < 100) return;
		key(0);
		next(7, now);
	}
	else if (stage == 7) {
		if (!woke) return;
		if (waking) pthread_join(waker, NULL);
		waking = 0;
		say("open after sleep %i", host->audio_open(ID, RATE, 2));
		next(8, now);
		if (!pak[0]) {
			say("done");
			return;
		}
		say("open pak %i", host->open_path(pak, MOSAIC_ENTRY_PAK));
		say("after launch queued %i", host->audio_queued(ID));
		say("open after launch %i", host->audio_open(ID, RATE, 2));
	}
}

static void beforeSleep(void) {
	if (stage < 6) return;
	say("before_sleep queued %i", host->audio_queued(ID) > 0);
	waking = pthread_create(&waker, NULL, wake, NULL) == 0;
}

static void afterResume(void) {
	if (stage < 6) return;
	// stock starts SDL's audio itself on waking (PWR_exitSleep), so it isn't
	// looked at from here on
	say("after_resume queued %i", host->audio_queued(ID));
	woke = 1;
}

static void quit(void) {
	if (waking) pthread_join(waker, NULL);
	say("quit queued %i", host->audio_queued(ID));
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.before_sleep = beforeSleep,
	.after_resume = afterResume,
	.quit = quit,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
