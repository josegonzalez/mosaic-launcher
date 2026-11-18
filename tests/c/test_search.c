// The search plugin: its model on its own, how long a search of a few
// thousand games takes, then the plugin against a fake host on a fixture
// card.

#include <stdlib.h>
#include <sys/time.h>
#include "check.h"
#include "fakeproviders.h"
#include "fakesets.h"
#include "../../plugins/search/search.c"
#include "fakeindex.h"

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

// SE_search for a query with no filters to pass.
static int find(const SEIndex* index, const char* text, SEHit* hits) {
	SEQuery query;
	SE_parse(text, &query);
	return SE_search(index, &query, NULL, NULL, hits);
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

	SEHit hits[9];
	int count = find(&index, "  MARIO ", hits);
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
	CHECK(find(&index, "pokemon", hits) == 1);
	CHECK(find(&index, "POK\xc3\x89MON red", hits) == 1);
	CHECK(find(&index, "red pok", hits) == 1);
	CHECK(find(&index, "zelda", hits) == 1 && index.games[hits[0].game].type == MOSAIC_ENTRY_DIR);
	CHECK(find(&index, "luigi", hits) == 0);
	CHECK(find(&index, "", hits) == 0);
	CHECK(find(&index, "!?", hits) == 0);

	// the order results keep
	count = find(&index, "mario", hits);
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
	CHECK(index.game_count == 0 && index.games == NULL);
}

static void testParse(void) {
	SEQuery q;
	SE_parse("  fav: System:gb  mario -TAG:beaten land unplayed -unplayed tag: -x ", &q);
	CHECK_STR(q.words, "mario land tag: -x");
	CHECK(q.filter_count == 5);
	CHECK(q.filters[0].kind == SE_FILTER_FAV && !q.filters[0].negate);
	CHECK(q.filters[1].kind == SE_FILTER_SYSTEM && !q.filters[1].negate);
	CHECK_STR(q.filters[1].arg, "gb");
	CHECK(q.filters[2].kind == SE_FILTER_TAG && q.filters[2].negate);
	CHECK_STR(q.filters[2].arg, "beaten");
	CHECK(q.filters[3].kind == SE_FILTER_UNPLAYED && !q.filters[3].negate);
	CHECK(q.filters[4].kind == SE_FILTER_UNPLAYED && q.filters[4].negate);

	SE_parse("", &q);
	CHECK(!q.words[0] && !q.filter_count);
	SE_parse("- fav:x favorite", &q); // a lone '-', and fav: with more after it, are words
	CHECK_STR(q.words, "- fav:x favorite");
	CHECK(!q.filter_count);
	// past SE_FILTERS_MAX filters, the rest are words
	SE_parse("fav: fav: fav: fav: fav: fav: fav: fav: unplayed", &q);
	CHECK(q.filter_count == SE_FILTERS_MAX);
	CHECK_STR(q.words, "unplayed");

	char out[64];
	SE_compact("Beat 'em Up!", out, sizeof(out));
	CHECK_STR(out, "beatemup");
	CHECK(SE_tagMatches("tags:Beat 'em Up", "beatemup"));
	CHECK(SE_tagMatches("tags:Beaten", "BEATEN"));
	CHECK(!SE_tagMatches("tags:Beaten", "beat"));
	CHECK(!SE_tagMatches("favorites", "favorites"));
	CHECK(!SE_tagMatches("smart:Mario", "mario"));

	CHECK(SE_systemMatches("/r", "/r/Game Boy (GB)/Tetris.gb", "gb"));
	CHECK(SE_systemMatches("/r", "/r/Game Boy (GB)/Hacks/Tetris.gb", "GB"));
	CHECK(SE_systemMatches("/r", "/r/Game Boy (GB)/Tetris.gb", "gameboy"));
	CHECK(SE_systemMatches("/r", "/r/Game Boy (GB)/Tetris.gb", "game"));
	CHECK(!SE_systemMatches("/r", "/r/Game Boy Color (GBC)/Tetris.gbc", "gb"));
	CHECK(!SE_systemMatches("/r", "/r/Game Boy (GB)/Tetris.gb", "boy"));
	CHECK(SE_systemMatches("/r", "/r/NES/Mario.nes", "nes"));
	CHECK(!SE_systemMatches("/r", "/r/Tetris.gb", "gb"));
	CHECK(!SE_systemMatches("/r", "/x/Game Boy (GB)/Tetris.gb", "gb"));
	CHECK(!SE_systemMatches("/r", "/r/Game Boy (GB)/Tetris.gb", ""));

	SEPaths paths;
	SE_pathsInit(&paths);
	CHECK(!SE_pathsHas(&paths, "/a"));
	CHECK(SE_pathsAdd(&paths, "/c") == 0 && SE_pathsAdd(&paths, "/a") == 0);
	CHECK(SE_pathsHas(&paths, "/a") && SE_pathsHas(&paths, "/c") && !SE_pathsHas(&paths, "/b"));
	CHECK(SE_pathsAdd(&paths, "/b") == 0);
	CHECK(SE_pathsHas(&paths, "/b"));
	SE_pathsFree(&paths);
	CHECK(paths.count == 0 && !SE_pathsHas(&paths, "/a"));
}

