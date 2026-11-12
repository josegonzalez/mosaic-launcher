// What the launcher lists for a given card. These pin stock nextui.c's
// behavior so the listing code can be restructured safely.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "launcher.h"
#include "stubs.h"

// Recreates the fixture card from scratch.
static void resetCard(void) {
	CHECK(system("rm -rf '" SDCARD_PATH "'") == 0);
	CHECK(mkdir(SDCARD_PATH, 0755) == 0);
}

// mkdir -p for the parent of path, then write contents to path.
static void writeFile(const char* path, const char* contents) {
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "mkdir -p \"$(dirname '%s')\"", path);
	CHECK(system(cmd) == 0);
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(contents, f);
	fclose(f);
}

static void addEmu(const char* tag) {
	char path[512];
	snprintf(path, sizeof(path), "%s/Emus/%s.pak/launch.sh", PAKS_PATH, tag);
	writeFile(path, "#!/bin/sh\n");
}

static Entry* at(Directory* dir, int i) {
	return dir->entries->items[i];
}

static void test_console_folder_sorted_and_hidden_files_skipped(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Zelda (USA).gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/tetris (World).gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Alleyway.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/.hidden.gb", "");

	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->entries->count == 3);
	if (dir->entries->count == 3) {
		CHECK_STR(at(dir, 0)->name, "Alleyway");
		CHECK_STR(at(dir, 1)->name, "tetris");
		CHECK_STR(at(dir, 2)->name, "Zelda");
		CHECK(at(dir, 0)->type == ENTRY_ROM);
		// one letter group per first letter
		CHECK(dir->alphas->count == 3);
		CHECK(at(dir, 0)->alpha == 0);
		CHECK(at(dir, 1)->alpha == 1);
		CHECK(at(dir, 2)->alpha == 2);
	}
	Directory_free(dir);
}

static void test_map_renames_resorts_and_hides(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Zelda (USA).gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/tetris (World).gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Alleyway.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/map.txt",
		"tetris (World).gb\tA Tetris\r\n"
		"Zelda (USA).gb\t.secret\n");

	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->entries->count == 2);
	if (dir->entries->count == 2) {
		CHECK_STR(at(dir, 0)->name, "A Tetris");
		CHECK_STR(at(dir, 1)->name, "Alleyway");
		CHECK(dir->alphas->count == 1);
	}
	Directory_free(dir);
}

static void test_same_name_shows_file_names(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Mario (USA).gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Mario (Europe).gb", "");

	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->entries->count == 2);
	if (dir->entries->count == 2) {
		CHECK_STR(at(dir, 0)->name, "Mario");
		CHECK_STR(at(dir, 1)->name, "Mario");
		CHECK(at(dir, 0)->unique && at(dir, 1)->unique);
		if (at(dir, 0)->unique && at(dir, 1)->unique) {
			int usa_first = !strcmp(at(dir, 0)->unique, "Mario (USA).gb");
			CHECK_STR(at(dir, usa_first ? 0 : 1)->unique, "Mario (USA).gb");
			CHECK_STR(at(dir, usa_first ? 1 : 0)->unique, "Mario (Europe).gb");
		}
	}
	Directory_free(dir);
}

static void test_collated_folders_merge_and_tag_duplicates(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GBX)/Tetris.gb", "");
	writeFile(ROMS_PATH "/Game Boy Color (GBC)/Pokemon.gbc", "");

	// "Game Boy (" collates both Game Boy folders, but not Game Boy Color
	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->entries->count == 2);
	if (dir->entries->count == 2) {
		CHECK(at(dir, 0)->unique && at(dir, 1)->unique);
		if (at(dir, 0)->unique && at(dir, 1)->unique) {
			int gb_first = !strcmp(at(dir, 0)->unique, "Tetris (GB)");
			CHECK_STR(at(dir, gb_first ? 0 : 1)->unique, "Tetris (GB)");
			CHECK_STR(at(dir, gb_first ? 1 : 0)->unique, "Tetris (GBX)");
		}
	}
	Directory_free(dir);
}

static void test_roms_lists_playable_consoles_once(void) {
	resetCard();
	addEmu("GB");
	addEmu("GBX");
	addEmu("NES");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GBX)/Tetris.gb", "");
	writeFile(ROMS_PATH "/Nintendo (NES)/.only-hidden", "");
	writeFile(ROMS_PATH "/Sega Genesis (MD)/Sonic.md", ""); // no emulator

	Array* roms = getRoms();
	CHECK(roms->count == 1);
	if (roms->count == 1) {
		Entry* entry = roms->items[0];
		CHECK_STR(entry->name, "Game Boy");
		CHECK(entry->type == ENTRY_DIR);
	}
	EntryArray_free(roms);
}

static void test_roms_map_renames_consoles(void) {
	resetCard();
	addEmu("GB");
	addEmu("NES");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gb", "");
	writeFile(ROMS_PATH "/Nintendo (NES)/Mario.nes", "");
	writeFile(ROMS_PATH "/map.txt", "Nintendo (NES)\tFamicom\n");

	Array* roms = getRoms();
	CHECK(roms->count == 2);
	if (roms->count == 2) {
		CHECK_STR(((Entry*)roms->items[0])->name, "Famicom");
		CHECK_STR(((Entry*)roms->items[1])->name, "Game Boy");
	}
	EntryArray_free(roms);
}

static void test_m3u_lists_discs(void) {
	resetCard();
	writeFile(ROMS_PATH "/PS (PS)/Game/Game (Disc 1).cue", "");
	writeFile(ROMS_PATH "/PS (PS)/Game/Game (Disc 2).cue", "");
	writeFile(ROMS_PATH "/PS (PS)/Game/Game.m3u",
		"Game (Disc 1).cue\n"
		"Missing.cue\n"
		"Game (Disc 2).cue\n");

	Directory* dir = Directory_new(ROMS_PATH "/PS (PS)/Game/Game.m3u", 0);
	CHECK(dir->entries->count == 2);
	if (dir->entries->count == 2) {
		CHECK_STR(at(dir, 0)->name, "Disc 1");
		CHECK_STR(at(dir, 1)->name, "Disc 2");
		CHECK_STR(at(dir, 1)->path, ROMS_PATH "/PS (PS)/Game/Game (Disc 2).cue");
	}
	Directory_free(dir);
}

