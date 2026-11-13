// Random game: picks a game at random and launches it.
//
// Y on the game list picks from the system you're in, from any folder
// inside it, or from every system on the root. A Random game row on the
// root (the "root" setting) and a Random tile on the quick menu (the
// "tile" setting) pick from every system too.
//
// Every game is as likely as any other, so a system with more games is
// picked more often, except that games not in Recently Played are
// RG_UNPLAYED_WEIGHT times as likely while the "unplayed" setting is on.
// Games the hidden plugin hides (its .userdata/<platform>/launcher/
// hidden.txt) are never picked.
//
// The card is scanned once per launcher start, the first time a pick or
// the root needs it, the way stock lists it: dot files, .disabled and
// map.txt names left out, and only systems with an emulator. A folder with
// a .cue or .m3u named after itself is one game, launched as opening it
// would.
//
// Opening the root row shows the picked game and launches it on the next
// frame; if a plugin cancels the launch it stays there to try again.

#include <dirent.h>
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
#define MAX_DEPTH 3   // subfolders below a system folder that are scanned
#define MAX_RECENTS 64 // more than the game switcher keeps

static const MosaicHost* host;
static char folder_path[RG_PATH_LEN]; // <sd>/Random game, the root row
static char roms_path[RG_PATH_LEN];   // <sd>/Roms
static char hidden_path[RG_PATH_LEN + 32]; // <userdata>/launcher/hidden.txt
static RGLibrary library;
static RGPathSet hidden;
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

static int isDir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static int isOn(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(key, fallback), "off") != 0;
}

static char* readFile(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* text = size >= 0 ? malloc((size_t)size + 1) : NULL;
	if (text) text[fread(text, 1, (size_t)size, f)] = '\0';
	fclose(f);
	return text;
}

// True when a name is hidden in its folder, by name or by map.txt.
static int hiddenIn(const char* map, const char* name) {
	if (RG_hiddenName(name)) return 1;
	char alias[256];
	return map && RG_mapLookup(map, name, alias, sizeof(alias)) && RG_hiddenName(alias);
}

///////////////////////////////////////
// scanning

// True when a system folder has an emulator, as stock checks before
// listing it: the emulator is the tag in the folder's last parentheses.
static int hasEmulator(const char* folder) {
	char emu[256];
	const char* open = strrchr(folder, '(');
	if (snprintf(emu, sizeof(emu), "%s", open ? open + 1 : folder) >= (int)sizeof(emu)) return 0;
	char* close = strchr(emu, ')');
	if (open && close) *close = '\0';

	char path[RG_PATH_LEN * 2];
	if (snprintf(path, sizeof(path), "%s/.system/%s/paks/Emus/%s.pak/launch.sh", host->sdcard_path, host->platform, emu) < (int)sizeof(path)
			&& exists(path)) return 1;
	return snprintf(path, sizeof(path), "%s/Emus/%s/%s.pak/launch.sh", host->sdcard_path, host->platform, emu) < (int)sizeof(path)
		&& exists(path);
}

// A folder that launches a game when opened: it holds a .cue or .m3u
// named after itself.
static int isGameFolder(const char* path, const char* name) {
	char file[RG_PATH_LEN * 2];
	if (snprintf(file, sizeof(file), "%s/%s.cue", path, name) < (int)sizeof(file) && exists(file)) return 1;
	return snprintf(file, sizeof(file), "%s/%s.m3u", path, name) < (int)sizeof(file) && exists(file);
}

static void scanDir(const char* dir, const char* system, int depth) {
	DIR* dh = opendir(dir);
	if (!dh) return;

	char map_path[RG_PATH_LEN + 16];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", dir);
	char* map = readFile(map_path);

	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (hiddenIn(map, dp->d_name)) continue;
		char path[RG_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;

		int type = MOSAIC_ENTRY_ROM;
		if (isDir(path)) {
			if (endsWith(dp->d_name, ".pak")) type = MOSAIC_ENTRY_PAK;
			else if (isGameFolder(path, dp->d_name)) type = MOSAIC_ENTRY_DIR;
			else {
				if (depth < MAX_DEPTH) scanDir(path, system, depth + 1);
				continue;
			}
		}
		if (RG_add(&library, path, type, system) != 0) break;
	}
	closedir(dh);
	free(map);
}

