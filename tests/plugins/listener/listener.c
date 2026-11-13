// Subscribes to herald's events and the launcher's for the events case,
// and adds a favorite through the bundled favorites plugin to hear the set
// change. Its settings name the folder to open (folder) and a value only
// it has (probe), read in its callbacks to show they run as it. It logs:
//
//   subscribe <a> <b> <c> <d> <e>
//                         herald.ping, herald.hello, launcher.return,
//                         launcher.set_changed, and a bad name
//   got <name> [<payload, lines comma-separated>] as <probe>
//                         each event delivered
//   ping n=<n> from=<from> missing=<rc>
//                         event_value on herald.ping's payload
//   names <n> <names>     set_names, as favorites changed
//   open folder <rc>      open_path to the folder
//   toggle <rc>           run_action("favorites.toggle") on its first game
//   done                  once favorites' change is heard

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "listener"

static const MosaicHost* host;
static char folder[512];
static int ticks = 0, toggled = 0, done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[2048];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static char names[1024];
static void addName(const char* name, void* data) {
	(void)data;
	size_t len = strlen(names);
	snprintf(names + len, sizeof(names) - len, "%s%s", len ? "," : "", name);
}

static void heard(const char* name, const char* payload, void* data) {
	(void)data;
	char flat[1100];
	snprintf(flat, sizeof(flat), "%s", payload ? payload : "(null)");
	for (char* c = flat; *c; c++) {
		if (*c == '\n') *c = ',';
	}
	say("got %s [%s] as %s", name, flat, host->cfg_get("probe", "nobody"));
	if (!strcmp(name, "herald.ping")) {
		char n[8], from[16];
		host->event_value(payload, "n", n, sizeof(n));
		host->event_value(payload, "from", from, sizeof(from));
		say("ping n=%s from=%s missing=%d", n, from, host->event_value(payload, "nope", NULL, 0));
	}
	if (!strcmp(name, "launcher.set_changed") && !done) {
		names[0] = '\0';
		int count = host->set_names(addName, NULL);
		say("names %d %s", count, names);
		say("done");
		done = 1;
	}
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) say("open folder %d", host->open_path(folder, MOSAIC_ENTRY_DIR));
	const char* path = host->current_path();
	if (toggled || ticks < 3 || !path || strcmp(path, folder) != 0) return;
	toggled = 1;
	host->select_index(0);
	say("toggle %d", host->run_action("favorites.toggle", NULL));
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, set_names)) {
		say("no event calls");
		return 1;
	}
	snprintf(folder, sizeof(folder), "%s", host->cfg_get("folder", ""));
	say("subscribe %d %d %d %d %d", host->subscribe("herald.ping", heard, NULL), host->subscribe("herald.hello", heard, NULL),
		host->subscribe("launcher.return", heard, NULL), host->subscribe("launcher.set_changed", heard, NULL),
		host->subscribe("bad name", heard, NULL));
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