static void test_collection_keeps_file_order_without_letter_index(void) {
	resetCard();
	writeFile(ROMS_PATH "/Game Boy (GB)/Zelda.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Alleyway.gb", "");
	writeFile(COLLECTIONS_PATH "/Favorites.txt",
		"/Roms/Game Boy (GB)/Zelda.gb\n"
		"/Roms/Game Boy (GB)/Missing.gb\n"
		"/Roms/Game Boy (GB)/Alleyway.gb\n");

	Directory* dir = Directory_new(COLLECTIONS_PATH "/Favorites.txt", 0);
	CHECK(dir->entries->count == 2);
	if (dir->entries->count == 2) {
		CHECK_STR(at(dir, 0)->name, "Zelda");
		CHECK_STR(at(dir, 1)->name, "Alleyway");
	}
	CHECK(dir->alphas->count == 0);
	Directory_free(dir);
}

static void test_collections_folder_lists_collections_as_folders(void) {
	resetCard();
	writeFile(COLLECTIONS_PATH "/Favorites.txt", "");
	writeFile(COLLECTIONS_PATH "/Action.txt", "");
	writeFile(COLLECTIONS_PATH "/.hidden.txt", "");

	CHECK(hasCollections());
	Directory* dir = Directory_new(COLLECTIONS_PATH, 0);
	CHECK(dir->entries->count == 2);
	if (dir->entries->count == 2) {
		CHECK_STR(at(dir, 0)->name, "Action");
		CHECK(at(dir, 0)->type == ENTRY_DIR);
	}
	Directory_free(dir);
}

static void test_root_orders_collections_consoles_tools(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gb", "");
	writeFile(COLLECTIONS_PATH "/Favorites.txt", "");
	writeFile(TOOLS_PATH "/Clock.pak/launch.sh", "");

	Array* root = getRoot();
	CHECK(root->count == 3);
	if (root->count == 3) {
		CHECK_STR(((Entry*)root->items[0])->path, COLLECTIONS_PATH);
		CHECK_STR(((Entry*)root->items[1])->name, "Game Boy");
		CHECK_STR(((Entry*)root->items[2])->path, TOOLS_PATH);
	}
	EntryArray_free(root);

	stub_show_collections = false;
	stub_show_tools = false;
	root = getRoot();
	CHECK(root->count == 1);
	EntryArray_free(root);
	stub_show_collections = true;
	stub_show_tools = true;
}

static void test_root_promotes_collections_without_consoles(void) {
	resetCard();
	writeFile(COLLECTIONS_PATH "/Favorites.txt", "");
	writeFile(COLLECTIONS_PATH "/Action.txt", "");

	Array* root = getRoot();
	CHECK(root->count == 2);
	if (root->count == 2) {
		CHECK_STR(((Entry*)root->items[0])->name, "Action");
		CHECK_STR(((Entry*)root->items[1])->name, "Favorites");
	}
	EntryArray_free(root);
}

// The whole file, or "" if it can't be read.
static char file_text[4096];
static const char* readFile(const char* path) {
	file_text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return file_text;
	size_t got = fread(file_text, 1, sizeof(file_text) - 1, f);
	file_text[got] = '\0';
	fclose(f);
	return file_text;
}

static void test_root_replace_skips_stock_but_loads_recents(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gb", "");
	writeFile(TOOLS_PATH "/Clock.pak/launch.sh", "");
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/Tetris.gb\n");

	Array* replacement = Array_new();
	Array_push(replacement, Entry_new(SDCARD_PATH "/Focus", ENTRY_DIR));
	stub_root_replace = replacement;
	Array* root = getRoot();
	CHECK(root == replacement);
	CHECK(root->count == 1);
	// recents still load for the game switcher
	CHECK(state.recents->count == 1);
	EntryArray_free(root);

	// without an owner's list, stock's root again
	root = getRoot();
	CHECK(root->count == 3);
	if (root->count == 3) CHECK_STR(((Entry*)root->items[0])->path, FAUX_RECENT_PATH);
	EntryArray_free(root);
}

static void test_recents_remove_and_clear_save(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/A.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/B.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/C.gb", "");
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/A.gb\n/Roms/Game Boy (GB)/B.gb\tBee\n/Roms/Game Boy (GB)/C.gb\n");
	hasRecents();
	CHECK(state.recents->count == 3);

	CHECK(Recents_remove(1) == 0);
	CHECK(state.recents->count == 2);
	CHECK_STR(readFile(RECENT_PATH), "/Roms/Game Boy (GB)/A.gb\n/Roms/Game Boy (GB)/C.gb\n");
	CHECK(Recents_remove(2) == -1);
	CHECK(Recents_remove(-1) == -1);
	CHECK(state.recents->count == 2);

	Recents_clear();
	CHECK(state.recents->count == 0);
	CHECK_STR(readFile(RECENT_PATH), "");
}

// repair_path stand-in: missing games moved into "Moved/", except the
// ones it has no answer for, plus a few bad answers.
static int repair_calls = 0;
static int repairPath(const char* missing, char* out, size_t size) {
	repair_calls++;
	const char* name = strrchr(missing, '/') + 1;
	if (!strcmp(name, "Lost.gb")) return 0;
	if (!strcmp(name, "Gone.gb")) snprintf(out, size, "%s", "/elsewhere/Gone.gb");          // off the card
	else if (!strcmp(name, "Dup.gb")) snprintf(out, size, ROMS_PATH "/Game Boy (GB)/B.gb"); // a later recent
	else if (!strcmp(name, "Up.gb")) snprintf(out, size, ROMS_PATH "/../Roms/Game Boy (GB)/Moved/Up.gb");
	else snprintf(out, size, ROMS_PATH "/Game Boy (GB)/Moved/%s", name);
	return 1;
}

