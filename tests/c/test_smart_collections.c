// The smart collections plugin: its model on its own, then the plugin
// against a fake host and a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "check.h"
#include "fakeproviders.h"
#include "fakesets.h"
#include "../../plugins/smart-collections/smart_collections.c"
#include "fakeindex.h"

struct MosaicEntry {
	char path[512];
	char name[128];
	int type;
};
struct MosaicList {
	MosaicEntry* items[64];
	int count;
};

static MosaicEntry* selected = NULL;
static int (*available[4])(const char*, void*);
static void (*run[4])(const char*, void*);
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
static char cfg_keys[24][32];
static char cfg_values[24][4096];
static int cfg_count = 0;
static const char* fakeCfgGet(const char* key, const char* fallback) {
	for (int i = 0; i < cfg_count; i++) {
		if (!strcmp(cfg_keys[i], key)) return cfg_values[i];
	}
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) {
	int i = 0;
	while (i < cfg_count && strcmp(cfg_keys[i], key)) i++;
	if (i == cfg_count) snprintf(cfg_keys[cfg_count++], 32, "%s", key);
	snprintf(cfg_values[i], 4096, "%s", value);
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
static void fakeListPush(MosaicList* l, MosaicEntry* e) { if (l->count < 64) l->items[l->count++] = e; }
static const MosaicEntry* fakeSelected(void) { return selected; }
static void fakeRedraw(void) {}
static void fakeReload(void) { reloads++; }
static int fakeRegister(const char* id, const char* label, int flags, int (*avail)(const char*, void*), void (*go)(const char*, void*), void* data) {
	(void)flags;
	(void)id; (void)label; (void)data;
	available[actions] = avail;
	run[actions] = go;
	actions++;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)trigger; (void)id;
	if (screen == MOSAIC_SCREEN_GAMELIST && button == MOSAIC_BUTTON_Y) bound_y++;
	return 0;
}

// Y on the game list: the first available binding runs, as the host does.
// How many lines text has.
static int countLines(const char* text) {
	int n = 0;
	for (; *text; text++) n += *text == '\n';
	return n;
}

static int pressY(void) {
	for (int i = 0; i < actions; i++) {
		if (available[i](NULL, NULL)) {
			run[i](NULL, NULL);
			return 1;
		}
	}
	return 0;
}

#define CARD TEST_SDCARD_PATH
#define FOLDER CARD "/Smart Collections"

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
	.set_register = fakeSetRegister,
	.set_unregister = fakeSetUnregister,
	.set_changed = fakeSetChanged,
	.set_names = fakeSetNames,
	.is_filtered = fakeIsFiltered,
	.subscribe = fakeIndexSubscribe,
	FAKE_INDEX_HOST,
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

// As the card index names it.
static const char* cleaned(const char* file_name) {
	static char out[256];
	Index_cleanName(file_name, out, sizeof(out));
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

	// grouping: a game can be in two franchises, and others aren't kept
	SMIndex index;
	SM_indexInit(&index);
	CHECK(SM_indexAdd(&index, &catalog, "/r/b.gb", "Super Mario Land", MOSAIC_ENTRY_ROM) == 1);
	CHECK(SM_indexAdd(&index, &catalog, "/r/a.wii", "Mario & Sonic at the Olympic Games", MOSAIC_ENTRY_ROM) == 2);
	CHECK(SM_indexAdd(&index, &catalog, "/r/c.gb", "Tetris", MOSAIC_ENTRY_ROM) == 1);
	CHECK(SM_indexAdd(&index, &catalog, "/r/d.gb", "Some Other Game", MOSAIC_ENTRY_ROM) == 0);
	CHECK(SM_indexAdd(&index, &catalog, "/r/e.gb", "dr. mario", MOSAIC_ENTRY_ROM) == 1);
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

// The franchises alone: every rule collection off.
static void rulesOff(void) {
	const char* keys[] = { "never", "recent", "long", "added", "decades", "genres" };
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) fakeCfgSet(keys[i], "off");
}

///////////////////////////////////////
// rules

static int bad_lines[16];
static char bad_reasons[16][128];
static int bad_count = 0;
static void collectError(int line, const char* reason, const char* text, void* data) {
	(void)text;
	(void)data;
	if (bad_count >= 16) return;
	bad_lines[bad_count] = line;
	snprintf(bad_reasons[bad_count++], sizeof(bad_reasons[0]), "%s", reason);
}

// played: the games with "played" in the path, all of them unplayed but the first
static int ruleFakeTest(const SEGame* game, int filter, const SEFilter* f, void* data) {
	(void)filter;
	(void)data;
	if (f->kind == SE_FILTER_UNPLAYED) return strstr(game->path, "played") == NULL;
	return 0;
}

