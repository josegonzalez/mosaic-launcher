// The smart collections plugin: its model on its own, then the plugin
// against a fake host and a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/smart-collections/smart_collections.c"

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
static int (*available[4])(void*);
static void (*run[4])(void*);
static int actions = 0;
static int bound_y = 0;
static int reloads = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static char toasted[160];
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }

static char asked[256];
static void (*answer_yes)(void*) = NULL;
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)yes; (void)data;
	snprintf(asked, sizeof(asked), "%s", message);
	answer_yes = on_yes;
}
static void sayYes(void) {
	void (*yes)(void*) = answer_yes;
	answer_yes = NULL;
	if (yes) yes(NULL);
}

// a tiny cfg, enough for a few keys
static char cfg_keys[8][32];
static char cfg_values[8][1024];
static int cfg_count = 0;
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	for (int i = 0; i < cfg_count; i++) {
		if (!strcmp(cfg_keys[i], key)) return cfg_values[i];
	}
	return fallback;
}
static void fakeCfgSet(const char* id, const char* key, const char* value) {
	(void)id;
	int i = 0;
	while (i < cfg_count && strcmp(cfg_keys[i], key)) i++;
	if (i == cfg_count) snprintf(cfg_keys[cfg_count++], 32, "%s", key);
	snprintf(cfg_values[i], 1024, "%s", value);
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
static void fakeListPush(NUIList* l, NUIEntry* e) { if (l->count < 64) l->items[l->count++] = e; }
static const NUIEntry* fakeSelected(void) { return selected; }
static void fakeRedraw(void) {}
static void fakeReload(void) { reloads++; }
static int fakeRegister(const char* id, const char* label, int (*avail)(void*), void (*go)(void*), void* data) {
	(void)id; (void)label; (void)data;
	available[actions] = avail;
	run[actions] = go;
	actions++;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)trigger; (void)id;
	if (screen == NUI_SCREEN_GAMELIST && button == NUI_BUTTON_Y) bound_y++;
	return 0;
}

// Y on the game list: the first available binding runs, as the host does.
static int pressY(void) {
	for (int i = 0; i < actions; i++) {
		if (available[i](NULL)) {
			run[i](NULL);
			return 1;
		}
	}
	return 0;
}

#define CARD TEST_SDCARD_PATH
#define FOLDER CARD "/Smart Collections"

static const NUIHost fake = {
	.size = sizeof(NUIHost),
	.api_version = NUI_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/test",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
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
};

static void touch(const char* path) {
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
}

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static const char* cleaned(const char* file_name) {
	static char out[256];
	SM_cleanName(file_name, out, sizeof(out));
	return out;
}

static const char* normalized(const char* in) {
	static char out[256];
	SM_normalize(in, out, sizeof(out));
	return out;
}

// True when the built-in catalog files the game under franchise.
static int inBuiltin(const char* franchise, const char* file_name) {
	static SMCatalog catalog;
	SM_catalogInit(&catalog);
	SM_catalogAddBuiltin(&catalog);
	int i = SM_find(&catalog, franchise);
	if (i < 0) return 0;
	char title[256];
	SM_normalize(cleaned(file_name), title, sizeof(title));
	return SM_inFranchise(&catalog.items[i], title);
}

