// The collections-plus plugin and its model against a fake host, on a
// fixture card of its own.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/collections-plus/collections_plus.c"

struct MosaicEntry {
	char path[512];
	char name[128];
	int type;
};
struct MosaicList {
	MosaicEntry* items[64];
	int count;
};

#define CARD TEST_SDCARD_PATH
#define COLL CARD "/Collections"

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }

// settings by key, NULL for the default
static const char* settings[][2] = {
	{ "promote", NULL }, { "order", NULL }, { "hide_folder", NULL }, { "file_order", NULL },
	{ "own_map", NULL }, { "art", NULL }, { "hide_games", NULL },
};
#define SETTING_COUNT (int)(sizeof(settings) / sizeof(settings[0]))

static void set(const char* key, const char* value) {
	for (int i = 0; i < SETTING_COUNT; i++) {
		if (!strcmp(settings[i][0], key)) settings[i][1] = value;
	}
}
static void resetSettings(void) {
	for (int i = 0; i < SETTING_COUNT; i++) settings[i][1] = NULL;
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	for (int i = 0; i < SETTING_COUNT; i++) {
		if (!strcmp(settings[i][0], key) && settings[i][1]) return settings[i][1];
	}
	return fallback;
}

static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = calloc(1, sizeof(MosaicEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	if (name) snprintf(e->name, sizeof(e->name), "%s", name);
	else {
		// stock's name, near enough: the file name without its extension
		const char* slash = strrchr(path, '/');
		snprintf(e->name, sizeof(e->name), "%s", slash ? slash + 1 : path);
		char* dot = strrchr(e->name, '.');
		if (dot && dot != e->name) *dot = '\0';
	}
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

static char hidden_tile[64];
static int fakeHideTile(const char* name, int hidden) {
	if (hidden) snprintf(hidden_tile, sizeof(hidden_tile), "%s", name);
	return 0;
}

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
	.hide_quick_tile = fakeHideTile,
};

static void writeText(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static const MosaicPlugin* start(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	if (p->quit) p->quit();
	CHECK(p->init(&fake) == 0);
	return p;
}

static void freeList(MosaicList* list) {
	for (int i = 0; i < list->count; i++) free(list->items[i]);
	free(list);
}

// The list's names, comma-separated.
static void names(const MosaicList* list, char* out, size_t size) {
	out[0] = '\0';
	for (int i = 0; i < list->count; i++) {
		size_t used = strlen(out);
		snprintf(out + used, size - used, "%s%s", i ? "," : "", list->items[i]->name);
	}
}

// The root's names after the plugin's root entries, with stock's in front.
static void rootNames(const MosaicPlugin* p, const char* stock_path, char* out, size_t size) {
	MosaicList* root = fakeListNew();
	if (stock_path) fakeListPush(root, fakeEntryNew(stock_path, MOSAIC_ENTRY_DIR, NULL));
	p->root_entries(root);
	names(root, out, size);
	freeList(root);
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

// Lists entries the way the launcher does: filter, display_name, sort,
// list_ready. Writes the names left, in order.
static void listing(const MosaicPlugin* p, const char* dir, MosaicEntry** items, int n, char* out, size_t size) {
	MosaicList* list = fakeListNew();
	for (int i = 0; i < n; i++) {
		if (p->filter(items[i], dir)) fakeListPush(list, items[i]);
		else free(items[i]);
	}
	for (int i = 0; i < list->count; i++) {
		char name[128] = "";
		if (p->display_name(list->items[i], name, sizeof(name))) snprintf(list->items[i]->name, sizeof(list->items[i]->name), "%s", name);
	}
	sortBy(p, list->items, list->count, dir);
	p->list_ready(list, dir);
	names(list, out, size);
	freeList(list);
}

static void testModel(void) {
	CPNames n;
	CP_parseNames(" RPGs ,Shooters,,rpgs, ", &n);
	CHECK(n.count == 2 && !n.all);
	CHECK_STR(n.items[0], "RPGs");
	CHECK_STR(n.items[1], "Shooters");
	CP_parseNames(" * ", &n);
	CHECK(n.all && n.count == 0);
	CP_parseNames("*, A", &n);
	CHECK(!n.all && n.count == 2);
	CP_parseNames("", &n);
	CHECK(!n.all && n.count == 0);
	CP_parseNames(NULL, &n);
	CHECK(n.count == 0);

	CP_parseNames("RPGs, Bee", &n);
	CHECK(CP_rank(&n, "rpgs", NULL) == 0);
	CHECK(CP_rank(&n, "B", "bee") == 1);
	CHECK(CP_rank(&n, "Other", NULL) == -1);
	CHECK(CP_compareRanks(0, 1) < 0);
	CHECK(CP_compareRanks(2, 1) > 0);
	CHECK(CP_compareRanks(1, -1) < 0);
	CHECK(CP_compareRanks(-1, 0) > 0);
	CHECK(CP_compareRanks(-1, -1) == 0);

	char tag[32];
	CP_systemTag("Game Boy (GB)", tag, sizeof(tag));
	CHECK_STR(tag, "GB");
	CP_systemTag("Sega (Japan) (MD)", tag, sizeof(tag));
	CHECK_STR(tag, "MD");
	CP_systemTag("Homebrew", tag, sizeof(tag));
	CHECK_STR(tag, "");
	CP_systemTag("Odd ()", tag, sizeof(tag));
	CHECK_STR(tag, "");
	CP_parseNames("game boy, RPGs, sfc, Game Boy Advance (GBA)", &n);
	CHECK(CP_rankSystem(&n, "Game Boy (GB)", "Game Boy") == 0);
	CHECK(CP_rankSystem(&n, "Super Nintendo (SFC)", "Super Nintendo") == 2);
	CHECK(CP_rankSystem(&n, "Game Boy Advance (GBA)", "Game Boy Advance") == 3);
	CHECK(CP_rankSystem(&n, "Nintendo (FC)", NULL) == -1);
	CHECK(CP_rankSystem(&n, "RPGs", NULL) == 1); // a folder name like any other

	// rows: GB, GBA, NES, SNES, Collections, RPGs, Shooters
	int kinds[] = { CP_ROW_SYSTEM, CP_ROW_SYSTEM, CP_ROW_SYSTEM, CP_ROW_SYSTEM, CP_ROW_OTHER, CP_ROW_COLLECTION,
		CP_ROW_COLLECTION };
	int places[7];
	// "Game Boy, RPGs, SNES": RPGs right after Game Boy
	int ranks[] = { 0, -1, -1, 2, -1, 1, -1 };
	CP_placeRows(ranks, kinds, 7, places);
	CHECK(places[0] == 0 && places[5] == 1 && places[1] == 2 && places[2] == 3 && places[3] == 4);
	CHECK(places[4] == 5 && places[6] == 6);
	// "RPGs, SNES, Shooters, Game Boy": systems swap places, RPGs before
	// the first, Shooters after SNES
	int swapped[] = { 3, -1, -1, 1, -1, 0, 2 };
	CP_placeRows(swapped, kinds, 7, places);
	CHECK(places[5] == 0 && places[3] == 1 && places[6] == 2 && places[1] == 3 && places[2] == 4);
	CHECK(places[0] == 5 && places[4] == 6);
	// equal ranks: the collection after the system
	int same[] = { -1, -1, 0, -1, -1, 0, -1 };
	CP_placeRows(same, kinds, 7, places);
	CHECK(places[2] == 2 && places[5] == 3 && places[3] == 4 && places[4] == 5 && places[6] == 6);
	// no system named, nothing moves
	int none[] = { -1, -1, -1, -1, -1, 1, 0 };
	CP_placeRows(none, kinds, 7, places);
	for (int i = 0; i < 7; i++) CHECK(places[i] == i);
	CP_placeRows(none, kinds, 0, places);

	CHECK(CP_isCollection("/c/Collections", "/c/Collections/A.txt"));
	CHECK(CP_isCollection("/c/Collections", "/c/Collections/Sub/B.txt"));
	CHECK(!CP_isCollection("/c/Collections", "/c/Collections"));
	CHECK(!CP_isCollection("/c/Collections", "/c/Collections/Sub"));
	CHECK(!CP_isCollection("/c/Collections", "/c/CollectionsX/A.txt"));
	char name[64];
	CHECK(CP_topCollection("/c/Collections", "/c/Collections/A (x).txt", name, sizeof(name)));
	CHECK_STR(name, "A (x)");
	CHECK(!CP_topCollection("/c/Collections", "/c/Collections/Sub/B.txt", name, sizeof(name)));
	CHECK(!CP_topCollection("/c/Collections", "/c/Collections/.txt", name, sizeof(name)));

	char paths[2][CP_PATH_LEN];
	CHECK(CP_mapPaths("/c/Collections", "/c/Collections/A.txt", paths, 2) == 1);
	CHECK_STR(paths[0], "/c/Collections/.media/A.map.txt");
	CHECK(CP_mapPaths("/c/Collections", "/c/Collections/Sub/B.txt", paths, 2) == 2);
	CHECK_STR(paths[0], "/c/Collections/Sub/.media/B.map.txt");
	CHECK_STR(paths[1], "/c/Collections/Sub/map.txt");
	CHECK(CP_mapPaths("/c/Collections", "/c/Roms/GB", paths, 2) == 0);
	CHECK(CP_artPaths("/c/Collections/A.txt", paths, 2) == 2);
	CHECK_STR(paths[0], "/c/Collections/.media/A.png");
	CHECK_STR(paths[1], "/c/Collections/.media/A.jpg");
	CHECK(CP_artPaths("/c/Collections/A", paths, 2) == 0);
	CHECK(CP_iconPaths("/c/Collections", "/c/Collections/A.txt", paths, 2) == 1);
	CHECK_STR(paths[0], "/c/Collections/.icons/A.png");
	CHECK(CP_iconPaths("/c/Collections", "/c/Collections/Sub/B.txt", paths, 2) == 2);
	CHECK_STR(paths[0], "/c/Collections/Sub/.icons/B.png");
	CHECK_STR(paths[1], "/c/Collections/.icons/B.png");
	CHECK(CP_iconPaths("/c/Collections", "/c/Roms/GB.txt", paths, 2) == 0);

	char file[256];
	snprintf(file, sizeof(file), "%s/model-map.txt", CARD);
	writeText(file, "Tetris.gb\tTetris DX\r\n\nnotab\nTetris.gb\tSecond\nDr. Mario.gb\tDoctor\n");
	CPMap map;
	CP_mapInit(&map);
	CHECK(CP_mapLoad(&map, file));
	CHECK(map.count == 2);
	CHECK_STR(CP_mapGet(&map, "Tetris.gb"), "Tetris DX");
	CHECK_STR(CP_mapGet(&map, "Dr. Mario.gb"), "Doctor");
	CHECK(CP_mapGet(&map, "notab") == NULL);
	CP_mapFree(&map);
	CHECK(!CP_mapLoad(&map, CARD "/no-such-map.txt"));

	snprintf(file, sizeof(file), "%s/model-order.txt", CARD);
	writeText(file, "/Roms/GB/Zed.gb\r\n/Roms/GB/Alpha.gb\n\n/Roms/GB/Zed.gb\n/Roms/GB/Mid.gb\n");
	CPOrder order;
	CP_orderInit(&order);
	CHECK(CP_orderLoad(&order, file));
	CHECK(order.count == 4);
	CHECK(CP_orderIndex(&order, "/Roms/GB/Zed.gb") == 0);
	CHECK(CP_orderIndex(&order, "/Roms/GB/Alpha.gb") == 1);
	CHECK(CP_orderIndex(&order, "/Roms/GB/Mid.gb") == 3);
	CHECK(CP_orderIndex(&order, "/Roms/GB/Gone.gb") == -1);
	CHECK(CP_orderIndex(&order, NULL) == -1);
	CP_orderFree(&order);
	CHECK(CP_orderIndex(&order, "/Roms/GB/Zed.gb") == -1);
}

static void makeCard(void) {
	mkdir(CARD, 0755);
	mkdir(COLL, 0755);
	mkdir(COLL "/.media", 0755);
	mkdir(COLL "/Sub", 0755);
	writeText(COLL "/A.txt", "/Roms/GB/Zed.gb\n/Roms/GB/Alpha.gb\n/Roms/GB/Tetris.gb\n/Roms/GB/Hide.gb\n");
	writeText(COLL "/B.txt", "/Roms/GB/Alpha.gb\n");
	writeText(COLL "/C (x).txt", "/Roms/GB/Alpha.gb\n");
	writeText(COLL "/Gone.txt", "/Roms/GB/Alpha.gb\n");
	writeText(COLL "/D.txt.disabled", "/Roms/GB/Alpha.gb\n");
	writeText(COLL "/.dot.txt", "/Roms/GB/Alpha.gb\n");
	writeText(COLL "/map.txt", "B.txt\tBee\nGone.txt\t.gone\nTetris.gb\tShared Tetris\n");
	writeText(COLL "/.media/A.map.txt", "Tetris.gb\tTetris DX\nHide.gb\t.hidden\n");
	writeText(COLL "/.media/A.png", "");
	writeText(COLL "/.media/B.jpg", "");
	writeText(COLL "/Sub/S.txt", "/Roms/GB/Alpha.gb\n");
	writeText(COLL "/Sub/map.txt", "Alpha.gb\tSub Alpha\n");
	mkdir(COLL "/.icons", 0755);
	mkdir(COLL "/Sub/.icons", 0755);
	writeText(COLL "/.icons/A.png", "");
	writeText(COLL "/.icons/S.png", "");
	remove(COLL "/Sub/.icons/S.png"); // testArt adds it
}

static void testRoot(void) {
	char got[512];
	resetSettings();
	const MosaicPlugin* p = start();
	rootNames(p, NULL, got, sizeof(got));
	CHECK_STR(got, ""); // nothing promoted by default

	// every collection, by name: stock's shared map names B and hides Gone
	set("promote", "*");
	p = start();
	rootNames(p, NULL, got, sizeof(got));
	CHECK_STR(got, "A,Bee,C (x)");

	// in the order setting's order, then by name
	set("order", "c (x), Bee");
	p = start();
	rootNames(p, NULL, got, sizeof(got));
	CHECK_STR(got, "C (x),Bee,A");

	// only those named, in promote's order where order has no say
	set("order", NULL);
	set("promote", "B, A, Nope");
	p = start();
	rootNames(p, NULL, got, sizeof(got));
	CHECK_STR(got, "Bee,A");
	set("order", "A");
	p = start();
	rootNames(p, NULL, got, sizeof(got));
	CHECK_STR(got, "A,Bee");

	// one stock already promoted, with no systems, isn't added twice
	rootNames(p, COLL "/A.txt", got, sizeof(got));
	CHECK_STR(got, "A,Bee");

	// the Collections folder stays on the root unless hide_folder is on
	MosaicEntry* folder = fakeEntryNew(COLL, MOSAIC_ENTRY_DIR, NULL);
	CHECK(p->filter(folder, CARD) == 1);
	set("hide_folder", "on");
	p = start();
	CHECK(p->filter(folder, CARD) == 0);
	CHECK(p->filter(folder, CARD "/Roms") == 1);
	free(folder);
}

// The root as the launcher lists it: stock's rows in front, the plugin's
// root entries, rows added after them (eg. Tools), then the plugin's sort.
static void mixedRoot(const MosaicPlugin* p, const char** stock, const char** after, char* out, size_t size) {
	MosaicList* root = fakeListNew();
	for (int i = 0; stock[i]; i++) fakeListPush(root, fakeEntryNew(stock[i], MOSAIC_ENTRY_DIR, NULL));
	p->root_entries(root);
	for (int i = 0; after && after[i]; i++) fakeListPush(root, fakeEntryNew(after[i], MOSAIC_ENTRY_DIR, NULL));
	for (int i = 0; i < root->count; i++) p->filter(root->items[i], CARD);
	sortBy(p, root->items, root->count, CARD);
	p->list_ready(root, CARD);
	names(root, out, size);
	freeList(root);
}

static void testMixedRoot(void) {
	char got[512];
	const char* stock[] = { CARD "/Recently Played", COLL, CARD "/Roms/Game Boy (GB)", CARD "/Roms/Nintendo (FC)",
		CARD "/Roms/Super Nintendo (SFC)", NULL };
	const char* after[] = { CARD "/Most Played", CARD "/Tools/test", NULL };
	resetSettings();
	set("promote", "*");
	// order naming no systems: as before, the collections after the consoles
	set("order", "C (x), Bee");
	const MosaicPlugin* p = start();
	mixedRoot(p, stock, after, got, sizeof(got));
	CHECK_STR(got, "Recently Played,Collections,Game Boy (GB),Nintendo (FC),Super Nintendo (SFC),C (x),Bee,A,Most Played,test");

	// a collection between consoles, by a system's folder name, tag or shown name
	set("order", "game boy (gb), Bee, SFC");
	p = start();
	mixedRoot(p, stock, after, got, sizeof(got));
	CHECK_STR(got, "Recently Played,Collections,Game Boy (GB),Bee,Nintendo (FC),Super Nintendo (SFC),A,C (x),Most Played,test");
	set("order", "Bee, GB, A");
	p = start();
	mixedRoot(p, stock, after, got, sizeof(got));
	CHECK_STR(got, "Recently Played,Collections,Bee,Game Boy (GB),A,Nintendo (FC),Super Nintendo (SFC),C (x),Most Played,test");

	// other plugins' root folders and the Collections folder aren't named
	set("order", "Most Played, Collections, SFC, A");
	p = start();
	mixedRoot(p, stock, after, got, sizeof(got));
	CHECK_STR(got, "Recently Played,Collections,Game Boy (GB),Nintendo (FC),Super Nintendo (SFC),A,Bee,C (x),Most Played,test");

	// systems only, without promote: the systems in order
	set("promote", NULL);
	set("order", "SFC, GB");
	p = start();
	mixedRoot(p, stock, after, got, sizeof(got));
	CHECK_STR(got, "Recently Played,Collections,Super Nintendo (SFC),Nintendo (FC),Game Boy (GB),Most Played,test");

	// a root listed without root_entries, such as a plugin's own, isn't placed
	MosaicEntry* items[3] = { fakeEntryNew(CARD "/Roms/Game Boy (GB)", MOSAIC_ENTRY_DIR, NULL),
		fakeEntryNew(CARD "/Roms/Nintendo (FC)", MOSAIC_ENTRY_DIR, NULL),
		fakeEntryNew(CARD "/Roms/Super Nintendo (SFC)", MOSAIC_ENTRY_DIR, NULL) };
	listing(p, CARD, items, 3, got, sizeof(got));
	CHECK_STR(got, "Game Boy (GB),Nintendo (FC),Super Nintendo (SFC)");
}

static void testCollection(void) {
	char got[512];
	resetSettings();
	const MosaicPlugin* p = start();

	// the collection's own map names and hides, its file order holds, and
	// stock's shared name stays for what the own map leaves out
	MosaicEntry* items[4] = {
		fakeEntryNew(CARD "/Roms/GB/Alpha.gb", MOSAIC_ENTRY_ROM, NULL),
		fakeEntryNew(CARD "/Roms/GB/Hide.gb", MOSAIC_ENTRY_ROM, NULL),
		fakeEntryNew(CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM, "Shared Tetris"),
		fakeEntryNew(CARD "/Roms/GB/Zed.gb", MOSAIC_ENTRY_ROM, NULL),
	};
	listing(p, COLL "/A.txt", items, 4, got, sizeof(got));
	CHECK_STR(got, "Zed,Alpha,Tetris DX");

	// a collection in a folder takes the folder's map.txt
	MosaicEntry* sub[1] = { fakeEntryNew(CARD "/Roms/GB/Alpha.gb", MOSAIC_ENTRY_ROM, NULL) };
	listing(p, COLL "/Sub/S.txt", sub, 1, got, sizeof(got));
	CHECK_STR(got, "Sub Alpha");

	// a folder that isn't a collection isn't renamed or reordered
	MosaicEntry* roms[2] = {
		fakeEntryNew(CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM, NULL),
		fakeEntryNew(CARD "/Roms/GB/Hide.gb", MOSAIC_ENTRY_ROM, NULL),
	};
	listing(p, CARD "/Roms/GB", roms, 2, got, sizeof(got));
	CHECK_STR(got, "Tetris,Hide");

	// with both off, stock's names and order stay
	set("own_map", "off");
	set("file_order", "off");
	p = start();
	MosaicEntry* plain[3] = {
		fakeEntryNew(CARD "/Roms/GB/Alpha.gb", MOSAIC_ENTRY_ROM, NULL),
		fakeEntryNew(CARD "/Roms/GB/Hide.gb", MOSAIC_ENTRY_ROM, NULL),
		fakeEntryNew(CARD "/Roms/GB/Zed.gb", MOSAIC_ENTRY_ROM, NULL),
	};
	listing(p, COLL "/A.txt", plain, 3, got, sizeof(got));
	CHECK_STR(got, "Alpha,Hide,Zed");
}

static void testFolder(void) {
	char got[512];
	resetSettings();
	set("order", "C (x), Sub");
	const MosaicPlugin* p = start();
	MosaicEntry* items[4] = {
		fakeEntryNew(COLL "/A.txt", MOSAIC_ENTRY_DIR, NULL),
		fakeEntryNew(COLL "/B.txt", MOSAIC_ENTRY_DIR, "Bee"),
		fakeEntryNew(COLL "/C (x).txt", MOSAIC_ENTRY_DIR, NULL),
		fakeEntryNew(COLL "/Sub", MOSAIC_ENTRY_DIR, NULL),
	};
	listing(p, COLL, items, 4, got, sizeof(got));
	CHECK_STR(got, "C (x),Sub,A,Bee");
}

static void testArt(void) {
	resetSettings();
	const MosaicPlugin* p = start();
	char out[512];
	MosaicEntry* a = fakeEntryNew(COLL "/A.txt", MOSAIC_ENTRY_DIR, NULL);
	MosaicEntry* b = fakeEntryNew(COLL "/B.txt", MOSAIC_ENTRY_DIR, NULL);
	MosaicEntry* c = fakeEntryNew(COLL "/C (x).txt", MOSAIC_ENTRY_DIR, NULL);
	MosaicEntry* rom = fakeEntryNew(CARD "/Roms/GB/A.txt", MOSAIC_ENTRY_ROM, NULL);
	MosaicEntry* sub = fakeEntryNew(COLL "/Sub/S.txt", MOSAIC_ENTRY_DIR, NULL);
	// the icon is the thumbnail, over the art; the box stays the art
	CHECK(p->art_path(a, MOSAIC_ART_THUMBNAIL, out, sizeof(out)) == 1);
	CHECK_STR(out, COLL "/.icons/A.png");
	CHECK(p->art_path(a, MOSAIC_ART_BOX, out, sizeof(out)) == 1);
	CHECK_STR(out, COLL "/.media/A.png");
	CHECK(p->art_path(b, MOSAIC_ART_THUMBNAIL, out, sizeof(out)) == 1);
	CHECK_STR(out, COLL "/.media/B.jpg");
	CHECK(p->art_path(sub, MOSAIC_ART_THUMBNAIL, out, sizeof(out)) == 1);
	CHECK_STR(out, COLL "/.icons/S.png");
	writeText(COLL "/Sub/.icons/S.png", "");
	CHECK(p->art_path(sub, MOSAIC_ART_THUMBNAIL, out, sizeof(out)) == 1);
	CHECK_STR(out, COLL "/Sub/.icons/S.png");
	free(sub);
	CHECK(p->art_path(b, MOSAIC_ART_BOX, out, sizeof(out)) == 1);
	CHECK_STR(out, COLL "/.media/B.jpg");
	CHECK(p->art_path(a, MOSAIC_ART_LOGO, out, sizeof(out)) == 0);
	CHECK(p->art_path(c, MOSAIC_ART_BOX, out, sizeof(out)) == 0);
	CHECK(p->art_path(rom, MOSAIC_ART_THUMBNAIL, out, sizeof(out)) == 0);
	set("art", "off");
	p = start();
	CHECK(p->art_path(a, MOSAIC_ART_BOX, out, sizeof(out)) == 0);
	CHECK(p->art_path(a, MOSAIC_ART_THUMBNAIL, out, sizeof(out)) == 0);
	free(a);
	free(b);
	free(c);
	free(rom);
}

static void testGamesTile(void) {
	resetSettings();
	hidden_tile[0] = '\0';
	start();
	CHECK_STR(hidden_tile, "");
	set("hide_games", "on");
	start();
	CHECK_STR(hidden_tile, "Games");
}

int main(void) {
	makeCard();
	testModel();
	testRoot();
	testMixedRoot();
	testCollection();
	testFolder();
	testArt();
	testGamesTile();
	CHECK_DONE();
}
