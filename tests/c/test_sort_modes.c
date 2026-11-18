// sort-modes' model: the modes, which lists they order, how keys compare,
// the sections they give, discs and the cache of keys.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "check.h"
#include "sort_model.h"

#define SD "/mnt/SDCARD"

static void test_modes(void) {
	CHECK_STR(SM_modeId(SM_NAME), "name");
	CHECK_STR(SM_modeId(SM_ADDED), "recently added");
	CHECK_STR(SM_modeLabel(SM_MOST_PLAYED), "Most played");
	CHECK_STR(SM_modeId(-1), "name");
	CHECK_STR(SM_modeId(SM_MODES), "name");
	for (int i = 0; i < SM_MODES; i++) CHECK(SM_modeFind(SM_modeId(i)) == i);
	CHECK(SM_modeFind("Year") == -1);
	CHECK(SM_modeFind("") == -1);
	CHECK(SM_modeFind(NULL) == -1);
	// cycles through every mode and back to name
	int mode = SM_NAME, seen = 0;
	do {
		mode = SM_modeNext(mode);
		seen++;
	} while (mode != SM_NAME && seen < 10);
	CHECK(seen == SM_MODES);
	CHECK(SM_modeNext(SM_NAME) == SM_MOST_PLAYED);
	CHECK(SM_modeNext(SM_ADDED) == SM_NAME);
	CHECK(SM_modeReadsPlay(SM_MOST_PLAYED) && SM_modeReadsPlay(SM_LAST_PLAYED));
	CHECK(!SM_modeReadsPlay(SM_YEAR) && !SM_modeReadsPlay(SM_ADDED) && !SM_modeReadsPlay(SM_NAME));
	CHECK(SM_modeReadsIndex(SM_YEAR) && SM_modeReadsIndex(SM_PUBLISHER));
	CHECK(!SM_modeReadsIndex(SM_MOST_PLAYED) && !SM_modeReadsIndex(SM_ADDED));
}

static void test_which_lists(void) {
	CHECK(SM_sorts(SD, SD "/Roms/Game Boy (GB)"));
	CHECK(SM_sorts(SD, SD "/Roms/Game Boy (GB)/Puzzle"));
	CHECK(SM_sorts(SD, SD "/Collections/Favorites.txt"));
	CHECK(SM_sorts(SD, SD "/Favorites"));
	CHECK(SM_sorts(SD, SD "/Tags/Backlog"));
	CHECK(SM_sorts(SD, SD "/Smart Collections/1990s"));
	CHECK(SM_sorts(SD, SD "/Searching"));
	CHECK(SM_sorts(SD, SD "/Toolshed"));
	CHECK(SM_sorts(SD, SD "/Roms2"));
	// the first screen, systems, recents, the collections, search and tools keep theirs
	CHECK(!SM_sorts(SD, SD));
	CHECK(!SM_sorts(SD, SD "/Roms"));
	CHECK(!SM_sorts(SD, SD "/Recently Played"));
	CHECK(!SM_sorts(SD, SD "/Collections"));
	CHECK(!SM_sorts(SD, SD "/Search"));
	CHECK(!SM_sorts(SD, SD "/Search/tetris"));
	CHECK(!SM_sorts(SD, SD "/Tools"));
	CHECK(!SM_sorts(SD, SD "/Tools/tg5040"));
	CHECK(!SM_sorts(SD, NULL));
	CHECK(!SM_sorts(NULL, SD "/Roms/GB"));

	CHECK(SM_inRoms(SD, SD "/Roms/Game Boy (GB)/Tetris.gb"));
	CHECK(SM_inRoms(SD, SD "/Roms/Game Boy (GB)/Puzzle/Tetris.gb"));
	CHECK(!SM_inRoms(SD, SD "/Roms/Game Boy (GB)"));
	CHECK(!SM_inRoms(SD, SD "/Roms/Game Boy (GB)/"));
	CHECK(!SM_inRoms(SD, SD "/Roms"));
	CHECK(!SM_inRoms(SD, SD "/Tools/tg5040/Clock.pak"));
	CHECK(!SM_inRoms(SD, SD "/Romsy/GB/Tetris.gb"));
	CHECK(!SM_inRoms(SD, "/elsewhere/Roms/GB/Tetris.gb"));
	CHECK(!SM_inRoms(SD, NULL));
}

static SMKey game(int has, long long value) {
	SMKey k = { SM_GAME, has, value, NULL };
	return k;
}

