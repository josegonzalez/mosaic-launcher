// The title disambiguation plugin: splitting paths into parts, picking the
// parts that tell a group apart and renaming a list, then the plugin's
// list_ready against a fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/title-disambiguation/title_disambiguation.c"

#define CARD "/mnt/SDCARD"
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define GBC ROMS "/Game Boy Color (GBC)"
#define NES ROMS "/Nintendo (FC)"

struct NUIEntry {
	char path[512];
	char name[256];
	int type;
};

struct NUIList {
	NUIEntry* items[16];
	int count;
};

static const char* setting = NULL; // same_system, or NULL for the default
static int set_names = 0;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (setting && !strcmp(key, "same_system")) return setting;
	return fallback;
}
static const char* fakeEntryPath(const NUIEntry* e) { return e->path; }
static const char* fakeEntryName(const NUIEntry* e) { return e->name; }
static int fakeEntryType(const NUIEntry* e) { return e->type; }
static int fakeListCount(const NUIList* l) { return l->count; }
static NUIEntry* fakeListGet(const NUIList* l, int i) { return i >= 0 && i < l->count ? l->items[i] : NULL; }
static int fakeSetName(NUIEntry* e, const char* name) {
	snprintf(e->name, sizeof(e->name), "%s", name);
	set_names++;
	return 0;
}

static const NUIHost fake = {
	.size = sizeof(NUIHost),
	.api_version = NUI_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/test",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.entry_type = fakeEntryType,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.entry_set_name = fakeSetName,
};

static void testParts(void) {
	TitleParts p;
	Titles_parts(ROMS, GB "/Puzzle/Tetris (USA) (Rev 1).gb", &p);
	CHECK_STR(p.system, "GB");
	CHECK_STR(p.folder, "Puzzle");
	CHECK_STR(p.tags, "(USA) (Rev 1)");
	CHECK_STR(p.ext, ".gb");

	Titles_parts(ROMS, GBC "/Tetris DX [!].gbc", &p);
	CHECK_STR(p.system, "GBC");
	CHECK_STR(p.folder, "Game Boy Color (GBC)");
	CHECK_STR(p.tags, "[!]");
	CHECK_STR(p.ext, ".gbc");

	// several extensions, as the launcher drops them
	Titles_parts(ROMS, ROMS "/Pico-8 (P8)/Celeste.p8.png", &p);
	CHECK_STR(p.system, "P8");
	CHECK_STR(p.tags, "");
	CHECK_STR(p.ext, ".p8.png");

	// a console folder without a tag uses its whole name
	Titles_parts(ROMS, ROMS "/Arcade/pacman.zip", &p);
	CHECK_STR(p.system, "Arcade");

	// outside Roms there's no system
	Titles_parts(ROMS, CARD "/Tools/tg5040/Files.pak", &p);
	CHECK_STR(p.system, "");
	CHECK_STR(p.folder, "tg5040");
	CHECK_STR(p.ext, ".pak");

	// a folder, eg. a multi-disc game
	Titles_parts(ROMS, ROMS "/Sony PlayStation (PS)/Final Fantasy VII (USA)/", &p);
	CHECK_STR(p.system, "PS");
	CHECK_STR(p.folder, "Sony PlayStation (PS)");
	CHECK_STR(p.tags, "(USA)");
	CHECK_STR(p.ext, "");

	// a name starting with ( keeps it, as the launcher does
	Titles_parts(ROMS, GB "/(Hack) Tetris.gb", &p);
	CHECK_STR(p.tags, "");

	// a dot without an extension's length stays in the name
	Titles_parts(ROMS, GB "/Dr. Mario.gb", &p);
	CHECK_STR(p.ext, ".gb");
	Titles_parts(ROMS, GB "/St. Elmo's Fire", &p);
	CHECK_STR(p.ext, "");

	Titles_parts(ROMS, NULL, &p);
	CHECK_STR(p.system, "");
	Titles_parts(NULL, GB "/Tetris.gb", &p);
	CHECK_STR(p.system, "");
}

