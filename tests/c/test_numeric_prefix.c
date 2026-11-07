// The numeric prefix plugin: its parsing, ordering and cache on their own,
// then the plugin against a fake host, listing folders the way the
// launcher does (display names, sort by name, plugin sort).

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
#include "check.h"
#include "../../plugins/numeric-prefix/numeric_prefix.c"

struct NUIEntry {
	char path[512];
	char name[128];
	int type;
};

static const char* setting = NULL; // the hide setting, or NULL for the default

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (setting && !strcmp(key, "hide")) return setting;
	return fallback;
}
static const char* fakeEntryPath(const NUIEntry* e) { return e->path; }
static const char* fakeEntryName(const NUIEntry* e) { return e->name; }
static int fakeEntryType(const NUIEntry* e) { return e->type; }

#define CARD "/mnt/SDCARD"
#define GB CARD "/Roms/Game Boy (GB)"

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
};

static void testParse(void) {
	long n = -1;
	const char* rest = NULL;
	CHECK(NumPrefix_parse("1) Foo", &n, &rest) && n == 1);
	CHECK_STR(rest, "Foo");
	CHECK(NumPrefix_parse("02. Tetris", &n, &rest) && n == 2);
	CHECK_STR(rest, "Tetris");
	CHECK(NumPrefix_parse("[10] Dr. Mario", &n, &rest) && n == 10);
	CHECK_STR(rest, "Dr. Mario");
	CHECK(NumPrefix_parse("3)   Spaced", &n, &rest) && n == 3);
	CHECK_STR(rest, "Spaced");
	CHECK(NumPrefix_parse("007) Bond", &n, NULL) && n == 7);
	CHECK(NumPrefix_parse("123456789) Big", &n, NULL) && n == 123456789);

	// not prefixes
	CHECK(!NumPrefix_parse("1942", &n, &rest));
	CHECK(!NumPrefix_parse("3-D Worldrunner", &n, &rest));
	CHECK(!NumPrefix_parse("007 - Nightfire", &n, &rest));
	CHECK(!NumPrefix_parse("1 - Foo", &n, &rest));
	CHECK(!NumPrefix_parse("1)Foo", &n, &rest));
	CHECK(!NumPrefix_parse("1.5 Things", &n, &rest));
	CHECK(!NumPrefix_parse("[1 Foo", &n, &rest));
	CHECK(!NumPrefix_parse("[1) Foo", &n, &rest));
	CHECK(!NumPrefix_parse("1] Foo", &n, &rest));
	CHECK(!NumPrefix_parse("[] Foo", &n, &rest));
	CHECK(!NumPrefix_parse(") Foo", &n, &rest));
	CHECK(!NumPrefix_parse("1) ", &n, &rest));
	CHECK(!NumPrefix_parse("1)", &n, &rest));
	CHECK(!NumPrefix_parse("1234567890) Too long", &n, &rest));
	CHECK(!NumPrefix_parse("Foo 1) Bar", &n, &rest));
	CHECK(!NumPrefix_parse("", &n, &rest));
	CHECK(!NumPrefix_parse(NULL, &n, &rest));

	char out[64];
	CHECK(NumPrefix_strip("1) Foo", out, sizeof(out)));
	CHECK_STR(out, "Foo");
	CHECK(!NumPrefix_strip("Foo", out, sizeof(out)));
	CHECK_STR(out, "Foo");
	CHECK(!NumPrefix_strip("1942", out, sizeof(out)));
	CHECK_STR(out, "1942");
	CHECK(NumPrefix_strip("1) A long name", out, 5));
	CHECK_STR(out, "A lo");
}

