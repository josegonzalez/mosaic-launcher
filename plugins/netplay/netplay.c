// Netplay: host or join a game over the network.
//
// Y on a game opens a choice of Host a game or Join a game. Joining asks
// for the host's address on the keyboard (a name or IPv4 address, with
// :port to use another port), and remembers the last one. Then the game
// launches through its emulator pak with netplay's arguments before the
// game's path.
//
// Stock minarch has no netplay, so plugin.txt has requires=minarch-netplay
// and the loader leaves this plugin off, with that reason in the log,
// until a NextUI release with netplay ships something that id stands for.
// None of this runs until then.
//
// The launch is an assumption to confirm once minarch has netplay: the
// emulator pak's launch.sh (found the way stock finds it) is run as
//
//   '<pak>/launch.sh' '--host' '--port' '<port>' '<game>'
//   '<pak>/launch.sh' '--connect' '<address>' '--port' '<port>' '<game>'
//
// each part single-quoted by the launcher, so the pak takes the game as
// its last argument and passes the ones before it to minarch.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "netplay_model.h"

#define ID "netplay"

static const MosaicHost* host;
static char roms_path[NP_PATH_LEN];
static char picked[NP_PATH_LEN]; // the game Y was pressed on
static NPPending pending;

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

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int port(void) {
	return NP_port(host->cfg_get("port", "55435"), NP_DEFAULT_PORT);
}

// The emulator pak's launch.sh for a game, where stock looks: the
// system's own paks first, then Emus/<platform>/.
static int emulatorFor(const char* path, char* out, size_t size) {
	char tag[128];
	if (NP_emuTag(roms_path, path, tag, sizeof(tag)) != 0) return -1;
	if (snprintf(out, size, "%s/.system/%s/paks/Emus/%s.pak/launch.sh", host->sdcard_path, host->platform, tag) < (int)size
			&& isFile(out)) return 0;
	if (snprintf(out, size, "%s/Emus/%s/%s.pak/launch.sh", host->sdcard_path, host->platform, tag) < (int)size
			&& isFile(out)) return 0;
	return -1;
}

static void launch(int mode, const char* address, int at) {
	if (NP_setPending(&pending, mode, picked, address, at) != 0) {
		say("couldn't keep the netplay choice; the path or address is too long");
		return;
	}
	if (!host->open_path(picked, MOSAIC_ENTRY_ROM)) memset(&pending, 0, sizeof(pending));
}

static void addressTyped(const char* text, void* data) {
	(void)data;
	if (!text) return;
	char address[NP_HOST_LEN];
	int at;
	if (NP_parseAddress(text, port(), address, sizeof(address), &at) != 0) {
		toast(tr("Type a host name or IP address, with :port for another port"));
		return;
	}
	host->cfg_set("last_address", text);
	launch(NP_JOIN, address, at);
}

static void modePicked(int index, void* data) {
	(void)data;
	if (index == 0) launch(NP_HOST, NULL, port());
	else if (index == 1) host->keyboard(tr("Host address"), host->cfg_get("last_address", ""), addressTyped, NULL);
}

static int canOpen(void* data) {
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	char tag[128];
	return entry && host->entry_type(entry) == MOSAIC_ENTRY_ROM
		&& NP_emuTag(roms_path, host->entry_path(entry), tag, sizeof(tag)) == 0;
}

static void open_(void* data) {
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	if (!entry || snprintf(picked, sizeof(picked), "%s", host->entry_path(entry)) >= (int)sizeof(picked)) return;
	const char* choices[] = { tr("Host a game"), tr("Join a game") };
	int count = MOSAIC_HOST_HAS(host, keyboard) ? 2 : 1;
	if (host->choose(tr("Netplay"), choices, count, 0, modePicked, NULL) != 0) say("couldn't open the netplay choice");
}

static int preLaunch(const MosaicLaunch* l) {
	NPPending choice;
	if (NP_take(&pending, l->path, l->type == MOSAIC_ENTRY_ROM, &choice) == NP_NONE) return MOSAIC_LAUNCH_CONTINUE;

	char program[NP_PATH_LEN * 2];
	if (emulatorFor(l->path, program, sizeof(program)) != 0) {
		say("no emulator pak for this game; netplay launch cancelled");
		toast(tr("No emulator for netplay"));
		return MOSAIC_LAUNCH_CANCEL;
	}

	char args[NP_ARGS_MAX][NP_ARG_LEN];
	const char* argv[NP_ARGS_MAX];
	int n = NP_args(&choice, args, NP_ARGS_MAX);
	for (int i = 0; i < n; i++) argv[i] = args[i];
	if (n < 0 || host->replace_launch(program, argv, n) != 0) {
		say("couldn't replace the launch, maybe another plugin already did; netplay launch cancelled");
		toast(tr("Netplay couldn't start this game"));
		return MOSAIC_LAUNCH_CANCEL;
	}
	return MOSAIC_LAUNCH_REPLACE;
}

static int init(const MosaicHost* h) {
	host = h;
	memset(&pending, 0, sizeof(pending));
	picked[0] = '\0';
	snprintf(roms_path, sizeof(roms_path), "%s/Roms", host->sdcard_path);

	if (!MOSAIC_HOST_HAS(host, open_path) || !MOSAIC_HOST_HAS(host, choose) || !MOSAIC_HOST_HAS(host, replace_launch)) {
		say("this launcher can't open, choose or replace launches for plugins; not loading");
		return 1;
	}
	if (!MOSAIC_HOST_HAS(host, keyboard)) say("this launcher has no keyboard; only hosting is offered");
	if (host->register_action("netplay.open", "NETPLAY", canOpen, open_, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "netplay.open") != 0) {
		say("couldn't bind Y; not loading");
		return 1;
	}
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
