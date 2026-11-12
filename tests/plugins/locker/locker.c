// Presses the bundled hold-lock's chord, SELECT and START, to lock, then
// down, then the chord again to unlock and down again, logging what the
// game list and its own input filter (after hold-lock's in load order)
// saw. Like filterer, it pushes key events with SDL_PushEvent, found in
// the launcher with dlsym: a test-only shortcut, as plugins otherwise
// never touch SDL. It adds two folders to the root, so down has a row to
// move to.
//
// Settings (in its config):
//   hold=<ms>  how long to hold the chord; 1300 by default
//   unlock=0   leave it locked, logging done a second after the first down
//
// It logs "done: index <before> <locked> <unlocked>, locked presses <n>,
// after presses <n>, leaked <n>": the selection before, after the down
// while locked and after the down once unlocked; the presses its filter saw
// between the chord's first release and its second press, and after the
// second release; and the SELECT and START releases it saw after the
// unlock.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "locker"

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

// the desktop build's keys
#define KEY_SELECT 53 // grave
#define KEY_START 40  // return
#define KEY_DOWN 81   // down arrow

enum Step {
	IDLE,
	CHORD_1,  // holding the chord to lock
	LOCKED,   // let go, waiting to press down
	DOWN_1,   // holding down while locked
	SETTLE_1, // let go, waiting to look at the selection
	CHORD_2,  // holding the chord to unlock
	UNLOCKED, // let go, waiting to press down
	DOWN_2,
	SETTLE_2,
	DONE,
};

static int (*pushEvent)(Event* event);
static const MosaicHost* host;
static char folder_a[512], folder_b[512];
static unsigned long hold = 1300;
static int unlock = 1;

static int step = IDLE;
static unsigned long at = 0; // when the step started
static int ticks = 0;
static int index0 = -1, index1 = -1, index2 = -1;
static int locked_presses = 0, after_presses = 0, leaked = 0;

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

static void chord(int down) {
	key(KEY_SELECT, down);
	key(KEY_START, down);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, hold_switch)) {
		say("no input filters");
		return 1;
	}
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	if (!pushEvent) {
		say("no SDL_PushEvent");
		return 1;
	}
	snprintf(folder_a, sizeof(folder_a), "%s/Locker A", host->sdcard_path);
	snprintf(folder_b, sizeof(folder_b), "%s/Locker B", host->sdcard_path);
	unsigned long ms = strtoul(host->cfg_get("hold", "1300"), NULL, 10);
	if (ms) hold = ms;
	unlock = atoi(host->cfg_get("unlock", "1"));
	return 0;
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder_a, MOSAIC_ENTRY_DIR, "Locker A"));
	host->list_push(root, host->entry_new(folder_b, MOSAIC_ENTRY_DIR, "Locker B"));
}

static int inputFilter(const MosaicInput* input, unsigned int* consumed) {
	(void)consumed;
	unsigned int ends = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_SELECT) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_START);
	if (step >= LOCKED && step < CHORD_2 && input->pressed) locked_presses++;
	if (step >= UNLOCKED && input->pressed) after_presses++;
	if (step >= UNLOCKED && (input->released & ends)) leaked++;
	return 0;
}

static void next(int to, unsigned long now) {
	step = to;
	at = now;
}

static void tick(unsigned long now) {
	ticks++;
	unsigned long since = now - at;
	switch (step) {
	case IDLE:
		if (ticks < 40) return;
		index0 = host->selected_index();
		chord(1);
		next(CHORD_1, now);
		break;
	case CHORD_1:
		if (since < hold) return;
		chord(0);
		next(LOCKED, now);
		break;
	case LOCKED:
		if (since < 300) return;
		key(KEY_DOWN, 1);
		next(DOWN_1, now);
		break;
	case DOWN_1:
		if (since < 100) return;
		key(KEY_DOWN, 0);
		next(SETTLE_1, now);
		break;
	case SETTLE_1:
		if (since < (unlock ? 400UL : 1000UL)) return;
		index1 = host->selected_index();
		if (!unlock) {
			next(DONE, now);
			break;
		}
		chord(1);
		next(CHORD_2, now);
		break;
	case CHORD_2:
		if (since < hold) return;
		chord(0);
		next(UNLOCKED, now);
		break;
	case UNLOCKED:
		if (since < 300) return;
		key(KEY_DOWN, 1);
		next(DOWN_2, now);
		break;
	case DOWN_2:
		if (since < 100) return;
		key(KEY_DOWN, 0);
		next(SETTLE_2, now);
		break;
	case SETTLE_2:
		if (since < 400) return;
		index2 = host->selected_index();
		next(DONE, now);
		break;
	default:
		return;
	}
	if (step == DONE) {
		say("done: index %i %i %i, locked presses %i, after presses %i, leaked %i",
			index0, index1, index2, locked_presses, after_presses, leaked);
	}
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.tick = tick,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
