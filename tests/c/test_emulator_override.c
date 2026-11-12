// The emulator override plugin: its model on a fixture card, then the
// plugin against a fake host whose replace_launch checks and quotes the
// command with the launcher's own launchcmd.c, so the test sees the line
// /tmp/next would get.

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include "check.h"
#include "launchcmd.h"
#include "../../plugins/emulator-override/emulator_override.c"

struct MosaicEntry {
	char path[1024];
	char name[128];
	int type;
};

#define CARD TEST_SDCARD_PATH
#define OUTSIDE TEST_SDCARD_PATH "-outside"
#define USERDATA CARD "/.userdata/desktop"
#define CHOICES USERDATA "/launcher/emulator-override.txt"
#define GB CARD "/Roms/Game Boy (GB)"
#define PS CARD "/Roms/Sony PlayStation (PS)"
#define EMUS CARD "/Emus/desktop"
#define SYSTEM_EMUS CARD "/.system/desktop/paks/Emus"

static void run(const char* cmd) {
	if (system(cmd) != 0) {
		fprintf(stderr, "failed: %s\n", cmd);
		exit(1);
	}
}

static void writeText(const char* path, const char* text) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	*slash = '\0';
	char cmd[1200];
	snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", dir);
	run(cmd);
	FILE* f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "can't write %s\n", path);
		exit(1);
	}
	fputs(text, f);
	fclose(f);
}

static void touch(const char* path) {
	writeText(path, "x");
}

static char* readText(const char* path) {
	static char text[4096];
	text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return text;
	size_t got = fread(text, 1, sizeof(text) - 1, f);
	text[got] = '\0';
	fclose(f);
	return text;
}

// Writes the choices file with an mtime of its own, so the plugin always
// sees it changed.
static void writeChoices(const char* text) {
	static int stamp = 0;
	writeText(CHOICES, text);
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "touch -t 2020010100%02d '%s'", stamp++ % 60, CHOICES);
	run(cmd);
}

static void makeCard(void) {
	run("rm -rf '" CARD "' '" OUTSIDE "'");
	touch(GB "/Tetris.gb");
	touch(GB "/Link's Awakening.gb");
	touch(GB "/Hacks/Tetris DX.gb");
	touch(GB "/Multi/Multi.m3u");
	touch(GB "/Multi/Multi (Disc 1).gb");
	touch(GB "/Cued/Cued.cue");
	touch(GB "/Cued/Cued.bin");
	touch(PS "/Game.bin");
	run("mkdir -p '" USERDATA "'"); // a card has it; the plugin makes launcher/ in it

	// stock's paks, and one the card's Emus folder also has
	touch(SYSTEM_EMUS "/GB.pak/launch.sh");
	touch(SYSTEM_EMUS "/GBC.pak/launch.sh");
	touch(SYSTEM_EMUS "/PS.pak/launch.sh");
	touch(EMUS "/GBC.pak/launch.sh");
	// extras: a compatible one, one for another system, one without a launch.sh
	touch(EMUS "/MGBA.pak/launch.sh");
	touch(EMUS "/FOO.pak/launch.sh");
	run("mkdir -p '" EMUS "/SGB.pak'");
	// ones that lead elsewhere: a pak folder linked off the card, a
	// launch.sh linked off the card, a launch.sh linked to another pak's,
	// and a launch.sh that's a folder
	touch(OUTSIDE "/LINK.pak/launch.sh");
	touch(OUTSIDE "/escape.sh");
	run("ln -s '" OUTSIDE "/LINK.pak' '" EMUS "/LINK.pak'");
	run("mkdir -p '" EMUS "/ESC.pak' && ln -s '" OUTSIDE "/escape.sh' '" EMUS "/ESC.pak/launch.sh'");
	run("mkdir -p '" EMUS "/OTHER.pak' && ln -s '../MGBA.pak/launch.sh' '" EMUS "/OTHER.pak/launch.sh'");
	run("mkdir -p '" EMUS "/DIR.pak/launch.sh'");
	// a pak name the choices file can't hold
	touch(EMUS "/.dot.pak/launch.sh");
}

