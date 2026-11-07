// Quick menu plus: relabels and hides quick menu tiles, and adds toggle
// tiles for stock settings, a Stay awake toggle and a Reboot to stock tile.
//
// The labels setting renames the top row's built-in tiles, eg.
// "Recents=Played, Games=Library", and the hide setting hides tiles by
// name, built-in or a plugin tile's action id, eg. "Pak Store, Bluetooth".
//
// The toggles setting picks which of stock's on/off settings get a tile,
// eg. "save, load, clock": the in-game notifications, the clock, the
// battery percentage and haptics. A tile flips the setting the way stock's
// Settings app does.
//
// Stay awake holds off auto-sleep while it's on, until the device reboots
// or powers off; a file in /tmp remembers it across games.
//
// Reboot to stock turns the launcher off with the pak's manage.sh, then
// reboots, so the device comes back on stock NextUI's launcher. It asks
// first, and shows only when manage.sh is found.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "quick_model.h"

#define ID "quick-menu-plus"
#define MAX_PATH_LEN 1024
// as in plugin.txt: the in-game notifications
#define QMP_TOGGLES_DEFAULT "save, load, screenshot, adjustments"

#ifndef QMP_AWAKE_FILE
#define QMP_AWAKE_FILE "/tmp/quick-menu-plus.awake"
#endif

static const MosaicHost* host;
static int awake = 0;
static char pak[MAX_PATH_LEN]; // Launcher.pak, or "" when manage.sh isn't found

static void say(const char* message) {
	host->log(ID, message);
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int isOn(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(ID, key, fallback), "off") != 0;
}

///////////////////////////////////////
// labels and hidden tiles

static void applyLabels(void) {
	const char* setting = host->cfg_get(ID, "labels", "");
	if (!setting[0]) return;
	if (!MOSAIC_HOST_HAS(host, rename_quick_tile)) {
		say("this launcher can't relabel quick menu tiles");
		return;
	}
	QmpLabel labels[QMP_MAX];
	int skipped = 0;
	int count = Qmp_parseLabels(setting, labels, QMP_MAX, &skipped);
	for (int i = 0; i < count; i++) {
		if (host->rename_quick_tile(labels[i].name, labels[i].label) != 0) {
			char message[200];
			snprintf(message, sizeof(message), "couldn't relabel %s", labels[i].name);
			say(message);
		}
	}
	if (skipped) {
		char message[200];
		snprintf(message, sizeof(message), "left out %d label(s): only Recents, Collections, Games and Tools take one, as name=label", skipped);
		say(message);
	}
}

static void applyHidden(void) {
	const char* setting = host->cfg_get(ID, "hide", "");
	if (!setting[0]) return;
	if (!MOSAIC_HOST_HAS(host, hide_quick_tile)) {
		say("this launcher can't hide quick menu tiles");
		return;
	}
	char names[QMP_MAX][QMP_NAME_LEN];
	int count = Qmp_parseHidden(setting, names, QMP_MAX);
	for (int i = 0; i < count; i++) {
		if (host->hide_quick_tile(names[i], 1) != 0) {
			char message[200];
			snprintf(message, sizeof(message), "couldn't hide %s", names[i]);
			say(message);
		}
	}
}

///////////////////////////////////////
// stock toggles

static int stockOn(void* data) {
	const QmpToggle* toggle = data;
	return host->stock_toggle_get(toggle->name) == 1;
}

static void flipStock(void* data) {
	const QmpToggle* toggle = data;
	int on = !stockOn(data);
	char message[200];
	if (host->stock_toggle_set(toggle->name, on) != 0) {
		snprintf(message, sizeof(message), "Couldn't change %s", toggle->label);
		toast(message);
		return;
	}
	snprintf(message, sizeof(message), "%s %s", toggle->label, on ? "on" : "off");
	toast(message);
	host->request_redraw();
}

///////////////////////////////////////
// stay awake

static int awakeOn(void* data) {
	(void)data;
	return awake;
}

static int always(void* data) {
	(void)data;
	return 1;
}

static void setAwake(int on) {
	awake = on;
	host->inhibit_sleep(ID, on);
	if (on) {
		FILE* f = fopen(QMP_AWAKE_FILE, "w");
		if (f) fclose(f);
	}
	else unlink(QMP_AWAKE_FILE);
}

static void toggleAwake(void* data) {
	(void)data;
	setAwake(!awake);
	toast(awake ? "Staying awake" : "Sleeping as usual");
	host->request_redraw();
}

///////////////////////////////////////
// reboot to stock

static int hasManage(const char* dir) {
	char script[MAX_PATH_LEN + 32];
	snprintf(script, sizeof(script), "%s/bin/manage.sh", dir);
	return access(script, X_OK) == 0;
}

