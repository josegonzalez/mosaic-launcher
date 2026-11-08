// A plugin that reads the play history. Its cfg names the games to ask
// about (game1 to game4, full paths) and the database. Once the lists are
// up it logs that it's asking, then each game's play time, last played time
// and session count, and today's total. It asks for every game on every
// frame after that, so a log with one read shows the answers are kept.
// Then it moves the database away and asks again, getting the same
// answers, and puts it back. Its post_return logs today's total too.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "tracker"
#define GAMES 4

static const MosaicHost* host;
static char games[GAMES][512];
static char db[512];
static int ticks = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
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

static void logGames(const char* label) {
	for (int i = 0; i < GAMES; i++) {
		if (!games[i][0]) continue;
		say("%s %s time %lld last %lld count %i", label, nameOf(games[i]), host->play_time(games[i]),
			host->last_played(games[i]), host->play_count(games[i]));
	}
	say("%s today %lld", label, host->play_time_today());
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) {
		say("asking");
		logGames("game");
		say("null %lld %lld %i", host->play_time(NULL), host->last_played(NULL), host->play_count(NULL));
	}
	else if (ticks > 2 && ticks < 30) {
		for (int i = 0; i < GAMES; i++) {
			if (games[i][0]) host->play_time(games[i]);
		}
		host->play_time_today();
	}
	else if (ticks == 30) {
		char moved[600];
		snprintf(moved, sizeof(moved), "%s.moved", db);
		int renamed = db[0] && rename(db, moved) == 0;
		logGames("kept");
		if (renamed) rename(moved, db);
		say("done");
	}
}

static void postReturn(void) {
	say("post_return today %lld", host->play_time_today());
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, play_time)) {
		say("no play history");
		return 1;
	}
	for (int i = 0; i < GAMES; i++) {
		char key[8];
		snprintf(key, sizeof(key), "game%i", i + 1);
		snprintf(games[i], sizeof(games[i]), "%s", host->cfg_get(ID, key, ""));
	}
	snprintf(db, sizeof(db), "%s", host->cfg_get(ID, "db", ""));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.post_return = postReturn,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