static void test_recents_repair_missing(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/A.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/B.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Moved/Old.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Moved/Up.gb", "");
	const char* lines =
		"/Roms/Game Boy (GB)/A.gb\n"
		"/Roms/Game Boy (GB)/Old.gb\tOldie\n"   // moved: repaired in place, alias kept
		"/Roms/Game Boy (GB)/Lost.gb\n"          // no answer: dropped
		"/Roms/Game Boy (GB)/Gone.gb\n"          // off the card: dropped
		"/Roms/Game Boy (GB)/Dup.gb\n"           // already a recent: dropped
		"/Roms/Game Boy (GB)/Up.gb\n"            // through ..: dropped
		"/Roms/Game Boy (GB)/Nowhere.gb\n"       // the new path isn't there: dropped
		"/Roms/Game Boy (GB)/B.gb\n";

	// with no plugin, stock drops every missing line
	writeFile(RECENT_PATH, lines);
	CHECK(hasRecents());
	CHECK_STR(readFile(RECENT_PATH), "/Roms/Game Boy (GB)/A.gb\n/Roms/Game Boy (GB)/B.gb\n");

	// a plugin repairs what it can; files that exist are never asked about
	stub_repair_path = repairPath;
	repair_calls = 0;
	writeFile(RECENT_PATH, lines);
	CHECK(hasRecents());
	CHECK(repair_calls == 6);
	CHECK_STR(readFile(RECENT_PATH),
		"/Roms/Game Boy (GB)/A.gb\n/Roms/Game Boy (GB)/Moved/Old.gb\tOldie\n/Roms/Game Boy (GB)/B.gb\n");
	CHECK(state.recents->count == 3);

	// once repaired, nothing is missing and the plugin isn't asked again
	repair_calls = 0;
	CHECK(hasRecents());
	CHECK(repair_calls == 0);
	stub_repair_path = NULL;
}

static void addFocusEntry(Array* root) {
	Array_push(root, Entry_new(SDCARD_PATH "/Added", ENTRY_DIR));
}

static void test_root_replace_adds_entries_only_when_asked(void) {
	resetCard();
	stub_root_entries = addFocusEntry;

	// focus mode style: the owner's list alone
	Array* replacement = Array_new();
	Array_push(replacement, Entry_new(SDCARD_PATH "/Focus", ENTRY_DIR));
	stub_root_replace = replacement;
	stub_root_with_entries = 0;
	Array* root = getRoot();
	CHECK(root->count == 1);
	EntryArray_free(root);

	// all games style: plugins' entries follow the owner's
	replacement = Array_new();
	Array_push(replacement, Entry_new(SDCARD_PATH "/All", ENTRY_DIR));
	stub_root_replace = replacement;
	stub_root_with_entries = 1;
	root = getRoot();
	CHECK(root->count == 2);
	if (root->count == 2) {
		CHECK_STR(((Entry*)root->items[0])->path, SDCARD_PATH "/All");
		CHECK_STR(((Entry*)root->items[1])->path, SDCARD_PATH "/Added");
	}
	EntryArray_free(root);

	stub_root_entries = NULL;
	stub_root_with_entries = 0;
}

static void test_recents_set_rewrites_a_path(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/A.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/B.gb", "");
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/A.gb\tAy\n/Roms/Game Boy (GB)/B.gb\n");
	hasRecents();
	CHECK(state.recents->count == 2);

	// keeps its place and alias
	CHECK(Recents_set(0, ROMS_PATH "/Game Boy (GB)/A2.gb") == 0);
	CHECK_STR(readFile(RECENT_PATH), "/Roms/Game Boy (GB)/A2.gb\tAy\n/Roms/Game Boy (GB)/B.gb\n");
	// setting the same path again is fine; another recent's path isn't
	CHECK(Recents_set(0, ROMS_PATH "/Game Boy (GB)/A2.gb") == 0);
	CHECK(Recents_set(0, ROMS_PATH "/Game Boy (GB)/B.gb") == -1);
	CHECK(Recents_set(2, ROMS_PATH "/Game Boy (GB)/C.gb") == -1);
	CHECK(Recents_set(-1, ROMS_PATH "/Game Boy (GB)/C.gb") == -1);
	CHECK(Recents_set(1, "/elsewhere/C.gb") == -1);
	CHECK(Recents_set(1, NULL) == -1);
	CHECK_STR(readFile(RECENT_PATH), "/Roms/Game Boy (GB)/A2.gb\tAy\n/Roms/Game Boy (GB)/B.gb\n");
}

// Plugin tiles' actions, as a plugin registers them.
static int tile_ok = 1, light_on = 1;
static int tileAvailable(void* d) { (void)d; return tile_ok; }
static int lightIsOn(void* d) { (void)d; return light_on; }
static void tileRun(void* d) { (void)d; }
static const Action tileAction = { "test.tile", "Tile", tileAvailable, tileRun };
static const Action lightAction = { "test.light", "Light", NULL, tileRun };

static Array* twoToggles(void) {
	Array* entries = Array_new();
	Array_push(entries, Entry_new("Wifi", ENTRY_DIP));
	Array_push(entries, Entry_new("Reboot", ENTRY_DIP));
	return entries;
}

static void rowNames(Array* entries, char* out, size_t size) {
	out[0] = '\0';
	for (int i = 0; i < entries->count; i++) {
		size_t len = strlen(out);
		snprintf(out + len, size - len, "%s%s", i ? "," : "", ((Entry*)entries->items[i])->name);
	}
}

