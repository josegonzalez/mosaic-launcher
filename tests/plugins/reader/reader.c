// Runs pather's actions and lists every action, for the action paths case.
// From tick, one step every 20 ticks from the 40th, it logs:
//
//   selected <path>         the game list's selected entry's path
//   run path <rc>           run_action("pather.show") on its game setting
//   run plain <rc>          run_action("pather.plain") on that game
//   run empty <rc>          run_action("pather.show", ""), on the selected entry
//   count <n>               actions_count, then a line per action:
//   action <id> '<label>' owner '<plugin>' path <0|1> binds <n> [<by>:<screen>:<button>:<trigger>:<plugin>:<setting> ...]
//   small <rc> <id> <label byte>
//                           action_info for gamelist.back into a struct
//                           that ends before label, which is left alone
//   bad <past end> <negative> <null out>
//   run menu <rc>           run_action("gamelist.quick_menu")
//   run none <rc> on <screen>
//                           run_action("pather.show", NULL) on the quick
//                           menu, where there's no selected entry
//
// then presses B to go back to the game list and Y for pather.show there,
// pushing key events with SDL_PushEvent (found with dlsym, as seeker
// does), and logs "done".

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "reader"

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
#define SCANCODE_B 4 // A, the desktop build's B
#define SCANCODE_Y 20 // Q, its Y

static int (*pushEvent)(Event* event);
static const MosaicHost* host;
static char game[512];
static int ticks = 0, done = 0;
static int key_up_at = 0, key_scancode = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
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

static void press(int scancode) {
	key_scancode = scancode;
	key(key_scancode, 1);
	key_up_at = ticks + 3;
}

static void list(void) {
	int count = host->actions_count();
	say("count %d", count);
	int back = -1;
	for (int i = 0; i < count; i++) {
		MosaicActionInfo info = { .size = sizeof(info) };
		if (host->action_info(i, &info) != 0) {
			say("action %d refused", i);
			continue;
		}
		if (!strcmp(info.id, "gamelist.back")) back = i;
		char binds[1024] = "";
		size_t used = 0;
		for (int j = 0; j < info.binding_count && j < MOSAIC_ACTION_BINDINGS && used < sizeof(binds); j++) {
			const MosaicActionBinding* b = &info.bindings[j];
			used += (size_t)snprintf(binds + used, sizeof(binds) - used, "%s%d:%d:%d:%d:%s:%s", j ? " " : "",
				b->by, b->screen, b->button, b->trigger, b->plugin_id, b->setting);
		}
		say("action %s '%s' owner '%s' path %d binds %d [%s]", info.id, info.label, info.plugin_id,
			info.takes_path, info.binding_count, binds);
	}
	// a plugin built against a struct that ends before label
	MosaicActionInfo small;
	memset(&small, 'x', sizeof(small));
	small.size = offsetof(MosaicActionInfo, label);
	int rc = host->action_info(back, &small);
	small.id[sizeof(small.id) - 1] = '\0';
	say("small %d %s %c", rc, small.id, small.label[0]);
	MosaicActionInfo info = { .size = sizeof(info) };
	say("bad %d %d %d", host->action_info(count, &info), host->action_info(-1, &info), host->action_info(0, NULL));
}

static void step(int at) {
	switch (at) {
		case 0: {
			const MosaicEntry* entry = host->selected_entry();
			say("selected %s", entry && host->entry_path(entry) ? host->entry_path(entry) : "(none)");
			say("run path %d", host->run_action("pather.show", game));
			break;
		}
		case 1: say("run plain %d", host->run_action("pather.plain", game)); break;
		case 2: say("run empty %d", host->run_action("pather.show", "")); break;
		case 3: list(); break;
		case 4: say("run menu %d", host->run_action("gamelist.quick_menu", NULL)); break;
		case 5: say("run none %d on %s", host->run_action("pather.show", NULL), host->current_screen()); break;
		case 6: press(SCANCODE_B); break;
		case 7: press(SCANCODE_Y); break;
		case 9:
			done = 1;
			say("done");
			break;
		default: break;
	}
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	if (key_up_at && ticks >= key_up_at) {
		key(key_scancode, 0);
		key_up_at = 0;
	}
	if (done || ticks < 40 || (ticks - 40) % 20) return;
	step((ticks - 40) / 20);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, action_info)) {
		say("no action_info");
		return 1;
	}
	snprintf(game, sizeof(game), "%s", host->cfg_get("game", ""));
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
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
