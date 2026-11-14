// The first run plugin: its model on its own, then the plugin against a
// fake host on a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "fakeloaded.h"
#include "fakeproviders.h"
#include "../../plugins/first-run/first_run.c"

#define CARD TEST_SDCARD_PATH
#define USERDATA CARD "/.userdata/desktop"

static unsigned int bit(int button) {
	return MOSAIC_BUTTON_BIT(button);
}

///////////////////////////////////////
// the model

static void testPages(void) {
	FrWizard w;
	FR_init(&w, 3, 2, 1, 1, 5);
	CHECK(w.page == FR_WELCOME);
	CHECK(FR_pageCount(&w) == 7);
	CHECK(FR_pageNumber(&w, FR_SETUP) == 3);
	CHECK(FR_pageNumber(&w, FR_PIN) == 4);
	CHECK(FR_pageNumber(&w, FR_BUTTONS) == 6);
	CHECK(FR_nextPage(&w, FR_WELCOME) == FR_TOGGLES);
	CHECK(FR_nextPage(&w, FR_TOGGLES) == FR_SETUP);
	CHECK(FR_nextPage(&w, FR_DONE) == -1);
	CHECK(FR_prevPage(&w, FR_WELCOME) == -1);

	// pages left out are passed over both ways
	FR_init(&w, 0, 0, 0, 1, 0);
	CHECK(FR_pageCount(&w) == 3);
	CHECK(FR_nextPage(&w, FR_WELCOME) == FR_PLUGINS);
	CHECK(FR_prevPage(&w, FR_DONE) == FR_PLUGINS);
	CHECK(FR_pageNumber(&w, FR_DONE) == 3);
	FR_init(&w, 0, 0, 0, 0, 0);
	CHECK(FR_pageCount(&w) == 2);
	CHECK(FR_nextPage(&w, FR_WELCOME) == FR_DONE);
	FR_init(&w, 0, 1, 0, 0, 4);
	CHECK(FR_nextPage(&w, FR_WELCOME) == FR_SETUP);
	CHECK(FR_nextPage(&w, FR_SETUP) == FR_BUTTONS);

	// toggle rows are capped
	FR_init(&w, 99, 0, 0, 0, 0);
	CHECK(w.rows[FR_TOGGLES] == FR_TOGGLES_MAX);
	FR_init(&w, -2, 0, 0, 0, 0);
	CHECK(w.rows[FR_TOGGLES] == 0 && !w.has[FR_TOGGLES]);

	// the buttons page alone
	FR_initButtons(&w, 0);
	CHECK(w.page == FR_BUTTONS && w.alone);
	CHECK(FR_pageCount(&w) == 1);
	CHECK(FR_nextPage(&w, FR_BUTTONS) == -1 && FR_prevPage(&w, FR_BUTTONS) == -1);
}

static void testInput(void) {
	FrWizard w;
	FR_init(&w, 2, 2, 1, 2, 10);
	// B does nothing on the first page, and the d-pad nothing off the rows
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_B)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_RIGHT)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_NOTHING);
	CHECK(FR_input(&w, 0) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_TOGGLES);

	// up and down wrap through the rows, left and right flip
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_UP)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 0);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_LEFT)) == FR_FLIP);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_RIGHT)) == FR_FLIP);

	// on the setup page left and right change the selected row's value
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_SETUP && w.row == 0);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_LEFT)) == FR_CHANGE && w.dir == -1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_RIGHT)) == FR_CHANGE && w.dir == 1);

	// A first when pressed together, and a page change starts at the top row
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A) | bit(MOSAIC_BUTTON_B)) == FR_REDRAW && w.page == FR_PIN && w.row == 0);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_B)) == FR_REDRAW && w.page == FR_SETUP);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_PIN);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_Y)) == FR_SKIP);
	// the plugins page's rows are picked, not flipped
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_PLUGINS);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_UP)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_RIGHT)) == FR_NOTHING);

	// the buttons page scrolls a line at a time, as far as the last line fits
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_BUTTONS && w.row == 0);
	FR_setVisible(&w, 8);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_UP)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 2);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_NOTHING && w.row == 2);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_LEFT)) == FR_NOTHING);
	FR_setVisible(&w, 9); // more room: back up so the page stays full
	CHECK(w.row == 1);
	FR_setVisible(&w, 20);
	CHECK(w.row == 0);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_UP)) == FR_NOTHING);

	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_DONE);
	// nothing left to skip on the last page, where A finishes
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_Y)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_FINISH);
	CHECK(w.page == FR_DONE);

	// alone, A and B close it, Y does nothing, and up and down scroll
	FR_initButtons(&w, 3);
	FR_setVisible(&w, 2);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_Y)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_B)) == FR_CLOSE);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_CLOSE);
}

