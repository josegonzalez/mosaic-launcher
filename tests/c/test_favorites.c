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
static char toasted[160];
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id; (void)key;
	return fallback;
}
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
#define COLLECTION CARD "/Collections/Favorites.txt"

static const NUIHost fake = {
	.size = sizeof(NUIHost),
	.api_version = NUI_API_VERSION,
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
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
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

int main(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" CARD "/Roms/GB' '" CARD "/Roms/PS/Game' '" CARD "/Roms/PS/Single' '" USERDATA "/launcher'") == 0);
	writeText(CARD "/Roms/GB/Tetris.gb", "");
	writeText(CARD "/Roms/GB/Zelda.gb", "");
	writeText(CARD "/Roms/PS/Game/Game (Disc 1).cue", "");
	writeText(CARD "/Roms/PS/Game/Game.m3u", "Game (Disc 1).cue\n");
	writeText(CARD "/Roms/PS/Single/Single.cue", "");
	// a list kept by an earlier build of the plugin
	writeText(USERDATA "/launcher/favorites.txt", "/Roms/GB/Zelda.gb\n");

	const NUIPlugin* p = nextui_plugin_get(&fake);
	CHECK(p->size == sizeof(NUIPlugin));
	CHECK_STR(p->id, "favorites");
	CHECK(p->init(&fake) == 0);
	CHECK_STR(last_action, "favorites.toggle");
	CHECK(bound_screen == NUI_SCREEN_GAMELIST && bound_button == NUI_BUTTON_Y);

	// the old list moved into the collection
	char saved[1024];
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n");
	CHECK(!exists(USERDATA "/launcher/favorites.txt"));
	CHECK(exists(USERDATA "/launcher/favorites.txt.migrated"));

	// a Favorites folder on the root
	NUIList* root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 1);
	CHECK_STR(root->items[0]->path, CARD "/Favorites");
	CHECK_STR(root->items[0]->name, "Favorites");
	CHECK(root->items[0]->type == NUI_ENTRY_DIR);

	// other folders are the launcher's to list
	CHECK(p->virtual_dir(CARD "/Roms") == NULL);

	// plain folders and things off the card can't be favorites
	selected = NULL;
	CHECK(!action_available(NULL));
	selectEntry(CARD "/Roms/GB", NUI_ENTRY_DIR);
	CHECK(!action_available(NULL));
	selectEntry("/elsewhere/Tetris.gb", NUI_ENTRY_ROM);
	CHECK(!action_available(NULL));

	// adding goes at the end of the collection
	selectEntry(CARD "/Roms/GB/Tetris.gb", NUI_ENTRY_ROM);
	CHECK(action_available(NULL));
	action_run(NULL);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n");
	CHECK(redraws == 1);
	CHECK_STR(toasted, "Added to Favorites");

	// a multi-disc game's folder, and a disc inside it, both mean its .m3u
	selectEntry(CARD "/Roms/PS/Game", NUI_ENTRY_DIR);
	CHECK(action_available(NULL));
	action_run(NULL);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n/Roms/PS/Game/Game.m3u\n");
	selectEntry(CARD "/Roms/PS/Game/Game (Disc 1).cue", NUI_ENTRY_ROM);
	action_run(NULL); // same game, so this removes it
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n");
	CHECK_STR(toasted, "Removed from Favorites");

	// a single-disc folder means its .cue
	selectEntry(CARD "/Roms/PS/Single", NUI_ENTRY_DIR);
	action_run(NULL);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n/Roms/PS/Single/Single.cue\n");

	// the folder lists the collection, including edits made elsewhere
	writeText(COLLECTION, "/Roms/PS/Game/Game.m3u\n/Roms/GB/Tetris.gb\n");
	NUIList* favs = p->virtual_dir(CARD "/Favorites");
	CHECK(favs->count == 2);
	if (favs->count == 2) {
		CHECK_STR(favs->items[0]->path, CARD "/Roms/PS/Game/Game.m3u");
		CHECK(favs->items[0]->type == NUI_ENTRY_ROM);
		CHECK_STR(favs->items[1]->path, CARD "/Roms/GB/Tetris.gb");
	}

	// toggling an existing favorite removes it, keeping the rest in order
	selectEntry(CARD "/Roms/PS/Game", NUI_ENTRY_DIR);
	action_run(NULL);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Tetris.gb\n");

	// an art-only Collections/Favorites/ folder: hidden while the collection
	// exists, and its background used for Favorites
	CHECK(system("mkdir -p '" CARD "/Collections/Favorites/.media'") == 0);
	writeText(CARD "/Collections/Favorites/.media/bg.png", "");
	NUIEntry* art_folder = fakeEntryNew(CARD "/Collections/Favorites", NUI_ENTRY_DIR, NULL);
	NUIEntry* collection = fakeEntryNew(COLLECTION, NUI_ENTRY_DIR, NULL);
	CHECK(p->filter(art_folder, CARD "/Collections") == 0);
	CHECK(p->filter(collection, CARD "/Collections") == 1);
	CHECK(p->filter(art_folder, CARD "/Roms") == 1); // only in Collections
	char art[1024];
	CHECK(p->art_path(root->items[0], NUI_ART_FOLDER_BG, art, sizeof(art)) == 1);
	CHECK_STR(art, CARD "/Collections/Favorites/.media/bg.png");
	CHECK(p->art_path(collection, NUI_ART_FOLDER_BG, art, sizeof(art)) == 1);
	CHECK(p->art_path(collection, NUI_ART_THUMBNAIL, art, sizeof(art)) == 0);
	CHECK(p->art_path(art_folder, NUI_ART_FOLDER_BG, art, sizeof(art)) == 0);
	// a folder holding real content isn't hidden
	writeText(CARD "/Collections/Favorites/Something.txt", "");
	CHECK(p->filter(art_folder, CARD "/Collections") == 1);
	remove(CARD "/Collections/Favorites/Something.txt");
	// nor is it once the collection is gone
	char keep[1024];
	readText(COLLECTION, keep, sizeof(keep));
	remove(COLLECTION);
	CHECK(p->filter(art_folder, CARD "/Collections") == 1);
	writeText(COLLECTION, keep);

	// a favorite whose file is gone isn't listed, but stays saved
	CHECK(remove(CARD "/Roms/GB/Tetris.gb") == 0);
	CHECK(p->virtual_dir(CARD "/Favorites")->count == 0);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Tetris.gb\n");

	CHECK_DONE();
}
