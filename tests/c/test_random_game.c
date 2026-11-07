// The random game plugin: its model on its own, with a random source the
// test controls, then the plugin against a fake host on a fixture card.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/random-game/random_game.c"

struct MosaicEntry {
	char path[1024];
	char name[128];
	int type;
};
struct MosaicList {
	MosaicEntry* items[16];
	int count;
};

#define CARD TEST_SDCARD_PATH
#define GB CARD "/Roms/Game Boy (GB)"
#define MGB CARD "/Roms/Game Boy (MGB)"
#define NES CARD "/Roms/NES (FC)"

static char cfg_root[8] = "", cfg_tile[8] = "", cfg_unplayed[8] = "";
static char current[1024] = CARD;
static const char* recents[4];
static int recent_count = 0;
static char opened[1024];
static int opened_type = -1, opens = 0;
static char toasted[64];

#define MAX_FAKE_ACTIONS 4
static struct {
	char id[64];
	char label[32];
	int (*available)(void*);
	void (*run)(void*);
} actions[MAX_FAKE_ACTIONS];
static int action_count = 0;
static char bound[64];
static int bound_button = -1;
static char tile_action[64];

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "root") && cfg_root[0]) return cfg_root;
	if (!strcmp(key, "tile") && cfg_tile[0]) return cfg_tile;
	if (!strcmp(key, "unplayed") && cfg_unplayed[0]) return cfg_unplayed;
	return fallback;
}
static void fakeCfgSet(const char* id, const char* key, const char* value) { (void)id; (void)key; (void)value; }
static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = calloc(1, sizeof(MosaicEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
	e->type = type;
	return e;
}
static MosaicList* fakeListNew(void) { return calloc(1, sizeof(MosaicList)); }
static void fakeListPush(MosaicList* l, MosaicEntry* e) { l->items[l->count++] = e; }
static const char* fakeCurrentPath(void) { return current; }
static void fakeRedraw(void) {}
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)data;
	if (action_count >= MAX_FAKE_ACTIONS) return -1;
	snprintf(actions[action_count].id, sizeof(actions[0].id), "%s", id);
	snprintf(actions[action_count].label, sizeof(actions[0].label), "%s", label);
	actions[action_count].available = available;
	actions[action_count].run = run;
	action_count++;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen; (void)trigger;
	snprintf(bound, sizeof(bound), "%s", id);
	bound_button = button;
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static int fakeOpenPath(const char* path, int type) {
	snprintf(opened, sizeof(opened), "%s", path);
	opened_type = type;
	opens++;
	return 1;
}
static int fakeRecentsCount(void) { return recent_count; }
static const char* fakeRecentPath(int i) { return i < recent_count ? recents[i] : NULL; }
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)row; (void)icon; (void)icon_off; (void)is_on;
	snprintf(tile_action, sizeof(tile_action), "%s", id);
	return 0;
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
	.entry_new = fakeEntryNew,
	.list_new = fakeListNew,
	.list_push = fakeListPush,
	.current_path = fakeCurrentPath,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.open_path = fakeOpenPath,
	.recents_count = fakeRecentsCount,
	.recent_path = fakeRecentPath,
	.quick_tile = fakeQuickTile,
};

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static void freeList(MosaicList* list) {
	if (!list) return;
	for (int i = 0; i < list->count; i++) free(list->items[i]);
	free(list);
}

