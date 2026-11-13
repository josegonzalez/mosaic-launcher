// The focus mode plugin: its model on its own, then the plugin against a
// fake host with fake entry sets, on a fixture card.

#include <stdlib.h>
#include "check.h"
#include "fakeproviders.h"
#include "fakesets.h"
#include "../../plugins/focus-mode/focus_mode.c"

struct MosaicEntry {
	char path[1024];
	char name[128];
	int type;
};
struct MosaicList {
	MosaicEntry* items[FOCUS_MAX + 1];
	int count;
};

#define CARD TEST_SDCARD_PATH

// cfg: focus mode's own
static char focus_on[8] = "", focus_set[80] = "", pick_tile[8] = "", pick_button[8] = "";
#define ACTIONS_MAX 4
static struct {
	char id[64], label[64];
	int (*available)(const char*, void*);
	void (*run)(const char*, void*);
} actions[ACTIONS_MAX];
static int action_count = 0;
static char tiles[ACTIONS_MAX][64];
static int tile_count = 0, tile_row = -1;
static int (*tile_is_on)(void*) = NULL;
static char bound[64];
static int bound_button = -1;
static int reloads = 0;
static char toasted[128];
static void (*set_changed_fn)(const char*, const char*, void*);
static int chooses = 0, choice_count = 0, choice_selected = -1;
static char choices[8][160];
static void (*choice_pick)(int, void*);

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "on") && focus_on[0]) return focus_on;
	if (!strcmp(key, "set") && focus_set[0]) return focus_set;
	if (!strcmp(key, "pick_tile") && pick_tile[0]) return pick_tile;
	if (!strcmp(key, "pick_button") && pick_button[0]) return pick_button;
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) {
	if (!strcmp(key, "on")) snprintf(focus_on, sizeof(focus_on), "%s", value);
	if (!strcmp(key, "set")) snprintf(focus_set, sizeof(focus_set), "%s", value);
}
static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = calloc(1, sizeof(MosaicEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
	e->type = type;
	return e;
}
static MosaicList* fakeListNew(void) { return calloc(1, sizeof(MosaicList)); }
static void fakeListPush(MosaicList* l, MosaicEntry* e) { l->items[l->count++] = e; }
static void fakeRedraw(void) {}
static int fakeRegister(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)data;
	if (action_count >= ACTIONS_MAX) return -1;
	snprintf(actions[action_count].id, sizeof(actions[0].id), "%s", id);
	snprintf(actions[action_count].label, sizeof(actions[0].label), "%s", label);
	actions[action_count].available = available;
	actions[action_count].run = run;
	action_count++;
	return 0;
}
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)icon; (void)icon_off;
	if (tile_count >= ACTIONS_MAX) return -1;
	snprintf(tiles[tile_count++], sizeof(tiles[0]), "%s", id);
	if (!strcmp(id, "focus-mode.toggle")) {
		tile_row = row;
		tile_is_on = is_on;
	}
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen; (void)trigger;
	snprintf(bound, sizeof(bound), "%s", id);
	bound_button = button;
	return 0;
}
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	if (!strcmp(name, "launcher.set_changed")) set_changed_fn = on_event;
	return 0;
}
static int fakeSetListCall(const char* name, void (*each)(const char*, void*), void* data) {
	int i = fakeSetFind(name);
	if (i < 0) return -1;
	if (each) fake_set_list[i](name, each, data);
	return 0;
}
static int fakeChoose(const char* title, const char* const* list, int count, int selected, void (*on_pick)(int, void*), void* data) {
	(void)title;
	(void)data;
	chooses++;
	choice_count = count;
	choice_selected = selected;
	for (int i = 0; i < count && i < 8; i++) snprintf(choices[i], sizeof(choices[0]), "%s", list[i]);
	choice_pick = on_pick;
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeReload(void) { reloads++; }

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
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.toast = fakeToast,
	.reload_lists = fakeReload,
	.quick_tile = fakeQuickTile,
	.bind_button = fakeBind,
	.choose = fakeChoose,
	.set_list = fakeSetListCall,
	.set_names = fakeSetNames,
	.is_filtered = fakeIsFiltered,
	.subscribe = fakeSubscribe,
	.event_value = fakeEventValue,
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
	mkdir(CARD, 0755);
	mkdir(CARD "/Collections", 0755);
	mkdir(CARD "/Roms", 0755);
	mkdir(CARD "/Roms/GB", 0755);
	mkdir(CARD "/Roms/PS", 0755);
	mkdir(CARD "/Roms/PS/Disc Game", 0755);
	mkdir(CARD "/Tools", 0755);
	mkdir(CARD "/Tools/test", 0755);
	mkdir(CARD "/Tools/test/Clock.pak", 0755);
	writeText(CARD "/Roms/GB/Tetris.gb", "");
	writeText(CARD "/Roms/GB/Kirby.gb", "");
}

