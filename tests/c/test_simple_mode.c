// The simple mode plugin: its model on its own, then the plugin against a
// fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/simple-mode/simple_mode.c"

struct MosaicEntry {
	char path[256];
	int type;
};

#define CARD "/sd"

// simple mode's cfg
static char cfg_on[8] = "";
static char cfg_settings[8] = "";
static char cfg_pin[SIMPLE_PIN_LEN] = "";
static char cfg_kid[64] = "";
static char cfg_kiosk[8] = "";
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
	if (!strcmp(key, "kid") && cfg_kid[0]) return cfg_kid;
	if (!strcmp(key, "kiosk") && cfg_kiosk[0]) return cfg_kiosk;
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) {
	if (!strcmp(key, "on")) snprintf(cfg_on, sizeof(cfg_on), "%s", value);
	// the plugin never writes the PIN, or anything else
	else CHECK(0);
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
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

// the tags plugin's sets, one tag at most, and the recents
static char set_name[64] = "";
static const char* set_games[8];
static int set_games_count = 0;
static int set_lists = 0;
static int fakeSetList(const char* name, void (*each)(const char*, void*), void* data) {
	set_lists++;
	if (!set_name[0] || strcmp(name, set_name) != 0) return -1;
	for (int i = 0; i < set_games_count; i++) if (each) each(set_games[i], data);
	return set_games_count;
}
static int fakeSetNames(void (*each)(const char*, void*), void* data) {
	if (!set_name[0]) return 0;
	if (each) each(set_name, data);
	return 1;
}
static int fakeIsFiltered(const char* path) { (void)path; return 0; }
static void (*set_changed)(const char*, const char*, void*) = NULL;
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	CHECK_STR(name, "launcher.set_changed");
	set_changed = on_event;
	return 0;
}
static int fakeEventValue(const char* payload, const char* key, char* out, size_t size) {
	size_t len = strlen(key);
	if (!payload || strncmp(payload, key, len) != 0 || payload[len] != '=') return -1;
	snprintf(out, size, "%s", payload + len + 1);
	return (int)strlen(payload + len + 1);
}
static const char* recents[4];
static int recents_n = 0;
static int fakeRecentsCount(void) { return recents_n; }
static const char* fakeRecentPath(int i) { return i >= 0 && i < recents_n ? recents[i] : NULL; }

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
	.entry_type = fakeEntryType,
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

	// the kid tag's games
	KidList kid = { 0 };
	CHECK(Kid_add(&kid, "/sd/Roms/Game Boy (GB)/Tetris.gb") == 0);
	CHECK(Kid_add(&kid, "/sd/Roms/Sony PlayStation (PS)/Final Fantasy VII/Final Fantasy VII.m3u") == 0);
	CHECK(Kid_add(&kid, "/sd/Roms/Ports (PORTS)/Doom.pak") == 0);
	CHECK(Kid_allows(&kid, "/sd/Roms/Game Boy (GB)/Tetris.gb"));
	CHECK(!Kid_allows(&kid, "/sd/Roms/Game Boy (GB)/Tetris 2.gb"));
	CHECK(!Kid_allows(&kid, "/sd/Roms/Game Boy (GB)/Tetris.g"));
	// the folders it's in, but not a folder whose name only starts the same
	CHECK(Kid_allows(&kid, "/sd/Roms/Game Boy (GB)"));
	CHECK(Kid_allows(&kid, "/sd/Roms"));
	CHECK(!Kid_allows(&kid, "/sd/Roms/Game Boy"));
	CHECK(!Kid_allows(&kid, "/sd/Roms/Game Boy Color (GBC)"));
	// a multi-disc game: its folder, its .m3u and its discs
	CHECK(Kid_allows(&kid, "/sd/Roms/Sony PlayStation (PS)/Final Fantasy VII"));
	CHECK(Kid_allows(&kid, "/sd/Roms/Sony PlayStation (PS)/Final Fantasy VII/Final Fantasy VII (Disc 2).cue"));
	CHECK(!Kid_allows(&kid, "/sd/Roms/Sony PlayStation (PS)/Crash.cue"));
	CHECK(Kid_allows(&kid, "/sd/Roms/Ports (PORTS)/Doom.pak"));
	CHECK(!Kid_allows(&kid, NULL) && !Kid_allows(&kid, ""));
	// a .m3u loose in a system folder isn't a game folder's
	KidList loose = { 0 };
	CHECK(Kid_add(&loose, "/sd/Roms/Sony PlayStation (PS)/Crash.m3u") == 0);
	CHECK(!Kid_allows(&loose, "/sd/Roms/Sony PlayStation (PS)/Spyro.cue"));
	Kid_free(&loose);

	CHECK(Kid_place("/sd/Roms/Game Boy (GB)", MOSAIC_ENTRY_DIR, "/sd") == KID_GAME);
	CHECK(Kid_place("/sd/Roms/Game Boy (GB)/Tetris.gb", MOSAIC_ENTRY_ROM, "/sd") == KID_GAME);
	CHECK(Kid_place("/sd/Roms/Ports (PORTS)/Doom.pak", MOSAIC_ENTRY_PAK, "/sd") == KID_GAME);
	CHECK(Kid_place("/sd/Recently Played", MOSAIC_ENTRY_DIR, "/sd") == KID_RECENTS);
	CHECK(Kid_place("/sd/Collections", MOSAIC_ENTRY_DIR, "/sd") == KID_COLLECTIONS);
	CHECK(Kid_place("/sd/Collections/Mario.txt", MOSAIC_ENTRY_DIR, "/sd") == KID_COLLECTION);
	CHECK(Kid_place("/sd/Collections/map.cfg", MOSAIC_ENTRY_DIR, "/sd") == KID_KEEP);
	CHECK(Kid_place("/sd/Tags", MOSAIC_ENTRY_DIR, "/sd") == KID_KEEP);
	CHECK(Kid_place("/sd/Romsx", MOSAIC_ENTRY_DIR, "/sd") == KID_KEEP);
	CHECK(Kid_place("/sd/Roms", MOSAIC_ENTRY_TOGGLE, "/sd") == KID_KEEP);

	CHECK(Kid_collectionAllows(&kid, "/Roms/Game Boy (GB)/Dr. Mario.gb\n/Roms/Game Boy (GB)/Tetris.gb\r\n", "/sd"));
	CHECK(!Kid_collectionAllows(&kid, "/Roms/Game Boy (GB)/Dr. Mario.gb\n\n", "/sd"));
	CHECK(!Kid_collectionAllows(&kid, "", "/sd"));
	Kid_clear(&kid);
	CHECK(kid.count == 0 && !Kid_allows(&kid, "/sd/Roms"));
	Kid_free(&kid);

	char tag[16];
	CHECK(Kid_tag("  Kids \n", tag, sizeof(tag)) && !strcmp(tag, "Kids"));
	CHECK(!Kid_tag("   ", tag, sizeof(tag)) && !tag[0]);
	CHECK(!Kid_tag(NULL, tag, sizeof(tag)));

	// kiosk
	CHECK(Kiosk_gate(1, 1, MOSAIC_ACTION_EDITS, 1) == MOSAIC_GATE_PIN);
	CHECK(Kiosk_gate(1, 1, MOSAIC_ACTION_EDITS | MOSAIC_ACTION_PATH, 0) == MOSAIC_GATE_REFUSE);
	CHECK(Kiosk_gate(1, 1, MOSAIC_ACTION_PATH, 1) == MOSAIC_GATE_RUN);
	CHECK(Kiosk_gate(1, 0, MOSAIC_ACTION_EDITS, 1) == MOSAIC_GATE_RUN);
	CHECK(Kiosk_gate(0, 1, MOSAIC_ACTION_EDITS, 1) == MOSAIC_GATE_RUN);
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

