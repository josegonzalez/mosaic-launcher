#include "check.h"
#include "entrysets.h"

static int hasA(const char* name, const char* path) { (void)name; (void)path; return 1; }
static int hasB(const char* name, const char* path) { (void)name; (void)path; return 0; }
static void listA(const char* name, void (*add)(const char* path, void* ctx), void* ctx) { (void)name; add("/a", ctx); }

static void testNames(void) {
	CHECK(EntrySets_nameAllowed("favorites", "favorites"));
	CHECK(EntrySets_nameAllowed("favorites", "rival"));
	CHECK(EntrySets_nameAllowed("hidden", "hidden"));
	CHECK(EntrySets_nameAllowed("tags:RPG", "tags"));
	CHECK(EntrySets_nameAllowed("tags:Two words", "tags"));
	CHECK(EntrySets_nameAllowed("smart:Mario", "smart-collections"));
	CHECK(EntrySets_nameAllowed("music.liked", "music"));
	// a prefix needs something after it
	CHECK(!EntrySets_nameAllowed("tags:", "tags"));
	CHECK(!EntrySets_nameAllowed("smart:", "smart-collections"));
	CHECK(!EntrySets_nameAllowed("music.", "music"));
	// anything else is another plugin's, or no one's
	CHECK(!EntrySets_nameAllowed("liked", "music"));
	CHECK(!EntrySets_nameAllowed("romm.liked", "music"));
	CHECK(!EntrySets_nameAllowed("musicx.liked", "music"));
	CHECK(!EntrySets_nameAllowed("Favorites", "favorites"));
	CHECK(!EntrySets_nameAllowed("tag:RPG", "tags"));
	CHECK(!EntrySets_nameAllowed("", "music"));
	CHECK(!EntrySets_nameAllowed(NULL, "music"));
	CHECK(!EntrySets_nameAllowed("favorites", NULL));
	CHECK(!EntrySets_nameAllowed("favorites", ""));
	CHECK(!EntrySets_nameAllowed("tags:a\tb", "tags"));
	CHECK(!EntrySets_nameAllowed("tags:a\nb", "tags"));
	char name[SET_NAME_MAX + 1];
	memset(name, 'a', sizeof(name) - 1);
	memcpy(name, "tags:", 5);
	name[sizeof(name) - 1] = '\0';
	CHECK(!EntrySets_nameAllowed(name, "tags"));
	name[SET_NAME_MAX - 1] = '\0';
	CHECK(EntrySets_nameAllowed(name, "tags"));
}

// the first to register a name owns it
static void testOwnership(void) {
	EntrySets s;
	EntrySets_init(&s);
	CHECK(EntrySets_register(&s, "favorites", "favorites", hasA, listA) == SET_OK);
	CHECK(EntrySets_register(&s, "favorites", "rival", hasB, listA) == SET_TAKEN);
	EntrySet* set = EntrySets_find(&s, "favorites");
	CHECK(set && !strcmp(set->owner, "favorites") && set->has == hasA);
	// the owner can register it again, with new callbacks
	CHECK(EntrySets_register(&s, "favorites", "favorites", hasB, listA) == SET_OK);
	set = EntrySets_find(&s, "favorites");
	CHECK(set && set->has == hasB);
	CHECK(s.count == 1);
	// exact names only
	CHECK(EntrySets_find(&s, "Favorites") == NULL);
	CHECK(EntrySets_find(&s, "hidden") == NULL);
	CHECK(EntrySets_find(&s, NULL) == NULL);
	// reserved and bad
	CHECK(EntrySets_register(&s, "romm.liked", "music", hasA, listA) == SET_RESERVED);
	CHECK(EntrySets_register(&s, "liked", "music", hasA, listA) == SET_RESERVED);
	CHECK(EntrySets_register(&s, "music.liked", "music", NULL, listA) == SET_BAD);
	CHECK(EntrySets_register(&s, "music.liked", "music", hasA, NULL) == SET_BAD);
	CHECK(EntrySets_register(&s, NULL, "music", hasA, listA) == SET_BAD);
	CHECK(EntrySets_register(&s, "music.liked", NULL, hasA, listA) == SET_BAD);
	CHECK(EntrySets_register(&s, "", "music", hasA, listA) == SET_BAD);
	CHECK(s.count == 1);
	// only the owner unregisters, and then anyone can take the name
	CHECK(EntrySets_unregister(&s, "favorites", "rival") == -1);
	CHECK(EntrySets_unregister(&s, "hidden", "favorites") == -1);
	CHECK(EntrySets_unregister(&s, "favorites", NULL) == -1);
	CHECK(EntrySets_unregister(&s, "favorites", "favorites") == 0);
	CHECK(EntrySets_find(&s, "favorites") == NULL);
	CHECK(EntrySets_unregister(&s, "favorites", "favorites") == -1);
	CHECK(EntrySets_register(&s, "favorites", "rival", hasB, listA) == SET_OK);
	set = EntrySets_find(&s, "favorites");
	CHECK(set && !strcmp(set->owner, "rival"));
	EntrySets_free(&s);
	CHECK(s.count == 0 && s.items == NULL);
}