///////////////////////////////////////
// the model

static int typeAll(const char* path) {
	size_t len = strlen(path);
	return len > 4 && !strcmp(path + len - 4, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
}

static int typeNoTetris(const char* path) {
	return strstr(path, "Tetris") ? -1 : typeAll(path);
}

static void testList(void) {
	static FocusList list;
	list.count = 0;
	CHECK(Focus_add(&list, "/sd/Roms/GB/Tetris.gb"));
	CHECK(!Focus_add(&list, "/sd/Roms/GB/Tetris.gb"));
	CHECK(!Focus_add(&list, ""));
	CHECK(!Focus_add(&list, NULL));
	CHECK(Focus_add(&list, "/sd/Tools/test/Clock.pak"));
	CHECK(list.count == 2);
	for (int i = 0; i < FOCUS_MAX + 4; i++) {
		char path[64];
		snprintf(path, sizeof(path), "/sd/Roms/GB/%i.gb", i);
		Focus_add(&list, path);
	}
	CHECK(list.count == FOCUS_MAX);

	CHECK(Focus_focusable("favorites") && Focus_focusable("tags:RPG") && Focus_focusable("smart:Mario"));
	CHECK(!Focus_focusable("hidden") && !Focus_focusable("") && !Focus_focusable(NULL));
	char label[64];
	Focus_setLabel("favorites", "Favs", "Tag %s", "Collection %s", label, sizeof(label));
	CHECK_STR(label, "Favs");
	Focus_setLabel("tags:RPG", "Favs", "Tag %s", "Collection %s", label, sizeof(label));
	CHECK_STR(label, "Tag RPG");
	Focus_setLabel("smart:Zelda", "Favs", "Tag %s", "Collection %s", label, sizeof(label));
	CHECK_STR(label, "Collection Zelda");
	Focus_setLabel("x.y", "Favs", "Tag %s", "Collection %s", label, sizeof(label));
	CHECK_STR(label, "x.y");
}

static void testRows(void) {
	static FocusList list;
	static FocusRow out[FOCUS_MAX + 1];

	list.count = 0;
	CHECK(Focus_rows(&list, "/sd", typeAll, out) == 0);

	Focus_add(&list, "/sd/Roms/GB/Tetris.gb");
	Focus_add(&list, "/sd/Tools/test/Clock.pak");
	CHECK(Focus_rows(&list, "/sd", typeAll, out) == 3);
	CHECK_STR(out[0].path, "/sd/Roms/GB/Tetris.gb");
	CHECK(out[0].type == MOSAIC_ENTRY_ROM && out[0].name == NULL);
	CHECK_STR(out[1].path, "/sd/Tools/test/Clock.pak");
	CHECK(out[1].type == MOSAIC_ENTRY_PAK);
	CHECK_STR(out[2].path, "/sd/Roms");
	CHECK(out[2].type == MOSAIC_ENTRY_DIR);
	CHECK_STR(out[2].name, "All systems");

	// games off the card are skipped, and with none left it's stock's root
	CHECK(Focus_rows(&list, "/sd", typeNoTetris, out) == 2);
	CHECK_STR(out[0].path, "/sd/Tools/test/Clock.pak");
	list.count = 1;
	CHECK(Focus_rows(&list, "/sd", typeNoTetris, out) == 0);

	// a full list still has room for All systems
	list.count = 0;
	for (int i = 0; i < FOCUS_MAX; i++) {
		char path[64];
		snprintf(path, sizeof(path), "/sd/Roms/GB/%i.gb", i);
		Focus_add(&list, path);
	}
	CHECK(Focus_rows(&list, "/sd", typeAll, out) == FOCUS_MAX + 1);
	CHECK_STR(out[FOCUS_MAX].name, "All systems");
}

///////////////////////////////////////
// the plugin

static const char* fav_paths[3];
static int fav_count = 0;
static void favList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	for (int i = 0; i < fav_count; i++) add(fav_paths[i], ctx);
}
static void rpgList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(CARD "/Roms/GB/Tetris.gb", ctx);
}
static void smartList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(CARD "/Roms/PS/Disc Game", ctx); // a game folder
}
static int noHas(const char* name, const char* path) {
	(void)name;
	(void)path;
	return 0;
}