static void test_quick_tiles_hide_and_add(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/A.gb", "");
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/A.gb\n");
	writeFile(TOOLS_PATH "/Clock.pak/launch.sh", "");
	hasRecents();
	stub_quick_items = twoToggles;
	char got[256];

	// no plugin tiles: stock's rows
	Array* top = getQuickEntries();
	rowNames(top, got, sizeof(got));
	CHECK_STR(got, "Recents,Games,Tools");
	EntryArray_free(top);
	Array* bottom = getQuickToggles();
	rowNames(bottom, got, sizeof(got));
	CHECK_STR(got, "Wifi,Reboot");
	EntryArray_free(bottom);

	// unknown actions and rows are refused
	Actions_register(&tileAction);
	Actions_register(&lightAction);
	CHECK(QuickTiles_add("test.missing", QUICK_ROW_TOP, NULL, NULL, NULL) == -1);
	CHECK(QuickTiles_add("test.tile", 7, NULL, NULL, NULL) == -1);
	CHECK(QuickTiles_hide("", 1) == -1);
	CHECK(!QuickTiles_any());

	// plugin tiles go after the built-ins; hidden built-ins go away
	CHECK(QuickTiles_add("test.tile", QUICK_ROW_TOP, "/icon.png", NULL, NULL) == 0);
	CHECK(QuickTiles_add("test.light", QUICK_ROW_BOTTOM, "/on.png", "/off.png", lightIsOn) == 0);
	CHECK(QuickTiles_hide("Games", 1) == 0);
	CHECK(QuickTiles_hide("Reboot", 1) == 0);
	CHECK(QuickTiles_hide("Reboot", 1) == 0); // twice is once
	top = getQuickEntries();
	rowNames(top, got, sizeof(got));
	CHECK_STR(got, "Recents,Tools,Tile");
	if (top->count == 3) {
		Entry* tile = top->items[2];
		CHECK_STR(tile->path, "action:test.tile");
		CHECK(tile->type == ENTRY_DIP);
		const QuickTile* t = QuickTiles_forEntry(tile);
		CHECK(t && t->action == &tileAction && t->icon && !strcmp(t->icon, "/icon.png"));
		CHECK(!QuickTile_isOff(t));
		CHECK(QuickTiles_forEntry(top->items[0]) == NULL);
	}
	EntryArray_free(top);
	bottom = getQuickToggles();
	rowNames(bottom, got, sizeof(got));
	CHECK_STR(got, "Wifi,Light");
	if (bottom->count == 2) {
		const QuickTile* t = QuickTiles_forEntry(bottom->items[1]);
		CHECK(t && !QuickTile_isOff(t));
		light_on = 0;
		CHECK(QuickTile_isOff(t));
		light_on = 1;
	}
	EntryArray_free(bottom);

	// a tile shows only while its action is available, and hides by action id
	tile_ok = 0;
	top = getQuickEntries();
	rowNames(top, got, sizeof(got));
	CHECK_STR(got, "Recents,Tools");
	EntryArray_free(top);
	tile_ok = 1;
	CHECK(QuickTiles_hide("test.tile", 1) == 0);
	CHECK(QuickTiles_hide("Games", 0) == 0);
	top = getQuickEntries();
	rowNames(top, got, sizeof(got));
	CHECK_STR(got, "Recents,Games,Tools");
	EntryArray_free(top);

	// with recents cleared, the rebuilt row has no Recents tile
	Recents_clear();
	CHECK(QuickTiles_hide("test.tile", 0) == 0);
	top = getQuickEntries();
	rowNames(top, got, sizeof(got));
	CHECK_STR(got, "Games,Tools,Tile");
	EntryArray_free(top);

	QuickTiles_reset();
	CHECK(!QuickTiles_any());
	stub_quick_items = NULL;
}

static void test_quick_tiles_rename(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/A.gb", "");
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/A.gb\n");
	writeFile(TOOLS_PATH "/Clock.pak/launch.sh", "");
	hasRecents();

	// only the top row's built-ins take a label
	CHECK(QuickTiles_rename("Wifi", "Net") == -1);
	CHECK(QuickTiles_rename("test.tile", "Other") == -1);
	CHECK(QuickTiles_rename(NULL, "Other") == -1);
	CHECK(!QuickTiles_any());

	CHECK(QuickTiles_rename("Games", "Library") == 0);
	CHECK(QuickTiles_rename("Tools", "Apps") == 0);
	CHECK(QuickTiles_any());
	Actions_register(&tileAction);
	CHECK(QuickTiles_add("test.tile", QUICK_ROW_TOP, NULL, NULL, NULL) == 0);
	CHECK(QuickTiles_hide("Tools", 1) == 0);
	Array* top = getQuickEntries();
	char got[256];
	rowNames(top, got, sizeof(got));
	// names stay the built-in ones, so hiding and art still match them
	CHECK_STR(got, "Recents,Games,Tile");
	if (top->count == 3) {
		CHECK_STR(QuickTiles_label(top->items[0]), "Recents");
		CHECK_STR(QuickTiles_label(top->items[1]), "Library");
		CHECK_STR(QuickTiles_label(top->items[2]), "Tile");
	}
	EntryArray_free(top);

	// NULL or "" puts the built-in label back
	CHECK(QuickTiles_rename("Games", "") == 0);
	CHECK(QuickTiles_rename("Tools", NULL) == 0);
	top = getQuickEntries();
	if (top->count >= 2) CHECK_STR(QuickTiles_label(top->items[1]), "Games");
	EntryArray_free(top);

	QuickTiles_reset();
	CHECK(!QuickTiles_any());
}

