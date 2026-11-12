// The all games plugin: its model on its own, then the plugin against a
// fake host and a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "check.h"
#include "../../plugins/all-games/all_games.c"

struct MosaicEntry {
	char path[512];
	char name[256];
	int type;
};
struct MosaicList {
	MosaicEntry* items[64];
	int count;
};

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define GBC ROMS "/Game Boy Color (GBC)"
#define PS ROMS "/PlayStation (PS)"

static char tags_setting[8] = "";
static const MosaicList* current = NULL;
static const char* current_path = CARD;
static int selected_row = -1;
static const char* newest_recent = NULL;

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "tags") && tags_setting[0]) return tags_setting;
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) { (void)key; (void)value; }
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
static MosaicEntry* fakeListGet(const MosaicList* l, int i) { return i >= 0 && i < l->count ? l->items[i] : NULL; }
static void fakeListPush(MosaicList* l, MosaicEntry* e) { if (l->count < 64) l->items[l->count++] = e; }
static const char* fakeCurrentPath(void) { return current_path; }
static void fakeRedraw(void) {}
static int fakeSetName(MosaicEntry* e, const char* name) {
	snprintf(e->name, sizeof(e->name), "%s", name);
	return 0;
}
static const MosaicList* fakeCurrentList(void) { return current; }
static int fakeSelectedIndex(void) { return selected_row; }
static int fakeSelectIndex(int i) {
	if (!current || i < 0 || i >= current->count) return -1;
	selected_row = i;
	return 0;
}
static int fakeRecentsCount(void) { return newest_recent ? 1 : 0; }
static const char* fakeRecentPath(int i) { return i == 0 ? newest_recent : NULL; }

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
	.current_path = fakeCurrentPath,
	.request_redraw = fakeRedraw,
	.recents_count = fakeRecentsCount,
	.recent_path = fakeRecentPath,
	.entry_set_name = fakeSetName,
	.current_list = fakeCurrentList,
	.selected_index = fakeSelectedIndex,
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

// Sets a folder's modification time, so a change shows (or doesn't)
// whatever the file system's clock resolution.
static void age(const char* path, long seconds) {
	struct timeval times[2] = { { seconds, 0 }, { seconds, 0 } };
	utimes(path, times);
}

///////////////////////////////////////
// the model

static const char* cleaned(const char* file_name) {
	static char out[AG_NAME_LEN];
	AG_cleanName(file_name, out, sizeof(out));
	return out;
}

static void testNames(void) {
	CHECK_STR(cleaned("Tetris (World) (Rev 1).gb"), "Tetris");
	CHECK_STR(cleaned("Legend of Zelda, The [!] (USA).sfc"), "Legend of Zelda, The");
	CHECK_STR(cleaned("Celeste.p8.png"), "Celeste");
	CHECK_STR(cleaned("(Hack) Mario (USA).gb"), "(Hack) Mario");
	CHECK_STR(cleaned("(USA).gb"), "(USA)");
	CHECK_STR(cleaned("Final Fantasy VII (USA)"), "Final Fantasy VII");
	CHECK_STR(cleaned("Dr. Mario.gb"), "Dr. Mario");
	CHECK_STR(cleaned("a.b"), "a.b"); // one letter isn't an extension
	CHECK_STR(cleaned("Doom.wad.doom"), "Doom");
	CHECK_STR(cleaned("Name   .gb"), "Name");

	CHECK(AG_hidden(".Hidden.gb"));
	CHECK(AG_hidden("Old.gb.disabled"));
	CHECK(AG_hidden("map.txt"));
	CHECK(!AG_hidden("Tetris.gb"));

	char tag[32];
	AG_emuTag("Game Boy (GB)", tag, sizeof(tag));
	CHECK_STR(tag, "GB");
	AG_emuTag("Sega (Mega) (MD)", tag, sizeof(tag));
	CHECK_STR(tag, "MD");
	AG_emuTag("GBA", tag, sizeof(tag));
	CHECK_STR(tag, "GBA");
}