static void makeCard(void) {
	(void)system("rm -rf '" CARD "'");
	(void)system("mkdir -p '" CARD "/.system/test/paks/Emus/GB.pak' '" CARD "/Emus/test/MGB.pak' '" CARD "/Emus/test/FC.pak'"
		" '" GB "' '" MGB "' '" NES "/Disc Game' '" NES "/Hacks' '" NES "/Ports/Doom.pak'"
		" '" CARD "/Roms/Nothing (XX)' '" CARD "/.userdata/test/launcher'");
	writeText(CARD "/.system/test/paks/Emus/GB.pak/launch.sh", "");
	writeText(CARD "/Emus/test/MGB.pak/launch.sh", "");
	writeText(CARD "/Emus/test/FC.pak/launch.sh", "");
	writeText(GB "/Tetris.gb", "");
	writeText(GB "/Kirby.gb", "");
	writeText(GB "/.Hidden.gb", "");
	writeText(GB "/Old.gb.disabled", "");
	writeText(GB "/Secret.gb", "");
	writeText(GB "/map.txt", "Secret.gb\t.Secret\r\nTetris.gb\tTetris DX\n");
	writeText(MGB "/Pokemon.gb", "");
	writeText(NES "/Mario.nes", "");
	writeText(NES "/Disc Game/Disc Game.cue", "");
	writeText(NES "/Disc Game/Disc Game.bin", "");
	writeText(NES "/Hacks/Zelda.nes", "");
	writeText(NES "/Ports/Doom.pak/launch.sh", "");
	writeText(CARD "/Roms/Nothing (XX)/Game.x", "");
}

///////////////////////////////////////
// the model

// A random source that hands out the numbers it's given, in turn.
typedef struct Script {
	const unsigned long* values;
	int count;
	int next;
} Script;

static unsigned long scripted(unsigned long below, void* data) {
	Script* s = data;
	unsigned long value = s->values[s->next++ % s->count];
	CHECK(value < below);
	return value;
}

static void testSystems(void) {
	char key[64];
	RG_systemKey("Game Boy (GB)", key, sizeof(key));
	CHECK_STR(key, "Game Boy (");
	RG_systemKey("Game Boy (MGB)", key, sizeof(key));
	CHECK_STR(key, "Game Boy (");
	RG_systemKey("Arcade", key, sizeof(key));
	CHECK_STR(key, "Arcade");
	RG_systemKey("Super (Japan) (SFC)", key, sizeof(key));
	CHECK_STR(key, "Super (Japan) (");

	RGLibrary lib;
	RG_init(&lib);
	CHECK(RG_add(&lib, "/r/Game Boy (GB)/Tetris.gb", MOSAIC_ENTRY_ROM, "Game Boy (GB)") == 0);
	CHECK(RG_add(&lib, "/r/NES (FC)/Mario.nes", MOSAIC_ENTRY_ROM, "NES (FC)") == 0);
	CHECK(RG_add(&lib, "/r/Game Boy (MGB)/Pokemon.gb", MOSAIC_ENTRY_ROM, "Game Boy (MGB)") == 0);
	CHECK(lib.count == 3);
	CHECK(lib.system_count == 2);
	CHECK(lib.games[0].system == lib.games[2].system);
	CHECK(lib.games[1].system != lib.games[0].system);

	CHECK(RG_systemOf(&lib, "/r", "/r/Game Boy (MGB)") == lib.games[0].system);
	CHECK(RG_systemOf(&lib, "/r", "/r/Game Boy (GB)/Hacks/Deep") == lib.games[0].system);
	CHECK(RG_systemOf(&lib, "/r", "/r/NES (FC)") == lib.games[1].system);
	CHECK(RG_systemOf(&lib, "/r", "/r/Arcade") == -1);
	CHECK(RG_systemOf(&lib, "/r", "/r") == -1);
	CHECK(RG_systemOf(&lib, "/r", "/rx/NES (FC)") == -1);
	CHECK(RG_systemOf(&lib, "/r", "/Collections") == -1);
	RG_free(&lib);
	CHECK(lib.count == 0);
}

static void testNames(void) {
	CHECK(RG_hiddenName(".Hidden.gb"));
	CHECK(RG_hiddenName("Old.gb.disabled"));
	CHECK(RG_hiddenName("map.txt"));
	CHECK(!RG_hiddenName("Tetris.gb"));

	char name[64];
	const char* map = "Secret.gb\t.Secret\r\nTetris.gb\tTetris DX\nTetris\tNot this\n";
	CHECK(RG_mapLookup(map, "Secret.gb", name, sizeof(name)));
	CHECK_STR(name, ".Secret");
	CHECK(RG_mapLookup(map, "Tetris.gb", name, sizeof(name)));
	CHECK_STR(name, "Tetris DX");
	CHECK(!RG_mapLookup(map, "Tetris.g", name, sizeof(name)));
	CHECK(!RG_mapLookup(map, "Kirby.gb", name, sizeof(name)));
	CHECK(!RG_mapLookup("", "Kirby.gb", name, sizeof(name)));
}

