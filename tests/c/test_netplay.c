// The netplay plugin: its model on its own, then the plugin against a fake
// host on a fixture card, from Y through the choice and the keyboard to
// the launch it replaces.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/netplay/netplay.c"

struct MosaicEntry {
	char path[1024];
	int type;
};

#define CARD TEST_SDCARD_PATH
#define GB CARD "/Roms/Game Boy (GB)"
#define NES CARD "/Roms/NES (FC)"
#define GB_PAK CARD "/.system/test/paks/Emus/GB.pak/launch.sh"
#define FC_PAK CARD "/Emus/test/FC.pak/launch.sh"

static MosaicEntry selected;
static int has_selection = 0;
static char cfg_port[16] = "", cfg_address[64] = "";
static char toasted[128];

static char action_id[64];
static int (*action_available)(void*);
static void (*action_run)(void*);
static int bound_button = -1, bind_result = 0;

static char chose_title[32];
static int chose_count = 0;
static void (*on_pick)(int, void*);
static char typed_initial[64];
static void (*on_typed)(const char*, void*);

static char opened[1024];
static int opens = 0, open_result = 1;
static int open_launches = 1; // open_path runs pre_launch, as the launcher does

static char replaced_program[1024];
static char replaced_args[8][256];
static int replaced_count = -1, replace_result = 0;
static int launch_result = -1;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "port") && cfg_port[0]) return cfg_port;
	if (!strcmp(key, "last_address") && cfg_address[0]) return cfg_address;
	return fallback;
}
static void fakeCfgSet(const char* id, const char* key, const char* value) {
	(void)id;
	if (!strcmp(key, "last_address")) snprintf(cfg_address, sizeof(cfg_address), "%s", value);
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static const MosaicEntry* fakeSelected(void) { return has_selection ? &selected : NULL; }
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)label; (void)data;
	snprintf(action_id, sizeof(action_id), "%s", id);
	action_available = available;
	action_run = run;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen; (void)trigger; (void)id;
	bound_button = button;
	return bind_result;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeKeyboard(const char* title, const char* initial, void (*done)(const char*, void*), void* data) {
	(void)title; (void)data;
	snprintf(typed_initial, sizeof(typed_initial), "%s", initial);
	on_typed = done;
}
static int fakeChoose(const char* title, const char* const* choices, int count, int sel, void (*pick)(int, void*), void* data) {
	(void)choices; (void)sel; (void)data;
	snprintf(chose_title, sizeof(chose_title), "%s", title);
	chose_count = count;
	on_pick = pick;
	return 0;
}
static int fakeReplace(const char* program, const char* const* args, int count) {
	if (replace_result != 0) return replace_result;
	snprintf(replaced_program, sizeof(replaced_program), "%s", program);
	for (int i = 0; i < count; i++) snprintf(replaced_args[i], sizeof(replaced_args[i]), "%s", args[i]);
	replaced_count = count;
	return 0;
}
static int fakeOpenPath(const char* path, int type) {
	snprintf(opened, sizeof(opened), "%s", path);
	opens++;
	if (!open_result) return 0;
	if (open_launches) {
		MosaicLaunch l = { .path = path, .type = type };
		launch_result = plugin.pre_launch(&l);
	}
	return 1;
}

static MosaicHost fake = {
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
	.selected_entry = fakeSelected,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.keyboard = fakeKeyboard,
	.open_path = fakeOpenPath,
	.choose = fakeChoose,
	.replace_launch = fakeReplace,
};

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static void makeCard(void) {
	(void)system("rm -rf '" CARD "'");
	(void)system("mkdir -p '" CARD "/.system/test/paks/Emus/GB.pak' '" CARD "/Emus/test/FC.pak'"
		" '" GB "' '" NES "/Hacks' '" CARD "/Roms/Nothing (XX)'");
	writeText(GB_PAK, "");
	writeText(FC_PAK, "");
	writeText(GB "/Tetris.gb", "");
	writeText(NES "/Hacks/Zelda.nes", "");
	writeText(CARD "/Roms/Nothing (XX)/Game.x", "");
}