// Favorites are the games with "fav" in the path, and played ones "played";
// counts how many unplayed filters were asked.
static int unplayed_asked = 0;
static int fakeTest(const SEGame* game, int filter, const SEFilter* f, void* data) {
	(void)filter;
	(void)data;
	if (f->kind == SE_FILTER_FAV) return strstr(game->path, "fav") != NULL;
	if (f->kind == SE_FILTER_UNPLAYED) {
		unplayed_asked++;
		return strstr(game->path, "played") == NULL;
	}
	if (f->kind == SE_FILTER_SYSTEM) return SE_systemMatches("/r", game->path, f->arg);
	return 0;
}

static int findWith(const SEIndex* index, const char* text, SEHit* hits) {
	SEQuery query;
	SE_parse(text, &query);
	return SE_search(index, &query, fakeTest, NULL, hits);
}

static void testFilters(void) {
	SEIndex index;
	SE_indexInit(&index);
	SE_indexAdd(&index, "/r/Game Boy (GB)/fav Mario.gb", "Mario", MOSAIC_ENTRY_ROM);
	SE_indexAdd(&index, "/r/Game Boy (GB)/played fav Zelda.gb", "Zelda", MOSAIC_ENTRY_ROM);
	SE_indexAdd(&index, "/r/NES/fav Mario.nes", "Mario Bros", MOSAIC_ENTRY_ROM);
	SE_indexAdd(&index, "/r/NES/Tetris.nes", "Tetris", MOSAIC_ENTRY_ROM);
	SE_indexAdd(&index, "/r/Game Boy (GB)/played Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM);
	SEHit hits[5];

	CHECK(findWith(&index, "fav: system:gb mario", hits) == 1);
	CHECK_STR(index.games[hits[0].game].path, "/r/Game Boy (GB)/fav Mario.gb");
	CHECK(findWith(&index, "fav: mario", hits) == 2);
	CHECK(findWith(&index, "-fav: tetris", hits) == 2);
	CHECK(findWith(&index, "-fav: unplayed", hits) == 1);
	CHECK_STR(index.games[hits[0].game].path, "/r/NES/Tetris.nes");
	CHECK(findWith(&index, "-unplayed", hits) == 2);
	// filters alone list every game passing them, by name
	CHECK(findWith(&index, "system:gb", hits) == 3);
	CHECK_STR(index.games[hits[0].game].path, "/r/Game Boy (GB)/fav Mario.gb");
	CHECK_STR(index.games[hits[1].game].path, "/r/Game Boy (GB)/played Tetris.gb");
	CHECK(hits[0].rank == SE_RANK_EXACT);
	// unplayed is asked only of games passing the rest
	unplayed_asked = 0;
	CHECK(findWith(&index, "unplayed system:nes mario", hits) == 1);
	CHECK(unplayed_asked == 1);
	// with no way to test filters, nothing passes
	CHECK(find(&index, "fav: mario", hits) == 0);
	CHECK(find(&index, "mario", hits) == 2);
	SE_indexFree(&index);
}

// genre:, year: and decade:, from the index alone
static void testInfoFilters(void) {
	CHECK(SE_year("1991") == 1991);
	CHECK(SE_year("1991-03-21") == 1991);
	CHECK(SE_year("03/21/1994") == 1994);
	CHECK(SE_year("19911") == 0); // five digits aren't a year
	CHECK(SE_year("1850") == 0);
	CHECK(SE_year("") == 0 && SE_year(NULL) == 0 && SE_year("unknown") == 0);

	SEQuery q;
	SE_parse("genre:rpg -year:1990-1994 decade:90s played:7 hours:10 added:30 x", &q);
	CHECK(q.filter_count == 6 && !q.bad[0] && !q.dropped);
	CHECK(q.filters[0].kind == SE_FILTER_GENRE);
	CHECK_STR(q.filters[0].arg, "rpg");
	CHECK(q.filters[1].kind == SE_FILTER_YEAR && q.filters[1].negate && q.filters[1].from == 1990 && q.filters[1].to == 1994);
	CHECK(q.filters[2].kind == SE_FILTER_YEAR && q.filters[2].from == 1990 && q.filters[2].to == 1999);
	CHECK(q.filters[3].kind == SE_FILTER_PLAYED && q.filters[3].from == 7);
	CHECK(q.filters[4].kind == SE_FILTER_HOURS && q.filters[4].from == 10);
	CHECK(q.filters[5].kind == SE_FILTER_ADDED && q.filters[5].from == 30);
	CHECK_STR(q.words, "x");
	// each way to name a decade
	const char* decades[][2] = { { "1990s", "1990" }, { "1990", "1990" }, { "90S", "1990" }, { "00s", "2000" },
		{ "10s", "2010" }, { "2020s", "2020" } };
	for (size_t i = 0; i < sizeof(decades) / sizeof(decades[0]); i++) {
		char text[32];
		snprintf(text, sizeof(text), "DECADE:%s", decades[i][0]);
		SE_parse(text, &q);
		CHECK(q.filter_count == 1 && q.filters[0].from == atoi(decades[i][1]) && q.filters[0].to == atoi(decades[i][1]) + 9);
	}
	// what isn't one is a word, and the first is kept
	const char* bad[] = { "played:x", "played:0", "hours:", "added:-3", "added:100001", "year:94", "year:1995-1990",
		"year:1990-", "decade:1995", "decade:9", "decade:s", "genre:", "tag:" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		SE_parse(bad[i], &q);
		CHECK(q.filter_count == 0);
		CHECK_STR(q.bad, bad[i]);
		CHECK_STR(q.words, bad[i]);
	}
	SE_parse("played:x hours:y", &q);
	CHECK_STR(q.bad, "played:x");
	SE_parse("fav: fav: fav: fav: fav: fav: fav: fav: genre:a decade:90s", &q);
	CHECK(q.filter_count == SE_FILTERS_MAX && q.dropped == 2);
	CHECK(!SE_filterNeedsTest(SE_FILTER_GENRE) && !SE_filterNeedsTest(SE_FILTER_YEAR));
	CHECK(SE_filterNeedsTest(SE_FILTER_PLAYED) && SE_filterNeedsTest(SE_FILTER_UNPLAYED));

	SEIndex index;
	SE_indexInit(&index);
	CHECK(SE_indexAddInfo(&index, "/r/a.gb", "Alpha", MOSAIC_ENTRY_ROM, "Role-Playing (RPG)", "1991") == 0);
	CHECK(SE_indexAddInfo(&index, "/r/b.gb", "Bravo", MOSAIC_ENTRY_ROM, "Action RPG", "1999-12-01") == 0);
	CHECK(SE_indexAddInfo(&index, "/r/c.gb", "Charlie", MOSAIC_ENTRY_ROM, "Beat 'em Up / Action", "2004") == 0);
	CHECK(SE_indexAddInfo(&index, "/r/d.gb", "Delta", MOSAIC_ENTRY_ROM, "", "") == 0);
	CHECK(SE_indexAddInfo(&index, "/r/e.gb", "Echo", MOSAIC_ENTRY_ROM, "Platformer", NULL) == 0);
	CHECK_STR(index.games[0].genre, "role playing rpg");
	CHECK(index.games[3].genre == NULL && index.games[3].year == 0);
	CHECK(index.games[1].year == 1999);
	SEHit hits[5];
	// no test needed for them
	CHECK(find(&index, "genre:rpg", hits) == 2);
	CHECK(find(&index, "genre:role-playing", hits) == 1);
	CHECK(find(&index, "genre:beatemup", hits) == 0); // a part, compacted, is only words
	CHECK(find(&index, "genre:beatemupaction", hits) == 1); // the whole genre, compacted
	CHECK(find(&index, "genre:act", hits) == 2); // words start words
	CHECK(find(&index, "genre:latform", hits) == 0); // not inside one
	CHECK(find(&index, "-genre:rpg", hits) == 3); // no genre isn't an RPG
	CHECK(find(&index, "decade:1990s", hits) == 2);
	CHECK(find(&index, "year:1991", hits) == 1);
	CHECK(find(&index, "year:1992-2010", hits) == 2);
	CHECK(find(&index, "-decade:90s", hits) == 3); // with no year too
	CHECK(find(&index, "genre:rpg decade:90s bravo", hits) == 1);
	CHECK_STR(index.games[hits[0].game].name, "Bravo");
	SE_indexFree(&index);
}

// played:, hours: and added:, from a test's answers
static long long play_values[4];
static int play_asked = 0;
static long long askValue(int kind, const char* path, void* data) {
	(void)data;
	play_asked++;
	if (strstr(path, ".m3u")) return kind == SE_FILTER_HOURS ? 50 * 3600 : 0;
	if (strstr(path, ".cue")) return 1000;
	int i = path[strlen(path) - 1] - '0';
	return i >= 0 && i < 4 && kind != SE_FILTER_HOURS ? play_values[i] : 0;
}

static int timeTest(const SEGame* game, int filter, const SEFilter* f, void* data) {
	(void)filter;
	return SE_timeMatches(f, SE_playValue(game, game->type == MOSAIC_ENTRY_DIR, f->kind, askValue, NULL), *(long long*)data);
}

static void testTimeFilters(void) {
	long long now = 1800000000LL;
	SEFilter played = { .kind = SE_FILTER_PLAYED, .from = 7 };
	CHECK(SE_timeMatches(&played, now - 7 * 86400, now));
	CHECK(!SE_timeMatches(&played, now - 7 * 86400 - 1, now));
	CHECK(!SE_timeMatches(&played, 0, now) && !SE_timeMatches(&played, -1, now));
	SEFilter hours = { .kind = SE_FILTER_HOURS, .from = 10 };
	CHECK(SE_timeMatches(&hours, 10 * 3600 + 1, now));
	CHECK(!SE_timeMatches(&hours, 10 * 3600, now)); // more than, not as much as
	SEFilter added = { .kind = SE_FILTER_ADDED, .from = 30 };
	CHECK(SE_timeMatches(&added, now - 86400, now) && !SE_timeMatches(&added, now - 31 * 86400, now));
	SEFilter fav = { .kind = SE_FILTER_FAV };
	CHECK(!SE_timeMatches(&fav, now, now));

	SEIndex index;
	SE_indexInit(&index);
	SE_indexAdd(&index, "/r/g0", "Zero", MOSAIC_ENTRY_ROM);
	SE_indexAdd(&index, "/r/g1", "One", MOSAIC_ENTRY_ROM);
	SE_indexAdd(&index, "/r/g2", "Two", MOSAIC_ENTRY_ROM);
	SE_indexAdd(&index, "/r/Disc Game", "Disc Game", MOSAIC_ENTRY_DIR);
	play_values[0] = now - 86400;      // played yesterday...
	play_values[1] = now - 30 * 86400; // ...a month ago...
	play_values[2] = 0;                // ...never
	SEHit hits[4];
	SEQuery q;
	SE_parse("played:7", &q);
	CHECK(SE_search(&index, &q, timeTest, &now, hits) == 1);
	CHECK_STR(index.games[hits[0].game].name, "Zero");
	SE_parse("-played:7", &q);
	CHECK(SE_search(&index, &q, timeTest, &now, hits) == 3);
	// a game folder's hours are its .m3u's, as stock plays it
	SE_parse("hours:10", &q);
	CHECK(SE_search(&index, &q, timeTest, &now, hits) == 1);
	CHECK_STR(index.games[hits[0].game].name, "Disc Game");
	// asked after the cheaper filters, of the games passing them
	play_asked = 0;
	SE_parse("played:7 zero", &q);
	CHECK(SE_search(&index, &q, timeTest, &now, hits) == 1 && play_asked == 1);
	SE_parse("-genre:x played:7", &q);
	CHECK(SE_search(&index, &q, NULL, NULL, hits) == 0); // played: needs a test
	SE_parse("-genre:x", &q);
	CHECK(SE_search(&index, &q, NULL, NULL, hits) == 4);
	SE_indexFree(&index);
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
		for (int r = 0; r < 10; r++) found = find(&index, queries[q], hits);
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
static int (*action_available)(const char*, void*);
static void (*action_run)(const char*, void*);
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
static int fakeRegister(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
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
static int fakeRunAction(const char* id, const char* path) {
	(void)path;
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
	.subscribe = fakeIndexSubscribe,
	FAKE_INDEX_HOST,
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
	fakeIndexDrop();
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "search");
	CHECK(p->init(&fake) == 0);
	CHECK_STR(action_id, "search.open");
	CHECK(action_available && action_available(NULL, NULL));
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
	action_run(NULL, NULL); // the action asks from anywhere: recents first now
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
	action_run(NULL, NULL);
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
	action_run(NULL, NULL);
	choice_pick(-1, NULL);
	CHECK(runs == 0);
	action_run(NULL, NULL);
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

	// a game added shows once the index has it, which lists the folder again
	writeText(GB "/Mario Golf.gb", "");
	list = listSearch(p);
	CHECK(list && indexOf(list, GB "/Mario Golf.gb") < 0);
	freeList(list);
	snprintf(current, sizeof(current), "%s", SEARCH);
	reloads = 0;
	fakeIndexRebuild();
	CHECK(reloads == 1);
	list = listSearch(p);
	CHECK(list && indexOf(list, GB "/Mario Golf.gb") >= 0);
	freeList(list);
	snprintf(current, sizeof(current), "%s", GB);
	p->quit();

	// before the index is ready nothing is found, and nothing said
	fakeIndexDrop();
	fake_index_ready = 0;
	CHECK(p->init(&fake) == 0);
	toasted[0] = '\0';
	keyboard_done("mario", NULL);
	CHECK(toasted[0] == '\0');
	list = listSearch(p);
	CHECK(list && list->count == 0);
	freeList(list);
	fakeIndexRebuild();
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
	action_run(NULL, NULL);
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
	action_run(NULL, NULL);
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

///////////////////////////////////////
// the filters against fake sets

static const char* fav_paths[] = { GB "/Kirby.gb", PS "/Crash (USA)/Crash (USA).cue" };
static const char* tag_paths[] = { GB "/Tetris (World) (Rev 1).gb" };

static void listPaths(const char* const* paths, int count, void (*add)(const char*, void*), void* ctx) {
	for (int i = 0; i < count; i++) add(paths[i], ctx);
}
static void favList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	listPaths(fav_paths, 2, add, ctx);
}
static void tagList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	listPaths(tag_paths, 1, add, ctx);
}
static int noHas(const char* name, const char* path) {
	(void)name;
	(void)path;
	return 0;
}
static int fakeSetListCall(const char* name, void (*each)(const char*, void*), void* data) {
	int i = fakeSetFind(name);
	if (i < 0) return -1;
	if (each) fake_set_list[i](name, each, data);
	return 0;
}

// Tetris is the one played game.
static int playedFn(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	snprintf(out, size, "played=%d", strstr(args, "Tetris") != NULL);
	return 0;
}

static void (*set_changed_fn)(const char*, const char*, void*);
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	if (!strcmp(name, "launcher.set_changed")) set_changed_fn = on_event;
	return fakeIndexSubscribe(name, on_event, data);
}

