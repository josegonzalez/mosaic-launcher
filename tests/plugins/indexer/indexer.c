// Reads the card index for the card index case, and adds a game mid-run.
// It logs:
//
//   init <count> <thread>  index_count(NULL) from init (MOSAIC_INDEX_NOT_READY
//                          before the first copy), and from its own thread
//   ready <n> games, <name>: <found>, <name>: <found>
//                          on each launcher.index_ready: the games in its
//                          system folder (Roms/Index Test (IXT)), and
//                          whether index_find finds Added.gb and Later.gb
//   entry <path> <name> <year> <publisher> <genre> <tag> <emulator>
//                          Tetris's entry, the first time it's ready
//   added <rc>             Added.gb written to the card, and index_refresh's
//                          return, 30 ticks after the first ready
//   done                   once it has seen Added.gb

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "indexer"
#define SYSTEM "Index Test (IXT)"

static const MosaicHost* host;
static char dir[1024];
static int readies = 0, added_at = -1, ticks = 0, done = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static int games(void) {
	int n = 0, count = host->index_count(SYSTEM);
	for (int i = 0; i < count; i++) {
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_entry(SYSTEM, i, &e) == 0 && e.kind == MOSAIC_INDEX_GAME) n++;
	}
	return n;
}

static int found(const char* name) {
	char path[1100];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return host->index_find(path, NULL) >= 0;
}

static void ready(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	readies++;
	int added = found("Added.gb");
	say("ready %d games, Added.gb: %s, Later.gb: %s", games(), added ? "yes" : "no", found("Later.gb") ? "yes" : "no");
	if (readies == 1) {
		char path[1100];
		snprintf(path, sizeof(path), "%s/Tetris (World).gb", dir);
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_find(path, &e) >= 0) {
			say("entry %s %s %s %s %s %s %d", e.path, e.name, e.year, e.publisher, e.genre, e.tag, e.emulator);
		}
		added_at = ticks + 30;
	}
	if (added && !done) {
		done = 1;
		say("done");
	}
}

static void* fromThread(void* arg) {
	*(int*)arg = host->index_count(NULL);
	return NULL;
}

static void tick(unsigned long now) {
	(void)now;
	ticks++;
	if (ticks != added_at) return;
	char path[1100];
	snprintf(path, sizeof(path), "%s/Added.gb", dir);
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
	say("added %d", host->index_refresh());
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, index_refresh)) {
		say("no card index");
		return 1;
	}
	snprintf(dir, sizeof(dir), "%s/Roms/" SYSTEM, host->sdcard_path);
	host->subscribe("launcher.index_ready", ready, NULL);
	int count = host->index_count(NULL);
	int other = 0;
	pthread_t thread;
	if (pthread_create(&thread, NULL, fromThread, &other) == 0) pthread_join(thread, NULL);
	say("init %s %d", count == MOSAIC_INDEX_NOT_READY ? "not-ready" : count > 0 ? "cached" : "empty", other);
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
