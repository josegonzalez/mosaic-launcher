#include "check.h"
#include "fnbinds.h"

static void testValidAction(void) {
	CHECK(FnBinds_validAction("action:music.toggle"));
	CHECK(FnBinds_validAction("pak:Bootlogo.pak"));
	CHECK(FnBinds_validAction("pak:Group/Clock.pak"));
	CHECK(!FnBinds_validAction("action:"));
	CHECK(!FnBinds_validAction("pak:"));
	CHECK(!FnBinds_validAction(""));
	CHECK(!FnBinds_validAction(NULL));
	CHECK(!FnBinds_validAction("music.toggle"));
	CHECK(!FnBinds_validAction("rom:Game.gb"));
	char long_action[FN_ACTION_MAX + 1];
	memset(long_action, 'a', sizeof(long_action) - 1);
	memcpy(long_action, "pak:", 4);
	long_action[sizeof(long_action) - 1] = '\0';
	CHECK(!FnBinds_validAction(long_action));
	long_action[FN_ACTION_MAX - 1] = '\0';
	CHECK(FnBinds_validAction(long_action));
}

// two plugins racing for a button: the first keeps it
static void testFirstWins(void) {
	FnBinds b;
	FnBinds_clear(&b);
	CHECK(FnBinds_bind(&b, 2, "remap", "action:remap.open", 1, "") == FN_BIND_OK);
	CHECK(FnBinds_bind(&b, 2, "rival", "action:rival.go", 1, "") == FN_BIND_TAKEN);
	const char* owner = NULL;
	CHECK_STR(FnBinds_effective(&b, 2, "", &owner), "action:remap.open");
	CHECK(owner && !strcmp(owner, "remap"));
	// the holder can bind it again, to something else
	CHECK(FnBinds_bind(&b, 2, "remap", "pak:Clock.pak", 1, NULL) == FN_BIND_OK);
	CHECK_STR(FnBinds_effective(&b, 2, NULL, &owner), "pak:Clock.pak");
	// and the other buttons are free
	CHECK(FnBinds_bind(&b, 0, "rival", "action:rival.go", 1, "") == FN_BIND_OK);
	CHECK_STR(FnBinds_effective(&b, 0, "", &owner), "action:rival.go");
	CHECK(owner && !strcmp(owner, "rival"));
	CHECK_STR(FnBinds_effective(&b, 1, "", &owner), "");
	CHECK(owner == NULL);
}

// the user's own choice in Settings always wins
static void testUserWins(void) {
	FnBinds b;
	FnBinds_clear(&b);
	CHECK(FnBinds_bind(&b, 0, "remap", "action:remap.open", 1, "pak:Clock.pak") == FN_BIND_USER);
	const char* owner = "x";
	CHECK_STR(FnBinds_effective(&b, 0, "pak:Clock.pak", &owner), "pak:Clock.pak");
	CHECK(owner == NULL);

	// assigned after a plugin bound it: the plugin's binding is dropped
	CHECK(FnBinds_bind(&b, 1, "remap", "action:remap.open", 1, "") == FN_BIND_OK);
	char dropped[FN_OWNER_MAX] = "";
	CHECK(FnBinds_yield(&b, 1, "", dropped, sizeof(dropped)) == 0);
	CHECK_STR(dropped, "");
	CHECK(FnBinds_yield(&b, 1, "action:gamelist.switcher", dropped, sizeof(dropped)) == 1);
	CHECK_STR(dropped, "remap");
	CHECK_STR(FnBinds_effective(&b, 1, "action:gamelist.switcher", &owner), "action:gamelist.switcher");
	CHECK(owner == NULL);
	// nothing left to drop, and once the user unassigns it, it does nothing
	CHECK(FnBinds_yield(&b, 1, "action:gamelist.switcher", NULL, 0) == 0);
	CHECK_STR(FnBinds_effective(&b, 1, "", &owner), "");
	CHECK(FnBinds_release(&b, 1, "remap") == -1);
	// and is free for any plugin again
	CHECK(FnBinds_bind(&b, 1, "rival", "action:rival.go", 1, "") == FN_BIND_OK);
}

