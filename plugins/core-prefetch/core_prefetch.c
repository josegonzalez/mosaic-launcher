// Core prefetch: warms a game's core before it launches, so it starts
// faster.
//
// In pre_launch it finds the emulator pak the game launches with, as stock
// does (or the one emulator-override chose for it, when that plugin is on),
// reads the pak's launch.sh for the core it runs, and asks the kernel to
// read that core, then the game, into the page cache with
// posix_fadvise(POSIX_FADV_WILLNEED). The reads go on in the background
// while the launcher exits and the pak's script starts, so the core and
// the start of the game are in memory by the time minarch opens them.
//
// It only opens files read-only: it never writes anything, starts a
// process or cancels a launch. Only files on the card are opened, with
// links resolved. It stops at the max_mb setting across the files, and
// starts no new chunk once the max_ms setting has passed since pre_launch
// began. Without posix_fadvise, and on desktop, where the card is a folder
// on a fast disk, it loads but logs that it's unsupported and does nothing.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "prefetch_model.h"

#define ID "core-prefetch"
#define CHUNK (1024 * 1024)          // bytes per posix_fadvise call
#define CHOICES_MAX 16384            // the most of plugins.txt read
#define OVERRIDE_MAX (1024 * 1024)   // the most of emulator-override.txt read, as it reads it

static const MosaicHost* host;
static int supported = 0;
static char card_real[PATH_MAX];

static void say(const char* message) {
	host->log(ID, message);
}

static long setting(const char* key, const char* fallback, long lo, long hi) {
	const char* text = host->cfg_get(ID, key, fallback);
	char* end;
	long v = strtol(text, &end, 10);
	if (end == text || *end || v < lo || v > hi) v = strtol(fallback, NULL, 10);
	return v;
}

static long long nowMs(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#ifdef POSIX_FADV_WILLNEED
static int willNeed(int fd, long long offset, long long len) {
	return posix_fadvise(fd, (off_t)offset, (off_t)len, POSIX_FADV_WILLNEED);
}
static int (*advise)(int fd, long long offset, long long len) = willNeed;
#else
static int (*advise)(int fd, long long offset, long long len) = NULL;
#endif
static long long (*now_ms)(void) = nowMs;

// The pak emulator-override launches path with, when it's on and has a
// valid choice for it.
static int overridePak(const char* path, char* out, size_t size) {
	char file[CP_PATH_LEN];
	char choices[CHOICES_MAX];
	char key[CP_PATH_LEN];
	snprintf(file, sizeof(file), "%s/launcher/plugins.txt", host->userdata_path);
	if (CP_readText(file, choices, sizeof(choices)) < 0 || !CP_overrideOn(choices)) return 0;
	if (!CP_overrideKey(host->sdcard_path, path, key, sizeof(key))) return 0;
	snprintf(file, sizeof(file), "%s/launcher/emulator-override.txt", host->userdata_path);
	return CP_overridePak(file, key, OVERRIDE_MAX, out, size);
}

static int preLaunch(const MosaicLaunch* launch) {
	if (!supported || !launch || launch->type != MOSAIC_ENTRY_ROM || !launch->path) return MOSAIC_LAUNCH_CONTINUE;
	long long start = now_ms();
	char message[CP_PATH_LEN + 256];

	char pak[CP_NAME_LEN];
	if (!CP_emuTag(host->sdcard_path, launch->path, pak, sizeof(pak))) return MOSAIC_LAUNCH_CONTINUE;
	char chosen[CP_NAME_LEN];
	char script_path[CP_PATH_LEN];
	int found = 0;
	if (overridePak(launch->path, chosen, sizeof(chosen))
			&& CP_pakLaunch(host->sdcard_path, host->platform, chosen, script_path, sizeof(script_path))) {
		snprintf(pak, sizeof(pak), "%s", chosen);
		found = 1;
	}
	if (!found) found = CP_pakLaunch(host->sdcard_path, host->platform, pak, script_path, sizeof(script_path));

	char files[CP_MAX_FILES][CP_PATH_LEN];
	int count = 0;
	char core[CP_PATH_LEN] = "";
	if (found) {
		char script[CP_SCRIPT_MAX];
		char pak_dir[CP_PATH_LEN];
		snprintf(pak_dir, sizeof(pak_dir), "%s", script_path);
		char* slash = strrchr(pak_dir, '/');
		if (slash) *slash = '\0';
		if (CP_readText(script_path, script, sizeof(script)) >= 0
				&& CP_scriptCore(script, pak_dir, host->sdcard_path, host->platform, core, sizeof(core))
				&& CP_cardFile(card_real, core, files[count], CP_PATH_LEN)) {
			count++;
		}
		else {
			snprintf(message, sizeof(message), "can't tell which core %s.pak runs, or it isn't on the card", pak);
			say(message);
			core[0] = '\0';
		}
	}
	else {
		snprintf(message, sizeof(message), "no emulator pak %s on the card for this game", pak);
		say(message);
	}

	if (!strcmp(host->cfg_get(ID, "rom", "on"), "on")) {
		char roms[CP_MAX_FILES][CP_PATH_LEN];
		int n = CP_romFiles(launch->path, roms, CP_MAX_FILES - count);
		for (int i = 0; i < n && count < CP_MAX_FILES; i++) {
			if (CP_cardFile(card_real, roms[i], files[count], CP_PATH_LEN)) count++;
		}
	}
	if (!count) return MOSAIC_LAUNCH_CONTINUE;

	CPBudget budget = {
		.max_bytes = (long long)setting("max_mb", "16", 1, 256) * 1024 * 1024,
		.max_ms = setting("max_ms", "20", 1, 1000),
		.chunk = CHUNK,
		.start_ms = start,
		.advise = advise,
		.now_ms = now_ms,
	};
	CPResult result;
	CP_warm(files, count, &budget, &result);
	snprintf(message, sizeof(message), "warmed %lld KB in %d file%s (%s %s.pak) in %lld ms%s%s%s",
		result.bytes / 1024, result.files, result.files == 1 ? "" : "s",
		core[0] ? "the core from" : "the game only, no core from", pak,
		now_ms() - start, result.capped ? ", stopped at max_mb" : "", result.timed_out ? ", stopped at max_ms" : "",
		result.failed ? ", some couldn't be read" : "");
	say(message);
	return MOSAIC_LAUNCH_CONTINUE;
}

static int init(const MosaicHost* h) {
	host = h;
	supported = advise != NULL && host->platform && strcmp(host->platform, "desktop") != 0
		&& host->sdcard_path && realpath(host->sdcard_path, card_real) != NULL;
	if (!supported) say("unsupported on this platform: it has no posix_fadvise, or is desktop, so nothing is warmed");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