// A plugin whose init declines takes its tiles, hidden names and labels
// with it: the loader saves them before init and puts them back.
static void test_quick_tiles_save_restore(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/A.gb", "");
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/A.gb\n");
	writeFile(TOOLS_PATH "/Clock.pak/launch.sh", "");
	hasRecents();
	Actions_register(&tileAction);
	Actions_register(&lightAction);
	char got[256];

	// an earlier plugin's tile, hidden name and label stay
	CHECK(QuickTiles_add("test.tile", QUICK_ROW_TOP, "/icon.png", NULL, NULL) == 0);
	CHECK(QuickTiles_hide("Tools", 1) == 0);
	CHECK(QuickTiles_rename("Games", "Library") == 0);
	QuickTilesSaved* saved = QuickTiles_save();

	// the declining one changes all three, then is undone
	CHECK(QuickTiles_add("test.light", QUICK_ROW_TOP, NULL, NULL, lightIsOn) == 0);
	CHECK(QuickTiles_add("test.tile", QUICK_ROW_TOP, "/other.png", NULL, NULL) == 0);
	CHECK(QuickTiles_hide("Tools", 0) == 0);
	CHECK(QuickTiles_hide("Recents", 1) == 0);
	CHECK(QuickTiles_rename("Games", "Other") == 0);
	CHECK(QuickTiles_rename("Recents", "Played") == 0);
	QuickTiles_restore(saved);

	Array* top = getQuickEntries();
	rowNames(top, got, sizeof(got));
	CHECK_STR(got, "Recents,Games,Tile");
	if (top->count == 3) {
		CHECK_STR(QuickTiles_label(top->items[0]), "Recents");
		CHECK_STR(QuickTiles_label(top->items[1]), "Library");
		const QuickTile* t = QuickTiles_forEntry(top->items[2]);
		CHECK(t && t->icon && !strcmp(t->icon, "/icon.png") && t->isOn == NULL);
	}
	EntryArray_free(top);

	// with nothing added before, restoring leaves no tiles at all
	QuickTiles_reset();
	saved = QuickTiles_save();
	CHECK(QuickTiles_add("test.tile", QUICK_ROW_TOP, NULL, NULL, NULL) == 0);
	CHECK(QuickTiles_hide("Games", 1) == 0);
	QuickTiles_restore(saved);
	CHECK(!QuickTiles_any());

	// a copy that isn't needed is freed without changing anything
	CHECK(QuickTiles_add("test.tile", QUICK_ROW_TOP, "/icon.png", NULL, NULL) == 0);
	QuickTiles_discard(QuickTiles_save());
	CHECK(QuickTiles_any());

	QuickTiles_reset();
	CHECK(!QuickTiles_any());
}

static void addGames(const char* names) {
	char list[256];
	snprintf(list, sizeof(list), "%s", names);
	for (char* name = strtok(list, ","); name; name = strtok(NULL, ",")) {
		char path[512];
		snprintf(path, sizeof(path), ROMS_PATH "/Game Boy (GB)/%s.gb", name);
		writeFile(path, "");
	}
}

static void removeGame(const char* name) {
	char path[512];
	snprintf(path, sizeof(path), ROMS_PATH "/Game Boy (GB)/%s.gb", name);
	CHECK(unlink(path) == 0);
}

static void test_reload_follows_the_selected_entry(void) {
	resetCard();
	addEmu("GB");
	addGames("A,B,C,D");
	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 2);
	dir->start = 0;
	dir->end = 4;
	CHECK_STR(at(dir, dir->selected)->name, "C");

	// a new game before it moves it down a row
	addGames("AA");
	Directory_reload(dir);
	CHECK(dir->entries->count == 5);
	CHECK(dir->selected == 3);
	CHECK_STR(at(dir, dir->selected)->name, "C");
	CHECK(dir->start == 0 && dir->end == 5);

	// gone: the same row, now the next game
	removeGame("C");
	Directory_reload(dir);
	CHECK(dir->selected == 3);
	CHECK_STR(at(dir, dir->selected)->name, "D");

	// gone from the end: the new last row
	removeGame("D");
	Directory_reload(dir);
	CHECK(dir->entries->count == 3);
	CHECK(dir->selected == 2);
	CHECK(dir->start == 0 && dir->end == 3);

	removeGame("A");
	removeGame("AA");
	removeGame("B");
	Directory_reload(dir);
	CHECK(dir->entries->count == 0);
	CHECK(dir->selected == 0);
	CHECK(dir->start == 0 && dir->end == 0);
	Directory_free(dir);
}

static void test_reload_keeps_the_selection_in_view(void) {
	resetCard();
	addEmu("GB");
	addGames("A,B,C,D,E,F,G,H");
	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 7);
	dir->start = 8 - MAIN_ROW_COUNT;
	dir->end = 8;

	// fewer rows than fit: everything shows
	removeGame("H");
	removeGame("G");
	removeGame("F");
	Directory_reload(dir);
	CHECK(dir->entries->count == 5);
	CHECK(dir->selected == 4);
	CHECK(dir->start == 0 && dir->end == 5);

	// more than fit, selection near the top: the window starts at it
	addGames("A1,A2,A3,A4");
	dir->selected = 0;
	dir->start = 0;
	dir->end = 5;
	Directory_reload(dir);
	CHECK(dir->entries->count == 9);
	CHECK(dir->selected == 0);
	CHECK(dir->start == 0 && dir->end == MAIN_ROW_COUNT);
	Directory_free(dir);
}

///////////////////////////////////////
// plugin sort and list_ready

// Names starting with letter first; no opinion on the rest.
static int pinLetter(const MosaicEntry* a, const MosaicEntry* b, char letter) {
	int pa = ((const Entry*)a)->name[0] == letter;
	int pb = ((const Entry*)b)->name[0] == letter;
	return pb - pa;
}
static int pinC(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	(void)dir_path;
	return pinLetter(a, b, 'C');
}
static int pinB(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	(void)dir_path;
	return pinLetter(a, b, 'B');
}
static int reverseNames(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	(void)dir_path;
	return strcmp(((const Entry*)b)->name, ((const Entry*)a)->name);
}
static int noOpinion(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	(void)a;
	(void)b;
	(void)dir_path;
	return 0;
}

