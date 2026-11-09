// A plugin that asks about Wi-Fi from init and tick. Desktop has no Wi-Fi,
// so it's told so, unless MOSAIC_LAUNCHER_WIFI_FAKE names a file standing in
// for a device: then it asks every tick until the first look is in, reads
// the answer into a full and a short MosaicWifi, times the asks, and turns
// the test's Wi-Fi off and on again by rewriting the file.

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "mosaic_plugin.h"

#define ID "wifier"

static const MosaicHost* host;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static long nowMs(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static void report(const char* when, int rc, const MosaicWifi* w) {
	say("%s: supported %i, enabled %i, connection %i \"%s\" \"%s\" rssi %i bars %i freq %i link %i", when,
		host->wifi_supported(), host->wifi_enabled(), rc, w->ssid, w->ip, w->rssi, w->bars, w->freq_mhz, w->link_mbps);
}

static int writeFake(const char* text) {
	const char* path = getenv("MOSAIC_LAUNCHER_WIFI_FAKE");
	if (!path || !path[0]) return -1;
	FILE* f = fopen(path, "w");
	if (!f) return -1;
	fputs(text, f);
	fclose(f);
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, wifi_connection)) return 1;
	MosaicWifi w = {.size = sizeof(w)};
	long before = nowMs();
	int rc = host->wifi_connection(&w);
	long took = nowMs() - before;
	report("init", rc, &w);
	say("init ask under 50 ms %i, null %i", took < 50, host->wifi_connection(NULL));
	return 0;
}

static int step = 0;
static long slowest = 0;

static void tick(unsigned long now) {
	(void)now;
	if (step < 0) return;
	MosaicWifi w = {.size = sizeof(w)};
	long before = nowMs();
	int rc = host->wifi_connection(&w);
	long took = nowMs() - before;
	if (took > slowest) slowest = took;
	if (!host->wifi_supported()) {
		report("tick", rc, &w);
		step = -1;
		return;
	}
	switch (step) {
		case 0: { // waiting for the first look
			if (rc == -1) return;
			report("tick", rc, &w);
			say("slowest ask under 50 ms %i", slowest < 50);
			// a plugin built before ip: only ssid, the rest untouched
			MosaicWifi small;
			memset(&small, 0x7f, sizeof(small));
			small.size = offsetof(MosaicWifi, ip);
			int small_rc = host->wifi_connection(&small);
			say("small %i \"%s\" ip untouched %i", small_rc, small.ssid, (unsigned char)small.ip[0] == 0x7f);
			writeFake("enabled=0\nconnected=1\nssid=Home\n");
			step = 1;
			break;
		}
		case 1: // off: no answer at once
			report("off", rc, &w);
			writeFake("enabled=1\nconnected=0\n");
			step = 2;
			break;
		case 2: // on again, but no network
			if (rc == -1) return;
			report("joined nothing", rc, &w);
			say("done");
			step = -1;
			break;
	}
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
