// The collate subfolders plugin: its model on its own, then the plugin
// against a fake host and a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#define CS_LAST_FILE TEST_SDCARD_PATH ".last"
#include "../../plugins/collate-subfolders/collate_subfolders.c"

struct MosaicEntry {
	char path[512];
	char name[256];
	int type;
};
struct MosaicList {
	MosaicEntry* items[64];
	int count;
};

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	(void)key;
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
static void fakeListPush(MosaicList* l, MosaicEntry* e) {
	if (l->count < 64) l->items[l->count++] = e;
	else free(e);
}

// The folders the user has open, as open_path leaves them.
static char open_paths[8][512];
static int open_count = 0;
static MosaicList* open_list = NULL;
static int selected = -1;
static void freeList(MosaicList* list);
static const char* fakeCurrentPath(void) { return open_count ? open_paths[open_count - 1] : NULL; }
static int fakeOpenPath(const char* path, int type) {
	if (type != MOSAIC_ENTRY_DIR || open_count == 8) return 0;
	snprintf(open_paths[open_count++], 512, "%s", path);
	freeList(open_list);
	open_list = virtualDir(path);
	selected = 0;
	return 1;
}
static const MosaicList* fakeCurrentList(void) { return open_list; }
static int fakeSelectIndex(int index) {
	selected = index;
	return 0;
}

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define MGB ROMS "/Game Boy (MGB)"
#define GBC ROMS "/Game Boy Color (GBC)"
#define BARE ROMS "/GB"
#define BARE_HACKS ROMS "/GB Hacks (GB)"

static const MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/test",
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
	.open_path = fakeOpenPath,
	.current_path = fakeCurrentPath,
	.current_list = fakeCurrentList,
	.select_index = fakeSelectIndex,
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

static void freeList(MosaicList* list) {
	if (!list) return;
	for (int i = 0; i < list->count; i++) free(list->items[i]);
	free(list);
}

// A list's names, joined with "|".
static const char* names(const MosaicList* list) {
	static char out[2048];
	out[0] = '\0';
	if (!list) return "(null)";
	for (int i = 0; i < list->count; i++) {
		if (i) strncat(out, "|", sizeof(out) - strlen(out) - 1);
		strncat(out, list->items[i]->name, sizeof(out) - strlen(out) - 1);
	}
	return out;
}

static const char* listed(const char* dir) {
	MosaicList* list = virtualDir(dir);
	const char* out = names(list);
	freeList(list);
	return out;
}

// The path and type of the entry with this name in a merged folder.
static const MosaicEntry* entryNamed(MosaicList* list, const char* name) {
	for (int i = 0; list && i < list->count; i++) {
		if (!strcmp(list->items[i]->name, name)) return list->items[i];
	}
	return NULL;
}

// Whether filter keeps an entry in a list of dir, a new list each time.
static int kept(const char* dir, const char* path, int type) {
	tick(0);
	MosaicEntry* e = fakeEntryNew(path, type, "");
	int keep = filter(e, dir);
	free(e);
	return keep;
}

