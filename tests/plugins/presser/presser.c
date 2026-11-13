// Not a plugin: a library run.sh preloads into the desktop build (through
// desktop-smoke.sh's SMOKE_PRELOAD) to drive screens that load no plugins,
// such as the Launcher pak's settings screen, and to read back what they
// draw. It wraps SDL_PollEvent to press buttons, SDL_ttf's text rendering
// to write down each piece of text drawn, and SDL_RenderPresent to save the
// frames shown.
//
//   PRESSER_KEYS  buttons to press, one at a time, separated by spaces: a,
//                 b, x, y, start, up, down, left, right, l1, r1, select,
//                 menu, or wait (a press of nothing). Buttons joined by +
//                 are pressed together, and /<ms> after them holds them
//                 that long (60 by default), eg. select+start/1500
//   PRESSER_LOG   the file each drawn text is added to, one per line
//   PRESSER_STEP  milliseconds between presses (300 by default); the first
//                 comes a second after the first poll
//   PRESSER_FRAME a path without an extension: each frame presented before
//                 "presser: done" replaces <path>.bmp (32 bits, top row
//                 first) and <path>.txt (the texts drawn for it, one per
//                 line as "rrggbb text"), so they end up as the last one
//   PRESSER_SHOT  milliseconds: with PRESSER_FRAME, the first frame presented
//                 this long after the last press is the last one saved, so
//                 the files end up as the screen was then, such as partway
//                 through an animation
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
	{ "l1", 43 },     // tab
	{ "r1", 5 },      // B
	{ "select", 53 }, // `
	{ "menu", 44 },   // space
	{ "wait", 0 },
};

#define PRESSES_MAX 256
#define CHORD_MAX 4
typedef struct {
	int codes[CHORD_MAX]; // the desktop build's keys, none for a wait
	int count;
	unsigned long hold; // milliseconds
} Press;
static Press presses[PRESSES_MAX];
static int press_count = -1; // -1 until PRESSER_KEYS is read
static int next_press = 0;
static const Press* held = NULL; // the press going on, or NULL
static int downs = 0, ups = 0; // its keys sent down and up so far
static unsigned long started = 0, down_at = 0, last_at = 0;
static unsigned long step = 300;
static int finished = 0;
static unsigned long pressed_last = 0; // when the last press went down, or 0
static int kept = 0; // PRESSER_SHOT's frame is saved

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
	char* words = NULL;
	for (char* word = strtok_r(copy, " \t\n", &words); word && press_count < PRESSES_MAX;
			word = strtok_r(NULL, " \t\n", &words)) {
		Press press = { .count = 0, .hold = 60 };
		char* hold = strchr(word, '/');
		if (hold) {
			*hold++ = '\0';
			if (atol(hold) > 0) press.hold = (unsigned long)atol(hold);
		}
		int ok = 1;
		char* names = NULL;
		for (char* name = strtok_r(word, "+", &names); name; name = strtok_r(NULL, "+", &names)) {
			int found = -1;
			for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
				if (!strcmp(keys[i].name, name)) found = keys[i].scancode;
			}
			if (found < 0 || press.count >= CHORD_MAX) {
				fprintf(stderr, "presser: unknown key %s\n", name);
				ok = 0;
			} else if (found) {
				press.codes[press.count++] = found;
			}
		}
		if (ok) presses[press_count++] = press;
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
	if (held && downs < held->count) return keyEvent(out, held->codes[downs++], 1);
	if (held && now - down_at >= held->hold) {
		int code = held->codes[ups++];
		if (ups >= held->count) held = NULL;
		return keyEvent(out, code, 0);
	}
	if (held || next_press >= press_count || now - last_at < step) {
		if (!finished && !held && next_press >= press_count && now - last_at >= 1000) {
			finished = 1;
			fprintf(stderr, "presser: done\n");
		}
		return 0;
	}
	const Press* press = &presses[next_press++];
	last_at = now;
	if (next_press >= press_count) pressed_last = now;
	if (!press->count) return 0; // wait
	held = press;
	downs = 1;
	ups = 0;
	down_at = now;
	return keyEvent(out, press->codes[0], 1);
}

// the texts drawn since the last frame was presented, for PRESSER_FRAME
static char frame_texts[16384];
static size_t frame_texts_len = 0;