static void testHiddenSet(void) {
	RGPathSet set;
	RG_setInit(&set);
	CHECK(RG_setLoad(&set, CARD "/missing.txt") == 0);
	CHECK(set.count == 0);
	CHECK(!RG_isHidden(&set, "/r/GB/Tetris.gb"));

	writeText(CARD "/hidden-test.txt", "# a comment\n\n/r/GB/Tetris.gb\r\n/r/PS/Disc Game/\n/r/GB/Tetris.gb\n");
	CHECK(RG_setLoad(&set, CARD "/hidden-test.txt") == 0);
	CHECK(set.count == 2);
	CHECK(RG_isHidden(&set, "/r/GB/Tetris.gb"));
	CHECK(!RG_isHidden(&set, "/r/GB/Tetris.gbc"));
	CHECK(RG_isHidden(&set, "/r/PS/Disc Game"));
	CHECK(RG_isHidden(&set, "/r/PS/Disc Game/Disc Game.m3u")); // inside a hidden folder
	CHECK(!RG_isHidden(&set, "/r/PS/Other.bin"));
	CHECK(!RG_isHidden(NULL, "/r/GB/Tetris.gb"));

	// a new read replaces the old
	writeText(CARD "/hidden-test.txt", "/r/GB/Kirby.gb\n");
	CHECK(RG_setLoad(&set, CARD "/hidden-test.txt") == 0);
	CHECK(set.count == 1);
	CHECK(!RG_isHidden(&set, "/r/GB/Tetris.gb"));
	RG_setClear(&set);
}

static void testPlayed(void) {
	const char* list[] = { "/r/GB/Tetris.gb", "/r/PS/Disc Game/Disc Game.m3u", NULL };
	CHECK(RG_isPlayed("/r/GB/Tetris.gb", list, 3));
	CHECK(!RG_isPlayed("/r/GB/Tetris", list, 3));
	CHECK(!RG_isPlayed("/r/GB/Tetris.gbc", list, 3));
	CHECK(RG_isPlayed("/r/PS/Disc Game", list, 3)); // a game folder by its .m3u
	CHECK(!RG_isPlayed("/r/PS/Disc", list, 3));
	CHECK(!RG_isPlayed("/r/GB/Tetris.gb", list, 0));
}

static void testWeights(void) {
	RGLibrary lib;
	RG_init(&lib);
	RG_add(&lib, "/r/GB (GB)/Tetris.gb", MOSAIC_ENTRY_ROM, "GB (GB)");
	RG_add(&lib, "/r/GB (GB)/Kirby.gb", MOSAIC_ENTRY_ROM, "GB (GB)");
	RG_add(&lib, "/r/GB (GB)/Hidden.gb", MOSAIC_ENTRY_ROM, "GB (GB)");
	RG_add(&lib, "/r/NES (FC)/Mario.nes", MOSAIC_ENTRY_ROM, "NES (FC)");

	RGPathSet hidden_set;
	RG_setInit(&hidden_set);
	RG_setAdd(&hidden_set, "/r/GB (GB)/Hidden.gb");
	const char* played[] = { "/r/GB (GB)/Tetris.gb" };

	unsigned w[4];
	CHECK(RG_weights(&lib, -1, &hidden_set, played, 1, 4, w) == 9);
	CHECK(w[0] == 1 && w[1] == 4 && w[2] == 0 && w[3] == 4);

	// one system
	int gb = lib.games[0].system;
	CHECK(RG_weights(&lib, gb, &hidden_set, played, 1, 4, w) == 5);
	CHECK(w[0] == 1 && w[1] == 4 && w[2] == 0 && w[3] == 0);

	// no unplayed preference: every game alike
	CHECK(RG_weights(&lib, -1, &hidden_set, played, 1, 1, w) == 3);
	CHECK(w[0] == 1 && w[1] == 1 && w[2] == 0 && w[3] == 1);
	CHECK(RG_weights(&lib, -1, NULL, NULL, 0, 0, w) == 4);

	RG_setClear(&hidden_set);
	RG_free(&lib);
}

