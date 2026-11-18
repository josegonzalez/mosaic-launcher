// The bug report plugin against a fake host, without the launcher: its
// model, its action, tile and button, the wait screen, and the toast.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/bug-report/bug_report_model.c"
#include "../../plugins/bug-report/bug_report.c"

static const char* settings[8][2];
static int setting_count = 0;
static char logged[4096];
static char toasted[256];
static int report_rc = 0, report_calls = 0, report_flags = -1;
static int open_rc = 0, opens = 0, closes = 0, index_asks = 0;
static char action_id[64], action_label[64], tile_action[64], bound_action[64];
static void (*action_run)(const char*, void*) = NULL;
static int tile_count = 0, bound_count = 0, bound_button = -1;
static MosaicScreenHooks screen_hooks;
static char screen_id[64];
static int has_bug_report = 1;

static void fakeLog(const char* id, const char* message) {
	(void)id;
	size_t used = strlen(logged);
	snprintf(logged + used, sizeof(logged) - used, "%s\n", message);
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	for (int i = 0; i < setting_count; i++) {
		if (!strcmp(settings[i][0], key)) return settings[i][1];
	}
	return fallback;
}

static void set(const char* key, const char* value) {
	settings[setting_count][0] = key;
	settings[setting_count][1] = value;
	setting_count++;
}

static int fakeBugReport(int flags, char* out, size_t size) {
	report_calls++;
	report_flags = flags;
	if (report_rc == 0) snprintf(out, size, "Reports/mosaic-2026-11-17-153012.zip");
	return report_rc;
}

static int fakeRegisterAction(const char* id, const char* label, int flags, int (*available)(const char*, void*),
		void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)available;
	(void)data;
	snprintf(action_id, sizeof(action_id), "%s", id);
	snprintf(action_label, sizeof(action_label), "%s", label);
	action_run = run;
	return 0;
}

static int fakeQuickTile(const char* action, int row, const char* icon, const char* icon_off, int (*on)(void*)) {
	(void)icon;
	(void)icon_off;
	(void)on;
	if (row != MOSAIC_QUICK_TOP) return -1;
	snprintf(tile_action, sizeof(tile_action), "%s", action);
	tile_count++;
	return 0;
}

static int fakeBindButton(int screen, int button, int trigger, const char* action) {
	(void)screen;
	(void)trigger;
	bound_count++;
	bound_button = button;
	snprintf(bound_action, sizeof(bound_action), "%s", action);
	return 0;
}

static int fakeRegisterScreen(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(screen_id, sizeof(screen_id), "%s", id);
	screen_hooks = *hooks;
	return 0;
}

static int fakeOpenScreen(const char* id) {
	opens++;
	if (open_rc != 0 || strcmp(id, screen_id) != 0) return -1;
	return screen_hooks.enter(NULL);
}

static int fakeCloseScreen(const char* id) {
	closes++;
	if (strcmp(id, screen_id) != 0) return -1;
	screen_hooks.exit(NULL);
	return 0;
}

static int fakeIndexCount(const char* system) {
	(void)system;
	index_asks++;
	return MOSAIC_INDEX_NOT_READY;
}

static void fakeToast(const char* message) { snprintf(toasted, sizeof(toasted), "%s", message); }
static int fakeScale(void) { return 2; }
static int fakeThemeColor(int index) {
	(void)index;
	return 0x112233;
}
static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)target;
	(void)rect;
	(void)color;
	return 0;
}
static int fakeTextHeight(int font) {
	(void)font;
	return 20;
}
static char drawn_text[4][128];
static int drawn_count = 0;
static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	(void)font;
	(void)max_width;
	snprintf(out, size, "%s", text);
	return 100;
}
static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)target;
	(void)font;
	(void)x;
	(void)y;
	(void)color;
	(void)max_width;
	if (drawn_count < 4) snprintf(drawn_text[drawn_count++], sizeof(drawn_text[0]), "%s", text);
	return 100;
}

static MosaicHost fake;

static void reset(void) {
	setting_count = 0;
	logged[0] = toasted[0] = '\0';
	report_rc = report_calls = 0;
	report_flags = -1;
	open_rc = opens = closes = index_asks = 0;
	action_id[0] = tile_action[0] = bound_action[0] = '\0';
	action_run = NULL;
	tile_count = bound_count = 0;
	bound_button = -1;
	drawn_count = 0;
	memset(&fake, 0, sizeof(fake));
	fake.size = sizeof(fake);
	fake.log = fakeLog;
	fake.cfg_get = fakeCfgGet;
	fake.register_action = fakeRegisterAction;
	fake.bind_button = fakeBindButton;
	fake.quick_tile = fakeQuickTile;
	fake.toast = fakeToast;
	fake.register_screen = fakeRegisterScreen;
	fake.open_screen = fakeOpenScreen;
	fake.close_screen = fakeCloseScreen;
	fake.screen_scale = fakeScale;
	fake.theme_color = fakeThemeColor;
	fake.fill_rect = fakeFill;
	fake.text_height = fakeTextHeight;
	fake.text_truncate = fakeTruncate;
	fake.draw_text = fakeDrawText;
	fake.index_count = fakeIndexCount;
	if (has_bug_report) fake.bug_report = fakeBugReport;
}