static void testRulesModel(void) {
	SMCatalog catalog;
	SM_catalogInit(&catalog);
	SM_catalogAddBuiltin(&catalog);
	SMRules rules;
	SM_rulesInit(&rules);
	CHECK(SM_rulesAddBuiltin(&rules, &catalog, SM_RULE_NEVER, 0) == SM_ADDED);
	CHECK(SM_rulesAddBuiltin(&rules, &catalog, SM_RULE_RECENT, 7) == SM_ADDED);
	CHECK(SM_rulesAddBuiltin(&rules, &catalog, SM_RULE_LONG, 10) == SM_ADDED);
	CHECK(SM_rulesAddBuiltin(&rules, &catalog, SM_RULE_ADDED, 30) == SM_ADDED);
	CHECK(SM_rulesAddBuiltin(&rules, &catalog, SM_RULE_ADDED, 90) == SM_TAKEN);
	CHECK(SM_rulesAddBuiltin(&rules, &catalog, SM_RULE_USER, 1) == SM_TAKEN);
	CHECK(rules.count == 4);
	CHECK_STR(rules.items[0].name, "Never played");
	CHECK_STR(rules.items[0].query, "unplayed");
	CHECK_STR(rules.items[1].name, "Recently played");
	CHECK_STR(rules.items[1].query, "played:7");
	CHECK(rules.items[1].number == 7);
	CHECK_STR(rules.items[2].query, "hours:10");
	CHECK_STR(rules.items[3].name, "Recently added");
	CHECK_STR(rules.items[3].query, "added:30");

	// rules.txt: good lines, comments and blanks, and each bad line by number
	const char* text =
		"\xEF\xBB\xBF# my rules\n"
		"RPGs I haven't played = genre:rpg unplayed\n"
		"\n"
		"  Nineties GB  =  decade:90s system:gb \r\n"
		"no equals here\n"
		" = unplayed\n"
		"Empty =   \n"
		"Bad/Name = unplayed\n"
		".Hidden = unplayed\n"
		"Odd = played:x\n"
		"Many = fav: fav: fav: fav: fav: fav: fav: fav: fav:\n"
		"mario = unplayed\n"
		"never PLAYED = -unplayed\n"
		"rpgs i haven't played = unplayed\n"
		"A name far too long for a collection folder at sixty four bytes or more = unplayed\n"
		"Long query = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
		"Kirby games = -hours:1 = x"; // a second = is the query's, and no newline at the end
	bad_count = 0;
	CHECK(SM_rulesParse(&rules, &catalog, text, collectError, NULL) == 3);
	CHECK(rules.count == 7);
	CHECK_STR(rules.items[4].name, "RPGs I haven't played");
	CHECK_STR(rules.items[4].query, "genre:rpg unplayed");
	CHECK(rules.items[4].kind == SM_RULE_USER);
	CHECK_STR(rules.items[5].name, "Nineties GB");
	CHECK_STR(rules.items[5].query, "decade:90s system:gb");
	CHECK_STR(rules.items[6].query, "-hours:1 = x");
	int want_lines[] = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	const char* want_reasons[] = { "no = between the name and the query", "no name before the =", "no query after the =",
		"a name can't start with . or hold / or ,", "a name can't start with . or hold / or ,", "played:x isn't a filter",
		"more than 8 filters", "a collection already has the name", "a collection already has the name",
		"a collection already has the name", "the name is longer than 63 bytes", "the query is longer than 127 bytes" };
	CHECK(bad_count == 12);
	for (int i = 0; i < bad_count && i < 12; i++) {
		CHECK(bad_lines[i] == want_lines[i]);
		CHECK_STR(bad_reasons[i], want_reasons[i]);
	}
	CHECK(SM_ruleFind(&rules, "nineties gb") == 5);
	CHECK(SM_rulesParse(&rules, &catalog, NULL, collectError, NULL) == 0);

	// past SM_MAX_RULES, the rest are counted
	SMRules full;
	SM_rulesInit(&full);
	char name[32], line[64];
	for (int i = 0; i < SM_MAX_RULES + 2; i++) {
		snprintf(name, sizeof(name), "Rule %d", i);
		snprintf(line, sizeof(line), "%s = unplayed", name);
		int rc = SM_rulesParse(&full, NULL, line, NULL, NULL);
		CHECK(rc == (i < SM_MAX_RULES));
	}
	CHECK(full.count == SM_MAX_RULES && full.dropped == 2);

	// genres: their names, once each, sorted
	char out[SM_NAME_LEN];
	CHECK(SM_genreName("Action / Platform", out, sizeof(out)) == 0);
	CHECK_STR(out, "Action - Platform");
	CHECK(SM_genreName(" Shooter,Vertical ; Arcade| ", out, sizeof(out)) == 0);
	CHECK_STR(out, "Shooter - Vertical - Arcade");
	CHECK(SM_genreName("/ Puzzle /", out, sizeof(out)) == 0);
	CHECK_STR(out, "Puzzle");
	CHECK(SM_genreName(" / ", out, sizeof(out)) == -1);
	CHECK(SM_genreName("A genre whose name is far too long for one folder of the collections", out, sizeof(out)) == -1);
	SMRules genres;
	SM_rulesInit(&genres);
	CHECK(SM_genresAdd(&genres, "Platform") == SM_ADDED);
	CHECK(SM_genresAdd(&genres, "Action / Platform") == SM_ADDED);
	CHECK(SM_genresAdd(&genres, "platform") == 1); // there already, ignoring case
	CHECK(SM_genresAdd(&genres, "Beat 'em Up") == SM_ADDED);
	CHECK(SM_genresAdd(&genres, "beat-em-up") == 1);
	CHECK(SM_genresAdd(&genres, "!!") == -3);
	CHECK(genres.count == 3);
	SM_rulesSort(&genres);
	CHECK_STR(genres.items[0].name, "Action - Platform");
	CHECK_STR(genres.items[0].query, "genre:actionplatform");
	CHECK_STR(genres.items[1].name, "Beat 'em Up");
	CHECK_STR(genres.items[1].query, "genre:beatemup");
	CHECK(genres.items[2].kind == SM_RULE_GENRE);

	// decades, oldest first, and matching
	SEIndex games;
	SE_indexInit(&games);
	SE_indexAddInfo(&games, "/r/played b.gb", "Bravo", MOSAIC_ENTRY_ROM, "Platform", "1994");
	SE_indexAddInfo(&games, "/r/a.gb", "alpha", MOSAIC_ENTRY_ROM, "Action / Platform", "1989-01-01");
	SE_indexAddInfo(&games, "/r/c.gb", "Charlie", MOSAIC_ENTRY_ROM, "Beat 'em Up", "1991");
	SE_indexAddInfo(&games, "/r/d.gb", "Delta", MOSAIC_ENTRY_ROM, "", "unknown");
	int decades[4];
	CHECK(SM_decades(&games, decades, 4) == 2 && decades[0] == 1980 && decades[1] == 1990);
	CHECK(SM_decades(&games, decades, 1) == 1 && decades[0] == 1990); // the first found, when out of room

	SMRules some;
	SM_rulesInit(&some);
	SM_rulesAddBuiltin(&some, NULL, SM_RULE_NEVER, 0);
	SM_rulesAdd(&some, NULL, SM_RULE_DECADE, 1990, "1990s", "decade:1990s");
	SM_rulesAdd(&some, NULL, SM_RULE_GENRE, 0, "Platform", "genre:platform");
	SM_rulesAdd(&some, NULL, SM_RULE_USER, 0, "Nothing", "zzz");
	SMRuleHits hits;
	SM_ruleHitsInit(&hits);
	CHECK(SM_rulesMatch(&some, &games, ruleFakeTest, NULL, &hits) == 0);
	CHECK(hits.count[0] == 3); // by name: alpha, Charlie, Delta
	CHECK(hits.members[0][0] == 1 && hits.members[0][1] == 2 && hits.members[0][2] == 3);
	CHECK(hits.count[1] == 2 && hits.members[1][0] == 0 && hits.members[1][1] == 2);
	CHECK(hits.count[2] == 2); // Platform, and Action - Platform's words
	CHECK(hits.count[3] == 0 && hits.members[3] == NULL);
	unsigned long a = SM_hitsHash(&games, hits.members[1], hits.count[1]);
	CHECK(a == SM_hitsHash(&games, hits.members[1], hits.count[1]));
	CHECK(a != SM_hitsHash(&games, hits.members[2], hits.count[2]));
	CHECK(a != SM_hitsHash(&games, NULL, 0));
	SM_ruleHitsFree(&hits);
	CHECK(hits.count[0] == 0);
	SE_indexFree(&games);
}