static void testPick(void) {
	unsigned w[] = { 1, 3, 0, 2 };
	const unsigned long values[] = { 0, 1, 3, 4, 5 };
	const int want[] = { 0, 1, 1, 3, 3 };
	Script s = { values, 5, 0 };
	for (int i = 0; i < 5; i++) CHECK(RG_pick(w, 4, 6, scripted, &s) == want[i]);

	unsigned none[] = { 0, 0 };
	CHECK(RG_pick(none, 2, 0, scripted, &s) == -1);

	// weighted by folder size: a system with three games is picked three
	// times as often as one with one, over every number the source can give
	RGLibrary lib;
	RG_init(&lib);
	RG_add(&lib, "/r/GB (GB)/A.gb", MOSAIC_ENTRY_ROM, "GB (GB)");
	RG_add(&lib, "/r/GB (GB)/B.gb", MOSAIC_ENTRY_ROM, "GB (GB)");
	RG_add(&lib, "/r/NES (FC)/C.nes", MOSAIC_ENTRY_ROM, "NES (FC)");
	RG_add(&lib, "/r/GB (GB)/D.gb", MOSAIC_ENTRY_ROM, "GB (GB)");
	unsigned weights[4];
	unsigned long total = RG_weights(&lib, -1, NULL, NULL, 0, RG_UNPLAYED_WEIGHT, weights);
	CHECK(total == 16);
	int per_system[2] = { 0, 0 };
	unsigned long every[16];
	for (unsigned long r = 0; r < 16; r++) every[r] = r;
	Script all = { every, 16, 0 };
	for (int i = 0; i < 16; i++) {
		int at = RG_pick(weights, 4, total, scripted, &all);
		CHECK(at >= 0);
		if (at >= 0) per_system[lib.games[at].system]++;
	}
	CHECK(per_system[lib.games[0].system] == 12);
	CHECK(per_system[lib.games[2].system] == 4);

	// an unplayed game is RG_UNPLAYED_WEIGHT times as likely as a played one
	const char* played[] = { "/r/GB (GB)/A.gb" };
	total = RG_weights(&lib, -1, NULL, played, 1, RG_UNPLAYED_WEIGHT, weights);
	CHECK(total == 13);
	int a = 0, b = 0;
	all.next = 0;
	for (int i = 0; i < 13; i++) {
		int at = RG_pick(weights, 4, total, scripted, &all);
		if (at == 0) a++;
		if (at == 1) b++;
	}
	CHECK(a == 1);
	CHECK(b == RG_UNPLAYED_WEIGHT);
	RG_free(&lib);
}

static void testXorshift(void) {
	unsigned long long x = 0, y = 0;
	unsigned long long first = RG_xorshift(&x);
	CHECK(first != 0);
	CHECK(RG_xorshift(&y) == first); // the same seed, the same numbers
	CHECK(RG_xorshift(&x) != first);
}

///////////////////////////////////////
// the plugin

static int findAction(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) return i;
	}
	return -1;
}

static void start(void) {
	quit();
	action_count = 0;
	bound[0] = tile_action[0] = toasted[0] = opened[0] = '\0';
	bound_button = opened_type = -1;
	opens = 0;
	CHECK(mosaic_plugin_get(&fake)->init(&fake) == 0);
}