static void testLabel(void) {
	TitleParts p;
	Titles_parts(ROMS, GB "/Puzzle/Tetris (USA).gb", &p);
	char out[256];
	Titles_label("Tetris", &p, TITLES_SYSTEM, out, sizeof(out));
	CHECK_STR(out, "Tetris (GB)");
	Titles_label("Tetris", &p, TITLES_TAGS | TITLES_EXT, out, sizeof(out));
	CHECK_STR(out, "Tetris (USA).gb");
	Titles_label("Tetris", &p, TITLES_SYSTEM | TITLES_TAGS | TITLES_EXT | TITLES_FOLDER, out, sizeof(out));
	CHECK_STR(out, "Tetris (GB) (Puzzle) (USA).gb");
	Titles_label("Tetris", &p, 0, out, sizeof(out));
	CHECK_STR(out, "Tetris");
	Titles_label("Tetris", &p, TITLES_FILE | TITLES_TAGS | TITLES_EXT, out, sizeof(out));
	CHECK_STR(out, "Tetris (Tetris (USA).gb)");

	// cut short at a character boundary: "é" is two bytes
	Titles_label("Pokémon", &p, TITLES_SYSTEM, out, 6);
	CHECK_STR(out, "Pok\xc3\xa9");
	Titles_label("Pokémon", &p, TITLES_SYSTEM, out, 5);
	CHECK_STR(out, "Pok");
}

static int pick(const char* const* paths, int n) {
	TitleParts parts[8];
	for (int i = 0; i < n; i++) Titles_parts(ROMS, paths[i], &parts[i]);
	return Titles_pick("Tetris", parts, n);
}

static void testPick(void) {
	// different systems: the system alone, even when more differs
	const char* systems[] = { GB "/Tetris (USA).gb", GBC "/Tetris (Europe).gbc" };
	CHECK(pick(systems, 2) == TITLES_SYSTEM);

	// one system: tags, then the extension
	const char* regions[] = { GB "/Tetris (USA).gb", GB "/Tetris (Europe).gb" };
	CHECK(pick(regions, 2) == TITLES_TAGS);
	const char* exts[] = { GB "/Tetris.gb", GB "/Tetris.gbc" };
	CHECK(pick(exts, 2) == TITLES_EXT);

	// the folder when nothing else differs
	const char* folders[] = { GB "/Tetris.gb", GB "/Hacks/Tetris.gb" };
	CHECK(pick(folders, 2) == TITLES_FOLDER);

	// two parts when one isn't enough
	const char* both[] = { GB "/Tetris (USA).gb", GB "/Tetris (Europe).gb", GBC "/Tetris (USA).gbc" };
	CHECK(pick(both, 3) == (TITLES_SYSTEM | TITLES_TAGS));

	// nothing differs
	const char* same[] = { GB "/Tetris.gb", GB "/Tetris.gb" };
	CHECK(pick(same, 2) == 0);
	CHECK(pick(same, 1) == 0);

	// the file name, when only it differs
	const char* files[] = { GB "/a.gb", GB "/b.gb" };
	CHECK(pick(files, 2) == TITLES_FILE);
}

// Runs Titles_list over names and paths and checks the new names, "" for
// one that keeps its name.
static void checkList(int same_system, int n, const char* const* names, const char* const* paths, const char* const* want) {
	char* labels[8];
	int renamed = Titles_list(ROMS, same_system, n, names, paths, labels);
	int expected = 0;
	for (int i = 0; i < n; i++) {
		if (want[i][0]) expected++;
		CHECK_STR(labels[i] ? labels[i] : "", want[i]);
		free(labels[i]);
	}
	CHECK(renamed == expected);
}