// What matching rules costs on 5,000 games, with 6 built-ins, 4 decades
// and 24 genres.
static long long benchPlay(int kind, const char* path, void* data) {
	(void)kind;
	(void)data;
	return (long long)strlen(path) * 100000;
}
static int benchTest(const SEGame* game, int filter, const SEFilter* f, void* data) {
	(void)filter;
	(void)data;
	if (f->kind == SE_FILTER_UNPLAYED) return strlen(game->path) % 3 == 0;
	return SE_timeMatches(f, SE_playValue(game, 0, f->kind, benchPlay, NULL), 2000000000LL);
}

static void testRulesTiming(void) {
	SEIndex games;
	SE_indexInit(&games);
	SMRules genres;
	SM_rulesInit(&genres);
	for (int i = 0; i < 5000; i++) {
		char path[64], name[64], genre[32], year[8];
		snprintf(path, sizeof(path), "/r/System %d/Game number %d.gb", i % 12, i);
		snprintf(name, sizeof(name), "Game number %d", i);
		snprintf(genre, sizeof(genre), "Genre %c", 'A' + i % 24);
		snprintf(year, sizeof(year), "%d", 1980 + i % 40);
		SE_indexAddInfo(&games, path, name, MOSAIC_ENTRY_ROM, genre, year);
		SM_genresAdd(&genres, genre);
	}
	SMRules rules;
	SM_rulesInit(&rules);
	for (int kind = SM_RULE_NEVER; kind <= SM_RULE_ADDED; kind++) SM_rulesAddBuiltin(&rules, NULL, kind, 7);
	SM_rulesParse(&rules, NULL, "Unplayed RPGs = genre:rpg unplayed\nNineties = decade:90s -unplayed", NULL, NULL);
	int decades[8];
	int n = SM_decades(&games, decades, 8);
	for (int i = 0; i < n; i++) {
		char name[16], query[32];
		snprintf(name, sizeof(name), "%ds", decades[i]);
		snprintf(query, sizeof(query), "decade:%ds", decades[i]);
		SM_rulesAdd(&rules, NULL, SM_RULE_DECADE, decades[i], name, query);
	}
	SM_rulesSort(&genres);
	for (int i = 0; i < genres.count; i++) SM_rulesAdd(&rules, NULL, SM_RULE_GENRE, 0, genres.items[i].name, genres.items[i].query);
	CHECK(rules.count == 34);
	SMRuleHits hits;
	SM_ruleHitsInit(&hits);
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	CHECK(SM_rulesMatch(&rules, &games, benchTest, NULL, &hits) == 0);
	clock_gettime(CLOCK_MONOTONIC, &b);
	double ms = (double)(b.tv_sec - a.tv_sec) * 1000.0 + (double)(b.tv_nsec - a.tv_nsec) / 1e6;
	printf("  matching %d rules against 5000 games took %.2f ms\n", rules.count, ms);
	CHECK(hits.count[rules.count - 1] > 0);
	SM_ruleHitsFree(&hits);
	SE_indexFree(&games);
}

