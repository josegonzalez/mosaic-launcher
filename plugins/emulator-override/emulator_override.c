// Emulator override: a per-game choice of emulator pak.
//
// Y on a game (the "button" setting picks another) opens a list of the
// emulator paks that can run its system, with Default first; picking one
// saves it for that game and Default clears it. Other plugins' actions on
// the same button share it, as a menu.
//
// The choices are in .userdata/<platform>/launcher/emulator-override.txt,
// one card-relative game path, a tab and the pak's name per line. A game
// folder, its discs and its .m3u share one line, keyed as override_model.c
// keys them.
//
// Before a game launches, its choice is checked again: the pak has to be
// in Emus/<platform>/ or .system/<platform>/paks/Emus/ on the card, found
// the way stock finds a system's pak, with a launch.sh that doesn't lead
// anywhere else. Then pre_launch replaces the launch with that launch.sh,
// which the launcher quotes and gives the game's path, as it does stock's
// own pak. A choice that fails the check is logged and the game launches
// as usual.
//
// Other plugins ask for a game's choice with the emulator.choice service:
// path=<game or game folder> gives pak=<the chosen pak, empty for none>
// and launch=<its launch.sh, empty when there's none or it fails that
// check>.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "override_model.h"

#define ID "emulator-override"
#define MAX_PAKS 63 // choose takes 64 choices, one is Default

static const MosaicHost* host;
static char state_dir[EO_PATH_LEN];  // <userdata>/launcher
static char map_path[EO_PATH_LEN + 32];
static EOMap map;
static struct stat map_seen; // the choices file as last read or written
static int list_all = 0;

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

///////////////////////////////////////
// the choices file

static void rememberMap(void) {
	if (stat(map_path, &map_seen) != 0) memset(&map_seen, 0, sizeof(map_seen));
}

// Reads the choices again if the file changed since we last saw it.
static void refreshMap(void) {
	struct stat st;
	if (stat(map_path, &st) != 0) memset(&st, 0, sizeof(st));
	if (st.st_mtime == map_seen.st_mtime && st.st_size == map_seen.st_size && st.st_ino == map_seen.st_ino) return;
	if (EO_mapLoad(&map, map_path) != 0) say("couldn't read the choices file");
	map_seen = st;
}

static int saveMap(void) {
	mkdir(state_dir, 0755);
	if (EO_mapSave(&map, map_path) != 0) {
		say("couldn't save the choices file");
		return -1;
	}
	rememberMap();
	return 0;
}

///////////////////////////////////////
// choosing

// The selected game, as the last check found it.
static char checked[EO_PATH_LEN];
static int checked_type = -1;
static int checked_ok;
static char key[EO_PATH_LEN];
static char tag[EO_NAME_LEN];
static char* paks[MAX_PAKS];
static int pak_count;

// What the open list is for, waiting on a pick.
static char picking[EO_PATH_LEN];

static void freePaks(void) {
	for (int i = 0; i < pak_count; i++) free(paks[i]);
	pak_count = 0;
}

static void forgetCheck(void) {
	checked[0] = '\0';
	checked_type = -1;
	checked_ok = 0;
	freePaks();
}

// Looks at the selected game again when the selection moves.
static int check(void) {
	const MosaicEntry* entry = host->selected_entry();
	if (!entry) return 0;
	int type = host->entry_type(entry);
	const char* path = host->entry_path(entry);
	if ((type != MOSAIC_ENTRY_ROM && type != MOSAIC_ENTRY_DIR) || !path) return 0;
	// asked every time the hints draw, so only look on the card when the
	// selection moves
	if (type == checked_type && !strcmp(path, checked)) return checked_ok;
	forgetCheck();
	snprintf(checked, sizeof(checked), "%s", path);
	checked_type = type;
	if (EO_gameKey(host->sdcard_path, path, type == MOSAIC_ENTRY_DIR, key, sizeof(key), tag, sizeof(tag))) {
		pak_count = EO_paks(host->sdcard_path, host->platform, tag, list_all, paks, MAX_PAKS);
		refreshMap();
		// with nothing to pick, it's still there to clear a choice
		checked_ok = pak_count > 0 || EO_mapGet(&map, key) != NULL;
	}
	return checked_ok;
}

static int canChoose(void* data) {
	(void)data;
	return check();
}

static void picked(int index, void* data) {
	(void)data;
	if (index < 0 || index > pak_count || !picking[0] || strcmp(picking, key) != 0) {
		picking[0] = '\0';
		return;
	}
	refreshMap();
	const char* pak = index == 0 ? NULL : paks[index - 1];
	int changed = EO_mapSet(&map, picking, pak);
	if (changed < 0 || (changed && saveMap() != 0)) {
		EO_mapLoad(&map, map_path);
		toast(tr("Couldn't save the emulator"));
		picking[0] = '\0';
		return;
	}
	char message[EO_PATH_LEN + EO_NAME_LEN + 32];
	if (pak) snprintf(message, sizeof(message), "%s now launches with %s", picking, pak);
	else snprintf(message, sizeof(message), "%s now launches with its default emulator", picking);
	if (changed) say(message);
	char shown[EO_NAME_LEN + 256];
	if (pak) snprintf(shown, sizeof(shown), tr("Emulator set to %s"), pak);
	else snprintf(shown, sizeof(shown), "%s", tr("Using the default emulator"));
	toast(shown);
	picking[0] = '\0';
	forgetCheck(); // clearing the last choice can leave nothing to pick
}