static void select_(const char* path, int type) {
	snprintf(selected.path, sizeof(selected.path), "%s", path);
	selected.type = type;
	has_selection = 1;
}

static void reset(void) {
	toasted[0] = opened[0] = replaced_program[0] = '\0';
	opens = 0;
	open_result = open_launches = 1;
	replaced_count = launch_result = -1;
	replace_result = 0;
	on_pick = NULL;
	on_typed = NULL;
	chose_count = 0;
}

///////////////////////////////////////
// the model

static void testPort(void) {
	CHECK(NP_port("55435", 1) == 55435);
	CHECK(NP_port(" 8080 ", 1) == 8080);
	CHECK(NP_port("1", 9) == 1);
	CHECK(NP_port("65535", 9) == 65535);
	CHECK(NP_port("65536", 9) == 9);
	CHECK(NP_port("0", 9) == 9);
	CHECK(NP_port("-5", 9) == 9);
	CHECK(NP_port("80x", 9) == 9);
	CHECK(NP_port("", 9) == 9);
	CHECK(NP_port(NULL, 9) == 9);
}

static void testAddress(void) {
	char host[NP_HOST_LEN];
	int port = 0;
	CHECK(NP_parseAddress("192.168.1.20", 55435, host, sizeof(host), &port) == 0);
	CHECK_STR(host, "192.168.1.20");
	CHECK(port == 55435);
	CHECK(NP_parseAddress("  handheld.local:5000 ", 55435, host, sizeof(host), &port) == 0);
	CHECK_STR(host, "handheld.local");
	CHECK(port == 5000);
	CHECK(NP_parseAddress("my-brick", 7, host, sizeof(host), &port) == 0);
	CHECK_STR(host, "my-brick");
	CHECK(port == 7);

	port = 1;
	CHECK(NP_parseAddress("", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("   ", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress(":5000", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("host:", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("host:0", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("host:70000", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("host:12a", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("host:1:2", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("two words", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("a'b", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("-flag", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("--host", 55435, host, sizeof(host), &port) == -1);
	CHECK(NP_parseAddress("[::1]:5000", 55435, host, sizeof(host), &port) == -1);
	CHECK(port == 1); // left alone on failure

	char long_name[300];
	memset(long_name, 'a', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	CHECK(NP_parseAddress(long_name, 55435, host, sizeof(host), &port) == -1);
	char small[4];
	CHECK(NP_parseAddress("abcd", 55435, small, sizeof(small), &port) == -1);
	CHECK(NP_parseAddress("abc", 55435, small, sizeof(small), &port) == 0);
}

static void testPending(void) {
	NPPending p, out;
	memset(&p, 0, sizeof(p));
	CHECK(NP_setPending(&p, NP_NONE, "/g", NULL, 1) == -1);
	CHECK(NP_setPending(&p, NP_HOST, "", NULL, 1) == -1);
	CHECK(NP_setPending(&p, NP_HOST, "/g", NULL, 0) == -1);
	CHECK(NP_setPending(&p, NP_JOIN, "/g", "", 1) == -1);
	CHECK(NP_setPending(&p, NP_JOIN, "/g", NULL, 1) == -1);

	CHECK(NP_setPending(&p, NP_JOIN, "/g", "brick", 5000) == 0);
	CHECK(NP_take(&p, "/g", 1, &out) == NP_JOIN);
	CHECK_STR(out.host, "brick");
	CHECK(out.port == 5000);
	CHECK(p.mode == NP_NONE);
	CHECK(NP_take(&p, "/g", 1, &out) == NP_NONE); // once only

	// another launch, or a pak, clears it
	CHECK(NP_setPending(&p, NP_HOST, "/g", NULL, 1) == 0);
	CHECK(NP_take(&p, "/other", 1, &out) == NP_NONE);
	CHECK(NP_take(&p, "/g", 1, &out) == NP_NONE);
	CHECK(NP_setPending(&p, NP_HOST, "/g", NULL, 1) == 0);
	CHECK(NP_take(&p, "/g", 0, &out) == NP_NONE);
	CHECK(p.mode == NP_NONE);
}

static void testArgs(void) {
	NPPending p;
	char args[NP_ARGS_MAX][NP_ARG_LEN];
	CHECK(NP_setPending(&p, NP_HOST, "/g", NULL, 55435) == 0);
	CHECK(NP_args(&p, args, NP_ARGS_MAX) == 3);
	CHECK_STR(args[0], "--host");
	CHECK_STR(args[1], "--port");
	CHECK_STR(args[2], "55435");

	CHECK(NP_setPending(&p, NP_JOIN, "/g", "10.0.0.2", 6000) == 0);
	CHECK(NP_args(&p, args, NP_ARGS_MAX) == 4);
	CHECK_STR(args[0], "--connect");
	CHECK_STR(args[1], "10.0.0.2");
	CHECK_STR(args[2], "--port");
	CHECK_STR(args[3], "6000");
	CHECK(NP_args(&p, args, 3) == -1);

	memset(&p, 0, sizeof(p));
	CHECK(NP_args(&p, args, NP_ARGS_MAX) == -1);
}

static void testEmuTag(void) {
	char tag[16];
	CHECK(NP_emuTag("/r", "/r/Game Boy (GB)/Tetris.gb", tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "GB");
	CHECK(NP_emuTag("/r", "/r/Super (Japan) (SFC)/Hacks/Mario.sfc", tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "SFC");
	CHECK(NP_emuTag("/r", "/r/Arcade/game.zip", tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "Arcade");
	CHECK(NP_emuTag("/r", "/r/Broken (GB/x.gb", tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "GB");
	CHECK(NP_emuTag("/r", "/r/Game Boy (GB)", tag, sizeof(tag)) == -1);
	CHECK(NP_emuTag("/r", "/r/Game Boy (GB)/", tag, sizeof(tag)) == -1);
	CHECK(NP_emuTag("/r", "/rx/Game Boy (GB)/x.gb", tag, sizeof(tag)) == -1);
	CHECK(NP_emuTag("/r", "/Collections/x.txt", tag, sizeof(tag)) == -1);
	CHECK(NP_emuTag("/r", "/r/A very long system name/x", tag, sizeof(tag)) == -1);
	CHECK(NP_emuTag("/r", "/r/Empty ()/x", tag, sizeof(tag)) == -1);
}

///////////////////////////////////////
// the plugin

static void testInit(void) {
	MosaicHost old = fake;
	old.size = offsetof(MosaicHost, choose);
	CHECK(plugin.init(&old) != 0); // without choose or replace_launch it stays off

	bind_result = -1;
	CHECK(plugin.init(&fake) != 0);
	bind_result = 0;

	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(action_id, "netplay.open");
	CHECK(bound_button == MOSAIC_BUTTON_Y);
}

static void testAvailable(void) {
	has_selection = 0;
	CHECK(!action_available(NULL));
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	CHECK(action_available(NULL));
	select_(NES "/Hacks", MOSAIC_ENTRY_DIR);
	CHECK(!action_available(NULL));
	select_(CARD "/Tools/test/Files.pak", MOSAIC_ENTRY_PAK);
	CHECK(!action_available(NULL));
	select_(CARD "/Collections/Mine.txt", MOSAIC_ENTRY_ROM);
	CHECK(!action_available(NULL));
}

static void testHost(void) {
	reset();
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	CHECK_STR(chose_title, "Netplay");
	CHECK(chose_count == 2);
	on_pick(0, NULL);
	CHECK_STR(opened, GB "/Tetris.gb");
	CHECK(launch_result == MOSAIC_LAUNCH_REPLACE);
	CHECK_STR(replaced_program, GB_PAK);
	CHECK(replaced_count == 3);
	CHECK_STR(replaced_args[0], "--host");
	CHECK_STR(replaced_args[1], "--port");
	CHECK_STR(replaced_args[2], "55435");

	// the next launch of the same game is a plain one
	MosaicLaunch l = { .path = GB "/Tetris.gb", .type = MOSAIC_ENTRY_ROM };
	replaced_count = -1;
	CHECK(plugin.pre_launch(&l) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(replaced_count == -1);

	// the port setting, from Emus/<platform>/ when the system has none
	reset();
	snprintf(cfg_port, sizeof(cfg_port), "6000");
	select_(NES "/Hacks/Zelda.nes", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	on_pick(0, NULL);
	CHECK_STR(replaced_program, FC_PAK);
	CHECK_STR(replaced_args[2], "6000");
	snprintf(cfg_port, sizeof(cfg_port), "nope");
	reset();
	action_run(NULL);
	on_pick(0, NULL);
	CHECK_STR(replaced_args[2], "55435");
	cfg_port[0] = '\0';

	// backing out of the choice launches nothing
	reset();
	action_run(NULL);
	on_pick(-1, NULL);
	CHECK(opens == 0);
}

static void testJoin(void) {
	reset();
	cfg_address[0] = '\0';
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	on_pick(1, NULL);
	CHECK(on_typed != NULL);
	CHECK_STR(typed_initial, "");
	CHECK(opens == 0);
	on_typed("192.168.1.20:5000", NULL);
	CHECK(launch_result == MOSAIC_LAUNCH_REPLACE);
	CHECK(replaced_count == 4);
	CHECK_STR(replaced_args[0], "--connect");
	CHECK_STR(replaced_args[1], "192.168.1.20");
	CHECK_STR(replaced_args[2], "--port");
	CHECK_STR(replaced_args[3], "5000");
	CHECK_STR(cfg_address, "192.168.1.20:5000");

	// the last address is offered next time, and the port setting fills in
	reset();
	action_run(NULL);
	on_pick(1, NULL);
	CHECK_STR(typed_initial, "192.168.1.20:5000");
	on_typed("brick.local", NULL);
	CHECK_STR(replaced_args[1], "brick.local");
	CHECK_STR(replaced_args[3], "55435");

	// a bad address or a cancelled keyboard launches nothing
	reset();
	action_run(NULL);
	on_pick(1, NULL);
	on_typed("not an address", NULL);
	CHECK(opens == 0);
	CHECK(toasted[0] != '\0');
	CHECK_STR(cfg_address, "brick.local");
	reset();
	action_run(NULL);
	on_pick(1, NULL);
	on_typed(NULL, NULL);
	CHECK(opens == 0);

	// without a keyboard only hosting is offered
	MosaicHost no_keyboard = fake;
	no_keyboard.keyboard = NULL;
	CHECK(plugin.init(&no_keyboard) == 0);
	reset();
	action_run(NULL);
	CHECK(chose_count == 1);
	CHECK(plugin.init(&fake) == 0);
}

static void testLaunches(void) {
	// a launch that didn't go through Y is left alone
	reset();
	MosaicLaunch l = { .path = GB "/Tetris.gb", .type = MOSAIC_ENTRY_ROM };
	CHECK(plugin.pre_launch(&l) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(replaced_count == -1);

	// a launch cancelled before pre_launch runs leaves nothing pending
	reset();
	open_result = 0;
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	on_pick(0, NULL);
	CHECK(plugin.pre_launch(&l) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(replaced_count == -1);

	// another game launching first takes the choice away
	reset();
	open_launches = 0;
	action_run(NULL);
	on_pick(0, NULL);
	MosaicLaunch other = { .path = NES "/Hacks/Zelda.nes", .type = MOSAIC_ENTRY_ROM };
	CHECK(plugin.pre_launch(&other) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(plugin.pre_launch(&l) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(replaced_count == -1);

	// no emulator, or another plugin replaced it first: cancelled, not faked
	reset();
	select_(CARD "/Roms/Nothing (XX)/Game.x", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	on_pick(0, NULL);
	CHECK(launch_result == MOSAIC_LAUNCH_CANCEL);
	CHECK(replaced_count == -1);
	CHECK(toasted[0] != '\0');
	reset();
	replace_result = -1;
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	on_pick(0, NULL);
	CHECK(launch_result == MOSAIC_LAUNCH_CANCEL);
	CHECK(toasted[0] != '\0');
}

int main(void) {
	testPort();
	testAddress();
	testPending();
	testArgs();
	testEmuTag();

	makeCard();
	testInit();
	testAvailable();
	testHost();
	testJoin();
	testLaunches();
	CHECK_DONE();
}