static void testCompare(void) {
	NumKey one = { NUMPREFIX_ORDERED, 1, 1 };
	NumKey two = { NUMPREFIX_ORDERED, 1, 2 };
	NumKey ten = { NUMPREFIX_ORDERED, 1, 10 };
	NumKey plain = { NUMPREFIX_ORDERED, 0, 0 };
	NumKey other = { NUMPREFIX_ORDERED, 0, 0 };
	NumKey first = { NUMPREFIX_BEFORE, 0, 0 };
	NumKey last = { NUMPREFIX_AFTER, 1, 1 };

	CHECK(NumPrefix_compare(&two, &ten) < 0);
	CHECK(NumPrefix_compare(&ten, &two) > 0);
	CHECK(NumPrefix_compare(&one, &one) == 0);
	CHECK(NumPrefix_compare(&ten, &plain) < 0);
	CHECK(NumPrefix_compare(&plain, &one) > 0);
	CHECK(NumPrefix_compare(&plain, &other) == 0);
	CHECK(NumPrefix_compare(&first, &one) < 0);
	CHECK(NumPrefix_compare(&last, &plain) > 0);
	CHECK(NumPrefix_compare(&last, &first) > 0);

	// numbers only count in the ordered group
	NumKey after = { NUMPREFIX_AFTER, 0, 0 };
	CHECK(NumPrefix_compare(&last, &after) == 0);
	CHECK(NumPrefix_compare(&after, &last) == 0);
}

static void testGroups(void) {
	CHECK(NumPrefix_rootGroup(CARD, CARD "/Recently Played") == NUMPREFIX_BEFORE);
	CHECK(NumPrefix_rootGroup(CARD, GB) == NUMPREFIX_ORDERED);
	CHECK(NumPrefix_rootGroup(CARD, CARD "/Collections/1) Best.txt") == NUMPREFIX_ORDERED);
	CHECK(NumPrefix_rootGroup(CARD, CARD "/Collections") == NUMPREFIX_AFTER);
	CHECK(NumPrefix_rootGroup(CARD, CARD "/Tools/tg5040") == NUMPREFIX_AFTER);
	CHECK(NumPrefix_rootGroup(CARD, CARD "/Favorites") == NUMPREFIX_AFTER);
	CHECK(NumPrefix_rootGroup(CARD, CARD "/Roms") == NUMPREFIX_AFTER);
	CHECK(NumPrefix_rootGroup(CARD, CARD "/Romsick/x") == NUMPREFIX_AFTER);

	CHECK(NumPrefix_keepsOrder(CARD, CARD "/Recently Played"));
	CHECK(NumPrefix_keepsOrder(CARD, CARD "/Collections/Best.txt"));
	CHECK(!NumPrefix_keepsOrder(CARD, CARD "/Collections"));
	CHECK(!NumPrefix_keepsOrder(CARD, GB));
	CHECK(!NumPrefix_keepsOrder(CARD, CARD));
	CHECK(!NumPrefix_keepsOrder(CARD, NULL));
}

static void testCache(void) {
	NumCache cache;
	NumCache_init(&cache);
	long n = 0;
	CHECK(!NumCache_get(&cache, "/a", &n));
	CHECK(NumCache_set(&cache, "/a", 3) == 0);
	CHECK(NumCache_set(&cache, "/b", 4) == 0);
	CHECK(NumCache_get(&cache, "/a", &n) && n == 3);
	CHECK(NumCache_set(&cache, "/a", 5) == 0);
	CHECK(NumCache_get(&cache, "/a", &n) && n == 5);
	CHECK(cache.count == 2);
	NumCache_remove(&cache, "/a");
	NumCache_remove(&cache, "/missing");
	CHECK(!NumCache_get(&cache, "/a", &n));
	CHECK(NumCache_get(&cache, "/b", &n) && n == 4);
	CHECK(cache.count == 1);

	// enough to share buckets
	char path[32];
	for (int i = 0; i < 3000; i++) {
		snprintf(path, sizeof(path), "/p%d", i);
		NumCache_set(&cache, path, i);
	}
	CHECK(cache.count == 3001);
	CHECK(NumCache_get(&cache, "/p2999", &n) && n == 2999);
	NumCache_clear(&cache);
	CHECK(cache.count == 0);
	CHECK(!NumCache_get(&cache, "/b", &n));
}