static void noteText(const char* text, Color fg) {
	if (!text) return;
	if (getenv("PRESSER_FRAME")) {
		int n = snprintf(frame_texts + frame_texts_len, sizeof(frame_texts) - frame_texts_len, "%02x%02x%02x %s\n",
			fg.r, fg.g, fg.b, text);
		if (n > 0 && frame_texts_len + (size_t)n < sizeof(frame_texts)) frame_texts_len += (size_t)n;
	}
	const char* path = getenv("PRESSER_LOG");
	if (!path) return;
	FILE* f = fopen(path, "a");
	if (!f) return;
	fprintf(f, "%s\n", text);
	fclose(f);
}

typedef struct Renderer Renderer;
int SDL_GetRendererOutputSize(Renderer* renderer, int* w, int* h);
int SDL_RenderReadPixels(Renderer* renderer, const void* rect, unsigned int format, void* pixels, int pitch);
void SDL_RenderPresent(Renderer* renderer);
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u

static void put32(unsigned char* p, unsigned int v) {
	p[0] = v & 255;
	p[1] = v >> 8 & 255;
	p[2] = v >> 16 & 255;
	p[3] = v >> 24 & 255;
}

// Saves the frame about to be presented, and the texts drawn for it.
static void saveFrame(Renderer* renderer) {
	const char* prefix = getenv("PRESSER_FRAME");
	size_t texts_len = frame_texts_len;
	frame_texts_len = 0;
	if (!prefix || !renderer || finished || kept) return;
	const char* shot = getenv("PRESSER_SHOT");
	// each frame replaces the last until one comes that long after the last press
	if (shot && *shot && press_count >= 0 && next_press >= press_count && pressed_last
			&& nowMs() - pressed_last >= (unsigned long)atol(shot))
		kept = 1;
	int w = 0, h = 0;
	if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0 || w <= 0 || h <= 0) return;
	unsigned char* pixels = malloc((size_t)w * h * 4);
	if (!pixels) return;
	char path[1024];
	if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, w * 4) == 0) {
		snprintf(path, sizeof(path), "%s.bmp", prefix);
		FILE* f = fopen(path, "wb");
		if (f) {
			// a negative height puts the top row first
			unsigned char head[54] = { 'B', 'M' };
			put32(head + 2, 54 + (unsigned int)(w * h * 4));
			put32(head + 10, 54);
			put32(head + 14, 40);
			put32(head + 18, (unsigned int)w);
			put32(head + 22, (unsigned int)-h);
			head[26] = 1;
			head[28] = 32;
			fwrite(head, 1, sizeof(head), f);
			fwrite(pixels, 1, (size_t)w * h * 4, f);
			fclose(f);
		}
	}
	free(pixels);
	snprintf(path, sizeof(path), "%s.txt", prefix);
	FILE* f = fopen(path, "w");
	if (!f) return;
	fwrite(frame_texts, 1, texts_len, f);
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
	noteText(text, fg);
	return TTF_RenderUTF8_Blended(font, text, fg);
}
static void* renderWrapped(Font* font, const char* text, Color fg, unsigned int wrap) {
	noteText(text, fg);
	return TTF_RenderUTF8_Blended_Wrapped(font, text, fg, wrap);
}

static void renderPresent(Renderer* renderer) {
	saveFrame(renderer);
	SDL_RenderPresent(renderer);
}

#define INTERPOSE(replacement, replacee) \
	__attribute__((used)) static const struct { const void* a; const void* b; } interpose_##replacee \
	__attribute__((section("__DATA,__interpose"))) = { (const void*)&replacement, (const void*)&replacee }
INTERPOSE(pollEvent, SDL_PollEvent);
INTERPOSE(renderBlended, TTF_RenderUTF8_Blended);
INTERPOSE(renderWrapped, TTF_RenderUTF8_Blended_Wrapped);
INTERPOSE(renderPresent, SDL_RenderPresent);
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
	noteText(text, fg);
	return real ? real(font, text, fg) : NULL;
}
void* TTF_RenderUTF8_Blended_Wrapped(Font* font, const char* text, Color fg, unsigned int wrap) {
	static void* (*real)(Font*, const char*, Color, unsigned int);
	if (!real) real = (void* (*)(Font*, const char*, Color, unsigned int))dlsym(RTLD_NEXT, "TTF_RenderUTF8_Blended_Wrapped");
	noteText(text, fg);
	return real ? real(font, text, fg, wrap) : NULL;
}
void SDL_RenderPresent(Renderer* renderer) {
	static void (*real)(Renderer*);
	if (!real) real = (void (*)(Renderer*))dlsym(RTLD_NEXT, "SDL_RenderPresent");
	saveFrame(renderer);
	if (real) real(renderer);
}
#endif
