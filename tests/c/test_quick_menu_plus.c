// The quick menu plus plugin against a fake host, without the launcher.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/quick-menu-plus/quick_menu_plus.c"

#define PAK TEST_DIR "/qmp-pak"
#define USERDATA TEST_DIR "/qmp-userdata"
#define RAN TEST_DIR "/qmp-ran"

static const char* labels_setting = NULL;
static const char* hide_setting = NULL;
static const char* awake_setting = NULL;
static const char* stock_setting = NULL;
static const char* toggles_setting = "";

#define MAX_ACTIONS 12
static int action_count = 0;
static char action_id[MAX_ACTIONS][64];
static char action_label[MAX_ACTIONS][64];
static int (*action_available[MAX_ACTIONS])(void*);
static void (*action_run[MAX_ACTIONS])(void*);
static void* action_data[MAX_ACTIONS];
static int tile_count = 0;
static char tile_action[MAX_ACTIONS][64];
static int tile_row[MAX_ACTIONS];
static int (*tile_is_on[MAX_ACTIONS])(void*);

static char renamed[8][2][64];
static int rename_count = 0;
static char hidden[16][64];
static int hidden_count = 0;
static int inhibited = 0, inhibit_calls = 0;
static char asked[160], yes_label[32];
static void (*pending_yes)(void*) = NULL;
static int power_offs = 0, last_reboot = -1;
static char toasted[160];

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeRedraw(void) {}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (labels_setting && !strcmp(key, "labels")) return labels_setting;
	if (hide_setting && !strcmp(key, "hide")) return hide_setting;
	if (awake_setting && !strcmp(key, "awake_tile")) return awake_setting;
	if (stock_setting && !strcmp(key, "stock_tile")) return stock_setting;
	if (toggles_setting && !strcmp(key, "toggles")) return toggles_setting;
	return fallback;
}
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	if (action_count >= MAX_ACTIONS) return -1;
	action_data[action_count] = data;
	snprintf(action_id[action_count], sizeof(action_id[0]), "%s", id);
	snprintf(action_label[action_count], sizeof(action_label[0]), "%s", label);
	action_available[action_count] = available;
	action_run[action_count] = run;
	action_count++;
	return 0;
}
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)icon; (void)icon_off;
	if (tile_count >= MAX_ACTIONS) return -1;
	snprintf(tile_action[tile_count], sizeof(tile_action[0]), "%s", id);
	tile_row[tile_count] = row;
	tile_is_on[tile_count] = is_on;
	tile_count++;
	return 0;
}
static int fakeRename(const char* name, const char* label) {
	if (rename_count >= 8) return -1;
	snprintf(renamed[rename_count][0], 64, "%s", name);
	snprintf(renamed[rename_count][1], 64, "%s", label);
	rename_count++;
	return 0;
}
static int fakeHide(const char* name, int on) {
	if (!on || hidden_count >= 16) return -1;
	snprintf(hidden[hidden_count++], 64, "%s", name);
	return 0;
}
static void fakeInhibit(const char* id, int on) { (void)id; inhibited = on; inhibit_calls++; }
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)data;
	snprintf(asked, sizeof(asked), "%s", message);
	snprintf(yes_label, sizeof(yes_label), "%s", yes);
	pending_yes = on_yes;
}
static void fakePowerOff(int reboot) { power_offs++; last_reboot = reboot; }