// The names in items, comma separated.
static char names_text[256];
static const char* names(Array* items) {
	names_text[0] = '\0';
	for (int i = 0; i < items->count; i++) {
		if (i) strcat(names_text, ",");
		strcat(names_text, ((Entry*)items->items[i])->name);
	}
	return names_text;
}

static Array* namedEntries(const char* list) {
	Array* items = Array_new();
	char copy[256];
	snprintf(copy, sizeof(copy), "%s", list);
	for (char* name = strtok(copy, ","); name; name = strtok(NULL, ",")) {
		char path[300];
		snprintf(path, sizeof(path), "/x/%s", name);
		Array_push(items, Entry_newNamed(path, ENTRY_ROM, name));
	}
	return items;
}

static void test_sort_chain_first_opinion_wins(void) {
	Array* items = namedEntries("A,B,C,D,E");

	// no comparators, or none with an opinion: unchanged
	ListSort_chain(items, NULL, 0, "/x");
	CHECK_STR(names(items), "A,B,C,D,E");
	ListSortCompare none[] = { noOpinion };
	ListSort_chain(items, none, 1, "/x");
	CHECK_STR(names(items), "A,B,C,D,E");

	// one opinion: the rest keep their order
	ListSortCompare pin[] = { pinC };
	ListSort_chain(items, pin, 1, "/x");
	CHECK_STR(names(items), "C,A,B,D,E");

	// the first comparator decides; a later one only breaks its ties
	ListSortCompare chain[] = { pinC, pinB, reverseNames };
	ListSort_chain(items, chain, 3, "/x");
	CHECK_STR(names(items), "C,B,E,D,A");

	// a plugin without an opinion doesn't stop the next one
	ListSortCompare skip[] = { noOpinion, pinB };
	ListSort_chain(items, skip, 2, "/x");
	CHECK_STR(names(items), "B,C,E,D,A");
	EntryArray_free(items);
}

static const char* sorted_dir = NULL;
static int pinZelda(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	sorted_dir = dir_path;
	return pinLetter(a, b, 'Z');
}

static void test_plugin_sort_runs_after_names(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Zelda.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/tetris.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Alleyway.gb", "");
	// map.txt renames, and the launcher sorts by name, before plugins sort
	writeFile(ROMS_PATH "/Game Boy (GB)/map.txt", "Alleyway.gb\tBreakout\n");

	stub_sort_compare = pinZelda;
	sorted_dir = NULL;
	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	stub_sort_compare = NULL;
	CHECK_STR(names(dir->entries), "Zelda,Breakout,tetris");
	CHECK(sorted_dir && !strcmp(sorted_dir, ROMS_PATH "/Game Boy (GB)"));
	// the letter index follows the plugin's order
	CHECK(dir->alphas->count == 3);
	if (dir->entries->count == 3) {
		CHECK(at(dir, 0)->alpha == 0);
		CHECK(at(dir, 1)->alpha == 1);
		CHECK(at(dir, 2)->alpha == 2);
		CHECK(dir->alphas->items[1] == 1);
	}
	Directory_free(dir);
}

static void test_plugin_sort_reorders_collections(void) {
	resetCard();
	writeFile(ROMS_PATH "/Game Boy (GB)/Alleyway.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Breakout.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Zelda.gb", "");
	writeFile(COLLECTIONS_PATH "/Favorites.txt",
		"/Roms/Game Boy (GB)/Breakout.gb\n"
		"/Roms/Game Boy (GB)/Alleyway.gb\n"
		"/Roms/Game Boy (GB)/Zelda.gb\n");

	// pinned first, the rest in the collection's own order
	stub_sort_compare = pinZelda;
	Directory* dir = Directory_new(COLLECTIONS_PATH "/Favorites.txt", 0);
	stub_sort_compare = NULL;
	CHECK_STR(names(dir->entries), "Zelda,Breakout,Alleyway");
	CHECK(dir->alphas->count == 0);
	Directory_free(dir);
}

// Gives games that share a title their extension, as a title
// disambiguation plugin would. Returns 1 if it renamed any.
static const char* ready_dir = NULL;
static int addExtensions(Array* entries, const char* dir_path) {
	ready_dir = dir_path;
	int renamed = 0;
	int collides[entries->count];
	for (int i = 0; i < entries->count; i++) {
		collides[i] = 0;
		for (int j = 0; j < entries->count; j++) {
			if (j != i && !strcmp(((Entry*)entries->items[j])->name, ((Entry*)entries->items[i])->name)) collides[i] = 1;
		}
	}
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		if (!collides[i]) continue;
		char name[300];
		snprintf(name, sizeof(name), "%s (%s)", entry->name, strrchr(entry->path, '.') + 1);
		free(entry->name);
		entry->name = strdup(name);
		renamed = 1;
	}
	// a name that sorts differently, to show the list is sorted again
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		if (strcmp(entry->name, "Alleyway")) continue;
		free(entry->name);
		entry->name = strdup("Breakout");
		renamed = 1;
	}
	return renamed;
}

static void test_list_ready_names_are_drawn_and_indexed(void) {
	resetCard();
	addEmu("GB");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gbc", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Alleyway.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Zelda.gb", "");

	// stock: the colliding titles show their file names
	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->entries->count == 4);
	if (dir->entries->count == 4) {
		CHECK_STR(at(dir, 1)->name, "Tetris");
		CHECK(at(dir, 1)->unique && !strcmp(at(dir, 1)->unique, "Tetris.gb"));
	}
	Directory_free(dir);

	stub_list_ready = addExtensions;
	stub_sort_compare = pinZelda;
	ready_dir = NULL;
	dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	stub_list_ready = NULL;
	stub_sort_compare = NULL;
	CHECK(ready_dir && !strcmp(ready_dir, ROMS_PATH "/Game Boy (GB)"));
	// sorted again by the new names, then by the plugin
	CHECK_STR(names(dir->entries), "Zelda,Breakout,Tetris (gb),Tetris (gbc)");
	if (dir->entries->count == 4) {
		// no longer duplicates, so the new names are what's drawn
		CHECK(at(dir, 2)->unique == NULL);
		CHECK(at(dir, 3)->unique == NULL);
		// and the letter index uses them
		CHECK(dir->alphas->count == 3);
		CHECK(at(dir, 1)->alpha == 1);
		CHECK(at(dir, 2)->alpha == 2);
		CHECK(at(dir, 3)->alpha == 2);
	}
	Directory_free(dir);
}

