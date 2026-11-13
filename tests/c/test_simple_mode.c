// The simple mode plugin: its model on its own, then the plugin against a
// fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/simple-mode/simple_mode.c"

struct MosaicEntry {
	char path[256];
};

#define CARD "/sd"

// simple mode's cfg
static char cfg_on[8] = "";
static char cfg_settings[8] = "";
static char cfg_pin[SIMPLE_PIN_LEN] = "";
static int has_pin = 0;
static char last_action[64], last_label[64];
static void (*action_run)(const char*, void*) = NULL;
static int (*tile_is_on)(void*) = NULL;
static int tile_row = -1;
static char hidden[16][32];
static int hidden_count = 0;
static int reloads = 0;
static char toasted[64];
static void (*keyboard_done)(const char*, void*) = NULL;
static void (*confirm_yes)(void*) = NULL;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "on") && cfg_on[0]) return cfg_on;
	if (!strcmp(key, "settings") && cfg_settings[0]) return cfg_settings;
	if (!strcmp(key, "pin") && has_pin) return cfg_pin;
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) {
	if (!strcmp(key, "on")) snprintf(cfg_on, sizeof(cfg_on), "%s", value);
	// the plugin never writes the PIN, or anything else
	else CHECK(0);
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static void fakeRedraw(void) {}
static int fakeRegister(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)available; (void)data;
	snprintf(last_action, sizeof(last_action), "%s", id);
	snprintf(last_label, sizeof(last_label), "%s", label);
	action_run = run;
	return 0;
}
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)id; (void)icon; (void)icon_off;
	tile_row = row;
	tile_is_on = is_on;
	return 0;
}
static int hiddenAt(const char* name) {
	for (int i = 0; i < hidden_count; i++) {
		if (!strcmp(hidden[i], name)) return i;
	}
	return -1;
}
static int fakeHideQuickTile(const char* name, int hide) {
	int at = hiddenAt(name);
	if (hide && at < 0) snprintf(hidden[hidden_count++], sizeof(hidden[0]), "%s", name);
	else if (!hide && at >= 0) {
		hidden_count--;
		memcpy(hidden[at], hidden[hidden_count], sizeof(hidden[0]));
	}
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeReload(void) { reloads++; }
static void fakeKeyboard(const char* title, const char* initial, void (*on_done)(const char*, void*), void* data) {
	(void)title; (void)initial; (void)data;
	keyboard_done = on_done;
}
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)message; (void)yes; (void)data;
	confirm_yes = on_yes;
}

// the launcher's PIN, for a host with ask_pin
static int host_pin = 0;
static char asked_title[64];
static void (*pin_done)(int, void*) = NULL;
static int fakeHasPin(void) { return host_pin; }
static void fakeAskPin(const char* title, void (*on_done)(int, void*), void* data) {
	snprintf(asked_title, sizeof(asked_title), "%s", title);
	if (!host_pin) on_done(MOSAIC_PIN_RIGHT, data);
	else pin_done = on_done;
}

static const MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/test",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
	.entry_path = fakeEntryPath,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.toast = fakeToast,
	.confirm = fakeConfirm,
	.keyboard = fakeKeyboard,
	.reload_lists = fakeReload,
	.quick_tile = fakeQuickTile,
	.hide_quick_tile = fakeHideQuickTile,
};

///////////////////////////////////////
// the model

