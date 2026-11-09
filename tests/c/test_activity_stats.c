// The activity stats plugin: its model on its own, then the plugin against
// a fake host with a fixture play history.

#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "../../plugins/activity-stats/activity_stats.c"

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms/Stats"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

///////////////////////////////////////
// the model

static void testName(void) {
	char out[64];
	AS_name("Tetris", "/card/Roms/GB/Other.gb", out, sizeof(out));
	CHECK_STR(out, "Tetris");
	AS_name("", "/card/Roms/GB/Tetris (USA).gb", out, sizeof(out));
	CHECK_STR(out, "Tetris (USA)");
	AS_name(NULL, "/card/Roms/GB/Disc.v1.m3u", out, sizeof(out));
	CHECK_STR(out, "Disc.v1");
	AS_name("", "/card/Roms/GB/.hidden", out, sizeof(out));
	CHECK_STR(out, ".hidden");
	AS_name("", "", out, sizeof(out));
	CHECK_STR(out, "Unknown game");
	AS_name(NULL, NULL, out, sizeof(out));
	CHECK_STR(out, "Unknown game");
	AS_name("", "/card/Roms/GB/", out, sizeof(out));
	CHECK_STR(out, "Unknown game");
	char small[4];
	AS_name("", "/x/Longer.gb", small, sizeof(small));
	CHECK_STR(small, "Lon");
}

static void testRank(void) {
	ASGame games[5];
	memset(games, 0, sizeof(games));
	// seconds, last, count
	games[0] = (ASGame){ "", "A", 100, 10, 9 };
	games[1] = (ASGame){ "", "B", 500, 20, 1 };
	games[2] = (ASGame){ "", "C", 100, 30, 9 };
	games[3] = (ASGame){ "", "D", 100, 5, 3 };
	games[4] = (ASGame){ "", "E", 500, 20, 1 };
	int order[5];
	AS_rankPlayed(games, 5, order);
	// B and E tie on everything, so the first listed goes first; then C over
	// A by the latest played, then D with fewer sessions
	CHECK(order[0] == 1 && order[1] == 4 && order[2] == 2 && order[3] == 0 && order[4] == 3);
	AS_rankLaunched(games, 5, order);
	CHECK(order[0] == 2 && order[1] == 0 && order[2] == 3 && order[3] == 1 && order[4] == 4);
	AS_rankPlayed(games, 0, order); // nothing to do

	long long seconds = 0, sessions = 0;
	games[3].seconds = -1;
	games[3].count = -1;
	AS_totals(games, 5, &seconds, &sessions);
	CHECK(seconds == 1200);
	CHECK(sessions == 20);
	AS_totals(games, 0, &seconds, &sessions);
	CHECK(seconds == 0 && sessions == 0);
}

static void testFormat(void) {
	char out[64];
	AS_duration(-1, out, sizeof(out));
	CHECK_STR(out, "");
	AS_duration(0, out, sizeof(out));
	CHECK_STR(out, "0m");
	AS_duration(30, out, sizeof(out));
	CHECK_STR(out, "<1m");
	AS_duration(60, out, sizeof(out));
	CHECK_STR(out, "1m");
	AS_duration(2700, out, sizeof(out));
	CHECK_STR(out, "45m");
	AS_duration(7200, out, sizeof(out));
	CHECK_STR(out, "2h");
	AS_duration(7500, out, sizeof(out));
	CHECK_STR(out, "2h 5m");
	AS_duration(360059, out, sizeof(out));
	CHECK_STR(out, "100h");

	AS_sessions(1, out, sizeof(out));
	CHECK_STR(out, "1 session");
	AS_sessions(0, out, sizeof(out));
	CHECK_STR(out, "0 sessions");
	AS_sessions(4, out, sizeof(out));
	CHECK_STR(out, "4 sessions");

	ASGame game = { "", "Zelda", 8100, 0, 2 };
	AS_rowRight(&game, 0, out, sizeof(out));
	CHECK_STR(out, "2h 15m \xc2\xb7 2 sessions");
	AS_rowRight(&game, 1, out, sizeof(out));
	CHECK_STR(out, "2 sessions \xc2\xb7 2h 15m");
	game.count = 1;
	game.seconds = 20;
	AS_rowRight(&game, 0, out, sizeof(out));
	CHECK_STR(out, "<1m \xc2\xb7 1 session");

	AS_listCaption(3, 9000, 5, out, sizeof(out));
	CHECK_STR(out, "3 games \xc2\xb7 2h 30m \xc2\xb7 5 sessions");
	AS_listCaption(1, 60, 1, out, sizeof(out));
	CHECK_STR(out, "1 game \xc2\xb7 1m \xc2\xb7 1 session");

	long long week[7] = { 900, 1600, 7200, 50, 300, 0, 0 };
	AS_chartCaption(week, 7, out, sizeof(out));
	CHECK_STR(out, "7 days: 2h 47m \xc2\xb7 23m a day");
	// unknown days are left out of the average
	long long some[3] = { 3600, -1, -1 };
	AS_chartCaption(some, 3, out, sizeof(out));
	CHECK_STR(out, "3 days: 1h \xc2\xb7 1h a day");
	long long none[2] = { -1, -1 };
	AS_chartCaption(none, 2, out, sizeof(out));
	CHECK_STR(out, "2 days: no play history");
}