static void testShouldOpen(void) {
	CHECK(FR_shouldOpen(0, 0, "gamelist", NULL) == 1);
	CHECK(FR_shouldOpen(0, 0, "home-dashboard.main", "home-dashboard.main") == 1);
	CHECK(FR_shouldOpen(0, 0, "gamelist", "home-dashboard.main") == 1);
	CHECK(FR_shouldOpen(1, 0, "gamelist", NULL) == 0);
	CHECK(FR_shouldOpen(0, 1, "gamelist", NULL) == 0);
	CHECK(FR_shouldOpen(0, 0, "quickmenu", NULL) == 0);
	CHECK(FR_shouldOpen(0, 0, "switcher", NULL) == 0);
	CHECK(FR_shouldOpen(0, 0, "dialog", NULL) == 0);
	CHECK(FR_shouldOpen(0, 0, "switcher-plus.main", "home-dashboard.main") == 0);
	CHECK(FR_shouldOpen(0, 0, "charging-screen.main", NULL) == 0);
	CHECK(FR_shouldOpen(0, 0, NULL, NULL) == 0);
	CHECK(FR_shouldOpen(0, 0, "", "") == 0);
}

static void testHints(void) {
	FrWizard w;
	MosaicHint left[2], right[1];
	int lc = 0, rc = 0;
	FR_init(&w, 1, 0, 0, 0, 0);
	FR_hints(&w, left, &lc, right, &rc);
	CHECK(lc == 1 && left[0].button == MOSAIC_BUTTON_Y);
	CHECK_STR(left[0].label, "SKIP");
	CHECK(rc == 1 && right[0].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "NEXT");
	w.page = FR_TOGGLES;
	FR_hints(&w, left, &lc, right, &rc);
	CHECK(lc == 2 && left[0].button == MOSAIC_BUTTON_B && left[1].button == MOSAIC_BUTTON_Y);
	CHECK_STR(left[0].label, "BACK");
	w.page = FR_DONE;
	FR_hints(&w, left, &lc, right, &rc);
	CHECK(lc == 1 && left[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(right[0].label, "DONE");
	FR_initButtons(&w, 4);
	FR_hints(&w, left, &lc, right, &rc);
	CHECK(lc == 1 && left[0].button == MOSAIC_BUTTON_B && rc == 0);
	CHECK_STR(left[0].label, "BACK");
}

static void testIds(void) {
	char ids[FR_SUGGEST_MAX][FR_ID_MAX];
	CHECK(FR_parseIds(" home-dashboard , favorites,,search, favorites ,", ids, FR_SUGGEST_MAX) == 3);
	CHECK_STR(ids[0], "home-dashboard");
	CHECK_STR(ids[1], "favorites");
	CHECK_STR(ids[2], "search");
	CHECK(FR_parseIds("", ids, FR_SUGGEST_MAX) == 0);
	CHECK(FR_parseIds(NULL, ids, FR_SUGGEST_MAX) == 0);
	CHECK(FR_parseIds("a,b,c", ids, 2) == 2);
	char longer[FR_ID_MAX + 8];
	memset(longer, 'x', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = '\0';
	CHECK(FR_parseIds(longer, ids, FR_SUGGEST_MAX) == 0);

	MosaicHost h = { .size = sizeof(MosaicHost), .plugin_loaded = fakePluginLoaded };
	fake_loaded = "favorites,search";
	CHECK(FR_pluginLoaded(&h, "favorites") == 1);
	CHECK(FR_pluginLoaded(&h, "search") == 1);
	CHECK(FR_pluginLoaded(&h, "favorites-plus") == 0);
	CHECK(FR_pluginLoaded(&h, NULL) == 0);
	CHECK(FR_pluginLoaded(NULL, "favorites") == 0);
	// an older launcher's table, without plugin_loaded
	h.size = FAKE_HOST_BEFORE_LOADED;
	CHECK(FR_pluginLoaded(&h, "favorites") == 0);
	fake_loaded = "";
}

static void testValues(void) {
	char out[16];
	CHECK(FR_value("a=1\nkey= spaced \r\nkey=second", "key", out, sizeof(out)) == 6);
	CHECK_STR(out, "spaced");
	CHECK(FR_value("keys=no\nkey=", "key", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(FR_value("other=1", "key", out, sizeof(out)) == -1);
	CHECK(FR_value("key=a very long value", "key", out, 5) == 17);
	CHECK_STR(out, "a ve");
	CHECK(FR_value(NULL, "key", out, sizeof(out)) == -1);

	// a plugin.txt: name and description, else the summary
	char name[64], text[256];
	const char* manifest = "# a comment\nid=tags\nname=Tags\nsummary=Short.\ndescription=Longer, for first-run.\n";
	CHECK(FR_manifest(manifest, "tags", name, sizeof(name), text, sizeof(text)) == 1);
	CHECK_STR(name, "Tags");
	CHECK_STR(text, "Longer, for first-run.");
	CHECK(FR_manifest("id=old\nsummary=Only a summary.\n", "old", name, sizeof(name), text, sizeof(text)) == 1);
	CHECK_STR(name, "old");
	CHECK_STR(text, "Only a summary.");
	CHECK(FR_manifest("id=bare\n", "bare", name, sizeof(name), text, sizeof(text)) == 1);
	CHECK_STR(text, "");
	CHECK(FR_manifest(manifest, "tag", name, sizeof(name), text, sizeof(text)) == 0);
	CHECK(FR_manifest(manifest, "", name, sizeof(name), text, sizeof(text)) == 0);
}

static void testSetup(void) {
	FrSetup s;
	const char* reply = "mode=minui\nmode.count=3\nmode.0=dashboard\nmode.label.0=Dashboard\nmode.1=minui\nmode.label.1=MinUI\n"
		"mode.2=pico8\nrestart=1";
	CHECK(FR_parseSetup(reply, "mode", &s) == 0);
	CHECK_STR(s.value, "minui");
	CHECK(s.count == 3 && s.restart == 1);
	CHECK_STR(s.labels[1], "MinUI");
	CHECK_STR(s.labels[2], "pico8"); // the value without a label
	CHECK(FR_setupIndex(&s) == 1);
	CHECK_STR(FR_setupLabel(&s), "MinUI");
	CHECK(FR_setupStep(&s, 1) == 2);
	CHECK(FR_setupStep(&s, -1) == 0);
	snprintf(s.value, sizeof(s.value), "pico8");
	CHECK(FR_setupStep(&s, 1) == 0); // wraps
	snprintf(s.value, sizeof(s.value), "gone");
	CHECK(FR_setupIndex(&s) == -1);
	CHECK_STR(FR_setupLabel(&s), "gone");
	CHECK(FR_setupStep(&s, 1) == 0);
	CHECK(FR_setupStep(&s, -1) == 2);

	// a missing choice is passed over; no key or no choices is no row
	CHECK(FR_parseSetup("theme=a\ntheme.count=2\ntheme.1=b\nrestart=0", "theme", &s) == 0);
	CHECK(s.count == 1 && s.restart == 0);
	CHECK_STR(s.values[0], "b");
	CHECK(FR_parseSetup("theme=a\ntheme.count=0", "theme", &s) == -1);
	CHECK(FR_parseSetup("mode=a\nmode.count=1\nmode.0=a", "theme", &s) == -1);
	CHECK(FR_parseSetup("", "theme", &s) == -1);
	s.count = 0;
	CHECK(FR_setupStep(&s, 1) == -1);
}

static MosaicActionInfo action(const char* id, const char* label) {
	MosaicActionInfo a;
	memset(&a, 0, sizeof(a));
	a.size = sizeof(a);
	snprintf(a.id, sizeof(a.id), "%s", id);
	snprintf(a.label, sizeof(a.label), "%s", label);
	return a;
}

static void bindAction(MosaicActionInfo* a, int screen, int button) {
	if (a->binding_count < MOSAIC_ACTION_BINDINGS) {
		a->bindings[a->binding_count].screen = screen;
		a->bindings[a->binding_count].button = button;
	}
	a->binding_count++;
}

static void testButtonLines(void) {
	MosaicActionInfo actions[5];
	actions[0] = action("gamelist.open", "OPEN");
	bindAction(&actions[0], MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_A);
	actions[1] = action("favorites.toggle", "FAVORITE");
	bindAction(&actions[1], MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y);
	bindAction(&actions[1], -1, MOSAIC_BUTTON_FN2);
	actions[2] = action("first-run.open", "First run");
	actions[3] = action("switcher.thing", "");
	bindAction(&actions[3], MOSAIC_SCREEN_SWITCHER, MOSAIC_BUTTON_A);
	bindAction(&actions[3], MOSAIC_SCREEN_QUICKMENU, MOSAIC_BUTTON_SELECT);
	actions[4] = action("theme-plus.pick", "Theme");

	MosaicFnBinding fn[3];
	memset(fn, 0, sizeof(fn));
	fn[0].by = MOSAIC_FN_USER;
	snprintf(fn[0].action, sizeof(fn[0].action), "pak:Clock.pak");
	fn[1].by = MOSAIC_FN_PLUGIN;
	snprintf(fn[1].action, sizeof(fn[1].action), "action:favorites.toggle");
	int there[3] = { 1, 1, 0 };
	FrLine out[FR_LINES_MAX];
	int n = FR_buttonLines(actions, 5, fn, there, out, FR_LINES_MAX);
	CHECK(n == 14);
	CHECK(out[0].heading && out[0].button == MOSAIC_BUTTON_A);
	CHECK(!out[1].heading && out[1].screen == MOSAIC_SCREEN_GAMELIST);
	CHECK_STR(out[1].label, "OPEN");
	CHECK(out[2].screen == MOSAIC_SCREEN_SWITCHER);
	CHECK_STR(out[2].label, "switcher.thing"); // the id without a label
	CHECK(out[3].heading && out[3].button == MOSAIC_BUTTON_Y);
	CHECK_STR(out[4].label, "FAVORITE");
	CHECK(out[5].heading && out[5].button == MOSAIC_BUTTON_SELECT);
	CHECK(out[6].screen == MOSAIC_SCREEN_QUICKMENU);
	// the FN buttons: a pak, and an action bound to one
	CHECK(out[7].heading && out[7].button == MOSAIC_BUTTON_FN1);
	CHECK_STR(out[8].label, "Clock");
	CHECK(out[8].screen == -1);
	CHECK(out[9].heading && out[9].button == MOSAIC_BUTTON_FN2);
	CHECK_STR(out[10].label, "FAVORITE");
	// last the actions on no button
	CHECK(out[11].heading && out[11].button == -1);
	CHECK_STR(out[12].label, "First run");
	CHECK_STR(out[13].label, "Theme");

	// a device without the FN buttons, and nothing unbound: no headings without items
	there[0] = there[1] = 0;
	n = FR_buttonLines(actions, 2, fn, there, out, FR_LINES_MAX);
	CHECK(n == 4);
	CHECK(out[2].heading && out[2].button == MOSAIC_BUTTON_Y);
	// an FN button with nothing on it is left out
	there[2] = 1;
	CHECK(FR_buttonLines(actions, 2, fn, there, out, FR_LINES_MAX) == 4);
	// out of room: a heading isn't left last
	CHECK(FR_buttonLines(actions, 5, fn, there, out, 4) == 3);
	CHECK(FR_buttonLines(actions, 5, fn, there, out, 2) == 2);
	CHECK(FR_buttonLines(NULL, 0, NULL, NULL, out, FR_LINES_MAX) == 0);

	CHECK_STR(FR_buttonName(MOSAIC_BUTTON_SELECT), "SELECT");
	CHECK(FR_buttonName(MOSAIC_BUTTON_FN1) == NULL);
	CHECK(FR_buttonName(-1) == NULL);
}

static int charWidth(const char* line, void* data) {
	(void)data;
	return 10 * (int)strlen(line);
}

static void testWrap(void) {
	char lines[4][FR_LINE_MAX];
	CHECK(FR_wrap("one two three four", 100, charWidth, NULL, lines, 4) == 2);
	CHECK_STR(lines[0], "one two");
	CHECK_STR(lines[1], "three four");
	CHECK(FR_wrap("  short", 200, charWidth, NULL, lines, 4) == 1);
	CHECK_STR(lines[0], "short");
	// a word too wide alone gets a line of its own
	CHECK(FR_wrap("a abcdefghijkl b", 50, charWidth, NULL, lines, 4) == 3);
	CHECK_STR(lines[0], "a");
	CHECK_STR(lines[1], "abcdefghijkl");
	CHECK_STR(lines[2], "b");
	// more than fits: the last line is cut with dots
	CHECK(FR_wrap("aa bb cc dd ee ff", 50, charWidth, NULL, lines, 2) == 2);
	CHECK_STR(lines[0], "aa bb");
	CHECK_STR(lines[1], "cc...");
	CHECK(FR_wrap("", 50, charWidth, NULL, lines, 2) == 0);
	CHECK(FR_wrap("x", 0, charWidth, NULL, lines, 2) == 0);
	CHECK(FR_wrap("x", 50, charWidth, NULL, lines, 0) == 0);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
};

static MosaicSurface screen_surface = { 1024, 768 };
static MosaicHost fake_host;
static MosaicScreenHooks registered, registered_buttons;
static int registered_count = 0;
static char action_id[64], tile_id[64], tiles[160];
static void (*action_run)(const char*, void*);
static void (*buttons_run)(const char*, void*);
static int bound_button = -1;
static char all_log[16384];
static int redraws = 0, opens = 0, open_rc = 0, pin = 0, renders_ok = 0;
static char current[64] = "gamelist";
static const char* home = NULL;
static char cfg_seen[8] = "off";
static const char* cfg_toggles = "on";
static const char* cfg_pin = "on";
static const char* cfg_suggest = NULL;
static const char* cfg_setup = "on";
static const char* cfg_buttons = "on";
static int toggle_value[4] = { 1, 0, 1, -1 }; // notify_adjustments unknown
static char hint_text[256];

static void fakeLog(const char* id, const char* message) {
	size_t used = strlen(all_log);
	snprintf(all_log + used, sizeof(all_log) - used, "%s\n", message);
}

static int logged(const char* line) {
	char want[256];
	snprintf(want, sizeof(want), "%s\n", line);
	return strstr(all_log, want) != NULL;
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "seen")) return cfg_seen;
	if (!strcmp(key, "toggles")) return cfg_toggles;
	if (!strcmp(key, "pin")) return cfg_pin;
	if (!strcmp(key, "suggest") && cfg_suggest) return cfg_suggest;
	if (!strcmp(key, "setup")) return cfg_setup;
	if (!strcmp(key, "buttons")) return cfg_buttons;
	return fallback;
}

static void fakeCfgSet(const char* key, const char* value) {
	CHECK_STR(key, "seen");
	snprintf(cfg_seen, sizeof(cfg_seen), "%s", value);
}

static int toggleIndex(const char* name) {
	static const char* names[] = { "show_clock", "show_battery_percent", "haptics", "notify_adjustments" };
	for (int i = 0; i < 4; i++) {
		if (!strcmp(names[i], name)) return i;
	}
	return -1;
}

static int fakeToggleGet(const char* name) {
	int i = toggleIndex(name);
	return i < 0 ? -1 : toggle_value[i];
}

static int fakeToggleSet(const char* name, int on) {
	int i = toggleIndex(name);
	if (i < 0 || toggle_value[i] < 0 || (on != 0 && on != 1)) return -1;
	toggle_value[i] = on;
	return 0;
}

static int fakeHasPin(void) {
	return pin;
}

static void fakeAskPin(const char* title, void (*on_done)(int, void*), void* data) {
	(void)title;
	on_done(MOSAIC_PIN_RIGHT, data);
}

static void fakeRedraw(void) {
	redraws++;
}

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	if (!strcmp(id, BUTTONS_SCREEN_ID)) registered_buttons = *hooks;
	else {
		CHECK_STR(id, SCREEN_ID);
		registered = *hooks;
	}
	registered_count++;
	return 0;
}

static int fakeAction(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)data;
	(void)available;
	(void)label;
	if (!strcmp(id, BUTTONS_ACTION_ID)) {
		buttons_run = run;
		return 0;
	}
	snprintf(action_id, sizeof(action_id), "%s", id);
	action_run = run;
	return 0;
}

static int fakeTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)row;
	(void)icon;
	(void)icon_off;
	(void)is_on;
	if (!tile_id[0]) snprintf(tile_id, sizeof(tile_id), "%s", id);
	size_t used = strlen(tiles);
	snprintf(tiles + used, sizeof(tiles) - used, "%s ", id);
	return 0;
}