static MosaicEntry* named(MosaicList* list, const char* name) {
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
	fakeIndexDrop();
	rulesOff();

	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "smart-collections");
	CHECK(p->init(&fake) == 0);
	CHECK(actions == 3 && bound_y == 3);

	// one folder on the root
	MosaicList* root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 1);
	CHECK_STR(root->items[0]->path, FOLDER);
	CHECK_STR(root->items[0]->name, "Smart Collections");
	CHECK(root->items[0]->type == MOSAIC_ENTRY_DIR);
	CHECK(p->virtual_dir(CARD "/Roms") == NULL);

	// franchises with two games or more, by name
	MosaicList* top = p->virtual_dir(FOLDER);
	CHECK(top->count == 4);
	if (top->count == 4) {
		CHECK_STR(top->items[0]->name, "Final Fantasy");
		CHECK_STR(top->items[1]->name, "Mario");
		CHECK_STR(top->items[2]->name, "Street Fighter");
		CHECK_STR(top->items[3]->name, "Zelda");
		CHECK_STR(top->items[1]->path, FOLDER "/Mario");
		CHECK(top->items[1]->type == MOSAIC_ENTRY_DIR);
	}

	// across systems and subfolders, skipping hidden, disabled, mapped-away
	// games and systems without an emulator
	MosaicList* mario = p->virtual_dir(FOLDER "/Mario");
	CHECK(mario->count == 3);
	if (mario->count == 3) {
		CHECK_STR(mario->items[0]->name, "Super Mario Land");
		CHECK_STR(mario->items[1]->name, "Super Mario Land DX");
		CHECK_STR(mario->items[1]->path, CARD "/Roms/Game Boy (GB)/Hacks/Super Mario Land DX.gb");
		CHECK_STR(mario->items[2]->name, "Super Mario World");
		CHECK(mario->items[2]->type == MOSAIC_ENTRY_ROM);
	}

	// a multi-disc folder is one game, and map.txt names count
	MosaicList* ff = p->virtual_dir(FOLDER "/Final Fantasy");
	CHECK(ff->count == 2);
	if (ff->count == 2) {
		CHECK_STR(ff->items[1]->name, "Final Fantasy VII");
		CHECK_STR(ff->items[1]->path, CARD "/Roms/PlayStation (PS)/Final Fantasy VII (USA)");
		CHECK(ff->items[1]->type == MOSAIC_ENTRY_DIR);
	}
	MosaicList* sf = p->virtual_dir(FOLDER "/Street Fighter");
	CHECK(sf->count == 2);
	if (sf->count == 2) CHECK_STR(sf->items[0]->name, "Street Fighter II");

	// a set per franchise, holding what its folder lists
	CHECK(fakeSetFind("smart:Mario") >= 0 && fakeSetFind("smart:Zelda") >= 0);
	CHECK(fakeSetHas("smart:Mario", CARD "/Roms/Game Boy (GB)/Hacks/Super Mario Land DX.gb") == 1);
	CHECK(fakeSetHas("smart:Mario", CARD "/Roms/Game Boy (GB)/Tetris (World) (Rev 1).gb") == 0);
	CHECK_STR(fakeSetList("smart:Street Fighter"), CARD "/Roms/Arcade (FBN)/sf2.zip\n" CARD "/Roms/Arcade (FBN)/sf2ce.zip\n");

	// an unknown collection is an empty folder, not the launcher's to list
	MosaicList* none = p->virtual_dir(FOLDER "/Nothing");
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
	fake_set_changed[0] = '\0';
	sayYes();
	CHECK_STR(fake_set_changed, "smart:Mario\n");
	CHECK_STR(toasted, "Dismissed Mario");
	CHECK(reloads == 1);
	CHECK_STR(fakeCfgGet("dismissed", ""), "Mario");
	MosaicList* after = p->virtual_dir(FOLDER);
	CHECK(after->count == 3);
	CHECK(named(after, "Mario") == NULL);
	// a dismissed collection's set is empty
	CHECK_STR(fakeSetList("smart:Mario"), "");
	CHECK(fakeSetHas("smart:Mario", CARD "/Roms/Game Boy (GB)/Hacks/Super Mario Land DX.gb") == 0);

	// cancelling keeps it
	selected = top->items[3];
	CHECK(pressY());
	answer_yes = NULL;
	CHECK(p->virtual_dir(FOLDER)->count == 3);

	// still dismissed on the next start, and the folder stays on the root
	// even when every collection is dismissed
	fakeCfgSet("dismissed", "Mario,Zelda,Final Fantasy,Street Fighter");
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
	fake_set_changed[0] = '\0';
	sayYes();
	// each brought back is announced
	CHECK(countLines(fake_set_changed) == 4);
	CHECK_STR(fakeCfgGet("dismissed", "x"), "");
	CHECK(p->virtual_dir(FOLDER)->count == 4);
	CHECK(!pressY()); // nothing left to bring back

	// a game deleted since the index was read isn't listed
	CHECK(remove(CARD "/Roms/Super Nintendo (SFC)/Super Mario World (USA).sfc") == 0);
	CHECK(p->virtual_dir(FOLDER "/Mario")->count == 2);
	// and once the index has changed, its collection is grouped again
	reloads = 0;
	fakeIndexRebuild();
	CHECK(reloads == 1);
	CHECK(p->virtual_dir(FOLDER "/Mario")->count == 2);

	// settings: a bigger minimum, extra franchises, no built-ins
	p->quit();
	actions = 0;
	fakeCfgSet("min", "3");
	CHECK(p->init(&fake) == 0);
	CHECK(p->virtual_dir(FOLDER)->count == 0);
	root = fakeListNew();
	p->root_entries(root);
	CHECK(root->count == 0); // nothing big enough, so no folder

	p->quit();
	actions = 0;
	fakeCfgSet("min", "2");
	fakeCfgSet("builtin", "off");
	fakeCfgSet("extra", "Super, Legend");
	CHECK(p->init(&fake) == 0);
	MosaicList* mine = p->virtual_dir(FOLDER);
	CHECK(mine->count == 2);
	if (mine->count == 2) {
		CHECK_STR(mine->items[0]->name, "Legend");
		CHECK_STR(mine->items[1]->name, "Super");
	}
	p->quit();
}