static MosaicEntry typed(const char* path, int type) {
	MosaicEntry e = entry(path);
	e.type = type;
	return e;
}

// The kid tag, against the tags plugin's set.
static void testKid(void) {
	MosaicHost with_sets = fake;
	with_sets.set_list = fakeSetList;
	with_sets.set_names = fakeSetNames;
	with_sets.is_filtered = fakeIsFiltered;
	with_sets.subscribe = fakeSubscribe;
	with_sets.event_value = fakeEventValue;
	with_sets.recents_count = fakeRecentsCount;
	with_sets.recent_path = fakeRecentPath;
	const MosaicPlugin* p = mosaic_plugin_get(&with_sets);
	snprintf(cfg_on, sizeof(cfg_on), "on");
	CHECK(p->init(&with_sets) == 0);
	CHECK(set_changed != NULL);

	MosaicEntry gb = typed(CARD "/Roms/Game Boy (GB)", MOSAIC_ENTRY_DIR);
	MosaicEntry nes = typed(CARD "/Roms/Nintendo (FC)", MOSAIC_ENTRY_DIR);
	MosaicEntry tetris = typed(CARD "/Roms/Game Boy (GB)/Tetris.gb", MOSAIC_ENTRY_ROM);
	MosaicEntry mario = typed(CARD "/Roms/Game Boy (GB)/Dr. Mario.gb", MOSAIC_ENTRY_ROM);
	MosaicEntry recent = typed(CARD "/Recently Played", MOSAIC_ENTRY_DIR);
	MosaicEntry tags = typed(CARD "/Tags", MOSAIC_ENTRY_DIR);

	// no kid tag: everything but Tools, as before
	CHECK(p->filter(&nes, CARD) == 1 && p->filter(&mario, CARD) == 1);
	CHECK(set_lists == 0);

	// a kid tag: only its games, and the folders they're in
	snprintf(cfg_kid, sizeof(cfg_kid), " Kids ");
	snprintf(set_name, sizeof(set_name), "tags:Kids");
	set_games[0] = CARD "/Roms/Game Boy (GB)/Tetris.gb";
	set_games_count = 1;
	CHECK(p->filter(&gb, CARD) == 1);
	CHECK(p->filter(&nes, CARD) == 0);
	CHECK(p->filter(&tetris, CARD "/Roms/Game Boy (GB)") == 1);
	CHECK(p->filter(&mario, CARD "/Roms/Game Boy (GB)") == 0);
	CHECK(p->filter(&tags, CARD) == 1);
	CHECK(set_lists == 1); // read once, not per entry
	recents_n = 0;
	CHECK(p->filter(&recent, CARD) == 0);
	recents[0] = CARD "/Roms/Game Boy (GB)/Dr. Mario.gb";
	recents_n = 1;
	CHECK(p->filter(&recent, CARD) == 0);
	recents[1] = CARD "/Roms/Game Boy (GB)/Tetris.gb";
	recents_n = 2;
	CHECK(p->filter(&recent, CARD) == 1);

	// the switcher can't start a game without the tag
	MosaicLaunch other = { CARD "/Roms/Game Boy (GB)/Dr. Mario.gb", MOSAIC_ENTRY_ROM };
	MosaicLaunch allowed = { CARD "/Roms/Game Boy (GB)/Tetris.gb", MOSAIC_ENTRY_ROM };
	CHECK(p->pre_launch(&other) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(toasted, "That game is off in simple mode");
	CHECK(p->pre_launch(&allowed) == MOSAIC_LAUNCH_CONTINUE);

	// tagged again: read on the set's change, not before
	set_games[1] = CARD "/Roms/Game Boy (GB)/Dr. Mario.gb";
	set_games_count = 2;
	CHECK(p->filter(&mario, CARD "/Roms/Game Boy (GB)") == 0);
	set_changed("launcher.set_changed", "name=favorites", NULL);
	CHECK(p->filter(&mario, CARD "/Roms/Game Boy (GB)") == 0);
	set_changed("launcher.set_changed", "name=tags:Kids", NULL);
	CHECK(p->filter(&mario, CARD "/Roms/Game Boy (GB)") == 1);

	// a tag named the same but for case
	snprintf(cfg_kid, sizeof(cfg_kid), "kids");
	CHECK(p->filter(&mario, CARD "/Roms/Game Boy (GB)") == 1);
	// no such tag: no games at all
	snprintf(cfg_kid, sizeof(cfg_kid), "Other");
	CHECK(p->filter(&tetris, CARD "/Roms/Game Boy (GB)") == 0);
	CHECK(p->filter(&gb, CARD) == 0);

	// simple mode off: everything, whatever the tag
	snprintf(cfg_on, sizeof(cfg_on), "off");
	CHECK(p->filter(&nes, CARD) == 1);
	CHECK(p->pre_launch(&other) == MOSAIC_LAUNCH_CONTINUE);
	p->quit();
	cfg_kid[0] = '\0';
	set_name[0] = '\0';
	recents_n = 0;
}

// Kiosk: editing actions behind the PIN, and refused without one.
static void testKiosk(void) {
	MosaicHost with_pin = fake;
	with_pin.has_pin = fakeHasPin;
	with_pin.ask_pin = fakeAskPin;
	const MosaicPlugin* p = mosaic_plugin_get(&with_pin);
	snprintf(cfg_on, sizeof(cfg_on), "on");
	CHECK(p->init(&with_pin) == 0);
	CHECK(p->action_gate != NULL);

	// off by default
	CHECK(p->action_gate("tags.edit", MOSAIC_ACTION_EDITS) == MOSAIC_GATE_RUN);
	snprintf(cfg_kiosk, sizeof(cfg_kiosk), "on");
	host_pin = 1;
	CHECK(p->action_gate("tags.edit", MOSAIC_ACTION_EDITS) == MOSAIC_GATE_PIN);
	CHECK(p->action_gate("manuals.open", MOSAIC_ACTION_PATH) == MOSAIC_GATE_RUN);
	CHECK(p->action_gate("simple-mode.toggle", 0) == MOSAIC_GATE_RUN);
	host_pin = 0;
	toasted[0] = '\0';
	CHECK(p->action_gate("favorites.toggle", MOSAIC_ACTION_EDITS) == MOSAIC_GATE_REFUSE);
	CHECK_STR(toasted, "Set a PIN to make changes in kiosk mode");
	// with simple mode off, kiosk is too
	snprintf(cfg_on, sizeof(cfg_on), "off");
	CHECK(p->action_gate("favorites.toggle", MOSAIC_ACTION_EDITS) == MOSAIC_GATE_RUN);
	cfg_kiosk[0] = '\0';
}

int main(void) {
	testModel();
	testPlugin();
	testHostPin();
	testKid();
	testKiosk();
	CHECK_DONE();
}
