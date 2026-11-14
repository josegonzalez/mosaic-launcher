// The card index case's second reader: on each launcher.index_ready it
// counts every game in the index, as indexer counts its own folder's.
// Once indexer's Added.gb shows, it adds Later.gb and reloads the lists,
// which checks the card again. It logs:
//
//   ready <n> games, Added.gb: <found>, Later.gb: <found>
//   reloaded               Later.gb written and reload_lists called
//   done                   once it has seen Later.gb

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "lister"
#define SYSTEM "Index Test (IXT)"

static const MosaicHost* host;
static char dir[1024];
static int reloaded = 0, done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int found(const char* name) {
	char path[1100];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	MosaicIndexEntry e = { .size = sizeof(e) };
	return host->index_find(path, &e) >= 0 && e.kind == MOSAIC_INDEX_GAME && !strcmp(e.system, SYSTEM);
}

static void ready(const char* name, const char* payload, void* data) {
	(void)name;
	(void)data;
	char count[16] = "";
	host->event_value(payload, "count", count, sizeof(count));
	int games = 0, total = host->index_count(NULL);
	for (int i = 0; i < total; i++) {
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_entry(NULL, i, &e) == 0 && e.kind == MOSAIC_INDEX_GAME && !strcmp(e.system, SYSTEM)) games++;
	}
	int added = found("Added.gb"), later = found("Later.gb");
	say("ready %d games, Added.gb: %s, Later.gb: %s, count %s", games, added ? "yes" : "no", later ? "yes" : "no",
		atoi(count) == total ? "matches" : "differs");
	if (added && !reloaded) {
		reloaded = 1;
		char path[1100];
		snprintf(path, sizeof(path), "%s/Later.gb", dir);
		FILE* f = fopen(path, "w");
		if (f) fclose(f);
		host->reload_lists();
		say("reloaded");
	}
	if (later && !done) {
		done = 1;
		say("done");
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, index_refresh)) {
		say("no card index");
		return 1;
	}
	snprintf(dir, sizeof(dir), "%s/Roms/" SYSTEM, host->sdcard_path);
	host->subscribe("launcher.index_ready", ready, NULL);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
