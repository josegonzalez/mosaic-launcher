// Bindings run like stock's if/else-if input chains, and hints show only
// what's available, in the order given. Bindings that share a button run
// their one available action, or offer a menu of them.

#include "check.h"
#include "actions.h"

// The buttons, and stock's repeat pace.
#include "defines.h"
#include "api.h"
enum { REPEAT_DELAY = PAD_REPEAT_DELAY, REPEAT_INTERVAL = PAD_REPEAT_INTERVAL };

// The pad, as the tests set it.
static int pressed_button = -1;
static int released_button = -1;
static int repeated_button = -1;
int PAD_justPressed(int btn) { return btn == pressed_button; }
int PAD_justReleased(int btn) { return btn == released_button; }
int PAD_justRepeated(int btn) { return btn == repeated_button; }
void LOG_note(int level, const char* fmt, ...) { (void)level; (void)fmt; }

static int open_ok = 1, back_ok = 1, resume_ok = 1;
static const char* ran = NULL;

static int canOpen(void* d) { (void)d; return open_ok; }
static int canBack(void* d) { (void)d; return back_ok; }
static int canResume(void* d) { (void)d; return resume_ok; }
static void runOpen(void* d) { (void)d; ran = "open"; }
static void runBack(void* d) { (void)d; ran = "back"; }
static void runResume(void* d) { (void)d; ran = "resume"; }

static const Action resumeAction = { "test.resume", "RESUME", canResume, runResume };
static const Action openAction = { "test.open", "OPEN", canOpen, runOpen };
static const Action backAction = { "test.back", "BACK", canBack, runBack };

static const Binding bindings[] = {
	{ BTN_X, TRIGGER_RELEASE, "X", &resumeAction },
	{ BTN_A, TRIGGER_PRESS, "A", &openAction },
	{ BTN_B, TRIGGER_PRESS, "B", &backAction },
};

static void press(int btn) { pressed_button = btn; released_button = -1; repeated_button = -1; ran = NULL; }
static void release(int btn) { released_button = btn; pressed_button = -1; repeated_button = -1; ran = NULL; }

// Plugin actions that can share Y.
static int fav_ok = 1, random_ok = 1;
static int canFav(void* d) { (void)d; return fav_ok; }
static int canRandom(void* d) { (void)d; return random_ok; }
static void runFav(void* d) { (void)d; ran = "favorite"; }
static void runRandom(void* d) { (void)d; ran = "random"; }
static void runDelete(void* d) { (void)d; ran = "delete"; }
static const Action favAction = { "test.fav", "FAVORITE", canFav, runFav };
static const Action randomAction = { "test.random", "RANDOM", canRandom, runRandom };
static const Action deleteAction = { "test.delete", "DELETE", NULL, runDelete };

static const Action* menu_actions[8];
static int menu_count = 0;
static void menu(const Action** actions, int n) {
	menu_count = n;
	for (int i = 0; i < n && i < 8; i++) menu_actions[i] = actions[i];
}

