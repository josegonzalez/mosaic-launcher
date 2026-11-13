#include <stdio.h>
#include "check.h"
#include "providers.h"

static Providers p;

static int echo(const char* name, const char* args, char* out, size_t size) {
	snprintf(out, size, "name=%s\nargs=%s", name, args);
	return 0;
}
static int other(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "other=1");
	return 0;
}
static int fails(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "half=");
	return 1;
}
static int badReply(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "not key value");
	return 0;
}
static int big(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	CHECK(size == CALL_TEXT_MAX);
	memset(out, 'v', size - 1);
	memcpy(out, "k=", 2);
	return 0;
}

// what each nested call returned, from inside the callbacks
static int nested_same = 99, nested_other = 99, nested_len = -1;
static char nested_reply[64];
static int nest(const char* name, const char* args, char* out, size_t size);

// runs the callback as plugins.c does, minus the caller
static int run(const Provider* provider, const char* args, char* reply, size_t size, void* ctx) {
	(void)ctx;
	return provider->fn(provider->name, args, reply, size);
}

static int nest(const char* name, const char* args, char* out, size_t size) {
	(void)args;
	char got[64];
	nested_same = Providers_call(&p, name, NULL, got, sizeof(got), run, NULL);
	nested_other = Providers_call(&p, "test.echo", "a=1", nested_reply, sizeof(nested_reply), run, NULL);
	nested_len = nested_other;
	snprintf(out, size, "nested=1");
	return 0;
}

// calls itself, through another name, until the depth runs out
static int deepest = 0;
static int deep(const char* name, const char* args, char* out, size_t size) {
	(void)args;
	static int level = 0;
	level++;
	if (level > deepest) deepest = level;
	char next[PROVIDER_NAME_MAX];
	snprintf(next, sizeof(next), "deep.%c", name[5] + 1);
	char got[64];
	int rc = Providers_call(&p, next, NULL, got, sizeof(got), run, NULL);
	snprintf(out, size, "rc=%i", rc);
	level--;
	return 0;
}

static void testNames(void) {
	CHECK(Providers_nameValid("captions.text"));
	CHECK(Providers_nameValid("games.played"));
	CHECK(Providers_nameValid("a.b"));
	CHECK(Providers_nameValid("my-kind_2.a.b:c"));
	CHECK(!Providers_nameValid("captions"));
	CHECK(!Providers_nameValid("captions."));
	CHECK(!Providers_nameValid(".text"));
	CHECK(!Providers_nameValid("cap tions.text"));
	CHECK(!Providers_nameValid("captions.te xt"));
	CHECK(!Providers_nameValid("captions.text\n"));
	CHECK(!Providers_nameValid("cap:tions.text"));
	CHECK(!Providers_nameValid(""));
	CHECK(!Providers_nameValid(NULL));
	char name[PROVIDER_NAME_MAX + 1];
	memset(name, 'a', sizeof(name) - 1);
	name[1] = '.';
	name[sizeof(name) - 1] = '\0';
	CHECK(!Providers_nameValid(name));
	name[PROVIDER_NAME_MAX - 1] = '\0';
	CHECK(Providers_nameValid(name));
	CHECK(Providers_nameReserved("sets.favorites"));
	CHECK(Providers_nameReserved("events.music.track"));
	CHECK(!Providers_nameReserved("setsx.a"));
	CHECK(!Providers_nameReserved("captions.text"));
}