static char keyboard_initial[128];
static void (*keyboard_done)(const char*, void*);
static void fakeKeyboard(const char* title, const char* initial, void (*done)(const char*, void*), void* data) {
	(void)title;
	(void)data;
	snprintf(keyboard_initial, sizeof(keyboard_initial), "%s", initial);
	keyboard_done = done;
}
static int tags_on = 1;
static int fakeHasCapability(const char* name) { return tags_on && !strcmp(name, "tags.copy"); }
static char copied[512];
static const char* copy_reply = "tag=Mario games\ngames=2";
static int fakeTagsCopy(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	snprintf(copied, sizeof(copied), "%s", args);
	snprintf(out, size, "%s", copy_reply);
	return 0;
}

static void testSaveAsTag(void) {
	cfg_count = 0;
	rulesOff();
	fake.keyboard = fakeKeyboard;
	fake.has_capability = fakeHasCapability;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	fake_provider_count = 0;
	fakeProvide("tags.copy", fakeTagsCopy);
	actions = 0;
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
	MosaicList* top = p->virtual_dir(FOLDER);
	MosaicEntry* mario = named(top, "Mario");
	CHECK(mario != NULL);
	if (!mario) return;

	// on a collection, Y has Dismiss and Save as tag, which the launcher
	// puts in a menu
	selected = mario;
	CHECK(available[0](NULL, NULL) && available[2](NULL, NULL));
	MosaicList* games = p->virtual_dir(FOLDER "/Mario");
	selected = games->items[0];
	CHECK(!available[2](NULL, NULL)); // not on a game
	selected = mario;

	run[2](NULL, NULL);
	CHECK_STR(keyboard_initial, "Mario");
	keyboard_done("Mario games", NULL);
	CHECK_STR(copied, "tag=Mario games\nset=smart:Mario");
	CHECK_STR(toasted, "Saved 2 games as Mario games");

	// what tags.copy refuses
	run[2](NULL, NULL);
	copy_reply = "error=exists";
	keyboard_done("Mario games", NULL);
	CHECK_STR(toasted, "A tag named Mario games already exists");
	run[2](NULL, NULL);
	copy_reply = "error=empty";
	keyboard_done("Mario games", NULL);
	CHECK_STR(toasted, "Couldn't save the tag");
	// cancelled
	run[2](NULL, NULL);
	copied[0] = '\0';
	keyboard_done(NULL, NULL);
	CHECK_STR(copied, "");

	// a dismissed collection, or tags off, has no Save as tag
	tags_on = 0;
	CHECK(!available[2](NULL, NULL));
	tags_on = 1;
	run[0](NULL, NULL);
	sayYes();
	CHECK(!available[2](NULL, NULL));
	p->quit();
	fake.keyboard = NULL;
	fake.has_capability = NULL;
	fake.call = NULL;
}