static time_t localTime(int year, int month, int day, int hour, int minute) {
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon = month - 1;
	tm.tm_mday = day;
	tm.tm_hour = hour;
	tm.tm_min = minute;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

static void testDays(void) {
	setenv("TZ", "UTC0", 1);
	tzset();
	time_t now = localTime(2026, 11, 9, 12, 0); // a Monday
	char out[16];
	AS_dayLabel(now, 7, 0, out, sizeof(out));
	CHECK_STR(out, "Mon");
	AS_dayLabel(now, 7, 1, out, sizeof(out));
	CHECK_STR(out, "Sun");
	AS_dayLabel(now, 7, 6, out, sizeof(out));
	CHECK_STR(out, "Tue");
	AS_dayLabel(now, 7, 7, out, sizeof(out));
	CHECK_STR(out, "");
	AS_dayLabel(now, 7, -1, out, sizeof(out));
	CHECK_STR(out, "");
	AS_dayLabel(now, 30, 0, out, sizeof(out));
	CHECK_STR(out, "9");
	AS_dayLabel(now, 30, 3, out, sizeof(out));
	CHECK_STR(out, "");
	AS_dayLabel(now, 30, 7, out, sizeof(out));
	CHECK_STR(out, "2");
	AS_dayLabel(now, 30, 14, out, sizeof(out));
	CHECK_STR(out, "26");
	AS_dayLabel(now, 30, 28, out, sizeof(out));
	CHECK_STR(out, "12");
	AS_dayLabel(now, 30, 30, out, sizeof(out));
	CHECK_STR(out, "");

	// just after a 25 hour day, early in the morning
	setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
	tzset();
	now = localTime(2026, 11, 2, 0, 30);
	struct tm tm;
	AS_dayOf(now, 0, &tm);
	CHECK(tm.tm_mday == 2 && tm.tm_mon == 10 && tm.tm_hour == 12);
	AS_dayOf(now, 1, &tm);
	CHECK(tm.tm_mday == 1 && tm.tm_mon == 10);
	AS_dayOf(now, 2, &tm);
	CHECK(tm.tm_mday == 31 && tm.tm_mon == 9);
	// and late in the evening of a 23 hour one
	now = localTime(2026, 3, 8, 23, 50);
	AS_dayOf(now, 1, &tm);
	CHECK(tm.tm_mday == 7 && tm.tm_mon == 2);
	unsetenv("TZ");
	tzset();
}

static void testChart(void) {
	long long zeros[3] = { 0, 0, -1 };
	CHECK(AS_axisMax(zeros, 3) == 3600);
	CHECK(AS_axisMax(zeros, 0) == 3600);
	long long a[2] = { 100, 0 };
	CHECK(AS_axisMax(a, 2) == 900);
	long long b[1] = { 900 };
	CHECK(AS_axisMax(b, 1) == 900);
	long long c[2] = { 0, 901 };
	CHECK(AS_axisMax(c, 2) == 1800);
	long long d[3] = { 5000, -1, 20 };
	CHECK(AS_axisMax(d, 3) == 7200);
	long long e[1] = { 50000 };
	CHECK(AS_axisMax(e, 1) == 86400);
	long long f[1] = { 90000 };
	CHECK(AS_axisMax(f, 1) == 172800);

	CHECK(AS_barHeight(0, 3600, 100) == 0);
	CHECK(AS_barHeight(-1, 3600, 100) == 0);
	CHECK(AS_barHeight(1, 3600, 100) == 1);
	CHECK(AS_barHeight(1800, 3600, 100) == 50);
	CHECK(AS_barHeight(3600, 3600, 100) == 100);
	CHECK(AS_barHeight(7200, 3600, 100) == 100);
	CHECK(AS_barHeight(100, 0, 100) == 0);
	CHECK(AS_barHeight(100, 3600, 0) == 0);

	MosaicRect plot = { 10, 20, 70, 100 };
	// the oldest on the left, today on the right
	CHECK(rectIs(AS_slot(&plot, 7, 6), 10, 20, 10, 100));
	CHECK(rectIs(AS_slot(&plot, 7, 0), 70, 20, 10, 100));
	CHECK(AS_slot(&plot, 7, 7).w == 0);
	CHECK(AS_slot(&plot, 7, -1).w == 0);
	CHECK(rectIs(AS_bar(&plot, 7, 0, 1800, 3600), 72, 70, 6, 50));
	CHECK(rectIs(AS_bar(&plot, 7, 6, 3600, 3600), 12, 20, 6, 100));
	CHECK(AS_bar(&plot, 7, 3, 0, 3600).w == 0);
	CHECK(AS_bar(&plot, 7, 3, -1, 3600).w == 0);
	// thirty slots in 70: uneven, but they cover the plot with no gaps
	int x = plot.x;
	for (int i = 29; i >= 0; i--) {
		MosaicRect slot = AS_slot(&plot, 30, i);
		CHECK(slot.x == x);
		CHECK(slot.w >= 2 && slot.w <= 3);
		CHECK(AS_bar(&plot, 30, i, 60, 3600).w >= 1);
		x += slot.w;
	}
	CHECK(x == plot.x + plot.w);
	MosaicRect none = { 0, 0, 0, 0 };
	CHECK(AS_bar(&none, 7, 0, 60, 3600).w == 0);
}

static void testTabs(void) {
	CHECK_STR(AS_tabLabel(AS_TAB_WEEK), "7 days");
	CHECK_STR(AS_tabLabel(AS_TAB_MONTH), "30 days");
	CHECK_STR(AS_tabLabel(AS_TAB_GAMES), "Games");
	CHECK_STR(AS_tabLabel(AS_TAB_LAUNCHES), "Launches");
	CHECK_STR(AS_tabLabel(AS_TABS), "");
	CHECK(!AS_isList(AS_TAB_WEEK) && !AS_isList(AS_TAB_MONTH));
	CHECK(AS_isList(AS_TAB_GAMES) && AS_isList(AS_TAB_LAUNCHES));
	CHECK(AS_tabStep(AS_TAB_WEEK, 1) == AS_TAB_MONTH);
	CHECK(AS_tabStep(AS_TAB_LAUNCHES, 1) == AS_TAB_WEEK);
	CHECK(AS_tabStep(AS_TAB_WEEK, -1) == AS_TAB_LAUNCHES);

	CHECK(AS_step(0, 5, 1, 1) == 1);
	CHECK(AS_step(4, 5, 1, 1) == 0);
	CHECK(AS_step(4, 5, 1, 0) == 4);
	CHECK(AS_step(0, 5, -1, 1) == 4);
	CHECK(AS_step(0, 5, -1, 0) == 0);
	CHECK(AS_step(9, 5, -1, 0) == 3);
	CHECK(AS_step(0, 0, 1, 1) == 0);
	CHECK(AS_step(0, 1, 1, 1) == 0);

	CHECK(AS_window(0, 10, 4, 0) == 0);
	CHECK(AS_window(3, 10, 4, 0) == 0);
	CHECK(AS_window(4, 10, 4, 0) == 1);
	CHECK(AS_window(2, 10, 4, 1) == 1);
	CHECK(AS_window(0, 10, 4, 5) == 0);
	CHECK(AS_window(9, 10, 4, 0) == 6);
	CHECK(AS_window(2, 3, 4, 2) == 0);
	CHECK(AS_window(0, 0, 4, 2) == 0);

	MosaicHint left[1], right[1];
	int lc = -1, rc = -1;
	AS_hints(1, left, &lc, right, &rc);
	CHECK(lc == 1 && left[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(left[0].label, "BACK");
	CHECK(rc == 1 && right[0].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "PLAY");
	AS_hints(0, left, &lc, right, &rc);
	CHECK(lc == 1 && rc == 0);
}

static void testLayout(void) {
	MosaicRect area = { 0, 0, 640, 480 };
	MosaicRect status = { 500, 10, 130, 30 }, header = { 440, 10, 50, 30 }, none = { 0, 0, 0, 0 };
	CHECK(rectIs(AS_hole(&status, &header), 440, 10, 190, 30));
	CHECK(rectIs(AS_hole(&status, &none), 500, 10, 130, 30));
	CHECK(rectIs(AS_hole(NULL, &header), 440, 10, 50, 30));
	CHECK(AS_hole(NULL, NULL).w == 0);

	MosaicRect parts[4];
	MosaicRect hole = AS_hole(&status, &header);
	CHECK(AS_avoid(&area, &hole, parts) == 4);
	CHECK(rectIs(parts[0], 0, 0, 640, 10));
	CHECK(rectIs(parts[1], 0, 10, 440, 30));
	CHECK(rectIs(parts[2], 630, 10, 10, 30));
	CHECK(rectIs(parts[3], 0, 40, 640, 440));
	CHECK(AS_avoid(&area, &none, parts) == 1 && rectIs(parts[0], 0, 0, 640, 480));
	CHECK(AS_avoid(&none, &hole, parts) == 0);

	CHECK(rectIs(AS_tabRoom(&area, &hole, 1), 10, 10, 420, 30));
	CHECK(rectIs(AS_tabRoom(&area, &none, 1), 10, 10, 620, 30));
	CHECK(rectIs(AS_tabRoom(&area, &none, 2), 20, 20, 600, 60));
	MosaicRect wide = { 0, 0, 640, 480 }, full = { 15, 0, 620, 40 };
	CHECK(AS_tabRoom(&wide, &full, 1).w == 0);
	CHECK(AS_tabRoom(&area, &hole, 0).w == 0);

	int widths[4] = { 100, 100, 100, 100 };
	CHECK(AS_tabsFit(widths, 4, 20, 420));
	CHECK(!AS_tabsFit(widths, 4, 21, 420));

	CHECK(rectIs(AS_room(&area, &hole, 1), 10, 50, 620, 380));
	MosaicRect tall = { 400, 10, 200, 80 };
	CHECK(rectIs(AS_room(&area, &tall, 1), 10, 100, 620, 330));
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(AS_room(&tiny, &none, 1).w == 0);

	MosaicRect room = AS_room(&area, &hole, 1);
	// caption 12 and a PADDING; axis 20 and half a PADDING; labels 8 and half a PADDING
	CHECK(rectIs(AS_plot(&room, 1, 12, 20, 8), 35, 72, 595, 345));
	CHECK(AS_plot(&room, 1, 400, 20, 8).w == 0);
	CHECK(AS_plot(&room, 0, 12, 20, 8).w == 0);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
};

static MosaicSurface screen_surface = { 1024, 768 };
static const MosaicRect AREA = { 0, 0, 1024, 768 };
static const MosaicRect STATUS = { 800, 10, 214, 30 };

static MosaicHost fake_host;
static MosaicScreenHooks registered;
static char registered_id[64];
static char action_id[64], action_label[64], tile_id[64];
static int (*action_available)(void*);
static void (*action_run)(void*);
static int bound_button = -1;
static char last_log[1200];
static char opened_screen[64];
static char opened_path[600];
static int opened_type = -1, open_answer = 1;
static int redraws = 0;
static MosaicHint hints_right[2];
static int hints_right_count = 0, hints_left_count = 0;
static char toast_text[300];
static const char* tile_setting = "on";
static const char* button_setting = "none";
static int fills_into_hole = 0, texts_into_hole = 0;

typedef struct {
	const char* path;
	const char* name;
	long long seconds, first, last;
	int count, orphan;
} Fixture;
static const Fixture* fixture = NULL;
static int fixture_count = -1;
static long long days_played[AS_DAYS + 1];

static int meetsHole(const MosaicRect* r) {
	return r->x < STATUS.x + STATUS.w && STATUS.x < r->x + r->w && r->y < STATUS.y + STATUS.h && STATUS.y < r->y + r->h;
}

static void fakeLog(const char* id, const char* message) {
	(void)id;
	snprintf(last_log, sizeof(last_log), "%s", message);
}

static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "tile")) return tile_setting;
	if (!strcmp(key, "button")) return button_setting;
	return fallback;
}