static void testModel(void) {
	// names, as stock shows them
	CHECK_STR(cleaned("Super Mario Bros. 3 (USA) (Rev 1).nes"), "Super Mario Bros. 3");
	CHECK_STR(cleaned("Legend of Zelda, The - A Link to the Past (USA) [!].sfc"), "Legend of Zelda, The - A Link to the Past");
	CHECK_STR(cleaned("Celeste.p8.png"), "Celeste");
	CHECK_STR(cleaned("(Demo) Game.gb"), "(Demo) Game");
	CHECK_STR(cleaned("Final Fantasy VII (Disc 1)"), "Final Fantasy VII");

	CHECK_STR(normalized("Pokémon - Red Version"), "pokemon red version");
	CHECK_STR(normalized("  Pac-Man's  Revenge!! "), "pac man s revenge");

	// whole words, with region tags, numbering and punctuation
	CHECK(inBuiltin("Mario", "Super Mario Bros. 3 (USA).nes"));
	CHECK(inBuiltin("Mario", "Dr. Mario (World).gb"));
	CHECK(inBuiltin("Mario", "Mario's Picross (USA, Europe) (SGB Enhanced).gb"));
	CHECK(inBuiltin("Mario", "Super Mario 64 (USA).z64"));
	CHECK(inBuiltin("Mario", "001 - Mario Kart - Super Circuit (USA).gba"));
	CHECK(!inBuiltin("Mario", "Marionette Handler (Japan).gb"));
	CHECK(inBuiltin("Zelda", "Legend of Zelda, The - Link's Awakening DX (USA, Europe) (Rev 2).gbc"));
	CHECK(inBuiltin("Zelda", "Zelda II - The Adventure of Link (USA).nes"));
	CHECK(inBuiltin("Sonic", "Sonic The Hedgehog 2 (World).md"));
	CHECK(inBuiltin("Sonic", "Sonic2.gg")); // a number straight after
	CHECK(!inBuiltin("Sonic", "Supersonic Racers (Europe).md"));
	CHECK(inBuiltin("Pokemon", "Pokémon Pinball (Europe).gbc"));
	CHECK(inBuiltin("Pac-Man", "Ms. Pac-Man (USA).nes"));
	CHECK(inBuiltin("Pac-Man", "Pacman Collection.gba"));
	CHECK(inBuiltin("F-Zero", "F-Zero - Maximum Velocity (USA, Europe).gba"));
	CHECK(inBuiltin("Mega Man", "Mega Man X3 (USA).sfc"));
	CHECK(inBuiltin("Mega Man", "Rockman 2 - Dr. Wily no Nazo (Japan).nes"));
	CHECK(inBuiltin("Final Fantasy", "Final Fantasy VI Advance (USA).gba"));
	CHECK(!inBuiltin("Tetris", "Tetrisphere (USA).z64"));

	// user franchises: trimmed, no duplicates, no slashes
	SMCatalog catalog;
	SM_catalogInit(&catalog);
	SM_catalogAddBuiltin(&catalog);
	int builtins = catalog.count;
	CHECK(SM_catalogAddList(&catalog, " Kirby , Lufia,, Bad/Name , lufia, Ys ") == 2);
	CHECK(catalog.count == builtins + 2);
	CHECK(SM_find(&catalog, "lufia") >= 0);
	CHECK(SM_find(&catalog, "Ys") >= 0);
	CHECK(SM_find(&catalog, "Bad/Name") < 0);
	char title[256];
	SM_normalize("Lufia II - Rise of the Sinistrals", title, sizeof(title));
	CHECK(SM_inFranchise(&catalog.items[SM_find(&catalog, "Lufia")], title));

	// map.txt aliases
	const char* map = "sf2.zip\tStreet Fighter II\r\nmslug.zip\tMetal Slug\n";
	char alias[64];
	CHECK(SM_mapLookup(map, "sf2.zip", alias, sizeof(alias)));
	CHECK_STR(alias, "Street Fighter II");
	CHECK(SM_mapLookup(map, "mslug.zip", alias, sizeof(alias)));
	CHECK_STR(alias, "Metal Slug");
	CHECK(!SM_mapLookup(map, "sf2", alias, sizeof(alias)));

	// grouping: a game can be in two franchises, and others aren't kept
	SMIndex index;
	SM_indexInit(&index);
	CHECK(SM_indexAdd(&index, &catalog, "/r/b.gb", "Super Mario Land", NUI_ENTRY_ROM) == 1);
	CHECK(SM_indexAdd(&index, &catalog, "/r/a.wii", "Mario & Sonic at the Olympic Games", NUI_ENTRY_ROM) == 2);
	CHECK(SM_indexAdd(&index, &catalog, "/r/c.gb", "Tetris", NUI_ENTRY_ROM) == 1);
	CHECK(SM_indexAdd(&index, &catalog, "/r/d.gb", "Some Other Game", NUI_ENTRY_ROM) == 0);
	CHECK(SM_indexAdd(&index, &catalog, "/r/e.gb", "dr. mario", NUI_ENTRY_ROM) == 1);
	CHECK(index.game_count == 4);
	SM_indexSort(&index, &catalog);
	int mario = SM_find(&catalog, "Mario");
	CHECK(index.member_count[mario] == 3);
	CHECK_STR(index.games[index.members[mario][0]].name, "dr. mario"); // case doesn't matter
	CHECK_STR(index.games[index.members[mario][1]].name, "Mario & Sonic at the Olympic Games");
	CHECK_STR(index.games[index.members[mario][2]].name, "Super Mario Land");

	// which show: a minimum size, minus dismissed ones, by name
	int shown[SM_MAX_FRANCHISES];
	CHECK(SM_visible(&index, &catalog, 1, NULL, shown) == 3);
	CHECK_STR(catalog.items[shown[0]].name, "Mario");
	CHECK_STR(catalog.items[shown[1]].name, "Sonic");
	CHECK_STR(catalog.items[shown[2]].name, "Tetris");
	CHECK(SM_visible(&index, &catalog, 2, NULL, shown) == 1);
	CHECK(SM_visible(&index, &catalog, 0, NULL, shown) == 3); // at least 1

	SMNameSet set;
	SM_setParse(&set, "Sonic, tetris,,Sonic");
	CHECK(set.count == 2);
	CHECK(SM_setHas(&set, "TETRIS"));
	CHECK(SM_visible(&index, &catalog, 1, &set, shown) == 1);
	CHECK_STR(catalog.items[shown[0]].name, "Mario");
	CHECK(!SM_setAdd(&set, "sonic"));
	CHECK(!SM_setAdd(&set, "A,B"));
	CHECK(SM_setAdd(&set, "Zelda"));
	CHECK(SM_setRemove(&set, "tetris"));
	CHECK(!SM_setRemove(&set, "tetris"));
	char list[256];
	SM_setFormat(&set, list, sizeof(list));
	CHECK_STR(list, "Sonic,Zelda");
	SM_indexFree(&index);
	CHECK(index.game_count == 0);
}

