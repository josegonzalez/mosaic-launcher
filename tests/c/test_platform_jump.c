// The platform jump plugin: its model on its own, then the plugin against
// a fake host with lists of its own.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/platform-jump/platform_jump.c"

#define CARD "/sd"
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define MGB ROMS "/Game Boy (MGB)"
#define GBA ROMS "/Game Boy Advance (GBA)"
#define SNES ROMS "/Super Nintendo (SFC)"
#define RECENTS CARD "/Recently Played"
#define TOOLS CARD "/Tools/desktop"
#define RPG CARD "/Collections/RPG.txt"

///////////////////////////////////////
// the model

static void testKey(void) {
	char key[64];
	CHECK(PJ_systemKey(ROMS, GB, key, sizeof(key)));
	CHECK_STR(key, "Game Boy (");
	CHECK(PJ_systemKey(ROMS, MGB "/Tetris.gb", key, sizeof(key)));
	CHECK_STR(key, "Game Boy (");
	CHECK(PJ_systemKey(ROMS, GBA "/Hacks (2024)/Game.gba", key, sizeof(key)));
	CHECK_STR(key, "Game Boy Advance (");
	CHECK(PJ_systemKey(ROMS, ROMS "/PICO8/game.p8", key, sizeof(key)));
	CHECK_STR(key, "PICO8");
	CHECK(!PJ_systemKey(ROMS, ROMS, key, sizeof(key)));
	CHECK(!PJ_systemKey(ROMS, ROMS "/", key, sizeof(key)));
	CHECK(!PJ_systemKey(ROMS, CARD "/Romsx/GB/a.gb", key, sizeof(key)));
	CHECK(!PJ_systemKey(ROMS, TOOLS "/Clock.pak", key, sizeof(key)));
	char small[4];
	CHECK(PJ_systemKey(ROMS, GB, small, sizeof(small)));
	CHECK_STR(small, "Gam");

	CHECK(PJ_isSystemDir(ROMS, GB));
	CHECK(!PJ_isSystemDir(ROMS, GB "/Tetris.gb"));
	CHECK(!PJ_isSystemDir(ROMS, ROMS));
	CHECK(!PJ_isSystemDir(ROMS, RECENTS));
}

static void testSystems(void) {
	PJSystems s;
	PJ_systemsInit(&s);
	CHECK(PJ_systemsAdd(&s, "Game Boy (", GB) == 0);
	CHECK(PJ_systemsAdd(&s, "Super Nintendo (", SNES) == 1);
	CHECK(PJ_systemsAdd(&s, "Game Boy (", MGB) == 0); // collated: keeps the first
	CHECK(s.count == 2);
	CHECK_STR(s.paths[0], GB);
	CHECK(PJ_systemsFind(&s, "Super Nintendo (") == 1);
	CHECK(PJ_systemsFind(&s, "Game Boy Advance (") == -1);
	for (int i = 0; i < 40; i++) {
		char key[16];
		snprintf(key, sizeof(key), "S%d", i);
		CHECK(PJ_systemsAdd(&s, key, NULL) == i + 2);
	}
	CHECK(s.count == 42);
	CHECK_STR(s.paths[41], "");
	PJ_systemsClear(&s);
	CHECK(s.count == 0 && !s.keys);
}