static void testModel(void) {
	char out[256];
	CHECK(CS_prefix("Game Boy (GB)", out, sizeof(out)));
	CHECK_STR(out, "Game Boy (");
	CHECK(CS_prefix("Arcade (FBN) (Hacks)", out, sizeof(out)));
	CHECK_STR(out, "Arcade (FBN) (");
	CHECK(!CS_prefix("GB", out, sizeof(out)));

	CHECK(CS_collates("Game Boy (GB)", "Game Boy (MGB)"));
	CHECK(CS_collates("Game Boy (GB)", "Game Boy (GB)"));
	CHECK(!CS_collates("Game Boy (GB)", "Game Boy Color (GBC)"));
	CHECK(!CS_collates("Game Boy (GB)", "Game Boy (GB) Hacks (GB)"));
	CHECK(!CS_collates("GB", "GB"));

	CHECK(CS_consoleDir("/r", "/r/Game Boy (GB)", out, sizeof(out)));
	CHECK_STR(out, "Game Boy (GB)");
	CHECK(!CS_consoleDir("/r", "/r/Game Boy (GB)/Hacks", out, sizeof(out)));
	CHECK(!CS_consoleDir("/r", "/r", out, sizeof(out)));
	CHECK(!CS_consoleDir("/r", "/rx/Game Boy (GB)", out, sizeof(out)));
	CHECK(!CS_consoleDir("/r", NULL, out, sizeof(out)));

	GVGroups groups;
	GV_groupsInit(&groups);
	GV_groupsAdd(&groups, "Hacks", "/r/A (X)/Hacks", "A (X)");
	GV_groupsAdd(&groups, "hacks", "/r/A (Y)/hacks", "A (Y)");
	GV_groupsAdd(&groups, "Homebrew", "/r/A (Y)/Homebrew", "A (Y)");
	CHECK(CS_find(&groups, "/r/A (Y)/hacks") == 0);
	CHECK(CS_find(&groups, "/r/A (Y)/Homebrew") == 1);
	CHECK(CS_find(&groups, "/r/A (Y)/Tetris.gb") == -1);
	CHECK(CS_shown(&groups, "/r/A (X)/Hacks"));
	CHECK(!CS_shown(&groups, "/r/A (Y)/hacks"));
	CHECK(CS_shown(&groups, "/r/A (Y)/Homebrew"));
	CHECK(CS_shown(&groups, "/r/A (Y)/Tetris.gb"));
	GV_groupsFree(&groups);
}

static void makeCard(void) {
	mkdir(CARD, 0755);
	mkdir(ROMS, 0755);
	const char* dirs[] = {
		GB, GB "/Hacks", GB "/Hacks/Sub", GB "/Homebrew", GB "/Disc", GB "/Tool.pak", GB "/.hidden",
		MGB, MGB "/hacks", MGB "/hacks/Sub", MGB "/hacks/Only", MGB "/hacks/Extra.pak", MGB "/Disc", MGB "/Bonus",
		GBC, GBC "/Hacks",
		BARE, BARE "/Hacks", BARE_HACKS, BARE_HACKS "/Hacks",
	};
	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) mkdir(dirs[i], 0755);
	const char* files[] = {
		GB "/Tetris.gb", GB "/Hacks/Alpha.gb", GB "/Hacks/Tetris.gb", GB "/Hacks/Sub/x.gb", GB "/Disc/Disc.m3u", GB "/Homebrew/h.gb",
		MGB "/Tetris.gb", MGB "/hacks/Bravo.gb", MGB "/hacks/Tetris.gb", MGB "/hacks/Sub/y.gb", MGB "/hacks/Only/z.gb",
		MGB "/Disc/Disc.m3u", MGB "/Bonus/Delta.gb",
		GBC "/Hacks/c.gbc", BARE "/Hacks/b.gb", BARE_HACKS "/Hacks/d.gb",
	};
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) touch(files[i]);
	// each folder names its own games below the system's list
	writeText(MGB "/hacks/map.txt", "Bravo.gb\tBravo!\n");
}