///////////////////////////////////////
// listing like the launcher

static NUIEntry entries[16];
static NUIEntry* list[16];
static int count;

static void reset(void) {
	count = 0;
}

static void add(const char* path, const char* name, int type) {
	NUIEntry* e = &entries[count];
	snprintf(e->path, sizeof(e->path), "%s", path);
	snprintf(e->name, sizeof(e->name), "%s", name);
	e->type = type;
	list[count++] = e;
}

static int byName(const void* a, const void* b) {
	return strcasecmp((*(NUIEntry**)a)->name, (*(NUIEntry**)b)->name);
}

// display names, then (unless the list keeps its order) a sort by name,
// then a stable sort by the plugin
static void listing(const NUIPlugin* p, const char* dir, int sort_by_name) {
	for (int i = 0; i < count; i++) {
		char name[128];
		name[0] = '\0';
		if (p->display_name(list[i], name, sizeof(name)) && name[0]) {
			snprintf(list[i]->name, sizeof(list[i]->name), "%s", name);
		}
	}
	if (sort_by_name) qsort(list, count, sizeof(list[0]), byName);
	for (int i = 1; i < count; i++) {
		NUIEntry* e = list[i];
		int j = i;
		while (j > 0 && p->sort_compare(list[j - 1], e, dir) > 0) {
			list[j] = list[j - 1];
			j--;
		}
		list[j] = e;
	}
}

static void checkOrder(const char* const* want, int n) {
	CHECK(count == n);
	for (int i = 0; i < n && i < count; i++) CHECK_STR(list[i]->name, want[i]);
}

static void testGames(const NUIPlugin* p) {
	reset();
	add(GB "/10) Tetris.gb", "10) Tetris", NUI_ENTRY_ROM);
	add(GB "/2) Kirby.gb", "2) Kirby", NUI_ENTRY_ROM);
	add(GB "/Zelda.gb", "Zelda", NUI_ENTRY_ROM);
	add(GB "/[1] Wario.gb", "[1] Wario", NUI_ENTRY_ROM);
	add(GB "/Alleyway.gb", "Alleyway", NUI_ENTRY_ROM);
	add(GB "/03. Metroid", "03. Metroid", NUI_ENTRY_DIR);
	add(GB "/1942.gb", "1942", NUI_ENTRY_ROM);
	add(GB "/mapped.gb", "4) Mapped", NUI_ENTRY_ROM); // from map.txt
	listing(p, GB, 1);
	const char* want[] = { "Wario", "Kirby", "Metroid", "Mapped", "Tetris", "1942", "Alleyway", "Zelda" };
	checkOrder(want, 8);

	// the letter index runs on the shown names, so no group is "other"
	// except the game that really starts with a digit
	for (int i = 0; i < count; i++) {
		if (strcmp(list[i]->name, "1942")) CHECK(isalpha((unsigned char)list[i]->name[0]));
	}

	// the same number keeps the launcher's order
	reset();
	add(GB "/1) Beta.gb", "1) Beta", NUI_ENTRY_ROM);
	add(GB "/1) Alpha.gb", "1) Alpha", NUI_ENTRY_ROM);
	listing(p, GB, 1);
	const char* tie[] = { "Alpha", "Beta" };
	checkOrder(tie, 2);

	// a map.txt name without a number drops the old one
	reset();
	add(GB "/mapped.gb", "Mapped", NUI_ENTRY_ROM);
	add(GB "/2) Kirby.gb", "2) Kirby", NUI_ENTRY_ROM);
	listing(p, GB, 1);
	const char* unmapped[] = { "Kirby", "Mapped" };
	checkOrder(unmapped, 2);
}