static void testJump(void) {
	// three systems in order, one row each
	int order[] = { 0, 1, 2 };
	CHECK(PJ_groupCount(order, 3) == 3);
	CHECK(PJ_jump(order, 3, 0, 1, 1) == 1);
	CHECK(PJ_jump(order, 3, 2, 1, 1) == 0);
	CHECK(PJ_jump(order, 3, 2, 1, 0) == -1);
	CHECK(PJ_jump(order, 3, 0, -1, 1) == 2);
	CHECK(PJ_jump(order, 3, 0, -1, 0) == -1);
	CHECK(PJ_jump(order, 3, 1, -1, 0) == 0);

	// a root: Recently Played, two systems, Collections, Tools
	int root[] = { -1, 0, 1, -1, -1 };
	CHECK(PJ_groupCount(root, 5) == 2);
	CHECK(PJ_jump(root, 5, 0, 1, 0) == 1);  // from Recently Played
	CHECK(PJ_jump(root, 5, 0, -1, 0) == -1);
	CHECK(PJ_jump(root, 5, 0, -1, 1) == 2); // wraps to the last system
	CHECK(PJ_jump(root, 5, 4, -1, 0) == 2); // from Tools
	CHECK(PJ_jump(root, 5, 4, 1, 0) == -1);
	CHECK(PJ_jump(root, 5, 4, 1, 1) == 1);
	CHECK(PJ_jump(root, 5, 2, 1, 1) == 1);

	// a collection with systems mixed up, taken in the order they appear
	int mixed[] = { 5, 5, 9, 5, -1, 7, 9 };
	CHECK(PJ_groupCount(mixed, 7) == 3);
	CHECK(PJ_jump(mixed, 7, 0, 1, 0) == 2);
	CHECK(PJ_jump(mixed, 7, 3, 1, 0) == 2); // a later row of the first system
	CHECK(PJ_jump(mixed, 7, 6, 1, 0) == 5); // 9 first appears before 7
	CHECK(PJ_jump(mixed, 7, 5, 1, 0) == -1);
	CHECK(PJ_jump(mixed, 7, 5, 1, 1) == 0);
	CHECK(PJ_jump(mixed, 7, 5, -1, 0) == 2);
	CHECK(PJ_jump(mixed, 7, 6, -1, 0) == 0);
	CHECK(PJ_jump(mixed, 7, 4, 1, 0) == 5); // from a row in no system
	CHECK(PJ_jump(mixed, 7, 4, -1, 0) == 2);

	// one system has nowhere to go
	int one[] = { 3, 3, -1 };
	CHECK(PJ_groupCount(one, 3) == 1);
	CHECK(PJ_jump(one, 3, 0, 1, 1) == -1);
	CHECK(PJ_jump(one, 3, 1, -1, 1) == -1);
	CHECK(PJ_jump(one, 3, 2, 1, 1) == 0);

	CHECK(PJ_groupCount(NULL, 0) == 0);
	CHECK(PJ_jump(order, 3, 5, 1, 1) == -1);
	CHECK(PJ_jump(order, 3, 0, 0, 1) == -1);
}

///////////////////////////////////////
// a fake host

struct MosaicEntry {
	char path[256];
	int type;
};

#define LIST_MAX 16
struct MosaicList {
	MosaicEntry entries[LIST_MAX];
	int count;
};

static void add(MosaicList* list, const char* path, int type) {
	MosaicEntry* e = &list->entries[list->count++];
	snprintf(e->path, sizeof(e->path), "%s", path);
	e->type = type;
}

static MosaicList root_list, games_list, systems_list, rpg_list;
static const MosaicList* showing = NULL;
static char showing_path[256] = "";
static int showing_selected = 0;

static char cfg_root[8] = "", cfg_lists[8] = "", cfg_wrap[8] = "";
static char opened[8][256];
static int open_count = 0;
static int selects[8];
static int select_count = 0;

static void show(const char* path, const MosaicList* list, int selected) {
	snprintf(showing_path, sizeof(showing_path), "%s", path);
	showing = list;
	showing_selected = selected;
}

static void fakeLog(const char* id, const char* m) {
	(void)m;
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "root") && cfg_root[0]) return cfg_root;
	if (!strcmp(key, "lists") && cfg_lists[0]) return cfg_lists;
	if (!strcmp(key, "wrap") && cfg_wrap[0]) return cfg_wrap;
	return fallback;
}
static const char* fakeEntryPath(const MosaicEntry* e) {
	return e->path;
}
static int fakeEntryType(const MosaicEntry* e) {
	return e->type;
}
static int fakeListCount(const MosaicList* l) {
	return l->count;
}
static MosaicEntry* fakeListGet(const MosaicList* l, int i) {
	return i >= 0 && i < l->count ? (MosaicEntry*)&l->entries[i] : NULL;
}
static const char* fakeCurrentPath(void) {
	return showing_path[0] ? showing_path : NULL;
}
static const MosaicList* fakeCurrentList(void) {
	return showing;
}
static int fakeSelectedIndex(void) {
	return showing && showing->count ? showing_selected : -1;
}
static int fakeSelectIndex(int index) {
	if (!showing || index < 0 || index >= showing->count) return -1;
	showing_selected = index;
	selects[select_count++] = index;
	return 0;
}
// opening the root lists it again, as the launcher would
static int fakeOpenPath(const char* path, int type) {
	CHECK(type == MOSAIC_ENTRY_DIR);
	snprintf(opened[open_count++], sizeof(opened[0]), "%s", path);
	if (!strcmp(path, CARD)) {
		show(CARD, &root_list, 0);
		plugin.list_ready(&root_list, CARD);
	}
	else show(path, &games_list, 0);
	return 1;
}

// what init asked for, in order
static char actions[4][64];
static int (*availables[4])(const char*, void*);
static void (*runs[4])(const char*, void*);
static int action_count = 0;
static char overrides[8][64];
static int override_buttons[8];
static int override_triggers[8];
static int override_count = 0;
static int refuse_r1 = 0;