static SMKey publisher(const char* text) {
	SMKey k = { SM_GAME, text != NULL, 0, (char*)text };
	return k;
}

static SMKey folder(void) {
	SMKey k = { SM_FOLDER, 0, 0, NULL };
	return k;
}

// Sorts names by their keys in mode, as the launcher's chain would with
// only this plugin: by SM_compare, ties keeping the given order.
typedef struct Row {
	const char* name;
	SMKey key;
	int at;
} Row;
static int row_mode;
static int byRow(const void* x, const void* y) {
	const Row* a = x;
	const Row* b = y;
	int order = SM_compare(row_mode, &a->key, &b->key, a->name, b->name);
	return order ? order : a->at - b->at;
}
static const char* order(int mode, Row* rows, int count) {
	static char out[512];
	for (int i = 0; i < count; i++) rows[i].at = i;
	row_mode = mode;
	qsort(rows, (size_t)count, sizeof(Row), byRow);
	out[0] = '\0';
	for (int i = 0; i < count; i++) {
		if (i) strcat(out, ",");
		strcat(out, rows[i].name);
	}
	return out;
}

static void test_compare_each_mode(void) {
	// most played and the times: the most or newest first, then the rest by name
	Row played[] = {
		{ "Delta", game(0, 0) }, { "bravo", game(1, 60) }, { "Alpha", game(0, 0) },
		{ "Charlie", game(1, 3600) }, { "Echo", game(1, 60) },
	};
	CHECK_STR(order(SM_MOST_PLAYED, played, 5), "Charlie,bravo,Echo,Alpha,Delta");
	Row last[] = { { "Alpha", game(1, 1000) }, { "Bravo", game(1, 3000) }, { "Charlie", game(0, 0) }, { "Delta", game(1, 2000) } };
	CHECK_STR(order(SM_LAST_PLAYED, last, 4), "Bravo,Delta,Alpha,Charlie");
	Row added[] = { { "Alpha", game(1, 1000) }, { "Bravo", game(1, 3000) }, { "Charlie", game(0, 0) }, { "Delta", game(1, 2000) } };
	CHECK_STR(order(SM_ADDED, added, 4), "Bravo,Delta,Alpha,Charlie");
	// years the oldest first
	Row years[] = {
		{ "Alpha", game(1, 1993) }, { "Bravo", game(0, 0) }, { "Charlie", game(1, 1987) },
		{ "Delta", game(1, 1993) }, { "Able", game(0, 0) },
	};
	CHECK_STR(order(SM_YEAR, years, 5), "Charlie,Alpha,Delta,Able,Bravo");
	// publishers A to Z ignoring case, then by name within one
	Row publishers[] = {
		{ "Zelda", publisher("Nintendo") }, { "Mega Man", publisher("capcom") }, { "Tetris", publisher(NULL) },
		{ "Mario", publisher("nintendo") }, { "Street Fighter", publisher("Capcom") }, { "Asteroids", publisher(NULL) },
	};
	CHECK_STR(order(SM_PUBLISHER, publishers, 6), "Mega Man,Street Fighter,Mario,Zelda,Asteroids,Tetris");
}

static void test_folders_first_in_their_order(void) {
	Row rows[] = {
		{ "Zeta", game(1, 5) }, { "Puzzle", folder() }, { "Alpha", game(0, 0) }, { "Action", folder() }, { "Beta", game(1, 9) },
	};
	// the folders keep the order they had, as their compare is 0
	CHECK_STR(order(SM_MOST_PLAYED, rows, 5), "Puzzle,Action,Beta,Zeta,Alpha");
	SMKey f = folder(), g = game(1, 1);
	CHECK(SM_compare(SM_YEAR, &f, &g, "Z", "A") < 0);
	CHECK(SM_compare(SM_YEAR, &g, &f, "A", "Z") > 0);
	CHECK(SM_compare(SM_YEAR, &f, &f, "Z", "A") == 0);
}

static void test_name_mode_has_no_opinion(void) {
	SMKey a = game(1, 1), b = game(1, 2), f = folder();
	CHECK(SM_compare(SM_NAME, &a, &b, "A", "B") == 0);
	CHECK(SM_compare(SM_NAME, &f, &a, "A", "B") == 0);
	CHECK(SM_compare(-1, &a, &b, "A", "B") == 0);
	CHECK(SM_compare(SM_MODES, &a, &b, "A", "B") == 0);
	CHECK(SM_compare(SM_YEAR, NULL, &b, "A", "B") == 0);
	// the same name and value: no order, so the launcher's stays
	CHECK(SM_compare(SM_YEAR, &a, &a, "Tetris", "tetris") == 0);
	CHECK(SM_compare(SM_YEAR, &a, &a, NULL, NULL) == 0);
	// compares are -1, 0 or 1
	SMKey big = game(1, 3000000000LL), small = game(1, 1);
	CHECK(SM_compare(SM_MOST_PLAYED, &big, &small, "A", "B") == -1);
	CHECK(SM_compare(SM_MOST_PLAYED, &small, &big, "A", "B") == 1);
	CHECK(SM_compare(SM_YEAR, &small, &small, "Alpha", "Zulu") == -1);
}