static void testSystemList(void) {
	// the first collated folder's keeps its place, from either one's list
	CHECK(kept(GB, GB "/Hacks", MOSAIC_ENTRY_DIR));
	CHECK(!kept(GB, MGB "/hacks", MOSAIC_ENTRY_DIR));
	CHECK(kept(MGB, GB "/Hacks", MOSAIC_ENTRY_DIR));
	CHECK(!kept(MGB, MGB "/hacks", MOSAIC_ENTRY_DIR));
	// folders only one has, games, paks and game folders stay
	CHECK(kept(GB, GB "/Homebrew", MOSAIC_ENTRY_DIR));
	CHECK(kept(GB, MGB "/Bonus", MOSAIC_ENTRY_DIR));
	CHECK(kept(GB, GB "/Tetris.gb", MOSAIC_ENTRY_ROM));
	CHECK(kept(GB, MGB "/Tetris.gb", MOSAIC_ENTRY_ROM));
	CHECK(kept(GB, GB "/Disc", MOSAIC_ENTRY_DIR));
	CHECK(kept(GB, MGB "/Disc", MOSAIC_ENTRY_DIR));
	CHECK(kept(GB, GB "/Tool.pak", MOSAIC_ENTRY_PAK));
	// a system that isn't collated is stock's
	CHECK(kept(GBC, GBC "/Hacks", MOSAIC_ENTRY_DIR));
	CHECK(kept(BARE, BARE "/Hacks", MOSAIC_ENTRY_DIR));
	CHECK(kept(BARE, BARE_HACKS "/Hacks", MOSAIC_ENTRY_DIR));
	// only a system's own list is filtered
	CHECK(kept(GB "/Hacks", MGB "/hacks/Sub", MOSAIC_ENTRY_DIR));
	CHECK(kept(CARD, MGB "/hacks", MOSAIC_ENTRY_DIR));
}

static void testMergedFolders(void) {
	CHECK_STR(listed(GB "/Hacks"), "Alpha|Bravo!|Extra|Only|Sub|Tetris|Tetris");
	MosaicList* list = virtualDir(GB "/Hacks");
	const MosaicEntry* sub = entryNamed(list, "Sub");
	const MosaicEntry* only = entryNamed(list, "Only");
	const MosaicEntry* bravo = entryNamed(list, "Bravo!");
	CHECK(sub && !strcmp(sub->path, GB "/Hacks/Sub") && sub->type == MOSAIC_ENTRY_DIR);
	CHECK(only && !strcmp(only->path, MGB "/hacks/Only") && only->type == MOSAIC_ENTRY_DIR);
	CHECK(bravo && !strcmp(bravo->path, MGB "/hacks/Bravo.gb") && bravo->type == MOSAIC_ENTRY_ROM);
	// the same game from both, GB's first
	CHECK(list && list->count == 7 && !strcmp(list->items[5]->path, GB "/Hacks/Tetris.gb")
		&& !strcmp(list->items[6]->path, MGB "/hacks/Tetris.gb"));
	const MosaicEntry* extra = entryNamed(list, "Extra");
	CHECK(extra && extra->type == MOSAIC_ENTRY_PAK);
	freeList(list);

	// opened from the folder it hid too, and a level down
	CHECK_STR(listed(MGB "/hacks"), "Alpha|Bravo!|Extra|Only|Sub|Tetris|Tetris");
	CHECK_STR(listed(GB "/Hacks/Sub"), "x|y");
	CHECK_STR(listed(MGB "/hacks/Sub"), "x|y");

	// everything else is stock's
	CHECK_STR(listed(MGB "/hacks/Only"), "(null)");
	CHECK_STR(listed(GB "/Homebrew"), "(null)");
	CHECK_STR(listed(GB "/Disc"), "(null)");
	CHECK_STR(listed(GB), "(null)");
	CHECK_STR(listed(ROMS), "(null)");
	CHECK_STR(listed(CARD), "(null)");
	CHECK_STR(listed(GBC "/Hacks"), "(null)");
	CHECK_STR(listed(BARE "/Hacks"), "(null)");
	CHECK_STR(listed(GB "/Missing"), "(null)");

}

// The system's list names every collated folder by the map.txt of the
// folder it was opened from, as stock does.
static void testSystemMap(void) {
	writeText(GB "/map.txt", "Bonus\tHacks\n");
	CHECK(!kept(GB, MGB "/Bonus", MOSAIC_ENTRY_DIR));
	CHECK(kept(GB, GB "/Hacks", MOSAIC_ENTRY_DIR));
	CHECK_STR(listed(GB "/Hacks"), "Alpha|Bravo!|Delta|Extra|Only|Sub|Tetris|Tetris");

	// opened from MGB, its map has no alias, so Bonus is its own folder
	CHECK(kept(MGB, MGB "/Bonus", MOSAIC_ENTRY_DIR));
	CHECK_STR(listed(GB "/Hacks"), "Alpha|Bravo!|Extra|Only|Sub|Tetris|Tetris");
	CHECK_STR(listed(MGB "/Bonus"), "(null)");
	remove(GB "/map.txt");
}

