// Random game: picks a game at random and launches it.
//
// Y on the game list picks from the system you're in, from any folder
// inside it, or from every system on the root. A Random game row on the
// root (the "root" setting) and a Random tile on the quick menu (the
// "tile" setting) pick from every system too.
//
// Every game is as likely as any other, so a system with more games is
// picked more often, except that unplayed games are RG_UNPLAYED_WEIGHT
// times as likely while the "unplayed" setting is on, and only unplayed
// games are picked while the "unplayed_only" setting is on. A game is
// played as row-badges' games.played service says, the one definition of
// it; without that service, as when row-badges is off, a game is played
// when it's in Recently Played. Games any plugin's filter hides, such as
// the hidden plugin's, are never picked (is_filtered).
//
// The "from" setting limits every pick to the games in one entry set:
// all (the default) for every game, or a set's name, such as favorites,
// tags:<name> or smart:<name>. A set nobody registered, as when its
// plugin is off, has no games. The random-game.from action lists All
// games and every set but hidden (set_names) to pick from with choose,
// from a Pick from tile on the quick menu (the "from_tile" setting), a
// game list button (the "from_button" setting), or an FN button.
//
// The games come from the launcher's card index (index_count and the
// rest), as stock lists them: dot files, .disabled and map.txt names left
// out, only systems with an emulator, three folders deep. A folder with a
// .cue or .m3u named after itself is one game, launched as opening it
// would. They're listed again each time the index changes
// (launcher.index_ready); until it's first ready there's nothing to pick.
//
// Opening the root row shows the picked game and launches it on the next
// frame; if a plugin cancels the launch it stays there to try again.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "random_model.h"

#define ID "random-game"
#define FOLDER_NAME "Random game"
#define MAX_DEPTH 3   // subfolders below a system folder picked from
#define MAX_RECENTS 64 // more than the game switcher keeps

static const MosaicHost* host;
static char folder_path[RG_PATH_LEN]; // <sd>/Random game, the root row
static char roms_path[RG_PATH_LEN];   // <sd>/Roms
static RGLibrary library;
static RGPathSet only; // the from setting's set's games, while a pick runs
static int scanned = 0;
static unsigned long long seed;

// the root row's pick, kept while its folder is open
static char shown_path[RG_PATH_LEN];
static int shown_type;
static int pending = 0; // launch shown_path on the next tick

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int isOn(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(key, fallback), "off") != 0;
}

///////////////////////////////////////
// the games, from the card index

static void scan(void) {
	if (scanned) return;
	RG_free(&library);
	RG_init(&library);
	if (!MOSAIC_HOST_HAS(host, index_refresh) || host->index_count(NULL) == MOSAIC_INDEX_NOT_READY) return;
	scanned = 1;
	int count = host->index_count(NULL);
	for (int i = 0; i < count; i++) {
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_entry(NULL, i, &e) != 0) break;
		if (e.kind != MOSAIC_INDEX_GAME || !e.emulator || e.depth > MAX_DEPTH) continue;
		if (RG_add(&library, e.path, e.type, e.system) != 0) break;
	}

	char message[96];
	snprintf(message, sizeof(message), "%d games in %d systems", library.count, library.system_count);
	say(message);
}

// The index changed: list the games again when next asked, and the root,
// whose row shows only while there's a game.
static void indexReady(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	scanned = 0;
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
}

///////////////////////////////////////
// picking

static unsigned long below(unsigned long n, void* data) {
	(void)data;
	return (unsigned long)(RG_xorshift(&seed) % n);
}

static void addMember(const char* path, void* data) {
	(void)data;
	RG_setAdd(&only, path);
}

static int filtered(const char* path) {
	return MOSAIC_HOST_HAS(host, is_filtered) && host->is_filtered(path);
}

// what a pick asks about played games: games.played while it answers,
// else recents
static int use_service = 0;
static char recent_copies[MAX_RECENTS][RG_PATH_LEN]; // recent_path's string only lasts until the next call
static const char* recent_paths[MAX_RECENTS];
static int recents_read = 0;

static void readRecents(void) {
	recents_read = 0;
	if (!MOSAIC_HOST_HAS(host, recents_count) || !MOSAIC_HOST_HAS(host, recent_path)) return;
	int count = host->recents_count();
	for (int i = 0; i < count && recents_read < MAX_RECENTS; i++) {
		const char* path = host->recent_path(i);
		if (!path) break;
		snprintf(recent_copies[recents_read], RG_PATH_LEN, "%s", path);
		recent_paths[recents_read] = recent_copies[recents_read];
		recents_read++;
	}
}

