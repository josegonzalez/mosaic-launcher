// The search plugin: its model on its own, how long a search of a few
// thousand games takes, then the plugin against a fake host on a fixture
// card.

#include <stdlib.h>
#include <sys/time.h>
#include "check.h"
#include "../../plugins/search/search.c"

struct MosaicEntry {
	char path[1024];
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
#define PS ROMS "/Sony PlayStation (PS)"
#define GG ROMS "/Game Gear (GG)"
#define SEARCH CARD "/Search"

///////////////////////////////////////
// the model

static void fold(const char* text, const char* want) {
	char out[256];
	size_t len = SE_fold(text, out, sizeof(out));
	CHECK_STR(out, want);
	CHECK(len == strlen(want));
}

static void testFold(void) {
	fold("Super Mario Bros.", "super mario bros");
	fold("  The Legend of Zelda: Link's Awakening DX ", "the legend of zelda links awakening dx");
	fold("Pok\xc3\xa9mon Red", "pokemon red");                // é
	fold("POK\xc3\x89MON", "pokemon");                        // É
	fold("Poke\xcc\x81mon", "pokemon");                       // e and a combining acute
	fold("Stra\xc3\x9f" "e", "strasse");                     // ß
	fold("\xc3\x86on Flux", "aeon flux");                     // Æ
	fold("\xc5\x81\xc3\xb3" "d\xc5\xba", "lodz");                // Łódź
	fold("Link\xe2\x80\x99s", "links");                       // ’
	fold("Mega Man X\xe2\x80\x94" "Command", "mega man x command"); // em dash
	fold("Caf\xc3\xa9\xc2\xae", "cafe");                      // ®
	fold("\xe3\x83\x9e\xe3\x83\xaa\xe3\x82\xaa 64", "\xe3\x83\x9e\xe3\x83\xaa\xe3\x82\xaa 64"); // kept
	fold("A\xc3", "a\xc3");                                    // cut short: the byte alone
	fold("--- ", "");
	fold("", "");
	fold(NULL, "");

	char small[6];
	CHECK(SE_fold("Pokemon", small, sizeof(small)) == 5);
	CHECK_STR(small, "pokem");
}

static void testNames(void) {
	char name[SE_NAME_LEN];
	SE_cleanName("Tetris (World) (Rev 1).gb", name, sizeof(name));
	CHECK_STR(name, "Tetris");
	SE_cleanName("Pokemon Red [!] (USA).gb", name, sizeof(name));
	CHECK_STR(name, "Pokemon Red");
	SE_cleanName("Doom.pak", name, sizeof(name));
	CHECK_STR(name, "Doom");
	SE_cleanName("Game.p8.png", name, sizeof(name));
	CHECK_STR(name, "Game");
	SE_cleanName("(USA).gb", name, sizeof(name));
	CHECK_STR(name, "(USA)");
	SE_cleanName("Final Fantasy VII (USA)", name, sizeof(name));
	CHECK_STR(name, "Final Fantasy VII");

	CHECK(SE_hidden(".media"));
	CHECK(SE_hidden("Old.gb.disabled"));
	CHECK(SE_hidden("map.txt"));
	CHECK(!SE_hidden("Tetris.gb"));

	SEMap map;
	CHECK(SE_mapParse(&map, strdup("Tetris.gb\tTetris DX\r\nbad line\nEmpty.gb\t\nTetris.gb\tTetris Again\nKirby.gb\tKirby\n")) == 0);
	CHECK(map.count == 2);
	CHECK_STR(SE_mapGet(&map, "Tetris.gb"), "Tetris Again");
	CHECK_STR(SE_mapGet(&map, "Kirby.gb"), "Kirby");
	CHECK(SE_mapGet(&map, "Empty.gb") == NULL);
	CHECK(SE_mapGet(&map, "Mario.gb") == NULL);
	SE_mapFree(&map);
	CHECK(SE_mapParse(&map, NULL) == 0);
	CHECK(SE_mapGet(&map, "Tetris.gb") == NULL);
	SE_mapFree(&map);
}

static void testRank(void) {
	CHECK(SE_rank("tetris", "tetris") == SE_RANK_EXACT);
	CHECK(SE_rank("tetris dx", "tetris") == SE_RANK_PREFIX);
	CHECK(SE_rank("tetris dx", "tet") == SE_RANK_PREFIX);
	CHECK(SE_rank("super mario land", "mario") == SE_RANK_WORDS);
	CHECK(SE_rank("super mario land", "land mar") == SE_RANK_WORDS);
	CHECK(SE_rank("super mario land", "ario") == SE_RANK_INSIDE);
	CHECK(SE_rank("super mario land", "mario and") == SE_RANK_INSIDE);
	CHECK(SE_rank("super mario land", "zelda") == -1);
	CHECK(SE_rank("super mario land", "mario zelda") == -1);
	CHECK(SE_rank("super mario land", "") == -1);
	CHECK(SE_rank("", "mario") == -1);
}

static void testSearch(void) {
	SEIndex index;
	SE_indexInit(&index);
	CHECK(SE_indexAdd(&index, "/r/GB/Super Mario Land.gb", "Super Mario Land", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/NES/Mario.nes", "Mario", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/GB/Mario.gb", "Mario", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/SNES/Mario Kart.sfc", "Mario Kart", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/GB/Dr. Mario.gb", "Dr. Mario", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/N64/Mario Party.z64", "Mario Party", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/GBA/Somariom.gba", "Somariom", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/GB/Pokemon.gb", "Pok\xc3\xa9mon Red", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SE_indexAdd(&index, "/r/GB/Zelda", "Zelda", MOSAIC_ENTRY_DIR) == 0);
	CHECK(index.game_count == 9);
	CHECK_STR(index.games[7].folded, "pokemon red");
	CHECK(SE_indexAddFolder(&index, "/r/GB", 7) == 0);
	CHECK(index.folder_count == 1 && index.folders[0].mtime == 7);

	SEHit hits[9];
	int count = SE_search(&index, "  MARIO ", hits);
	const char* want[] = {
		"/r/GB/Mario.gb", "/r/NES/Mario.nes",                  // the name, by path
		"/r/SNES/Mario Kart.sfc", "/r/N64/Mario Party.z64",   // starting with it, by name
		"/r/GB/Dr. Mario.gb", "/r/GB/Super Mario Land.gb",    // a word starting with it
		"/r/GBA/Somariom.gba",                                 // inside a word
	};
	CHECK(count == 7);
	for (int i = 0; i < count && i < 7; i++) CHECK_STR(index.games[hits[i].game].path, want[i]);
	CHECK(hits[0].rank == SE_RANK_EXACT && hits[2].rank == SE_RANK_PREFIX && hits[4].rank == SE_RANK_WORDS
		&& hits[6].rank == SE_RANK_INSIDE);

	// accents and case fold both ways
	CHECK(SE_search(&index, "pokemon", hits) == 1);
	CHECK(SE_search(&index, "POK\xc3\x89MON red", hits) == 1);
	CHECK(SE_search(&index, "red pok", hits) == 1);
	CHECK(SE_search(&index, "zelda", hits) == 1 && index.games[hits[0].game].type == MOSAIC_ENTRY_DIR);
	CHECK(SE_search(&index, "luigi", hits) == 0);
	CHECK(SE_search(&index, "", hits) == 0);
	CHECK(SE_search(&index, "!?", hits) == 0);

	// the order results keep
	count = SE_search(&index, "mario", hits);
	SEOrder order;
	CHECK(SE_orderBuild(&order, &index, hits, count) == 0);
	CHECK(order.count == 7);
	CHECK(SE_orderOf(&order, "/r/GB/Mario.gb") == 0);
	CHECK(SE_orderOf(&order, "/r/GBA/Somariom.gba") == 6);
	CHECK(SE_orderOf(&order, "/r/GB/Pokemon.gb") == -1);
	CHECK(SE_orderOf(&order, NULL) == -1);
	SE_orderFree(&order);
	CHECK(SE_orderBuild(&order, &index, hits, 0) == 0);
	CHECK(SE_orderOf(&order, "/r/GB/Mario.gb") == -1);
	SE_orderFree(&order);

	SE_indexFree(&index);
	CHECK(index.game_count == 0 && index.folder_count == 0 && index.games == NULL);
}

static void testRecents(void) {
	char text[32] = "  tetris  ";
	SE_trim(text);
	CHECK_STR(text, "tetris");
	snprintf(text, sizeof(text), "   ");
	SE_trim(text);
	CHECK_STR(text, "");

	SERecents recents = { .count = 0 };
	SE_recentsAdd(&recents, "mario", 3);
	SE_recentsAdd(&recents, " zelda ", 3);
	SE_recentsAdd(&recents, "   ", 3);
	CHECK(recents.count == 2);
	CHECK_STR(recents.items[0], "zelda");
	CHECK_STR(recents.items[1], "mario");
	// the same query again moves to the front, as typed this time
	SE_recentsAdd(&recents, "MARIO", 3);
	CHECK(recents.count == 2);
	CHECK_STR(recents.items[0], "MARIO");
	CHECK_STR(recents.items[1], "zelda");
	SE_recentsAdd(&recents, "kirby", 3);
	SE_recentsAdd(&recents, "metroid", 3);
	CHECK(recents.count == 3);
	CHECK_STR(recents.items[0], "metroid");
	CHECK_STR(recents.items[2], "MARIO");
	SE_recentsAdd(&recents, "tetris", 0);
	CHECK(recents.count == 3 && !strcmp(recents.items[0], "metroid"));
	for (int i = 0; i < 20; i++) {
		char q[16];
		snprintf(q, sizeof(q), "q%d", i);
		SE_recentsAdd(&recents, q, 100);
	}
	CHECK(recents.count == SE_RECENTS_MAX);
	CHECK_STR(recents.items[0], "q19");
}

///////////////////////////////////////
// timing

static double now_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

// Fake names from a few words, so many share words with the queries.
static void testTiming(void) {
	static const char* words[] = {
		"Super", "Mario", "Zelda", "Pok\xc3\xa9mon", "Kart", "Land", "Quest", "Dragon", "Final", "Fantasy",
		"Street", "Fighter", "Mega", "Man", "Sonic", "Hedgehog", "Kirby", "Dream", "World", "Castle",
	};
	const int n = 5000;
	SEIndex index;
	SE_indexInit(&index);
	unsigned seed = 12345;
	double start = now_ms();
	for (int i = 0; i < n; i++) {
		char name[128] = "";
		int count = 2 + (int)(seed % 4);
		for (int w = 0; w < count; w++) {
			seed = seed * 1103515245u + 12345u;
			strcat(name, words[(seed >> 16) % 20]);
			strcat(name, w + 1 < count ? " " : "");
		}
		char path[192];
		snprintf(path, sizeof(path), "/r/System %d (S%d)/%s %d.rom", i % 30, i % 30, name, i);
		CHECK(SE_indexAdd(&index, path, name, MOSAIC_ENTRY_ROM) == 0);
	}
	double indexed = now_ms() - start;

	SEHit* hits = malloc(sizeof(SEHit) * (size_t)n);
	const char* queries[] = { "mario", "pokemon kart", "q", "zzz", "dragon quest final" };
	double worst = 0;
	for (size_t q = 0; q < sizeof(queries) / sizeof(queries[0]); q++) {
		start = now_ms();
		int found = 0;
		for (int r = 0; r < 10; r++) found = SE_search(&index, queries[q], hits);
		double each = (now_ms() - start) / 10;
		if (each > worst) worst = each;
		printf("  search \"%s\": %d of %d games in %.3f ms\n", queries[q], found, n, each);
	}
	printf("  indexing %d games took %.2f ms; the slowest search %.3f ms\n", n, indexed, worst);
	CHECK(worst < 250); // only catches something badly wrong; a desktop takes well under 1 ms
	free(hits);
	SE_indexFree(&index);
}

///////////////////////////////////////
// the plugin against a fake host

#define MAX_CFG 16
static struct {
	char key[32];
	char value[SE_QUERY_LEN];
} cfg[MAX_CFG];
static int cfg_count = 0;
static int cfg_sets = 0;
static char current[1024] = CARD;
static int lists_ready = 1;
static char action_id[64];
static int (*action_available)(void*);
static void (*action_run)(void*);
static int bound_button = -1;
static char toasted[160];
static char opened[1024];
static int opens = 0, reloads = 0;
static char ran[64];
static int runs = 0;

// the open dialog, if any
static int keyboards = 0;
static char keyboard_title[64];
static void (*keyboard_done)(const char*, void*);
static int chooses = 0;
static char choices[SE_RECENTS_MAX + 1][SE_QUERY_LEN];
static int choice_count = 0;
static void (*choice_pick)(int, void*);

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	for (int i = 0; i < cfg_count; i++) {
		if (!strcmp(cfg[i].key, key)) return cfg[i].value;
	}
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) {
	cfg_sets++;
	for (int i = 0; i < cfg_count; i++) {
		if (!strcmp(cfg[i].key, key)) {
			snprintf(cfg[i].value, sizeof(cfg[i].value), "%s", value);
			return;
		}
	}
	if (cfg_count >= MAX_CFG) return;
	snprintf(cfg[cfg_count].key, sizeof(cfg[0].key), "%s", key);
	snprintf(cfg[cfg_count].value, sizeof(cfg[0].value), "%s", value);
	cfg_count++;
}
static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = calloc(1, sizeof(MosaicEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name ? name : "");
	e->type = type;
	return e;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e ? e->path : NULL; }
static MosaicList* fakeListNew(void) { return calloc(1, sizeof(MosaicList)); }
static void fakeListPush(MosaicList* l, MosaicEntry* e) {
	if (l->count < 64) l->items[l->count++] = e;
	else free(e);
}
static const char* fakeCurrentPath(void) { return lists_ready ? current : NULL; }
static void fakeRedraw(void) {}
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)label; (void)data;
	snprintf(action_id, sizeof(action_id), "%s", id);
	action_available = available;
	action_run = run;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen; (void)trigger; (void)id;
	bound_button = button;
	return 0;
}
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static void fakeKeyboard(const char* title, const char* initial, void (*on_done)(const char*, void*), void* data) {
	(void)initial; (void)data;
	keyboards++;
	snprintf(keyboard_title, sizeof(keyboard_title), "%s", title);
	keyboard_done = on_done;
}
static int fakeChoose(const char* title, const char* const* list, int count, int selected, void (*on_pick)(int, void*), void* data) {
	(void)title; (void)selected; (void)data;
	chooses++;
	choice_count = count;
	for (int i = 0; i < count && i <= SE_RECENTS_MAX; i++) snprintf(choices[i], sizeof(choices[i]), "%s", list[i]);
	choice_pick = on_pick;
	return 0;
}
// the list the game list shows after an open_path of the Search folder
static MosaicList* shown = NULL;
static int selected_row = -1;
static const char* screen_name = "gamelist";