// unloading a plugin releases its sets, and only its
static void testRelease(void) {
	EntrySets s;
	EntrySets_init(&s);
	char name[32];
	for (int i = 0; i < 40; i++) {
		snprintf(name, sizeof(name), "tags:t%d", i);
		CHECK(EntrySets_register(&s, name, "tags", hasA, listA) == SET_OK);
		if (i == 10) CHECK(EntrySets_register(&s, "hidden", "hidden", hasA, listA) == SET_OK);
	}
	CHECK(EntrySets_register(&s, "tags.extra", "tags", hasA, listA) == SET_OK);
	CHECK(s.count == 42);
	CHECK(EntrySets_releaseAll(&s, "tags") == 41);
	CHECK(s.count == 1);
	CHECK(EntrySets_find(&s, "tags:t0") == NULL);
	CHECK(EntrySets_find(&s, "tags:t39") == NULL);
	EntrySet* set = EntrySets_find(&s, "hidden");
	CHECK(set && !strcmp(set->owner, "hidden"));
	CHECK(EntrySets_releaseAll(&s, "tags") == 0);
	CHECK(EntrySets_releaseAll(&s, NULL) == 0);
	// a released name is free for another plugin
	CHECK(EntrySets_register(&s, "tags:t0", "rival", hasB, listA) == SET_OK);
	CHECK(EntrySets_releaseAll(&s, "hidden") == 1);
	CHECK(EntrySets_find(&s, "hidden") == NULL);
	CHECK(s.count == 1);
	EntrySets_free(&s);
}

// unregistering keeps the order of the rest
static void testOrder(void) {
	EntrySets s;
	EntrySets_init(&s);
	CHECK(EntrySets_register(&s, "smart:A", "smart-collections", hasA, listA) == SET_OK);
	CHECK(EntrySets_register(&s, "smart:B", "smart-collections", hasA, listA) == SET_OK);
	CHECK(EntrySets_register(&s, "smart:C", "smart-collections", hasA, listA) == SET_OK);
	CHECK(EntrySets_unregister(&s, "smart:B", "smart-collections") == 0);
	CHECK(s.count == 2);
	CHECK_STR(s.items[0].name, "smart:A");
	CHECK_STR(s.items[1].name, "smart:C");
	CHECK(s.items[0].busy == 0);
	EntrySets_free(&s);
}

static void testFull(void) {
	EntrySets s;
	EntrySets_init(&s);
	char name[32];
	for (int i = 0; i < SETS_MAX; i++) {
		snprintf(name, sizeof(name), "tags:t%d", i);
		if (EntrySets_register(&s, name, "tags", hasA, listA) != SET_OK) {
			CHECK(0);
			break;
		}
	}
	CHECK(EntrySets_register(&s, "hidden", "hidden", hasA, listA) == SET_FULL);
	// one already there still re-registers
	CHECK(EntrySets_register(&s, "tags:t0", "tags", hasB, listA) == SET_OK);
	CHECK(EntrySets_unregister(&s, "tags:t5", "tags") == 0);
	CHECK(EntrySets_register(&s, "hidden", "hidden", hasA, listA) == SET_OK);
	EntrySets_free(&s);
}

int main(void) {
	testNames();
	testOwnership();
	testRelease();
	testOrder();
	testFull();
	CHECK_DONE();
}