static void testMap(void) {
	AGMap map;
	CHECK(AG_mapParse(&map, NULL) == 0);
	CHECK(AG_mapGet(&map, "x") == NULL);
	AG_mapFree(&map);
	CHECK(AG_mapGet(NULL, "x") == NULL);

	char* text = strdup("sf2.zip\tStreet Fighter II\r\nno tab here\nmslug.zip\tMetal Slug\n\nsf2.zip\tSF2\nempty.zip\t\nz.zip\tZed");
	CHECK(AG_mapParse(&map, text) == 0);
	CHECK(map.count == 3);
	CHECK_STR(AG_mapGet(&map, "sf2.zip"), "SF2"); // the later line wins
	CHECK_STR(AG_mapGet(&map, "mslug.zip"), "Metal Slug");
	CHECK_STR(AG_mapGet(&map, "z.zip"), "Zed");
	CHECK(AG_mapGet(&map, "empty.zip") == NULL);
	CHECK(AG_mapGet(&map, "sf2") == NULL);
	CHECK(AG_mapGet(&map, "no tab here") == NULL);
	AG_mapFree(&map);
}

static void testPaths(void) {
	char out[256];
	CHECK(AG_systemOf("/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris.gb", out, sizeof(out)));
	CHECK_STR(out, "Game Boy (GB)");
	CHECK(AG_systemOf("/sd/Roms", "/sd/Roms/GB/Hacks/a.gb", out, sizeof(out)));
	CHECK_STR(out, "GB");
	CHECK(AG_systemOf("/sd/Roms", "/sd/Roms/GB/a.gb", NULL, 0));
	CHECK(!AG_systemOf("/sd/Roms", "/sd/Roms/GB", out, sizeof(out)));
	CHECK(!AG_systemOf("/sd/Roms", "/sd/Roms/GB/", out, sizeof(out)));
	CHECK(!AG_systemOf("/sd/Roms", "/sd/Roms", out, sizeof(out)));
	CHECK(!AG_systemOf("/sd/Roms", "/sd/Romsx/GB/a.gb", out, sizeof(out)));
	CHECK(!AG_systemOf("/sd/Roms", "/sd/Favorites", out, sizeof(out)));

	CHECK(AG_folderArt("/sd/Roms/PS/FF7/FF7.m3u", out, sizeof(out)));
	CHECK_STR(out, "/sd/Roms/PS/.media/FF7.png");
	CHECK(AG_folderArt("/sd/Roms/PS/Crash (USA)/Crash (USA).cue", out, sizeof(out)));
	CHECK_STR(out, "/sd/Roms/PS/.media/Crash (USA).png");
	CHECK(!AG_folderArt("/sd/Roms/PS/Crash.cue", out, sizeof(out)));     // not in its own folder
	CHECK(!AG_folderArt("/sd/Roms/PS/FF7/Disc 1.cue", out, sizeof(out))); // named otherwise
	CHECK(!AG_folderArt("/sd/Roms/PS/FF7/FF7.bin", out, sizeof(out)));
	CHECK(!AG_folderArt("FF7.cue", out, sizeof(out)));
	CHECK(!AG_folderArt("/FF7.cue", out, sizeof(out)));

	CHECK(AG_rootCompare("/sd/Roms", "/sd/Roms", "/sd/Roms/GB/a.gb") < 0);
	CHECK(AG_rootCompare("/sd/Roms", "/sd/Roms/GB/a.gb", "/sd/Favorites") > 0);
	CHECK(AG_rootCompare("/sd/Roms", "/sd/Roms/GB/a.gb", "/sd/Roms/GBC/b.gbc") == 0);
	CHECK(AG_rootCompare("/sd/Roms", "/sd/Favorites", "/sd/Hidden") == 0);
}

