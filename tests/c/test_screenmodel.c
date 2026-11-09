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

static void testHome(void) {
	ScreenSet set;
	ScreenSet_clear(&set);
	CHECK(set.home == -1 && !ScreenSet_homeReady(&set) && !ScreenSet_homeOpen(&set));
	CHECK(ScreenSet_homeId(&set) == NULL);
	MosaicScreenHooks plain = hooks();
	MosaicScreenHooks home = hooks();
	home.flags = MOSAIC_SCREEN_HOME | MOSAIC_SCREEN_FULL;
	ScreenSet_add(&set, "a.home", 0, &home); // owner 0 doesn't own the slot below
	ScreenSet_add(&set, "b.plain", 1, &plain);
	ScreenSet_add(&set, "b.home", 1, &home);
	ScreenSet_add(&set, "b.home2", 1, &home);

	// only the owner's, its first with the flag
	CHECK(ScreenSet_claimHome(&set, 2) == -1);
	CHECK(!ScreenSet_homeReady(&set));
	CHECK(ScreenSet_homeId(&set) == NULL); // an owner with no screen flagged
	CHECK(ScreenSet_claimHome(&set, 1) == 2);
	CHECK(set.home == 2 && ScreenSet_homeReady(&set) && !ScreenSet_homeOpen(&set));
	// its id whether or not it's open
	CHECK(ScreenSet_homeId(&set) && !strcmp(ScreenSet_homeId(&set), "b.home"));

	// open anywhere on the stack
	CHECK(ScreenSet_push(&set, 1, 0) == SCREEN_OPENS);
	CHECK(!ScreenSet_homeOpen(&set));
	CHECK(!strcmp(ScreenSet_leftFrom(&set), "list"));
	CHECK(ScreenSet_push(&set, 2, 0) == SCREEN_OPENS);
	CHECK(ScreenSet_homeOpen(&set));
	CHECK(!strcmp(ScreenSet_leftFrom(&set), "home"));
	CHECK(!strcmp(ScreenSet_homeId(&set), "b.home"));

	// a dialog or another plugin screen over it keeps the stack
	CHECK(!ScreenSet_coveredBy(&set, SCREENKIND_DIALOG));
	CHECK(!ScreenSet_coveredBy(&set, SCREENKIND_PLUGIN));
	CHECK(!set.under);
	// the quick menu over it closes the stack, and goes back to it after
	CHECK(ScreenSet_coveredBy(&set, SCREENKIND_QUICKMENU));
	CHECK(set.under);
	while (ScreenSet_pop(&set) >= 0) {}
	CHECK(!ScreenSet_coveredBy(&set, SCREENKIND_QUICKMENU));
	CHECK(set.under && !strcmp(ScreenSet_leftFrom(&set), "home"));
	CHECK(ScreenSet_backToHome(&set));
	CHECK(!set.under && !ScreenSet_backToHome(&set));

	// the game list in front forgets it, as a quick menu tile opening a folder does
	set.under = 1;
	CHECK(!ScreenSet_coveredBy(&set, SCREENKIND_LIST));
	CHECK(!set.under && !strcmp(ScreenSet_leftFrom(&set), "list"));
	// the switcher over the list alone doesn't go to it
	CHECK(!ScreenSet_coveredBy(&set, SCREENKIND_SWITCHER));
	CHECK(!set.under);

	// the list in front of it some other way closes it
	ScreenSet_push(&set, 2, 0);
	CHECK(ScreenSet_coveredBy(&set, SCREENKIND_LIST));
	CHECK(!set.under);
	ScreenSet_pop(&set);

	// after an error, never again until a restart
	set.under = 1;
	set.screens[2].off = 1;
	CHECK(!ScreenSet_homeReady(&set));
	CHECK(ScreenSet_homeId(&set) == NULL); // the list stands in for it
	CHECK(!ScreenSet_backToHome(&set));
	CHECK(!set.under);
	set.screens[2].off = 0;
	CHECK(!strcmp(ScreenSet_homeId(&set), "b.home"));

	// where the launcher starts
	CHECK(Home_start(0, NULL, SCREENKIND_LIST) == HOME_START_NONE);
	CHECK(Home_start(0, "home", SCREENKIND_LIST) == HOME_START_NONE);
	CHECK(Home_start(1, NULL, SCREENKIND_LIST) == HOME_START_OPEN);
	CHECK(Home_start(1, "", SCREENKIND_LIST) == HOME_START_OPEN);
	CHECK(Home_start(1, "home", SCREENKIND_LIST) == HOME_START_OPEN);
	CHECK(Home_start(1, "home\n", SCREENKIND_LIST) == HOME_START_OPEN);
	CHECK(Home_start(1, "list", SCREENKIND_LIST) == HOME_START_NONE);
	CHECK(Home_start(1, "list\n", SCREENKIND_SWITCHER) == HOME_START_NONE);
	CHECK(Home_start(1, NULL, SCREENKIND_SWITCHER) == HOME_START_UNDER);
	CHECK(Home_start(1, "home", SCREENKIND_QUICKMENU) == HOME_START_UNDER);
	CHECK(Home_start(1, NULL, SCREENKIND_DIALOG) == HOME_START_NONE);
}

