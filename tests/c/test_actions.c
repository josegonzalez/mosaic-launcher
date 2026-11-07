// Bindings run like stock's if/else-if input chains, and hints show only
// what's available, in the order given.

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

enum { BTN_X = 1, BTN_A = 2, BTN_B = 3 };
static const Binding bindings[] = {
	{ BTN_X, TRIGGER_RELEASE, "X", &resumeAction },
	{ BTN_A, TRIGGER_PRESS, "A", &openAction },
	{ BTN_B, TRIGGER_PRESS, "B", &backAction },
};

static void press(int btn) { pressed_button = btn; released_button = -1; ran = NULL; }
static void release(int btn) { released_button = btn; pressed_button = -1; ran = NULL; }

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

	CHECK_DONE();
}
