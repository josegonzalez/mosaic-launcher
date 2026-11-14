// The launcher's built-in lock and the ambient screens' order.

#include <stdlib.h>
#include "check.h"
#include "lockmodel.h"
#include "ambientmodel.h"

#define BIT MOSAIC_BUTTON_BIT
#define CHORD (BIT(MOSAIC_BUTTON_SELECT) | BIT(MOSAIC_BUTTON_START))

///////////////////////////////////////
// the lock

static void testParseChord(void) {
	unsigned int mask = 7;
	CHECK(Lock_parseChord("select+start", &mask) && mask == CHORD);
	CHECK(Lock_parseChord("L1+R1", &mask) && mask == (BIT(MOSAIC_BUTTON_L1) | BIT(MOSAIC_BUTTON_R1)));
	CHECK(Lock_parseChord("menu+down", &mask) && mask == (BIT(MOSAIC_BUTTON_MENU) | BIT(MOSAIC_BUTTON_DOWN)));
	CHECK(Lock_parseChord("y", &mask) && mask == BIT(MOSAIC_BUTTON_Y));
	CHECK(Lock_parseChord("off", &mask) && mask == 0);
	mask = 7;
	CHECK(!Lock_parseChord("select+", &mask) && mask == 7);
	CHECK(!Lock_parseChord("+start", &mask));
	CHECK(!Lock_parseChord("select+power", &mask));
	CHECK(!Lock_parseChord("", &mask));
	CHECK(!Lock_parseChord(NULL, &mask));
	CHECK(mask == 7);
}

static void testParseHold(void) {
	CHECK(Lock_parseHoldMs("1", 9) == 1000);
	CHECK(Lock_parseHoldMs("3", 9) == 3000);
	CHECK(Lock_parseHoldMs("5", 9) == 5000);
	CHECK(Lock_parseHoldMs("6", 9) == 9);
	CHECK(Lock_parseHoldMs("0", 9) == 9);
	CHECK(Lock_parseHoldMs("-1", 9) == 9);
	CHECK(Lock_parseHoldMs("1s", 9) == 9);
	CHECK(Lock_parseHoldMs("", 9) == 9);
	CHECK(Lock_parseHoldMs(NULL, 9) == 9);
}

static void testChord(void) {
	LockState lock;
	unsigned int taken = 1;
	Lock_init(&lock, CHORD, 1000, 0);
	CHECK(Lock_frame(&lock, 0, -1, 100, &taken) == LOCK_SAME && taken == 0);
	// one of the two isn't enough
	CHECK(Lock_frame(&lock, BIT(MOSAIC_BUTTON_SELECT), -1, 200, &taken) == LOCK_SAME && taken == 0);
	CHECK(Lock_frame(&lock, BIT(MOSAIC_BUTTON_SELECT), -1, 2000, &taken) == LOCK_SAME && taken == 0);
	// both, with other buttons too, for a second
	unsigned int both = CHORD | BIT(MOSAIC_BUTTON_A);
	CHECK(Lock_frame(&lock, both, -1, 3000, &taken) == LOCK_SAME && taken == 0);
	CHECK(Lock_frame(&lock, both, -1, 3999, &taken) == LOCK_SAME && taken == 0);
	CHECK(Lock_frame(&lock, both, -1, 4000, &taken) == LOCK_LOCKED_CHORD && taken == ~0u);
	CHECK(Lock_locked(&lock));
	// still held: nothing more until it's let go
	CHECK(Lock_frame(&lock, both, -1, 9000, &taken) == LOCK_SAME && taken == ~0u);
	CHECK(Lock_frame(&lock, 0, -1, 9100, &taken) == LOCK_SAME && taken == ~0u);
	CHECK(Lock_frame(&lock, BIT(MOSAIC_BUTTON_DOWN), -1, 9200, &taken) == LOCK_SAME && taken == ~0u);
	// a short hold doesn't unlock
	CHECK(Lock_frame(&lock, CHORD, -1, 10000, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, 0, -1, 10500, &taken) == LOCK_SAME && Lock_locked(&lock));
	// a full one does, then takes everything until it's all let go
	CHECK(Lock_frame(&lock, CHORD, -1, 11000, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, -1, 12000, &taken) == LOCK_UNLOCKED_CHORD && taken == ~0u);
	CHECK(!Lock_locked(&lock));
	CHECK(Lock_frame(&lock, BIT(MOSAIC_BUTTON_START), -1, 12100, &taken) == LOCK_SAME && taken == ~0u);
	CHECK(Lock_frame(&lock, 0, -1, 12200, &taken) == LOCK_SAME && taken == ~0u);
	CHECK(Lock_frame(&lock, 0, -1, 12300, &taken) == LOCK_SAME && taken == 0);
	CHECK(Lock_frame(&lock, BIT(MOSAIC_BUTTON_DOWN), -1, 12400, &taken) == LOCK_SAME && taken == 0);
	// the clock wrapping past zero still counts the hold
	Lock_init(&lock, CHORD, 1000, 0);
	CHECK(Lock_frame(&lock, CHORD, -1, (unsigned long)-500, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, -1, 499, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, -1, 500, &taken) == LOCK_LOCKED_CHORD);
	// no chord never locks
	Lock_init(&lock, 0, 1000, 0);
	CHECK(Lock_frame(&lock, 0, -1, 0, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, ~0u, -1, 5000, &taken) == LOCK_SAME && taken == 0);
}