static void fakeRedraw(void) {
	redraws++;
}

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(registered_id, sizeof(registered_id), "%s", id);
	registered = *hooks;
	return 0;
}

static int fakeAction(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)data;
	snprintf(action_id, sizeof(action_id), "%s", id);
	snprintf(action_label, sizeof(action_label), "%s", label);
	action_available = available;
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
	(void)id;
	bound_button = button;
	return 0;
}

static int fakeOpenScreen(const char* id) {
	snprintf(opened_screen, sizeof(opened_screen), "%s", id);
	return 0;
}

static int fakeOpenPath(const char* path, int type) {
	snprintf(opened_path, sizeof(opened_path), "%s", path);
	opened_type = type;
	return open_answer;
}

static void fakeToast(const char* message) {
	snprintf(toast_text, sizeof(toast_text), "%s", message);
}

static int fakeThemeColor(int index) {
	return index == MOSAIC_COLOR_BACKGROUND ? 0x000000 : 0xFFFFFF;
}

static int fakeScale(void) {
	return 3;
}

static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)target;
	(void)color;
	if (meetsHole(rect)) fills_into_hole++;
	return 0;
}

static int fakeFillRound(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	(void)radius;
	return fakeFill(target, rect, color);
}

static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return 20 * (int)strlen(text);
}

