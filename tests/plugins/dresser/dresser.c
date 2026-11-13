// Reads the keys the theme slot's owner passes on, for the theme extras
// case. Its theme_file setting names the theme.txt the bundled theme-plus
// reads; on its first tick it writes a new one there and reloads the
// lists, which asks for the theme again. It logs:
//
//   init <word>           theme_value("dresser.word") from init, before
//                         the theme is in place: its fallback, unset
//   first word=<word> missing=<value>
//                         its key, and one the theme doesn't set
//   wrote <rc>            the new theme.txt, and the lists reloading
//   changed word=<word>   its key again on launcher.theme_changed
//   done

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "dresser"

static const MosaicHost* host;
static int wrote = 0, done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void changed(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	if (done) return;
	say("changed word=%s", host->theme_value(ID ".word", "unset"));
	say("done");
	done = 1;
}

static void tick(unsigned long now) {
	(void)now;
	if (wrote || !host->current_list()) return;
	wrote = 1;
	say("first word=%s missing=%s", host->theme_value(ID ".word", "unset"), host->theme_value(ID ".missing", "fallback"));
	FILE* f = fopen(host->cfg_get("theme_file", ""), "w");
	int rc = -1;
	if (f) {
		fprintf(f, "version=1\nname=Dressed\nled-status.colour=hint\ndresser.word=changed\n");
		rc = fclose(f);
	}
	say("wrote %d", rc);
	host->reload_lists();
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, theme_value)) {
		say("no theme_value");
		return 1;
	}
	say("init %s", host->theme_value(ID ".word", "unset"));
	if (host->subscribe("launcher.theme_changed", changed, NULL) != 0) say("couldn't subscribe");
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