// the fake's stock settings; haptics is one this launcher doesn't know
static const char* stock_names[] = { "notify_manual_save", "notify_load", "notify_screenshot", "notify_adjustments", "show_clock", "show_battery_percent" };
static int stock_values[6] = { 1, 1, 0, 1, 1, 0 };
static int refuse_sets = 0, stock_sets = 0;
static int stockIndex(const char* name) {
	for (int i = 0; i < 6; i++) {
		if (!strcmp(stock_names[i], name)) return i;
	}
	return -1;
}
static int fakeStockGet(const char* name) {
	int i = stockIndex(name);
	return i < 0 ? -1 : stock_values[i];
}
static int fakeStockSet(const char* name, int on) {
	int i = stockIndex(name);
	if (i < 0 || (on != 0 && on != 1) || refuse_sets) return -1;
	stock_values[i] = on;
	stock_sets++;
	return 0;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = "/card",
	.userdata_path = USERDATA,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.toast = fakeToast,
	.inhibit_sleep = fakeInhibit,
	.confirm = fakeConfirm,
	.quick_tile = fakeQuickTile,
	.hide_quick_tile = fakeHide,
	.rename_quick_tile = fakeRename,
	.power_off = fakePowerOff,
	.stock_toggle_get = fakeStockGet,
	.stock_toggle_set = fakeStockSet,
};

static int find(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(action_id[i], id)) return i;
	}
	return -1;
}

static void reset(void) {
	action_count = tile_count = rename_count = hidden_count = 0;
	inhibit_calls = power_offs = 0;
	last_reboot = -1;
	pending_yes = NULL;
	toasted[0] = asked[0] = '\0';
}

static void answer(int yes) {
	void (*on_yes)(void*) = pending_yes;
	pending_yes = NULL;
	if (yes && on_yes) on_yes(NULL);
}

static void writeFile(const char* path, const char* text, int mode) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
	chmod(path, mode);
}

// A fake pak whose manage.sh records its arguments and exits with code.
static void makePak(int code) {
	mkdir(PAK, 0755);
	mkdir(PAK "/bin", 0755);
	char script[512];
	snprintf(script, sizeof(script), "#!/bin/sh\necho \"$@\" >\"%s\"\nexit %d\n", RAN, code);
	writeFile(PAK "/bin/manage.sh", script, 0755);
	unlink(RAN);
}

static int ranWith(const char* want) {
	FILE* f = fopen(RAN, "r");
	if (!f) return 0;
	char line[128] = "";
	if (!fgets(line, sizeof(line), f)) line[0] = '\0';
	fclose(f);
	line[strcspn(line, "\n")] = '\0';
	return !strcmp(line, want);
}