static void test_shared_buttons(void) {
	const Binding shared[] = {
		{ BTN_Y, TRIGGER_PRESS, "Y", &favAction },
		{ BTN_Y, TRIGGER_PRESS, "Y", &randomAction },
		{ BTN_Y, TRIGGER_PRESS, "Y", &favAction }, // bound twice, listed once
		{ BTN_Y, TRIGGER_RELEASE, "Y", &deleteAction }, // another trigger isn't shared
		{ BTN_X, TRIGGER_PRESS, "X", &deleteAction },
	};
	const Action* got[4];

	// which bindings share a button
	CHECK(Bindings_shared(shared, 5, BTN_Y, TRIGGER_PRESS, got, 4) == 2);
	CHECK(got[0] == &favAction && got[1] == &randomAction);
	CHECK(Bindings_shared(shared, 5, BTN_Y, TRIGGER_RELEASE, got, 4) == 1);
	CHECK(Bindings_shared(shared, 5, BTN_A, TRIGGER_PRESS, got, 4) == 0);
	random_ok = 0;
	CHECK(Bindings_shared(shared, 5, BTN_Y, TRIGGER_PRESS, got, 4) == 1);
	random_ok = 1;

	// two available actions on Y open the menu with both, in order
	press(BTN_Y);
	menu_count = 0;
	CHECK(Bindings_dispatchShared(shared, 5, menu) == 1);
	CHECK(ran == NULL);
	CHECK(menu_count == 2);
	CHECK(menu_actions[0] == &favAction && menu_actions[1] == &randomAction);

	// one available action runs straight away
	random_ok = 0;
	press(BTN_Y);
	menu_count = 0;
	CHECK(Bindings_dispatchShared(shared, 5, menu) == 1);
	CHECK_STR(ran ? ran : "", "favorite");
	CHECK(menu_count == 0);

	// none available: nothing runs
	fav_ok = 0;
	press(BTN_Y);
	CHECK(Bindings_dispatchShared(shared, 5, menu) == 0);
	CHECK(ran == NULL && menu_count == 0);
	fav_ok = 1;
	random_ok = 1;

	// a lone button runs as before
	press(BTN_X);
	CHECK(Bindings_dispatchShared(shared, 5, menu) == 1);
	CHECK_STR(ran ? ran : "", "delete");

	// hints: MORE for a shared button, the label for a lone one
	const char* glyph = NULL;
	const char* label = NULL;
	CHECK(Bindings_sharedHint(shared, 5, &glyph, &label) == 1);
	CHECK_STR(glyph, "Y");
	CHECK_STR(label, "MORE");
	random_ok = 0;
	CHECK(Bindings_sharedHint(shared, 5, &glyph, &label) == 1);
	CHECK_STR(label, "FAVORITE");
	fav_ok = 0;
	CHECK(Bindings_sharedHint(shared, 2, &glyph, &label) == 0);
	CHECK(Bindings_sharedHint(shared, 5, &glyph, &label) == 1);
	CHECK_STR(label, "DELETE");
	fav_ok = 1;
	random_ok = 1;
	CHECK(Bindings_sharedHint(shared, 0, &glyph, &label) == 0);
}

// A binding's own fires check replaces its trigger.
static int tap_button = -1;
static int tapped(int btn) { return btn == tap_button; }

static void test_fires(void) {
	back_ok = 1;
	const Binding tap[] = { { BTN_B, TRIGGER_PRESS, "B", &backAction, tapped } };
	press(BTN_B);
	tap_button = -1;
	CHECK(Bindings_dispatch(tap, 1) == 0);
	tap_button = BTN_B;
	CHECK(Bindings_dispatch(tap, 1) == 1);
	CHECK_STR(ran ? ran : "", "back");
	tap_button = -1;
}

