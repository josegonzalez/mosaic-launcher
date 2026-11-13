// The favorites plugin against a fake host, without the launcher.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "fakesets.h"
#include "../../plugins/favorites/favorites.c"

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
static char last_action[64];
static int bound_screen = -1, bound_button = -1;
static int (*action_available)(void*) = NULL;
static void (*action_run)(void*) = NULL;
static int redraws = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static char toasted[160];
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }

// The dialog: remembers what was asked, and answers when the test says so.
static char asked[256];
static void (*answer_yes)(void*) = NULL;
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)yes; (void)data;
	snprintf(asked, sizeof(asked), "%s", message);
	answer_yes = on_yes;
}
static void sayYes(void) {
	if (answer_yes) answer_yes(NULL);
	answer_yes = NULL;
}
static const char* pin_setting = NULL; // the "pin" setting, or NULL for its default
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "pin") && pin_setting) return pin_setting;
	return fallback;
}
static int reloads = 0;
static void fakeReload(void) { reloads++; }
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
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.confirm = fakeConfirm,
	.reload_lists = fakeReload,
	.set_register = fakeSetRegister,
	.set_unregister = fakeSetUnregister,
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

// Sorts entries in place by the plugin's sort_compare, keeping the order of
// pairs it has no opinion on, as the launcher does.
static void sortBy(const MosaicPlugin* p, MosaicEntry** items, int n, const char* dir) {
	for (int i = 1; i < n; i++) {
		MosaicEntry* e = items[i];
		int j = i;
		while (j > 0 && p->sort_compare(items[j - 1], e, dir) > 0) {
			items[j] = items[j - 1];
			j--;
		}
		items[j] = e;
	}
}