static NUIEntry* named(NUIList* list, const char* name) {
	for (int i = 0; i < list->count; i++) {
		if (!strcmp(list->items[i]->name, name)) return list->items[i];
	}
	return NULL;
}

static void testPlugin(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" CARD "/Roms/Game Boy (GB)/Hacks' '" CARD "/Roms/Super Nintendo (SFC)'"
		" '" CARD "/Roms/PlayStation (PS)/Final Fantasy VII (USA)' '" CARD "/Roms/Arcade (FBN)'"
		" '" CARD "/Roms/Unplayable (NOPE)' '" CARD "/.system/test/paks/Emus/GB.pak'"
		" '" CARD "/.system/test/paks/Emus/SFC.pak' '" CARD "/Emus/test/PS.pak' '" CARD "/Emus/test/FBN.pak'") == 0);
	touch(CARD "/.system/test/paks/Emus/GB.pak/launch.sh");
	touch(CARD "/.system/test/paks/Emus/SFC.pak/launch.sh");
	touch(CARD "/Emus/test/PS.pak/launch.sh");
	touch(CARD "/Emus/test/FBN.pak/launch.sh");

	touch(CARD "/Roms/Game Boy (GB)/Super Mario Land (World).gb");
	touch(CARD "/Roms/Game Boy (GB)/Tetris (World) (Rev 1).gb");
	touch(CARD "/Roms/Game Boy (GB)/Legend of Zelda, The - Link's Awakening (USA).gb");
	touch(CARD "/Roms/Game Boy (GB)/.Hidden Mario.gb");
	touch(CARD "/Roms/Game Boy (GB)/Mario Golf.gb.disabled");
	touch(CARD "/Roms/Game Boy (GB)/Hacks/Super Mario Land DX.gb");
	touch(CARD "/Roms/Super Nintendo (SFC)/Super Mario World (USA).sfc");
	touch(CARD "/Roms/Super Nintendo (SFC)/Legend of Zelda, The - A Link to the Past (USA).sfc");
	touch(CARD "/Roms/Super Nintendo (SFC)/Final Fantasy III (USA).sfc");
	touch(CARD "/Roms/PlayStation (PS)/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u");
	touch(CARD "/Roms/PlayStation (PS)/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 1).bin");
	touch(CARD "/Roms/Arcade (FBN)/sf2.zip");
	touch(CARD "/Roms/Arcade (FBN)/sf2ce.zip");
	touch(CARD "/Roms/Arcade (FBN)/mario.zip");
	writeText(CARD "/Roms/Arcade (FBN)/map.txt", "sf2.zip\tStreet Fighter II\nsf2ce.zip\tStreet Fighter II' - Champion Edition\nmario.zip\t.Mario Bros.\n");
	touch(CARD "/Roms/Unplayable (NOPE)/Super Mario Bros.nes"); // no emulator

	const NUIPlugin* p = nextui_plugin_get(&fake);
	CHECK(p->size == sizeof(NUIPlugin));
	CHECK_STR(p->id, "smart-collections");
	CHECK(p->init(&fake) == 0);
	CHECK(actions == 2 && bound_y == 2);

	// one folder on the root
	NUIList* root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 1);
	CHECK_STR(root->items[0]->path, FOLDER);
	CHECK_STR(root->items[0]->name, "Smart Collections");
	CHECK(root->items[0]->type == NUI_ENTRY_DIR);
	CHECK(p->virtual_dir(CARD "/Roms") == NULL);

	// franchises with two games or more, by name
	NUIList* top = p->virtual_dir(FOLDER);
	CHECK(top->count == 4);
	if (top->count == 4) {
		CHECK_STR(top->items[0]->name, "Final Fantasy");
		CHECK_STR(top->items[1]->name, "Mario");
		CHECK_STR(top->items[2]->name, "Street Fighter");
		CHECK_STR(top->items[3]->name, "Zelda");
		CHECK_STR(top->items[1]->path, FOLDER "/Mario");
		CHECK(top->items[1]->type == NUI_ENTRY_DIR);
	}

	// across systems and subfolders, skipping hidden, disabled, mapped-away
	// games and systems without an emulator
	NUIList* mario = p->virtual_dir(FOLDER "/Mario");
	CHECK(mario->count == 3);
	if (mario->count == 3) {
		CHECK_STR(mario->items[0]->name, "Super Mario Land");
		CHECK_STR(mario->items[1]->name, "Super Mario Land DX");
		CHECK_STR(mario->items[1]->path, CARD "/Roms/Game Boy (GB)/Hacks/Super Mario Land DX.gb");
		CHECK_STR(mario->items[2]->name, "Super Mario World");
		CHECK(mario->items[2]->type == NUI_ENTRY_ROM);
	}

	// a multi-disc folder is one game, and map.txt names count
	NUIList* ff = p->virtual_dir(FOLDER "/Final Fantasy");
	CHECK(ff->count == 2);
	if (ff->count == 2) {
		CHECK_STR(ff->items[1]->name, "Final Fantasy VII");
		CHECK_STR(ff->items[1]->path, CARD "/Roms/PlayStation (PS)/Final Fantasy VII (USA)");
		CHECK(ff->items[1]->type == NUI_ENTRY_DIR);
	}
	NUIList* sf = p->virtual_dir(FOLDER "/Street Fighter");
	CHECK(sf->count == 2);
	if (sf->count == 2) CHECK_STR(sf->items[0]->name, "Street Fighter II");

	// an unknown collection is an empty folder, not the launcher's to list
	NUIList* none = p->virtual_dir(FOLDER "/Nothing");
	CHECK(none && none->count == 0);

	// Y does nothing on a game, or on the folder with nothing dismissed
	selected = mario->items[0];
	CHECK(!pressY());
	selected = root->items[0];
	CHECK(!pressY());

	// Y on a collection asks, then dismisses it and remembers that
	selected = top->items[1];
	CHECK(pressY());
	CHECK_STR(asked, "Dismiss Mario?");
	CHECK(p->virtual_dir(FOLDER)->count == 4); // not until answered
	sayYes();
	CHECK_STR(toasted, "Dismissed Mario");
	CHECK(reloads == 1);
	CHECK_STR(fakeCfgGet(ID, "dismissed", ""), "Mario");
	NUIList* after = p->virtual_dir(FOLDER);
	CHECK(after->count == 3);
	CHECK(named(after, "Mario") == NULL);

	// cancelling keeps it
	selected = top->items[3];
	CHECK(pressY());
	answer_yes = NULL;
	CHECK(p->virtual_dir(FOLDER)->count == 3);

	// still dismissed on the next start, and the folder stays on the root
	// even when every collection is dismissed
	fakeCfgSet(ID, "dismissed", "Mario,Zelda,Final Fantasy,Street Fighter");
	p->quit();
	actions = 0;
	CHECK(p->init(&fake) == 0);
	CHECK(p->virtual_dir(FOLDER)->count == 0);
	root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 1);

	// Y on the folder brings them all back
	selected = root->items[0];
	CHECK(pressY());
	CHECK_STR(asked, "Bring back 4 dismissed collections?");
	sayYes();
	CHECK_STR(fakeCfgGet(ID, "dismissed", "x"), "");
	CHECK(p->virtual_dir(FOLDER)->count == 4);
	CHECK(!pressY()); // nothing left to bring back

	// a game deleted since the scan isn't listed
	CHECK(remove(CARD "/Roms/Super Nintendo (SFC)/Super Mario World (USA).sfc") == 0);
	CHECK(p->virtual_dir(FOLDER "/Mario")->count == 2);

	// settings: a bigger minimum, extra franchises, no built-ins
	p->quit();
	actions = 0;
	fakeCfgSet(ID, "min", "3");
	CHECK(p->init(&fake) == 0);
	CHECK(p->virtual_dir(FOLDER)->count == 0);
	root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 0); // nothing big enough, so no folder

	p->quit();
	actions = 0;
	fakeCfgSet(ID, "min", "2");
	fakeCfgSet(ID, "builtin", "off");
	fakeCfgSet(ID, "extra", "Super, Legend");
	CHECK(p->init(&fake) == 0);
	NUIList* mine = p->virtual_dir(FOLDER);
	CHECK(mine->count == 2);
	if (mine->count == 2) {
		CHECK_STR(mine->items[0]->name, "Legend");
		CHECK_STR(mine->items[1]->name, "Super");
	}
	p->quit();
}

int main(void) {
	testModel();
	testPlugin();
	CHECK_DONE();
}