static void test_list_ready_renames_keep_collection_order(void) {
	resetCard();
	writeFile(ROMS_PATH "/Game Boy (GB)/Zelda.gb", "");
	writeFile(ROMS_PATH "/Game Boy (GB)/Alleyway.gb", "");
	writeFile(COLLECTIONS_PATH "/Favorites.txt",
		"/Roms/Game Boy (GB)/Zelda.gb\n"
		"/Roms/Game Boy (GB)/Alleyway.gb\n");

	stub_list_ready = addExtensions;
	Directory* dir = Directory_new(COLLECTIONS_PATH "/Favorites.txt", 0);
	stub_list_ready = NULL;
	CHECK_STR(names(dir->entries), "Zelda,Breakout");
	Directory_free(dir);
}

///////////////////////////////////////
// hiding names

static int hide_asks = 0;
static const char* hide_dir = NULL;
static int hideB(const MosaicEntry* entry, const char* dir_path) {
	hide_asks++;
	hide_dir = dir_path;
	return !strcmp(((const Entry*)entry)->name, "B");
}
static int hideVowels(const MosaicEntry* entry, const char* dir_path) {
	(void)dir_path;
	return strchr("AEIOU", ((const Entry*)entry)->name[0]) ? 1 : 0;
}
static int hideNothing(const MosaicEntry* entry, const char* dir_path) {
	(void)entry;
	(void)dir_path;
	hide_asks++;
	return 0;
}

static void test_hide_chain_any_hides(void) {
	Array* items = namedEntries("A,B,C,D,E");

	// no hooks, no items, or none hiding anything: no flags
	CHECK(ListHide_chain(items, NULL, 0, "/x") == NULL);
	ListHideName nothing[] = { hideNothing };
	hide_asks = 0;
	CHECK(ListHide_chain(items, nothing, 1, "/x") == NULL);
	CHECK(hide_asks == 5);
	Array* empty = Array_new();
	CHECK(ListHide_chain(empty, nothing, 1, "/x") == NULL);
	Array_free(empty);

	// one byte an item, 1 for each one any hook hides
	ListHideName both[] = { hideVowels, hideB };
	hide_asks = 0;
	unsigned char* hidden = ListHide_chain(items, both, 2, "/x");
	CHECK(hidden != NULL);
	if (hidden) CHECK(hidden[0] == 1 && hidden[1] == 1 && hidden[2] == 0 && hidden[3] == 0 && hidden[4] == 1);
	// the first to hide one ends the asking for it: A and E aren't asked again
	CHECK(hide_asks == 3);
	CHECK(hide_dir && !strcmp(hide_dir, "/x"));
	free(hidden);
	EntryArray_free(items);
}

static void test_hidden_names_follow_the_final_order(void) {
	resetCard();
	addEmu("GB");
	addGames("A,B,C");

	// stock: none
	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->names_hidden == NULL);
	Directory_free(dir);

	// after the plugins' sort, so the flags line up with what's drawn
	stub_hide_name = hideB;
	stub_sort_compare = reverseNames;
	hide_dir = NULL;
	dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK_STR(names(dir->entries), "C,B,A");
	CHECK(hide_dir && !strcmp(hide_dir, ROMS_PATH "/Game Boy (GB)"));
	CHECK(dir->names_hidden && !dir->names_hidden[0] && dir->names_hidden[1] && !dir->names_hidden[2]);

	// asked again when the folder is listed again
	removeGame("B");
	addGames("AA");
	hide_asks = 0;
	Directory_reload(dir);
	CHECK(hide_asks == 3);
	CHECK(dir->names_hidden == NULL);
	addGames("B");
	Directory_reload(dir);
	CHECK_STR(names(dir->entries), "C,B,AA,A");
	CHECK(dir->names_hidden && dir->names_hidden[1] && !dir->names_hidden[0] && !dir->names_hidden[2] && !dir->names_hidden[3]);
	stub_hide_name = NULL;
	stub_sort_compare = NULL;
	Directory_free(dir);
}

///////////////////////////////////////
// selecting a row

static void test_select_keeps_the_row_in_view(void) {
	resetCard();
	addEmu("GB");
	addGames("A,B,C,D,E,F,G,H,I,J,K,L");
	int count = 12;
	Directory* dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->entries->count == count);
	dir->start = 0;
	dir->end = MAIN_ROW_COUNT;

	// already in view: nothing scrolls
	Directory_select(dir, 1);
	CHECK(dir->selected == 1);
	CHECK(dir->start == 0 && dir->end == MAIN_ROW_COUNT);

	// below the window: it scrolls until the row is the last one shown
	Directory_select(dir, 10);
	CHECK(dir->selected == 10);
	CHECK(dir->end == 11 && dir->start == 11 - MAIN_ROW_COUNT);

	// the last row
	Directory_select(dir, count - 1);
	CHECK(dir->end == count && dir->start == count - MAIN_ROW_COUNT);

	// above the window: it becomes the first one shown
	Directory_select(dir, 2);
	CHECK(dir->selected == 2);
	CHECK(dir->start == 2 && dir->end == 2 + MAIN_ROW_COUNT);

	Directory_select(dir, 0);
	CHECK(dir->start == 0 && dir->end == MAIN_ROW_COUNT);
	Directory_free(dir);
}

