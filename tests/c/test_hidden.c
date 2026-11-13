// The hidden games plugin: its hidden set on its own, then the plugin
// against a fake host, without the launcher.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "fakesets.h"
#include "../../plugins/hidden/hidden.c"

struct MosaicEntry {
	char path[512];
	char name[128];
	int type;
};
struct MosaicList {
	MosaicEntry* items[64];
	int count;
};

static MosaicEntry* selected = NULL;
static char current[512];
static const char* setting = NULL; // the button setting, or NULL for the default
static int registered = 0;
static int bound_screen = -1, bound_count = 0;
static int bound_button[4];
static char bound_action[4][64];
static int (*available_for[2])(const char*, void*);
static void (*run_for[2])(const char*, void*);
static int reloads = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static char toasted[160];
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (setting && !strcmp(key, "button")) return setting;
	return fallback;
}
static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = calloc(1, sizeof(MosaicEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
	e->type = type;
	return e;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static MosaicList* fakeListNew(void) { return calloc(1, sizeof(MosaicList)); }
static int fakeListCount(const MosaicList* l) { return l->count; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) { return l->items[i]; }
static void fakeListPush(MosaicList* l, MosaicEntry* e) { l->items[l->count++] = e; }
static const MosaicEntry* fakeSelected(void) { return selected; }
static const char* fakeCurrentPath(void) { return current; }
static void fakeRedraw(void) {}
static void fakeReload(void) { reloads++; }
static int fakeRegister(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)label; (void)data;
	int i = !strcmp(id, "hidden.unhide");
	available_for[i] = available;
	run_for[i] = run;
	registered++;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)trigger;
	bound_screen = screen;
	if (bound_count < 4) {
		bound_button[bound_count] = button;
		snprintf(bound_action[bound_count], sizeof(bound_action[0]), "%s", id);
		bound_count++;
	}
	return 0;
}

#define CARD TEST_SDCARD_PATH
#define USERDATA CARD "/.userdata/test"
#define LIST USERDATA "/launcher/hidden.txt"
#define GB CARD "/Roms/GB"