static void freeList(MosaicList* list);

static int fakeOpenPath(const char* path, int type) {
	(void)type;
	snprintf(opened, sizeof(opened), "%s", path);
	snprintf(current, sizeof(current), "%s", path);
	opens++;
	freeList(shown);
	shown = !strcmp(path, SEARCH) ? plugin.virtual_dir(path) : NULL;
	selected_row = 0;
	return 1;
}
static void fakeReload(void) { reloads++; }
// gamelist.back goes to the folder above, as far as the root
static int fakeRunAction(const char* id) {
	snprintf(ran, sizeof(ran), "%s", id);
	runs++;
	if (!strcmp(id, "gamelist.back")) {
		if (!strcmp(current, CARD)) return -1;
		*strrchr(current, '/') = '\0';
	}
	return 0;
}
static const MosaicList* fakeCurrentList(void) { return shown; }
static int fakeListCount(const MosaicList* l) { return l ? l->count : 0; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) { return l && i >= 0 && i < l->count ? l->items[i] : NULL; }
static int fakeSelectIndex(int i) {
	selected_row = i;
	return 0;
}
static const char* fakeCurrentScreen(void) { return screen_name; }

static MosaicHost fake = {
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
	.list_new = fakeListNew,
	.list_push = fakeListPush,
	.current_path = fakeCurrentPath,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.keyboard = fakeKeyboard,
	.open_path = fakeOpenPath,
	.reload_lists = fakeReload,
	.choose = fakeChoose,
	.run_action = fakeRunAction,
	.current_list = fakeCurrentList,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.select_index = fakeSelectIndex,
	.current_screen = fakeCurrentScreen,
};

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

