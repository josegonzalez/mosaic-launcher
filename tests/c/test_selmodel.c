// selmodel.c: seeing the selection move, and when it has settled.

#include "check.h"
#include "selmodel.h"

static void testKey(void) {
	int top = 0, list = 0, other = 0;
	SelKey key;
	SelKey_reset(&key);
	// the first look is a change, even at index -1
	CHECK(SelKey_changed(&key, NULL, NULL, -1) == 1);
	CHECK(SelKey_changed(&key, NULL, NULL, -1) == 0);
	CHECK(SelKey_changed(&key, &top, &list, 0) == 1);
	CHECK(SelKey_changed(&key, &top, &list, 0) == 0);
	// a new index, folder or list each count
	CHECK(SelKey_changed(&key, &top, &list, 1) == 1);
	CHECK(SelKey_changed(&key, &other, &list, 1) == 1);
	CHECK(SelKey_changed(&key, &other, &other, 1) == 1);
	CHECK(SelKey_changed(&key, &other, &other, 1) == 0);
	SelKey_reset(&key);
	CHECK(SelKey_changed(&key, &other, &other, 1) == 1);
}

// frames 16 ms apart, as the main loop runs
#define FRAME 16

static void testFirst(void) {
	SelSettle s;
	SelSettle_init(&s);
	// nothing happens before the first move
	CHECK(SelSettle_step(&s, 1000, 0, 0, "/sd", "/sd/A") == 0);
	// the first selection is news once it settles
	CHECK(SelSettle_step(&s, 1000, 1, 0, "/sd", "/sd/A") == 0);
	CHECK(SelSettle_step(&s, 1000 + SEL_SETTLE_MS - 1, 0, 0, "/sd", "/sd/A") == 0);
	CHECK(SelSettle_step(&s, 1000 + SEL_SETTLE_MS, 0, 0, "/sd", "/sd/A") == 1);
	CHECK_STR(s.folder, "/sd");
	CHECK_STR(s.entry, "/sd/A");
	// and only once
	CHECK(SelSettle_step(&s, 5000, 0, 0, "/sd", "/sd/A") == 0);
}

static void testFastScroll(void) {
	SelSettle s;
	SelSettle_init(&s);
	unsigned long now = 0;
	SelSettle_step(&s, now, 1, 0, "/sd/Roms", "/sd/Roms/0");
	now += SEL_SETTLE_MS;
	CHECK(SelSettle_step(&s, now, 0, 0, "/sd/Roms", "/sd/Roms/0") == 1);

	// held down: a move every 100 ms after 300, as stock repeats, with
	// frames in between, and nothing goes out until it's let go and settles
	char entry[32];
	int fired = 0;
	for (int i = 1; i <= 30; i++) {
		snprintf(entry, sizeof(entry), "/sd/Roms/%i", i);
		for (int f = 0; f < 6; f++) {
			now += FRAME;
			fired += SelSettle_step(&s, now, f == 0, 1, "/sd/Roms", entry);
		}
	}
	CHECK(fired == 0);
	// let go: still not settled until SEL_SETTLE_MS after the last move
	unsigned long last = now - 5 * FRAME;
	while (now + FRAME < last + SEL_SETTLE_MS) {
		now += FRAME;
		CHECK(SelSettle_step(&s, now, 0, 0, "/sd/Roms", entry) == 0);
	}
	CHECK(SelSettle_step(&s, last + SEL_SETTLE_MS, 0, 0, "/sd/Roms", entry) == 1);
	CHECK_STR(s.entry, "/sd/Roms/30");

	// tapping faster than it settles is one event too, at the end
	fired = 0;
	for (int i = 0; i < 10; i++) {
		snprintf(entry, sizeof(entry), "/sd/Roms/%i", 40 + i);
		now += SEL_SETTLE_MS / 2;
		fired += SelSettle_step(&s, now, 1, 0, "/sd/Roms", entry);
	}
	CHECK(fired == 0);
	CHECK(SelSettle_step(&s, now + SEL_SETTLE_MS, 0, 0, "/sd/Roms", entry) == 1);
	CHECK_STR(s.entry, "/sd/Roms/49");
}

static void testHeld(void) {
	SelSettle s;
	SelSettle_init(&s);
	SelSettle_step(&s, 0, 1, 0, "/sd", "/sd/A");
	CHECK(SelSettle_step(&s, SEL_SETTLE_MS, 0, 0, "/sd", "/sd/A") == 1);
	// held at the end of the list: no moves, but not settled while held
	SelSettle_step(&s, 1000, 1, 1, "/sd", "/sd/Z");
	CHECK(SelSettle_step(&s, 3000, 0, 1, "/sd", "/sd/Z") == 0);
	// let go long after the last move: settled at once
	CHECK(SelSettle_step(&s, 3016, 0, 0, "/sd", "/sd/Z") == 1);
	// holding a button with no move since is nothing
	CHECK(SelSettle_step(&s, 4000, 0, 1, "/sd", "/sd/Z") == 0);
	CHECK(SelSettle_step(&s, 5000, 0, 0, "/sd", "/sd/Z") == 0);
}