static int fakeTextHeight(int font) {
	return font == MOSAIC_FONT_LARGE ? 48 : font == MOSAIC_FONT_SMALL ? 36 : 24;
}

static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	snprintf(out, size, "%s", text);
	int w = fakeTextWidth(font, out);
	return w > max_width ? max_width : w;
}

static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)target;
	(void)color;
	int w = fakeTextWidth(font, text);
	if (w > max_width) w = max_width;
	MosaicRect r = { x, y, w, fakeTextHeight(font) };
	if (w > 0 && meetsHole(&r)) texts_into_hole++;
	return w;
}

static int fakeDrawHints(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	(void)screen;
	(void)drawn;
	if (group == MOSAIC_HINTS_RIGHT) {
		hints_right_count = count;
		for (int i = 0; i < count && i < 2; i++) hints_right[i] = hints[i];
	} else {
		hints_left_count = count;
	}
	return 0;
}

static int fakeGamesCount(void) {
	return fixture_count;
}

static int fakeGameInfo(int index, MosaicPlayGame* out) {
	if (!out || index < 0 || index >= fixture_count) return -1;
	const Fixture* f = &fixture[index];
	snprintf(out->path, sizeof(out->path), "%s", f->path);
	snprintf(out->name, sizeof(out->name), "%s", f->name);
	out->seconds = f->seconds;
	out->first_played = f->first;
	out->last_played = f->last;
	out->count = f->count;
	out->orphan = f->orphan;
	return 0;
}