static void testGameKey(void) {
	char key[1024];
	char tag[64];
	CHECK(EO_gameKey(CARD, GB "/Tetris.gb", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Tetris.gb");
	CHECK_STR(tag, "GB");
	CHECK(EO_gameKey(CARD, GB "/Hacks/Tetris DX.gb", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Hacks/Tetris DX.gb");
	// a multi-disc game is its .m3u, from the folder, a disc or the .m3u
	CHECK(EO_gameKey(CARD, GB "/Multi", 1, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Multi/Multi.m3u");
	CHECK(EO_gameKey(CARD, GB "/Multi/Multi (Disc 1).gb", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Multi/Multi.m3u");
	CHECK(EO_gameKey(CARD, GB "/Multi/Multi.m3u", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Multi/Multi.m3u");
	// a cue sheet game is its .cue, from the folder or the .cue
	CHECK(EO_gameKey(CARD, GB "/Cued", 1, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Cued/Cued.cue");
	CHECK(EO_gameKey(CARD, GB "/Cued/Cued.cue", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(key, "/Roms/Game Boy (GB)/Cued/Cued.cue");
	CHECK(EO_gameKey(CARD, PS "/Game.bin", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK_STR(tag, "PS");

	// not games
	CHECK(!EO_gameKey(CARD, GB "/Hacks", 1, key, sizeof(key), tag, sizeof(tag)));
	CHECK(!EO_gameKey(CARD, GB, 1, key, sizeof(key), tag, sizeof(tag)));
	CHECK(!EO_gameKey(CARD, CARD "/Roms", 1, key, sizeof(key), tag, sizeof(tag)));
	CHECK(!EO_gameKey(CARD, CARD "/Tools/desktop/Thing.pak", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK(!EO_gameKey(CARD, GB "/../Game Boy (GB)/Tetris.gb", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK(!EO_gameKey(CARD, GB "//Tetris.gb", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK(!EO_gameKey(CARD, "/elsewhere/Roms/Game Boy (GB)/Tetris.gb", 0, key, sizeof(key), tag, sizeof(tag)));
	CHECK(!EO_gameKey(CARD, CARD "/Romsx/A (GB)/Tetris.gb", 0, key, sizeof(key), tag, sizeof(tag)));
}

static void testPaks(void) {
	CHECK(EO_validName("MGBA"));
	CHECK(EO_validName("GB-gambatte 2"));
	CHECK(!EO_validName(""));
	CHECK(!EO_validName(NULL));
	CHECK(!EO_validName(".."));
	CHECK(!EO_validName(".dot"));
	CHECK(!EO_validName("a/b"));
	CHECK(!EO_validName("a\\b"));
	CHECK(!EO_validName("a\tb"));
	CHECK(!EO_validName("a\nb"));
	CHECK(!EO_validName("0123456789012345678901234567890123456789012345678901234567890123"));

	char card[PATH_MAX];
	CHECK(realpath(CARD, card) != NULL);
	char out[1024];
	char want[PATH_MAX + 64];
	CHECK(EO_pakLaunch(CARD, "desktop", "MGBA", out, sizeof(out)));
	snprintf(want, sizeof(want), "%s/Emus/desktop/MGBA.pak/launch.sh", card);
	CHECK_STR(out, want);
	// stock's own folder when the card's Emus doesn't have it
	CHECK(EO_pakLaunch(CARD, "desktop", "GB", out, sizeof(out)));
	snprintf(want, sizeof(want), "%s/.system/desktop/paks/Emus/GB.pak/launch.sh", card);
	CHECK_STR(out, want);
	// and the card's Emus first, as stock looks
	CHECK(EO_pakLaunch(CARD, "desktop", "GBC", out, sizeof(out)));
	snprintf(want, sizeof(want), "%s/Emus/desktop/GBC.pak/launch.sh", card);
	CHECK_STR(out, want);

	CHECK(!EO_pakLaunch(CARD, "desktop", "SGB", out, sizeof(out)));   // no launch.sh
	CHECK(!EO_pakLaunch(CARD, "desktop", "NOPE", out, sizeof(out)));  // not there
	CHECK(!EO_pakLaunch(CARD, "desktop", "LINK", out, sizeof(out)));  // pak linked off the card
	CHECK(!EO_pakLaunch(CARD, "desktop", "ESC", out, sizeof(out)));   // launch.sh linked off the card
	CHECK(!EO_pakLaunch(CARD, "desktop", "OTHER", out, sizeof(out))); // launch.sh is another pak's
	CHECK(!EO_pakLaunch(CARD, "desktop", "DIR", out, sizeof(out)));   // launch.sh is a folder
	CHECK(!EO_pakLaunch(CARD, "desktop", ".dot", out, sizeof(out)));
	CHECK(!EO_pakLaunch(CARD, "desktop", "../desktop/MGBA", out, sizeof(out)));
	CHECK(!EO_pakLaunch(CARD, "../desktop", "MGBA", out, sizeof(out)));
	CHECK(!EO_pakLaunch(CARD, "tg5040", "MGBA", out, sizeof(out)));   // another platform's
	CHECK(!EO_pakLaunch(CARD, "desktop", "MGBA", out, 10));           // doesn't fit

	CHECK(EO_compatible("GB", "MGBA"));
	CHECK(EO_compatible("gb", "gbc"));
	CHECK(EO_compatible("GBA", "MGBA"));
	CHECK(EO_compatible("SFC", "SUPA"));
	CHECK(!EO_compatible("GBA", "GB"));
	CHECK(!EO_compatible("GB", "GB"));
	CHECK(!EO_compatible("PS", "MGBA"));

	char* names[16];
	int n = EO_paks(CARD, "desktop", "GB", 0, names, 16);
	CHECK(n == 2);
	if (n == 2) {
		CHECK_STR(names[0], "GBC"); // once, though both folders have it
		CHECK_STR(names[1], "MGBA");
	}
	for (int i = 0; i < n; i++) free(names[i]);
	n = EO_paks(CARD, "desktop", "GB", 1, names, 16);
	CHECK(n == 4);
	if (n == 4) {
		CHECK_STR(names[0], "FOO");
		CHECK_STR(names[1], "GBC");
		CHECK_STR(names[2], "MGBA");
		CHECK_STR(names[3], "PS");
	}
	for (int i = 0; i < n; i++) free(names[i]);
	n = EO_paks(CARD, "desktop", "GB", 1, names, 2);
	CHECK(n == 2);
	if (n == 2) CHECK_STR(names[1], "GBC");
	for (int i = 0; i < n; i++) free(names[i]);
	CHECK(EO_paks(CARD, "desktop", "PS", 0, names, 16) == 0);
}

static void testMap(void) {
	EOMap m;
	EO_mapInit(&m);
	CHECK(EO_mapGet(&m, "/Roms/A (GB)/a.gb") == NULL);
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/a.gb", "MGBA") == 1);
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/a.gb", "MGBA") == 0);
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/b.gb", "GBC") == 1);
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/a.gb", "SGB") == 1);
	CHECK_STR(EO_mapGet(&m, "/Roms/A (GB)/a.gb"), "SGB");
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/c\t.gb", "MGBA") == -1);
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/c\n.gb", "MGBA") == -1);
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/c.gb", "../x") == -1);
	CHECK(EO_mapSet(&m, "", "MGBA") == -1);
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/c.gb", NULL) == 0);
	CHECK(m.count == 2);

	run("mkdir -p '" USERDATA "/launcher'");
	CHECK(EO_mapSave(&m, CHOICES) == 0);
	CHECK_STR(readText(CHOICES), "/Roms/A (GB)/a.gb\tSGB\n/Roms/A (GB)/b.gb\tGBC\n");
	CHECK(EO_mapSet(&m, "/Roms/A (GB)/a.gb", "") == 1);
	CHECK(m.count == 1);
	CHECK(EO_mapGet(&m, "/Roms/A (GB)/a.gb") == NULL);
	CHECK(EO_mapLoad(&m, CHOICES) == 0);
	CHECK(m.count == 2);
	CHECK_STR(EO_mapGet(&m, "/Roms/A (GB)/a.gb"), "SGB");

	// lines without a tab or with a name the map can't hold are skipped
	writeText(CHOICES, "no tab here\n/Roms/A (GB)/a.gb\t../up\n/Roms/A (GB)/b.gb\tGBC\r\n\t\n/Roms/A (GB)/d.gb\tMGBA");
	CHECK(EO_mapLoad(&m, CHOICES) == 0);
	CHECK(m.count == 2);
	CHECK_STR(EO_mapGet(&m, "/Roms/A (GB)/b.gb"), "GBC");
	CHECK_STR(EO_mapGet(&m, "/Roms/A (GB)/d.gb"), "MGBA");
	unlink(CHOICES);
	CHECK(EO_mapLoad(&m, CHOICES) == 0);
	CHECK(m.count == 0);
	EO_mapFree(&m);
}

///////////////////////////////////////
// the plugin, against a fake host

static MosaicEntry selected_entry;
static int has_selection = 0;
static char action_id[64];
static char action_label[64];
static int action_button = -1;
static int (*action_available)(void*) = NULL;
static void (*action_run)(void*) = NULL;
static char toasted[128];
static char logged[2048];
static char chosen_title[64];
static char chosen[8][160];
static int chosen_count = -1;
static int chosen_selected = -1;
static void (*on_pick)(int, void*) = NULL;
static char cfg_all[8] = "";
static int in_pre_launch = 0;
static int refuse_replace = 0;
static LaunchCmd replaced;

static void fakeLog(const char* id, const char* m) {
	snprintf(logged, sizeof(logged), "%s", m);
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "all") && cfg_all[0]) return cfg_all;
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) { (void)key; (void)value; }
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static const MosaicEntry* fakeSelected(void) { return has_selection ? &selected_entry : NULL; }
static void fakeRedraw(void) {}
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run_)(void*), void* data) {
	(void)data;
	snprintf(action_id, sizeof(action_id), "%s", id);
	snprintf(action_label, sizeof(action_label), "%s", label);
	action_available = available;
	action_run = run_;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	if (screen != MOSAIC_SCREEN_GAMELIST || trigger != MOSAIC_TRIGGER_PRESS || strcmp(id, action_id) != 0) return -1;
	action_button = button;
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static int fakeChoose(const char* title, const char* const* list, int count, int sel, void (*pick)(int, void*), void* data) {
	(void)data;
	if (count < 1 || count > 64) return -1;
	snprintf(chosen_title, sizeof(chosen_title), "%s", title);
	chosen_count = count;
	chosen_selected = sel;
	for (int i = 0; i < count && i < 8; i++) snprintf(chosen[i], sizeof(chosen[0]), "%s", list[i]);
	on_pick = pick;
	return 0;
}
// As the launcher's: checked and stored with launchcmd.c, only in pre_launch.
static int fakeReplaceLaunch(const char* program, const char* const* args, int count) {
	if (!in_pre_launch || refuse_replace) return -1;
	return LaunchCmd_set(&replaced, CARD, program, args, count);
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "desktop",
	.sdcard_path = CARD,
	.userdata_path = USERDATA,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.entry_type = fakeEntryType,
	.selected_entry = fakeSelected,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.choose = fakeChoose,
	.replace_launch = fakeReplaceLaunch,
};

static void select_(const char* path, int type) {
	snprintf(selected_entry.path, sizeof(selected_entry.path), "%s", path);
	selected_entry.type = type;
	has_selection = 1;
}

static const MosaicPlugin* start(void) {
	action_id[0] = '\0';
	action_button = -1;
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
	return p;
}

// Runs pre_launch as the launcher would, writing the command that replaced
// the launch into cmd, or "" when it went ahead as usual.
static int launch(const MosaicPlugin* p, const char* path, int type, char* cmd, size_t size) {
	MosaicLaunch l = { path, type };
	memset(&replaced, 0, sizeof(replaced));
	cmd[0] = '\0';
	in_pre_launch = 1;
	int result = p->pre_launch(&l);
	in_pre_launch = 0;
	if (result == MOSAIC_LAUNCH_REPLACE) CHECK(LaunchCmd_format(&replaced, type == MOSAIC_ENTRY_ROM ? path : NULL, cmd, size) == 0);
	return result;
}

static void testPlugin(void) {
	makeCard();
	run("rm -f '" CHOICES "'");
	char card[PATH_MAX];
	CHECK(realpath(CARD, card) != NULL);
	char want[PATH_MAX * 2];
	char cmd[LAUNCH_CMD_SIZE];

	const MosaicPlugin* p = start();
	CHECK_STR(p->id, "emulator-override");
	CHECK_STR(action_id, "emulator-override.choose");
	CHECK_STR(action_label, "EMULATOR");
	CHECK(action_button == MOSAIC_BUTTON_Y);
	if (!action_available || !action_run) return;

	// where it's offered
	has_selection = 0;
	CHECK(!action_available(NULL));
	select_(PS "/Game.bin", MOSAIC_ENTRY_ROM);
	CHECK(!action_available(NULL)); // nothing else runs PS games
	select_(GB "/Hacks", MOSAIC_ENTRY_DIR);
	CHECK(!action_available(NULL));
	select_(CARD "/Tools/desktop/Thing.pak", MOSAIC_ENTRY_PAK);
	CHECK(!action_available(NULL));
	select_(GB "/Multi", MOSAIC_ENTRY_DIR);
	CHECK(action_available(NULL));

	// with nothing chosen, games launch as usual
	CHECK(launch(p, GB "/Tetris.gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_CONTINUE);

	// picking one
	select_(GB "/Link's Awakening.gb", MOSAIC_ENTRY_ROM);
	CHECK(action_available(NULL));
	action_run(NULL);
	CHECK_STR(chosen_title, "Emulator");
	CHECK(chosen_count == 3);
	CHECK_STR(chosen[0], "Default (GB)");
	CHECK_STR(chosen[1], "GBC");
	CHECK_STR(chosen[2], "MGBA");
	CHECK(chosen_selected == 0);
	on_pick(2, NULL);
	CHECK_STR(toasted, "Emulator set to MGBA");
	CHECK_STR(readText(CHOICES), "/Roms/Game Boy (GB)/Link's Awakening.gb\tMGBA\n");
	action_run(NULL);
	CHECK(chosen_selected == 2);
	on_pick(-1, NULL); // backing out changes nothing
	CHECK_STR(readText(CHOICES), "/Roms/Game Boy (GB)/Link's Awakening.gb\tMGBA\n");

	// launching it runs MGBA's launch.sh with the game, quoted as stock quotes
	CHECK(launch(p, GB "/Link's Awakening.gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_REPLACE);
	CHECK(replaced.count == 0);
	snprintf(want, sizeof(want), "'%s/Emus/desktop/MGBA.pak/launch.sh' '%s'", card, GB "/Link'\\''s Awakening.gb");
	CHECK_STR(cmd, want);
	CHECK(strstr(logged, "launching /Roms/Game Boy (GB)/Link's Awakening.gb with ") != NULL);
	// other games and paks aren't touched
	CHECK(launch(p, GB "/Tetris.gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(launch(p, CARD "/Tools/desktop/Thing.pak", MOSAIC_ENTRY_PAK, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_CONTINUE);

	// a multi-disc game, chosen from its folder, applies to its discs
	select_(GB "/Multi", MOSAIC_ENTRY_DIR);
	action_run(NULL);
	on_pick(1, NULL);
	CHECK(launch(p, GB "/Multi/Multi (Disc 1).gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_REPLACE);
	snprintf(want, sizeof(want), "'%s/Emus/desktop/GBC.pak/launch.sh' '%s'", card, GB "/Multi/Multi (Disc 1).gb");
	CHECK_STR(cmd, want);

	// Default clears it
	select_(GB "/Multi", MOSAIC_ENTRY_DIR);
	action_run(NULL);
	CHECK(chosen_selected == 1);
	on_pick(0, NULL);
	CHECK_STR(toasted, "Using the default emulator");
	CHECK_STR(readText(CHOICES), "/Roms/Game Boy (GB)/Link's Awakening.gb\tMGBA\n");
	CHECK(launch(p, GB "/Multi/Multi (Disc 1).gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_CONTINUE);

	// choices edited on a computer are read again, and checked before use:
	// each of these falls back to the usual launch and says why
	const char* bad[] = { "LINK", "ESC", "OTHER", "DIR", "SGB", "NOPE" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		char text[256];
		snprintf(text, sizeof(text), "/Roms/Game Boy (GB)/Tetris.gb\t%s\n/Roms/Game Boy (GB)/Hacks/Tetris DX.gb\t%s\n", bad[i], bad[i]);
		writeChoices(text);
		logged[0] = '\0';
		CHECK(launch(p, GB "/Tetris.gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_CONTINUE);
		CHECK(strstr(logged, "isn't an emulator pak") != NULL);
	}
	// a name the file can't hold is never read
	writeChoices("/Roms/Game Boy (GB)/Tetris.gb\t../../../escape\n");
	CHECK(launch(p, GB "/Tetris.gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_CONTINUE);
	// one that's fine, though not in the list, is used
	writeChoices("/Roms/Game Boy (GB)/Tetris.gb\tFOO\n");
	CHECK(launch(p, GB "/Tetris.gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_REPLACE);
	// a PS game with a choice can clear it, though there's nothing to pick
	writeChoices("/Roms/Sony PlayStation (PS)/Game.bin\tFOO\n");
	p->post_return();
	select_(PS "/Game.bin", MOSAIC_ENTRY_ROM);
	CHECK(action_available(NULL));
	action_run(NULL);
	CHECK(chosen_count == 1);
	on_pick(0, NULL);
	CHECK_STR(readText(CHOICES), "");
	CHECK(!action_available(NULL));

	// when the launcher refuses the replacement, it launches as usual
	writeChoices("/Roms/Game Boy (GB)/Tetris.gb\tMGBA\n");
	refuse_replace = 1;
	CHECK(launch(p, GB "/Tetris.gb", MOSAIC_ENTRY_ROM, cmd, sizeof(cmd)) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(strstr(logged, "couldn't replace the launch") != NULL);
	refuse_replace = 0;
	p->quit();

	// every pak, with the setting on
	snprintf(cfg_all, sizeof(cfg_all), "on");
	p = start();
	select_(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	CHECK(chosen_count == 5);
	CHECK(chosen_selected == 3);
	CHECK_STR(chosen[1], "FOO");
	CHECK_STR(chosen[4], "PS");
	on_pick(-1, NULL);
	p->quit();
	cfg_all[0] = '\0';

	// a launcher without replace_launch doesn't load it
	fake.replace_launch = NULL;
	p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) != 0);
	fake.replace_launch = fakeReplaceLaunch;
}

int main(void) {
	makeCard();
	testGameKey();
	testPaks();
	testMap();
	testPlugin();
	run("rm -rf '" CARD "' '" OUTSIDE "'");
	CHECK_DONE();
}