static void testScan(void) {
	start();
	scan();
	// Tetris, Kirby, Pokemon (collated with GB), Mario, Disc Game, Zelda, Doom
	CHECK(library.count == 7);
	CHECK(library.system_count == 2);
	int disc = 0, pak = 0, secret = 0, nothing = 0;
	for (int i = 0; i < library.count; i++) {
		const RGGame* g = &library.games[i];
		if (strstr(g->path, "/Disc Game")) {
			disc++;
			CHECK_STR(g->path, NES "/Disc Game");
			CHECK(g->type == MOSAIC_ENTRY_DIR);
		}
		if (strstr(g->path, "Doom.pak")) {
			pak++;
			CHECK(g->type == MOSAIC_ENTRY_PAK);
		}
		if (strstr(g->path, "Secret") || strstr(g->path, "Hidden") || strstr(g->path, "Old")) secret++;
		if (strstr(g->path, "Nothing")) nothing++;
	}
	CHECK(disc == 1);
	CHECK(pak == 1);
	CHECK(secret == 0); // stock's hidden names and map.txt's
	CHECK(nothing == 0); // no emulator
}

static void testButton(void) {
	start();
	int at = findAction("random-game.pick");
	CHECK(at >= 0);
	if (at < 0) return;
	CHECK_STR(actions[at].label, "RANDOM");
	CHECK_STR(bound, "random-game.pick");
	CHECK(bound_button == MOSAIC_BUTTON_Y);
	CHECK(findAction("random-game.tile") < 0); // the tile is off by default

	// where Y picks
	snprintf(current, sizeof(current), "%s", CARD);
	CHECK(actions[at].available(NULL));
	snprintf(current, sizeof(current), "%s", GB "/Sub");
	CHECK(actions[at].available(NULL));
	snprintf(current, sizeof(current), "%s", CARD "/Recently Played");
	CHECK(!actions[at].available(NULL));
	snprintf(current, sizeof(current), "%s", CARD "/Collections/Favorites.txt");
	CHECK(!actions[at].available(NULL));
	snprintf(current, sizeof(current), "%s", CARD "/Tools/test");
	CHECK(!actions[at].available(NULL));
	snprintf(current, sizeof(current), "%s", CARD "/Roms/Nothing (XX)"); // no emulator, so no games
	CHECK(!actions[at].available(NULL));

	// in a system folder: only that system, collated folders included,
	// and never a hidden game
	writeText(CARD "/.userdata/test/launcher/hidden.txt", GB "/Tetris.gb\n" MGB "\n");
	snprintf(current, sizeof(current), "%s", MGB);
	for (int i = 0; i < 20; i++) {
		seed = (unsigned long long)i * 7919 + 1;
		actions[at].run(NULL);
		CHECK_STR(opened, GB "/Kirby.gb");
		CHECK(opened_type == MOSAIC_ENTRY_ROM);
	}

	// everything there hidden
	writeText(CARD "/.userdata/test/launcher/hidden.txt", GB "\n" MGB "\n");
	opened[0] = '\0';
	actions[at].run(NULL);
	CHECK_STR(opened, "");
	CHECK_STR(toasted, "No games to pick from");

	// on the root: any system
	unlink(CARD "/.userdata/test/launcher/hidden.txt");
	snprintf(current, sizeof(current), "%s", CARD);
	int nes = 0, gb = 0;
	for (int i = 0; i < 200; i++) {
		seed = (unsigned long long)i * 104729 + 3;
		actions[at].run(NULL);
		if (!strncmp(opened, NES "/", strlen(NES "/"))) nes++;
		if (!strncmp(opened, CARD "/Roms/Game Boy (", strlen(CARD "/Roms/Game Boy ("))) gb++;
	}
	CHECK(nes > 0);
	CHECK(gb > 0);
	CHECK(nes + gb == 200);

	// played games: with every game but one played, the unplayed one comes
	// up most
	const char* gb_played[] = { GB "/Tetris.gb", MGB "/Pokemon.gb" };
	recents[0] = gb_played[0];
	recents[1] = gb_played[1];
	recent_count = 2;
	snprintf(current, sizeof(current), "%s", GB);
	int kirby = 0;
	for (int i = 0; i < 300; i++) {
		seed = (unsigned long long)i * 15485863 + 5;
		actions[at].run(NULL);
		if (!strcmp(opened, GB "/Kirby.gb")) kirby++;
	}
	CHECK(kirby > 150); // 4 in 6 expected
	CHECK(kirby < 300);
	recent_count = 0;
}

