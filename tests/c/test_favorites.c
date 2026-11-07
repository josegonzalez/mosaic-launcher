// The favorites plugin against a fake host, without the launcher.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/favorites/favorites.c"

struct NUIEntry {
	char path[512];
	char name[128];
	int type;
};
struct NUIList {
	NUIEntry* items[64];
	int count;
};

static NUIEntry* selected = NULL;
static char last_action[64];
static int bound_screen = -1, bound_button = -1;
static int (*action_available)(void*) = NULL;
static void (*action_run)(void*) = NULL;
static int redraws = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static NUIEntry* fakeEntryNew(const char* path, int type, const char* name) {
	NUIEntry* e = calloc(1, sizeof(NUIEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
	e->type = type;
	return e;
}
static const char* fakeEntryPath(const NUIEntry* e) { return e->path; }
static const char* fakeEntryName(const NUIEntry* e) { return e->name; }
static int fakeEntryType(const NUIEntry* e) { return e->type; }
static NUIList* fakeListNew(void) { return calloc(1, sizeof(NUIList)); }
static int fakeListCount(const NUIList* l) { return l->count; }
static NUIEntry* fakeListGet(const NUIList* l, int i) { return l->items[i]; }
static void fakeListPush(NUIList* l, NUIEntry* e) { l->items[l->count++] = e; }
static const NUIEntry* fakeSelected(void) { return selected; }
static void fakeRedraw(void) { redraws++; }
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)label; (void)data;
	snprintf(last_action, sizeof(last_action), "%s", id);
	action_available = available;
	action_run = run;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)trigger; (void)id;
	bound_screen = screen;
	bound_button = button;
	return 0;
}

#define CARD TEST_SDCARD_PATH
#define USERDATA CARD "/.userdata/test"

static const NUIHost fake = {
	.size = sizeof(NUIHost),
	.api_version = NUI_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = USERDATA,
	.log = fakeLog,
	.entry_new = fakeEntryNew,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.entry_type = fakeEntryType,
	.list_new = fakeListNew,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.list_push = fakeListPush,
	.selected_entry = fakeSelected,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
};

static void touch(const char* path) {
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
}

int main(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" CARD "/Roms/GB' '" USERDATA "/launcher'") == 0);
	touch(CARD "/Roms/GB/Tetris.gb");
	touch(CARD "/Roms/GB/Zelda.gb");

	const NUIPlugin* p = nextui_plugin_get(&fake);
	CHECK(p->size == sizeof(NUIPlugin));
	CHECK_STR(p->id, "favorites");
	CHECK(p->init(&fake) == 0);
	CHECK_STR(last_action, "favorites.toggle");
	CHECK(bound_screen == NUI_SCREEN_GAMELIST && bound_button == NUI_BUTTON_Y);

	// a Favorites folder on the root
	NUIList* root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 1);
	CHECK_STR(root->items[0]->path, CARD "/Favorites");
	CHECK_STR(root->items[0]->name, "Favorites");
	CHECK(root->items[0]->type == NUI_ENTRY_DIR);

	// other folders are the launcher's to list
	CHECK(p->virtual_dir(CARD "/Roms") == NULL);
	CHECK(p->virtual_dir(CARD "/Favorites")->count == 0);

	// only games and paks on the card can be favorites
	selected = NULL;
	CHECK(!action_available(NULL));
	selected = fakeEntryNew(CARD "/Roms/GB", NUI_ENTRY_DIR, NULL);
	CHECK(!action_available(NULL));
	selected = fakeEntryNew("/elsewhere/Tetris.gb", NUI_ENTRY_ROM, NULL);
	CHECK(!action_available(NULL));

	// add two, newest first, and they persist
	selected = fakeEntryNew(CARD "/Roms/GB/Tetris.gb", NUI_ENTRY_ROM, NULL);
	CHECK(action_available(NULL));
	action_run(NULL);
	selected = fakeEntryNew(CARD "/Roms/GB/Zelda.gb", NUI_ENTRY_ROM, NULL);
	action_run(NULL);
	CHECK(redraws == 2);
	NUIList* favs = p->virtual_dir(CARD "/Favorites");
	CHECK(favs->count == 2);
	if (favs->count == 2) {
		CHECK_STR(favs->items[0]->path, CARD "/Roms/GB/Zelda.gb");
		CHECK_STR(favs->items[1]->path, CARD "/Roms/GB/Tetris.gb");
		CHECK(favs->items[0]->type == NUI_ENTRY_ROM);
	}
	char saved[256] = "";
	FILE* f = fopen(USERDATA "/launcher/favorites.txt", "r");
	CHECK(f != NULL);
	if (f) {
		size_t got = fread(saved, 1, sizeof(saved) - 1, f);
		saved[got] = '\0';
		fclose(f);
	}
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n");

	// toggling again removes it
	action_run(NULL);
	CHECK(p->virtual_dir(CARD "/Favorites")->count == 1);

	// a favorite whose file is gone isn't listed, but stays saved
	CHECK(remove(CARD "/Roms/GB/Tetris.gb") == 0);
	CHECK(p->virtual_dir(CARD "/Favorites")->count == 0);
	load();
	CHECK(count == 1);

	CHECK_DONE();
}