static long long fakeTimeOn(int days_ago) {
	if (fixture_count < 0 || days_ago < 0) return MOSAIC_PLAY_UNKNOWN;
	return days_ago <= AS_DAYS ? days_played[days_ago] : MOSAIC_PLAY_UNKNOWN;
}

static void setupHost(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.sdcard_path = CARD;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.request_redraw = fakeRedraw;
	fake_host.register_action = fakeAction;
	fake_host.bind_button = fakeBind;
	fake_host.toast = fakeToast;
	fake_host.quick_tile = fakeTile;
	fake_host.open_path = fakeOpenPath;
	fake_host.register_screen = fakeRegister;
	fake_host.open_screen = fakeOpenScreen;
	fake_host.theme_color = fakeThemeColor;
	fake_host.screen_scale = fakeScale;
	fake_host.fill_rect = fakeFill;
	fake_host.fill_round_rect = fakeFillRound;
	fake_host.text_width = fakeTextWidth;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.draw_hints = fakeDrawHints;
	fake_host.play_games_count = fakeGamesCount;
	fake_host.play_game_info = fakeGameInfo;
	fake_host.play_time_on = fakeTimeOn;
}

static const MosaicPlugin* start(void) {
	setupHost();
	tile_id[0] = '\0';
	bound_button = -1;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	return p;
}