static void scan(void) {
	if (scanned) return;
	scanned = 1;

	DIR* dh = opendir(roms_path);
	if (!dh) return;
	char map_path[RG_PATH_LEN + 16];
	snprintf(map_path, sizeof(map_path), "%s/map.txt", roms_path);
	char* map = readFile(map_path);

	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		if (hiddenIn(map, dp->d_name) || !hasEmulator(dp->d_name)) continue;
		char path[RG_PATH_LEN];
		if (snprintf(path, sizeof(path), "%s/%s", roms_path, dp->d_name) >= (int)sizeof(path)) continue;
		if (isDir(path)) scanDir(path, dp->d_name, 0);
	}
	closedir(dh);
	free(map);

	char message[96];
	snprintf(message, sizeof(message), "%d games in %d systems", library.count, library.system_count);
	say(message);
}

///////////////////////////////////////
// picking

static unsigned long below(unsigned long n, void* data) {
	(void)data;
	return (unsigned long)(RG_xorshift(&seed) % n);
}

// Picks a game from system, or from every system with -1. Returns 1 and
// writes its path and type, or returns 0 when there's nothing to pick.
static int pick(int system, char* out, size_t size, int* type) {
	scan();
	if (!library.count) return 0;

	if (RG_setLoad(&hidden, hidden_path) != 0) say("couldn't read the hidden list");

	// recent_path's string only lasts until the next call
	static char recent_copies[MAX_RECENTS][RG_PATH_LEN];
	const char* recents[MAX_RECENTS];
	int recent_count = 0;
	if (MOSAIC_HOST_HAS(host, recents_count) && MOSAIC_HOST_HAS(host, recent_path)) {
		int count = host->recents_count();
		for (int i = 0; i < count && recent_count < MAX_RECENTS; i++) {
			const char* path = host->recent_path(i);
			if (!path) break;
			snprintf(recent_copies[recent_count], RG_PATH_LEN, "%s", path);
			recents[recent_count] = recent_copies[recent_count];
			recent_count++;
		}
	}

	unsigned* weights = malloc((size_t)library.count * sizeof(unsigned));
	if (!weights) return 0;
	unsigned unplayed = isOn("unplayed", "on") ? RG_UNPLAYED_WEIGHT : 1;
	unsigned long total = RG_weights(&library, system, &hidden, recents, recent_count, unplayed, weights);

	int found = 0;
	while (total) {
		int i = RG_pick(weights, library.count, total, below, NULL);
		if (i < 0) break;
		if (exists(library.games[i].path)) {
			snprintf(out, size, "%s", library.games[i].path);
			*type = library.games[i].type;
			found = 1;
			break;
		}
		total -= weights[i]; // deleted since the scan
		weights[i] = 0;
	}
	free(weights);

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
		toast(tr("No games to pick from"));
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
		toast(tr("No games to pick from"));
		return;
	}
	host->open_path(shown_path, shown_type);
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(folder_path, sizeof(folder_path), "%s/" FOLDER_NAME, host->sdcard_path);
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);
	snprintf(hidden_path, sizeof(hidden_path), "%s/launcher/hidden.txt", host->userdata_path);
	RG_init(&library);
	RG_setInit(&hidden);
	scanned = 0;
	shown_path[0] = '\0';
	pending = 0;
	seed = (unsigned long long)time(NULL) ^ ((unsigned long long)getpid() << 32);

	if (!MOSAIC_HOST_HAS(host, open_path)) {
		say("this launcher can't open games for plugins; nothing to do");
		return 0;
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
	return 0;
}

static void quit(void) {
	RG_free(&library);
	RG_setClear(&hidden);
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