static void testRoot(const NUIPlugin* p) {
	reset();
	add(CARD "/Recently Played", "Recently Played", NUI_ENTRY_DIR);
	add(CARD "/Roms/Game Boy Advance (GBA)", "Game Boy Advance", NUI_ENTRY_DIR);
	add(CARD "/Roms/10) Super Nintendo (SFC)", "10) Super Nintendo", NUI_ENTRY_DIR);
	add(CARD "/Roms/2) Game Boy (GB)", "2) Game Boy", NUI_ENTRY_DIR);
	add(CARD "/Roms/Nintendo (FC)", "Nintendo", NUI_ENTRY_DIR);
	add(CARD "/Collections", "Collections", NUI_ENTRY_DIR);
	add(CARD "/Favorites", "1) Favorites", NUI_ENTRY_DIR);
	add(CARD "/Tools/tg5040", "Tools", NUI_ENTRY_DIR);
	listing(p, CARD, 0);
	const char* want[] = { "Recently Played", "Game Boy", "Super Nintendo", "Game Boy Advance", "Nintendo",
		"Collections", "Favorites", "Tools" };
	checkOrder(want, 8);

	// collections promoted to the root move like consoles
	reset();
	add(CARD "/Recently Played", "Recently Played", NUI_ENTRY_DIR);
	add(CARD "/Collections/Beat em ups.txt", "Beat em ups", NUI_ENTRY_DIR);
	add(CARD "/Collections/1) Best.txt", "1) Best", NUI_ENTRY_DIR);
	add(CARD "/Tools/tg5040", "Tools", NUI_ENTRY_DIR);
	listing(p, CARD, 0);
	const char* promoted[] = { "Recently Played", "Best", "Beat em ups", "Tools" };
	checkOrder(promoted, 4);
}

static void testKeptOrder(const NUIPlugin* p) {
	reset();
	add(GB "/Zelda.gb", "Zelda", NUI_ENTRY_ROM);
	add(GB "/2) Kirby.gb", "2) Kirby", NUI_ENTRY_ROM);
	add(GB "/1) Wario.gb", "1) Wario", NUI_ENTRY_ROM);
	listing(p, CARD "/Recently Played", 0);
	const char* want[] = { "Zelda", "Kirby", "Wario" };
	checkOrder(want, 3);

	reset();
	add(GB "/2) Kirby.gb", "2) Kirby", NUI_ENTRY_ROM);
	add(GB "/1) Wario.gb", "1) Wario", NUI_ENTRY_ROM);
	listing(p, CARD "/Collections/Mine.txt", 0);
	const char* collection[] = { "Kirby", "Wario" };
	checkOrder(collection, 2);

	// the collections folder itself is sorted
	reset();
	add(CARD "/Collections/2) Kirby.txt", "2) Kirby", NUI_ENTRY_DIR);
	add(CARD "/Collections/1) Wario.txt", "1) Wario", NUI_ENTRY_DIR);
	listing(p, CARD "/Collections", 1);
	const char* folder[] = { "Wario", "Kirby" };
	checkOrder(folder, 2);
}

static void testShown(void) {
	setting = "off";
	const NUIPlugin* p = nextui_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
	reset();
	add(GB "/10) Tetris.gb", "10) Tetris", NUI_ENTRY_ROM);
	add(GB "/Alleyway.gb", "Alleyway", NUI_ENTRY_ROM);
	add(GB "/2) Kirby.gb", "2) Kirby", NUI_ENTRY_ROM);
	listing(p, GB, 1);
	const char* want[] = { "2) Kirby", "10) Tetris", "Alleyway" };
	checkOrder(want, 3);
	p->quit();
	setting = NULL;
}

int main(void) {
	testParse();
	testCompare();
	testGroups();
	testCache();

	const NUIPlugin* p = nextui_plugin_get(&fake);
	CHECK(p->size == sizeof(NUIPlugin));
	CHECK_STR(p->id, "numeric-prefix");
	CHECK(p->init(&fake) == 0);
	testGames(p);
	testRoot(p);
	testKeptOrder(p);
	p->quit();

	testShown();
	CHECK_DONE();
}