static void draw(void) {
	MosaicScreenView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.area = AREA;
	v.status = STATUS;
	v.screen = SCREEN_ID;
	hints_right_count = 0;
	hints_left_count = 0;
	CHECK(registered.render(&screen_surface, &v, registered.data) == 0);
}

static int press(int button) {
	MosaicInput in = { sizeof(MosaicInput), MOSAIC_BUTTON_BIT(button), MOSAIC_BUTTON_BIT(button), 0,
		MOSAIC_BUTTON_BIT(button) };
	return registered.input(&in, registered.data);
}

static void testInit(void) {
	start();
	CHECK_STR(registered_id, "activity-stats.main");
	CHECK(registered.flags == 0);
	CHECK(registered.enter && registered.exit && registered.input && registered.render);
	CHECK_STR(action_id, "activity-stats.open");
	CHECK_STR(action_label, "Activity");
	CHECK_STR(tile_id, "activity-stats.open");
	CHECK(bound_button == -1);
	CHECK(action_available(NULL) == 1);
	action_run(NULL);
	CHECK_STR(opened_screen, "activity-stats.main");

	tile_setting = "off";
	button_setting = "SELECT";
	start();
	CHECK_STR(tile_id, "");
	CHECK(bound_button == MOSAIC_BUTTON_SELECT);
	tile_setting = "on";
	button_setting = "none";

	// a launcher without the whole play history
	setupHost();
	fake_host.size = offsetof(MosaicHost, play_games_count);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) != 0);
}

static const Fixture history[] = {
	{ ROMS "/Zelda.gba", "Zelda", 8100, 100, 900, 2, 0 },
	{ ROMS "/Tetris.gb", "Tetris", 1600, 200, 800, 2, 0 },
	{ ROMS "/Puzzle.gb", "", 300, 300, 700, 5, 0 },
	{ "", "Metroid", 50, 50, 50, 1, 1 },
	{ ROMS "/Gone.gb", "Gone", 40, 40, 40, 1, 0 },
};

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" ROMS "'") == 0);
	const char* files[] = { ROMS "/Zelda.gba", ROMS "/Tetris.gb", ROMS "/Puzzle.gb" };
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		FILE* f = fopen(files[i], "w");
		CHECK(f != NULL);
		if (f) fclose(f);
	}
}