// Folders that show the same name match ignoring case, spacing and
// punctuation, but not words run together.
static void testMatching(void) {
	mkdir(MGB "/home-brew", 0755);
	touch(MGB "/home-brew/m.gb");
	CHECK(kept(GB, GB "/Homebrew", MOSAIC_ENTRY_DIR));
	CHECK(kept(GB, MGB "/home-brew", MOSAIC_ENTRY_DIR));

	mkdir(MGB "/HOMEBREW!", 0755);
	touch(MGB "/HOMEBREW!/n.gb");
	mkdir(MGB "/Homebrew (Old)", 0755);
	touch(MGB "/Homebrew (Old)/o.gb");
	CHECK(!kept(GB, MGB "/HOMEBREW!", MOSAIC_ENTRY_DIR));
	CHECK(!kept(GB, MGB "/Homebrew (Old)", MOSAIC_ENTRY_DIR));
	CHECK_STR(listed(GB "/Homebrew"), "h|n|o");
	CHECK_STR(listed(MGB "/home-brew"), "(null)");
}

// Back from a game, with stock having opened what it could again.
static void comeBack(const char* game, const char* stock_opened) {
	MosaicLaunch launch = { game, MOSAIC_ENTRY_ROM };
	CHECK(preLaunch(&launch) == MOSAIC_LAUNCH_CONTINUE);
	init(&fake);
	open_count = 0;
	freeList(open_list);
	open_list = NULL;
	selected = -1;
	fakeOpenPath(stock_opened, MOSAIC_ENTRY_DIR);
	postReturn();
	tick(0);
}

static void testComingBack(void) {
	// a game in a folder filter hid: its merged folders open, and it's selected
	comeBack(MGB "/hacks/Sub/y.gb", MGB);
	CHECK(open_count == 3);
	CHECK_STR(open_paths[1], GB "/Hacks");
	CHECK_STR(open_paths[2], GB "/Hacks/Sub");
	CHECK(selected == 1);
	CHECK(access(CS_LAST_FILE, F_OK) != 0);

	comeBack(MGB "/hacks/Bravo.gb", GB);
	CHECK(open_count == 2);
	CHECK_STR(open_paths[1], GB "/Hacks");
	CHECK(open_list && selected >= 0 && !strcmp(open_list->items[selected]->path, MGB "/hacks/Bravo.gb"));

	// a folder only MGB has, below a merged one
	comeBack(MGB "/hacks/Only/z.gb", MGB);
	CHECK(open_count == 3);
	CHECK_STR(open_paths[2], MGB "/hacks/Only");

	// stock got there itself, or the game isn't in a merged folder
	comeBack(GB "/Hacks/Alpha.gb", GB "/Hacks");
	CHECK(open_count == 1);
	comeBack(MGB "/Tetris.gb", MGB);
	CHECK(open_count == 1);
	comeBack(GBC "/Hacks/c.gbc", GBC);
	CHECK(open_count == 1);

	// a launch that isn't a game in Roms forgets the last one
	MosaicLaunch game = { MGB "/hacks/Bravo.gb", MOSAIC_ENTRY_ROM };
	preLaunch(&game);
	MosaicLaunch pak = { CARD "/Tools/test/Clock.pak", MOSAIC_ENTRY_PAK };
	preLaunch(&pak);
	CHECK(access(CS_LAST_FILE, F_OK) != 0);
	freeList(open_list);
	open_list = NULL;
}

int main(void) {
	testModel();
	CHECK(system("rm -rf '" CARD "'") == 0);
	makeCard();
	init(&fake);
	testSystemList();
	testMergedFolders();
	testSystemMap();
	testMatching();
	testComingBack();
	quit();
	CHECK_DONE();
}
