// The guest profile plugin against a fake host, without the launcher: its
// model, its actions and tiles, switching and clearing, and the header mark.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/guest-profile/guest_model.c"
#include "../../plugins/guest-profile/guest_profile.c"

static char live[32] = "";
static int switch_fails = 0, clear_fails = 0;
static int switches = 0, clears = 0;
static char cleared_name[32];
static const char* settings[8][2];
static int setting_count = 0;
static char logged[4096];
static char toasted[128];

#define MAX_ACTIONS 4
static int action_count = 0;
static char action_id[MAX_ACTIONS][64];
static char action_label[MAX_ACTIONS][64];
static void (*action_run[MAX_ACTIONS])(const char*, void*);
static int tile_count = 0;
static char tile_action[MAX_ACTIONS][64];
static int (*tile_on[MAX_ACTIONS])(void*);
static int bound_count = 0, bound_button = -1;
static char bound_action[64];
static char asked[256], yes_label[32];
static void (*pending_yes)(void*) = NULL;
static int text_w = 50;

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

static const char* fakeProfileName(void) { return live; }

static int fakeProfileSwitch(const char* name) {
	switches++;
	if (switch_fails) return -1;
	snprintf(live, sizeof(live), "%s", name);
	return 0;
}

static int fakeProfileClear(const char* name) {
	clears++;
	snprintf(cleared_name, sizeof(cleared_name), "%s", name);
	return clear_fails ? -1 : 0;
}

static int fakeProfilePath(const char* file, char* out, size_t size) {
	snprintf(out, size, "/launcher/%s", file);
	return 0;
}

static int fakeRegisterAction(const char* id, const char* label, int flags, int (*available)(const char*, void*),
		void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)available;
	(void)data;
	if (action_count >= MAX_ACTIONS) return -1;
	snprintf(action_id[action_count], sizeof(action_id[0]), "%s", id);
	snprintf(action_label[action_count], sizeof(action_label[0]), "%s", label);
	action_run[action_count] = run;
	action_count++;
	return 0;
}

static int fakeQuickTile(const char* action, int row, const char* icon, const char* icon_off, int (*on)(void*)) {
	(void)icon;
	(void)icon_off;
	if (row != MOSAIC_QUICK_TOP || tile_count >= MAX_ACTIONS) return -1;
	snprintf(tile_action[tile_count], sizeof(tile_action[0]), "%s", action);
	tile_on[tile_count] = on;
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

static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)data;
	snprintf(asked, sizeof(asked), "%s", message);
	snprintf(yes_label, sizeof(yes_label), "%s", yes);
	pending_yes = on_yes;
}

static void fakeToast(const char* message) { snprintf(toasted, sizeof(toasted), "%s", message); }
static void fakeRedraw(void) {}
static int fakeScale(void) { return 2; }
static int fakeTextWidth(int font, const char* text) {
	(void)font;
	(void)text;
	return text_w;
}
static int fakeTextHeight(int font) {
	(void)font;
	return 20;
}
static int fakeThemeColor(int index) {
	(void)index;
	return 0x123456;
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) {
	(void)s;
	(void)i;
	(void)r;
	(void)n;
	return 0;
}
static int fills = 0, draws = 0;
static int fakeFillRoundRect(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s;
	(void)r;
	(void)radius;
	(void)c;
	fills++;
	return 0;
}
static int fakeDrawText(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max) {
	(void)s;
	(void)font;
	(void)x;
	(void)y;
	(void)c;
	(void)max;
	CHECK_STR(text, "GUEST");
	draws++;
	return 0;
}

static MosaicHost fake;

static void reset(void) {
	memset(&fake, 0, sizeof(fake));
	fake.size = sizeof(fake);
	fake.log = fakeLog;
	fake.cfg_get = fakeCfgGet;
	fake.register_action = fakeRegisterAction;
	fake.bind_button = fakeBindButton;
	fake.request_redraw = fakeRedraw;
	fake.toast = fakeToast;
	fake.confirm = fakeConfirm;
	fake.quick_tile = fakeQuickTile;
	fake.screen_scale = fakeScale;
	fake.text_width = fakeTextWidth;
	fake.text_height = fakeTextHeight;
	fake.theme_color = fakeThemeColor;
	fake.decorate_cell = fakeDecorateCell;
	fake.fill_round_rect = fakeFillRoundRect;
	fake.draw_text = fakeDrawText;
	fake.profile_name = fakeProfileName;
	fake.profile_switch = fakeProfileSwitch;
	fake.profile_clear = fakeProfileClear;
	fake.profile_path = fakeProfilePath;
	live[0] = '\0';
	logged[0] = toasted[0] = asked[0] = '\0';
	setting_count = action_count = tile_count = bound_count = 0;
	switches = clears = switch_fails = clear_fails = 0;
	bound_button = -1;
	pending_yes = NULL;
	text_w = 50;
	mark_logged = 0;
}

static int run(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(action_id[i], id)) {
			action_run[i](NULL, NULL);
			return 1;
		}
	}
	return 0;
}

