// A plugin that reads the play history. Its cfg names the games to ask
// about (game1 to game4, full paths) and the database. Once the lists are
// up it logs that it's asking, then each game's play time, last played time
// and session count, and today's total. It asks for every game on every
// frame after that, so a log with one read shows the answers are kept.
// Then it moves the database away and asks again, getting the same
// answers, and puts it back. Its post_return logs today's total too. With
// history=1 in its cfg it also logs the whole history at each of those
// steps: the games listed, each game's details and the last five days'
// totals.

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "tracker"
#define GAMES 4
#define LISTED 8
#define DAYS 5

static const MosaicHost* host;
static char games[GAMES][512];
static char db[512];
static int history = 0;
static int ticks = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[900];
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

static void logHistory(const char* label) {
	int count = host->play_games_count();
	say("%s games %i", label, count);
	for (int i = 0; i < count && i < LISTED; i++) {
		MosaicPlayGame game;
		memset(&game, 0, sizeof(game));
		game.size = sizeof(game);
		if (host->play_game_info(i, &game)) {
			say("%s listed %i failed", label, i);
			continue;
		}
		say("%s listed %i %s name %s time %lld first %lld last %lld count %i orphan %i", label, i,
			game.path[0] ? game.path : "-", game.name[0] ? game.name : "-", game.seconds, game.first_played,
			game.last_played, game.count, game.orphan);
	}
	for (int d = 0; d < DAYS; d++) say("%s day %i %lld", label, d, host->play_time_on(d));
	say("%s day -1 %lld", label, host->play_time_on(-1));
}

// Bad asks, and a plugin built before the later fields: only what fits
// is filled.
static void logEdges(void) {
	MosaicPlayGame game;
	memset(&game, 0, sizeof(game));
	game.size = sizeof(game);
	int past = host->play_game_info(host->play_games_count(), &game);
	int null = host->play_game_info(0, NULL);
	int negative = host->play_game_info(-1, &game);
	say("edges %i %i %i", past, null, negative);
	memset(&game, 0x55, sizeof(game));
	game.size = offsetof(MosaicPlayGame, name);
	int rc = host->play_game_info(0, &game);
	int untouched = 1;
	const unsigned char* bytes = (const unsigned char*)&game;
	for (size_t i = offsetof(MosaicPlayGame, name); i < sizeof(game); i++) {
		if (bytes[i] != 0x55) untouched = 0;
	}
	say("small %i path %s untouched %i", rc, rc == 0 ? nameOf(game.path) : "-", untouched);
}

static void logGames(const char* label) {
	for (int i = 0; i < GAMES; i++) {
		if (!games[i][0]) continue;
		say("%s %s time %lld last %lld count %i", label, nameOf(games[i]), host->play_time(games[i]),
			host->last_played(games[i]), host->play_count(games[i]));
	}
	say("%s today %lld", label, host->play_time_today());
	if (history) logHistory(label);
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 2) {
		say("asking");
		logGames("game");
		say("null %lld %lld %i", host->play_time(NULL), host->last_played(NULL), host->play_count(NULL));
		if (history) logEdges();
	}
	else if (ticks > 2 && ticks < 30) {
		for (int i = 0; i < GAMES; i++) {
			if (games[i][0]) host->play_time(games[i]);
		}
		host->play_time_today();
		if (history) {
			MosaicPlayGame game;
			game.size = sizeof(game);
			int count = host->play_games_count();
			for (int i = 0; i < count; i++) host->play_game_info(i, &game);
			for (int d = 0; d < 30; d++) host->play_time_on(d);
		}
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
	if (history) logHistory("post_return");
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
	history = !strcmp(host->cfg_get(ID, "history", "0"), "1");
	if (history && !MOSAIC_HOST_HAS(host, play_time_on)) {
		say("no whole play history");
		return 1;
	}
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