static void testPinModel(void) {
	const char* paths[] = { "/Roms/GB/Zelda.gb", "/Roms/PS/Game/Game.m3u", "/Tools/desktop/Clock.pak",
		"/Roms/PS/Single/Single.cue", "", NULL };
	PinSet set;
	Pin_build(&set, paths, 6);
	CHECK(set.count == 4); // empty and missing paths are skipped
	CHECK(Pin_has(&set, "/Roms/GB/Zelda.gb"));
	CHECK(Pin_has(&set, "/Tools/desktop/Clock.pak"));
	CHECK(!Pin_has(&set, "/Roms/GB/Tetris.gb"));
	CHECK(!Pin_has(&set, NULL));

	// games and paks by their own path
	CHECK(Pin_isPinned(&set, "/Roms/GB/Zelda.gb", MOSAIC_ENTRY_ROM));
	CHECK(!Pin_isPinned(&set, "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM));
	CHECK(Pin_isPinned(&set, "/Tools/desktop/Clock.pak", MOSAIC_ENTRY_PAK));
	CHECK(!Pin_isPinned(&set, "/Roms/GB/Zelda.gb", MOSAIC_ENTRY_TOGGLE));
	// a folder that launches a game, by its .m3u or .cue
	CHECK(Pin_isPinned(&set, "/Roms/PS/Game", MOSAIC_ENTRY_DIR));
	CHECK(Pin_isPinned(&set, "/Roms/PS/Single", MOSAIC_ENTRY_DIR));
	CHECK(!Pin_isPinned(&set, "/Roms/PS", MOSAIC_ENTRY_DIR));
	CHECK(!Pin_isPinned(&set, "/Roms/PS/Game/", MOSAIC_ENTRY_DIR));
	// a disc inside a multi-disc game, by the game's .m3u
	CHECK(Pin_isPinned(&set, "/Roms/PS/Game/Game (Disc 2).cue", MOSAIC_ENTRY_ROM));
	CHECK(Pin_isPinned(&set, "/Roms/PS/Game/Game.m3u", MOSAIC_ENTRY_ROM));
	CHECK(!Pin_isPinned(&set, "/Roms/PS/Other/Other (Disc 1).cue", MOSAIC_ENTRY_ROM));
	CHECK(!Pin_isPinned(&set, NULL, MOSAIC_ENTRY_ROM));
	PinSet none;
	Pin_build(&none, paths, 0);
	CHECK(!Pin_isPinned(&none, "/Roms/GB/Zelda.gb", MOSAIC_ENTRY_ROM));

	// which folders put favorites first
	CHECK(Pin_applies("/Roms/GB", "/Favorites"));
	CHECK(Pin_applies("/Tools/desktop", "/Favorites"));
	CHECK(Pin_applies("/Recently Played Games", "/Favorites"));
	CHECK(Pin_applies("/Favorites Old", "/Favorites"));
	CHECK(!Pin_applies("/Recently Played", "/Favorites"));
	CHECK(!Pin_applies("/Collections", "/Favorites"));
	CHECK(!Pin_applies("/Collections/Favorites.txt", "/Favorites"));
	CHECK(Pin_applies("/CollectionsX", "/Favorites"));
	CHECK(!Pin_applies("/Favorites", "/Favorites"));
	CHECK(!Pin_applies(NULL, "/Favorites"));

	// pinned first, no opinion otherwise
	CHECK(Pin_compare(1, 0) < 0);
	CHECK(Pin_compare(0, 1) > 0);
	CHECK(Pin_compare(1, 1) == 0);
	CHECK(Pin_compare(0, 0) == 0);
	CHECK(Pin_compare(5, 0) < 0);
}

int main(void) {
	testPinModel();

	CHECK(system("rm -rf '" CARD "' && mkdir -p '" CARD "/Roms/GB' '" CARD "/Roms/PS/Game' '" CARD "/Roms/PS/Single' '" USERDATA "/launcher'") == 0);
	writeText(CARD "/Roms/GB/Tetris.gb", "");
	writeText(CARD "/Roms/GB/Zelda.gb", "");
	writeText(CARD "/Roms/PS/Game/Game (Disc 1).cue", "");
	writeText(CARD "/Roms/PS/Game/Game.m3u", "Game (Disc 1).cue\n");
	writeText(CARD "/Roms/PS/Single/Single.cue", "");
	// a list kept by an earlier build of the plugin
	writeText(USERDATA "/launcher/favorites.txt", "/Roms/GB/Zelda.gb\n");

	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "favorites");
	CHECK(p->init(&fake) == 0);
	CHECK_STR(last_action, "favorites.toggle");
	CHECK(bound_screen == MOSAIC_SCREEN_GAMELIST && bound_button == MOSAIC_BUTTON_Y);

	// the old list moved into the collection
	char saved[1024];
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n");
	CHECK(!exists(USERDATA "/launcher/favorites.txt"));
	CHECK(exists(USERDATA "/launcher/favorites.txt.migrated"));

	// the favorites set, by full path
	CHECK(fake_set_count == 1);
	CHECK(fakeSetHas("favorites", CARD "/Roms/GB/Zelda.gb") == 1);
	CHECK(fakeSetHas("favorites", CARD "/Roms/GB/Tetris.gb") == 0);
	CHECK(fakeSetHas("favorites", "/Roms/GB/Zelda.gb") == 0);
	CHECK_STR(fakeSetList("favorites"), CARD "/Roms/GB/Zelda.gb\n");

	// a Favorites folder on the root
	MosaicList* root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 1);
	CHECK_STR(root->items[0]->path, CARD "/Favorites");
	CHECK_STR(root->items[0]->name, "Favorites");
	CHECK(root->items[0]->type == MOSAIC_ENTRY_DIR);

	// other folders are the launcher's to list
	CHECK(p->virtual_dir(CARD "/Roms") == NULL);

	// plain folders and things off the card can't be favorites
	selected = NULL;
	CHECK(!action_available(NULL));
	selectEntry(CARD "/Roms/GB", MOSAIC_ENTRY_DIR);
	CHECK(!action_available(NULL));
	selectEntry("/elsewhere/Tetris.gb", MOSAIC_ENTRY_ROM);
	CHECK(!action_available(NULL));

	// adding goes at the end of the collection
	selectEntry(CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM);
	CHECK(action_available(NULL));
	action_run(NULL);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n");
	CHECK(redraws == 1);
	CHECK_STR(toasted, "Added to Favorites");
	CHECK(fakeSetHas("favorites", CARD "/Roms/GB/Tetris.gb") == 1);
	CHECK_STR(fakeSetList("favorites"), CARD "/Roms/GB/Zelda.gb\n" CARD "/Roms/GB/Tetris.gb\n");

	// a multi-disc game's folder, and a disc inside it, both mean its .m3u
	selectEntry(CARD "/Roms/PS/Game", MOSAIC_ENTRY_DIR);
	CHECK(action_available(NULL));
	action_run(NULL);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n/Roms/PS/Game/Game.m3u\n");
	selectEntry(CARD "/Roms/PS/Game/Game (Disc 1).cue", MOSAIC_ENTRY_ROM);
	action_run(NULL); // same game, so this asks to remove it
	CHECK_STR(asked, "Remove Game from Favorites?"); // the game, not "Disc 1"
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n/Roms/PS/Game/Game.m3u\n"); // not until answered
	sayYes();
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n");
	CHECK_STR(toasted, "Removed from Favorites");

	// cancelling the dialog keeps the favorite
	selectEntry(CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	answer_yes = NULL; // the user chose B
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n");

	// a single-disc folder means its .cue
	selectEntry(CARD "/Roms/PS/Single", MOSAIC_ENTRY_DIR);
	action_run(NULL);
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Zelda.gb\n/Roms/GB/Tetris.gb\n/Roms/PS/Single/Single.cue\n");

	// the folder lists the collection, including edits made elsewhere
	writeText(COLLECTION, "/Roms/PS/Game/Game.m3u\n/Roms/GB/Tetris.gb\n");
	MosaicList* favs = p->virtual_dir(CARD "/Favorites");
	CHECK(favs->count == 2);
	if (favs->count == 2) {
		CHECK_STR(favs->items[0]->path, CARD "/Roms/PS/Game/Game.m3u");
		CHECK(favs->items[0]->type == MOSAIC_ENTRY_ROM);
		CHECK_STR(favs->items[1]->path, CARD "/Roms/GB/Tetris.gb");
	}

	// toggling an existing favorite removes it, keeping the rest in order
	selectEntry(CARD "/Roms/PS/Game", MOSAIC_ENTRY_DIR);
	action_run(NULL);
	sayYes();
	readText(COLLECTION, saved, sizeof(saved));
	CHECK_STR(saved, "/Roms/GB/Tetris.gb\n");

	// an art-only Collections/Favorites/ folder: hidden while the collection
	// exists, and its background used for Favorites
	CHECK(system("mkdir -p '" CARD "/Collections/Favorites/.media'") == 0);
	writeText(CARD "/Collections/Favorites/.media/bg.png", "");
	MosaicEntry* art_folder = fakeEntryNew(CARD "/Collections/Favorites", MOSAIC_ENTRY_DIR, NULL);
	MosaicEntry* collection = fakeEntryNew(COLLECTION, MOSAIC_ENTRY_DIR, NULL);
	CHECK(p->filter(art_folder, CARD "/Collections") == 0);
	CHECK(p->filter(collection, CARD "/Collections") == 1);
	CHECK(p->filter(art_folder, CARD "/Roms") == 1); // only in Collections
	char art[1024];
	CHECK(p->art_path(root->items[0], MOSAIC_ART_FOLDER_BG, art, sizeof(art)) == 1);
	CHECK_STR(art, CARD "/Collections/Favorites/.media/bg.png");
	CHECK(p->art_path(collection, MOSAIC_ART_FOLDER_BG, art, sizeof(art)) == 1);
	CHECK(p->art_path(collection, MOSAIC_ART_THUMBNAIL, art, sizeof(art)) == 0);
	CHECK(p->art_path(art_folder, MOSAIC_ART_FOLDER_BG, art, sizeof(art)) == 0);
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

	// pin to top is off by default: no opinion on any order
	writeText(CARD "/Roms/GB/Tetris.gb", "");
	writeText(CARD "/Roms/GB/Alpha.gb", "");
	writeText(COLLECTION, "/Roms/GB/Zelda.gb\n/Roms/PS/Game/Game.m3u\n");
	MosaicEntry* alpha = fakeEntryNew(CARD "/Roms/GB/Alpha.gb", MOSAIC_ENTRY_ROM, NULL);
	MosaicEntry* tetris = fakeEntryNew(CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM, NULL);
	MosaicEntry* zelda = fakeEntryNew(CARD "/Roms/GB/Zelda.gb", MOSAIC_ENTRY_ROM, NULL);
	CHECK(p->sort_compare(tetris, zelda, CARD "/Roms/GB") == 0);
	CHECK(p->sort_compare(zelda, tetris, CARD "/Roms/GB") == 0);
	int reloads_before = reloads;
	selectEntry(CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	CHECK(reloads == reloads_before); // nothing to re-sort
	writeText(COLLECTION, "/Roms/GB/Zelda.gb\n/Roms/PS/Game/Game.m3u\n");

	// with it on, favorites go first, each group in the launcher's order
	pin_setting = "on";
	CHECK(p->init(&fake) == 0);
	CHECK(p->sort_compare(tetris, zelda, CARD "/Roms/GB") > 0);
	CHECK(p->sort_compare(zelda, tetris, CARD "/Roms/GB") < 0);
	CHECK(p->sort_compare(alpha, tetris, CARD "/Roms/GB") == 0);
	MosaicEntry* gb[] = { alpha, tetris, zelda };
	sortBy(p, gb, 3, CARD "/Roms/GB");
	CHECK(gb[0] == zelda && gb[1] == alpha && gb[2] == tetris);

	// a multi-disc game's folder, by its .m3u
	MosaicEntry* game = fakeEntryNew(CARD "/Roms/PS/Game", MOSAIC_ENTRY_DIR, NULL);
	MosaicEntry* single = fakeEntryNew(CARD "/Roms/PS/Single", MOSAIC_ENTRY_DIR, NULL);
	CHECK(p->sort_compare(single, game, CARD "/Roms/PS") > 0);

	// Recently Played, collections and Favorites keep their own order, and
	// so does anything off the card
	CHECK(p->sort_compare(tetris, zelda, CARD "/Recently Played") == 0);
	CHECK(p->sort_compare(tetris, zelda, COLLECTION) == 0);
	CHECK(p->sort_compare(tetris, zelda, CARD "/Favorites") == 0);
	CHECK(p->sort_compare(tetris, zelda, "/elsewhere") == 0);

	// a new favorite moves up once the folder is listed again
	reloads_before = reloads;
	selectEntry(CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM);
	action_run(NULL);
	CHECK(reloads == reloads_before + 1);
	CHECK(p->sort_compare(alpha, tetris, CARD "/Roms/GB") > 0);
	CHECK(p->sort_compare(zelda, tetris, CARD "/Roms/GB") == 0);
	// and back down once removed
	action_run(NULL);
	sayYes();
	CHECK(reloads == reloads_before + 2);
	CHECK(p->sort_compare(alpha, tetris, CARD "/Roms/GB") == 0);

	// an edit made elsewhere shows the next time a folder is sorted
	writeText(COLLECTION, "/Roms/GB/Alpha.gb\n");
	CHECK(p->sort_compare(tetris, zelda, CARD "/Roms/PS") == 0); // another folder re-reads, and Zelda is gone
	CHECK(p->sort_compare(alpha, zelda, CARD "/Roms/GB") < 0);

	CHECK_DONE();
}
