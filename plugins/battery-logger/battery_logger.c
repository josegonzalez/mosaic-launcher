// Battery logger: logs how much battery each game used to a CSV.
//
// The launcher exits while a game runs, so a session is two readings of
// the battery and the clock: one in pre_launch, just before the game
// starts, kept in a file in /tmp, and one in post_return, as the launcher
// starts again after it. Play time is the difference between the two
// CLOCK_MONOTONIC readings, which leaves out time the device spent
// suspended. A reboot or power cut while the game runs clears /tmp, and
// the launcher doesn't call post_return after one, so that session is
// dropped rather than logged wrong.
//
// Paks aren't logged. A session isn't logged when either reading isn't a
// level from 1 to 100, when it's shorter than the min_minutes setting, or,
// unless the charged setting is on, when it charged: charging at either
// reading, or the level went up between them.
//
// Rows go to .userdata/<platform>/launcher/battery-logger.csv. On a
// platform without a battery, such as desktop, the plugin loads but logs
// that it's unsupported and logs nothing.
//
// Other plugins read the log with the battery.history service, on any
// platform: the last 64 sessions from battery-logger.old.csv and
// battery-logger.csv, oldest first, or the last count=<n> of them, as
// count=<n>, seconds=<their play time> and
// session.<i>=<start>,<seconds>,<start_percent>,<end_percent>,<charged>
// with the start a Unix time and charged 1 or 0. Both files are read
// again when either changed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "battery_model.h"

#define ID "battery-logger"

#ifndef BL_START_FILE
#define BL_START_FILE "/tmp/mosaic-battery-logger.start"
#endif

static const MosaicHost* host;
static int supported = 0;
static char csv_path[BL_PATH_LEN];
static char old_path[BL_PATH_LEN];

static long long monoNow(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
	return (long long)ts.tv_sec;
}
static long long (*mono_now)(void) = monoNow;

static void say(const char* message) {
	host->log(ID, message);
}

static long setting(const char* key, const char* fallback, long lo, long hi) {
	const char* text = host->cfg_get(key, fallback);
	char* end;
	long v = strtol(text, &end, 10);
	if (end == text || *end || v < lo || v > hi) v = strtol(fallback, NULL, 10);
	return v;
}

static int saveStart(const BLStart* start) {
	char line[BL_PATH_LEN + 128];
	if (BL_formatStart(start, line, sizeof(line)) != 0) return -1;
	char tmp[sizeof(BL_START_FILE) + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", BL_START_FILE);
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	int ok = fputs(line, f) >= 0;
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, BL_START_FILE) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

// Reads and removes the start a launch left, so it's used at most once.
static int takeStart(BLStart* out) {
	char line[BL_PATH_LEN + 128];
	FILE* f = fopen(BL_START_FILE, "r");
	if (!f) return -1;
	char* got = fgets(line, sizeof(line), f);
	fclose(f);
	unlink(BL_START_FILE);
	return got && BL_parseStart(line, out) == 0 ? 0 : -1;
}

///////////////////////////////////////
// the battery.history service

static BLHistory history;
static struct stat seen[2]; // the old and current logs as last read, st_size -1 for not yet

static int changed(const char* path, struct stat* was) {
	struct stat st;
	if (stat(path, &st) != 0) memset(&st, 0, sizeof(st));
	int differs = st.st_size != was->st_size || st.st_mtime != was->st_mtime || st.st_ino != was->st_ino;
	*was = st;
	return differs;
}

// Up to max bytes of the file at path, NUL-terminated, or NULL.
static char* readLog(const char* path, long max) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	char* text = malloc((size_t)max + 1);
	size_t got = text ? fread(text, 1, (size_t)max, f) : 0;
	fclose(f);
	if (text) text[got] = '\0';
	return text;
}

