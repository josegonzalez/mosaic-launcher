// screenmodel.c: registering plugins' screens, the stack of those open,
// and which screens slide and carry overlays.

#include "check.h"
#include "screenmodel.h"

static int draw(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)screen;
	(void)view;
	(void)data;
	return 0;
}
static int enter(void* data) {
	(void)data;
	return 0;
}

static MosaicScreenHooks hooks(void) {
	MosaicScreenHooks h = { sizeof(MosaicScreenHooks), NULL, 0, enter, NULL, NULL, draw, NULL };
	return h;
}

static void testAdd(void) {
	ScreenSet set;
	ScreenSet_clear(&set);
	MosaicScreenHooks h = hooks();
	CHECK(ScreenSet_add(&set, "a.one", 0, &h) == SCREEN_ADDED);
	CHECK(set.count == 1 && set.screens[0].owner == 0);
	CHECK(ScreenSet_find(&set, "a.one") == 0);
	CHECK(ScreenSet_find(&set, "a.two") == -1);
	CHECK(ScreenSet_find(&set, NULL) == -1);

	// ids
	CHECK(ScreenSet_add(&set, "a.one", 1, &h) == SCREEN_TAKEN);
	CHECK(ScreenSet_add(&set, NULL, 0, &h) == SCREEN_BAD_ID);
	CHECK(ScreenSet_add(&set, "", 0, &h) == SCREEN_BAD_ID);
	char longest[SCREEN_ID_MAX + 1];
	memset(longest, 'x', sizeof(longest));
	longest[SCREEN_ID_MAX - 1] = '\0';
	CHECK(ScreenSet_add(&set, longest, 0, &h) == SCREEN_ADDED);
	longest[SCREEN_ID_MAX - 1] = 'x';
	longest[SCREEN_ID_MAX] = '\0';
	CHECK(ScreenSet_add(&set, longest, 0, &h) == SCREEN_BAD_ID);
	const char* builtin[] = { "gamelist", "switcher", "quickmenu", "dialog" };
	for (int i = 0; i < 4; i++) {
		CHECK(Screen_builtinName(builtin[i]));
		CHECK(ScreenSet_add(&set, builtin[i], 0, &h) == SCREEN_BAD_ID);
	}
	CHECK(!Screen_builtinName("gamelist2"));
	CHECK(!Screen_builtinName(NULL));

	// hooks: render is required, and the size has to reach it
	CHECK(ScreenSet_add(&set, "a.null", 0, NULL) == SCREEN_BAD_HOOKS);
	MosaicScreenHooks none = hooks();
	none.render = NULL;
	CHECK(ScreenSet_add(&set, "a.none", 0, &none) == SCREEN_BAD_HOOKS);
	MosaicScreenHooks small = hooks();
	small.size = offsetof(MosaicScreenHooks, render);
	CHECK(ScreenSet_add(&set, "a.small", 0, &small) == SCREEN_BAD_HOOKS);
	CHECK(set.count == 2);

	// an older plugin's hooks: what's past its size is left zero
	MosaicScreenHooks old = hooks();
	old.size = offsetof(MosaicScreenHooks, animating);
	old.animating = (int (*)(void*))enter;
	CHECK(ScreenSet_add(&set, "a.old", 0, &old) == SCREEN_ADDED);
	const PluginScreen* s = &set.screens[ScreenSet_find(&set, "a.old")];
	CHECK(s->hooks.animating == NULL && s->hooks.render == draw && s->hooks.enter == enter);
	CHECK(s->hooks.size == sizeof(MosaicScreenHooks));

	// full
	ScreenSet_clear(&set);
	for (int i = 0; i < SCREENS_MAX; i++) {
		char id[16];
		snprintf(id, sizeof(id), "s.%i", i);
		CHECK(ScreenSet_add(&set, id, 0, &h) == SCREEN_ADDED);
	}
	CHECK(ScreenSet_add(&set, "s.more", 0, &h) == SCREEN_FULL);
}

static void testDropOwner(void) {
	ScreenSet set;
	ScreenSet_clear(&set);
	MosaicScreenHooks h = hooks();
	ScreenSet_add(&set, "a.one", 0, &h);
	ScreenSet_add(&set, "b.one", 1, &h);
	ScreenSet_add(&set, "b.two", 1, &h);
	CHECK(ScreenSet_dropOwner(&set, 1) == 2);
	CHECK(set.count == 1 && ScreenSet_find(&set, "a.one") == 0 && ScreenSet_find(&set, "b.one") == -1);
	CHECK(ScreenSet_dropOwner(&set, 5) == 0);
	// the id is free again
	CHECK(ScreenSet_add(&set, "b.one", 1, &h) == SCREEN_ADDED);
}

