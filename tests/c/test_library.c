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
	CHECK_DONE();
}