static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen;
	(void)trigger;
	bound_button = button;
	return 0;
}

// the screen open, for press and draw
static MosaicScreenHooks* showing = &registered;

static int fakeOpenScreen(const char* id) {
	opens++;
	if (open_rc != 0) return open_rc;
	snprintf(current, sizeof(current), "%s", id);
	if (!strcmp(id, BUTTONS_SCREEN_ID)) showing = &registered_buttons;
	else {
		CHECK_STR(id, SCREEN_ID);
		showing = &registered;
	}
	return showing->enter ? showing->enter(showing->data) : 0;
}

// home-dashboard's and theme-plus's setup services, which keep what they're set to
static char fake_mode[32] = "dashboard", fake_theme[32] = "stock";
static int fakeHomeSetup(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char wanted[32];
	int refused = 0;
	if (fakeEventValue(args, "mode", wanted, sizeof(wanted)) >= 0) {
		if (!strcmp(wanted, "dashboard") || !strcmp(wanted, "minui")) snprintf(fake_mode, sizeof(fake_mode), "%s", wanted);
		else refused = 1;
	}
	snprintf(out, size, "mode=%s\nmode.count=3\nmode.0=dashboard\nmode.label.0=Dashboard\nmode.1=minui\nmode.label.1=MinUI\n"
		"mode.2=pico8\nrestart=1%s", fake_mode, refused ? "\nerror=mode" : "");
	return 0;
}
static int fakeThemeSetup(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char wanted[32];
	if (fakeEventValue(args, "theme", wanted, sizeof(wanted)) >= 0) snprintf(fake_theme, sizeof(fake_theme), "%s", wanted);
	snprintf(out, size, "theme=%s\ntheme.count=2\ntheme.0=stock\ntheme.label.0=Stock\ntheme.1=Dark\nrestart=0", fake_theme);
	return 0;
}

