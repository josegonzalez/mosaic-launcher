// A plugin that calls every service the bundled providers offer, once
// every plugin has loaded, on its first tick: captions.text, games.played,
// states.last, emulator.choice, battery.history and info.format, for the
// games in its roms setting's folder, and copycat's own. It logs each
// reply with its lines joined by commas, and what call returns for a name
// nobody provides, a name whose provider declined, bad arguments, an out
// too small for the reply and a call from a thread of its own.

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "patron"

static const MosaicHost* host;
static int ticked = 0;
static char roms[512];

static void ask(const char* name, const char* args, size_t size) {
	char out[MOSAIC_CALL_MAX];
	int rc = host->call(name, args, out, size);
	for (char* c = out; *c; c++) {
		if (*c == '\n') *c = ',';
	}
	char message[MOSAIC_CALL_MAX + 128];
	snprintf(message, sizeof(message), "%s %i [%s]", name, rc < 0 ? rc : 0, out);
	host->log(ID, message);
}

static void askGame(const char* name, const char* game) {
	char args[600];
	snprintf(args, sizeof(args), "path=%s/%s", roms, game);
	ask(name, args, MOSAIC_CALL_MAX);
}

static int thread_rc = 99;
static void* run(void* data) {
	(void)data;
	char out[64];
	thread_rc = host->call("copycat.echo", "x=thread", out, sizeof(out));
	return NULL;
}

static void tick(unsigned long now) {
	(void)now;
	if (ticked++) return;
	askGame("captions.text", "Played.gb");
	askGame("games.played", "Played.gb");
	askGame("games.played", "Saved.gb");
	askGame("games.played", "Fresh.gb");
	askGame("states.last", "Played.gb");
	askGame("states.last", "Fresh.gb");
	askGame("emulator.choice", "Played.gb");
	askGame("emulator.choice", "Fresh.gb");
	ask("battery.history", NULL, MOSAIC_CALL_MAX);
	askGame("info.format", "Played.gb");
	ask("copycat.echo", "x=1", MOSAIC_CALL_MAX);
	ask("copycat.nest", NULL, MOSAIC_CALL_MAX);
	ask("nobody.here", NULL, MOSAIC_CALL_MAX);
	ask("dropper.gone", NULL, MOSAIC_CALL_MAX);
	ask("copycat.echo", "no equals", MOSAIC_CALL_MAX);
	ask("copycat.echo", "x=1", 8);
	pthread_t thread;
	if (pthread_create(&thread, NULL, run, NULL) == 0) pthread_join(thread, NULL);
	char message[256];
	snprintf(message, sizeof(message), "thread %i", thread_rc);
	host->log(ID, message);
	static const char* names[] = { "captions.text", "games.played", "states.last", "emulator.choice", "battery.history",
		"info.format", "dropper.gone", "nobody.here" };
	size_t used = (size_t)snprintf(message, sizeof(message), "capabilities");
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		used += (size_t)snprintf(message + used, sizeof(message) - used, " %i", host->has_capability(names[i]));
	}
	host->log(ID, message);
	host->log(ID, "done");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, call)) {
		host->log(ID, "no providers");
		return -1;
	}
	snprintf(roms, sizeof(roms), "%s", host->cfg_get("roms", ""));
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