static void testModel(void) {
	CHECK(Simple_isOn(NULL));
	CHECK(Simple_isOn("on"));
	CHECK(Simple_isOn(""));
	CHECK(!Simple_isOn("off"));

	CHECK(Simple_isTools("/sd/Tools/tg5040", "/sd", "tg5040"));
	CHECK(Simple_isTools("/sd/Tools/tg5040/Launcher.pak", "/sd", "tg5040"));
	CHECK(!Simple_isTools("/sd/Tools/tg5040x", "/sd", "tg5040"));
	CHECK(!Simple_isTools("/sd/Tools", "/sd", "tg5040"));
	CHECK(!Simple_isTools("/sd/Tools/h700/Clock.pak", "/sd", "tg5040"));
	CHECK(!Simple_isTools("/sd/Roms/GB/Tools.gb", "/sd", "tg5040"));
	CHECK(!Simple_isTools(NULL, "/sd", "tg5040"));

	const char* names[SIMPLE_TILES_MAX];
	CHECK(Simple_tiles(0, names) == 2);
	CHECK_STR(names[0], "Tools");
	CHECK_STR(names[1], "Pak Store");
	CHECK(Simple_tiles(1, names) == 6);
	CHECK_STR(names[2], "Settings");
	CHECK_STR(names[5], "Reboot");

	char pin[SIMPLE_PIN_LEN];
	Simple_cleanPin(" 1234\r\n", pin);
	CHECK_STR(pin, "1234");
	Simple_cleanPin(NULL, pin);
	CHECK_STR(pin, "");
	Simple_cleanPin(" \t ", pin);
	CHECK_STR(pin, "");
	char lng[200];
	memset(lng, '7', sizeof(lng) - 1);
	lng[sizeof(lng) - 1] = '\0';
	Simple_cleanPin(lng, pin);
	CHECK(strlen(pin) == SIMPLE_PIN_LEN - 1);

	CHECK(Simple_leaveStep(NULL) == SIMPLE_LEAVE_CONFIRM);
	CHECK(Simple_leaveStep("  ") == SIMPLE_LEAVE_CONFIRM);
	CHECK(Simple_leaveStep("1234") == SIMPLE_LEAVE_PIN);

	CHECK(Simple_pinMatches("1234", "1234"));
	CHECK(Simple_pinMatches(" 1234", "1234 "));
	CHECK(!Simple_pinMatches("1234", "123"));
	CHECK(!Simple_pinMatches("1234", NULL));
	CHECK(!Simple_pinMatches("", ""));
	CHECK(!Simple_pinMatches(NULL, "1234"));
}

///////////////////////////////////////
// the plugin

static MosaicEntry entry(const char* path) {
	MosaicEntry e;
	snprintf(e.path, sizeof(e.path), "%s", path);
	return e;
}