static void testIndex(void) {
	AGIndex index;
	AG_indexInit(&index);
	CHECK(AG_indexAdd(&index, "/r/GB/b.gb", "tetris", MOSAIC_ENTRY_ROM) == 0);
	CHECK(AG_indexAdd(&index, "/r/GB/c.gb", "Asteroids", MOSAIC_ENTRY_ROM) == 0);
	CHECK(AG_indexAdd(&index, "/r/GB/a.gb", "Tetris", MOSAIC_ENTRY_ROM) == 0);
	CHECK(AG_indexAdd(&index, "/r/GB/p.pak", "Zork", MOSAIC_ENTRY_PAK) == 0);
	for (int i = 0; i < 200; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/r/X/%03d", i);
		CHECK(AG_indexAdd(&index, path, "Many", MOSAIC_ENTRY_ROM) == 0); // grows
	}
	CHECK(AG_indexAddFolder(&index, "/r/GB", 42) == 0);
	CHECK(index.game_count == 204);
	AG_indexSort(&index);
	CHECK_STR(index.games[0].name, "Asteroids");
	CHECK_STR(index.games[201].path, "/r/GB/a.gb"); // same name ignoring case: by path
	CHECK_STR(index.games[202].path, "/r/GB/b.gb");
	CHECK(index.games[203].type == MOSAIC_ENTRY_PAK);
	CHECK(index.folders[0].mtime == 42);
	AG_indexFree(&index);
	CHECK(index.game_count == 0 && index.folder_count == 0 && index.games == NULL);
}

static void testCollisions(void) {
	const char* names[] = { "Tetris", "Tetris", "Kirby", "Tetris", "Mario", "Mario", "tetris" };
	const char* systems[] = { "GB", "GBC", "GB", "GB", "NES", "NES", "SFC" };
	int tagged[7];
	CHECK(AG_collisions(names, systems, 7, tagged) == 3);
	CHECK(tagged[0] && tagged[1] && tagged[3]);    // all three Tetris, across systems
	CHECK(!tagged[2]);                              // no other Kirby
	CHECK(!tagged[4] && !tagged[5]);                // the same system: stock tells them apart
	CHECK(!tagged[6]);                              // names match exactly

	CHECK(AG_collisions(names, systems, 1, tagged) == 0);
	CHECK(AG_collisions(names, systems, 0, tagged) == 0);
}

///////////////////////////////////////
// the plugin

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" GB "/Hacks/Deep/Deeper' '" GBC "' '" PS "/Final Fantasy VII (USA)'"
		" '" PS "/Crash (USA)' '" PS "/Loose' '" ROMS "/Game Gear (GG)' '" GB "/Tool.pak'"
		" '" CARD "/.system/test/paks/Emus/GB.pak' '" CARD "/Emus/test/GBC.pak' '" CARD "/Emus/test/PS.pak'") == 0);
	touch(CARD "/.system/test/paks/Emus/GB.pak/launch.sh");
	touch(CARD "/Emus/test/GBC.pak/launch.sh");
	touch(CARD "/Emus/test/PS.pak/launch.sh");

	touch(GB "/Tetris (World) (Rev 1).gb");
	touch(GB "/Kirby.gb");
	touch(GB "/.Secret.gb");
	touch(GB "/Old.gb.disabled");
	touch(GB "/Away.gb");
	writeText(GB "/map.txt", "Kirby.gb\tKirby's Dream Land\nAway.gb\t.Away\n");
	touch(GB "/Hacks/Mario Hack.gb");
	touch(GB "/Hacks/Deep/Deep Game.gb");
	touch(GB "/Hacks/Deep/Deeper/Too Deep.gb"); // past the depth that's scanned
	touch(GBC "/Tetris DX.gbc");
	writeText(GBC "/map.txt", "Tetris DX.gbc\tTetris\n"); // the same name as GB's
	touch(PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u");
	touch(PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 1).cue");
	touch(PS "/Crash (USA)/Crash (USA).cue");
	touch(PS "/Crash (USA)/Crash (USA).m3u");
	touch(PS "/Loose/Spyro.cue");
	touch(ROMS "/Game Gear (GG)/Sonic.gg"); // no emulator
	age(GBC, 1000000000);
}