int main(void) {
	// the model
	CHECK(GP_button("L2") == MOSAIC_BUTTON_L2);
	CHECK(GP_button("R2") == MOSAIC_BUTTON_R2);
	CHECK(GP_button("SELECT") == MOSAIC_BUTTON_SELECT);
	CHECK(GP_button("none") == -1);
	CHECK(GP_button("Y") == -1);
	CHECK(GP_button(NULL) == -1);
	CHECK(GP_isGuest("guest"));
	CHECK(!GP_isGuest(""));
	CHECK(!GP_isGuest("kid"));
	CHECK(!GP_isGuest(NULL));
	CHECK(GP_markWidth(50, 24, 200) == 98);
	CHECK(GP_markWidth(50, 24, 98) == 98);
	CHECK(GP_markWidth(50, 24, 97) == 0);
	CHECK(GP_markWidth(0, 24, 200) == 0);
	CHECK(GP_markWidth(-1, 24, 200) == 0);

	// without profiles it declines
	reset();
	fake.size = offsetof(MosaicHost, profile_name);
	CHECK(plugin.init(&fake) == 1);
	CHECK(strstr(logged, "unsupported: this launcher has no profiles") != NULL);

	// the defaults: both tiles, no button, the owner's live
	reset();
	CHECK(plugin.init(&fake) == 0);
	CHECK(action_count == 2);
	CHECK_STR(action_id[0], "guest-profile.toggle");
	CHECK_STR(action_label[0], "Guest");
	CHECK_STR(action_id[1], "guest-profile.clear");
	CHECK_STR(action_label[1], "Clear guest");
	CHECK(tile_count == 2);
	CHECK_STR(tile_action[0], "guest-profile.toggle");
	CHECK(tile_on[0] != NULL);
	CHECK_STR(tile_action[1], "guest-profile.clear");
	CHECK(tile_on[1] == NULL);
	CHECK(bound_count == 0);
	CHECK(strstr(logged, "the owner's profile is live") != NULL);
	CHECK(!tile_on[0](NULL));
	CHECK(plugin.header_width(400, 30) == 0);

	// switching on, with the mark
	CHECK(run("guest-profile.toggle"));
	CHECK(switches == 1);
	CHECK_STR(live, "guest");
	CHECK_STR(toasted, "Guest profile on");
	CHECK(strstr(logged, "guest on") != NULL);
	CHECK(tile_on[0](NULL));
	CHECK(plugin.header_width(400, 30) == 50 + 2 * 24);
	CHECK(strstr(logged, "mark 98 wide") != NULL);
	MosaicRect rect = { 0, 0, 98, 30 };
	CHECK(plugin.header_widget(NULL, &rect) == 0);
	CHECK(fills == 1 && draws == 1);
	// no room: no mark, never a cut one, and said so
	CHECK(plugin.header_width(97, 30) == 0);
	CHECK(strstr(logged, "no room for the mark in 97") != NULL);

	// and off again, with nothing asked
	CHECK(run("guest-profile.toggle"));
	CHECK(switches == 2);
	CHECK_STR(live, "");
	CHECK_STR(toasted, "Guest profile off");
	CHECK(asked[0] == '\0');
	CHECK(plugin.header_width(400, 30) == 0);

	// a switch the launcher refuses
	switch_fails = 1;
	CHECK(run("guest-profile.toggle"));
	CHECK_STR(live, "");
	CHECK_STR(toasted, "Couldn't switch profiles");
	CHECK(strstr(logged, "couldn't switch to the guest") != NULL);
	switch_fails = 0;

	// clearing asks first
	CHECK(run("guest-profile.clear"));
	CHECK(clears == 0);
	CHECK(strstr(asked, "Clear the guest's recents, favorites,\ncollections and play history?") != NULL);
	CHECK_STR(yes_label, "CLEAR");
	CHECK(pending_yes != NULL);
	pending_yes(NULL);
	CHECK(clears == 1);
	CHECK_STR(cleared_name, "guest");
	CHECK_STR(toasted, "Guest data cleared");
	clear_fails = 1;
	pending_yes(NULL);
	CHECK_STR(toasted, "Couldn't clear guest data");

	// settings: no tiles, no mark, a button
	reset();
	set("tile", "off");
	set("clear_tile", "off");
	set("mark", "off");
	set("button", "R2");
	snprintf(live, sizeof(live), "guest");
	CHECK(plugin.init(&fake) == 0);
	CHECK(tile_count == 0);
	CHECK(action_count == 2);
	CHECK(bound_count == 1);
	CHECK(bound_button == MOSAIC_BUTTON_R2);
	CHECK_STR(bound_action, "guest-profile.toggle");
	CHECK(strstr(logged, "the guest's profile is live") != NULL);
	CHECK(plugin.header_width(400, 30) == 0);

	// without header widgets: no mark, said so
	reset();
	fake.decorate_cell = NULL;
	CHECK(plugin.init(&fake) == 0);
	CHECK(strstr(logged, "no header widgets, so there's no guest mark") != NULL);
	snprintf(live, sizeof(live), "guest");
	CHECK(plugin.header_width(400, 30) == 0);

	CHECK_DONE();
}