static void testSwitch(void) {
	LockState lock;
	unsigned int taken = 1;
	// unused, the switch does nothing
	Lock_init(&lock, CHORD, 1000, 0);
	CHECK(Lock_frame(&lock, 0, 1, 0, &taken) == LOCK_SAME && taken == 0);
	// used, on locks and off unlocks
	Lock_init(&lock, CHORD, 1000, 1);
	CHECK(Lock_frame(&lock, 0, 0, 0, &taken) == LOCK_SAME && taken == 0);
	CHECK(Lock_frame(&lock, 0, 1, 100, &taken) == LOCK_LOCKED_SWITCH && taken == ~0u);
	// the chord can't unlock it while it's on
	CHECK(Lock_frame(&lock, CHORD, 1, 200, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, 1, 1300, &taken) == LOCK_SAME && Lock_locked(&lock));
	CHECK(Lock_frame(&lock, 0, 1, 1400, &taken) == LOCK_SAME && taken == ~0u);
	// switched off with a button down: taken until it's let go
	CHECK(Lock_frame(&lock, BIT(MOSAIC_BUTTON_A), 0, 1500, &taken) == LOCK_UNLOCKED_SWITCH && taken == ~0u);
	CHECK(!Lock_locked(&lock));
	CHECK(Lock_frame(&lock, 0, 0, 1600, &taken) == LOCK_SAME && taken == ~0u);
	CHECK(Lock_frame(&lock, 0, 0, 1700, &taken) == LOCK_SAME && taken == 0);
	// a chord lock outlasts the switch
	CHECK(Lock_frame(&lock, CHORD, 0, 2000, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, 0, 3000, &taken) == LOCK_LOCKED_CHORD);
	CHECK(Lock_frame(&lock, 0, 1, 3100, &taken) == LOCK_LOCKED_SWITCH);
	CHECK(Lock_frame(&lock, 0, 0, 3200, &taken) == LOCK_UNLOCKED_SWITCH && taken == ~0u && Lock_locked(&lock));
	CHECK(Lock_frame(&lock, 0, 0, 3300, &taken) == LOCK_SAME && taken == ~0u);
	// no switch on the device
	Lock_init(&lock, CHORD, 1000, 1);
	CHECK(Lock_frame(&lock, 0, -1, 0, &taken) == LOCK_SAME && taken == 0);
}

static int inside(const MosaicRect* in, const MosaicRect* out) {
	return in->w > 0 && in->h > 0 && in->x >= out->x && in->y >= out->y
		&& in->x + in->w <= out->x + out->w && in->y + in->h <= out->y + out->h;
}

