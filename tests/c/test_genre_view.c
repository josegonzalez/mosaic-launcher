// The genre view plugin: its model on its own, then the plugin against a
// fake host and a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/genre-view/genre_view.c"

struct MosaicEntry {
	char path[512];
	char name[1024];
	int type;
};
struct MosaicList {
	MosaicEntry* items[64];
	int count;
};

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }

static char cfg_keys[8][32];
static char cfg_values[8][256];
static int cfg_count = 0;
static const char* fakeCfgGet(const char* key, const char* fallback) {
	for (int i = 0; i < cfg_count; i++) {
		if (!strcmp(cfg_keys[i], key)) return cfg_values[i];
	}
	return fallback;
}
static void setCfg(const char* key, const char* value) {
	int i = 0;
	while (i < cfg_count && strcmp(cfg_keys[i], key)) i++;
	if (i == cfg_count) snprintf(cfg_keys[cfg_count++], 32, "%s", key);
	snprintf(cfg_values[i], 256, "%s", value);
}
static void fakeCfgSet(const char* key, const char* value) {
	setCfg(key, value);
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
static int fakeSetName(MosaicEntry* e, const char* name) {
	snprintf(e->name, sizeof(e->name), "%s", name);
	return 0;
}

#define CARD TEST_SDCARD_PATH
#define FOLDER CARD "/Genres"
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define GBC ROMS "/Game Boy Color (GBC)"
#define GBA ROMS "/Game Boy Advance (GBA)"
#define MGBA ROMS "/Game Boy Advance (MGBA)"

static const MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
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
	.entry_set_name = fakeSetName,
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

// A list's names, joined with "|", after list_ready, as the host would show it.
static const char* names(MosaicList* list, const char* dir) {
	static char out[2048];
	out[0] = '\0';
	if (!list) return "(null)";
	listReady(list, dir);
	for (int i = 0; i < list->count; i++) {
		if (i) strncat(out, "|", sizeof(out) - strlen(out) - 1);
		strncat(out, list->items[i]->name, sizeof(out) - strlen(out) - 1);
	}
	return out;
}

static const char* listed(const char* dir) {
	MosaicList* list = virtualDir(dir);
	const char* out = names(list, dir);
	freeList(list);
	return out;
}

static int rootHasGenres(void) {
	MosaicList* root = fakeListNew();
	rootEntries(root);
	int found = root->count == 1 && !strcmp(root->items[0]->path, FOLDER) && root->items[0]->type == MOSAIC_ENTRY_DIR
		&& !strcmp(root->items[0]->name, "Genres");
	int any = root->count;
	freeList(root);
	return found ? 1 : any ? -1 : 0;
}

static void restart(void) {
	init(&fake);
}

static const char* key(const char* name) {
	static char out[256];
	GV_key(name, out, sizeof(out));
	return out;
}

static const char* cleaned(const char* name) {
	static char out[256];
	GV_cleanName(name, out, sizeof(out));
	return out;
}

static void testModel(void) {
	CHECK_STR(cleaned("Tetris (World) (Rev 1).gb"), "Tetris");
	CHECK_STR(cleaned("Shoot 'em up (Vertical)"), "Shoot 'em up");
	CHECK_STR(cleaned("Action"), "Action");
	CHECK_STR(cleaned("(Hacks)"), "(Hacks)");

	CHECK_STR(key("Shoot 'em Up"), "shoot em up");
	CHECK_STR(key("  shoot-em-up "), "shoot em up");
	CHECK_STR(key("RPG"), "rpg");
	CHECK_STR(key("Jeux de rôle"), "jeux de r\xc3\xb4le");
	CHECK_STR(key("!!!"), "!!!");

	CHECK(GV_listHas("Hacks, homebrew ,Imgs", "Homebrew"));
	CHECK(GV_listHas("Hacks,Imgs", "imgs"));
	CHECK(!GV_listHas("Hacks,Imgs", "Action"));
	CHECK(!GV_listHas("", "Action"));
	CHECK(!GV_listHas(NULL, "Action"));

	char out[256];
	GV_emuTag("Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, "GB");
	GV_systemName("Game Boy Advance (MGBA)", out, sizeof(out));
	CHECK_STR(out, "Game Boy Advance");
	GV_systemName("Pico-8", out, sizeof(out));
	CHECK_STR(out, "Pico-8");
	CHECK(GV_systemOf("/r", "/r/Game Boy (GB)/Action/Tetris.gb", out, sizeof(out)));
	CHECK_STR(out, "Game Boy (GB)");
	CHECK(!GV_systemOf("/r", "/r/Game Boy (GB)", out, sizeof(out)));
	CHECK(!GV_systemOf("/r", "/g/Action", out, sizeof(out)));

	char parts[GV_MAX_DEPTH][GV_NAME_LEN];
	CHECK(GV_pathNames("/g", "/g", parts, GV_MAX_DEPTH) == 0);
	CHECK(GV_pathNames("/g", "/g/Action/Arcade", parts, GV_MAX_DEPTH) == 2);
	CHECK_STR(parts[0], "Action");
	CHECK_STR(parts[1], "Arcade");
	CHECK(GV_pathNames("/g", "/gx/Action", parts, GV_MAX_DEPTH) == -1);
	CHECK(GV_pathNames("/g", "/g//Action", parts, GV_MAX_DEPTH) == -1);
	CHECK(GV_pathNames("/g", "/g/a/b/c", parts, 2) == -1);

	// map.txt names, later lines winning, aliases hiding entries
	char* text = strdup("Action\tAction Games\nOld\t.Old\nTetris.gb\tTetris DX\nTetris.gb\tTetris!\n");
	GVMap map;
	CHECK(GV_mapParse(&map, text) == 0);
	CHECK(GV_shownName(&map, "Action", out, sizeof(out)));
	CHECK_STR(out, "Action Games");
	CHECK(GV_shownName(&map, "Tetris.gb", out, sizeof(out)));
	CHECK_STR(out, "Tetris!");
	CHECK(!GV_shownName(&map, "Old", out, sizeof(out)));
	CHECK(!GV_shownName(&map, ".hidden", out, sizeof(out)));
	CHECK(!GV_shownName(&map, "map.txt", out, sizeof(out)));
	CHECK(GV_shownName(NULL, "Puzzle (Hard)", out, sizeof(out)));
	CHECK_STR(out, "Puzzle");
	GV_mapFree(&map);

	// merging folders by name across systems
	GVGroups groups;
	GV_groupsInit(&groups);
	CHECK(GV_groupsAdd(&groups, "Shoot 'em up", "/r/A (GBA)/Shoot 'em up", "A (GBA)") == 0);
	CHECK(GV_groupsAdd(&groups, "Action", "/r/A (GBA)/Action", "A (GBA)") == 1);
	CHECK(GV_groupsAdd(&groups, "shoot em up", "/r/A (MGBA)/shoot em up", "A (MGBA)") == 0);
	CHECK(GV_groupsAdd(&groups, "SHOOT-EM-UP", "/r/B (GB)/SHOOT-EM-UP", "B (GB)") == 0);
	CHECK(GV_groupsAdd(&groups, "Hacks", "/r/B (GB)/Hacks", "B (GB)") == 2);
	CHECK(GV_groupsAdd(&groups, "a/b", "/r/B (GB)/x", "B (GB)") == -1);
	CHECK(GV_groupsAdd(&groups, "", "/r/B (GB)/y", "B (GB)") == -1);
	CHECK(groups.count == 3);
	CHECK_STR(groups.items[0].name, "Shoot 'em up"); // the first name wins
	CHECK(groups.items[0].count == 3);
	CHECK(GV_groupSystems(&groups.items[0]) == 2); // GBA and MGBA are one console
	CHECK(GV_groupsFind(&groups, "Shoot Em Up") == 0);
	CHECK(GV_groupsFind(&groups, "Puzzle") == -1);
	GV_groupsSort(&groups);
	CHECK_STR(groups.items[0].name, "Action");
	CHECK_STR(groups.items[1].name, "Hacks");
	CHECK(GV_groupsKeep(&groups, 1, "hacks") == 2);
	CHECK_STR(groups.items[1].name, "Shoot 'em up");
	CHECK(GV_groupsKeep(&groups, 2, NULL) == 1);
	CHECK_STR(groups.items[0].name, "Shoot 'em up");
	GV_groupsFree(&groups);
	CHECK(groups.count == 0);

	// a merged folder's list, by name then path
	GVItems items;
	GV_itemsInit(&items);
	GV_itemsAdd(&items, "/r/B (GB)/x/tetris.gb", "tetris", MOSAIC_ENTRY_ROM);
	GV_itemsAdd(&items, "/r/A (GBA)/x/Tetris.gba", "Tetris", MOSAIC_ENTRY_ROM);
	GV_itemsAdd(&items, "/g/x/Arcade", "Arcade", MOSAIC_ENTRY_DIR);
	GV_itemsSort(&items);
	CHECK_STR(items.items[0].name, "Arcade");
	CHECK_STR(items.items[1].path, "/r/A (GBA)/x/Tetris.gba");
	CHECK_STR(items.items[2].path, "/r/B (GB)/x/tetris.gb");
	GV_itemsFree(&items);

	// telling games apart
	CHECK(GV_tagsMode("off") == GV_TAGS_OFF);
	CHECK(GV_tagsMode("always") == GV_TAGS_ALWAYS);
	CHECK(GV_tagsMode("shared") == GV_TAGS_SHARED);
	CHECK(GV_tagsMode("nonsense") == GV_TAGS_SHARED);
	CHECK(GV_tagsMode(NULL) == GV_TAGS_SHARED);
	const char* game_names[] = { "Arcade", "Tetris", "Tetris", "Kirby", "Kirby" };
	const char* game_systems[] = { NULL, "GB", "GBC", "GB", "GB" };
	int tagged[5];
	CHECK(GV_tags(game_names, game_systems, 5, GV_TAGS_SHARED, tagged) == 2);
	CHECK(!tagged[0] && tagged[1] && tagged[2] && !tagged[3] && !tagged[4]);
	CHECK(GV_tags(game_names, game_systems, 5, GV_TAGS_ALWAYS, tagged) == 4);
	CHECK(!tagged[0] && tagged[1] && tagged[4]);
	CHECK(GV_tags(game_names, game_systems, 5, GV_TAGS_OFF, tagged) == 0);
	CHECK(GV_tags(game_names, game_systems, 0, GV_TAGS_SHARED, tagged) == 0);
}

static void testPlugin(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" GB "/Puzzle' '" GB "/Action/Arcade' '" GB "/Imgs' '" GB "/RPG/Pokemon (USA)'"
		" '" GBC "/puzzle' '" GBC "/Shoot-em-up' '" GBC "/Action/arcade' '" GBA "/Shoot em up' '" MGBA "'"
		" '" GBA "/Plat' '" GBA "/Tool.pak' '" GBA "/.Secret' '" ROMS "/Game Gear (GG)/Puzzle'"
		" '" CARD "/.system/test/paks/Emus/GB.pak' '" CARD "/Emus/test/GBC.pak' '" CARD "/Emus/test/GBA.pak'"
		" '" CARD "/Emus/test/MGBA.pak'") == 0);
	CHECK(mkdir(MGBA "/Shoot 'em up", 0755) == 0); // a quote the shell can't take
	touch(CARD "/.system/test/paks/Emus/GB.pak/launch.sh");
	touch(CARD "/Emus/test/GBC.pak/launch.sh");
	touch(CARD "/Emus/test/GBA.pak/launch.sh");
	touch(CARD "/Emus/test/MGBA.pak/launch.sh");

	touch(GB "/Loose.gb"); // not in a genre
	touch(GB "/Puzzle/Tetris (World).gb");
	touch(GB "/Puzzle/Kwirk.gb");
	touch(GB "/Puzzle/.Hidden.gb");
	touch(GB "/Puzzle/Old.gb.disabled");
	touch(GB "/Action/Arcade/Pac-Man.gb");
	touch(GB "/RPG/Pokemon (USA)/Pokemon (USA).m3u"); // a game folder, not a subfolder
	touch(GBC "/puzzle/Tetris DX.gbc");
	touch(GBC "/puzzle/Tetris.gbc");
	touch(GBC "/Shoot-em-up/R-Type DX.gbc");
	touch(GBC "/Action/arcade/Galaga.gbc");
	touch(GBA "/Shoot em up/Iridion.gba");
	touch(MGBA "/Shoot 'em up/Gradius.gba");
	touch(GBA "/Plat/Mario.gba");
	touch(ROMS "/Game Gear (GG)/Puzzle/Columns.gg"); // no emulator
	writeText(GBA "/map.txt", "Plat\tPlatformer\n");

	restart();
	CHECK(rootHasGenres() == 1);
	CHECK(virtualDir(CARD "/Roms") == NULL);
	CHECK(virtualDir(CARD "/Genresque") == NULL);

	// genres by name, merged across systems, named as the first system names them
	CHECK_STR(listed(FOLDER), "Action|Imgs|Platformer|Puzzle|RPG|Shoot em up");

	MosaicList* list = virtualDir(FOLDER);
	CHECK(list && list->count == 6);
	if (list && list->count == 6) {
		CHECK_STR(list->items[0]->path, FOLDER "/Action");
		CHECK(list->items[0]->type == MOSAIC_ENTRY_DIR);
	}
	freeList(list);

	// games from each system together, with the system shown on shared names
	CHECK_STR(listed(FOLDER "/Puzzle"), "Kwirk|Tetris (GB)|Tetris (GBC)|Tetris DX");
	list = virtualDir(FOLDER "/puzzle");
	CHECK(list && list->count == 4);
	if (list && list->count == 4) {
		CHECK_STR(list->items[1]->path, GB "/Puzzle/Tetris (World).gb");
		CHECK(list->items[1]->type == MOSAIC_ENTRY_ROM);
		CHECK_STR(list->items[2]->path, GBC "/puzzle/Tetris.gbc");
	}
	freeList(list);
	CHECK_STR(listed(FOLDER "/Shoot em up"), "Gradius|Iridion|R-Type DX");
	CHECK_STR(listed(FOLDER "/Platformer"), "Mario");
	CHECK_STR(listed(FOLDER "/Imgs"), "");

	// a game folder is a game; it opens as stock's would
	list = virtualDir(FOLDER "/RPG");
	CHECK(list && list->count == 1);
	if (list && list->count == 1) {
		CHECK_STR(list->items[0]->name, "Pokemon");
		CHECK_STR(list->items[0]->path, GB "/RPG/Pokemon (USA)");
		CHECK(list->items[0]->type == MOSAIC_ENTRY_DIR);
	}
	freeList(list);

	// subfolders merge the same way
	list = virtualDir(FOLDER "/Action");
	CHECK(list && list->count == 1);
	if (list && list->count == 1) {
		CHECK_STR(list->items[0]->name, "Arcade");
		CHECK_STR(list->items[0]->path, FOLDER "/Action/Arcade");
	}
	freeList(list);
	CHECK_STR(listed(FOLDER "/Action/Arcade"), "Galaga|Pac-Man");
	CHECK_STR(listed(FOLDER "/Action/Nothing"), "");
	CHECK_STR(listed(FOLDER "/Nothing"), "");

	// every game tagged, or none
	setCfg("tags", "always");
	restart();
	CHECK_STR(listed(FOLDER "/Puzzle"), "Kwirk (GB)|Tetris (GB)|Tetris (GBC)|Tetris DX (GBC)");
	CHECK_STR(listed(FOLDER "/Action"), "Arcade");
	setCfg("tags", "off");
	restart();
	CHECK_STR(listed(FOLDER "/Puzzle"), "Kwirk|Tetris|Tetris|Tetris DX");
	setCfg("tags", "shared");

	// folders that aren't genres, and genres from too few consoles
	setCfg("skip", "imgs, RPG");
	restart();
	CHECK_STR(listed(FOLDER), "Action|Platformer|Puzzle|Shoot em up");
	CHECK_STR(listed(FOLDER "/RPG"), "");
	setCfg("min", "2");
	restart();
	CHECK_STR(listed(FOLDER), "Action|Puzzle|Shoot em up");
	setCfg("min", "3");
	restart();
	CHECK(rootHasGenres() == 0);
	CHECK_STR(listed(FOLDER), "");
	setCfg("min", "1");
	setCfg("skip", "");

	// a card without genre folders has no Genres folder
	CHECK(system("rm -rf '" ROMS "' && mkdir -p '" GB "'") == 0);
	touch(GB "/Tetris.gb");
	restart();
	CHECK(rootHasGenres() == 0);
	CHECK(system("rm -rf '" CARD "'") == 0);
	CHECK(rootHasGenres() == 0);
}

int main(void) {
	testModel();
	testPlugin();
	CHECK_DONE();
}