// the plugins loaded: first-run, at its bundled folder, and favorites
static int fakePluginsCount(void) {
	return 2;
}
static int fakePluginInfo(int index, MosaicPluginInfo* out) {
	if (index < 0 || index > 1 || !out) return -1;
	snprintf(out->id, sizeof(out->id), "%s", index ? "favorites" : "first-run");
	snprintf(out->res, sizeof(out->res), "%s/Launcher.pak/plugins/%s.plugin/res", CARD, out->id);
	snprintf(out->description, sizeof(out->description), "%s", index ? "Keeps your favorites." : "");
	return 0;
}

// the actions: the game list's A, favorites' Y and first-run's own
static int fakeActionsCount(void) {
	return 3;
}
static int fakeActionInfo(int index, MosaicActionInfo* out) {
	if (index < 0 || index > 2 || !out) return -1;
	static const char* const ids[] = { "gamelist.open", "favorites.toggle", "first-run.open" };
	static const char* const labels[] = { "OPEN", "FAVORITE", "First run" };
	static const int buttons[] = { MOSAIC_BUTTON_A, MOSAIC_BUTTON_Y, -1 };
	memset((char*)out + sizeof(out->size), 0, sizeof(*out) - sizeof(out->size));
	snprintf(out->id, sizeof(out->id), "%s", ids[index]);
	snprintf(out->label, sizeof(out->label), "%s", labels[index]);
	if (buttons[index] >= 0) {
		out->binding_count = 1;
		out->bindings[0].screen = MOSAIC_SCREEN_GAMELIST;
		out->bindings[0].button = buttons[index];
	}
	return 0;
}