static int fakeRegisterAction(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)label;
	(void)data;
	snprintf(actions[action_count], sizeof(actions[0]), "%s", id);
	availables[action_count] = available;
	runs[action_count] = run;
	action_count++;
	return 0;
}
static int fakeOverrideButton(int screen, int button, int trigger, const char* action_id) {
	CHECK(screen == MOSAIC_SCREEN_GAMELIST);
	if (refuse_r1 && button == MOSAIC_BUTTON_R1) return -1;
	override_buttons[override_count] = button;
	override_triggers[override_count] = trigger;
	snprintf(overrides[override_count++], sizeof(overrides[0]), "%s", action_id);
	return 0;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "desktop",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/desktop",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_path = fakeEntryPath,
	.entry_type = fakeEntryType,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.current_path = fakeCurrentPath,
	.register_action = fakeRegisterAction,
	.open_path = fakeOpenPath,
	.current_list = fakeCurrentList,
	.selected_index = fakeSelectedIndex,
	.select_index = fakeSelectIndex,
	.override_button = fakeOverrideButton,
	.override_repeat = 1,
};

static void reset(void) {
	open_count = 0;
	select_count = 0;
	cfg_root[0] = cfg_lists[0] = cfg_wrap[0] = '\0';
}

static int canJumpNow(void) {
	return availables[0](NULL, NULL);
}
static void prev(void) {
	runs[0](NULL, NULL);
}
static void next(void) {
	runs[1](NULL, NULL);
}

static void testInit(void) {
	CHECK(mosaic_plugin_get(&fake) == &plugin);
	CHECK(plugin.list_ready == listReady);

	// an older launcher without overrides
	MosaicHost old = fake;
	old.size = offsetof(MosaicHost, override_button);
	CHECK(plugin.init(&old) != 0);
	plugin.quit();

	// another plugin has R1
	refuse_r1 = 1;
	action_count = override_count = 0;
	CHECK(plugin.init(&fake) != 0);
	plugin.quit();
	refuse_r1 = 0;

	// a launcher that can't repeat an override: once per press
	old = fake;
	old.size = offsetof(MosaicHost, override_repeat);
	action_count = override_count = 0;
	CHECK(plugin.init(&old) == 0);
	CHECK(override_count == 4);
	for (int i = 0; i < 4; i++) CHECK(override_triggers[i] == MOSAIC_TRIGGER_PRESS);
	plugin.quit();

	action_count = override_count = 0;
	CHECK(plugin.init(&fake) == 0);
	CHECK(action_count == 2);
	CHECK_STR(actions[0], "platform-jump.prev");
	CHECK_STR(actions[1], "platform-jump.next");
	// letters stay as the fallback on each button, and all of them repeat
	// while held, so a held shoulder doesn't switch from one to the other
	CHECK(override_count == 4);
	CHECK(override_buttons[0] == MOSAIC_BUTTON_L1 && !strcmp(overrides[0], "platform-jump.prev"));
	CHECK(override_buttons[1] == MOSAIC_BUTTON_L1 && !strcmp(overrides[1], "gamelist.prev_letter"));
	CHECK(override_buttons[2] == MOSAIC_BUTTON_R1 && !strcmp(overrides[2], "platform-jump.next"));
	CHECK(override_buttons[3] == MOSAIC_BUTTON_R1 && !strcmp(overrides[3], "gamelist.next_letter"));
	for (int i = 0; i < 4; i++) CHECK(override_triggers[i] == MOSAIC_TRIGGER_REPEAT);

	// before any list
	CHECK(!canJumpNow());
}