static void testModel(void) {
	CHECK_STR(Qmp_builtinName(" pak store ", 0), "Pak Store");
	CHECK_STR(Qmp_builtinName("RECENTS", 1), "Recents");
	CHECK(Qmp_builtinName("Wifi", 1) == NULL);
	CHECK_STR(Qmp_builtinName("wifi", 0), "Wifi");
	CHECK(Qmp_builtinName("Nope", 0) == NULL);
	CHECK(Qmp_builtinName(NULL, 0) == NULL);

	QmpLabel labels[QMP_MAX];
	int skipped = -1;
	CHECK(Qmp_parseLabels("", labels, QMP_MAX, &skipped) == 0 && skipped == 0);
	CHECK(Qmp_parseLabels(NULL, labels, QMP_MAX, NULL) == 0);
	int n = Qmp_parseLabels(" recents = Played ,Games=Library,,Wifi=Net,Tools=,nonsense,Games=All games", labels, QMP_MAX, &skipped);
	CHECK(n == 2);
	CHECK(skipped == 3); // Wifi isn't top row, Tools has no label, nonsense has no =
	CHECK_STR(labels[0].name, "Recents");
	CHECK_STR(labels[0].label, "Played");
	CHECK_STR(labels[1].name, "Games");
	CHECK_STR(labels[1].label, "All games"); // the later pair wins
	CHECK(Qmp_parseLabels("Recents=A,Games=B,Tools=C", labels, 2, &skipped) == 2 && skipped == 1);

	char names[QMP_MAX][QMP_NAME_LEN];
	CHECK(Qmp_parseHidden("", names, QMP_MAX) == 0);
	n = Qmp_parseHidden("pak store, BLUETOOTH ,random-game.tile,Pak Store,,", names, QMP_MAX);
	CHECK(n == 3);
	CHECK_STR(names[0], "Pak Store");
	CHECK_STR(names[1], "Bluetooth");
	CHECK_STR(names[2], "random-game.tile");
	CHECK(Qmp_parseHidden("Wifi,Sleep,Reboot", names, 2) == 2);

	char dir[256];
	CHECK(Qmp_pakFromExe("/mnt/SDCARD/Tools/tg5040/Launcher.pak/bin/tg5040/nextui.elf", dir, sizeof(dir)));
	CHECK_STR(dir, "/mnt/SDCARD/Tools/tg5040/Launcher.pak");
	CHECK(!Qmp_pakFromExe("/mnt/SDCARD/.system/tg5040/bin/nextui.elf", dir, sizeof(dir))); // stock's own
	CHECK(!Qmp_pakFromExe("/build/desktop/launcher.elf", dir, sizeof(dir)));
	CHECK(!Qmp_pakFromExe("/bin/x/nextui.elf", dir, sizeof(dir)));
	CHECK(!Qmp_pakFromExe("relative/bin/x/nextui.elf", dir, sizeof(dir)));
	CHECK(!Qmp_pakFromExe(NULL, dir, sizeof(dir)));

	char cmd[256];
	CHECK(Qmp_manageCommand("/mnt/SDCARD/Tools/tg5040/Launcher.pak", "turn-off", cmd, sizeof(cmd)));
	CHECK_STR(cmd, "'/mnt/SDCARD/Tools/tg5040/Launcher.pak/bin/manage.sh' 'turn-off'");
	CHECK(Qmp_manageCommand("/card/it's.pak", "turn-off", cmd, sizeof(cmd)));
	CHECK_STR(cmd, "'/card/it'\\''s.pak/bin/manage.sh' 'turn-off'");
	CHECK(!Qmp_manageCommand("/card/Launcher.pak", "turn-off", cmd, 20));
	CHECK(!Qmp_manageCommand("", "turn-off", cmd, sizeof(cmd)));

	// every toggle's name is the SDK's
	CHECK_STR(Qmp_toggleFor("save")->name, MOSAIC_TOGGLE_NOTIFY_MANUAL_SAVE);
	CHECK_STR(Qmp_toggleFor("load")->name, MOSAIC_TOGGLE_NOTIFY_LOAD);
	CHECK_STR(Qmp_toggleFor("screenshot")->name, MOSAIC_TOGGLE_NOTIFY_SCREENSHOT);
	CHECK_STR(Qmp_toggleFor("adjustments")->name, MOSAIC_TOGGLE_NOTIFY_ADJUSTMENTS);
	CHECK_STR(Qmp_toggleFor("clock")->name, MOSAIC_TOGGLE_SHOW_CLOCK);
	CHECK_STR(Qmp_toggleFor("battery")->name, MOSAIC_TOGGLE_SHOW_BATTERY_PERCENT);
	CHECK_STR(Qmp_toggleFor("haptics")->name, MOSAIC_TOGGLE_HAPTICS);
	CHECK(Qmp_toggleFor(" CLOCK ") == Qmp_toggleFor("show_clock"));
	CHECK_STR(Qmp_toggleFor("clock")->label, "Clock");
	CHECK(Qmp_toggleFor("wifi") == NULL && Qmp_toggleFor(NULL) == NULL);

	const QmpToggle* picked[QMP_MAX];
	skipped = -1;
	CHECK(Qmp_parseToggles("", picked, QMP_MAX, &skipped) == 0 && skipped == 0);
	n = Qmp_parseToggles("clock, nope, Save,show_clock,,haptics", picked, QMP_MAX, &skipped);
	CHECK(n == 3 && skipped == 1);
	CHECK(picked[0] == Qmp_toggleFor("clock"));
	CHECK(picked[1] == Qmp_toggleFor("save"));
	CHECK(picked[2] == Qmp_toggleFor("haptics"));
	CHECK(Qmp_parseToggles("save, load, clock", picked, 2, NULL) == 2);
}