static int indexOf(const MosaicList* list, const char* path) {
	for (int i = 0; i < list->count; i++) {
		if (!strcmp(list->items[i]->path, path)) return i;
	}
	return -1;
}

// The host's root listing: root_replace, other plugins' root entries, then
// the sort by sort_compare (a stable insertion sort) and list_ready.
static MosaicList* listRoot(const MosaicPlugin* p, const char* extra) {
	MosaicList* root = p->root_replace();
	if (!root) return NULL;
	if (extra && p->root_with_entries) fakeListPush(root, fakeEntryNew(extra, MOSAIC_ENTRY_DIR, "Favorites"));
	for (int i = 1; i < root->count; i++) {
		MosaicEntry* e = root->items[i];
		int j = i - 1;
		while (j >= 0 && p->sort_compare(root->items[j], e, CARD) > 0) {
			root->items[j + 1] = root->items[j];
			j--;
		}
		root->items[j + 1] = e;
	}
	p->list_ready(root, CARD);
	return root;
}

static void testPlugin(void) {
	makeCard();
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "all-games");
	CHECK(p->root_with_entries == 1);
	CHECK(p->init(&fake) == 0);

	// Systems and other plugins' folders first, then every game by name
	MosaicList* root = listRoot(p, CARD "/Favorites");
	CHECK(root != NULL);
	if (!root) return;
	const char* want[][2] = {
		{ ROMS, "Systems" },
		{ CARD "/Favorites", "Favorites" },
		{ PS "/Crash (USA)/Crash (USA).cue", "Crash" },
		{ GB "/Hacks/Deep/Deep Game.gb", "Deep Game" },
		{ PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u", "Final Fantasy VII" },
		{ GB "/Kirby.gb", "Kirby's Dream Land" },
		{ GB "/Hacks/Mario Hack.gb", "Mario Hack" },
		{ PS "/Loose/Spyro.cue", "Spyro" },
		{ GB "/Tetris (World) (Rev 1).gb", "Tetris (GB)" },
		{ GBC "/Tetris DX.gbc", "Tetris (GBC)" },
		{ GB "/Tool.pak", "Tool" },
	};
	int count = (int)(sizeof(want) / sizeof(want[0]));
	CHECK(root->count == count);
	for (int i = 0; i < count && i < root->count; i++) {
		CHECK_STR(root->items[i]->path, want[i][0]);
		CHECK_STR(root->items[i]->name, want[i][1]);
	}
	CHECK(root->items[0]->type == MOSAIC_ENTRY_DIR);
	CHECK(root->items[indexOf(root, GB "/Kirby.gb")]->type == MOSAIC_ENTRY_ROM);
	CHECK(root->items[indexOf(root, PS "/Crash (USA)/Crash (USA).cue")]->type == MOSAIC_ENTRY_ROM);
	CHECK(root->items[indexOf(root, GB "/Tool.pak")]->type == MOSAIC_ENTRY_PAK);
	CHECK(indexOf(root, GB "/Hacks/Deep/Deeper/Too Deep.gb") < 0);
	CHECK(indexOf(root, ROMS "/Game Gear (GG)/Sonic.gg") < 0);

	// only the root is ours
	CHECK(p->sort_compare(root->items[2], root->items[0], CARD "/Roms") == 0);

	// a game folder's art is the folder's
	char art[512];
	int crash = indexOf(root, PS "/Crash (USA)/Crash (USA).cue");
	CHECK(p->art_path(root->items[crash], MOSAIC_ART_THUMBNAIL, art, sizeof(art)));
	CHECK_STR(art, PS "/.media/Crash (USA).png");
	CHECK(!p->art_path(root->items[crash], MOSAIC_ART_LIST_BG, art, sizeof(art)));
	CHECK(!p->art_path(root->items[indexOf(root, GB "/Kirby.gb")], MOSAIC_ART_THUMBNAIL, art, sizeof(art)));
	current_path = PS "/Crash (USA)"; // inside the folder, stock's art
	CHECK(!p->art_path(root->items[crash], MOSAIC_ART_THUMBNAIL, art, sizeof(art)));
	current_path = CARD;

	// back from a multi-disc game, its row is selected
	current = root;
	selected_row = 3;
	newest_recent = PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u";
	p->post_return();
	p->tick(0);
	CHECK(selected_row == indexOf(root, newest_recent));
	// only once, and not for other games, which stock already places
	selected_row = 3;
	p->tick(0);
	CHECK(selected_row == 3);
	newest_recent = GB "/Kirby.gb";
	p->post_return();
	p->tick(0);
	CHECK(selected_row == 3);
	// waits for the lists
	current = NULL;
	newest_recent = PS "/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u";
	p->post_return();
	p->tick(0);
	current = root;
	p->tick(0);
	CHECK(selected_row == indexOf(root, newest_recent));
	current = NULL;
	newest_recent = NULL;

	// a listing without changes doesn't scan again; a changed folder does
	touch(GBC "/Unseen.gbc");
	age(GBC, 1000000000);
	MosaicList* again = listRoot(p, NULL);
	CHECK(again && indexOf(again, GBC "/Unseen.gbc") < 0);
	freeList(again);
	age(GBC, 1000000100);
	again = listRoot(p, NULL);
	CHECK(again && indexOf(again, GBC "/Unseen.gbc") >= 0);
	freeList(again);
	// so does a folder that's gone (and this listing has no Favorites)
	CHECK(system("rm -rf '" PS "/Loose'") == 0);
	again = listRoot(p, NULL);
	CHECK(again && again->count == count - 1 && indexOf(again, PS "/Loose/Spyro.cue") < 0);
	freeList(again);
	freeList(root);

	// with tags off, names stay as the system's list shows them
	p->quit();
	snprintf(tags_setting, sizeof(tags_setting), "off");
	CHECK(p->init(&fake) == 0);
	root = listRoot(p, NULL);
	CHECK(root && indexOf(root, GBC "/Tetris DX.gbc") >= 0);
	if (root) {
		CHECK_STR(root->items[indexOf(root, GBC "/Tetris DX.gbc")]->name, "Tetris");
		CHECK_STR(root->items[indexOf(root, GB "/Tetris (World) (Rev 1).gb")]->name, "Tetris");
	}
	freeList(root);
	tags_setting[0] = '\0';
	p->quit();

	// names another plugin already told apart are left alone
	CHECK(p->init(&fake) == 0);
	root = p->root_replace();
	if (root) {
		for (int i = 0; i < root->count; i++) {
			if (!strcmp(root->items[i]->name, "Tetris")) strcat(root->items[i]->name, strstr(root->items[i]->path, ".gbc") ? " (.gbc)" : " (.gb)");
		}
		p->list_ready(root, CARD);
		CHECK_STR(root->items[indexOf(root, GBC "/Tetris DX.gbc")]->name, "Tetris (.gbc)");
		// and other folders are never renamed
		strcpy(root->items[indexOf(root, GBC "/Tetris DX.gbc")]->name, "Kirby's Dream Land");
		p->list_ready(root, GB);
		CHECK_STR(root->items[indexOf(root, GBC "/Tetris DX.gbc")]->name, "Kirby's Dream Land");
	}
	freeList(root);
	p->quit();

	// a card without games keeps stock's root
	CHECK(system("rm -rf '" ROMS "' && mkdir -p '" GB "'") == 0);
	CHECK(p->init(&fake) == 0);
	CHECK(p->root_replace() == NULL);
	CHECK(p->sort_compare(fakeEntryNew(GB "/a.gb", MOSAIC_ENTRY_ROM, "a"), fakeEntryNew(CARD "/Favorites", MOSAIC_ENTRY_DIR, "F"), CARD) == 0);
	p->quit();
	CHECK(system("rm -rf '" ROMS "'") == 0);
	CHECK(p->init(&fake) == 0);
	CHECK(p->root_replace() == NULL);
	p->quit();
}

int main(void) {
	testNames();
	testMap();
	testPaths();
	testIndex();
	testCollisions();
	testPlugin();
	CHECK_DONE();
}