static void testList(void) {
	// Recently Played: the collisions aren't neighbours
	const char* names[] = { "Tetris", "Zelda", "Tetris", "Mario" };
	const char* paths[] = { GB "/Tetris.gb", GB "/Zelda.gb", GBC "/Tetris.gbc", NES "/Mario.nes" };
	const char* want[] = { "Tetris (GB)", "", "Tetris (GBC)", "" };
	checkList(1, 4, names, paths, want);
	checkList(0, 4, names, paths, want);

	// one system folder: renamed only with same_system on
	const char* gb_names[] = { "Tetris", "Tetris", "Zelda" };
	const char* gb_paths[] = { GB "/Tetris (Europe).gb", GB "/Tetris (USA).gb", GB "/Zelda.gb" };
	const char* gb_want[] = { "Tetris (Europe)", "Tetris (USA)", "" };
	const char* gb_none[] = { "", "", "" };
	checkList(1, 3, gb_names, gb_paths, gb_want);
	checkList(0, 3, gb_names, gb_paths, gb_none);

	// a group where one entry has nothing to add keeps its name
	const char* pak_names[] = { "Files", "Files" };
	const char* pak_paths[] = { CARD "/Tools/tg5040/Files.pak", ROMS "/Ports (PORTS)/Files.sh" };
	const char* pak_want[] = { "", "Files (PORTS)" };
	checkList(1, 2, pak_names, pak_paths, pak_want);

	// map.txt names that collide across different file names
	const char* map_names[] = { "Best Game", "Best Game", "Best Game" };
	const char* map_paths[] = { GB "/a.gb", GB "/b.gb", GBC "/c.gbc" };
	const char* map_want[] = { "Best Game (a.gb)", "Best Game (b.gb)", "Best Game (c.gbc)" };
	checkList(1, 3, map_names, map_paths, map_want);

	// when nothing tells two apart, every part that differs is shown
	const char* deep_names[] = { "Tetris", "Tetris", "Tetris" };
	const char* deep_paths[] = { GB "/A/X/Tetris.gb", GB "/B/X/Tetris.gb", GB "/Tetris.gb" };
	const char* deep_want[] = { "Tetris (X)", "Tetris (X)", "Tetris (Game Boy (GB))" };
	checkList(1, 3, deep_names, deep_paths, deep_want);
	char* labels[3];

	// nothing to do
	CHECK(Titles_list(ROMS, 1, 0, NULL, NULL, labels) == 0);
	const char* one[] = { "Tetris" };
	const char* one_path[] = { GB "/Tetris.gb" };
	CHECK(Titles_list(ROMS, 1, 1, one, one_path, labels) == 0 && labels[0] == NULL);
}

static NUIEntry entries[8];
static NUIList list;

static void addEntry(const char* path, const char* name) {
	NUIEntry* e = &entries[list.count];
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name);
	e->type = NUI_ENTRY_ROM;
	list.items[list.count++] = e;
}

static void testPlugin(void) {
	setting = NULL;
	CHECK(nextui_plugin_get(&fake)->init(&fake) == 0);
	CHECK(plugin.list_ready == listReady);

	list.count = 0;
	addEntry(GB "/Tetris.gb", "Tetris");
	addEntry(NES "/Mario.nes", "Mario");
	addEntry(GBC "/Tetris.gbc", "Tetris");
	set_names = 0;
	listReady(&list, CARD "/.userdata/test/.minui/recent.txt");
	CHECK(set_names == 2);
	CHECK_STR(entries[0].name, "Tetris (GB)");
	CHECK_STR(entries[1].name, "Mario");
	CHECK_STR(entries[2].name, "Tetris (GBC)");

	// a second pass over the renamed list changes nothing
	set_names = 0;
	listReady(&list, CARD "/.userdata/test/.minui/recent.txt");
	CHECK(set_names == 0);

	// with same_system off, one system's games are left to the launcher
	setting = "off";
	CHECK(plugin.init(&fake) == 0);
	list.count = 0;
	addEntry(GB "/Tetris.gb", "Tetris");
	addEntry(GB "/Tetris.gbc", "Tetris");
	set_names = 0;
	listReady(&list, GB);
	CHECK(set_names == 0);
	CHECK_STR(entries[0].name, "Tetris");

	setting = "on";
	CHECK(plugin.init(&fake) == 0);
	listReady(&list, GB);
	CHECK_STR(entries[0].name, "Tetris.gb");
	CHECK_STR(entries[1].name, "Tetris.gbc");

	// an older launcher without entry_set_name skips the plugin
	NUIHost old = fake;
	old.entry_set_name = NULL;
	CHECK(plugin.init(&old) != 0);
}

int main(void) {
	testParts();
	testLabel();
	testPick();
	testList();
	testPlugin();
	CHECK_DONE();
}