static const char* fakeCurrent(void) {
	return current;
}

static const char* fakeHome(void) {
	return home;
}

static int fakeThemeColor(int index) {
	return 0x101010 * index;
}
static int fakeScale(void) {
	return 3;
}
static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)target;
	(void)rect;
	(void)color;
	return 0;
}
static int fakeFillRound(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	(void)radius;
	return fakeFill(target, rect, color);
}
static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return 10 * (int)strlen(text);
}
static int fakeTextHeight(int font) {
	return font == MOSAIC_FONT_LARGE ? 48 : font == MOSAIC_FONT_SMALL ? 36 : 30;
}
static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	snprintf(out, size, "%s", text);
	int w = fakeTextWidth(font, out);
	return w < max_width ? w : max_width;
}
static char drawn_text[8192];
static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)target;
	(void)font;
	(void)x;
	(void)y;
	(void)color;
	size_t used = strlen(drawn_text);
	snprintf(drawn_text + used, sizeof(drawn_text) - used, "%s|", text);
	return max_width;
}
static int fakeDrawHints(MosaicSurface* screen, int group, const MosaicHint* h, int count, MosaicRect* drawn) {
	(void)screen;
	(void)drawn;
	for (int i = 0; i < count; i++) {
		size_t used = strlen(hint_text);
		snprintf(hint_text + used, sizeof(hint_text) - used, "%s%s ", group == MOSAIC_HINTS_LEFT ? "L:" : "R:", h[i].label);
	}
	return 0;
}