///////////////////////////////////////
// the plugin's rules

#define RGB CARD "/Roms/Game Boy (GB)"
#define RULES CARD "/.userdata/test/launcher/smart-collections/rules.txt"

// the play history: seconds played and when last, by name in the path
static const char* play_names[5] = { "Alpha", "Bravo", "Charlie", "Delta", "Echo" };
static long long play_seconds[5];
static long long play_last[5];
static int playIndex(const char* path) {
	for (int i = 0; i < 5; i++) {
		if (strstr(path, play_names[i])) return i;
	}
	return -1;
}
static long long fakePlayTime(const char* path) {
	int i = playIndex(path);
	return i < 0 ? 0 : play_seconds[i];
}
static long long fakeLastPlayed(const char* path) {
	int i = playIndex(path);
	return i < 0 ? 0 : play_last[i];
}
// as row-badges says: played when the history has it
static int playedService(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	snprintf(out, size, "played=%d", fakePlayTime(args) > 0);
	return 0;
}

static void (*profile_fn)(const char*, const char*, void*);
static void (*set_fn)(const char*, const char*, void*);
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	if (!strcmp(name, "launcher.profile_changed")) profile_fn = on_event;
	else if (!strcmp(name, "launcher.set_changed")) set_fn = on_event;
	return fakeIndexSubscribe(name, on_event, data);
}
static int fakeProfilePath(const char* file, char* out, size_t size) {
	snprintf(out, size, CARD "/.userdata/test/launcher/%s", file);
	return 0;
}

static long long clock_offset = 0;
static time_t fakeNow(time_t* out) {
	time_t t = time(NULL) + (time_t)clock_offset;
	if (out) *out = t;
	return t;
}

// favorites: Alpha and Echo, or none
static int favorites_on = 1;
static int favHas(const char* name, const char* path) {
	(void)name;
	return favorites_on && (strstr(path, "Alpha") || strstr(path, "Echo"));
}
static void favList(const char* name, void (*add)(const char* path, void* ctx), void* ctx) {
	(void)name;
	if (!favorites_on) return;
	add(RGB "/Alpha.gb", ctx);
	add(RGB "/Echo.gb", ctx);
}

static char logged[16384];
static void keepLog(const char* id, const char* m) {
	(void)id;
	size_t len = strlen(logged);
	snprintf(logged + len, sizeof(logged) - len, "%s\n", m);
}

static const char* names(MosaicList* list) {
	static char out[1024];
	out[0] = '\0';
	for (int i = 0; i < list->count; i++) {
		size_t len = strlen(out);
		snprintf(out + len, sizeof(out) - len, "%s%s", i ? ", " : "", list->items[i]->name);
	}
	return out;
}

