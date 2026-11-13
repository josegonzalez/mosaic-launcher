// The focus mode plugin: its model on its own, then the plugin against a
// fake host, reading the favorites collection from a fixture card.

#include <stdlib.h>
#include "check.h"
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

// cfg: favorites' collection setting and focus mode's own
static const char* favorites_collection = NULL;
static char focus_on[8] = "";
static char last_action[64], last_label[64];
static int (*action_available)(const char*, void*) = NULL;
static void (*action_run)(const char*, void*) = NULL;
static char tile_action[64];
static int tile_row = -1;
static int (*tile_is_on)(void*) = NULL;
static int reloads = 0;
static char toasted[64];

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgRead(const char* id, const char* key, const char* fallback) {
	if (!strcmp(id, "favorites") && !strcmp(key, "collection") && favorites_collection) return favorites_collection;
	return fallback;
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "on") && focus_on[0]) return focus_on;
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) {
	if (!strcmp(key, "on")) snprintf(focus_on, sizeof(focus_on), "%s", value);
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
	snprintf(last_action, sizeof(last_action), "%s", id);
	snprintf(last_label, sizeof(last_label), "%s", label);
	action_available = available;
	action_run = run;
	return 0;
}
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)icon; (void)icon_off;
	snprintf(tile_action, sizeof(tile_action), "%s", id);
	tile_row = row;
	tile_is_on = is_on;
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeReload(void) { reloads++; }

static const MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/test",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
	.cfg_read = fakeCfgRead,
	.entry_new = fakeEntryNew,
	.list_new = fakeListNew,
	.list_push = fakeListPush,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.toast = fakeToast,
	.reload_lists = fakeReload,
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
	mkdir(CARD, 0755);
	mkdir(CARD "/Collections", 0755);
	mkdir(CARD "/Roms", 0755);
	mkdir(CARD "/Roms/GB", 0755);
	mkdir(CARD "/Tools", 0755);
	mkdir(CARD "/Tools/test", 0755);
	mkdir(CARD "/Tools/test/Clock.pak", 0755);
	writeText(CARD "/Roms/GB/Tetris.gb", "");
	writeText(CARD "/Roms/GB/Kirby.gb", "");
}

///////////////////////////////////////
// the model

static int everythingExists(const char* path) {
	(void)path;
	return 1;
}

static int noTetris(const char* path) {
	return strstr(path, "Tetris") == NULL;
}

static void testCollectionName(void) {
	CHECK_STR(Focus_collectionName(NULL), "Favorites");
	CHECK_STR(Focus_collectionName(""), "Favorites");
	CHECK_STR(Focus_collectionName("a/b"), "Favorites");
	CHECK_STR(Focus_collectionName("Mine"), "Mine");

	char path[FOCUS_PATH_LEN];
	Focus_listPath("/mnt/SDCARD", "Mine", path, sizeof(path));
	CHECK_STR(path, "/mnt/SDCARD/Collections/Mine.txt");
	Focus_listPath("/mnt/SDCARD", "", path, sizeof(path));
	CHECK_STR(path, "/mnt/SDCARD/Collections/Favorites.txt");
}

static void testLoad(void) {
	static FocusList list;
	CHECK(Focus_load(&list, CARD "/Collections/Missing.txt") == 0);

	writeText(CARD "/Collections/Load.txt", "/Roms/GB/Tetris.gb\r\n\n/Roms/GB/Kirby.gb\n/Roms/GB/Tetris.gb\n/Tools/test/Clock.pak");
	CHECK(Focus_load(&list, CARD "/Collections/Load.txt") == 3);
	CHECK_STR(list.items[0], "/Roms/GB/Tetris.gb");
	CHECK_STR(list.items[1], "/Roms/GB/Kirby.gb");
	CHECK_STR(list.items[2], "/Tools/test/Clock.pak");

	// a new read replaces the old
	writeText(CARD "/Collections/Load.txt", "/Roms/GB/Kirby.gb\n");
	CHECK(Focus_load(&list, CARD "/Collections/Load.txt") == 1);
}