int main(void) {
	testModel();

	mkdir(TEST_DIR, 0755);
	unlink(QMP_AWAKE_FILE);
	makePak(0);
	setenv("MOSAIC_LAUNCHER_PAK_DIR", PAK, 1);

	// by default: both tiles in the top row, nothing renamed or hidden
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "quick-menu-plus");
	CHECK(p->init(&fake) == 0);
	CHECK(rename_count == 0 && hidden_count == 0);
	int awakeTile = find("quick-menu-plus.awake"), stock = find("quick-menu-plus.stock");
	CHECK(action_count == 2 && awakeTile >= 0 && stock >= 0);
	if (awakeTile < 0 || stock < 0) CHECK_DONE();
	CHECK_STR(action_label[awakeTile], "Stay awake");
	CHECK_STR(action_label[stock], "Reboot to stock");
	CHECK(tile_count == 2 && tile_row[0] == MOSAIC_QUICK_TOP && tile_row[1] == MOSAIC_QUICK_TOP);
	CHECK_STR(tile_action[0], "quick-menu-plus.awake");
	CHECK(tile_is_on[0] != NULL); // a toggle
	CHECK(tile_is_on[1] == NULL);

	// stay awake flips, holds off sleep, and is remembered
	CHECK(action_available[awakeTile](NULL));
	CHECK(!tile_is_on[0](NULL) && inhibit_calls == 0);
	action_run[awakeTile](NULL);
	CHECK(tile_is_on[0](NULL) && inhibited == 1);
	CHECK_STR(toasted, "Staying awake");
	CHECK(access(QMP_AWAKE_FILE, F_OK) == 0);

	// a new start, as after a game, keeps it on
	reset();
	CHECK(p->init(&fake) == 0);
	CHECK(tile_is_on[0](NULL) && inhibited == 1 && inhibit_calls == 1);
	action_run[find("quick-menu-plus.awake")](NULL);
	CHECK(!tile_is_on[0](NULL) && inhibited == 0);
	CHECK_STR(toasted, "Sleeping as usual");
	CHECK(access(QMP_AWAKE_FILE, F_OK) != 0);

	// reboot to stock asks first, and backing out does nothing
	stock = find("quick-menu-plus.stock");
	CHECK(action_available[stock](NULL));
	action_run[stock](NULL);
	CHECK(pending_yes != NULL);
	CHECK_STR(asked, "Reboot to stock NextUI?\nTurn Launcher back on from Tools.");
	CHECK_STR(yes_label, "REBOOT");
	answer(0);
	CHECK(power_offs == 0 && !ranWith("turn-off"));

	// yes turns the launcher off, then reboots
	action_run[stock](NULL);
	answer(1);
	CHECK(ranWith("turn-off"));
	CHECK(power_offs == 1 && last_reboot == 1);

	// when manage.sh fails it doesn't reboot
	makePak(1);
	power_offs = 0;
	action_run[stock](NULL);
	answer(1);
	CHECK(ranWith("turn-off"));
	CHECK(power_offs == 0);
	CHECK_STR(toasted, "Couldn't turn the launcher off");

	// without manage.sh the tile doesn't show
	reset();
	setenv("MOSAIC_LAUNCHER_PAK_DIR", TEST_DIR "/qmp-nowhere", 1);
	p->init(&fake);
	CHECK(!action_available[find("quick-menu-plus.stock")](NULL));

	// the pak_dir turning the launcher on wrote is found too
	reset();
	unsetenv("MOSAIC_LAUNCHER_PAK_DIR");
	mkdir(USERDATA, 0755);
	mkdir(USERDATA "/launcher", 0755);
	writeFile(USERDATA "/launcher/pak_dir", PAK "\n", 0644);
	p->init(&fake);
	CHECK(action_available[find("quick-menu-plus.stock")](NULL));

	// labels and hidden tiles from the settings
	reset();
	labels_setting = "recents=Played, Games=Library, Wifi=Net";
	hide_setting = "pak store, Bluetooth, random-game.tile";
	awake_setting = "off";
	stock_setting = "off";
	CHECK(p->init(&fake) == 0);
	CHECK(rename_count == 2);
	CHECK_STR(renamed[0][0], "Recents");
	CHECK_STR(renamed[0][1], "Played");
	CHECK_STR(renamed[1][0], "Games");
	CHECK_STR(renamed[1][1], "Library");
	CHECK(hidden_count == 3);
	CHECK_STR(hidden[0], "Pak Store");
	CHECK_STR(hidden[1], "Bluetooth");
	CHECK_STR(hidden[2], "random-game.tile");
	CHECK(action_count == 0 && tile_count == 0);

	// an older launcher without quick tiles: loads, does nothing
	reset();
	labels_setting = hide_setting = awake_setting = stock_setting = NULL;
	fake.size = offsetof(MosaicHost, quick_tile);
	CHECK(p->init(&fake) == 0);
	CHECK(action_count == 0 && tile_count == 0 && rename_count == 0);
	fake.size = sizeof(MosaicHost);

	// one without power_off: no Reboot to stock tile
	reset();
	fake.size = offsetof(MosaicHost, power_off);
	CHECK(p->init(&fake) == 0);
	CHECK(find("quick-menu-plus.stock") < 0 && find("quick-menu-plus.awake") >= 0);
	fake.size = sizeof(MosaicHost);

	// the default toggles: a tile for each in-game notification, before
	// Stay awake and Reboot to stock
	reset();
	toggles_setting = NULL;
	CHECK(p->init(&fake) == 0);
	CHECK(action_count == 6 && tile_count == 6);
	CHECK_STR(tile_action[0], "quick-menu-plus.toggle.save");
	CHECK_STR(tile_action[1], "quick-menu-plus.toggle.load");
	CHECK_STR(tile_action[2], "quick-menu-plus.toggle.screenshot");
	CHECK_STR(tile_action[3], "quick-menu-plus.toggle.adjustments");
	CHECK_STR(tile_action[4], "quick-menu-plus.awake");
	CHECK_STR(tile_action[5], "quick-menu-plus.stock");
	CHECK_STR(action_label[0], "Save notices");
	CHECK(tile_row[0] == MOSAIC_QUICK_TOP && tile_is_on[0] != NULL);

	// a tile shows the setting and flips it
	int shot = find("quick-menu-plus.toggle.screenshot");
	CHECK(action_available[shot](action_data[shot]));
	CHECK(!tile_is_on[shot](action_data[shot]));
	action_run[shot](action_data[shot]);
	CHECK(stock_values[2] == 1 && tile_is_on[shot](action_data[shot]));
	CHECK_STR(toasted, "Screenshot notices on");
	action_run[shot](action_data[shot]);
	CHECK(stock_values[2] == 0);
	CHECK_STR(toasted, "Screenshot notices off");

	// a refused change says so and leaves it
	refuse_sets = 1;
	stock_sets = 0;
	action_run[shot](action_data[shot]);
	CHECK(stock_values[2] == 0 && stock_sets == 0);
	CHECK_STR(toasted, "Couldn't change Screenshot notices");
	refuse_sets = 0;

	// the setting picks them, and a name the launcher doesn't know gets none
	reset();
	toggles_setting = "battery, haptics, clock";
	awake_setting = stock_setting = "off";
	CHECK(p->init(&fake) == 0);
	CHECK(tile_count == 2);
	CHECK_STR(tile_action[0], "quick-menu-plus.toggle.battery");
	CHECK_STR(tile_action[1], "quick-menu-plus.toggle.clock");
	CHECK_STR(action_label[0], "Battery %");
	CHECK(!tile_is_on[0](action_data[0]) && tile_is_on[1](action_data[1]));

	// a launcher without stock toggles: the other tiles only
	reset();
	awake_setting = stock_setting = NULL;
	fake.size = offsetof(MosaicHost, stock_toggle_get);
	CHECK(p->init(&fake) == 0);
	CHECK(tile_count == 2 && find("quick-menu-plus.toggle.clock") < 0);
	fake.size = sizeof(MosaicHost);

	unlink(QMP_AWAKE_FILE);
	CHECK_DONE();
}