static void testRulesPlugin(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" RGB "/.media' '" CARD "/.system/test/paks/Emus/GB.pak'"
		" '" CARD "/.userdata/test/launcher/smart-collections'") == 0);
	touch(CARD "/.system/test/paks/Emus/GB.pak/launch.sh");
	for (int i = 0; i < 5; i++) {
		char path[512];
		snprintf(path, sizeof(path), RGB "/%s.gb", play_names[i]);
		touch(path);
	}
	writeText(RGB "/.media/Alpha.info", "genre=Platform\nyear=1991\n");
	writeText(RGB "/.media/Bravo.info", "genre=platform\nyear=1994-02-01\n");
	writeText(RGB "/.media/Charlie.info", "genre=Puzzle\nyear=1989\n");
	writeText(RGB "/.media/Echo.info", "genre=Role-Playing / RPG\nyear=1999\n");
	writeText(RULES, "Unplayed platformers = genre:platform unplayed\nBroken = played:soon\nFavorites unplayed = fav: unplayed\n");
	fakeIndexDrop();
	long long now = (long long)time(NULL);
	memset(play_seconds, 0, sizeof(play_seconds));
	memset(play_last, 0, sizeof(play_last));
	play_seconds[1] = 12 * 3600; // Bravo, yesterday
	play_last[1] = now - 86400;
	play_seconds[2] = 2 * 3600; // Charlie, 20 days ago
	play_last[2] = now - 20 * 86400;

	cfg_count = 0;
	fakeCfgSet("min", "1");
	fake.log = keepLog;
	logged[0] = '\0';
	fake.subscribe = fakeSubscribe;
	fake.profile_path = fakeProfilePath;
	fake.play_time = fakePlayTime;
	fake.last_played = fakeLastPlayed;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	fake_provider_count = 0;
	fakeProvide("games.played", playedService);
	fake_set_count = 0;
	fake.set_has = fakeSetHas;
	fakeSetRegister("favorites", favHas, favList);
	favorites_on = 1;
	now_fn = fakeNow;
	clock_offset = 0;
	actions = 0;
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
	CHECK(profile_fn != NULL && set_fn != NULL && p->post_return != NULL);
	// the bad line is logged by its number, and the built-ins' and the
	// user's sets are there from the start
	CHECK(strstr(logged, RULES " line 2 left out, played:soon isn't a filter: Broken = played:soon\n") != NULL);
	CHECK(strstr(logged, "2 rules from " RULES "\n") != NULL);
	CHECK(fakeSetFind("smart:Never played") >= 0 && fakeSetFind("smart:Unplayed platformers") >= 0);
	CHECK(fakeSetFind("smart:Broken") < 0);

	// the rules' collections first, in order, labelled, then the decades and genres
	MosaicList* top = p->virtual_dir(FOLDER);
	CHECK_STR(names(top), "Never played, Played in the last 7 days, Over 10 hours, Added in the last 30 days, "
		"Unplayed platformers, Favorites unplayed, 1980s, 1990s, Platform, Puzzle, Role-Playing - RPG");
	CHECK(top->count == 11);
	if (top->count != 11) return;
	CHECK_STR(top->items[0]->path, FOLDER "/Never played");
	CHECK_STR(top->items[1]->path, FOLDER "/Recently played");
	CHECK_STR(top->items[2]->path, FOLDER "/Long played");
	CHECK(strstr(logged, "rule Never played (unplayed): 3 games\n") != NULL);
	CHECK(strstr(logged, "matched 11 rules, 5 of them decades and genres, against 5 games in ") != NULL);
	CHECK_STR(names(p->virtual_dir(FOLDER "/Never played")), "Alpha, Delta, Echo");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Recently played")), "Bravo");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Long played")), "Bravo");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Recently added")), "Alpha, Bravo, Charlie, Delta, Echo");
	CHECK_STR(names(p->virtual_dir(FOLDER "/1990s")), "Alpha, Bravo, Echo");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Platform")), "Alpha, Bravo");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Unplayed platformers")), "Alpha");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Favorites unplayed")), "Alpha, Echo");
	CHECK_STR(fakeSetList("smart:Never played"), RGB "/Alpha.gb\n" RGB "/Delta.gb\n" RGB "/Echo.gb\n");
	CHECK(fakeSetHas("smart:1990s", RGB "/Echo.gb") == 1);
	CHECK(fakeSetHas("smart:1990s", RGB "/Charlie.gb") == 0);
	CHECK(fakeSetFind("smart:Role-Playing - RPG") >= 0); // registered once found

	// played: Alpha, just now: the play-based sets that changed are announced
	play_seconds[0] = 1800;
	play_last[0] = now;
	fake_set_changed[0] = '\0';
	p->post_return();
	CHECK_STR(fake_set_changed, "smart:Never played\nsmart:Recently played\nsmart:Unplayed platformers\nsmart:Favorites unplayed\n");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Recently played")), "Alpha, Bravo");
	// gone empty, it isn't listed
	CHECK(named(p->virtual_dir(FOLDER), "Unplayed platformers") == NULL);
	fake_set_changed[0] = '\0';
	p->post_return(); // nothing changed
	CHECK_STR(fake_set_changed, "");

	// a week on, without anything played: Bravo's yesterday is too long ago
	clock_offset = 7 * 86400;
	p->virtual_dir(FOLDER);
	CHECK_STR(fake_set_changed, "smart:Recently played\n");
	CHECK_STR(names(p->virtual_dir(FOLDER "/Recently played")), "Alpha");
	// and 40 days on, the games were put on the card too long ago
	clock_offset = 40 * 86400;
	fake_set_changed[0] = '\0';
	p->virtual_dir(FOLDER);
	CHECK(strstr(fake_set_changed, "smart:Recently added\n") != NULL);
	CHECK(named(p->virtual_dir(FOLDER), "Added in the last 30 days") == NULL);
	clock_offset = 0;
	fake_set_changed[0] = '\0';
	p->virtual_dir(FOLDER); // an hour has gone by, as far as it knows
	fake_set_changed[0] = '\0';

	// a set a rule reads: favorites gone
	favorites_on = 0;
	set_fn("launcher.set_changed", "name=smart:Never played", NULL); // its own, left alone
	CHECK_STR(fake_set_changed, "");
	set_fn("launcher.set_changed", "name=favorites", NULL);
	CHECK_STR(fake_set_changed, "smart:Favorites unplayed\n");
	favorites_on = 1;

	// another profile, which has played nothing
	memset(play_seconds, 0, sizeof(play_seconds));
	memset(play_last, 0, sizeof(play_last));
	fake_set_changed[0] = '\0';
	profile_fn("launcher.profile_changed", "profile=guest", NULL);
	CHECK(strstr(fake_set_changed, "smart:Never played\n") != NULL);
	CHECK(strstr(fake_set_changed, "smart:Recently played\n") != NULL);
	CHECK(strstr(fake_set_changed, "smart:Long played\n") != NULL);
	CHECK_STR(names(p->virtual_dir(FOLDER "/Never played")), "Alpha, Bravo, Charlie, Delta, Echo");
	CHECK(named(p->virtual_dir(FOLDER), "Over 10 hours") == NULL);

	// Y dismisses a rule's collection, by its own name
	top = p->virtual_dir(FOLDER);
	selected = named(top, "Never played");
	CHECK(pressY());
	CHECK_STR(asked, "Dismiss Never played?");
	fake_set_changed[0] = '\0';
	sayYes();
	CHECK_STR(fake_set_changed, "smart:Never played\n");
	CHECK_STR(toasted, "Dismissed Never played");
	CHECK_STR(fakeCfgGet("dismissed", ""), "Never played");
	CHECK(named(p->virtual_dir(FOLDER), "Never played") == NULL);
	CHECK_STR(fakeSetList("smart:Never played"), "");
	fake_set_changed[0] = '\0';
	p->post_return();
	CHECK_STR(fake_set_changed, ""); // not announced again

	// Save as tag on a rule's collection starts from the name it shows
	fake.keyboard = fakeKeyboard;
	fake.has_capability = fakeHasCapability;
	tags_on = 1;
	fakeProvide("tags.copy", fakeTagsCopy);
	copy_reply = "tag=Fresh\ngames=5";
	selected = named(p->virtual_dir(FOLDER), "Added in the last 30 days");
	CHECK(selected != NULL && available[2](NULL, NULL));
	run[2](NULL, NULL);
	CHECK_STR(keyboard_initial, "Added in the last 30 days");
	keyboard_done("Fresh", NULL);
	CHECK_STR(copied, "tag=Fresh\nset=smart:Recently added");
	CHECK_STR(toasted, "Saved 5 games as Fresh");

	// rules.txt changed: read again at the next match
	writeText(RULES, "Puzzles = genre:puzzle\n");
	p->post_return();
	CHECK(named(p->virtual_dir(FOLDER), "Puzzles") != NULL);
	CHECK(named(p->virtual_dir(FOLDER), "Unplayed platformers") == NULL);
	p->quit();

	// settings: each kind off, other numbers, and the smallest collection
	actions = 0;
	fakeCfgSet("dismissed", "");
	fakeCfgSet("min", "2");
	fakeCfgSet("never", "off");
	fakeCfgSet("decades", "off");
	fakeCfgSet("genres", "off");
	fakeCfgSet("recent_days", "30");
	fakeCfgSet("long_hours", "1");
	fakeCfgSet("added_days", "7");
	play_seconds[1] = 12 * 3600;
	play_last[1] = now - 86400;
	play_seconds[2] = 2 * 3600;
	play_last[2] = now - 20 * 86400;
	CHECK(p->init(&fake) == 0);
	CHECK_STR(names(p->virtual_dir(FOLDER)), "Played in the last 30 days, Over 1 hour, Added in the last 7 days");
	fakeCfgSet("added", "off");
	fakeCfgSet("recent", "off");
	fakeCfgSet("long", "off");
	p->quit();
	actions = 0;
	CHECK(p->init(&fake) == 0);
	CHECK_STR(names(p->virtual_dir(FOLDER)), "");
	p->quit();

	now_fn = time;
	fake.log = fakeLog;
	fake.subscribe = fakeIndexSubscribe;
	fake.profile_path = NULL;
	fake.play_time = NULL;
	fake.last_played = NULL;
	fake.call = NULL;
	fake.event_value = NULL;
	fake.keyboard = NULL;
	fake.has_capability = NULL;
	fake_set_count = 0;
}

int main(void) {
	testModel();
	testRulesModel();
	testRulesTiming();
	testPlugin();
	testSaveAsTag();
	testRulesPlugin();
	CHECK_DONE();
}