// A game is played as games.played says, or by recents when nobody
// provides it or it can't answer.
static int gamePlayed(const RGGame* game, void* data) {
	(void)data;
	if (use_service) {
		char args[RG_PATH_LEN + 8], reply[64], played[8];
		snprintf(args, sizeof(args), "path=%s", game->path);
		int got = host->call("games.played", args, reply, sizeof(reply));
		if (got >= 0 && host->event_value(reply, "played", played, sizeof(played)) > 0) return !strcmp(played, "1");
		if (got == MOSAIC_CALL_UNKNOWN) {
			use_service = 0;
			say("nobody provides games.played, so played games are the recents");
		}
	}
	return RG_isPlayed(game->path, recent_paths, recents_read);
}

static int unplayedOnly(void) {
	return isOn("unplayed_only", "off");
}

// The toast when a pick finds nothing.
static const char* nothingToPick(void) {
	return unplayedOnly() ? tr("No unplayed games to pick from") : tr("No games to pick from");
}

// Picks a game from system, or from every system with -1. Returns 1 and
// writes its path and type, or returns 0 when there's nothing to pick.
static int pick(int system, char* out, size_t size, int* type) {
	scan();
	if (!library.count) return 0;

	const char* from = host->cfg_get("from", "all");
	int all = !from[0] || !strcmp(from, "all");
	RG_setClear(&only);
	if (!all && MOSAIC_HOST_HAS(host, is_filtered)) host->set_list(from, addMember, NULL);

	readRecents();
	use_service = MOSAIC_HOST_HAS(host, call);
	unsigned* weights = malloc((size_t)library.count * sizeof(unsigned));
	if (!weights) return 0;
	unsigned unplayed = isOn("unplayed", "on") ? RG_UNPLAYED_WEIGHT : 1;
	unsigned long total = RG_weights(&library, system, all ? NULL : &only, gamePlayed, NULL, unplayed, unplayedOnly(),
		weights);

	int found = 0;
	while (total) {
		int i = RG_pick(weights, library.count, total, below, NULL);
		if (i < 0) break;
		if (exists(library.games[i].path) && !filtered(library.games[i].path)) {
			snprintf(out, size, "%s", library.games[i].path);
			*type = library.games[i].type;
			found = 1;
			break;
		}
		total -= weights[i]; // deleted since the scan, or hidden
		weights[i] = 0;
	}
	free(weights);
	RG_setClear(&only);

	if (found) {
		char message[RG_PATH_LEN + 16];
		snprintf(message, sizeof(message), "picked %s", out);
		say(message);
	}
	return found;
}

static void launch(int system) {
	char path[RG_PATH_LEN];
	int type;
	if (!pick(system, path, sizeof(path), &type)) {
		toast(nothingToPick());
		return;
	}
	host->open_path(path, type);
}

///////////////////////////////////////
// the actions

// The system the user is in, -1 on the root, -2 when there's nowhere to
// pick from here (Recently Played, collections, Tools and the like), or -3
// in a system with no games.
static int scope(void) {
	const char* current = host->current_path ? host->current_path() : NULL;
	if (!current) return -2;
	if (!strcmp(current, host->sdcard_path) || !strcmp(current, roms_path) || !strcmp(current, folder_path)) return -1;
	size_t len = strlen(roms_path);
	if (strncmp(current, roms_path, len) != 0 || current[len] != '/') return -2;
	scan();
	int system = RG_systemOf(&library, roms_path, current);
	return system >= 0 ? system : -3;
}

static int canPick(const char* path, void* data) {
	(void)path;
	(void)data;
	if (!MOSAIC_HOST_HAS(host, open_path)) return 0;
	int system = scope();
	if (system == -1) {
		scan();
		return library.count > 0;
	}
	return system >= 0;
}

static void pickHere(const char* path, void* data) {
	(void)path;
	if (!canPick(path, data)) return;
	launch(scope());
}

static int canPickAll(const char* path, void* data) {
	(void)path;
	(void)data;
	if (!MOSAIC_HOST_HAS(host, open_path)) return 0;
	scan();
	return library.count > 0;
}

static void pickAll(const char* path, void* data) {
	(void)path;
	(void)data;
	launch(-1);
}

///////////////////////////////////////
// picking from

#define FROM_MAX 64 // what choose takes, All games among them

static char from_names[FROM_MAX][80]; // "all", then the sets, for the list
static char from_labels[FROM_MAX][160];
static int from_count = 0;

static void addFrom(const char* name, void* data) {
	(void)data;
	if (!RG_pickable(name) || from_count >= FROM_MAX || strlen(name) >= sizeof(from_names[0])) return;
	snprintf(from_names[from_count], sizeof(from_names[0]), "%s", name);
	RG_setLabel(name, tr("Favorites"), tr("Tag: %s"), tr("Collection: %s"), from_labels[from_count], sizeof(from_labels[0]));
	from_count++;
}