static void testRootRow(void) {
	cfg_root[0] = '\0';
	start();
	MosaicList* root = fakeListNew();
	plugin.root_entries(root);
	CHECK(root->count == 1);
	if (root->count == 1) {
		CHECK_STR(root->items[0]->path, CARD "/Random game");
		CHECK_STR(root->items[0]->name, "Random game");
		CHECK(root->items[0]->type == MOSAIC_ENTRY_DIR);
	}
	freeList(root);

	CHECK(plugin.virtual_dir(CARD "/Roms") == NULL);

	// opening it shows a game, and the next frame launches it
	snprintf(current, sizeof(current), "%s", CARD);
	MosaicList* list = plugin.virtual_dir(CARD "/Random game");
	CHECK(list && list->count == 1);
	char picked[1024] = "";
	if (list && list->count == 1) snprintf(picked, sizeof(picked), "%s", list->items[0]->path);
	freeList(list);
	CHECK(opens == 0);
	snprintf(current, sizeof(current), "%s", CARD "/Random game");
	plugin.tick(1);
	CHECK(opens == 1);
	CHECK_STR(opened, picked);
	plugin.tick(2);
	CHECK(opens == 1); // once

	// a reload while it's open keeps the game and doesn't launch again
	list = plugin.virtual_dir(CARD "/Random game");
	CHECK(list && list->count == 1);
	if (list && list->count == 1) CHECK_STR(list->items[0]->path, picked);
	freeList(list);
	plugin.tick(3);
	CHECK(opens == 1);

	// leaving forgets it, so the next visit picks and launches again
	snprintf(current, sizeof(current), "%s", CARD);
	plugin.tick(4);
	CHECK(shown_path[0] == '\0');
	list = plugin.virtual_dir(CARD "/Random game");
	freeList(list);
	snprintf(current, sizeof(current), "%s", CARD "/Random game");
	plugin.tick(5);
	CHECK(opens == 2);

	// Y works there too, from every system
	int at = findAction("random-game.pick");
	CHECK(at >= 0 && actions[at].available(NULL));

	// the setting turns the row off
	snprintf(cfg_root, sizeof(cfg_root), "off");
	start();
	root = fakeListNew();
	plugin.root_entries(root);
	CHECK(root->count == 0);
	freeList(root);
	cfg_root[0] = '\0';
}

static void testTile(void) {
	snprintf(cfg_tile, sizeof(cfg_tile), "on");
	start();
	int at = findAction("random-game.tile");
	CHECK(at >= 0);
	CHECK_STR(tile_action, "random-game.tile");
	if (at >= 0) {
		CHECK_STR(actions[at].label, "Random");
		CHECK(actions[at].available(NULL));
		snprintf(current, sizeof(current), "%s", CARD "/Tools/test"); // anywhere
		actions[at].run(NULL);
		CHECK(opens == 1);
		CHECK(!strncmp(opened, CARD "/Roms/", strlen(CARD "/Roms/")));
	}
	cfg_tile[0] = '\0';
}

static void testOldLauncher(void) {
	fake.size = offsetof(MosaicHost, open_path);
	start();
	CHECK(action_count == 0);
	MosaicList* root = fakeListNew();
	plugin.root_entries(root);
	CHECK(root->count == 0);
	freeList(root);
	fake.size = sizeof(MosaicHost);
}

int main(void) {
	makeCard();
	testSystems();
	testNames();
	testHiddenSet();
	testPlayed();
	testWeights();
	testPick();
	testXorshift();
	testScan();
	testButton();
	testRootRow();
	testTile();
	testOldLauncher();
	quit();
	CHECK_DONE();
}