static void testPadlock(void) {
	int heights[] = { 30, 60, 90 };
	for (int i = 0; i < 3; i++) {
		MosaicRect rect = { 500, 15, heights[i], heights[i] };
		LockPadlock p;
		Lock_padlock(&rect, &p);
		CHECK(inside(&p.shackle, &rect));
		CHECK(inside(&p.body, &rect));
		CHECK(inside(&p.hole, &p.shackle));
		CHECK(inside(&p.keyhole, &p.body));
		// the shackle's legs run into the body, and the body is wider
		CHECK(p.shackle.y + p.shackle.h > p.body.y);
		CHECK(p.hole.y + p.hole.h > p.body.y);
		CHECK(p.body.w > p.shackle.w);
		CHECK(p.hole.y > p.shackle.y && p.hole.x > p.shackle.x);
		// centred
		int mid = rect.x + rect.w / 2;
		CHECK(abs(p.body.x + p.body.w / 2 - mid) <= 1);
		CHECK(abs(p.shackle.x + p.shackle.w / 2 - mid) <= 1);
	}
}

static void testRequest(void) {
	LockState lock;
	unsigned int taken = 1;
	// with no chord nothing could unlock it
	Lock_init(&lock, 0, 1000, 1);
	CHECK(Lock_request(&lock) == -1 && !Lock_locked(&lock));
	// the switch holds it already
	CHECK(Lock_frame(&lock, 0, 1, 0, &taken) == LOCK_LOCKED_SWITCH);
	CHECK(Lock_request(&lock) == 0);
	// a plugin locks; the chord unlocks
	Lock_init(&lock, CHORD, 1000, 0);
	CHECK(Lock_request(&lock) == 1 && Lock_locked(&lock));
	CHECK(Lock_request(&lock) == 0);
	CHECK(Lock_frame(&lock, 0, -1, 100, &taken) == LOCK_SAME && taken == ~0u);
	CHECK(Lock_frame(&lock, CHORD, -1, 200, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, -1, 1200, &taken) == LOCK_UNLOCKED_CHORD && !Lock_locked(&lock));
	// a chord already held as it locks doesn't unlock it until it's let go
	Lock_init(&lock, CHORD, 1000, 0);
	CHECK(Lock_frame(&lock, CHORD, -1, 0, &taken) == LOCK_SAME);
	CHECK(Lock_request(&lock) == 1);
	CHECK(Lock_frame(&lock, CHORD, -1, 5000, &taken) == LOCK_SAME && Lock_locked(&lock));
	CHECK(Lock_frame(&lock, 0, -1, 5100, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, -1, 5200, &taken) == LOCK_SAME);
	CHECK(Lock_frame(&lock, CHORD, -1, 6200, &taken) == LOCK_UNLOCKED_CHORD);
}

static void testChordName(void) {
	char name[32];
	unsigned int mask = 0;
	Lock_parseChord("start+select", &mask);
	Lock_chordName(mask, name, sizeof(name));
	CHECK_STR(name, "SELECT+START");
	Lock_parseChord("l2+r2", &mask);
	Lock_chordName(mask, name, sizeof(name));
	CHECK_STR(name, "L2+R2");
	Lock_chordName(0, name, sizeof(name));
	CHECK_STR(name, "");
	Lock_parseChord("select+start", &mask);
	Lock_chordName(mask, name, 8);
	CHECK_STR(name, "SELECT");
}

///////////////////////////////////////
// the ambient screens' order

static void checkOrder(const char* setting, const char* const* ids, int count, const char* want) {
	int order[AMBIENT_MAX];
	Ambient_order(setting, ids, count, order);
	char got[256] = "";
	for (int i = 0; i < count; i++) {
		strcat(got, i ? "," : "");
		strcat(got, ids[order[i]]);
	}
	CHECK_STR(got, want);
}

static void testOrder(void) {
	const char* loaded[] = { "screensaver.main", "lock", "test.b", "charging-screen.main" };
	// the default: lock, charging, screensaver, then the rest as they loaded
	checkOrder(NULL, loaded, 4, "lock,charging-screen.main,screensaver.main,test.b");
	checkOrder("", loaded, 4, "lock,charging-screen.main,screensaver.main,test.b");
	checkOrder("test.b,screensaver.main", loaded, 4, "test.b,screensaver.main,lock,charging-screen.main");
	// ids that aren't loaded, repeats and spaces don't matter
	checkOrder(" gone.x , test.b,test.b,,lock ", loaded, 4, "test.b,lock,screensaver.main,charging-screen.main");
	checkOrder("lock", loaded, 1, "screensaver.main");
	checkOrder(NULL, loaded, 0, "");
}