static void test_years(void) {
	CHECK(SM_yearOf("1991") == 1991);
	CHECK(SM_yearOf("1994-02-01") == 1994);
	CHECK(SM_yearOf(" 2001") == 2001);
	CHECK(SM_yearOf("19xx") == 0);
	CHECK(SM_yearOf("199") == 0);
	CHECK(SM_yearOf("19912") == 0);
	CHECK(SM_yearOf("") == 0);
	CHECK(SM_yearOf(NULL) == 0);
	CHECK(SM_yearOf("0000") == 0);
}

static const char* sectionOf(int mode, SMKey key, long long now) {
	static char out[64];
	const char* label = SM_section(mode, &key, now, out, sizeof(out));
	return label ? label : out;
}

static long long localTime(int year, int month, int day, int hour) {
	struct tm t;
	memset(&t, 0, sizeof(t));
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_isdst = -1;
	return (long long)mktime(&t);
}

static void test_sections(void) {
	long long now = localTime(2026, 11, 18, 15);
	CHECK_STR(sectionOf(SM_MOST_PLAYED, game(1, 400 * 3600LL), now), "100h+");
	CHECK_STR(sectionOf(SM_MOST_PLAYED, game(1, 100 * 3600LL), now), "100h+");
	CHECK_STR(sectionOf(SM_MOST_PLAYED, game(1, 12 * 3600LL), now), "10h+");
	CHECK_STR(sectionOf(SM_MOST_PLAYED, game(1, 3600), now), "1h+");
	CHECK_STR(sectionOf(SM_MOST_PLAYED, game(1, 3599), now), "Under 1h");
	CHECK_STR(sectionOf(SM_MOST_PLAYED, game(0, 0), now), "Not played");
	CHECK_STR(sectionOf(SM_LAST_PLAYED, game(0, 0), now), "Not played");
	CHECK_STR(sectionOf(SM_YEAR, game(1, 1991), now), "1991");
	CHECK_STR(sectionOf(SM_YEAR, game(0, 0), now), "No year");
	CHECK_STR(sectionOf(SM_PUBLISHER, publisher("Capcom"), now), "Capcom");
	CHECK_STR(sectionOf(SM_PUBLISHER, publisher(NULL), now), "No publisher");
	CHECK_STR(sectionOf(SM_ADDED, game(0, 0), now), "No date");
	// times by local day: today, the 6 days before, the 29 before, then years
	CHECK_STR(sectionOf(SM_LAST_PLAYED, game(1, localTime(2026, 11, 18, 0)), now), "Today");
	CHECK_STR(sectionOf(SM_LAST_PLAYED, game(1, localTime(2026, 11, 17, 23)), now), "Last 7 days");
	CHECK_STR(sectionOf(SM_ADDED, game(1, localTime(2026, 11, 12, 0)), now), "Last 7 days");
	CHECK_STR(sectionOf(SM_ADDED, game(1, localTime(2026, 11, 11, 23)), now), "Last 30 days");
	CHECK_STR(sectionOf(SM_ADDED, game(1, localTime(2026, 10, 20, 1)), now), "Last 30 days");
	CHECK_STR(sectionOf(SM_ADDED, game(1, localTime(2026, 10, 19, 23)), now), "2026");
	CHECK_STR(sectionOf(SM_LAST_PLAYED, game(1, localTime(2023, 6, 1, 12)), now), "2023");
	// a time after now is today's
	CHECK_STR(sectionOf(SM_LAST_PLAYED, game(1, now + 3600), now), "Today");
	// none for folders, or in name
	char out[16] = "x";
	SMKey f = folder(), g = game(1, 1991);
	CHECK(SM_section(SM_YEAR, &f, now, out, sizeof(out)) == NULL && out[0] == '\0');
	CHECK(SM_section(SM_NAME, &g, now, out, sizeof(out)) == NULL && out[0] == '\0');
	CHECK(SM_section(SM_YEAR, NULL, now, out, sizeof(out)) == NULL && out[0] == '\0');
	// cut to fit
	char tiny[4];
	SMKey p = publisher("Nintendo");
	CHECK(SM_section(SM_PUBLISHER, &p, now, tiny, sizeof(tiny)) == NULL);
	CHECK_STR(tiny, "Nin");
}