// Reads both logs again when either changed since the last call.
static void readHistory(void) {
	const char* paths[2] = { old_path, csv_path };
	int stale = changed(paths[0], &seen[0]);
	stale |= changed(paths[1], &seen[1]);
	if (!stale) return;
	BL_historyInit(&history);
	for (int i = 0; i < 2; i++) {
		// a log grows past max_kb's largest, 1024 KB, by at most a row
		char* text = readLog(paths[i], 1024 * 1024 + BL_ROW_LEN);
		if (text) BL_historyAdd(&history, text);
		free(text);
	}
}

// battery.history: count=<sessions> (optional) gives the last that many
// sessions logged, every one of the last BL_HISTORY_MAX without it.
static int provideHistory(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char text[16];
	int count = 0;
	if (host->event_value(args, "count", text, sizeof(text)) >= 0) {
		char* end;
		long n = strtol(text, &end, 10);
		if (end == text || *end || n < 1) return -1;
		count = n > BL_HISTORY_MAX ? BL_HISTORY_MAX : (int)n;
	}
	readHistory();
	return BL_reply(&history, count, out, size);
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(csv_path, sizeof(csv_path), "%s/launcher/" ID ".csv", host->userdata_path);
	snprintf(old_path, sizeof(old_path), "%s/launcher/" ID ".old.csv", host->userdata_path);
	BL_historyInit(&history);
	memset(seen, 0, sizeof(seen));
	seen[0].st_size = seen[1].st_size = -1;
	// what it logged before, on any platform
	if (MOSAIC_HOST_HAS(host, call) && host->provide("battery.history", provideHistory) != 0) say("couldn't provide battery.history");
	supported = MOSAIC_HOST_HAS(host, battery_level) && MOSAIC_HOST_HAS(host, is_charging)
		&& host->battery_level() != -1;
	if (!supported) say("unsupported on this platform: there's no battery to read, so nothing is logged");
	return 0;
}

static int preLaunch(const MosaicLaunch* launch) {
	if (!supported) return MOSAIC_LAUNCH_CONTINUE;
	unlink(BL_START_FILE); // a start from a launch that didn't go ahead
	if (launch->type != MOSAIC_ENTRY_ROM || !launch->path) return MOSAIC_LAUNCH_CONTINUE;

	BLStart start = { .level = host->battery_level(), .charging = host->is_charging(),
		.mono = mono_now(), .wall = (long long)time(NULL) };
	if (!BL_validReading(start.level, start.charging) || start.mono < 0) {
		say("not logging this game: the battery reading before it was bad");
		return MOSAIC_LAUNCH_CONTINUE;
	}
	snprintf(start.path, sizeof(start.path), "%s", launch->path);
	if (strlen(launch->path) >= sizeof(start.path) || saveStart(&start) != 0)
		say("not logging this game: couldn't keep the reading before it");
	return MOSAIC_LAUNCH_CONTINUE;
}

static void postReturn(void) {
	if (!supported) return;
	BLStart start;
	if (takeStart(&start) != 0) return; // a pak, or nothing to log

	long long min_seconds = setting("min_minutes", "1", 0, 24 * 60) * 60;
	int log_charged = !strcmp(host->cfg_get("charged", "off"), "on");
	BLSession session;
	switch (BL_session(&start, host->battery_level(), host->is_charging(), mono_now(),
			min_seconds, log_charged, &session)) {
		case BL_OK: break;
		case BL_BAD_READING: say("not logging the last game: the battery reading after it was bad"); return;
		case BL_BAD_TIME: say("not logging the last game: the clock went backwards"); return;
		case BL_TOO_SHORT: return;
		case BL_CHARGED: say("not logging the last game: the device charged while it ran"); return;
		default: return;
	}

	char row[BL_ROW_LEN];
	if (BL_formatRow(&session, host->sdcard_path, row, sizeof(row)) != 0) {
		say("not logging the last game: its row doesn't fit");
		return;
	}
	long max_bytes = setting("max_kb", "256", 1, 1024 * 1024) * 1024;
	if (BL_append(csv_path, old_path, row, max_bytes) != 0) say("couldn't write to battery-logger.csv");
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