// What the game list does with plugins' overrides: drop its own bindings on
// the overridden buttons, put the overrides after the rest, and find each
// action's hint on its new button.
static void test_overrides(void) {
	open_ok = back_ok = resume_ok = 1;
	// A and X swapped, resume on press
	const Binding swap[] = {
		{ BTN_A, TRIGGER_PRESS, "A", &resumeAction },
		{ BTN_X, TRIGGER_PRESS, "X", &openAction },
	};
	Binding effective[3 + 2];
	int n = Bindings_drop(bindings, 3, swap, 2, effective);
	CHECK(n == 1);
	CHECK(effective[0].action == &backAction);
	effective[n++] = swap[0];
	effective[n++] = swap[1];

	press(BTN_A);
	CHECK(Bindings_dispatch(effective, n) == 1);
	CHECK_STR(ran ? ran : "", "resume");
	press(BTN_X);
	CHECK(Bindings_dispatch(effective, n) == 1);
	CHECK_STR(ran ? ran : "", "open");
	// X's release no longer resumes: the override took the whole button
	release(BTN_X);
	CHECK(Bindings_dispatch(effective, n) == 0);
	press(BTN_B);
	CHECK(Bindings_dispatch(effective, n) == 1);
	CHECK_STR(ran ? ran : "", "back");

	// the hints follow the actions to their buttons
	const Binding* resume = Bindings_find(effective, n, &resumeAction);
	const Binding* open = Bindings_find(effective, n, &openAction);
	const Binding* back = Bindings_find(effective, n, &backAction);
	CHECK(resume && !strcmp(resume->glyph, "A"));
	CHECK(open && !strcmp(open->glyph, "X"));
	CHECK(back && !strcmp(back->glyph, "B"));
	const Binding* order[] = { back, open };
	char* pairs[5];
	CHECK(Bindings_hints(order, 2, pairs) == 2);
	CHECK_STR(pairs[0], "B"); CHECK_STR(pairs[2], "X"); CHECK_STR(pairs[3], "OPEN");
	CHECK(Bindings_find(effective, n, &favAction) == NULL);
	CHECK(Bindings_find(effective, n, NULL) == NULL);

	// several overrides on one trigger: the first available runs, so A can
	// resume when it can and open otherwise
	const Binding resumeOrOpen[] = {
		{ BTN_A, TRIGGER_PRESS, "A", &resumeAction },
		{ BTN_A, TRIGGER_PRESS, "A", &openAction },
	};
	n = Bindings_drop(bindings, 3, resumeOrOpen, 2, effective);
	CHECK(n == 2); // X resume and B back
	effective[n++] = resumeOrOpen[0];
	effective[n++] = resumeOrOpen[1];
	resume_ok = 0;
	press(BTN_A);
	CHECK(Bindings_dispatch(effective, n) == 1);
	CHECK_STR(ran ? ran : "", "open");
	resume_ok = 1;
	press(BTN_A);
	CHECK(Bindings_dispatch(effective, n) == 1);
	CHECK_STR(ran ? ran : "", "resume");

	// an override with no action leaves its button doing nothing
	const Binding nothing[] = { { BTN_B, TRIGGER_PRESS, "B", NULL } };
	n = Bindings_drop(bindings, 3, nothing, 1, effective);
	effective[n++] = nothing[0];
	press(BTN_B);
	CHECK(Bindings_dispatch(effective, n) == 0);
	CHECK(Bindings_find(effective, n, &backAction) == NULL);

	// plugins' shared bindings on an overridden button are dropped too,
	// and the rest still share
	const Binding shared[] = {
		{ BTN_Y, TRIGGER_PRESS, "Y", &favAction },
		{ BTN_Y, TRIGGER_PRESS, "Y", &randomAction },
		{ BTN_A, TRIGGER_RELEASE, "A", &deleteAction },
	};
	Binding kept[3];
	n = Bindings_drop(shared, 3, swap, 2, kept);
	CHECK(n == 2);
	press(BTN_Y);
	menu_count = 0;
	CHECK(Bindings_dispatchShared(kept, n, menu) == 1);
	CHECK(menu_count == 2);
	release(BTN_A);
	CHECK(Bindings_dispatchShared(kept, n, menu) == 0);

	// no overrides: everything is kept, in order
	n = Bindings_drop(bindings, 3, NULL, 0, effective);
	CHECK(n == 3);
	CHECK(effective[0].action == &resumeAction && effective[1].action == &openAction && effective[2].action == &backAction);
}

// A button held down, repeating the way stock's PAD_poll repeats it: on the
// press, then REPEAT_DELAY later, then every REPEAT_INTERVAL until it's let
// go. pollAt is one frame at tick.
static int held_button = -1;
static unsigned long repeat_at = 0;
static void hold(int btn, unsigned long tick) {
	press(btn);
	repeated_button = btn;
	held_button = btn;
	repeat_at = tick + REPEAT_DELAY;
}
static void pollAt(unsigned long tick) {
	pressed_button = released_button = repeated_button = -1;
	ran = NULL;
	if (held_button >= 0 && tick >= repeat_at) {
		repeated_button = held_button;
		repeat_at += REPEAT_INTERVAL;
	}
}
static void letGo(void) {
	release(held_button);
	held_button = -1;
}

// The letter jumps and a plugin's jump that goes first, as platform jump
// puts them on L1 and R1.
static int other_shoulder_down = 0;
static int jump_ok = 0;
static int prev_runs = 0, jump_runs = 0;
static int shoulderAlone(int btn) { (void)btn; return !other_shoulder_down; }
static int canJump(void* d) { (void)d; return jump_ok; }
static void runPrev(void* d) { (void)d; ran = "prev"; prev_runs++; }
static void runNext(void* d) { (void)d; ran = "next"; }
static void runJump(void* d) { (void)d; ran = "jump"; jump_runs++; }
static const Action prevAction = { "test.prev_letter", "PREVIOUS", NULL, runPrev };
static const Action nextAction = { "test.next_letter", "NEXT", NULL, runNext };
static const Action jumpAction = { "test.jump", "SYSTEM", canJump, runJump };
static const Binding letters[] = {
	{ BTN_L1, TRIGGER_REPEAT, "L1", &prevAction, NULL, shoulderAlone },
	{ BTN_R1, TRIGGER_REPEAT, "R1", &nextAction, NULL, shoulderAlone },
};

