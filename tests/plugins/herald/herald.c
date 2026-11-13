// Publishes events for the events case, from init and from a thread of its
// own. It logs:
//
//   init <a> <b> <c>      publish from init: herald.hello (its own), music.track
//                         (another plugin's prefix) and a bad payload
//   thread <a> <b>        publish from its thread: herald.ping, then
//                         romm.download_done (another plugin's prefix)

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include "mosaic_plugin.h"

#define ID "herald"

static const MosaicHost* host;
static pthread_t thread;
static int started = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void* run(void* arg) {
	(void)arg;
	int ping = host->publish("herald.ping", "n=1\nfrom=thread");
	int other = host->publish("romm.download_done", "path=/nope");
	say("thread %d %d", ping, other);
	return NULL;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, set_names)) {
		say("no event calls");
		return 1;
	}
	say("init %d %d %d", host->publish("herald.hello", NULL), host->publish("music.track", "title=x"),
		host->publish("herald.bad", "no equals sign"));
	started = pthread_create(&thread, NULL, run, NULL) == 0;
	return 0;
}

static void quit(void) {
	if (started) pthread_join(thread, NULL);
	started = 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