static void testModel(void) {
	CHECK(BR_button("Y") == MOSAIC_BUTTON_Y);
	CHECK(BR_button("L2") == MOSAIC_BUTTON_L2);
	CHECK(BR_button("R2") == MOSAIC_BUTTON_R2);
	CHECK(BR_button("SELECT") == MOSAIC_BUTTON_SELECT);
	CHECK(BR_button("none") == -1);
	CHECK(BR_button("A") == -1);
	CHECK(BR_button(NULL) == -1);
	CHECK(BR_flags("on") == MOSAIC_REPORT_KEEP_NETWORK);
	CHECK(BR_flags("off") == 0);
	CHECK(BR_flags(NULL) == 0);
	char out[128];
	BR_toast(0, "Reports/mosaic-1.zip", "Saved %s", "Couldn't", out, sizeof(out));
	CHECK_STR(out, "Saved mosaic-1.zip");
	BR_toast(0, "mosaic-1.zip", "Saved %s", "Couldn't", out, sizeof(out));
	CHECK_STR(out, "Saved mosaic-1.zip");
	BR_toast(-1, "Reports/mosaic-1.zip", "Saved %s", "Couldn't", out, sizeof(out));
	CHECK_STR(out, "Couldn't");
	BR_toast(0, "", "Saved %s", "Couldn't", out, sizeof(out));
	CHECK_STR(out, "Couldn't");
}

static void render(void) {
	MosaicScreenView view;
	memset(&view, 0, sizeof(view));
	view.area.w = 640;
	view.area.h = 480;
	CHECK(screen_hooks.render(NULL, &view, NULL) == 0);
}

static void testDefaults(void) {
	reset();
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(action_id, "bug-report.make");
	CHECK_STR(action_label, "Bug report");
	CHECK(tile_count == 1);
	CHECK_STR(tile_action, "bug-report.make");
	CHECK(bound_count == 0);
	CHECK(index_asks == 1);
	CHECK(strstr(logged, "ready\n") != NULL);

	// the action opens the wait screen; the report waits until it's drawn
	action_run(NULL, NULL);
	CHECK(opens == 1);
	plugin.tick(10);
	CHECK(report_calls == 0);
	render();
	CHECK(drawn_count == 2);
	CHECK_STR(drawn_text[0], "Making a bug report...");
	CHECK_STR(drawn_text[1], "It goes in Reports on the card");
	// pressing again while it waits does nothing
	action_run(NULL, NULL);
	CHECK(opens == 1);
	CHECK(screen_hooks.input(NULL, NULL) == MOSAIC_SCREEN_STAY);
	plugin.tick(20);
	CHECK(report_calls == 1);
	CHECK(report_flags == 0);
	CHECK(closes == 1);
	CHECK_STR(toasted, "Saved mosaic-2026-11-17-153012.zip");
	CHECK(strstr(logged, "saved Reports/mosaic-2026-11-17-153012.zip\n") != NULL);
	// done: the next tick makes nothing
	plugin.tick(30);
	CHECK(report_calls == 1);
}

static void testSettings(void) {
	reset();
	set("tile", "off");
	set("button", "L2");
	set("keep_network", "on");
	CHECK(plugin.init(&fake) == 0);
	CHECK(tile_count == 0);
	CHECK(bound_count == 1 && bound_button == MOSAIC_BUTTON_L2);
	CHECK_STR(bound_action, "bug-report.make");
	CHECK(strstr(logged, "ready, keeping network details") != NULL);
	action_run(NULL, NULL);
	render();
	plugin.tick(1);
	CHECK(report_flags == MOSAIC_REPORT_KEEP_NETWORK);
}

static void testFailures(void) {
	// the report fails
	reset();
	report_rc = -1;
	CHECK(plugin.init(&fake) == 0);
	action_run(NULL, NULL);
	render();
	plugin.tick(1);
	CHECK_STR(toasted, "Couldn't make a bug report");
	CHECK(strstr(logged, "couldn't make a bug report") != NULL);
	CHECK(closes == 1);

	// no wait screen, as from a dialog: made at once
	reset();
	open_rc = -1;
	CHECK(plugin.init(&fake) == 0);
	action_run(NULL, NULL);
	CHECK(report_calls == 1);
	CHECK_STR(toasted, "Saved mosaic-2026-11-17-153012.zip");
	CHECK(strstr(logged, "couldn't open the wait screen") != NULL);

	// a launcher without bug reports
	has_bug_report = 0;
	reset();
	CHECK(plugin.init(&fake) == 1);
	CHECK(strstr(logged, "unsupported") != NULL);
	has_bug_report = 1;
}

int main(void) {
	testModel();
	testDefaults();
	testSettings();
	testFailures();
	CHECK_DONE();
}