static void testPlugin(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK_STR(p->id, "simple-mode");
	CHECK(p->init(&fake) == 0);
	CHECK_STR(last_action, "simple-mode.toggle");
	CHECK_STR(last_label, "Simple mode");
	CHECK(tile_row == MOSAIC_QUICK_BOTTOM);

	// on by default, hiding Tools and the settings tiles
	CHECK(tile_is_on(NULL));
	CHECK(hidden_count == 6);
	CHECK(hiddenAt("Tools") >= 0 && hiddenAt("Pak Store") >= 0 && hiddenAt("Wifi") >= 0);
	CHECK(hiddenAt("Sleep") < 0 && hiddenAt("Poweroff") < 0 && hiddenAt("Recents") < 0);

	MosaicEntry tools = entry(CARD "/Tools/test");
	MosaicEntry pak = entry(CARD "/Tools/test/Launcher.pak");
	MosaicEntry game = entry(CARD "/Roms/GB/Tetris.gb");
	CHECK(p->filter(&tools, CARD) == 0);
	CHECK(p->filter(&pak, CARD "/Recently Played") == 0);
	CHECK(p->filter(&game, CARD) == 1);

	MosaicLaunch launch = { CARD "/Tools/test/Launcher.pak", MOSAIC_ENTRY_PAK };
	CHECK(p->pre_launch(&launch) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(toasted, "Tools are off in simple mode");
	MosaicLaunch rom = { CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM };
	CHECK(p->pre_launch(&rom) == MOSAIC_LAUNCH_CONTINUE);

	// no PIN: the tile asks first, then leaves
	action_run(NULL, NULL);
	CHECK(confirm_yes != NULL);
	CHECK(tile_is_on(NULL));
	confirm_yes(NULL);
	CHECK_STR(cfg_on, "off");
	CHECK(!tile_is_on(NULL));
	CHECK(hidden_count == 0);
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Simple mode off");
	CHECK(p->filter(&tools, CARD) == 1);
	CHECK(p->pre_launch(&launch) == MOSAIC_LAUNCH_CONTINUE);

	// turning it on needs nothing; without the settings tiles only Tools goes
	snprintf(cfg_settings, sizeof(cfg_settings), "off");
	confirm_yes = NULL;
	action_run(NULL, NULL);
	CHECK(confirm_yes == NULL);
	CHECK_STR(cfg_on, "on");
	CHECK(hidden_count == 2);
	CHECK(hiddenAt("Wifi") < 0);
	CHECK(reloads == 2);

	// with a PIN: a wrong one stays, a cancel stays, the right one leaves
	snprintf(cfg_pin, sizeof(cfg_pin), "4321");
	has_pin = 1;
	action_run(NULL, NULL);
	CHECK(confirm_yes == NULL);
	CHECK(keyboard_done != NULL);
	keyboard_done("1234", NULL);
	CHECK_STR(toasted, "Wrong PIN");
	CHECK_STR(cfg_on, "on");
	keyboard_done(NULL, NULL);
	CHECK_STR(cfg_on, "on");
	CHECK(reloads == 2);
	keyboard_done(" 4321 ", NULL);
	CHECK_STR(cfg_on, "off");
	CHECK(hidden_count == 0);
	CHECK(reloads == 3);

	// the setting turned off in the Launcher pak: nothing hidden at start
	snprintf(cfg_on, sizeof(cfg_on), "off");
	CHECK(p->init(&fake) == 0);
	CHECK(hidden_count == 0);
	CHECK(p->filter(&pak, CARD "/Tools/test") == 1);
}

// On a launcher with ask_pin, the host's PIN, never the cfg's.
static void testHostPin(void) {
	MosaicHost with_pin = fake;
	with_pin.has_pin = fakeHasPin;
	with_pin.ask_pin = fakeAskPin;
	const MosaicPlugin* p = mosaic_plugin_get(&with_pin);
	snprintf(cfg_on, sizeof(cfg_on), "on");
	snprintf(cfg_pin, sizeof(cfg_pin), "4321"); // ignored
	has_pin = 1;
	CHECK(p->init(&with_pin) == 0);

	// no PIN on the host: a confirm, as before, whatever the cfg says
	keyboard_done = NULL;
	confirm_yes = NULL;
	action_run(NULL, NULL);
	CHECK(keyboard_done == NULL);
	CHECK(confirm_yes != NULL);
	confirm_yes(NULL);
	CHECK_STR(cfg_on, "off");

	// a PIN on the host: wrong stays, cancelled stays, right leaves
	action_run(NULL, NULL);
	CHECK_STR(cfg_on, "on");
	host_pin = 1;
	confirm_yes = NULL;
	action_run(NULL, NULL);
	CHECK(confirm_yes == NULL);
	CHECK(keyboard_done == NULL);
	CHECK_STR(asked_title, "PIN to leave simple mode");
	CHECK(pin_done != NULL);
	toasted[0] = '\0';
	pin_done(MOSAIC_PIN_WRONG, NULL);
	CHECK_STR(toasted, "Wrong PIN");
	CHECK_STR(cfg_on, "on");
	toasted[0] = '\0';
	pin_done(MOSAIC_PIN_CANCELLED, NULL);
	CHECK_STR(toasted, "");
	CHECK_STR(cfg_on, "on");
	pin_done(MOSAIC_PIN_RIGHT, NULL);
	CHECK_STR(cfg_on, "off");
	has_pin = 0;
}

int main(void) {
	testModel();
	testPlugin();
	testHostPin();
	CHECK_DONE();
}
