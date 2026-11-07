// Bindings run like stock's if/else-if input chains, and hints show only
// what's available, in the order given. Bindings that share a button run
// their one available action, or offer a menu of them.

#include "check.h"
#include "actions.h"

// The pad, as the tests set it.
static int pressed_button = -1;
static int released_button = -1;
int PAD_justPressed(int btn) { return btn == pressed_button; }
int PAD_justReleased(int btn) { return btn == released_button; }
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

enum { BTN_X = 1, BTN_A = 2, BTN_B = 3, BTN_Y = 4 };
static const Binding bindings[] = {
	{ BTN_X, TRIGGER_RELEASE, "X", &resumeAction },
	{ BTN_A, TRIGGER_PRESS, "A", &openAction },
	{ BTN_B, TRIGGER_PRESS, "B", &backAction },
};

static void press(int btn) { pressed_button = btn; released_button = -1; ran = NULL; }
static void release(int btn) { released_button = btn; pressed_button = -1; ran = NULL; }

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

	test_shared_buttons();

	CHECK_DONE();
}