static void testStack(void) {
	ScreenSet set;
	ScreenSet_clear(&set);
	MosaicScreenHooks h = hooks();
	ScreenSet_add(&set, "a.one", 0, &h);
	ScreenSet_add(&set, "a.two", 0, &h);
	ScreenSet_add(&set, "a.three", 0, &h);
	CHECK(ScreenSet_top(&set) == -1);
	CHECK(ScreenSet_pop(&set) == -1);

	// the first records the built-in screen under the stack
	CHECK(ScreenSet_push(&set, 0, 7) == SCREEN_OPENS);
	CHECK(set.base == 7 && ScreenSet_top(&set) == 0);
	CHECK(ScreenSet_push(&set, 1, 9) == SCREEN_OPENS);
	CHECK(set.base == 7 && ScreenSet_top(&set) == 1);
	CHECK(ScreenSet_position(&set, 0) == 0 && ScreenSet_position(&set, 1) == 1 && ScreenSet_position(&set, 2) == -1);

	// one at a time
	CHECK(ScreenSet_canOpen(&set, 0) == SCREEN_IS_OPEN);
	CHECK(ScreenSet_push(&set, 1, 7) == SCREEN_IS_OPEN);
	CHECK(ScreenSet_canOpen(&set, -1) == SCREEN_UNKNOWN);
	CHECK(ScreenSet_canOpen(&set, 3) == SCREEN_UNKNOWN);
	set.screens[2].off = 1;
	CHECK(ScreenSet_push(&set, 2, 7) == SCREEN_IS_OFF);
	CHECK(set.depth == 2);

	// back in the order they opened
	CHECK(ScreenSet_pop(&set) == 1);
	CHECK(ScreenSet_top(&set) == 0);
	CHECK(ScreenSet_canOpen(&set, 1) == SCREEN_OPENS);
	CHECK(ScreenSet_pop(&set) == 0);
	CHECK(ScreenSet_top(&set) == -1 && set.depth == 0);

	// a new stack, a new base
	CHECK(ScreenSet_push(&set, 1, 3) == SCREEN_OPENS);
	CHECK(set.base == 3);

	// at most SCREEN_STACK_MAX open
	ScreenSet_clear(&set);
	for (int i = 0; i <= SCREEN_STACK_MAX; i++) {
		char id[16];
		snprintf(id, sizeof(id), "s.%i", i);
		ScreenSet_add(&set, id, 0, &h);
	}
	for (int i = 0; i < SCREEN_STACK_MAX; i++) CHECK(ScreenSet_push(&set, i, 0) == SCREEN_OPENS);
	CHECK(ScreenSet_push(&set, SCREEN_STACK_MAX, 0) == SCREEN_STACK_FULL);
	CHECK(ScreenSet_top(&set) == SCREEN_STACK_MAX - 1);
}

static void testSlides(void) {
	// like a folder between the list, the quick menu and plugin screens
	CHECK(ScreenKind_slides(SCREENKIND_LIST, SCREENKIND_PLUGIN));
	CHECK(ScreenKind_slides(SCREENKIND_PLUGIN, SCREENKIND_LIST));
	CHECK(ScreenKind_slides(SCREENKIND_QUICKMENU, SCREENKIND_PLUGIN));
	CHECK(ScreenKind_slides(SCREENKIND_PLUGIN, SCREENKIND_QUICKMENU));
	CHECK(ScreenKind_slides(SCREENKIND_PLUGIN, SCREENKIND_PLUGIN));
	// the switcher brings itself in, and dialogs show at once
	CHECK(!ScreenKind_slides(SCREENKIND_SWITCHER, SCREENKIND_PLUGIN));
	CHECK(!ScreenKind_slides(SCREENKIND_PLUGIN, SCREENKIND_SWITCHER));
	CHECK(!ScreenKind_slides(SCREENKIND_DIALOG, SCREENKIND_PLUGIN));
	CHECK(!ScreenKind_slides(SCREENKIND_PLUGIN, SCREENKIND_DIALOG));
}

static void testOverlays(void) {
	CHECK(ScreenKind_overlays(SCREENKIND_LIST, 0));
	CHECK(ScreenKind_overlays(SCREENKIND_QUICKMENU, 0));
	CHECK(ScreenKind_overlays(SCREENKIND_SWITCHER, 0));
	CHECK(ScreenKind_overlays(SCREENKIND_PLUGIN, 0));
	CHECK(!ScreenKind_overlays(SCREENKIND_PLUGIN, MOSAIC_SCREEN_FULL));
	CHECK(!ScreenKind_overlays(SCREENKIND_DIALOG, 0));
	// the flag only means something on a plugin screen
	CHECK(ScreenKind_overlays(SCREENKIND_LIST, MOSAIC_SCREEN_FULL));
}

int main(void) {
	testAdd();
	testDropOwner();
	testStack();
	testSlides();
	testOverlays();
	CHECK_DONE();
}