static void buildLists(void) {
	memset(&root_list, 0, sizeof(root_list));
	add(&root_list, RECENTS, MOSAIC_ENTRY_DIR);
	add(&root_list, GB, MOSAIC_ENTRY_DIR); // collated with MGB
	add(&root_list, GBA, MOSAIC_ENTRY_DIR);
	add(&root_list, SNES, MOSAIC_ENTRY_DIR);
	add(&root_list, CARD "/Random game", MOSAIC_ENTRY_DIR);
	add(&root_list, TOOLS, MOSAIC_ENTRY_DIR);

	memset(&games_list, 0, sizeof(games_list));
	add(&games_list, GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	add(&games_list, GB "/Hacks", MOSAIC_ENTRY_DIR);

	memset(&rpg_list, 0, sizeof(rpg_list));
	add(&rpg_list, SNES "/Chrono Trigger.sfc", MOSAIC_ENTRY_ROM);
	add(&rpg_list, MGB "/Link's Awakening.gb", MOSAIC_ENTRY_ROM);
	add(&rpg_list, SNES "/Earthbound.sfc", MOSAIC_ENTRY_ROM);
	add(&rpg_list, GB "/Pokemon Red.gb", MOSAIC_ENTRY_ROM);
	add(&rpg_list, GBA "/Golden Sun.gba", MOSAIC_ENTRY_ROM);

	memset(&systems_list, 0, sizeof(systems_list));
	add(&systems_list, SNES, MOSAIC_ENTRY_DIR);
	add(&systems_list, GBA, MOSAIC_ENTRY_DIR);
}

static void testListReady(void) {
	plugin.list_ready(&root_list, CARD);
	CHECK(root_has_systems);
	CHECK(systems.count == 3);
	CHECK_STR(systems.paths[0], GB);
	CHECK_STR(systems.paths[1], GBA);
	CHECK_STR(systems.paths[2], SNES);

	// a Systems folder elsewhere doesn't change the root's order
	plugin.list_ready(&systems_list, CARD "/Systems");
	CHECK(systems.count == 3);
	CHECK_STR(systems.paths[0], GB);

	// nor does a list with no systems
	plugin.list_ready(&games_list, GB);
	CHECK(systems.count == 3);
}

static void testSystemJump(void) {
	reset();
	show(GB "/Hacks", &games_list, 1);
	CHECK(canJumpNow());
	next();
	// back to the root, GBA selected there, then GBA
	CHECK(open_count == 2);
	CHECK_STR(opened[0], CARD);
	CHECK_STR(opened[1], GBA);
	CHECK(select_count == 1 && selects[0] == 2);
	CHECK_STR(showing_path, GBA);

	// a collated folder counts as its system
	reset();
	show(MGB, &games_list, 0);
	prev();
	CHECK(open_count == 2);
	CHECK_STR(opened[1], SNES); // wraps
	CHECK(select_count == 1 && selects[0] == 3);

	reset();
	strcpy(cfg_wrap, "off");
	show(GB, &games_list, 0);
	prev();
	CHECK(open_count == 0);
	CHECK(canJumpNow()); // L1 stays the system's at the ends
	show(SNES, &games_list, 0);
	next();
	CHECK(open_count == 0);
	prev();
	CHECK(open_count == 2);
	CHECK_STR(opened[1], GBA);

	// a system the root doesn't list
	reset();
	show(ROMS "/Arcade (FBN)", &games_list, 0);
	CHECK(!canJumpNow());
	next();
	CHECK(open_count == 0);
}

static void testListJump(void) {
	reset();
	show(RPG, &rpg_list, 0);
	CHECK(canJumpNow());
	next();
	CHECK(select_count == 1 && selects[0] == 1); // SNES, then Game Boy
	next();
	CHECK(selects[1] == 4);
	next();
	CHECK(selects[2] == 0); // wraps
	prev();
	CHECK(selects[3] == 4);
	CHECK(open_count == 0);

	strcpy(cfg_lists, "off");
	CHECK(!canJumpNow());

	// one system's games
	reset();
	MosaicList gb_only;
	memset(&gb_only, 0, sizeof(gb_only));
	add(&gb_only, GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	add(&gb_only, MGB "/Tetris DX.gb", MOSAIC_ENTRY_ROM);
	add(&gb_only, TOOLS "/Clock.pak", MOSAIC_ENTRY_PAK);
	show(RPG, &gb_only, 0);
	CHECK(!canJumpNow());
}

static void testRootJump(void) {
	reset();
	show(CARD, &root_list, 0);
	CHECK(!canJumpNow()); // letters by default
	strcpy(cfg_root, "on");
	CHECK(canJumpNow());
	next();
	CHECK(select_count == 1 && selects[0] == 1);
	show(CARD, &root_list, 5); // Tools
	prev();
	CHECK(selects[1] == 3);
	show(CARD, &root_list, 3);
	next();
	CHECK(selects[2] == 1); // wraps past Random game and Tools
	CHECK(open_count == 0);
}

// A replaced root with no systems: they come from the next list of
// systems, and a jump opens the system straight from where the user is.
static void testReplacedRoot(void) {
	plugin.quit();
	CHECK(plugin.init(&fake) == 0);
	MosaicList replaced;
	memset(&replaced, 0, sizeof(replaced));
	add(&replaced, CARD "/Systems", MOSAIC_ENTRY_DIR);
	add(&replaced, GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	plugin.list_ready(&replaced, CARD);
	CHECK(!root_has_systems);
	CHECK(systems.count == 0);

	reset();
	show(GBA, &games_list, 0);
	CHECK(!canJumpNow());

	plugin.list_ready(&systems_list, CARD "/Systems");
	CHECK(systems.count == 2);
	CHECK(canJumpNow());
	next();
	CHECK(open_count == 1);
	CHECK_STR(opened[0], SNES);
	CHECK(select_count == 0);
}

int main(void) {
	testKey();
	testSystems();
	testJump();
	testInit();
	buildLists();
	testListReady();
	testSystemJump();
	testListJump();
	testRootJump();
	testReplacedRoot();
	plugin.quit();
	CHECK_DONE();
}
