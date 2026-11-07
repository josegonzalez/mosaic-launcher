// Putting the selection back after closing a folder (state.restore_*),
// against nav.c's folder stack on a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "launcher.h"
#include "stubs.h"

// What nav.c reaches beyond the listing code; none of it runs here.
int openRom(char* path, char* last) { (void)path; (void)last; return 0; }
int openPak(char* path) { (void)path; return 0; }
void readyResume(Entry* entry) { (void)entry; }
void QuickItem_activate(const Entry* entry) { (void)entry; }
void QuickMenu_refit(void) {}

#define GB ROMS_PATH "/Game Boy (GB)"
#define GBA ROMS_PATH "/Game Boy Advance (GBA)"

static void resetCard(void) {
	CHECK(system("rm -rf '" SDCARD_PATH "'") == 0);
	CHECK(mkdir(SDCARD_PATH, 0755) == 0);
}

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

static void addGames(const char* folder, const char* tag, int count) {
	char path[512];
	snprintf(path, sizeof(path), "%s/Emus/%s.pak/launch.sh", PAKS_PATH, tag);
	writeFile(path, "#!/bin/sh\n");
	for (int i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s/Game %02d.gb", folder, i);
		writeFile(path, "");
	}
}

// Root: Recently Played, Game Boy (10 games), Game Boy Advance (2 games).
static void makeCard(void) {
	resetCard();
	addGames(GB, "GB", 10);
	addGames(GBA, "GBA", 2);
	writeFile(RECENT_PATH, "/Roms/Game Boy (GB)/Game 00.gb\n");
	stub_show_tools = false;
}

static void startMenu(void) {
	state.stack = Array_new();
	state.recents = Array_new();
	state.top = NULL;
	state.quick = NULL;
	state.restore_depth = -1;
	state.restore_relative = -1;
	openDirectory(SDCARD_PATH, 0);
}

static void stopMenu(void) {
	DirectoryArray_free(state.stack);
	RecentArray_free(state.recents);
	state.stack = NULL;
	state.recents = NULL;
	state.top = NULL;
}

static int rowOf(const char* path) {
	return EntryArray_indexOf(state.top->entries, (char*)path);
}

// Opens a root row the way pressing A on it does.
static void openRow(int row) {
	Directory_select(state.top, row);
	Entry_open(state.top->entries->items[row]);
}

// Leaves Game Boy with its last game selected.
static void leaveGameBoyOnLastGame(void) {
	openRow(rowOf(GB));
	CHECK_STR(state.top->path, GB);
	Directory_select(state.top, 9);
	closeDirectory();
	CHECK_STR(state.top->path, SDCARD_PATH);
}

static void test_reopening_restores_the_selection(void) {
	makeCard();
	startMenu();
	openRow(rowOf(GB));
	Directory_select(state.top, 9);
	int start = state.top->start, end = state.top->end;
	closeDirectory();

	openRow(rowOf(GB));
	CHECK_STR(state.top->path, GB);
	CHECK(state.top->selected == 9);
	CHECK(state.top->start == start && state.top->end == end);
	stopMenu();
}

static void test_reload_shifting_rows_keeps_another_folder_at_its_start(void) {
	makeCard();
	startMenu();
	leaveGameBoyOnLastGame();
	int was_gb = rowOf(GB);

	// clearing recents drops the Recently Played row, so Game Boy Advance
	// moves up into Game Boy's old row
	Recents_clear();
	Menu_reload();
	CHECK(rowOf(GBA) == was_gb);

	openRow(rowOf(GBA));
	CHECK_STR(state.top->path, GBA);
	CHECK(state.top->selected == 0);
	CHECK(state.top->start == 0 && state.top->end == 2);
	stopMenu();
}

static void test_rebuilt_root_keeps_another_folder_at_its_start(void) {
	makeCard();
	startMenu();
	leaveGameBoyOnLastGame();
	int was_gb = rowOf(GB);
	openRow(rowOf(GBA));

	// from inside a system, opening the root lists it again (as platform
	// jump does), without the Recently Played row recents had
	Recents_clear();
	CHECK(Entry_openPath(SDCARD_PATH, ENTRY_DIR) == 1);
	CHECK(state.stack->count == 1);
	CHECK(rowOf(GBA) == was_gb);

	Directory_select(state.top, rowOf(GBA));
	CHECK(Entry_openPath(GBA, ENTRY_DIR) == 1);
	CHECK_STR(state.top->path, GBA);
	CHECK(state.top->selected == 0);
	CHECK(state.top->start == 0 && state.top->end == 2);
	stopMenu();
}

static void test_restore_fits_a_folder_that_shrank(void) {
	makeCard();
	startMenu();
	leaveGameBoyOnLastGame();

	// games removed while the folder was closed
	char path[512];
	for (int i = 4; i < 10; i++) {
		snprintf(path, sizeof(path), GB "/Game %02d.gb", i);
		CHECK(unlink(path) == 0);
	}

	openRow(rowOf(GB));
	CHECK_STR(state.top->path, GB);
	CHECK(state.top->entries->count == 4);
	CHECK(state.top->selected == 3);
	CHECK(state.top->start == 0 && state.top->end == 4);
	stopMenu();
}

int main(void) {
	test_reopening_restores_the_selection();
	test_reload_shifting_rows_keeps_another_folder_at_its_start();
	test_rebuilt_root_keeps_another_folder_at_its_start();
	test_restore_fits_a_folder_that_shrank();
	CHECK_DONE();
}