static void testMove(void) {
	const char* shown[] = { "lock", "screensaver.main", "test.b" };
	char out[256];
	// down, keeping charging-screen, which is off, where it was
	CHECK(Ambient_move(NULL, shown, 3, 0, 1, out, sizeof(out)) == 1);
	CHECK_STR(out, "screensaver.main,charging-screen.main,lock,test.b");
	const char* after[] = { "screensaver.main", "lock", "test.b" };
	checkOrder(out, after, 3, "screensaver.main,lock,test.b");
	// up, past what the setting didn't name yet
	CHECK(Ambient_move("lock,screensaver.main", shown, 3, 2, -1, out, sizeof(out)) == 1);
	CHECK_STR(out, "lock,test.b,screensaver.main");
	// not past either end
	CHECK(Ambient_move(NULL, shown, 3, 0, -1, out, sizeof(out)) == -1);
	CHECK(Ambient_move(NULL, shown, 3, 2, 1, out, sizeof(out)) == -1);
	CHECK(Ambient_move(NULL, shown, 3, 5, 1, out, sizeof(out)) == -1);
	CHECK(Ambient_move(NULL, shown, 3, 1, 2, out, sizeof(out)) == -1);
	// nor into too small a setting
	CHECK(Ambient_move(NULL, shown, 3, 0, 1, out, 10) == -1);
}

static int wantsNone(unsigned long idle_ms, void* data) {
	(void)idle_ms;
	(void)data;
	return 0;
}

static void testSet(void) {
	AmbientSet set;
	AmbientSet_clear(&set);
	CHECK(AmbientSet_add(&set, AMBIENT_LOCK, -1, wantsNone, NULL) == AMBIENT_ADDED);
	CHECK(AmbientSet_add(&set, "a.one", 0, wantsNone, NULL) == AMBIENT_ADDED);
	CHECK(AmbientSet_add(&set, "b.one", 1, wantsNone, NULL) == AMBIENT_ADDED);
	CHECK(AmbientSet_add(&set, "a.two", 0, wantsNone, NULL) == AMBIENT_ADDED);
	CHECK(AmbientSet_add(&set, "a.one", 1, wantsNone, NULL) == AMBIENT_TAKEN);
	CHECK(AmbientSet_add(&set, "", 1, wantsNone, NULL) == AMBIENT_BAD);
	CHECK(AmbientSet_add(&set, "c.one", 1, NULL, NULL) == AMBIENT_BAD);
	CHECK(set.count == 4);
	AmbientSet_sort(&set, "b.one,a.two");
	CHECK_STR(set.items[set.order[0]].id, "b.one");
	CHECK_STR(set.items[set.order[1]].id, "a.two");
	CHECK_STR(set.items[set.order[2]].id, "lock");
	CHECK_STR(set.items[set.order[3]].id, "a.one");
	CHECK(AmbientSet_find(&set, "b.one") == 2 && AmbientSet_find(&set, "x") == -1);
	CHECK(AmbientSet_dropOwner(&set, 0) == 2 && set.count == 2);
	CHECK(AmbientSet_find(&set, "b.one") == 1);
	for (int i = 0; set.count < AMBIENT_MAX; i++) {
		char id[16];
		snprintf(id, sizeof(id), "f.%i", i);
		CHECK(AmbientSet_add(&set, id, 2, wantsNone, NULL) == AMBIENT_ADDED);
	}
	CHECK(AmbientSet_add(&set, "f.last", 2, wantsNone, NULL) == AMBIENT_FULL);
}

int main(void) {
	testParseChord();
	testParseHold();
	testChord();
	testSwitch();
	testPadlock();
	testRequest();
	testChordName();
	testOrder();
	testMove();
	testSet();
	CHECK_DONE();
}