// the first to provide a name keeps it, and only what provides= lists
static void testOwnership(void) {
	Providers_init(&p);
	CHECK(Providers_add(&p, "captions.text", "playtime-captions", "captions.text", echo) == PROVIDE_OK);
	CHECK(Providers_add(&p, "captions.text", "rival", "captions.text", other) == PROVIDE_TAKEN);
	Provider* found = Providers_find(&p, "captions.text");
	CHECK(found && !strcmp(found->owner, "playtime-captions") && found->fn == echo);
	// the owner can provide it again, with a new callback
	CHECK(Providers_add(&p, "captions.text", "playtime-captions", "captions.text", other) == PROVIDE_OK);
	CHECK(Providers_find(&p, "captions.text")->fn == other);
	CHECK(p.count == 1);
	// provides= has to list it, matched exactly, spaces around items skipped
	CHECK(Providers_add(&p, "games.played", "row-badges", "", echo) == PROVIDE_UNDECLARED);
	CHECK(Providers_add(&p, "games.played", "row-badges", NULL, echo) == PROVIDE_UNDECLARED);
	CHECK(Providers_add(&p, "games.played", "row-badges", "games.play,games.playedx", echo) == PROVIDE_UNDECLARED);
	CHECK(Providers_add(&p, "games.played", "row-badges", "Games.played", echo) == PROVIDE_UNDECLARED);
	CHECK(Providers_add(&p, "games.played", "row-badges", "x.y , games.played ", echo) == PROVIDE_OK);
	// other features' capabilities, bad names and missing parts
	CHECK(Providers_add(&p, "sets.favorites", "favorites", "sets.favorites", echo) == PROVIDE_RESERVED);
	CHECK(Providers_add(&p, "events.music.track", "music", "events.music.track", echo) == PROVIDE_RESERVED);
	CHECK(Providers_add(&p, "nodot", "a", "nodot", echo) == PROVIDE_BAD);
	CHECK(Providers_add(&p, "a.b", "a", "a.b", NULL) == PROVIDE_BAD);
	CHECK(Providers_add(&p, "a.b", "", "a.b", echo) == PROVIDE_BAD);
	CHECK(Providers_add(&p, "a.b", NULL, "a.b", echo) == PROVIDE_BAD);
	CHECK(Providers_add(&p, NULL, "a", "a.b", echo) == PROVIDE_BAD);
	CHECK(p.count == 2);
	CHECK(Providers_find(&p, "Captions.text") == NULL);
	CHECK(Providers_find(&p, NULL) == NULL);
}

// unloading releases a plugin's names, for the next to take
static void testRelease(void) {
	Providers_init(&p);
	CHECK(Providers_add(&p, "a.one", "a", "a.one,a.two", echo) == PROVIDE_OK);
	CHECK(Providers_add(&p, "b.one", "b", "b.one", echo) == PROVIDE_OK);
	CHECK(Providers_add(&p, "a.two", "a", "a.one,a.two", echo) == PROVIDE_OK);
	CHECK(Providers_releaseAll(&p, "a") == 2);
	CHECK(p.count == 1 && !strcmp(p.items[0].name, "b.one"));
	CHECK(Providers_releaseAll(&p, "a") == 0);
	CHECK(Providers_releaseAll(&p, NULL) == 0);
	char out[64];
	CHECK(Providers_call(&p, "a.one", NULL, out, sizeof(out), run, NULL) == CALL_UNKNOWN);
	CHECK(Providers_add(&p, "a.one", "c", "a.one", other) == PROVIDE_OK);
	CHECK(Providers_call(&p, "a.one", NULL, out, sizeof(out), run, NULL) == 7);
	CHECK_STR(out, "other=1");
}

static void testFull(void) {
	Providers_init(&p);
	char name[PROVIDER_NAME_MAX];
	int ok = 1;
	for (int i = 0; i < PROVIDERS_MAX; i++) {
		snprintf(name, sizeof(name), "n.%i", i);
		ok &= Providers_add(&p, name, "a", name, echo) == PROVIDE_OK;
	}
	CHECK(ok);
	CHECK(Providers_add(&p, "n.more", "a", "n.more", echo) == PROVIDE_FULL);
	// replacing one still works when full
	CHECK(Providers_add(&p, "n.0", "a", "n.0", other) == PROVIDE_OK);
}