static int indexOf(const MosaicList* list, const char* path) {
	for (int i = 0; list && i < list->count; i++) {
		if (!strcmp(list->items[i]->path, path)) return i;
	}
	return -1;
}

static void makeCard(void) {
	(void)system("rm -rf '" CARD "'");
	CHECK(system("mkdir -p '" CARD "/.system/test/paks/Emus/GB.pak' '" CARD "/Emus/test/PS.pak'"
		" '" GB "/Hacks/Deep/Deeper/Deepest' '" PS "/Crash (USA)' '" PS "/Loose' '" GG "' '" GB "/Tool.pak'") == 0);
	writeText(CARD "/.system/test/paks/Emus/GB.pak/launch.sh", "");
	writeText(CARD "/Emus/test/PS.pak/launch.sh", "");
	writeText(GB "/Tetris (World) (Rev 1).gb", "");
	writeText(GB "/Kirby.gb", "");
	writeText(GB "/Pokemon.gb", "");
	writeText(GB "/Secret Mario.gb", "");
	writeText(GB "/.Hidden Mario.gb", "");
	writeText(GB "/Old Mario.gb.disabled", "");
	writeText(GB "/map.txt", "Kirby.gb\tKirby's Dream Land\nPokemon.gb\tPok\xc3\xa9mon Red\nSecret Mario.gb\t.Secret\n");
	writeText(GB "/Hacks/Mario Hack.gb", "");
	writeText(GB "/Hacks/Deep/Deeper/Deep Mario.gb", "");
	writeText(GB "/Hacks/Deep/Deeper/Deepest/Too Deep Mario.gb", "");
	writeText(GB "/Tool.pak/launch.sh", "");
	writeText(PS "/Crash (USA)/Crash (USA).cue", "");
	writeText(PS "/Crash (USA)/Crash (USA).bin", "");
	writeText(PS "/Loose/Crash Bash.cue", "");
	writeText(GG "/Mario GG.gg", ""); // no emulator
}