static const MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = USERDATA,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_new = fakeEntryNew,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.entry_type = fakeEntryType,
	.list_new = fakeListNew,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.list_push = fakeListPush,
	.selected_entry = fakeSelected,
	.current_path = fakeCurrentPath,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.reload_lists = fakeReload,
	.set_register = fakeSetRegister,
	.set_unregister = fakeSetUnregister,
	.set_changed = fakeSetChanged,
	.set_names = fakeSetNames,
	.is_filtered = fakeIsFiltered,
};

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static void readText(const char* path, char* out, size_t size) {
	out[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return;
	size_t got = fread(out, 1, size - 1, f);
	out[got] = '\0';
	fclose(f);
}

static void selectEntry(const char* path, int type) {
	selected = fakeEntryNew(path, type, NULL);
}

// Whether the plugin keeps an entry in dir.
static int kept(const MosaicPlugin* p, const char* path, const char* dir) {
	MosaicEntry* e = fakeEntryNew(path, MOSAIC_ENTRY_ROM, NULL);
	int keep = p->filter(e, dir);
	free(e);
	return keep;
}

static void testSet(void) {
	HiddenSet set;
	HiddenSet_init(&set);
	CHECK(!HiddenSet_contains(&set, "/a"));
	CHECK(HiddenSet_remove(&set, "/a") == 0);

	// kept sorted, without duplicates
	CHECK(HiddenSet_add(&set, "/c") == 1);
	CHECK(HiddenSet_add(&set, "/a") == 1);
	CHECK(HiddenSet_add(&set, "/b") == 1);
	CHECK(HiddenSet_add(&set, "/a") == 0);
	CHECK(set.count == 3);
	CHECK_STR(set.paths[0], "/a");
	CHECK_STR(set.paths[1], "/b");
	CHECK_STR(set.paths[2], "/c");
	CHECK(HiddenSet_contains(&set, "/b"));
	CHECK(!HiddenSet_contains(&set, "/d"));

	// a folder's trailing slash doesn't count, and empty paths aren't added
	CHECK(HiddenSet_add(&set, "/dir/") == 1);
	CHECK(HiddenSet_contains(&set, "/dir"));
	CHECK(HiddenSet_add(&set, "/dir") == 0);
	CHECK(HiddenSet_add(&set, "") == -1);
	CHECK(HiddenSet_add(&set, NULL) == -1);

	CHECK(HiddenSet_remove(&set, "/b") == 1);
	CHECK(HiddenSet_remove(&set, "/b") == 0);
	CHECK(HiddenSet_remove(&set, "/dir/") == 1);
	CHECK(set.count == 2);
	CHECK_STR(set.paths[1], "/c");

	// many paths grow the set
	char path[32];
	for (int i = 0; i < 200; i++) {
		snprintf(path, sizeof(path), "/many/%03d", 199 - i);
		CHECK(HiddenSet_add(&set, path) == 1);
	}
	CHECK(set.count == 202);
	CHECK(HiddenSet_contains(&set, "/many/000") && HiddenSet_contains(&set, "/many/199"));
	CHECK_STR(set.paths[2], "/many/000");

	// saved one per line, in order, and read back the same
	char file[] = CARD "/set.txt";
	CHECK(HiddenSet_remove(&set, "/c") == 1);
	for (int i = 0; i < 200; i++) {
		snprintf(path, sizeof(path), "/many/%03d", i);
		HiddenSet_remove(&set, path);
	}
	HiddenSet_add(&set, "/Roms/GB/Tetris (World).gb");
	CHECK(HiddenSet_save(&set, file) == 0);
	char saved[256];
	readText(file, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Tetris (World).gb\n/a\n");
	CHECK(!exists(CARD "/set.txt.tmp"));

	// a hand-edited file: comments, blank lines, CRLF, slashes, repeats
	writeText(file, "# hidden\n\n/z\r\n/y/\n/z\n   \n");
	CHECK(HiddenSet_load(&set, file) == 0);
	CHECK(set.count == 2);
	CHECK_STR(set.paths[0], "/y");
	CHECK_STR(set.paths[1], "/z");

	// a missing file is an empty set
	CHECK(HiddenSet_load(&set, CARD "/missing.txt") == 0);
	CHECK(set.count == 0);
	// a file that can't be written fails
	CHECK(HiddenSet_save(&set, CARD "/no/such/dir/set.txt") == -1);

	HiddenSet_clear(&set);
	CHECK(set.count == 0 && set.paths == NULL);
}

int main(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" GB "' '" CARD "/Roms/PS/Game' '" CARD "/Roms/PS/Single' '" CARD "/Tools/test/Clock.pak' '" USERDATA "/launcher'") == 0);
	testSet();

	writeText(GB "/Tetris.gb", "");
	writeText(GB "/Zelda.gb", "");
	writeText(CARD "/Roms/PS/Game/Game (Disc 1).cue", "");
	writeText(CARD "/Roms/PS/Game/Game.m3u", "Game (Disc 1).cue\n");
	writeText(CARD "/Roms/PS/Single/Single.cue", "");

	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "hidden");
	CHECK(p->init(&fake) == 0);
	CHECK(registered == 2);
	CHECK(bound_screen == MOSAIC_SCREEN_GAMELIST);
	CHECK(bound_count == 2 && bound_button[0] == MOSAIC_BUTTON_Y && bound_button[1] == MOSAIC_BUTTON_Y);
	CHECK_STR(bound_action[0], "hidden.hide");
	CHECK_STR(bound_action[1], "hidden.unhide");
	int (*canHide)(const char*, void*) = available_for[0];
	void (*hideIt)(const char*, void*) = run_for[0];
	int (*canUnhide)(const char*, void*) = available_for[1];
	void (*unhideIt)(const char*, void*) = run_for[1];

	// nothing hidden: no folder on the root, and every entry kept
	MosaicList* root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 0);
	CHECK(kept(p, GB "/Tetris.gb", GB));
	CHECK(p->virtual_dir(CARD "/Roms") == NULL);
	CHECK(fake_set_count == 1);
	CHECK(fakeSetHas("hidden", GB "/Tetris.gb") == 0);
	CHECK_STR(fakeSetList("hidden"), "");

	// plain folders can't be hidden; games, paks and game folders can
	snprintf(current, sizeof(current), "%s", GB);
	selected = NULL;
	CHECK(!canHide(NULL, NULL));
	selectEntry(GB, MOSAIC_ENTRY_DIR);
	CHECK(!canHide(NULL, NULL));
	// nor can another plugin's virtual folders, which leaves their buttons free
	selectEntry(CARD "/Smart Collections", MOSAIC_ENTRY_DIR);
	CHECK(!canHide(NULL, NULL));
	selectEntry(CARD "/Smart Collections/Mario", MOSAIC_ENTRY_DIR);
	CHECK(!canHide(NULL, NULL));
	selectEntry(CARD "/Tools/test/Clock.pak", MOSAIC_ENTRY_PAK);
	CHECK(canHide(NULL, NULL));
	selectEntry(CARD "/Roms/PS/Single", MOSAIC_ENTRY_DIR);
	CHECK(canHide(NULL, NULL));
	selectEntry(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	CHECK(canHide(NULL, NULL));
	CHECK(!canUnhide(NULL, NULL)); // only in the Hidden folder

	// hiding saves the full path and lists the folders again
	hideIt(NULL, NULL);
	char saved[1024];
	readText(LIST, saved, sizeof(saved));
	CHECK_STR(saved, GB "/Tetris.gb\n");
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Hidden");
	CHECK(!kept(p, GB "/Tetris.gb", GB));
	CHECK(!kept(p, GB "/Tetris.gb", CARD "/Recently Played")); // every listing
	CHECK(kept(p, GB "/Zelda.gb", GB));

	// the folder shows on the root now
	root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 1);
	if (root->count == 1) {
		CHECK_STR(root->items[0]->path, CARD "/Hidden");
		CHECK_STR(root->items[0]->name, "Hidden");
		CHECK(root->items[0]->type == MOSAIC_ENTRY_DIR);
	}

	// a disc hides its game folder, which hides the .m3u wherever it's listed
	selectEntry(CARD "/Roms/PS/Game/Game (Disc 1).cue", MOSAIC_ENTRY_ROM);
	hideIt(NULL, NULL);
	selectEntry(CARD "/Tools/test/Clock.pak", MOSAIC_ENTRY_PAK);
	hideIt(NULL, NULL);
	readText(LIST, saved, sizeof(saved));
	CHECK_STR(saved, GB "/Tetris.gb\n" CARD "/Roms/PS/Game\n" CARD "/Tools/test/Clock.pak\n");
	CHECK(!kept(p, CARD "/Roms/PS/Game", CARD "/Roms/PS"));
	CHECK(!kept(p, CARD "/Roms/PS/Game/Game.m3u", CARD "/Collections/Mine.txt"));

	// each hide announced the set
	CHECK(strstr(fake_set_changed, "hidden\n") == fake_set_changed);

	// the hidden set lists the paths hidden, and has what the filter hides
	CHECK_STR(fakeSetList("hidden"), GB "/Tetris.gb\n" CARD "/Roms/PS/Game\n" CARD "/Tools/test/Clock.pak\n");
	CHECK(fakeSetHas("hidden", GB "/Tetris.gb") == 1);
	CHECK(fakeSetHas("hidden", CARD "/Roms/PS/Game/Game.m3u") == 1);
	CHECK(fakeSetHas("hidden", GB "/Zelda.gb") == 0);
	// a hand edit is seen
	writeText(LIST, GB "/Zelda.gb\n");
	CHECK(fakeSetHas("hidden", GB "/Zelda.gb") == 1);
	CHECK(fakeSetHas("hidden", GB "/Tetris.gb") == 0);
	writeText(LIST, GB "/Tetris.gb\n" CARD "/Roms/PS/Game\n" CARD "/Tools/test/Clock.pak\n");
	CHECK_STR(fakeSetList("hidden"), GB "/Tetris.gb\n" CARD "/Roms/PS/Game\n" CARD "/Tools/test/Clock.pak\n");

	// the Hidden folder lists them, with their types, and keeps them
	MosaicList* list = p->virtual_dir(CARD "/Hidden");
	CHECK(list->count == 3);
	if (list->count == 3) {
		CHECK_STR(list->items[0]->path, GB "/Tetris.gb");
		CHECK(list->items[0]->type == MOSAIC_ENTRY_ROM);
		CHECK_STR(list->items[1]->path, CARD "/Roms/PS/Game");
		CHECK(list->items[1]->type == MOSAIC_ENTRY_DIR);
		CHECK_STR(list->items[2]->path, CARD "/Tools/test/Clock.pak");
		CHECK(list->items[2]->type == MOSAIC_ENTRY_PAK);
		CHECK(p->filter(list->items[0], CARD "/Hidden") == 1);
	}

	// in the Hidden folder the button unhides instead
	snprintf(current, sizeof(current), "%s", CARD "/Hidden");
	selectEntry(GB "/Tetris.gb", MOSAIC_ENTRY_ROM);
	CHECK(!canHide(NULL, NULL));
	CHECK(canUnhide(NULL, NULL));
	reloads = 0;
	unhideIt(NULL, NULL);
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Unhidden");
	readText(LIST, saved, sizeof(saved));
	CHECK_STR(saved, CARD "/Roms/PS/Game\n" CARD "/Tools/test/Clock.pak\n");
	CHECK(kept(p, GB "/Tetris.gb", GB));
	CHECK(!canUnhide(NULL, NULL)); // not hidden any more

	// edits to the file are picked up
	writeText(LIST, "# by hand\n" GB "/Zelda.gb\n" GB "/Gone.gb\n");
	CHECK(p->virtual_dir(CARD "/Hidden")->count == 1); // a missing game isn't listed
	CHECK(!kept(p, GB "/Zelda.gb", GB));
	CHECK(kept(p, CARD "/Roms/PS/Game", CARD "/Roms/PS"));

	// unhiding the last one on the card drops the folder from the root,
	// but a hidden game whose file is gone stays saved
	selectEntry(GB "/Zelda.gb", MOSAIC_ENTRY_ROM);
	unhideIt(NULL, NULL);
	readText(LIST, saved, sizeof(saved));
	CHECK_STR(saved, GB "/Gone.gb\n");
	root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 0);

	p->quit();

	// the button setting picks another button
	setting = "L2";
	bound_count = 0;
	CHECK(p->init(&fake) == 0);
	CHECK(bound_count == 2 && bound_button[0] == MOSAIC_BUTTON_L2 && bound_button[1] == MOSAIC_BUTTON_L2);
	CHECK(HiddenSet_contains(&hidden, GB "/Gone.gb")); // read at start
	p->quit();
	setting = "nonsense";
	bound_count = 0;
	p->init(&fake);
	CHECK(bound_count == 2 && bound_button[0] == MOSAIC_BUTTON_Y);
	p->quit();

	CHECK_DONE();
}