static void testRows(void) {
	static FocusList list;
	static FocusRow out[FOCUS_MAX + 1];

	list.count = 0;
	CHECK(Focus_rows(&list, "/sd", everythingExists, out) == 0);

	snprintf(list.items[0], FOCUS_PATH_LEN, "/Roms/GB/Tetris.gb");
	snprintf(list.items[1], FOCUS_PATH_LEN, "/Tools/test/Clock.pak");
	list.count = 2;
	CHECK(Focus_rows(&list, "/sd", everythingExists, out) == 3);
	CHECK_STR(out[0].path, "/sd/Roms/GB/Tetris.gb");
	CHECK(out[0].type == MOSAIC_ENTRY_ROM && out[0].name == NULL);
	CHECK_STR(out[1].path, "/sd/Tools/test/Clock.pak");
	CHECK(out[1].type == MOSAIC_ENTRY_PAK);
	CHECK_STR(out[2].path, "/sd/Roms");
	CHECK(out[2].type == MOSAIC_ENTRY_DIR);
	CHECK_STR(out[2].name, "All systems");

	// favorites off the card are skipped, and with none left it's stock's root
	CHECK(Focus_rows(&list, "/sd", noTetris, out) == 2);
	CHECK_STR(out[0].path, "/sd/Tools/test/Clock.pak");
	list.count = 1;
	CHECK(Focus_rows(&list, "/sd", noTetris, out) == 0);

	// a full list still has room for All systems
	for (int i = 0; i < FOCUS_MAX; i++) snprintf(list.items[i], FOCUS_PATH_LEN, "/Roms/GB/%i.gb", i);
	list.count = FOCUS_MAX;
	CHECK(Focus_rows(&list, "/sd", everythingExists, out) == FOCUS_MAX + 1);
	CHECK_STR(out[FOCUS_MAX].name, "All systems");
}

///////////////////////////////////////
// the plugin

static void testPlugin(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK_STR(p->id, "focus-mode");
	CHECK(p->root_replace != NULL);
	CHECK(p->root_with_entries == 0); // the decision: no other plugins' root entries
	CHECK(p->init(&fake) == 0);
	CHECK_STR(last_action, "focus-mode.toggle");
	CHECK_STR(last_label, "Focus");
	CHECK_STR(tile_action, "focus-mode.toggle");
	CHECK(tile_row == MOSAIC_QUICK_TOP);
	CHECK(action_available(NULL, NULL));
	CHECK(tile_is_on(NULL)); // on by default

	// no favorites yet: stock's root
	CHECK(p->root_replace() == NULL);
	writeText(CARD "/Collections/Favorites.txt", "/Roms/GB/Gone.gb\n");
	CHECK(p->root_replace() == NULL);

	writeText(CARD "/Collections/Favorites.txt", "/Roms/GB/Kirby.gb\n/Roms/GB/Gone.gb\n/Tools/test/Clock.pak\n");
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

	// favorites' own collection setting picks the file
	favorites_collection = "Mine";
	CHECK(p->root_replace() == NULL);
	writeText(CARD "/Collections/Mine.txt", "/Roms/GB/Tetris.gb\n");
	root = p->root_replace();
	CHECK(root && root->count == 2);
	if (root && root->count == 2) CHECK_STR(root->items[0]->path, CARD "/Roms/GB/Tetris.gb");
	freeList(root);

	// the tile turns it off, back to stock's root, and on again
	action_run(NULL, NULL);
	CHECK_STR(focus_on, "off");
	CHECK(!tile_is_on(NULL));
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Focus mode off");
	CHECK(p->root_replace() == NULL);
	action_run(NULL, NULL);
	CHECK_STR(focus_on, "on");
	CHECK(reloads == 2);
	CHECK_STR(toasted, "Focus mode on");
	root = p->root_replace();
	CHECK(root && root->count == 2);
	freeList(root);
}

int main(void) {
	makeCard();
	testCollectionName();
	testLoad();
	testRows();
	testPlugin();
	CHECK_DONE();
}