static void setupHost(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.platform = "desktop";
	fake_host.sdcard_path = CARD;
	fake_host.userdata_path = USERDATA;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.cfg_set = fakeCfgSet;
	fake_host.request_redraw = fakeRedraw;
	fake_host.register_action = fakeAction;
	fake_host.bind_button = fakeBind;
	fake_host.quick_tile = fakeTile;
	fake_host.stock_toggle_get = fakeToggleGet;
	fake_host.stock_toggle_set = fakeToggleSet;
	fake_host.has_pin = fakeHasPin;
	fake_host.plugin_loaded = fakePluginLoaded;
	fake_host.ask_pin = fakeAskPin;
	fake_host.register_screen = fakeRegister;
	fake_host.open_screen = fakeOpenScreen;
	fake_host.current_screen = fakeCurrent;
	fake_host.home_screen = fakeHome;
	fake_host.theme_color = fakeThemeColor;
	fake_host.screen_scale = fakeScale;
	fake_host.fill_rect = fakeFill;
	fake_host.fill_round_rect = fakeFillRound;
	fake_host.text_width = fakeTextWidth;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.draw_hints = fakeDrawHints;
	fake_host.plugins_count = fakePluginsCount;
	fake_host.plugin_info = fakePluginInfo;
	fake_host.event_value = fakeEventValue;
	fake_host.provide = fakeProvide;
	fake_host.call = fakeCall;
	fake_host.actions_count = fakeActionsCount;
	fake_host.action_info = fakeActionInfo;
}

static void reset(void) {
	all_log[0] = '\0';
	redraws = opens = open_rc = pin = registered_count = 0;
	snprintf(current, sizeof(current), "gamelist");
	home = NULL;
	snprintf(cfg_seen, sizeof(cfg_seen), "off");
	cfg_toggles = cfg_pin = cfg_setup = cfg_buttons = "on";
	cfg_suggest = NULL;
	toggle_value[0] = 1;
	toggle_value[1] = 0;
	toggle_value[2] = 1;
	toggle_value[3] = -1;
	tile_id[0] = tiles[0] = '\0';
	showing = &registered;
	setupHost();
}