// Where Launcher.pak is, the way the launcher finds it: the override in
// MOSAIC_LAUNCHER_PAK_DIR, then the running binary, then the pak_dir that
// turning the launcher on wrote.
static void findPak(void) {
	pak[0] = '\0';
	const char* override = getenv("MOSAIC_LAUNCHER_PAK_DIR");
	if (override && override[0]) {
		if (hasManage(override)) snprintf(pak, sizeof(pak), "%s", override);
		return;
	}
	char exe[MAX_PATH_LEN];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len > 0) {
		exe[len] = '\0';
		char dir[MAX_PATH_LEN];
		if (Qmp_pakFromExe(exe, dir, sizeof(dir)) && hasManage(dir)) {
			snprintf(pak, sizeof(pak), "%s", dir);
			return;
		}
	}
	char saved[MAX_PATH_LEN];
	snprintf(saved, sizeof(saved), "%s/launcher/pak_dir", host->userdata_path);
	FILE* f = fopen(saved, "r");
	if (!f) return;
	char dir[MAX_PATH_LEN];
	if (fgets(dir, sizeof(dir), f)) {
		dir[strcspn(dir, "\r\n")] = '\0';
		if (dir[0] && hasManage(dir)) snprintf(pak, sizeof(pak), "%s", dir);
	}
	fclose(f);
}

static int canRebootToStock(void* data) {
	(void)data;
	return pak[0] != '\0';
}

static void rebootToStock(void* data) {
	(void)data;
	char cmd[MAX_PATH_LEN * 2];
	if (!Qmp_manageCommand(pak, "turn-off", cmd, sizeof(cmd))) {
		toast("Couldn't turn the launcher off");
		return;
	}
	int rc = system(cmd);
	if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
		say("manage.sh turn-off failed; not rebooting");
		toast("Couldn't turn the launcher off");
		return;
	}
	say("launcher turned off; rebooting to stock");
	host->power_off(1);
}

static void askRebootToStock(void* data) {
	(void)data;
	if (!pak[0]) return;
	host->confirm("Reboot to stock NextUI?\nTurn Launcher back on from Tools.", "REBOOT", rebootToStock, NULL);
}

///////////////////////////////////////
// hooks

static void addTile(const char* id, const char* label, int (*available)(void*), void (*run)(void*), int (*is_on)(void*), void* data) {
	if (host->register_action(id, label, available, run, data) != 0
			|| host->quick_tile(id, MOSAIC_QUICK_TOP, NULL, NULL, is_on) != 0) {
		char message[200];
		snprintf(message, sizeof(message), "couldn't add the %s tile", label);
		say(message);
	}
}

// A tile per toggle the toggles setting names that the launcher knows.
static void addStockToggles(void) {
	const char* setting = host->cfg_get(ID, "toggles", QMP_TOGGLES_DEFAULT);
	if (!setting[0]) return;
	if (!MOSAIC_HOST_HAS(host, stock_toggle_get) || !MOSAIC_HOST_HAS(host, stock_toggle_set)) {
		say("this launcher can't change stock settings; no toggle tiles");
		return;
	}
	const QmpToggle* toggles[QMP_MAX];
	int skipped = 0;
	int count = Qmp_parseToggles(setting, toggles, QMP_MAX, &skipped);
	char message[200];
	if (skipped) {
		snprintf(message, sizeof(message), "left out %d unknown toggle(s)", skipped);
		say(message);
	}
	for (int i = 0; i < count; i++) {
		if (host->stock_toggle_get(toggles[i]->name) < 0) {
			snprintf(message, sizeof(message), "this launcher doesn't know %s; no tile", toggles[i]->name);
			say(message);
			continue;
		}
		char id[QMP_NAME_LEN + 32];
		snprintf(id, sizeof(id), ID ".toggle.%s", toggles[i]->word);
		addTile(id, toggles[i]->label, always, flipStock, stockOn, (void*)toggles[i]);
	}
}

static int init(const MosaicHost* h) {
	host = h;
	awake = 0;
	applyLabels();
	applyHidden();

	if (!MOSAIC_HOST_HAS(host, quick_tile)) {
		say("this launcher can't add quick menu tiles");
		return 0;
	}

	addStockToggles();

	if (isOn("awake_tile", "on")) {
		if (!MOSAIC_HOST_HAS(host, inhibit_sleep)) say("this launcher can't hold off sleep; no Stay awake tile");
		else {
			addTile(ID ".awake", "Stay awake", always, toggleAwake, awakeOn, NULL);
			if (access(QMP_AWAKE_FILE, F_OK) == 0) setAwake(1);
		}
	}

	if (isOn("stock_tile", "on")) {
		if (!MOSAIC_HOST_HAS(host, power_off) || !MOSAIC_HOST_HAS(host, confirm)) {
			say("this launcher can't reboot for plugins; no Reboot to stock tile");
		}
		else {
			findPak();
			if (!pak[0]) say("manage.sh not found; no Reboot to stock tile");
			addTile(ID ".stock", "Reboot to stock", canRebootToStock, askRebootToStock, NULL, NULL);
		}
	}
	return 0;
}

static void quit(void) {
	if (awake) host->inhibit_sleep(ID, 0);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
