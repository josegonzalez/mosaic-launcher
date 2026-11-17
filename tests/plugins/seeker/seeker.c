// Drives the bundled search plugin. Its steps setting is a comma separated
// list run from tick, one every 20 ticks from the 40th: action:<id> calls
// run_action, and a, b, y, l1, r1, start, select, up, down, left and right
// press that button, +<button> holds it down until -<button> lets it go,
// and any other word waits (the desktop build reads the keyboard through
// SDL, so it pushes key events with SDL_PushEvent, found with dlsym: a
// test-only shortcut). With its sections setting, a folder's full path, it orders
// that folder by the last word of each name, such as a year, and gives
// that word as the entry's section with section_key, logging once what
// entry_section answers when asked from inside it. It logs
// "screen <id>" whenever current_screen changes, and "list <path>:
// <names> selected <index>" whenever the game list's folder or selection
// changes, and "done" 40 ticks after the last step. It logs "pre_launch
// <path>" for each launch it hears about, cancelling it when its
// cancel_launches setting is on.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "seeker"

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
static int ticks = 0, step_count = 0, done = 0;
static int key_up_at = 0, key_scancode = 0;
static char last_screen[80] = "";
static char last_list[1024] = "";

static void say(const char* message) {
	host->log(ID, message);
}

static void key(int scancode, int down) {
	if (!pushEvent) return;
	Event event;
	memset(&event, 0, sizeof(event));
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? 1 : 0;
	event.key.scancode = scancode;
	pushEvent(&event);
}

static int scancode(const char* name) {
	if (!strcmp(name, "a")) return 22;      // S
	if (!strcmp(name, "b")) return 4;       // A
	if (!strcmp(name, "y")) return 20;      // Q
	if (!strcmp(name, "l1")) return 43;     // tab
	if (!strcmp(name, "r1")) return 5;      // B
	if (!strcmp(name, "start")) return 40;  // return
	if (!strcmp(name, "select")) return 53; // `
	if (!strcmp(name, "up")) return 82;     // up arrow
	if (!strcmp(name, "down")) return 81;   // down arrow
	if (!strcmp(name, "left")) return 80;   // left arrow
	if (!strcmp(name, "right")) return 79;  // right arrow
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

static void logChanges(void) {
	char message[1200];
	const char* screen = host->current_screen();
	if (strcmp(screen, last_screen)) {
		snprintf(last_screen, sizeof(last_screen), "%s", screen);
		snprintf(message, sizeof(message), "screen %s", screen);
		say(message);
	}
	const char* path = host->current_path();
	const MosaicList* list = host->current_list();
	if (!path || !list) return;
	char now[1024];
	int n = snprintf(now, sizeof(now), "%s:", path);
	int count = host->list_count(list);
	for (int i = 0; i < count && i < 8 && n > 0 && n < (int)sizeof(now); i++) {
		n += snprintf(now + n, sizeof(now) - (size_t)n, "%s %s", i ? "," : "", host->entry_name(host->list_get(list, i)));
	}
	if (n > 0 && n < (int)sizeof(now)) snprintf(now + n, sizeof(now) - (size_t)n, " selected %d", host->selected_index());
	if (strcmp(now, last_list)) {
		snprintf(last_list, sizeof(last_list), "%s", now);
		snprintf(message, sizeof(message), "list %s", now);
		say(message);
	}
}

static int cancel_launches = 0;

static int preLaunch(const MosaicLaunch* launch) {
	char message[1100];
	snprintf(message, sizeof(message), "pre_launch %s", launch->path ? launch->path : "(none)");
	say(message);
	return cancel_launches ? MOSAIC_LAUNCH_CANCEL : MOSAIC_LAUNCH_CONTINUE;
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	logChanges();
	if (key_up_at && ticks >= key_up_at) {
		key(key_scancode, 0);
		key_up_at = 0;
	}
	if (done || ticks < 40 || (ticks - 40) % 20) return;
	int at = (ticks - 40) / 20;
	char step[128];
	if (at < step_count && stepAt(at, step, sizeof(step))) {
		char message[160];
		if (!strncmp(step, "action:", 7)) {
			snprintf(message, sizeof(message), "run %s %d", step + 7, host->run_action(step + 7, NULL));
			say(message);
		}
		else if ((step[0] == '+' || step[0] == '-') && scancode(step + 1)) {
			key(scancode(step + 1), step[0] == '+');
		}
		else if (scancode(step)) {
			key_scancode = scancode(step);
			key(key_scancode, 1);
			key_up_at = ticks + 3;
		}
	}
	else if (at >= step_count + 2) {
		done = 1;
		say("done");
	}
}

// the sections setting's folder, ordered by the last word of each name
static char sections_dir[1024] = "";

static const char* lastWord(const MosaicEntry* entry) {
	const char* name = host->entry_name(entry);
	const char* space = strrchr(name, ' ');
	return space ? space + 1 : name;
}

static int sortCompare(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	if (!sections_dir[0] || !dir_path || strcmp(dir_path, sections_dir)) return 0;
	return strcmp(lastWord(a), lastWord(b));
}

static int sectionKey(const MosaicEntry* entry, const char* dir_path, char* out, size_t size) {
	if (!sections_dir[0] || !dir_path || strcmp(dir_path, sections_dir)) return 0;
	static int asked = 0;
	if (!asked) {
		asked = 1;
		char inner[64];
		char message[96];
		snprintf(message, sizeof(message), "inner entry_section %d", host->entry_section(entry, dir_path, inner, sizeof(inner)));
		say(message);
	}
	snprintf(out, size, "%s", lastWord(entry));
	return 1;
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(steps, sizeof(steps), "%s", host->cfg_get("steps", ""));
	cancel_launches = !strcmp(host->cfg_get("cancel_launches", "off"), "on");
	snprintf(sections_dir, sizeof(sections_dir), "%s", host->cfg_get("sections", ""));
	step_count = steps[0] ? 1 : 0;
	for (const char* p = steps; *p; p++) {
		if (*p == ',') step_count++;
	}
	pushEvent = (int (*)(Event*))dlsym(RTLD_DEFAULT, "SDL_PushEvent");
	if (!pushEvent) say("no SDL_PushEvent");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
	.pre_launch = preLaunch,
	.sort_compare = sortCompare,
	.section_key = sectionKey,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