static void writeFile(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static int press(int button) {
	MosaicInput in = { sizeof(MosaicInput), bit(button), bit(button), 0, bit(button) };
	return showing->input(&in, showing->data);
}

static void draw(void) {
	MosaicScreenView view = { sizeof(MosaicScreenView), { 0, 0, 1024, 768 }, { 800, 10, 214, 90 }, { 0, 0, 0, 0 }, SCREEN_ID };
	hint_text[0] = '\0';
	drawn_text[0] = '\0';
	if (showing->render(&screen_surface, &view, showing->data) == 0) renders_ok++;
}

static void testPlugin(void) {
	mkdir(CARD, 0755);
	mkdir(CARD "/.userdata", 0755);
	mkdir(USERDATA, 0755);
	mkdir(USERDATA "/launcher", 0755);
	// the suggested plugins' plugin.txt files, bundled beside first-run's
	// folder or installed as a pak in Tools
	mkdir(CARD "/Launcher.pak", 0755);
	mkdir(CARD "/Launcher.pak/plugins", 0755);
	mkdir(CARD "/Launcher.pak/plugins/Search.plugin", 0755);
	writeFile(CARD "/Launcher.pak/plugins/Search.plugin/plugin.txt",
		"id=search\nname=Search\nsummary=Short.\ndescription=Finds games by typing part of a name.\n");
	mkdir(CARD "/Launcher.pak/plugins/switcher-plus.plugin", 0755);
	writeFile(CARD "/Launcher.pak/plugins/switcher-plus.plugin/plugin.txt", "id=switcher-plus\nname=Switcher plus\nsummary=Only a summary.\n");
	mkdir(CARD "/Tools", 0755);
	mkdir(CARD "/Tools/desktop", 0755);
	mkdir(CARD "/Tools/desktop/Home.pak", 0755);
	writeFile(CARD "/Tools/desktop/Home.pak/plugin.txt", "id=home-dashboard\nname=Home dashboard\ndescription=A home screen.\n");
	// search is on but isn't loaded, as when its init declined, so it shows off
	writeFile(USERDATA "/launcher/plugins.txt", "favorites=on\nsearch=on\n");
	fake_loaded = "favorites,first-run";
	fakeProvide("home-dashboard.setup", fakeHomeSetup);
	fakeProvide("theme-plus.setup", fakeThemeSetup);

	reset();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	CHECK(registered_count == 2);
	CHECK_STR(action_id, ACTION_ID);
	CHECK_STR(tiles, ACTION_ID " " BUTTONS_ACTION_ID " ");
	CHECK(logged("not seen yet"));

	// opens itself on the first tick over the game list, once
	p->tick(1);
	CHECK(opens == 1);
	CHECK(logged("opened by itself over gamelist"));
	CHECK(logged("toggles: show_clock on, show_battery_percent off, haptics on"));
	CHECK(logged("setup: home-dashboard.setup dashboard, theme-plus.setup stock"));
	CHECK(logged("pin: not set"));
	CHECK(logged("suggesting home-dashboard off, switcher-plus off, favorites on, search off"));
	CHECK(logged("about home-dashboard: A home screen."));
	CHECK(logged("about switcher-plus: Only a summary."));
	CHECK(logged("about favorites: Keeps your favorites."));
	CHECK(logged("about search: Finds games by typing part of a name."));
	CHECK(logged("on A: OPEN (gamelist)"));
	CHECK(logged("on Y: FAVORITE (gamelist)"));
	CHECK(logged("on no button: First run (any)"));
	p->tick(2);
	CHECK(opens == 1);

	draw();
	CHECK(logged("showing welcome, 1 of 7"));
	CHECK_STR(hint_text, "R:NEXT L:SKIP ");
	CHECK(strstr(drawn_text, "Welcome|") != NULL);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_STAY);
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK(logged("showing toggles, 2 of 7"));
	CHECK_STR(hint_text, "R:NEXT L:BACK L:SKIP ");
	CHECK(strstr(drawn_text, "Show clock|On|") != NULL);
	CHECK(strstr(drawn_text, "Volume") == NULL); // the host doesn't know it

	// flips go through the host and show its answer
	press(MOSAIC_BUTTON_RIGHT);
	CHECK(toggle_value[0] == 0);
	CHECK(logged("show_clock now off"));
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_LEFT);
	CHECK(toggle_value[1] == 1);
	CHECK(logged("show_battery_percent now on"));
	draw();
	CHECK(strstr(drawn_text, "Show clock|Off|") != NULL);
	CHECK(strstr(drawn_text, "Show battery percentage|On|") != NULL);

	// the other plugins' settings, through their services
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing setup, 3 of 7"));
	CHECK(strstr(drawn_text, "Home mode|Dashboard|") != NULL);
	CHECK(strstr(drawn_text, "Theme|Stock|") != NULL);
	CHECK(strstr(drawn_text, "Some show from the next start.") != NULL);
	press(MOSAIC_BUTTON_RIGHT);
	CHECK_STR(fake_mode, "minui");
	CHECK(logged("home-dashboard.setup now minui"));
	press(MOSAIC_BUTTON_RIGHT); // pico8, which it refuses
	CHECK_STR(fake_mode, "minui");
	CHECK(logged("home-dashboard.setup refused mode=pico8"));
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_LEFT);
	CHECK_STR(fake_theme, "Dark");
	CHECK(logged("theme-plus.setup now Dark"));
	draw();
	CHECK(strstr(drawn_text, "Home mode|MinUI|") != NULL);
	CHECK(strstr(drawn_text, "Theme|Dark|") != NULL);

	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing pin, 4 of 7"));
	CHECK(strstr(drawn_text, "The launcher has no PIN.|") != NULL);
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing plugins, 5 of 7"));
	CHECK(strstr(drawn_text, "Home dashboard|Off|") != NULL);
	CHECK(strstr(drawn_text, "A home screen.|") != NULL);
	press(MOSAIC_BUTTON_UP);
	draw();
	CHECK(strstr(drawn_text, "Search|Off|") != NULL);
	CHECK(strstr(drawn_text, "Finds games by typing part of a name.|") != NULL);
	CHECK(strstr(drawn_text, "A home screen.|") == NULL);
	press(MOSAIC_BUTTON_UP);
	draw();
	CHECK(strstr(drawn_text, "favorites|On|") != NULL); // no plugin.txt found: its id
	CHECK(strstr(drawn_text, "Keeps your favorites.|") != NULL);

	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing buttons, 6 of 7"));
	CHECK(strstr(drawn_text, "A|OPEN|Game list|Y|FAVORITE|Game list|Not on a button|First run|") != NULL);
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing done, 7 of 7"));
	CHECK_STR(hint_text, "R:DONE L:BACK ");
	CHECK(press(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_STAY);
	CHECK_STR(cfg_seen, "off");
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_CLOSE);
	CHECK(logged("finished"));
	CHECK_STR(cfg_seen, "on");
	registered.exit(registered.data);
	CHECK(logged("closed"));
	CHECK(renders_ok == 11);

	// the buttons page on its own, from its action: B closes it, and it's not seen for it
	reset();
	CHECK(p->init(&fake_host) == 0);
	buttons_run(NULL, NULL);
	CHECK(opens == 1);
	CHECK(logged("buttons opened"));
	draw();
	CHECK(logged("showing buttons, 1 of 1"));
	CHECK_STR(hint_text, "L:BACK ");
	CHECK(strstr(drawn_text, "of 1") == NULL);
	CHECK(strstr(drawn_text, "Buttons|") != NULL);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	CHECK_STR(cfg_seen, "off");

	// once seen it doesn't open by itself, but the action opens it, and Y skips
	reset();
	snprintf(cfg_seen, sizeof(cfg_seen), "on");
	pin = 1;
	CHECK(p->init(&fake_host) == 0);
	CHECK(logged("already seen"));
	p->tick(1);
	CHECK(opens == 0);
	action_run(NULL, NULL);
	CHECK(opens == 1);
	CHECK(logged("pin: set"));
	press(MOSAIC_BUTTON_A);
	press(MOSAIC_BUTTON_A);
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(strstr(drawn_text, "The launcher has a PIN.|") != NULL);
	CHECK(press(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_CLOSE);
	CHECK(logged("skipped"));

	// it waits for the home screen, not the quick menu or another screen
	reset();
	home = "home-dashboard.main";
	cfg_toggles = "off";
	cfg_pin = "off";
	cfg_suggest = "";
	cfg_setup = "off";
	cfg_buttons = "off";
	CHECK(p->init(&fake_host) == 0);
	snprintf(current, sizeof(current), "quickmenu");
	p->tick(1);
	snprintf(current, sizeof(current), "charging-screen.main");
	p->tick(2);
	CHECK(opens == 0);
	snprintf(current, sizeof(current), "home-dashboard.main");
	p->tick(3);
	CHECK(opens == 1);
	CHECK(logged("opened by itself over home-dashboard.main"));
	CHECK(logged("toggles: none"));
	CHECK(logged("setup: none"));
	CHECK(logged("buttons: none"));
	CHECK(!logged("pin: not set"));
	draw();
	CHECK(logged("showing welcome, 1 of 2"));
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing done, 2 of 2"));

	// a refused open is tried once a run
	reset();
	open_rc = -1;
	CHECK(p->init(&fake_host) == 0);
	p->tick(1);
	p->tick(2);
	CHECK(opens == 1);
	CHECK(logged("couldn't open by itself over gamelist"));

	// without screens or hints it doesn't load
	reset();
	fake_host.size = offsetof(MosaicHost, draw_hints);
	CHECK(p->init(&fake_host) != 0);
}

int main(void) {
	testPages();
	testInput();
	testShouldOpen();
	testHints();
	testIds();
	testValues();
	testSetup();
	testButtonLines();
	testWrap();
	testPlugin();
	CHECK_DONE();
}