// Lists the Search folder as the launcher would.
static MosaicList* listSearch(const MosaicPlugin* p) {
	return p->virtual_dir(SEARCH);
}

static void testPlugin(void) {
	makeCard();
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "search");
	CHECK(p->init(&fake) == 0);
	CHECK_STR(action_id, "search.open");
	CHECK(action_available && action_available(NULL));
	CHECK(bound_button == -1); // no button by default

	// a Search row on the root
	MosaicList* root = fake.list_new();
	p->root_entries(root);
	CHECK(root->count == 1);
	if (root->count == 1) {
		CHECK_STR(root->items[0]->path, SEARCH);
		CHECK_STR(root->items[0]->name, "Search");
		CHECK(root->items[0]->type == MOSAIC_ENTRY_DIR);
	}
	freeList(root);

	// other folders aren't ours, and nothing is searched yet
	CHECK(p->virtual_dir(GB) == NULL);
	MosaicList* list = listSearch(p);
	CHECK(list && list->count == 0);
	freeList(list);

	// at start the game list is on the root; opening the row asks, with
	// no recents straight on the keyboard
	p->tick(0);
	CHECK(keyboards == 0);
	snprintf(current, sizeof(current), "%s", SEARCH);
	p->tick(0);
	CHECK(keyboards == 1 && chooses == 0);
	CHECK_STR(keyboard_title, "Search");
	p->tick(0);
	CHECK(keyboards == 1); // only once per visit

	// typing lists the results there, best first
	opens = 0;
	keyboard_done(" mario ", NULL);
	CHECK(opens == 0 && reloads == 1); // already in the folder
	CHECK_STR(fakeCfgGet("query", ""), "mario");
	CHECK_STR(fakeCfgGet("recent1", ""), "mario");
	list = listSearch(p);
	CHECK(list != NULL);
	if (list) {
		const char* want[][2] = {
			{ GB "/Hacks/Deep/Deeper/Deep Mario.gb", "Deep Mario" },
			{ GB "/Hacks/Mario Hack.gb", "Mario Hack" },
		};
		CHECK(list->count == 2);
		for (int i = 0; i < 2 && i < list->count; i++) {
			CHECK_STR(list->items[i]->path, want[1 - i][0]); // prefix first
			CHECK_STR(list->items[i]->name, want[1 - i][1]);
		}
		// hidden, map-hidden, too deep and emulator-less games aren't searched
		CHECK(indexOf(list, GB "/Secret Mario.gb") < 0);
		CHECK(indexOf(list, GG "/Mario GG.gg") < 0);
		CHECK(indexOf(list, GB "/Hacks/Deep/Deeper/Deepest/Too Deep Mario.gb") < 0);

		// the ranked order holds through the launcher's sort
		CHECK(p->sort_compare(list->items[1], list->items[0], SEARCH) > 0);
		CHECK(p->sort_compare(list->items[0], list->items[1], SEARCH) < 0);
		CHECK(p->sort_compare(list->items[0], list->items[1], GB) == 0);
		MosaicEntry* other = fakeEntryNew(GB "/Kirby.gb", MOSAIC_ENTRY_ROM, "Kirby");
		CHECK(p->sort_compare(list->items[0], other, SEARCH) == 0);
		free(other);
	}
	freeList(list);

	// map.txt names, accents and case fold; game folders and paks are found
	keyboards = 0;
	action_run(NULL); // the action asks from anywhere: recents first now
	CHECK(chooses == 1 && keyboards == 0);
	CHECK(choice_count == 2);
	CHECK_STR(choices[0], "New search");
	CHECK_STR(choices[1], "mario");
	choice_pick(0, NULL);
	CHECK(keyboards == 1);
	reloads = 0;
	keyboard_done("POK\xc3\x89MON", NULL);
	CHECK(reloads == 1);
	list = listSearch(p);
	CHECK(list && list->count == 1);
	if (list && list->count == 1) {
		CHECK_STR(list->items[0]->path, GB "/Pokemon.gb");
		CHECK_STR(list->items[0]->name, "Pok\xc3\xa9mon Red");
	}
	freeList(list);
	keyboard_done("dream", NULL);
	list = listSearch(p);
	CHECK(list && list->count == 1 && !strcmp(list->items[0]->name, "Kirby's Dream Land"));
	freeList(list);
	keyboard_done("crash", NULL);
	list = listSearch(p);
	CHECK(list && list->count == 2);
	if (list && list->count == 2) {
		CHECK_STR(list->items[0]->path, PS "/Crash (USA)");
		CHECK(list->items[0]->type == MOSAIC_ENTRY_DIR);
		CHECK_STR(list->items[1]->path, PS "/Loose/Crash Bash.cue");
		CHECK(list->items[1]->type == MOSAIC_ENTRY_ROM);
	}
	freeList(list);
	keyboard_done("tool", NULL);
	list = listSearch(p);
	CHECK(list && list->count == 1 && list->items[0]->type == MOSAIC_ENTRY_PAK);
	freeList(list);

	// nothing found says so, and still shows the (empty) folder
	toasted[0] = '\0';
	keyboard_done("luigi", NULL);
	CHECK_STR(toasted, "No games match luigi");
	list = listSearch(p);
	CHECK(list && list->count == 0);
	freeList(list);
	CHECK_STR(fakeCfgGet("recent1", ""), "luigi");
	CHECK_STR(fakeCfgGet("recent2", ""), "tool");
	CHECK_STR(fakeCfgGet("recent5", ""), "POK\xc3\x89MON");
	CHECK_STR(fakeCfgGet("recent6", ""), "mario");

	// a recent runs again; from elsewhere it opens the folder
	snprintf(current, sizeof(current), "%s", GB);
	p->tick(0);
	chooses = 0;
	action_run(NULL);
	CHECK(chooses == 1 && choice_count == 7);
	CHECK_STR(choices[6], "mario");
	opens = 0;
	choice_pick(6, NULL);
	CHECK(opens == 1);
	CHECK_STR(opened, SEARCH);
	CHECK_STR(fakeCfgGet("query", ""), "mario");
	CHECK_STR(fakeCfgGet("recent1", ""), "mario");
	CHECK_STR(fakeCfgGet("recent2", ""), "luigi");
	// the folder it opened doesn't ask again
	keyboards = chooses = 0;
	p->tick(0);
	CHECK(keyboards == 0 && chooses == 0);

	// cancelling from the action stays where it is
	snprintf(current, sizeof(current), "%s", GB);
	p->tick(0);
	runs = 0;
	action_run(NULL);
	choice_pick(-1, NULL);
	CHECK(runs == 0);
	action_run(NULL);
	choice_pick(0, NULL);
	keyboard_done(NULL, NULL);
	CHECK(runs == 0);
	CHECK_STR(current, GB);

	// cancelling from the root row backs out, from the choices or the keyboard
	snprintf(current, sizeof(current), "%s", CARD);
	p->tick(0);
	snprintf(current, sizeof(current), "%s", SEARCH);
	chooses = 0;
	p->tick(0);
	CHECK(chooses == 1);
	choice_pick(-1, NULL);
	CHECK(runs == 1);
	CHECK_STR(ran, "gamelist.back");
	CHECK_STR(current, CARD);
	p->tick(0);
	snprintf(current, sizeof(current), "%s", SEARCH);
	p->tick(0);
	choice_pick(0, NULL);
	keyboard_done("   ", NULL); // nothing typed is a cancel too
	CHECK(runs == 2);
	CHECK_STR(current, CARD);
	CHECK_STR(fakeCfgGet("query", ""), "mario");
	p->quit();

	// started in the folder, it lists the same results without asking
	snprintf(current, sizeof(current), "%s", SEARCH);
	lists_ready = 0;
	CHECK(p->init(&fake) == 0);
	keyboards = chooses = 0;
	list = listSearch(p);
	CHECK(list && list->count == 2 && indexOf(list, GB "/Hacks/Mario Hack.gb") >= 0);
	freeList(list);
	p->tick(0); // the lists aren't there yet
	lists_ready = 1;
	p->tick(0);
	CHECK(keyboards == 0 && chooses == 0);

	// a game launched from the results keeps its path for post_return;
	// one launched elsewhere, or from a screen over the list, doesn't
	MosaicLaunch launch = { GB "/Hacks/Deep/Deeper/Deep Mario.gb", MOSAIC_ENTRY_ROM };
	CHECK(p->pre_launch(&launch) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(access(SE_LAST_FILE, F_OK) == 0);
	snprintf(current, sizeof(current), "%s", GB);
	CHECK(p->pre_launch(&launch) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(access(SE_LAST_FILE, F_OK) != 0);
	snprintf(current, sizeof(current), "%s", SEARCH);
	screen_name = "switcher";
	CHECK(p->pre_launch(&launch) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(access(SE_LAST_FILE, F_OK) != 0);
	screen_name = "gamelist";
	CHECK(p->pre_launch(&launch) == MOSAIC_LAUNCH_CONTINUE);
	p->quit();

	// back from it, stock opened the game's folder: back out to the root,
	// open the results again with the game selected, and don't ask
	snprintf(current, sizeof(current), "%s", GB "/Hacks/Deep/Deeper");
	lists_ready = 0;
	CHECK(p->init(&fake) == 0);
	p->post_return();
	CHECK(access(SE_LAST_FILE, F_OK) != 0);
	opens = 0;
	p->tick(0); // waits for the lists
	CHECK(opens == 0);
	lists_ready = 1;
	keyboards = chooses = 0;
	p->tick(0);
	CHECK(opens == 1);
	CHECK_STR(opened, SEARCH);
	CHECK_STR(current, SEARCH);
	CHECK(shown && selected_row == indexOf(shown, GB "/Hacks/Deep/Deeper/Deep Mario.gb") && selected_row == 1);
	p->tick(0);
	CHECK(keyboards == 0 && chooses == 0);
	// only once
	p->tick(0);
	CHECK(opens == 1);
	p->quit();

	// back to a screen over the list, such as the game switcher, it's left alone
	CHECK(p->init(&fake) == 0);
	CHECK(p->pre_launch(&launch) == MOSAIC_LAUNCH_CONTINUE);
	p->quit();
	snprintf(current, sizeof(current), "%s", GB "/Hacks/Deep/Deeper");
	CHECK(p->init(&fake) == 0);
	p->post_return();
	screen_name = "switcher";
	opens = 0;
	p->tick(0);
	CHECK(opens == 0);
	CHECK_STR(current, GB "/Hacks/Deep/Deeper");
	screen_name = "gamelist";
	p->tick(0);
	CHECK(opens == 0);
	freeList(shown);
	shown = NULL;

	// a changed folder is scanned again
	writeText(GB "/Mario Golf.gb", "");
	CHECK(system("touch -t 202001010000 '" GB "'") == 0);
	list = listSearch(p);
	CHECK(list && indexOf(list, GB "/Mario Golf.gb") >= 0);
	freeList(list);
	p->quit();

	// settings: no root row, no recents, a button
	fakeCfgSet("root", "off");
	fakeCfgSet("recents", "off");
	fakeCfgSet("button", "L2");
	CHECK(p->init(&fake) == 0);
	CHECK(bound_button == MOSAIC_BUTTON_L2);
	root = fake.list_new();
	p->root_entries(root);
	CHECK(root->count == 0);
	freeList(root);
	keyboards = chooses = 0;
	cfg_sets = 0;
	action_run(NULL);
	CHECK(keyboards == 1 && chooses == 0);
	keyboard_done("kirby", NULL);
	CHECK_STR(fakeCfgGet("query", ""), "kirby");
	CHECK_STR(fakeCfgGet("recent1", ""), "mario"); // not remembered
	p->quit();

	// without choose, the keyboard alone
	fakeCfgSet("recents", "on");
	fake.choose = NULL;
	CHECK(p->init(&fake) == 0);
	keyboards = chooses = 0;
	action_run(NULL);
	CHECK(keyboards == 1 && chooses == 0);
	p->quit();

	// without a keyboard there's nothing to do
	fake.keyboard = NULL;
	action_id[0] = '\0';
	CHECK(p->init(&fake) == 0);
	CHECK(action_id[0] == '\0');
	root = fake.list_new();
	p->root_entries(root);
	CHECK(root->count == 0);
	freeList(root);
	p->quit();
	fake.keyboard = fakeKeyboard;
	fake.choose = fakeChoose;

	(void)system("rm -rf '" CARD "'");
}

int main(void) {
	testFold();
	testNames();
	testRank();
	testSearch();
	testRecents();
	testTiming();
	testPlugin();
	CHECK_DONE();
}