static void testCalls(void) {
	Providers_init(&p);
	CHECK(Providers_add(&p, "test.echo", "a", "test.echo", echo) == PROVIDE_OK);
	CHECK(Providers_add(&p, "test.fails", "a", "test.fails", fails) == PROVIDE_OK);
	CHECK(Providers_add(&p, "test.bad", "a", "test.bad", badReply) == PROVIDE_OK);
	CHECK(Providers_add(&p, "test.big", "a", "test.big", big) == PROVIDE_OK);
	char out[64];
	CHECK(Providers_call(&p, "test.echo", "path=/a\nx=1", out, sizeof(out), run, NULL) == 31);
	CHECK_STR(out, "name=test.echo\nargs=path=/a\nx=1");
	// no arguments is ""
	CHECK(Providers_call(&p, "test.echo", NULL, out, sizeof(out), run, NULL) == 20);
	CHECK_STR(out, "name=test.echo\nargs=");
	// unknown
	strcpy(out, "x");
	CHECK(Providers_call(&p, "test.none", NULL, out, sizeof(out), run, NULL) == CALL_UNKNOWN);
	CHECK_STR(out, "");
	// too small: the reply and its terminator need 21 bytes
	strcpy(out, "x");
	CHECK(Providers_call(&p, "test.echo", NULL, out, 20, run, NULL) == CALL_TOO_SMALL);
	CHECK_STR(out, "");
	CHECK(Providers_call(&p, "test.echo", NULL, out, 21, run, NULL) == 20);
	// a reply as big as calls allow, and a buffer for it
	static char huge[CALL_TEXT_MAX];
	CHECK(Providers_call(&p, "test.big", NULL, huge, sizeof(huge), run, NULL) == CALL_TEXT_MAX - 1);
	CHECK(Providers_call(&p, "test.big", NULL, huge, sizeof(huge) - 1, run, NULL) == CALL_TOO_SMALL);
	// refused: bad arguments, no buffer
	CHECK(Providers_call(&p, "test.echo", "no equals", out, sizeof(out), run, NULL) == CALL_REFUSED);
	CHECK(Providers_call(&p, "test.echo", "a=b\tc", out, sizeof(out), run, NULL) == CALL_REFUSED);
	static char long_args[CALL_TEXT_MAX + 1];
	memset(long_args, 'v', CALL_TEXT_MAX);
	memcpy(long_args, "k=", 2);
	CHECK(Providers_call(&p, "test.echo", long_args, huge, sizeof(huge), run, NULL) == CALL_REFUSED);
	long_args[CALL_TEXT_MAX - 1] = '\0';
	CHECK(Providers_call(&p, "test.echo", long_args, out, sizeof(out), run, NULL) == CALL_TOO_SMALL);
	CHECK(Providers_call(&p, "test.echo", NULL, NULL, 10, run, NULL) == CALL_REFUSED);
	CHECK(Providers_call(&p, "test.echo", NULL, out, 0, run, NULL) == CALL_REFUSED);
	// failed: the provider said so, or its reply isn't key=value text
	strcpy(out, "x");
	CHECK(Providers_call(&p, "test.fails", NULL, out, sizeof(out), run, NULL) == CALL_FAILED);
	CHECK_STR(out, "");
	CHECK(Providers_call(&p, "test.bad", NULL, out, sizeof(out), run, NULL) == CALL_FAILED);
	CHECK(!Providers_find(&p, "test.fails")->busy);
}

// a callback can call other names, but not its own until it returns
static void testNesting(void) {
	Providers_init(&p);
	CHECK(Providers_add(&p, "test.echo", "a", "test.echo", echo) == PROVIDE_OK);
	CHECK(Providers_add(&p, "test.nest", "b", "test.nest", nest) == PROVIDE_OK);
	char out[64];
	CHECK(Providers_call(&p, "test.nest", NULL, out, sizeof(out), run, NULL) == 8);
	CHECK_STR(out, "nested=1");
	CHECK(nested_same == CALL_REFUSED);
	CHECK(nested_len == 23);
	CHECK_STR(nested_reply, "name=test.echo\nargs=a=1");
	CHECK(!Providers_find(&p, "test.nest")->busy);
	CHECK(p.depth == 0);
	// and once it has, again
	nested_same = 99;
	CHECK(Providers_call(&p, "test.nest", NULL, out, sizeof(out), run, NULL) == 8);
	CHECK(nested_same == CALL_REFUSED);

	// a chain of different names stops at CALL_DEPTH_MAX
	Providers_init(&p);
	char name[PROVIDER_NAME_MAX];
	for (char c = 'a'; c <= 'a' + CALL_DEPTH_MAX + 2; c++) {
		snprintf(name, sizeof(name), "deep.%c", c);
		Providers_add(&p, name, "a", name, deep);
	}
	CHECK(Providers_call(&p, "deep.a", NULL, out, sizeof(out), run, NULL) > 0);
	CHECK(deepest == CALL_DEPTH_MAX);
	CHECK(p.depth == 0);
}

int main(void) {
	testNames();
	testOwnership();
	testRelease();
	testFull();
	testCalls();
	testNesting();
	CHECK_DONE();
}
