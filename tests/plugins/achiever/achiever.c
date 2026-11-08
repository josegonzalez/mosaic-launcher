// A plugin that reads local RetroAchievements progress. Its cfg names the
// games to ask about (game1 to game4, full paths). Once the lists are up
// it logs that it's asking, with each game's first answer, then asks every
// tick until every game has an answer (or 8 seconds pass) and logs each
// one: the counts, the hash, and its answer for a NULL path. It also asks
// with a MosaicAchievements cut short after total, as an older plugin
// would, and logs whether the launcher kept to it. Then "done".

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "achiever"
#define GAMES 4
#define WAIT_MS 8000

static const MosaicHost* host;
static char games[GAMES][512];
static unsigned long started = 0;
static int stage = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[800];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static const char* nameOf(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int ask(const char* path, MosaicAchievements* out) {
	memset(out, 0, sizeof(*out));
	out->size = sizeof(*out);
	return host->achievements(path, out);
}

static void tick(unsigned long now) {
	if (stage == 2 || !host->current_list()) return;
	MosaicAchievements a;
	if (stage == 0) {
		stage = 1;
		started = now;
		say("asking");
		for (int i = 0; i < GAMES; i++)
			if (games[i][0]) say("first %s %i", nameOf(games[i]), ask(games[i], &a));
		return;
	}
	int waiting = 0;
	for (int i = 0; i < GAMES; i++)
		if (games[i][0] && ask(games[i], &a) == MOSAIC_ACHIEVEMENTS_UNKNOWN) waiting = 1;
	if (waiting && now - started < WAIT_MS) return;
	stage = 2;
	for (int i = 0; i < GAMES; i++) {
		if (!games[i][0]) continue;
		int rc = ask(games[i], &a);
		char hash[40] = "";
		int hashed = host->achievement_hash(games[i], hash, sizeof(hash));
		say("game %s rc %i total %i unlocked %i hardcore %i points %i/%i muted %i id %u hash %i %s", nameOf(games[i]), rc,
			a.total, a.unlocked, a.unlocked_hardcore, a.points_unlocked, a.points, a.muted, a.game_id, hashed,
			hash[0] ? hash : "-");
	}
	say("null %i", host->achievements(NULL, &a));
	// an older plugin's struct, ending after total
	if (games[0][0]) {
		MosaicAchievements old;
		memset(&old, 0x55, sizeof(old));
		old.size = offsetof(MosaicAchievements, unlocked);
		int rc = host->achievements(games[0], &old);
		say("short rc %i total %i rest untouched %i", rc, old.total, old.unlocked == 0x55555555 && old.game_id == 0x55555555u);
	}
	say("done");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, achievements)) {
		say("no achievements");
		return 1;
	}
	for (int i = 0; i < GAMES; i++) {
		char key[8];
		snprintf(key, sizeof(key), "game%i", i + 1);
		snprintf(games[i], sizeof(games[i]), "%s", host->cfg_get(ID, key, ""));
	}
	return 0;
}

static MosaicPlugin plugin = {
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
