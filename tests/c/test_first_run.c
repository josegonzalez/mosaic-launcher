// The first run plugin: its model on its own, then the plugin against a
// fake host on a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "fakeloaded.h"
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
	FR_init(&w, 3, 1, 1);
	CHECK(w.page == FR_WELCOME);
	CHECK(FR_pageCount(&w) == 5);
	CHECK(FR_pageNumber(&w, FR_PIN) == 3);
	CHECK(FR_nextPage(&w, FR_WELCOME) == FR_TOGGLES);
	CHECK(FR_nextPage(&w, FR_DONE) == -1);
	CHECK(FR_prevPage(&w, FR_WELCOME) == -1);

	// pages left out are passed over both ways
	FR_init(&w, 0, 0, 1);
	CHECK(FR_pageCount(&w) == 3);
	CHECK(FR_nextPage(&w, FR_WELCOME) == FR_PLUGINS);
	CHECK(FR_prevPage(&w, FR_DONE) == FR_PLUGINS);
	CHECK(FR_pageNumber(&w, FR_DONE) == 3);
	FR_init(&w, 0, 0, 0);
	CHECK(FR_pageCount(&w) == 2);
	CHECK(FR_nextPage(&w, FR_WELCOME) == FR_DONE);

	// rows are capped
	FR_init(&w, 99, 0, 0);
	CHECK(w.rows == FR_TOGGLES_MAX);
	FR_init(&w, -2, 0, 0);
	CHECK(w.rows == 0 && !w.has[FR_TOGGLES]);
}

static void testInput(void) {
	FrWizard w;
	FR_init(&w, 2, 1, 1);
	// B does nothing on the first page, and the d-pad nothing off the toggles
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_B)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_RIGHT)) == FR_NOTHING);
	CHECK(FR_input(&w, 0) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_TOGGLES);

	// up and down wrap through the rows, left and right flip
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_UP)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 0);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_DOWN)) == FR_REDRAW && w.row == 1);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_LEFT)) == FR_FLIP);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_RIGHT)) == FR_FLIP);

	// A first when pressed together, and a page change starts at the top row
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A) | bit(MOSAIC_BUTTON_B)) == FR_REDRAW && w.page == FR_PIN && w.row == 0);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_B)) == FR_REDRAW && w.page == FR_TOGGLES);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_PIN);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_Y)) == FR_SKIP);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_PLUGINS);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_REDRAW && w.page == FR_DONE);
	// nothing left to skip on the last page, where A finishes
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_Y)) == FR_NOTHING);
	CHECK(FR_input(&w, bit(MOSAIC_BUTTON_A)) == FR_FINISH);
	CHECK(w.page == FR_DONE);
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
	FR_init(&w, 1, 0, 0);
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

	CHECK(FR_describe("favorites") != NULL);
	CHECK(FR_describe("switcher-plus") != NULL);
	CHECK(FR_describe("tags") == NULL);
	CHECK(FR_describe(NULL) == NULL);

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
static MosaicScreenHooks registered;
static int registered_count = 0;
static char action_id[64], tile_id[64];
static void (*action_run)(const char*, void*);
static int bound_button = -1;
static char all_log[16384];
static int redraws = 0, opens = 0, open_rc = 0, pin = 0, renders_ok = 0;
static char current[64] = "gamelist";
static const char* home = NULL;
static char cfg_seen[8] = "off";
static const char* cfg_toggles = "on";
static const char* cfg_pin = "on";
static const char* cfg_suggest = NULL;
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
	CHECK_STR(id, SCREEN_ID);
	registered = *hooks;
	registered_count++;
	return 0;
}

static int fakeAction(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)data;
	(void)available;
	(void)label;
	snprintf(action_id, sizeof(action_id), "%s", id);
	action_run = run;
	return 0;
}

static int fakeTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)row;
	(void)icon;
	(void)icon_off;
	(void)is_on;
	snprintf(tile_id, sizeof(tile_id), "%s", id);
	return 0;
}

static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen;
	(void)trigger;
	bound_button = button;
	return 0;
}

static int fakeOpenScreen(const char* id) {
	CHECK_STR(id, SCREEN_ID);
	opens++;
	if (open_rc != 0) return open_rc;
	snprintf(current, sizeof(current), "%s", id);
	return registered.enter ? registered.enter(registered.data) : 0;
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
}

static void reset(void) {
	all_log[0] = '\0';
	redraws = opens = open_rc = pin = registered_count = 0;
	snprintf(current, sizeof(current), "gamelist");
	home = NULL;
	snprintf(cfg_seen, sizeof(cfg_seen), "off");
	cfg_toggles = cfg_pin = "on";
	cfg_suggest = NULL;
	toggle_value[0] = 1;
	toggle_value[1] = 0;
	toggle_value[2] = 1;
	toggle_value[3] = -1;
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
	return registered.input(&in, registered.data);
}

static void draw(void) {
	MosaicScreenView view = { sizeof(MosaicScreenView), { 0, 0, 1024, 768 }, { 800, 10, 214, 90 }, { 0, 0, 0, 0 }, SCREEN_ID };
	hint_text[0] = '\0';
	drawn_text[0] = '\0';
	if (registered.render(&screen_surface, &view, registered.data) == 0) renders_ok++;
}

static void testPlugin(void) {
	mkdir(CARD, 0755);
	mkdir(CARD "/.userdata", 0755);
	mkdir(USERDATA, 0755);
	mkdir(USERDATA "/launcher", 0755);
	// search is on but isn't loaded, as when its init declined, so it shows off
	writeFile(USERDATA "/launcher/plugins.txt", "favorites=on\nsearch=on\n");
	fake_loaded = "favorites,first-run";

	reset();
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	CHECK(registered_count == 1);
	CHECK_STR(action_id, ACTION_ID);
	CHECK_STR(tile_id, ACTION_ID);
	CHECK(logged("not seen yet"));

	// opens itself on the first tick over the game list, once
	p->tick(1);
	CHECK(opens == 1);
	CHECK(logged("opened by itself over gamelist"));
	CHECK(logged("toggles: show_clock on, show_battery_percent off, haptics on"));
	CHECK(logged("pin: not set"));
	CHECK(logged("suggesting home-dashboard off, switcher-plus off, favorites on, search off"));
	p->tick(2);
	CHECK(opens == 1);

	draw();
	CHECK(logged("showing welcome, 1 of 5"));
	CHECK_STR(hint_text, "R:NEXT L:SKIP ");
	CHECK(strstr(drawn_text, "Welcome|") != NULL);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_STAY);
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK(logged("showing toggles, 2 of 5"));
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

	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing pin, 3 of 5"));
	CHECK(strstr(drawn_text, "The launcher has no PIN.|") != NULL);
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing plugins, 4 of 5"));
	CHECK(strstr(drawn_text, "favorites - a Favorites folder, Y adds a game|On|") != NULL);
	CHECK(strstr(drawn_text, "search - search every game by name|Off|") != NULL);
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logged("showing done, 5 of 5"));
	CHECK_STR(hint_text, "R:DONE L:BACK ");
	CHECK(press(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_STAY);
	CHECK_STR(cfg_seen, "off");
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_CLOSE);
	CHECK(logged("finished"));
	CHECK_STR(cfg_seen, "on");
	registered.exit(registered.data);
	CHECK(logged("closed"));
	CHECK(renders_ok == 6);

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
	testWrap();
	testPlugin();
	CHECK_DONE();
}