static void test_discs(void) {
	char game_name[64];
	CHECK(SM_discGame("Game (USA) (Disc 1).chd", game_name, sizeof(game_name)));
	CHECK_STR(game_name, "Game (USA)");
	CHECK(SM_discGame("Game (Disc 2 of 3) (USA).cue", game_name, sizeof(game_name)));
	CHECK_STR(game_name, "Game (USA)");
	CHECK(SM_discGame("(cd 1) Game.bin", game_name, sizeof(game_name)));
	CHECK_STR(game_name, "Game");
	CHECK(SM_discGame("Dr. Mario (DISK 1)", game_name, sizeof(game_name)));
	CHECK_STR(game_name, "Dr. Mario");
	CHECK(!SM_discGame("Game (USA).chd", game_name, sizeof(game_name)));
	CHECK(!SM_discGame("Game (Disc 1) (Disc 2).chd", game_name, sizeof(game_name)));
	CHECK(!SM_discGame("Game (Disc A).chd", game_name, sizeof(game_name)));
	CHECK(!SM_discGame("Game (Disc 1.chd", game_name, sizeof(game_name)));
	CHECK(!SM_discGame("(Disc 1).chd", game_name, sizeof(game_name)));
	char tiny[4];
	CHECK(!SM_discGame("Game (Disc 1).chd", tiny, sizeof(tiny)));

	CHECK(SM_discOf("Game (USA).m3u", "Game (USA) (Disc 1).chd"));
	CHECK(SM_discOf("game (usa).m3u", "Game (USA) (Disc 2).chd"));
	CHECK(SM_discOf("Game (USA).m3u", "Game (Disc 2 of 2) (USA).bin"));
	CHECK(!SM_discOf("Game (USA).m3u", "Game (USA) (Rev 1) (Disc 1).chd"));
	CHECK(!SM_discOf("Game.m3u", "Game (USA) (Disc 1).chd"));
	CHECK(!SM_discOf("Game (USA).m3u", "Game (USA).chd"));
	CHECK(!SM_discOf(NULL, "Game (Disc 1).chd"));
}

static void test_cache(void) {
	SMCache cache;
	SM_cacheInit(&cache);
	CHECK(SM_cacheGet(&cache, "/a") == NULL);
	SMKey k = { SM_GAME, 1, 42, "Capcom" };
	const SMKey* kept = SM_cachePut(&cache, "/a", &k);
	CHECK(kept && kept->value == 42 && kept->text != k.text && !strcmp(kept->text, "Capcom"));
	CHECK(SM_cacheGet(&cache, "/a") == kept);
	// replaced in place
	SMKey other = { SM_GAME, 0, 7, NULL };
	const SMKey* again = SM_cachePut(&cache, "/a", &other);
	CHECK(again == kept && again->value == 7 && again->text == NULL);
	CHECK(cache.count == 1);
	CHECK(SM_cacheGet(&cache, NULL) == NULL);
	CHECK(SM_cachePut(&cache, NULL, &k) == NULL);

	// many, past several growths, each still found with its own key
	char path[64];
	for (int i = 0; i < 20000; i++) {
		snprintf(path, sizeof(path), "/Roms/GB/game %d.gb", i);
		SMKey key = { SM_GAME, 1, i, NULL };
		CHECK(SM_cachePut(&cache, path, &key) != NULL);
	}
	CHECK(cache.count == 20001);
	CHECK(cache.bucket_count >= cache.count / 2);
	int right = 0;
	for (int i = 0; i < 20000; i++) {
		snprintf(path, sizeof(path), "/Roms/GB/game %d.gb", i);
		const SMKey* got = SM_cacheGet(&cache, path);
		right += got && got->value == i;
	}
	CHECK(right == 20000);
	SM_cacheClear(&cache);
	CHECK(cache.count == 0 && SM_cacheGet(&cache, "/a") == NULL);
	SM_cacheClear(&cache);
}

