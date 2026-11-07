// The button remap plugin: its model on its own, then the plugin against a
// fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/button-remap/button_remap.c"

///////////////////////////////////////
// the fake host

#define CFG_MAX 16
static char cfg_keys[CFG_MAX][32];
static char cfg_values[CFG_MAX][128];
static int cfg_count = 0;

static void setCfg(const char* key, const char* value) {
	snprintf(cfg_keys[cfg_count], sizeof(cfg_keys[0]), "%s", key);
	snprintf(cfg_values[cfg_count], sizeof(cfg_values[0]), "%s", value);
	cfg_count++;
}

typedef struct Call {
	int button;
	int trigger;
	char action[64]; // "" for NULL
} Call;

#define CALLS_MAX 32
static Call overrides[CALLS_MAX];
static int override_count = 0;
static Call binds[CALLS_MAX];
static int bind_count = 0;
static int logs = 0;
static int refuse_button = -1; // override_button refuses this one, as if another plugin had it
static const char* unknown_action = NULL; // bind_button and override_button refuse this one

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; logs++; }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	if (strcmp(id, "button-remap") != 0) return fallback;
	for (int i = cfg_count - 1; i >= 0; i--) {
		if (!strcmp(cfg_keys[i], key)) return cfg_values[i];
	}
	return fallback;
}
static void fakeCfgSet(const char* id, const char* key, const char* value) {
	(void)id; (void)key; (void)value;
	CHECK(0); // the plugin never writes its settings
}
static void record(Call* calls, int* count, int button, int trigger, const char* id) {
	CHECK(*count < CALLS_MAX);
	Call* c = &calls[(*count)++];
	c->button = button;
	c->trigger = trigger;
	snprintf(c->action, sizeof(c->action), "%s", id ? id : "");
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	CHECK(screen == MOSAIC_SCREEN_GAMELIST);
	if (!id || (unknown_action && !strcmp(id, unknown_action))) return -1;
	record(binds, &bind_count, button, trigger, id);
	return 0;
}
static int fakeOverride(int screen, int button, int trigger, const char* id) {
	CHECK(screen == MOSAIC_SCREEN_GAMELIST);
	if (button == refuse_button || (id && unknown_action && !strcmp(id, unknown_action))) return -1;
	record(overrides, &override_count, button, trigger, id);
	return 0;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = "/sd",
	.userdata_path = "/sd/.userdata/test",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
	.bind_button = fakeBind,
	.override_button = fakeOverride,
};

static void reset(void) {
	cfg_count = 0;
	override_count = 0;
	bind_count = 0;
	logs = 0;
	refuse_button = -1;
	unknown_action = NULL;
}

// Starts the plugin as the loader would, then runs its first two ticks.
static void start(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->init(&fake) == 0);
	CHECK(bind_count == 0); // binds wait for the first tick
	p->tick(1);
	int after_first = bind_count;
	p->tick(2);
	CHECK(bind_count == after_first); // and happen once
}

static int checkCall(const Call* c, int button, int trigger, const char* action) {
	return c->button == button && c->trigger == trigger && !strcmp(c->action, action);
}

///////////////////////////////////////
// the model

static void testKeys(void) {
	CHECK_STR(Remap_key(MOSAIC_BUTTON_A), "a");
	CHECK_STR(Remap_key(MOSAIC_BUTTON_L1), "l1");
	CHECK_STR(Remap_key(MOSAIC_BUTTON_SELECT), "select");
	CHECK(Remap_key(-1) == NULL);
	CHECK(Remap_key(REMAP_BUTTONS) == NULL);

	CHECK(Remap_stockOwned(MOSAIC_BUTTON_A));
	CHECK(Remap_stockOwned(MOSAIC_BUTTON_B));
	CHECK(Remap_stockOwned(MOSAIC_BUTTON_X));
	CHECK(Remap_stockOwned(MOSAIC_BUTTON_L1));
	CHECK(Remap_stockOwned(MOSAIC_BUTTON_R1));
	CHECK(Remap_stockOwned(MOSAIC_BUTTON_SELECT));
	CHECK(!Remap_stockOwned(MOSAIC_BUTTON_Y));
	CHECK(!Remap_stockOwned(MOSAIC_BUTTON_L2));
	CHECK(!Remap_stockOwned(MOSAIC_BUTTON_R2));
	CHECK(!Remap_stockOwned(MOSAIC_BUTTON_START));
}