static void choose(void* data) {
	(void)data;
	if (!check()) return;
	refreshMap();
	char first[EO_NAME_LEN + 256];
	snprintf(first, sizeof(first), tr("Default (%s)"), tag);
	const char* list[MAX_PAKS + 1];
	list[0] = first;
	const char* current = EO_mapGet(&map, key);
	int selected = 0;
	for (int i = 0; i < pak_count; i++) {
		list[i + 1] = paks[i];
		if (current && !strcmp(current, paks[i])) selected = i + 1;
	}
	snprintf(picking, sizeof(picking), "%s", key);
	if (host->choose(tr("Emulator"), list, pak_count + 1, selected, picked, NULL) != 0) {
		picking[0] = '\0';
		toast(tr("Couldn't list the emulators"));
	}
}

///////////////////////////////////////
// launching

static int preLaunch(const MosaicLaunch* launch) {
	if (!launch || launch->type != MOSAIC_ENTRY_ROM || !launch->path) return MOSAIC_LAUNCH_CONTINUE;
	char game[EO_PATH_LEN];
	char sys_tag[EO_NAME_LEN];
	if (!EO_gameKey(host->sdcard_path, launch->path, 0, game, sizeof(game), sys_tag, sizeof(sys_tag))) return MOSAIC_LAUNCH_CONTINUE;
	refreshMap();
	const char* pak = EO_mapGet(&map, game);
	if (!pak) return MOSAIC_LAUNCH_CONTINUE;

	char message[EO_PATH_LEN * 2 + 128];
	char program[EO_PATH_LEN];
	if (!EO_pakLaunch(host->sdcard_path, host->platform, pak, program, sizeof(program))) {
		snprintf(message, sizeof(message), "%s for %s isn't an emulator pak with a launch.sh on the card, launching with the default", pak, game);
		say(message);
		return MOSAIC_LAUNCH_CONTINUE;
	}
	// refused when a plugin earlier in load order already replaced it
	if (host->replace_launch(program, NULL, 0) != 0) {
		snprintf(message, sizeof(message), "couldn't replace the launch of %s with %s, the launcher log says why", game, program);
		say(message);
		return MOSAIC_LAUNCH_CONTINUE;
	}
	snprintf(message, sizeof(message), "launching %s with %s", game, program);
	say(message);
	return MOSAIC_LAUNCH_REPLACE;
}

///////////////////////////////////////
// the emulator.choice service

// path=<game or game folder>: its choice, and the launch.sh it launches
// with, as pre_launch would check it now.
static int provideChoice(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char path[EO_PATH_LEN];
	int len = host->event_value(args, "path", path, sizeof(path));
	if (len <= 0 || len >= (int)sizeof(path)) return -1;
	struct stat st;
	int is_dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
	char game[EO_PATH_LEN], sys_tag[EO_NAME_LEN];
	if (!EO_gameKey(host->sdcard_path, path, is_dir, game, sizeof(game), sys_tag, sizeof(sys_tag))) return EO_reply(NULL, NULL, out, size);
	refreshMap();
	const char* pak = EO_mapGet(&map, game);
	char program[EO_PATH_LEN];
	int usable = pak && EO_pakLaunch(host->sdcard_path, host->platform, pak, program, sizeof(program));
	return EO_reply(pak, usable ? program : NULL, out, size);
}

///////////////////////////////////////
// hooks

static void postReturn(void) {
	refreshMap();
	forgetCheck();
}

static void quit(void) {
	EO_mapFree(&map);
	forgetCheck();
}

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "Y", MOSAIC_BUTTON_Y },
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
};

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, replace_launch) || !MOSAIC_HOST_HAS(host, choose)) {
		say("this launcher can't replace a launch or show a choice list");
		return 1;
	}
	snprintf(state_dir, sizeof(state_dir), "%s/launcher", host->userdata_path);
	snprintf(map_path, sizeof(map_path), "%s/emulator-override.txt", state_dir);
	EO_mapInit(&map);
	memset(&map_seen, 0, sizeof(map_seen));
	map_seen.st_size = -1; // so the first refresh reads the file
	refreshMap();
	forgetCheck();
	picking[0] = '\0';
	list_all = strcmp(host->cfg_get("all", "off"), "on") == 0;

	const char* name = host->cfg_get("button", "Y");
	int button = MOSAIC_BUTTON_Y;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (!strcmp(name, buttons[i].name)) button = buttons[i].button;
	}
	if (MOSAIC_HOST_HAS(host, call) && host->provide("emulator.choice", provideChoice) != 0) say("couldn't provide emulator.choice");
	if (host->register_action(ID ".choose", "EMULATOR", canChoose, choose, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, ID ".choose") != 0) {
		say("couldn't bind " ID ".choose");
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.pre_launch = preLaunch,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