static void test_memo(void) {
	SMMemo memo;
	SM_memoInit(&memo);
	int entries[5000];
	SMKey keys[5000];
	CHECK(SM_memoGet(&memo, &entries[0]) == NULL);
	CHECK(SM_memoPut(&memo, NULL, &keys[0]) == -1);
	CHECK(SM_memoPut(&memo, &entries[0], NULL) == -1);
	for (int i = 0; i < 5000; i++) CHECK(SM_memoPut(&memo, &entries[i], &keys[i]) == 0);
	CHECK(SM_memoPut(&memo, &entries[7], &keys[8]) == 0); // replaced
	CHECK(memo.count == 5000 && memo.room >= 10000);
	int right = 0;
	for (int i = 0; i < 5000; i++) right += SM_memoGet(&memo, &entries[i]) == &keys[i == 7 ? 8 : i];
	CHECK(right == 5000);
	size_t room = memo.room;
	SM_memoEmpty(&memo);
	CHECK(memo.count == 0 && memo.room == room && SM_memoGet(&memo, &entries[3]) == NULL);
	SM_memoFree(&memo);
	CHECK(memo.room == 0 && SM_memoGet(&memo, &entries[3]) == NULL);
}

// A sort of 5,000 games in a folder as the plugin sorts one: each compare
// finds both keys by entry in the memo, or by path in the cache the
// first time, each worked out once beforehand.
static SMCache bench_cache;
static SMMemo bench_memo;
static int bench_mode;
typedef struct BenchEntry {
	char path[128];
	char name[64];
} BenchEntry;
static int benchCompare(const void* x, const void* y) {
	const BenchEntry* a = *(BenchEntry* const*)x;
	const BenchEntry* b = *(BenchEntry* const*)y;
	const SMKey* ka = SM_memoGet(&bench_memo, a);
	if (!ka) SM_memoPut(&bench_memo, a, ka = SM_cacheGet(&bench_cache, a->path));
	const SMKey* kb = SM_memoGet(&bench_memo, b);
	if (!kb) SM_memoPut(&bench_memo, b, kb = SM_cacheGet(&bench_cache, b->path));
	return SM_compare(bench_mode, ka, kb, a->name, b->name);
}

static double millis(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void test_sorting_5000_games(void) {
	enum { COUNT = 5000 };
	BenchEntry* entries = malloc(sizeof(BenchEntry) * COUNT);
	BenchEntry** list = malloc(sizeof(BenchEntry*) * COUNT);
	CHECK(entries && list);
	if (!entries || !list) return;
	const char* publishers[] = { "Nintendo", "Capcom", "Konami", "Sega", "Namco", "Taito", "Hudson", "Irem" };
	for (int mode = SM_MOST_PLAYED; mode < SM_MODES; mode++) {
		SM_cacheInit(&bench_cache);
		SM_memoInit(&bench_memo);
		bench_mode = mode;
		srand(315);
		double started = millis();
		for (int i = 0; i < COUNT; i++) {
			snprintf(entries[i].name, sizeof(entries[i].name), "Game %05d", rand() % 100000);
			snprintf(entries[i].path, sizeof(entries[i].path), "/mnt/SDCARD/Roms/Game Boy (GB)/%s.gb", entries[i].name);
			// a third have no value
			int has = rand() % 3 != 0;
			SMKey key = { SM_GAME, has, has ? 1980 + rand() % 40 : 0, NULL };
			if (mode == SM_PUBLISHER && has) key.text = (char*)publishers[rand() % 8];
			if (mode != SM_YEAR && mode != SM_PUBLISHER && has) key.value = rand();
			SM_cachePut(&bench_cache, entries[i].path, &key);
			list[i] = &entries[i];
		}
		double keyed = millis();
		qsort(list, COUNT, sizeof(BenchEntry*), benchCompare);
		double sorted = millis();
		// in order: each pair compares no later than the next
		int bad = 0;
		for (int i = 1; i < COUNT; i++) bad += benchCompare(&list[i - 1], &list[i]) > 0;
		CHECK(bad == 0);
		printf("5000 games by %s: keys kept in %.2f ms, sorted in %.2f ms\n", SM_modeId(mode), keyed - started, sorted - keyed);
		SM_cacheClear(&bench_cache);
		SM_memoFree(&bench_memo);
	}
	free(entries);
	free(list);
}

int main(void) {
	setenv("TZ", "UTC", 1);
	tzset();
	test_modes();
	test_which_lists();
	test_compare_each_mode();
	test_folders_first_in_their_order();
	test_name_mode_has_no_opinion();
	test_years();
	test_sections();
	test_discs();
	test_cache();
	test_memo();
	test_sorting_5000_games();
	CHECK_DONE();
}