static void testParse(void) {
	RemapButton b;
	CHECK(Remap_parse(NULL, &b) == REMAP_UNSET && !b.set);
	CHECK(Remap_parse("", &b) == REMAP_UNSET && !b.set);
	CHECK(Remap_parse("   ", &b) == REMAP_UNSET && !b.set);

	CHECK(Remap_parse("gamelist.resume", &b) == REMAP_SET);
	CHECK(b.set && b.trigger == MOSAIC_TRIGGER_PRESS && b.count == 1);
	CHECK_STR(b.actions[0], "gamelist.resume");

	CHECK(Remap_parse(" release: gamelist.resume , random-game.pick ", &b) == REMAP_SET);
	CHECK(b.trigger == MOSAIC_TRIGGER_RELEASE && b.count == 2);
	CHECK_STR(b.actions[0], "gamelist.resume");
	CHECK_STR(b.actions[1], "random-game.pick");

	CHECK(Remap_parse("Press:favorites.toggle", &b) == REMAP_SET);
	CHECK(b.trigger == MOSAIC_TRIGGER_PRESS && b.count == 1);
	CHECK_STR(b.actions[0], "favorites.toggle");

	CHECK(Remap_parse("none", &b) == REMAP_SET && b.set && b.count == 0);
	CHECK(Remap_parse("release:None", &b) == REMAP_SET && b.count == 0 && b.trigger == MOSAIC_TRIGGER_RELEASE);

	CHECK(Remap_parse("a,b,c,d", &b) == REMAP_SET && b.count == 4);

	// bad ones leave the button unset
	CHECK(Remap_parse("a,b,c,d,e", &b) == REMAP_BAD && !b.set);
	CHECK(Remap_parse("hold:gamelist.open", &b) == REMAP_BAD && !b.set);
	CHECK(Remap_parse("release:", &b) == REMAP_BAD);
	CHECK(Remap_parse("gamelist.open,", &b) == REMAP_BAD);
	CHECK(Remap_parse(",gamelist.open", &b) == REMAP_BAD);
	CHECK(Remap_parse("gamelist open", &b) == REMAP_BAD);
	CHECK(Remap_parse("gamelist.open,gamelist.open", &b) == REMAP_BAD);
	CHECK(Remap_parse("none,gamelist.open", &b) == REMAP_BAD);
	CHECK(Remap_parse("press:release:gamelist.open", &b) == REMAP_BAD);
	CHECK(Remap_parse("releasereleaserelease:gamelist.open", &b) == REMAP_BAD); // longer than any timing word
	char long_id[REMAP_ID_LEN + 2];
	memset(long_id, 'a', sizeof(long_id) - 1);
	long_id[sizeof(long_id) - 1] = '\0';
	CHECK(Remap_parse(long_id, &b) == REMAP_BAD);
}

static void testPresets(void) {
	RemapTable t;
	Remap_presets(&t, 0, 0);
	for (int i = 0; i < REMAP_BUTTONS; i++) CHECK(!t.buttons[i].set);

	// resume with A: A resumes or opens on press, X opens on release
	Remap_presets(&t, 1, 0);
	const RemapButton* a = &t.buttons[MOSAIC_BUTTON_A];
	const RemapButton* x = &t.buttons[MOSAIC_BUTTON_X];
	CHECK(a->set && a->trigger == MOSAIC_TRIGGER_PRESS && a->count == 2);
	CHECK_STR(a->actions[0], "gamelist.resume");
	CHECK_STR(a->actions[1], "gamelist.open");
	CHECK(x->set && x->trigger == MOSAIC_TRIGGER_RELEASE && x->count == 1);
	CHECK_STR(x->actions[0], "gamelist.open");

	// resume on press: X resumes on press, A is stock's
	Remap_presets(&t, 0, 1);
	CHECK(!a->set);
	CHECK(x->set && x->trigger == MOSAIC_TRIGGER_PRESS && x->count == 1);
	CHECK_STR(x->actions[0], "gamelist.resume");

	// both: X opens on press
	Remap_presets(&t, 1, 1);
	CHECK(a->set && a->trigger == MOSAIC_TRIGGER_PRESS && a->count == 2);
	CHECK(x->set && x->trigger == MOSAIC_TRIGGER_PRESS && x->count == 1);
	CHECK_STR(x->actions[0], "gamelist.open");

	for (int i = 0; i < REMAP_BUTTONS; i++) {
		if (i != MOSAIC_BUTTON_A && i != MOSAIC_BUTTON_X) CHECK(!t.buttons[i].set);
	}
}