static int findAction(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) return i;
	}
	return -1;
}

static void start(void) {
	action_count = tile_count = 0;
	bound[0] = '\0';
	set_changed_fn = NULL;
	CHECK(plugin.init(&fake) == 0);
}

static void testPlugin(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK_STR(p->id, "focus-mode");
	CHECK(p->root_replace != NULL);
	CHECK(p->root_with_entries == 0); // the decision: no other plugins' root entries
	fake_set_count = 0;
	start();
	int toggle = findAction("focus-mode.toggle"), pick = findAction("focus-mode.pick");
	CHECK(toggle >= 0 && pick >= 0);
	if (toggle < 0 || pick < 0) return;
	CHECK_STR(actions[toggle].label, "Focus");
	CHECK_STR(actions[pick].label, "Focus on");
	CHECK(tile_count == 2 && !strcmp(tiles[0], "focus-mode.toggle") && !strcmp(tiles[1], "focus-mode.pick"));
	CHECK(tile_row == MOSAIC_QUICK_TOP);
	CHECK(bound[0] == '\0'); // no button by default
	CHECK(set_changed_fn != NULL);
	CHECK(actions[toggle].available(NULL, NULL));
	CHECK(tile_is_on(NULL)); // on by default

	// favorites off: no set, so stock's root, and nothing to pick
	CHECK(p->root_replace() == NULL);
	CHECK(!actions[pick].available(NULL, NULL));

	// no favorites on the card yet: stock's root
	fakeSetRegister("favorites", noHas, favList);
	fav_paths[0] = CARD "/Roms/GB/Gone.gb";
	fav_count = 1;
	CHECK(p->root_replace() == NULL);

	fav_paths[0] = CARD "/Roms/GB/Kirby.gb";
	fav_paths[1] = CARD "/Roms/GB/Gone.gb";
	fav_paths[2] = CARD "/Tools/test/Clock.pak";
	fav_count = 3;
	MosaicList* root = p->root_replace();
	CHECK(root && root->count == 3);
	if (root && root->count == 3) {
		CHECK_STR(root->items[0]->path, CARD "/Roms/GB/Kirby.gb");
		CHECK(root->items[0]->type == MOSAIC_ENTRY_ROM);
		CHECK_STR(root->items[1]->path, CARD "/Tools/test/Clock.pak");
		CHECK(root->items[1]->type == MOSAIC_ENTRY_PAK);
		CHECK_STR(root->items[2]->path, CARD "/Roms");
		CHECK_STR(root->items[2]->name, "All systems");
	}
	freeList(root);

	// a change to the focused set lists the root again, other sets' don't
	reloads = 0;
	set_changed_fn("launcher.set_changed", "name=favorites", NULL);
	CHECK(reloads == 1);
	set_changed_fn("launcher.set_changed", "name=tags:RPG", NULL);
	CHECK(reloads == 1);

	// picking another set: every set but hidden
	fakeSetRegister("hidden", noHas, rpgList);
	fakeSetRegister("tags:RPG", noHas, rpgList);
	fakeSetRegister("smart:Zelda", noHas, smartList);
	CHECK(actions[pick].available(NULL, NULL));
	chooses = 0;
	actions[pick].run(NULL, NULL);
	CHECK(chooses == 1 && choice_count == 3 && choice_selected == 0);
	CHECK_STR(choices[0], "Favorites");
	CHECK_STR(choices[1], "Tag: RPG");
	CHECK_STR(choices[2], "Collection: Zelda");
	snprintf(focus_on, sizeof(focus_on), "off");
	reloads = 0;
	choice_pick(1, NULL);
	CHECK_STR(focus_set, "tags:RPG");
	CHECK_STR(focus_on, "on"); // picking turns it on
	CHECK_STR(toasted, "Focusing on Tag: RPG");
	CHECK(reloads == 1);
	root = p->root_replace();
	CHECK(root && root->count == 2);
	if (root && root->count == 2) CHECK_STR(root->items[0]->path, CARD "/Roms/GB/Tetris.gb");
	freeList(root);
	set_changed_fn("launcher.set_changed", "name=favorites", NULL);
	CHECK(reloads == 1);
	set_changed_fn("launcher.set_changed", "name=tags:RPG", NULL);
	CHECK(reloads == 2);
	actions[pick].run(NULL, NULL);
	CHECK(choice_selected == 1);
	choice_pick(-1, NULL);
	CHECK_STR(focus_set, "tags:RPG");

	// a smart collection's game folder is a folder on the root
	choice_pick(2, NULL);
	root = p->root_replace();
	CHECK(root && root->count == 2);
	if (root && root->count == 2) {
		CHECK_STR(root->items[0]->path, CARD "/Roms/PS/Disc Game");
		CHECK(root->items[0]->type == MOSAIC_ENTRY_DIR);
	}
	freeList(root);

	// the tile turns it off, back to stock's root, and on again
	snprintf(focus_set, sizeof(focus_set), "favorites");
	reloads = 0;
	actions[toggle].run(NULL, NULL);
	CHECK_STR(focus_on, "off");
	CHECK(!tile_is_on(NULL));
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Focus mode off");
	CHECK(p->root_replace() == NULL);
	set_changed_fn("launcher.set_changed", "name=favorites", NULL);
	CHECK(reloads == 1); // off: nothing to list again
	actions[toggle].run(NULL, NULL);
	CHECK_STR(focus_on, "on");
	CHECK(reloads == 2);
	CHECK_STR(toasted, "Focus mode on");
	root = p->root_replace();
	CHECK(root && root->count == 3);
	freeList(root);

	// the settings: no tile, and a button
	snprintf(pick_tile, sizeof(pick_tile), "off");
	snprintf(pick_button, sizeof(pick_button), "L2");
	start();
	CHECK(tile_count == 1);
	CHECK_STR(bound, "focus-mode.pick");
	CHECK(bound_button == MOSAIC_BUTTON_L2);
	pick_tile[0] = pick_button[0] = '\0';

	// a launcher without sets: the toggle alone, and stock's root
	fake.size = offsetof(MosaicHost, set_register);
	start();
	CHECK(action_count == 1 && tile_count == 1);
	CHECK(p->root_replace() == NULL);
	fake.size = sizeof(MosaicHost);
	fake_set_count = 0;
}

int main(void) {
	makeCard();
	testList();
	testRows();
	testPlugin();
	CHECK_DONE();
}