static void testRelease(void) {
	FnBinds b;
	FnBinds_clear(&b);
	CHECK(FnBinds_bind(&b, 0, "remap", "action:remap.open", 1, "") == FN_BIND_OK);
	CHECK(FnBinds_bind(&b, 2, "remap", "action:remap.open", 1, "") == FN_BIND_OK);
	CHECK(FnBinds_bind(&b, 1, "rival", "action:rival.go", 1, "") == FN_BIND_OK);
	// only the holder can release it
	CHECK(FnBinds_release(&b, 0, "rival") == -1);
	CHECK(FnBinds_release(&b, 0, "remap") == 0);
	CHECK(FnBinds_release(&b, 0, "remap") == -1);
	CHECK(FnBinds_bind(&b, 0, "rival", "action:rival.go", 1, "") == FN_BIND_OK);
	// unloading releases everything it holds
	CHECK(FnBinds_bind(&b, 2, "rival", "action:rival.go", 1, "") == FN_BIND_TAKEN);
	CHECK(FnBinds_releaseAll(&b, "remap") == 1);
	CHECK(FnBinds_bind(&b, 2, "rival", "action:rival.go", 1, "") == FN_BIND_OK);
	CHECK(FnBinds_releaseAll(&b, "rival") == 3);
	CHECK(FnBinds_releaseAll(&b, "rival") == 0);
	for (int i = 0; i < FN_COUNT; i++) CHECK_STR(FnBinds_effective(&b, i, "", NULL), "");
	CHECK(FnBinds_release(&b, 0, "") == -1);
	CHECK(FnBinds_release(&b, 0, NULL) == -1);
	CHECK(FnBinds_release(&b, 3, "rival") == -1);
}

static void testRefused(void) {
	FnBinds b;
	FnBinds_clear(&b);
	CHECK(FnBinds_bind(&b, 0, "remap", "action:remap.open", 0, "") == FN_BIND_MISSING);
	CHECK(FnBinds_bind(&b, -1, "remap", "action:remap.open", 1, "") == FN_BIND_BAD);
	CHECK(FnBinds_bind(&b, FN_COUNT, "remap", "action:remap.open", 1, "") == FN_BIND_BAD);
	CHECK(FnBinds_bind(&b, 0, NULL, "action:remap.open", 1, "") == FN_BIND_BAD);
	CHECK(FnBinds_bind(&b, 0, "", "action:remap.open", 1, "") == FN_BIND_BAD);
	CHECK(FnBinds_bind(&b, 0, "remap", "remap.open", 1, "") == FN_BIND_BAD);
	CHECK(FnBinds_bind(&b, 0, "remap", NULL, 1, "") == FN_BIND_BAD);
	CHECK(FnBinds_bind(NULL, 0, "remap", "action:remap.open", 1, "") == FN_BIND_BAD);
	// a bad action is refused before the user's or another plugin's binding
	CHECK(FnBinds_bind(&b, 0, "remap", "", 1, "pak:Clock.pak") == FN_BIND_BAD);
	CHECK_STR(FnBinds_effective(&b, 0, "", NULL), "");
	CHECK_STR(FnBinds_effective(&b, -1, "pak:Clock.pak", NULL), "");
	CHECK_STR(FnBinds_effective(NULL, 0, "pak:Clock.pak", NULL), "pak:Clock.pak");
	CHECK_STR(FnBinds_effective(NULL, 0, "", NULL), "");
}

static void testParseNames(void) {
	char names[FN_COUNT][FN_NAME_MAX];
	FnBinds_parseNames("L3,R3", names);
	CHECK_STR(names[0], "L3");
	CHECK_STR(names[1], "R3");
	CHECK_STR(names[2], "");
	FnBinds_parseNames(",,HOME", names);
	CHECK_STR(names[0], "");
	CHECK_STR(names[1], "");
	CHECK_STR(names[2], "HOME");
	FnBinds_parseNames("L4,R4,HOME,EXTRA", names);
	CHECK_STR(names[0], "L4");
	CHECK_STR(names[1], "R4");
	CHECK_STR(names[2], "HOME");
	FnBinds_parseNames("TOOLONGNAME,R3", names);
	CHECK_STR(names[0], "");
	CHECK_STR(names[1], "R3");
	FnBinds_parseNames(NULL, names);
	CHECK_STR(names[0], "");
	CHECK_STR(names[2], "");
}

int main(void) {
	testValidAction();
	testFirstWins();
	testUserWins();
	testRelease();
	testRefused();
	testParseNames();
	CHECK_DONE();
}