static void testCalls(void) {
	RemapTable t;
	RemapCall c[REMAP_CALLS_MAX];
	Remap_presets(&t, 0, 0);
	CHECK(Remap_calls(&t, c) == 0);

	// the first action of every button comes first, so OPEN's hint is on X
	Remap_presets(&t, 1, 0);
	CHECK(Remap_calls(&t, c) == 3);
	CHECK(!c[0].bind && c[0].button == MOSAIC_BUTTON_A && !strcmp(c[0].action, "gamelist.resume"));
	CHECK(!c[1].bind && c[1].button == MOSAIC_BUTTON_X && c[1].trigger == MOSAIC_TRIGGER_RELEASE && !strcmp(c[1].action, "gamelist.open"));
	CHECK(!c[2].bind && c[2].button == MOSAIC_BUTTON_A && !strcmp(c[2].action, "gamelist.open"));

	// free buttons are bound, "none" overrides with nothing and goes first
	Remap_presets(&t, 0, 0);
	Remap_parse("random-game.pick,favorites.toggle", &t.buttons[MOSAIC_BUTTON_Y]);
	Remap_parse("none", &t.buttons[MOSAIC_BUTTON_START]);
	Remap_parse("release:none", &t.buttons[MOSAIC_BUTTON_SELECT]);
	CHECK(Remap_calls(&t, c) == 4);
	CHECK(!c[0].bind && c[0].button == MOSAIC_BUTTON_START && c[0].action == NULL);
	CHECK(!c[1].bind && c[1].button == MOSAIC_BUTTON_SELECT && c[1].action == NULL && c[1].trigger == MOSAIC_TRIGGER_RELEASE);
	CHECK(c[2].bind && c[2].button == MOSAIC_BUTTON_Y && !strcmp(c[2].action, "random-game.pick"));
	CHECK(c[3].bind && c[3].button == MOSAIC_BUTTON_Y && !strcmp(c[3].action, "favorites.toggle"));

	// every button full
	for (int i = 0; i < REMAP_BUTTONS; i++) Remap_parse("a,b,c,d", &t.buttons[i]);
	CHECK(Remap_calls(&t, c) == REMAP_CALLS_MAX);
}

///////////////////////////////////////
// the plugin

static void testNothingSet(void) {
	reset();
	start();
	CHECK(override_count == 0 && bind_count == 0 && logs == 0);
}

static void testResumeWithA(void) {
	reset();
	setCfg("resume_with_a", "on");
	start();
	CHECK(override_count == 3 && bind_count == 0);
	CHECK(checkCall(&overrides[0], MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.resume"));
	CHECK(checkCall(&overrides[1], MOSAIC_BUTTON_X, MOSAIC_TRIGGER_RELEASE, "gamelist.open"));
	CHECK(checkCall(&overrides[2], MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.open"));
}

static void testResumeOnPress(void) {
	reset();
	setCfg("resume_on_press", "on");
	start();
	CHECK(override_count == 1);
	CHECK(checkCall(&overrides[0], MOSAIC_BUTTON_X, MOSAIC_TRIGGER_PRESS, "gamelist.resume"));

	reset();
	setCfg("resume_with_a", "on");
	setCfg("resume_on_press", "on");
	start();
	CHECK(override_count == 3);
	CHECK(checkCall(&overrides[1], MOSAIC_BUTTON_X, MOSAIC_TRIGGER_PRESS, "gamelist.open"));
}

static void testButtons(void) {
	reset();
	setCfg("resume_with_a", "on");
	setCfg("x", "none");                                  // replaces the preset's X
	setCfg("y", "release:random-game.pick");              // a free button is bound
	setCfg("l1", "gamelist.next_letter");                 // only in the cfg file
	setCfg("r1", "gamelist.prev_letter");
	setCfg("b", "hold:gamelist.back");                    // bad, so B stays stock's
	start();
	CHECK(logs == 1);
	CHECK(override_count == 5);
	CHECK(checkCall(&overrides[0], MOSAIC_BUTTON_X, MOSAIC_TRIGGER_PRESS, ""));
	CHECK(checkCall(&overrides[1], MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.resume"));
	CHECK(checkCall(&overrides[2], MOSAIC_BUTTON_L1, MOSAIC_TRIGGER_PRESS, "gamelist.next_letter"));
	CHECK(checkCall(&overrides[3], MOSAIC_BUTTON_R1, MOSAIC_TRIGGER_PRESS, "gamelist.prev_letter"));
	CHECK(checkCall(&overrides[4], MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.open"));
	CHECK(bind_count == 1);
	CHECK(checkCall(&binds[0], MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_RELEASE, "random-game.pick"));
}

static void testRefusals(void) {
	// another plugin, eg. platform-jump, has L1; the rest still goes in
	reset();
	setCfg("l1", "gamelist.next_letter");
	setCfg("a", "favorites.toggle,gamelist.open");
	setCfg("l2", "favorites.toggle");
	refuse_button = MOSAIC_BUTTON_L1;
	unknown_action = "favorites.toggle";
	start();
	CHECK(override_count == 1);
	CHECK(checkCall(&overrides[0], MOSAIC_BUTTON_A, MOSAIC_TRIGGER_PRESS, "gamelist.open"));
	CHECK(bind_count == 0);
	CHECK(logs == 3); // L1, favorites on A and favorites on L2

	// a launcher without override_button still takes the free buttons
	reset();
	setCfg("resume_with_a", "on");
	setCfg("y", "gamelist.resume");
	fake.size = offsetof(MosaicHost, override_button);
	start();
	fake.size = sizeof(MosaicHost);
	CHECK(override_count == 0);
	CHECK(bind_count == 1);
	CHECK(checkCall(&binds[0], MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, "gamelist.resume"));
	CHECK(logs == 3);
}

int main(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "button-remap");

	testKeys();
	testParse();
	testPresets();
	testCalls();
	testNothingSet();
	testResumeWithA();
	testResumeOnPress();
	testButtons();
	testRefusals();
	CHECK_DONE();
}