static void testScreen(void) {
	makeCard();
	start();
	fixture = history;
	fixture_count = 5;
	memset(days_played, 0, sizeof(days_played));
	days_played[0] = 900;
	days_played[1] = 1600;
	days_played[2] = 7200;
	days_played[3] = 50;
	days_played[4] = 300;
	days_played[20] = 3600;
	days_played[AS_DAYS] = 99999; // past the month: never asked for
	tab = AS_TAB_WEEK;
	CHECK(registered.enter(NULL) == 0);
	CHECK_STR(last_log, "history: 5 games, 11 sessions, 10090 seconds");
	fills_into_hole = texts_into_hole = 0;
	draw();
	CHECK_STR(last_log, "showing 7 days: days 900 1600 7200 50 300 0 0, axis 7200, 7 days: 2h 47m \xc2\xb7 23m a day");
	CHECK(hints_left_count == 1 && hints_right_count == 0);

	redraws = 0;
	CHECK(press(MOSAIC_BUTTON_R1) == MOSAIC_SCREEN_STAY);
	CHECK(redraws == 1);
	draw();
	CHECK(strstr(last_log, "showing 30 days: days 900 1600 7200 50 300 0 ") == last_log);
	CHECK(strstr(last_log, " 3600 0 0 0 0 0 0 0 0 0, axis 7200, 30 days: 3h 47m \xc2\xb7 7m a day") != NULL);

	press(MOSAIC_BUTTON_RIGHT);
	draw();
	CHECK_STR(last_log, "showing Games: 0 of 5: Zelda, 2h 15m \xc2\xb7 2 sessions, can open");
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_A);
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 1 of 5: Tetris, 26m \xc2\xb7 2 sessions, can open");
	opened_path[0] = '\0';
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK_STR(opened_path, ROMS "/Tetris.gb");
	CHECK(opened_type == MOSAIC_ENTRY_ROM);
	CHECK_STR(last_log, "open " ROMS "/Tetris.gb 1");

	// the orphan and the game no longer on the card can't be opened
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 3 of 5: Metroid, <1m \xc2\xb7 1 session, can't open");
	CHECK(hints_right_count == 0);
	opened_path[0] = '\0';
	press(MOSAIC_BUTTON_A);
	CHECK_STR(opened_path, "");
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 4 of 5: Gone, <1m \xc2\xb7 1 session, can't open");
	// down from the last wraps on a press
	press(MOSAIC_BUTTON_DOWN);
	draw();
	CHECK_STR(last_log, "showing Games: 0 of 5: Zelda, 2h 15m \xc2\xb7 2 sessions, can open");

	// a launch that doesn't go ahead says so
	open_answer = 0;
	toast_text[0] = '\0';
	press(MOSAIC_BUTTON_A);
	CHECK_STR(toast_text, "Couldn't open Zelda");
	open_answer = 1;

	press(MOSAIC_BUTTON_R1);
	draw();
	CHECK_STR(last_log, "showing Launches: 0 of 5: Puzzle, 5 sessions \xc2\xb7 5m, can open");
	press(MOSAIC_BUTTON_UP);
	draw();
	CHECK_STR(last_log, "showing Launches: 4 of 5: Gone, 1 session \xc2\xb7 <1m, can't open");
	// each list keeps its own selection
	press(MOSAIC_BUTTON_L1);
	draw();
	CHECK_STR(last_log, "showing Games: 0 of 5: Zelda, 2h 15m \xc2\xb7 2 sessions, can open");
	press(MOSAIC_BUTTON_LEFT);
	press(MOSAIC_BUTTON_LEFT);
	draw();
	CHECK(strstr(last_log, "showing 7 days: ") == last_log);
	// up and down do nothing on a chart
	redraws = 0;
	press(MOSAIC_BUTTON_DOWN);
	CHECK(redraws == 0);
	CHECK(fills_into_hole == 0);
	CHECK(texts_into_hole == 0);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(NULL);
	CHECK(games == NULL && game_count == -1);

	// nothing played yet
	fixture_count = 0;
	memset(days_played, 0, sizeof(days_played));
	tab = AS_TAB_GAMES;
	registered.enter(NULL);
	CHECK_STR(last_log, "history: 0 games, 0 sessions, 0 seconds");
	draw();
	CHECK_STR(last_log, "showing Games: nothing played yet");
	CHECK(hints_right_count == 0);
	press(MOSAIC_BUTTON_A);
	press(MOSAIC_BUTTON_DOWN);
	press(MOSAIC_BUTTON_L1);
	press(MOSAIC_BUTTON_L1);
	draw();
	CHECK_STR(last_log, "showing 7 days: days 0 0 0 0 0 0 0, axis 3600, 7 days: 0m \xc2\xb7 0m a day");
	registered.exit(NULL);

	// no history: every tab says so
	fixture_count = -1;
	registered.enter(NULL);
	CHECK_STR(last_log, "no play history");
	for (int i = 0; i < AS_TABS; i++) {
		draw();
		char want[64];
		snprintf(want, sizeof(want), "showing %s: no play history", AS_tabLabel(tab));
		CHECK_STR(last_log, want);
		CHECK(hints_left_count == 1 && hints_right_count == 0);
		press(MOSAIC_BUTTON_R1);
	}
	press(MOSAIC_BUTTON_A);
	registered.exit(NULL);
	mosaic_plugin_get(&fake_host)->quit();
}

int main(void) {
	testName();
	testRank();
	testFormat();
	testDays();
	testChart();
	testTabs();
	testLayout();
	testInit();
	testScreen();
	CHECK_DONE();
}