static void pickedFrom(int index, void* data) {
	(void)data;
	if (index < 0 || index >= from_count) return;
	host->cfg_set("from", from_names[index]);
	char message[256];
	snprintf(message, sizeof(message), tr("Picking from %s"), from_labels[index]);
	toast(message);
	snprintf(message, sizeof(message), "picking from %s", from_names[index]);
	say(message);
}

static int canChooseFrom(const char* path, void* data) {
	(void)path;
	(void)data;
	return MOSAIC_HOST_HAS(host, choose) && MOSAIC_HOST_HAS(host, set_names);
}

static void chooseFrom(const char* path, void* data) {
	if (!canChooseFrom(path, data)) return;
	from_count = 0;
	snprintf(from_names[0], sizeof(from_names[0]), "all");
	snprintf(from_labels[0], sizeof(from_labels[0]), "%s", tr("All games"));
	from_count = 1;
	host->set_names(addFrom, NULL);
	const char* labels[FROM_MAX];
	int current = 0;
	const char* from = host->cfg_get("from", "all");
	for (int i = 0; i < from_count; i++) {
		labels[i] = from_labels[i];
		if (!strcmp(from_names[i], from)) current = i;
	}
	if (host->choose(tr("Pick from"), labels, from_count, current, pickedFrom, NULL) != 0) say("couldn't list the sets");
}

///////////////////////////////////////
// hooks

static void rootEntries(MosaicList* root) {
	if (!MOSAIC_HOST_HAS(host, open_path) || !isOn("root", "on")) return;
	scan();
	if (!library.count) return;
	host->list_push(root, host->entry_new(folder_path, MOSAIC_ENTRY_DIR, tr("Random game")));
}

// Opening the root row picks a game and shows it; tick launches it, since
// a list hook can't. A reload while it's open keeps the same game.
static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder_path) != 0) return NULL;
	MosaicList* list = host->list_new();
	if (!shown_path[0]) {
		pending = 1;
		if (!pick(-1, shown_path, sizeof(shown_path), &shown_type)) shown_path[0] = '\0';
	}
	if (shown_path[0] && exists(shown_path)) {
		host->list_push(list, host->entry_new(shown_path, shown_type, NULL));
	}
	return list;
}

static void tick(unsigned long now) {
	(void)now;
	if (!pending && !shown_path[0]) return;
	const char* current = host->current_path ? host->current_path() : NULL;
	if (!current || strcmp(current, folder_path) != 0) {
		pending = 0;
		shown_path[0] = '\0'; // left the folder: the next visit picks again
		return;
	}
	if (!pending) return;
	pending = 0;
	if (!shown_path[0]) {
		toast(nothingToPick());
		return;
	}
	host->open_path(shown_path, shown_type);
}

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
	{ "SELECT", MOSAIC_BUTTON_SELECT },
};

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/" FOLDER_NAME, host->sdcard_path);
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	RG_init(&library);
	RG_setInit(&only);
	scanned = 0;
	shown_path[0] = '\0';
	pending = 0;
	seed = (unsigned long long)time(NULL) ^ ((unsigned long long)getpid() << 32);

	if (!MOSAIC_HOST_HAS(host, open_path)) {
		say("this launcher can't open games for plugins; nothing to do");
		return 0;
	}
	if (!MOSAIC_HOST_HAS(host, index_refresh)) say("this launcher has no card index; there's nothing to pick");
	else {
		host->subscribe("launcher.index_ready", indexReady, NULL);
		host->index_count(NULL); // starts reading the card, so it's ready by the first pick
	}
	if (host->register_action("random-game.pick", "RANDOM", 0, canPick, pickHere, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "random-game.pick") != 0) {
		say("couldn't bind Y; pick a game from the root row or the quick menu");
	}
	if (isOn("tile", "off")) {
		if (host->register_action("random-game.tile", "Random", 0, canPickAll, pickAll, NULL) != 0
				|| !MOSAIC_HOST_HAS(host, quick_tile)
				|| host->quick_tile("random-game.tile", MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0) {
			say("couldn't add the Random tile");
		}
	}
	if (host->register_action("random-game.from", "Pick from", 0, canChooseFrom, chooseFrom, NULL) != 0) {
		say("couldn't register random-game.from");
	}
	else {
		const char* name = host->cfg_get("from_button", "none");
		for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
			if (strcmp(name, buttons[i].name) != 0) continue;
			if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, "random-game.from") != 0) {
				say("couldn't bind the pick from button");
			}
		}
		if (isOn("from_tile", "off") && (!MOSAIC_HOST_HAS(host, quick_tile)
				|| host->quick_tile("random-game.from", MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
			say("couldn't add the Pick from tile");
		}
	}
	return 0;
}

static void quit(void) {
	RG_free(&library);
	RG_setClear(&only);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