// A repeat binding fires on the press and again at stock's pace while held,
// and its allows check holds it back while the other shoulder is down.
static void test_repeat(void) {
	other_shoulder_down = 0;
	prev_runs = 0;
	unsigned long fired_at[16];
	int fires = 0;
	hold(BTN_L1, 0);
	for (unsigned long tick = 0; tick <= 650; tick += 10) {
		if (tick) pollAt(tick);
		if (Bindings_dispatch(letters, 2) && fires < 16) fired_at[fires++] = tick;
	}
	CHECK(fires == 5);
	CHECK(fired_at[0] == 0);
	CHECK(fired_at[1] == REPEAT_DELAY);
	CHECK(fired_at[2] == REPEAT_DELAY + REPEAT_INTERVAL);
	CHECK(fired_at[3] == REPEAT_DELAY + 2 * REPEAT_INTERVAL);
	CHECK(fired_at[4] == REPEAT_DELAY + 3 * REPEAT_INTERVAL);
	CHECK(prev_runs == 5);
	letGo();
	CHECK(Bindings_dispatch(letters, 2) == 0);
	pollAt(2000);
	CHECK(Bindings_dispatch(letters, 2) == 0);

	// press and release bindings don't fire on repeats
	const Binding once[] = {
		{ BTN_L1, TRIGGER_PRESS, "L1", &prevAction },
		{ BTN_L1, TRIGGER_RELEASE, "L1", &nextAction },
	};
	hold(BTN_L1, 0);
	CHECK(Bindings_dispatch(once, 2) == 1);
	CHECK_STR(ran ? ran : "", "prev");
	pollAt(REPEAT_DELAY);
	CHECK(repeated_button == BTN_L1);
	CHECK(Bindings_dispatch(once, 2) == 0);
	letGo();
	CHECK(Bindings_dispatch(once, 2) == 1);
	CHECK_STR(ran ? ran : "", "next");

	// the other shoulder down: nothing, on the press or the repeats, and it
	// picks up again at the next repeat once that's let go
	other_shoulder_down = 1;
	hold(BTN_L1, 0);
	CHECK(Bindings_dispatch(letters, 2) == 0);
	pollAt(REPEAT_DELAY);
	CHECK(Bindings_dispatch(letters, 2) == 0);
	other_shoulder_down = 0;
	pollAt(REPEAT_DELAY + 50);
	CHECK(Bindings_dispatch(letters, 2) == 0);
	pollAt(REPEAT_DELAY + REPEAT_INTERVAL);
	CHECK(Bindings_dispatch(letters, 2) == 1);
	CHECK_STR(ran ? ran : "", "prev");
	letGo();
}

// What the game list does with platform jump's overrides: a letter jump put
// back on its own shoulder keeps the stock check, and the plugin's jump
// ahead of it, and both repeat, so a held shoulder keeps doing one of them.
static void test_override_repeat(void) {
	Binding overrides[] = {
		{ BTN_L1, TRIGGER_REPEAT, "L1", &jumpAction },
		{ BTN_L1, TRIGGER_REPEAT, "L1", &prevAction },
		{ BTN_R1, TRIGGER_REPEAT, "R1", &jumpAction },
		{ BTN_R1, TRIGGER_REPEAT, "R1", &prevAction }, // a letter jump on the other shoulder
	};
	Bindings_keepChecks(overrides, 4, letters, 2);
	CHECK(overrides[0].allows == NULL);
	CHECK(overrides[1].allows == shoulderAlone);
	CHECK(overrides[2].allows == NULL);
	CHECK(overrides[3].allows == NULL);
	Bindings_keepChecks(overrides, 4, NULL, 0); // nothing to keep changes nothing
	CHECK(overrides[1].allows == shoulderAlone);

	Binding effective[2];
	CHECK(Bindings_drop(letters, 2, overrides, 4, effective) == 0);

	// in a list with systems the plugin's jump runs, on the press and repeats
	jump_ok = 1;
	jump_runs = prev_runs = 0;
	other_shoulder_down = 0;
	hold(BTN_L1, 0);
	CHECK(Bindings_dispatch(overrides, 2) == 1);
	pollAt(REPEAT_DELAY);
	CHECK(Bindings_dispatch(overrides, 2) == 1);
	pollAt(REPEAT_DELAY + REPEAT_INTERVAL);
	CHECK(Bindings_dispatch(overrides, 2) == 1);
	CHECK(jump_runs == 3 && prev_runs == 0);
	letGo();

	// elsewhere the letters repeat as stock's do
	jump_ok = 0;
	hold(BTN_L1, 0);
	CHECK(Bindings_dispatch(overrides, 2) == 1);
	pollAt(REPEAT_DELAY);
	CHECK(Bindings_dispatch(overrides, 2) == 1);
	CHECK(prev_runs == 2 && jump_runs == 3);

	// and keep stock's other shoulder check
	other_shoulder_down = 1;
	pollAt(REPEAT_DELAY + REPEAT_INTERVAL);
	CHECK(Bindings_dispatch(overrides, 2) == 0);
	CHECK(prev_runs == 2);
	other_shoulder_down = 0;
	letGo();
}