static void testFolders(void) {
	SelSettle s;
	SelSettle_init(&s);
	SelSettle_step(&s, 0, 1, 0, "/sd", "/sd/Roms/GB");
	CHECK(SelSettle_step(&s, SEL_SETTLE_MS, 0, 0, "/sd", "/sd/Roms/GB") == 1);
	// opening a folder is news for the new folder's selection
	SelSettle_step(&s, 1000, 1, 0, "/sd/Roms/GB", "/sd/Roms/GB/A.gb");
	CHECK(SelSettle_step(&s, 1000 + SEL_SETTLE_MS, 0, 0, "/sd/Roms/GB", "/sd/Roms/GB/A.gb") == 1);
	CHECK_STR(s.folder, "/sd/Roms/GB");
	// the same game listed in another folder, such as Recently Played, is news too
	SelSettle_step(&s, 2000, 1, 0, "/sd/Recently Played", "/sd/Roms/GB/A.gb");
	CHECK(SelSettle_step(&s, 2000 + SEL_SETTLE_MS, 0, 0, "/sd/Recently Played", "/sd/Roms/GB/A.gb") == 1);
	// backing out and straight in again without settling outside is nothing
	SelSettle_step(&s, 3000, 1, 0, "/sd", "/sd/Recently Played");
	SelSettle_step(&s, 3100, 1, 0, "/sd/Recently Played", "/sd/Roms/GB/A.gb");
	CHECK(SelSettle_step(&s, 3100 + SEL_SETTLE_MS, 0, 0, "/sd/Recently Played", "/sd/Roms/GB/A.gb") == 0);
	// an empty folder is news, with no entry
	SelSettle_step(&s, 4000, 1, 0, "/sd/Empty", NULL);
	CHECK(SelSettle_step(&s, 4000 + SEL_SETTLE_MS, 0, 0, "/sd/Empty", NULL) == 1);
	CHECK_STR(s.entry, "");
	SelSettle_step(&s, 5000, 1, 0, "/sd/Empty", NULL);
	CHECK(SelSettle_step(&s, 5000 + SEL_SETTLE_MS, 0, 0, "/sd/Empty", NULL) == 0);
}

static void testReload(void) {
	SelSettle s;
	SelSettle_init(&s);
	SelSettle_step(&s, 0, 1, 0, "/sd/Roms", "/sd/Roms/B");
	CHECK(SelSettle_step(&s, SEL_SETTLE_MS, 0, 0, "/sd/Roms", "/sd/Roms/B") == 1);
	// a reload is a move (a new list) that keeps the same entry: nothing
	SelSettle_step(&s, 1000, 1, 0, "/sd/Roms", "/sd/Roms/B");
	CHECK(SelSettle_step(&s, 1000 + SEL_SETTLE_MS, 0, 0, "/sd/Roms", "/sd/Roms/B") == 0);
	// one that leaves another entry selected, eg. B was deleted, is news
	SelSettle_step(&s, 2000, 1, 0, "/sd/Roms", "/sd/Roms/C");
	CHECK(SelSettle_step(&s, 2000 + SEL_SETTLE_MS, 0, 0, "/sd/Roms", "/sd/Roms/C") == 1);
	// moving away and back before it settles is nothing
	SelSettle_step(&s, 3000, 1, 0, "/sd/Roms", "/sd/Roms/D");
	SelSettle_step(&s, 3100, 1, 0, "/sd/Roms", "/sd/Roms/C");
	CHECK(SelSettle_step(&s, 3100 + SEL_SETTLE_MS, 0, 0, "/sd/Roms", "/sd/Roms/C") == 0);
}

static void testPaths(void) {
	SelSettle s;
	SelSettle_init(&s);
	SelSettle_step(&s, 0, 1, 0, "/sd", "/sd/A");
	CHECK(SelSettle_step(&s, SEL_SETTLE_MS, 0, 0, "/sd", "/sd/A") == 1);
	// another entry with no move seen, as when a reloaded list reuses the
	// last one's memory, still counts as a move
	CHECK(SelSettle_step(&s, 1000, 0, 0, "/sd", "/sd/B") == 0);
	CHECK(SelSettle_step(&s, 1000 + SEL_SETTLE_MS - 1, 0, 0, "/sd", "/sd/B") == 0);
	CHECK(SelSettle_step(&s, 1000 + SEL_SETTLE_MS, 0, 0, "/sd", "/sd/B") == 1);
	// so does another folder
	CHECK(SelSettle_step(&s, 2000, 0, 0, "/sd/Roms", "/sd/B") == 0);
	CHECK(SelSettle_step(&s, 2000 + SEL_SETTLE_MS, 0, 0, "/sd/Roms", "/sd/B") == 1);
}

static void testWrap(void) {
	// SDL_GetTicks wrapping past the top of an unsigned long
	SelSettle s;
	SelSettle_init(&s);
	unsigned long start = (unsigned long)-100;
	SelSettle_step(&s, start, 1, 0, "/sd", "/sd/A");
	CHECK(SelSettle_step(&s, start + 50, 0, 0, "/sd", "/sd/A") == 0);
	CHECK(SelSettle_step(&s, start + SEL_SETTLE_MS, 0, 0, "/sd", "/sd/A") == 1);
}

int main(void) {
	testKey();
	testFirst();
	testFastScroll();
	testHeld();
	testFolders();
	testReload();
	testPaths();
	testWrap();
	CHECK_DONE();
}