// A strings slot owner for the root's names, counting what it's asked.
static int tr_asks = 0;
static const char* rootTranslate(const char* plugin_id, const char* context, const char* text) {
	tr_asks++;
	if (plugin_id) return NULL; // the root's names are the launcher's own
	if (!context) {
		if (!strcmp(text, "Recently Played")) return "Joués récemment";
		if (!strcmp(text, "Tools")) return "Outils";
		if (!strcmp(text, "Tetris")) return "Tétris"; // a game, never asked
		return NULL;
	}
	if (!strcmp(context, "systems")) {
		if (!strcmp(text, "Game Boy")) return "Zelda Boy"; // sorts after Sega, but stays in place
		if (!strcmp(text, "Atari 2600")) return "Atari VCS";
		if (!strcmp(text, "Sega Genesis")) return "Sega Mega Drive";
		if (!strcmp(text, "Famicom")) return "Famicom (FR)";
	}
	return NULL;
}

static void test_root_names_translated_for_display(void) {
	resetCard();
	addEmu("GB");
	addEmu("A2600");
	addEmu("MD");
	addEmu("NES");
	writeFile(ROMS_PATH "/Game Boy (GB)/Tetris.gb", "");
	writeFile(ROMS_PATH "/01) Atari 2600 (A2600)/Pitfall.a26", "");
	writeFile(ROMS_PATH "/Sega Genesis (MD)/Sonic.md", "");
	writeFile(ROMS_PATH "/Nintendo (NES)/Mario.nes", "");
	// the user's name for a folder wins, and isn't translated
	writeFile(ROMS_PATH "/map.txt", "Nintendo (NES)\tFamicom\n");
	writeFile(COLLECTIONS_PATH "/Favorites.txt", "");
	writeFile(TOOLS_PATH "/Clock.pak/launch.sh", "");
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/Tetris.gb\n");

	TrOwner owner = { rootTranslate, NULL, "test", NULL };
	Tr_setOwner(&owner);
	tr_asks = 0;
	Directory* dir = Directory_new(SDCARD_PATH, 0);
	// stock's order, by the English names
	CHECK(dir->entries->count == 7);
	if (dir->entries->count == 7) {
		CHECK_STR(at(dir, 0)->name, "Joués récemment");
		CHECK_STR(at(dir, 0)->path, FAUX_RECENT_PATH);
		CHECK_STR(at(dir, 1)->name, "Collections"); // no translation
		CHECK_STR(at(dir, 2)->name, "01) Atari VCS");
		CHECK_STR(at(dir, 3)->name, "Famicom");
		CHECK_STR(at(dir, 4)->name, "Zelda Boy");
		// paths, which art goes by, stay English
		CHECK_STR(at(dir, 4)->path, ROMS_PATH "/Game Boy (GB)");
		CHECK_STR(at(dir, 5)->name, "Sega Mega Drive");
		CHECK_STR(at(dir, 6)->name, "Outils");
		CHECK_STR(at(dir, 6)->path, TOOLS_PATH);
	}
	Directory_free(dir);

	// the Roms folder shows the same names
	dir = Directory_new(ROMS_PATH, 0);
	CHECK(dir->entries->count == 4);
	if (dir->entries->count == 4) {
		CHECK_STR(at(dir, 0)->name, "01) Atari VCS");
		CHECK_STR(at(dir, 2)->name, "Zelda Boy");
	}
	Directory_free(dir);

	// a console's games aren't asked about
	int asks = tr_asks;
	dir = Directory_new(ROMS_PATH "/Game Boy (GB)", 0);
	CHECK(dir->entries->count == 1);
	if (dir->entries->count == 1) CHECK_STR(at(dir, 0)->name, "Tetris");
	CHECK(tr_asks == asks);
	Directory_free(dir);

	// with no owner, stock's English
	Tr_setOwner(NULL);
	dir = Directory_new(SDCARD_PATH, 0);
	CHECK(dir->entries->count == 7);
	if (dir->entries->count == 7) {
		CHECK_STR(at(dir, 0)->name, "Recently Played");
		CHECK_STR(at(dir, 2)->name, "01) Atari 2600");
		CHECK_STR(at(dir, 4)->name, "Game Boy");
		CHECK_STR(at(dir, 6)->name, "Tools");
	}
	Directory_free(dir);
}

int main(void) {
	state.recents = Array_new();
	test_root_orders_collections_consoles_tools();
	test_root_promotes_collections_without_consoles();
	test_console_folder_sorted_and_hidden_files_skipped();
	test_map_renames_resorts_and_hides();
	test_same_name_shows_file_names();
	test_collated_folders_merge_and_tag_duplicates();
	test_roms_lists_playable_consoles_once();
	test_roms_map_renames_consoles();
	test_m3u_lists_discs();
	test_collection_keeps_file_order_without_letter_index();
	test_collections_folder_lists_collections_as_folders();
	test_root_replace_skips_stock_but_loads_recents();
	test_recents_remove_and_clear_save();
	test_recents_repair_missing();
	test_root_replace_adds_entries_only_when_asked();
	test_recents_set_rewrites_a_path();
	test_quick_tiles_hide_and_add();
	test_quick_tiles_save_restore();
	test_quick_tiles_rename();
	test_reload_follows_the_selected_entry();
	test_reload_keeps_the_selection_in_view();
	test_sort_chain_first_opinion_wins();
	test_plugin_sort_runs_after_names();
	test_plugin_sort_reorders_collections();
	test_list_ready_names_are_drawn_and_indexed();
	test_list_ready_renames_keep_collection_order();
	test_hide_chain_any_hides();
	test_hidden_names_follow_the_final_order();
	test_select_keeps_the_row_in_view();
	test_root_names_translated_for_display();
	CHECK_DONE();
}
