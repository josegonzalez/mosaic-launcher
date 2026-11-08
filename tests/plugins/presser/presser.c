// Not a plugin: a library run.sh preloads into the desktop build (through
// desktop-smoke.sh's SMOKE_PRELOAD) to drive screens that load no plugins,
// such as the Launcher pak's settings screen, and to read back what they
// draw. It wraps SDL_PollEvent to press buttons, and SDL_ttf's text
// rendering to write down each piece of text drawn.
//
//   PRESSER_KEYS  buttons to press, one at a time, separated by spaces: a,
//                 b, x, y, start, up, down, left, right, or wait (a press of
//                 nothing)
//   PRESSER_LOG   the file each drawn text is added to, one per line
//   PRESSER_STEP  milliseconds between presses (300 by default); the first
//                 comes a second after the first poll
//
// After the last press and another second, it prints "presser: done" on
// stderr, which lands in the launcher's log.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

typedef struct {
	unsigned char r, g, b, a;
} Color;
typedef struct Font Font;

static const struct {
	const char* name;
	int scancode; // the desktop build's key for it
} keys[] = {
	{ "a", 22 },     // S
	{ "b", 4 },      // A
	{ "x", 26 },     // W
	{ "y", 20 },     // Q
	{ "start", 40 }, // return
	{ "up", 82 },
	{ "down", 81 },
	{ "left", 80 },
	{ "right", 79 },
	{ "wait", 0 },
};

#define PRESSES_MAX 256
static int presses[PRESSES_MAX];
static int press_count = -1; // -1 until PRESSER_KEYS is read
static int next_press = 0;
static int key_down = 0; // the scancode held, or 0
static unsigned long started = 0, down_at = 0, last_at = 0;
static unsigned long step = 300;
static int finished = 0;

static unsigned long nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)ts.tv_sec * 1000 + (unsigned long)ts.tv_nsec / 1000000;
}

static void readKeys(void) {
	press_count = 0;
	const char* s = getenv("PRESSER_STEP");
	if (s && atoi(s) > 0) step = (unsigned long)atoi(s);
	const char* list = getenv("PRESSER_KEYS");
	if (!list) return;
	char copy[2048];
	snprintf(copy, sizeof(copy), "%s", list);
	for (char* word = strtok(copy, " \t\n"); word && press_count < PRESSES_MAX; word = strtok(NULL, " \t\n")) {
		int found = -1;
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			if (!strcmp(keys[i].name, word)) found = keys[i].scancode;
		}
		if (found < 0) fprintf(stderr, "presser: unknown key %s\n", word);
		else presses[press_count++] = found;
	}
}

static int keyEvent(void* out, int scancode, int down) {
	Event e;
	memset(&e, 0, sizeof(e));
	e.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.state = down ? 1 : 0;
	e.key.scancode = scancode;
	memcpy(out, &e, sizeof(e));
	return 1;
}

// A scripted event when one is due: returns 1 and fills out.
static int scripted(void* out) {
	if (!out) return 0;
	if (press_count < 0) readKeys();
	unsigned long now = nowMs();
	if (!started) {
		started = now;
		last_at = now + 1000 - step; // the first press a second in
	}
	if (key_down && now - down_at >= 60) {
		int code = key_down;
		key_down = 0;
		return keyEvent(out, code, 0);
	}
	if (key_down || next_press >= press_count || now - last_at < step) {
		if (!finished && !key_down && next_press >= press_count && now - last_at >= 1000) {
			finished = 1;
			fprintf(stderr, "presser: done\n");
		}
		return 0;
	}
	int code = presses[next_press++];
	last_at = now;
	if (!code) return 0; // wait
	key_down = code;
	down_at = now;
	return keyEvent(out, code, 1);
}

static void noteText(const char* text) {
	const char* path = getenv("PRESSER_LOG");
	if (!path || !text) return;
	FILE* f = fopen(path, "a");
	if (!f) return;
	fprintf(f, "%s\n", text);
	fclose(f);
}

#ifdef __APPLE__
// dyld's interposing: calls from the launcher go to the wrappers, and the
// wrappers' own calls reach SDL.
int SDL_PollEvent(void* event);
void* TTF_RenderUTF8_Blended(Font* font, const char* text, Color fg);
void* TTF_RenderUTF8_Blended_Wrapped(Font* font, const char* text, Color fg, unsigned int wrap);

static int pollEvent(void* event) {
	if (scripted(event)) return 1;
	return SDL_PollEvent(event);
}
static void* renderBlended(Font* font, const char* text, Color fg) {
	noteText(text);
	return TTF_RenderUTF8_Blended(font, text, fg);
}
static void* renderWrapped(Font* font, const char* text, Color fg, unsigned int wrap) {
	noteText(text);
	return TTF_RenderUTF8_Blended_Wrapped(font, text, fg, wrap);
}

#define INTERPOSE(replacement, replacee) \
	__attribute__((used)) static const struct { const void* a; const void* b; } interpose_##replacee \
	__attribute__((section("__DATA,__interpose"))) = { (const void*)&replacement, (const void*)&replacee }
INTERPOSE(pollEvent, SDL_PollEvent);
INTERPOSE(renderBlended, TTF_RenderUTF8_Blended);
INTERPOSE(renderWrapped, TTF_RenderUTF8_Blended_Wrapped);
#else
// LD_PRELOAD: these come before SDL's, which they find with RTLD_NEXT.
int SDL_PollEvent(void* event) {
	static int (*real)(void*);
	if (!real) real = (int (*)(void*))dlsym(RTLD_NEXT, "SDL_PollEvent");
	if (scripted(event)) return 1;
	return real ? real(event) : 0;
}
void* TTF_RenderUTF8_Blended(Font* font, const char* text, Color fg) {
	static void* (*real)(Font*, const char*, Color);
	if (!real) real = (void* (*)(Font*, const char*, Color))dlsym(RTLD_NEXT, "TTF_RenderUTF8_Blended");
	noteText(text);
	return real ? real(font, text, fg) : NULL;
}
void* TTF_RenderUTF8_Blended_Wrapped(Font* font, const char* text, Color fg, unsigned int wrap) {
	static void* (*real)(Font*, const char*, Color, unsigned int);
	if (!real) real = (void* (*)(Font*, const char*, Color, unsigned int))dlsym(RTLD_NEXT, "TTF_RenderUTF8_Blended_Wrapped");
	noteText(text);
	return real ? real(font, text, fg, wrap) : NULL;
}
#endif