static void testSwitcher(void) {
	ScreenSet set;
	ScreenSet_clear(&set);
	CHECK(set.switcher == -1 && !ScreenSet_switcherReady(&set) && !ScreenSet_switcherOpen(&set));
	CHECK(ScreenSet_afterClose(&set, -1) == AFTER_BASE);
	MosaicScreenHooks plain = hooks();
	MosaicScreenHooks both = hooks();
	both.flags = MOSAIC_SCREEN_HOME | MOSAIC_SCREEN_GAME_SWITCHER;
	MosaicScreenHooks switcher = hooks();
	switcher.flags = MOSAIC_SCREEN_GAME_SWITCHER;
	ScreenSet_add(&set, "a.switcher", 0, &switcher); // owner 0 doesn't own the slot below
	ScreenSet_add(&set, "b.plain", 1, &plain);
	ScreenSet_add(&set, "b.both", 1, &both);
	ScreenSet_add(&set, "b.switcher", 1, &switcher);
	ScreenSet_add(&set, "b.switcher2", 1, &switcher);

	// only the owner's, its first with the flag
	CHECK(ScreenSet_claimSwitcher(&set, 2) == -1);
	CHECK(!ScreenSet_switcherReady(&set));
	CHECK(ScreenSet_claimSwitcher(&set, 1) == 2);
	// but never the home screen too
	CHECK(ScreenSet_claimHome(&set, 1) == 2);
	CHECK(ScreenSet_claimSwitcher(&set, 1) == 3);
	CHECK(ScreenSet_switcherReady(&set) && !ScreenSet_switcherOpen(&set));
	CHECK(ScreenSet_kind(&set, 3) == SCREENKIND_SWITCHER);
	CHECK(ScreenSet_kind(&set, 1) == SCREENKIND_PLUGIN);
	CHECK(ScreenSet_kind(&set, 2) == SCREENKIND_PLUGIN);
	CHECK(ScreenSet_kind(&set, -1) == SCREENKIND_PLUGIN);
	// so nothing slides to it or from it, as with the built-in one
	CHECK(!ScreenKind_slides(SCREENKIND_LIST, ScreenSet_kind(&set, 3)));
	CHECK(!ScreenKind_slides(ScreenSet_kind(&set, 3), SCREENKIND_PLUGIN));

	// opened over the home screen: that closes, and the switcher goes back to it
	ScreenSet_push(&set, 2, 0);
	CHECK(ScreenSet_coveredBy(&set, SCREENKIND_SWITCHER));
	CHECK(set.under);
	ScreenSet_pop(&set);
	ScreenSet_push(&set, 3, 0);
	CHECK(ScreenSet_switcherOpen(&set));
	// a launch from it comes back to the home screen
	CHECK(!strcmp(ScreenSet_leftFrom(&set), "home"));
	// the quick menu over it keeps that
	CHECK(ScreenSet_coveredBy(&set, SCREENKIND_QUICKMENU));
	CHECK(set.under);
	ScreenSet_pop(&set);
	CHECK(!ScreenSet_switcherOpen(&set));
	CHECK(ScreenSet_afterClose(&set, 3) == AFTER_HOME);
	CHECK(!set.under);

	// opened over the list, it goes back to the list
	ScreenSet_push(&set, 3, 0);
	CHECK(!strcmp(ScreenSet_leftFrom(&set), "list"));
	ScreenSet_pop(&set);
	CHECK(ScreenSet_afterClose(&set, 3) == AFTER_LIST);
	// any other screen goes back to the base
	CHECK(ScreenSet_afterClose(&set, 1) == AFTER_BASE);

	// after an error, the built-in switcher for the rest of the run, still
	// going back to the home screen under it
	set.under = 1;
	set.screens[3].off = 1;
	CHECK(!ScreenSet_switcherReady(&set));
	CHECK(ScreenSet_afterClose(&set, 3) == AFTER_SWITCHER);
	CHECK(set.under);
	CHECK(ScreenSet_backToHome(&set));
}

int main(void) {
	testAdd();
	testDropOwner();
	testStack();
	testSlides();
	testOverlays();
	testHome();
	testSwitcher();
	CHECK_DONE();
}