int main(void) {
	// dispatch: the fired, available binding runs
	press(BTN_A);
	CHECK(Bindings_dispatch(bindings, 3) == 1);
	CHECK_STR(ran ? ran : "", "open");

	// unavailable actions don't run, and nothing else runs in their place
	open_ok = 0;
	press(BTN_A);
	CHECK(Bindings_dispatch(bindings, 3) == 0);
	CHECK(ran == NULL);
	open_ok = 1;

	// resume fires on release, not press
	press(BTN_X);
	CHECK(Bindings_dispatch(bindings, 3) == 0);
	release(BTN_X);
	CHECK(Bindings_dispatch(bindings, 3) == 1);
	CHECK_STR(ran ? ran : "", "resume");

	// hints: stock's four game list cases, back then open, last one primary
	const Binding* const order[] = { &bindings[2], &bindings[1] };
	char* pairs[5];

	open_ok = 1; back_ok = 1; // a nested, non-empty folder
	CHECK(Bindings_hints(order, 2, pairs) == 2);
	CHECK_STR(pairs[0], "B"); CHECK_STR(pairs[1], "BACK");
	CHECK_STR(pairs[2], "A"); CHECK_STR(pairs[3], "OPEN");
	CHECK(pairs[4] == NULL);

	open_ok = 1; back_ok = 0; // the root
	CHECK(Bindings_hints(order, 2, pairs) == 1);
	CHECK_STR(pairs[0], "A");
	CHECK(pairs[2] == NULL);

	open_ok = 0; back_ok = 1; // a nested, empty folder
	CHECK(Bindings_hints(order, 2, pairs) == 1);
	CHECK_STR(pairs[0], "B");

	open_ok = 0; back_ok = 0; // an empty root
	CHECK(Bindings_hints(order, 2, pairs) == 0);
	CHECK(pairs[0] == NULL);

	// registry: find by id, re-registering replaces
	Actions_register(&openAction);
	Actions_register(&backAction);
	CHECK(Actions_find("test.open") == &openAction);
	CHECK(Actions_find("test.missing") == NULL);
	static const Action replacement = { "test.open", "GO", NULL, runOpen };
	Actions_register(&replacement);
	CHECK(Actions_find("test.open") == &replacement);

	// save and restore: what a declined plugin registered goes, and an
	// action it replaced comes back
	const Action* saved[ACTIONS_MAX];
	int saved_count = Actions_list(saved, ACTIONS_MAX);
	CHECK(saved_count == 2);
	static const Action added = { "test.added", "ADD", NULL, runOpen };
	static const Action taken = { "test.back", "TAKEN", NULL, runBack };
	Actions_register(&added);
	Actions_register(&taken);
	CHECK(Actions_find("test.added") == &added);
	CHECK(Actions_find("test.back") == &taken);
	Actions_restore(saved, saved_count);
	CHECK(Actions_find("test.added") == NULL);
	CHECK(Actions_find("test.back") == &backAction);
	CHECK(Actions_find("test.open") == &replacement);
	const Action* now[ACTIONS_MAX];
	CHECK(Actions_list(now, ACTIONS_MAX) == 2);
	CHECK(Actions_list(now, 1) == 2); // the count, even when out is short

	test_shared_buttons();
	test_fires();
	test_overrides();
	test_repeat();
	test_override_repeat();

	CHECK_DONE();
}