static const char* recent_list[1];
static int recent_total = 0;
static int fakeRecentsCount(void) { return recent_total; }
static const char* fakeRecentPath(int i) { return i >= 0 && i < recent_total ? recent_list[i] : NULL; }

// The results for text, as the folder lists them.
static MosaicList* results(const char* text) {
	hits_valid = 0; // recents and the played games change only across launches, but here between searches
	run(text);
	return listSearch(&plugin);
}

static void checkCount(const char* text, int want) {
	MosaicList* list = results(text);
	if (list->count != want) printf("  \"%s\": %d results, want %d\n", text, list->count, want);
	CHECK(list->count == want);
	freeList(list);
}

// Kirby was played yesterday, for 11 hours in all; the Crash folder's
// .cue a month ago.
static long long fakePlayTime(const char* path) {
	return strstr(path, "Kirby") ? 11 * 3600 : 0;
}
static long long fakeLastPlayed(const char* path) {
	if (strstr(path, "Kirby")) return (long long)time(NULL) - 86400;
	return strstr(path, "Crash (USA).cue") ? (long long)time(NULL) - 30 * 86400 : 0;
}

static void testPluginFilters(void) {
	makeCard();
	CHECK(system("mkdir -p '" GB "/.media'") == 0);
	writeText(GB "/.media/Kirby.info", "genre=Platform\nyear=1992\n");
	writeText(GB "/.media/Tetris (World) (Rev 1).info", "genre=Puzzle\nyear=1989-06-14\n");
	fakeIndexDrop();
	cfg_count = 0;
	fakeCfgSet("recents", "off");
	fake.set_list = fakeSetListCall;
	fake.set_names = fakeSetNames;
	fake.subscribe = fakeSubscribe;
	fake.event_value = fakeEventValue;
	fake.call = fakeCall;
	fake.recents_count = fakeRecentsCount;
	fake.recent_path = fakeRecentPath;
	fake_set_count = 0;
	fakeSetRegister("favorites", noHas, favList);
	fakeSetRegister("tags:Beat 'em Up", noHas, tagList);
	fake_provider_count = 0;
	fakeProvide("games.played", playedFn);
	set_changed_fn = NULL;

	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
	CHECK(set_changed_fn != NULL);

	// a game folder is in a set by its .cue
	MosaicList* list = results("fav:");
	CHECK(list->count == 2);
	CHECK(indexOf(list, GB "/Kirby.gb") >= 0 && indexOf(list, PS "/Crash (USA)") >= 0);
	freeList(list);
	checkCount("fav: system:gb", 1);
	checkCount("FAV: system:ps crash", 1);
	checkCount("-fav: system:ps", 1); // Crash Bash
	list = results("-tag:beatemup system:gb");
	CHECK(list->count == 5 && indexOf(list, GB "/Tetris (World) (Rev 1).gb") < 0);
	freeList(list);
	checkCount("tag:beat", 0);
	checkCount("tag:nothing", 0);
	checkCount("-tag:nothing system:gb", 6);

	// games.played decides unplayed
	checkCount("-unplayed", 1);
	checkCount("unplayed tetris", 0);
	checkCount("unplayed system:gb", 5);
	// without it, recents do
	fake.call = NULL;
	checkCount("-unplayed", 0);
	recent_list[0] = GB "/Kirby.gb";
	recent_total = 1;
	checkCount("-unplayed", 1);
	recent_list[0] = PS "/Crash (USA)/Crash (USA).cue";
	checkCount("-unplayed system:ps", 1); // a file in a game folder plays the folder
	recent_total = 0;
	fake.call = fakeCall;

	// the .info fields, through the card index
	checkCount("genre:platform", 1);
	checkCount("decade:80s", 1);
	checkCount("year:1989-1992", 2);
	checkCount("-genre:puzzle system:gb", 5);
	// and the play history: none without it
	checkCount("played:7", 0);
	fake.play_time = fakePlayTime;
	fake.last_played = fakeLastPlayed;
	checkCount("played:7", 1);
	checkCount("hours:10", 1);
	checkCount("hours:11", 0);
	checkCount("played:31 system:ps", 1); // a game folder by its .cue
	// and the card: every file was just made
	checkCount("added:1 system:ps", 2);
	fake.play_time = NULL;
	fake.last_played = NULL;

	// a set that changes searches again, and lists the folder again while it's open
	run("fav:");
	snprintf(current, sizeof(current), "%s", SEARCH);
	reloads = 0;
	CHECK(hits_valid);
	set_changed_fn("launcher.set_changed", "name=favorites", NULL);
	CHECK(!hits_valid && reloads == 1);
	set_changed_fn("launcher.set_changed", "name=favorites", NULL);
	CHECK(reloads == 1); // nothing searched since
	fake_set_count = 0; // favorites is off
	fakeSetRegister("tags:Beat 'em Up", noHas, tagList);
	checkCount("fav:", 0);
	checkCount("-fav: system:ps", 2);
	snprintf(current, sizeof(current), "%s", CARD);
	p->quit();

	// a launcher without sets: fav: and tag: hold nothing
	fake.set_list = NULL;
	fake.set_names = NULL;
	CHECK(p->init(&fake) == 0);
	checkCount("fav:", 0);
	checkCount("-fav: system:ps", 2);
	p->quit();
	fake.subscribe = fakeIndexSubscribe;
	fake.call = NULL;
	fake.recents_count = NULL;
	fake.recent_path = NULL;
	(void)system("rm -rf '" CARD "'");
}

int main(void) {
	testFold();
	testRank();
	testSearch();
	testParse();
	testFilters();
	testInfoFilters();
	testTimeFilters();
	testRecents();
	testTiming();
	testPlugin();
	testPluginFilters();
	CHECK_DONE();
}
