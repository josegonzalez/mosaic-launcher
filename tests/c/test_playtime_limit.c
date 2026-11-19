// The play time limit plugin's model, and the plugin against a fake host.

#include <stdlib.h>
#include <time.h>
#include "check.h"
#include "fakeproviders.h"

// the plugin's clock: the real one, or fake_now once a test sets it
static long long fake_now = 0;
#define PL_TIME() (fake_now ? (time_t)fake_now : time(NULL))
#include "../../plugins/playtime-limit/playtime_limit.c"

// Monday 17 November 2025, 00:00 UTC
#define MONDAY 1763337600LL
#define HOUR 3600LL
#define DAY (24 * HOUR)

static void today(char day[PL_DAY_LEN]) {
	Look l;
	clockLook((long long)PL_TIME(), &l);
	snprintf(day, PL_DAY_LEN, "%s", l.day);
}

///////////////////////////////////////
// the model

static void testParseMinutes(void) {
	CHECK(PL_parseMinutes("60", 0) == 60);
	CHECK(PL_parseMinutes(" 15 ", 0) == 15);
	CHECK(PL_parseMinutes("1440", 0) == PL_MINUTES_MAX);
	CHECK(PL_parseMinutes("1441", 7) == 7);
	CHECK(PL_parseMinutes("99999999999999999999", 7) == 7);
	CHECK(PL_parseMinutes("off", 0) == 0);
	CHECK(PL_parseMinutes("0", 5) == 5);
	CHECK(PL_parseMinutes("-5", 5) == 5);
	CHECK(PL_parseMinutes("10m", 5) == 5);
	CHECK(PL_parseMinutes("", 3) == 3);
	CHECK(PL_parseMinutes(NULL, 3) == 3);
}

static void testDayKey(void) {
	struct tm local = { 0 };
	local.tm_year = 2026 - 1900;
	local.tm_mon = 10;
	local.tm_mday = 8;
	char day[PL_DAY_LEN];
	PL_dayKey(&local, day);
	CHECK_STR(day, "2026-11-08");
	local.tm_mon = 0;
	local.tm_mday = 1;
	PL_dayKey(&local, day);
	CHECK_STR(day, "2026-01-01");
	// out of range parts still fit
	local.tm_year = 99999;
	local.tm_mon = 40;
	local.tm_mday = -3;
	PL_dayKey(&local, day);
	CHECK(strlen(day) == 10);
}

static void testGrants(void) {
	// a grant counts only on its day
	CHECK(PL_grantToday("2026-11-08", "15", "2026-11-08") == 15);
	CHECK(PL_grantToday("2026-11-07", "15", "2026-11-08") == 0);
	CHECK(PL_grantToday("", "15", "2026-11-08") == 0);
	CHECK(PL_grantToday(NULL, "15", "2026-11-08") == 0);
	CHECK(PL_grantToday("2026-11-08", "junk", "2026-11-08") == 0);
	CHECK(PL_grantToday("2026-11-08", NULL, "2026-11-08") == 0);
	// a day that can't be told isn't a match
	CHECK(PL_grantToday("", "15", "") == 0);

	// grants add up through the day and start again the next
	CHECK(PL_addGrant("", "0", "2026-11-08", 15) == 15);
	CHECK(PL_addGrant("2026-11-08", "15", "2026-11-08", 30) == 45);
	CHECK(PL_addGrant("2026-11-07", "45", "2026-11-08", 15) == 15);
	CHECK(PL_addGrant("2026-11-08", "1430", "2026-11-08", 60) == PL_MINUTES_MAX);
	CHECK(PL_addGrant("2026-11-08", "20", "2026-11-08", 0) == 20);
}

static void testSecondsLeft(void) {
	CHECK(PL_secondsLeft(60, 0, 0) == 3600);
	CHECK(PL_secondsLeft(60, 0, 600) == 3000);
	CHECK(PL_secondsLeft(60, 15, 3600) == 900);
	CHECK(PL_secondsLeft(60, 0, 3600) == 0);
	CHECK(PL_secondsLeft(60, 0, 9000) == 0);
	CHECK(PL_secondsLeft(60, -5, 0) == 3600);
	// no limit, or an unknown history
	CHECK(PL_secondsLeft(0, 15, 0) == -1);
	CHECK(PL_secondsLeft(60, 0, MOSAIC_PLAY_UNKNOWN) == -1);
}

static void testState(void) {
	CHECK(PL_state(0, 600, -1, 10) == PL_OFF);
	CHECK(PL_state(0, MOSAIC_PLAY_UNKNOWN, -1, 10) == PL_OFF);
	CHECK(PL_state(60, MOSAIC_PLAY_UNKNOWN, -1, 10) == PL_UNKNOWN);
	CHECK(PL_state(60, 0, 3600, 10) == PL_OK);
	CHECK(PL_state(60, 0, 601, 10) == PL_OK);
	CHECK(PL_state(60, 0, 600, 10) == PL_WARN);
	CHECK(PL_state(60, 0, 1, 10) == PL_WARN);
	CHECK(PL_state(60, 0, 0, 10) == PL_UP);
	// no threshold, no warning
	CHECK(PL_state(60, 0, 1, 0) == PL_OK);
}

static const char* format(long long left) {
	static char out[32];
	PL_format(left, out, sizeof(out));
	return out;
}

static void testFormat(void) {
	CHECK(PL_shownMinutes(0) == 0);
	CHECK(PL_shownMinutes(1) == 1);
	CHECK(PL_shownMinutes(60) == 1);
	CHECK(PL_shownMinutes(61) == 2);
	CHECK(PL_shownMinutes(-1) == 0);
	CHECK_STR(format(0), "Time's up");
	CHECK_STR(format(1), "1m left");
	CHECK_STR(format(45 * 60), "45m left");
	CHECK_STR(format(59 * 60 + 1), "1h 00m left");
	CHECK_STR(format(65 * 60), "1h 05m left");
	CHECK_STR(format(24 * 3600), "24h 00m left");
	char small[4];
	PL_format(45 * 60, small, sizeof(small));
	CHECK_STR(small, "45m");
	PL_format(45 * 60, NULL, 4);

	// in a translation's words
	static const char* const words[PL_WORDS_COUNT] = { "Fini", "encore %lld min", "encore %lld h %02lld" };
	char out[32];
	PL_formatIn(0, words, out, sizeof(out));
	CHECK_STR(out, "Fini");
	PL_formatIn(45 * 60, words, out, sizeof(out));
	CHECK_STR(out, "encore 45 min");
	PL_formatIn(65 * 60, words, out, sizeof(out));
	CHECK_STR(out, "encore 1 h 05");
}

static void testSchedules(void) {
	// the weekend follows the weekday limit unless set apart
	CHECK(PL_weekendLimit("same", 60) == 60);
	CHECK(PL_weekendLimit(" same ", 60) == 60);
	CHECK(PL_weekendLimit(NULL, 60) == 60);
	CHECK(PL_weekendLimit("", 60) == 60);
	CHECK(PL_weekendLimit("120", 60) == 120);
	CHECK(PL_weekendLimit("off", 60) == 0);
	CHECK(PL_weekendLimit("samey", 60) == 0);
	CHECK(PL_weekendLimit("same", 0) == 0);

	CHECK(PL_isWeekend(0) && PL_isWeekend(6));
	for (int d = 1; d <= 5; d++) CHECK(!PL_isWeekend(d));
	CHECK(PL_todayLimit(30, 90, 1) == 30);
	CHECK(PL_todayLimit(30, 90, 5) == 30);
	CHECK(PL_todayLimit(30, 90, 6) == 90);
	CHECK(PL_todayLimit(30, 90, 0) == 90);
}

static void testClock(void) {
	CHECK(PL_parseClock("21:00", -1) == 21 * 60);
	CHECK(PL_parseClock(" 07:30 ", -1) == 7 * 60 + 30);
	CHECK(PL_parseClock("00:00", -1) == 0);
	CHECK(PL_parseClock("23:59", -1) == 23 * 60 + 59);
	CHECK(PL_parseClock("24:00", -1) == -1);
	CHECK(PL_parseClock("12:60", -1) == -1);
	CHECK(PL_parseClock("7:30", -1) == -1);
	CHECK(PL_parseClock("07:30pm", -1) == -1);
	CHECK(PL_parseClock("off", -1) == -1);
	CHECK(PL_parseClock("", -1) == -1);
	CHECK(PL_parseClock(NULL, -1) == -1);

	char out[PL_CLOCK_LEN];
	PL_formatClock(21 * 60, out);
	CHECK_STR(out, "21:00");
	PL_formatClock(7 * 60 + 5, out);
	CHECK_STR(out, "07:05");
	PL_formatClock(PL_MINUTES_MAX + 30, out);
	CHECK_STR(out, "00:30");
	PL_formatClock(-30, out);
	CHECK_STR(out, "23:30");
}

static void testWindow(void) {
	int nine = 21 * 60, seven = 7 * 60;
	// across midnight
	CHECK(PL_inWindow(nine, seven, nine));
	CHECK(PL_inWindow(nine, seven, 23 * 60 + 59));
	CHECK(PL_inWindow(nine, seven, 0));
	CHECK(PL_inWindow(nine, seven, seven - 1));
	CHECK(!PL_inWindow(nine, seven, seven));
	CHECK(!PL_inWindow(nine, seven, 12 * 60));
	CHECK(!PL_inWindow(nine, seven, nine - 1));
	// inside one day
	CHECK(PL_inWindow(60, 120, 60));
	CHECK(!PL_inWindow(60, 120, 120));
	CHECK(!PL_inWindow(60, 120, 30));
	// empty or off
	CHECK(!PL_inWindow(nine, nine, nine));
	CHECK(!PL_inWindow(-1, seven, 0));
	CHECK(!PL_inWindow(nine, -1, 23 * 60));

	// an unlock counts until it runs out, but not from a clock that went back
	CHECK(PL_unlocked(1000, 999));
	CHECK(!PL_unlocked(1000, 1000));
	CHECK(!PL_unlocked(0, 1000));
	CHECK(PL_unlocked(1000 + PL_UNLOCK_MAX, 1000));
	CHECK(!PL_unlocked(1001 + PL_UNLOCK_MAX, 1000));
}

static void testBar(void) {
	CHECK(PL_barFill(0, 60, 0, 600) == 0);
	CHECK(PL_barFill(30 * 60, 60, 0, 600) == 300);
	CHECK(PL_barFill(45 * 60, 60, 0, 640) == 480);
	CHECK(PL_barFill(60 * 60, 60, 0, 600) == 600);
	CHECK(PL_barFill(5 * 3600, 60, 0, 600) == 600);
	// a grant makes room
	CHECK(PL_barFill(60 * 60, 60, 60, 600) == 300);
	CHECK(PL_barFill(30 * 60, 60, -5, 600) == 300);
	// no limit, an unknown history or no width
	CHECK(PL_barFill(0, 0, 0, 600) == -1);
	CHECK(PL_barFill(MOSAIC_PLAY_UNKNOWN, 60, 0, 600) == -1);
	CHECK(PL_barFill(0, 60, 0, 0) == -1);
}

static void testCode(void) {
	CHECK(PL_codeMatches("1234", "1234"));
	CHECK(PL_codeMatches(" 1234\n", "1234 "));
	CHECK(!PL_codeMatches("1234", "4321"));
	CHECK(!PL_codeMatches("1234", ""));
	CHECK(!PL_codeMatches("1234", NULL));
	CHECK(!PL_codeMatches("", ""));
	CHECK(!PL_codeMatches("  ", " "));
	CHECK(!PL_codeMatches(NULL, "1234"));
}

///////////////////////////////////////
// the plugin against a fake host

#define CODE "s3cret"

static char keys[8][32];
static char values[8][64];
static int key_count = 0;
static long long today_played = 0;
static int redraws = 0;
static char last_log[600];
static int logs = 0;
static int code_logged = 0;
static char last_toast[128];
static char confirm_message[128];
static void (*confirm_yes)(void* data);
static void (*keyboard_done)(const char* text, void* data);
static char drawn[64];
static unsigned int pill_rgb = 0, ink_rgb = 0;

static char all_logs[8192];
static void fakeLog(const char* id, const char* m) {
	snprintf(last_log, sizeof(last_log), "%s", m);
	size_t used = strlen(all_logs);
	snprintf(all_logs + used, sizeof(all_logs) - used, "%s\n", m);
	if (strstr(m, CODE)) code_logged = 1;
	logs++;
}
static const char* fakeCfgGet(const char* key, const char* fallback) {
	for (int i = 0; i < key_count; i++) {
		if (!strcmp(keys[i], key)) return values[i];
	}
	return fallback;
}
static void fakeCfgSet(const char* key, const char* value) {
	for (int i = 0; i < key_count; i++) {
		if (!strcmp(keys[i], key)) {
			snprintf(values[i], sizeof(values[i]), "%s", value);
			return;
		}
	}
	snprintf(keys[key_count], sizeof(keys[0]), "%s", key);
	snprintf(values[key_count], sizeof(values[0]), "%s", value);
	key_count++;
}
static void setCfg(const char* key, const char* value) {
	fakeCfgSet(key, value);
}
static void fakeRedraw(void) { redraws++; }
static void fakeToast(const char* message) { snprintf(last_toast, sizeof(last_toast), "%s", message); }
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void* data), void* data) {
	(void)yes; (void)data;
	snprintf(confirm_message, sizeof(confirm_message), "%s", message);
	confirm_yes = on_yes;
}
static void fakeKeyboard(const char* title, const char* initial, void (*on_done)(const char* text, void* data), void* data) {
	(void)title; (void)initial; (void)data;
	keyboard_done = on_done;
}
static long long fakeToday(void) { return today_played; }
static int fakeScale(void) { return 2; }
static int fakeTheme(int index) { return index == MOSAIC_COLOR_ACCENT ? 0x112233 : index == MOSAIC_COLOR_HINT ? 0xEEEEEE : 0; }
// every character is 5 wide and a line 12 tall
static int fakeTextWidth(int font, const char* text) { CHECK(font == FONT); return 5 * (int)strlen(text); }
static int fakeTextHeight(int font) { CHECK(font == FONT); return 12; }
static int fakeFillRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s; (void)r; (void)radius;
	pill_rgb = ((unsigned int)c.r << 16) | ((unsigned int)c.g << 8) | c.b;
	return 0;
}
static int fakeDrawText(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max_width) {
	(void)s; (void)font; (void)x; (void)y; (void)max_width;
	snprintf(drawn, sizeof(drawn), "%s", text);
	ink_rgb = ((unsigned int)c.r << 16) | ((unsigned int)c.g << 8) | c.b;
	return 0;
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) {
	(void)s; (void)i; (void)r; (void)n;
	return 0;
}

// the bar's two rects, the track then the fill, and their colours
static MosaicRect fills[2];
static unsigned int fill_rgb[2];
static int fill_alpha[2];
static int fill_count = 0;
static const char* home_id = NULL;
static const char* path_now = "/mnt/SDCARD";
static int fakeFillRect(MosaicSurface* s, const MosaicRect* r, MosaicColor c) {
	(void)s;
	if (fill_count < 2) {
		fills[fill_count] = *r;
		fill_rgb[fill_count] = ((unsigned int)c.r << 16) | ((unsigned int)c.g << 8) | c.b;
		fill_alpha[fill_count] = c.a;
	}
	fill_count++;
	return 0;
}
static int fakeWidth(void) { return 640; }
static const char* fakeHome(void) { return home_id; }
static const char* fakePath(void) { return path_now; }
static int fakeOpenScreen(const char* id) { (void)id; return 0; }

static MosaicHost fake;

static int logged(const char* line) {
	char want[640];
	snprintf(want, sizeof(want), "%s\n", line);
	return strstr(all_logs, want) != NULL;
}

static void reset(void) {
	key_count = 0;
	today_played = 0;
	redraws = logs = code_logged = 0;
	all_logs[0] = last_log[0] = last_toast[0] = confirm_message[0] = drawn[0] = '\0';
	confirm_yes = NULL;
	keyboard_done = NULL;
	fake_now = 0;
	fill_count = 0;
	home_id = NULL;
	path_now = "/mnt/SDCARD";
	memset(&fake, 0, sizeof(fake));
	fake.size = sizeof(fake);
	fake.api_version = MOSAIC_API_VERSION;
	fake.platform = "desktop";
	fake.log = fakeLog;
	fake.cfg_get = fakeCfgGet;
	fake.cfg_set = fakeCfgSet;
	fake.request_redraw = fakeRedraw;
	fake.toast = fakeToast;
	fake.confirm = fakeConfirm;
	fake.keyboard = fakeKeyboard;
	fake.screen_scale = fakeScale;
	fake.theme_color = fakeTheme;
	fake.text_width = fakeTextWidth;
	fake.text_height = fakeTextHeight;
	fake.fill_round_rect = fakeFillRound;
	fake.draw_text = fakeDrawText;
	fake.decorate_cell = fakeDecorateCell;
	fake.play_time_today = fakeToday;
	fake.sdcard_path = "/mnt/SDCARD";
	fake.current_path = fakePath;
	fake.fill_rect = fakeFillRect;
	fake.screen_width = fakeWidth;
	fake.home_screen = fakeHome;
	fake.open_screen = fakeOpenScreen;
}

static const char* const GAME = "/mnt/SDCARD/Roms/Game Boy (GB)/Tetris.gb";

static int launch(const char* path, int type) {
	MosaicLaunch l = { .path = path, .type = type };
	return plugin.pre_launch(&l);
}

static void testOffByDefault(void) {
	reset();
	today_played = 10 * 3600;
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(last_log, "no daily limit set, so nothing is limited");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	plugin.tick(1000);
	CHECK(plugin.header_width(500, 30) == 0);
	CHECK(redraws == 0);
	CHECK(!confirm_yes);
}

static void testNoHistoryCall(void) {
	reset();
	fake.size = offsetof(MosaicHost, play_time);
	setCfg("limit", "60");
	CHECK(plugin.init(&fake) == 1);
	CHECK_STR(last_log, "unsupported: this launcher has no play history");
}

static void testUnderAndOver(void) {
	reset();
	setCfg("limit", "60");
	setCfg("bar", "off"); // the bar's fill redraws too, as testBarPlugin checks
	setCfg("code", CODE);
	today_played = 40 * 60;
	CHECK(plugin.init(&fake) == 0);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK_STR(last_log, "allowed Tetris.gb: 20 of 60 minutes left today");

	// the widget shows the time left in the theme's colours
	plugin.tick(1000);
	CHECK(redraws == 1);
	CHECK(plugin.header_width(500, 30) == 5 * 8 + 48);
	MosaicRect rect = { 10, 15, 88, 30 };
	CHECK(plugin.header_widget(NULL, &rect) == 0);
	CHECK_STR(drawn, "20m left");
	CHECK(pill_rgb == 0x112233 && ink_rgb == 0xEEEEEE);
	CHECK_STR(last_log, "widget at 10,15 88x30: 20m left");
	// no room, no widget
	CHECK(plugin.header_width(40, 30) == 0);

	// looks once a second, and redraws only when the minute changes
	today_played += 20;
	plugin.tick(2000);
	plugin.tick(2000);
	CHECK(redraws == 1);
	today_played = 50 * 60 + 30;
	plugin.tick(2999);
	CHECK(redraws == 1);
	plugin.tick(3000);
	CHECK(redraws == 2);
	// under the threshold: the warning colour
	CHECK(plugin.header_width(500, 30) > 0);
	CHECK(plugin.header_widget(NULL, &rect) == 0);
	CHECK_STR(drawn, "10m left");
	CHECK(pill_rgb == WARN_RGB && ink_rgb == WARN_INK_RGB);
	CHECK_STR(last_log, "widget at 10,15 88x30: 10m left, warning");

	// paks aren't limited
	today_played = 2 * 3600;
	CHECK(launch("/mnt/SDCARD/Tools/tg5040/Files.pak", MOSAIC_ENTRY_PAK) == MOSAIC_LAUNCH_CONTINUE);

	// over the limit: refused, offering the code
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(last_log, "refused Tetris.gb: 7200 seconds played today, the limit is 60 minutes; offering extra time behind the code");
	CHECK_STR(confirm_message, "Time's up for today. Enter the code for 15 more minutes?");
	CHECK(confirm_yes != NULL);

	// a wrong code grants nothing
	confirm_yes(NULL);
	CHECK(keyboard_done != NULL);
	keyboard_done("nope", NULL);
	CHECK_STR(last_toast, "Wrong code");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	// nor does a cancelled keyboard
	keyboard_done(NULL, NULL);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);

	// the code grants 15 minutes for today, kept in the cfg
	today_played = 60 * 60;
	keyboard_done(" " CODE " ", NULL);
	CHECK_STR(last_toast, "15 more minutes today");
	CHECK_STR(last_log, "granted 15 more minutes today, 15 in all");
	char day[PL_DAY_LEN];
	today(day);
	CHECK_STR(fakeCfgGet("grant_day", ""), day);
	CHECK_STR(fakeCfgGet("grant_minutes", ""), "15");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK_STR(last_log, "allowed Tetris.gb: 15 of 75 minutes left today");
	// a second grant adds up
	keyboard_done(CODE, NULL);
	CHECK_STR(fakeCfgGet("grant_minutes", ""), "30");

	// back from a game with the time up: a warning
	today_played = 90 * 60;
	plugin.post_return();
	CHECK_STR(last_toast, "Time's up for today");
	CHECK(plugin.header_widget(NULL, &rect) == 0);
	CHECK_STR(drawn, "Time's up");
	CHECK(pill_rgb == WARN_RGB);
	// and with a little left
	today_played = 85 * 60;
	plugin.post_return();
	CHECK_STR(last_toast, "5m left today");

	CHECK(!code_logged);
}

static void testGrantExpires(void) {
	reset();
	setCfg("limit", "30");
	setCfg("code", CODE);
	// yesterday's grant doesn't count today
	setCfg("grant_day", "2000-01-01");
	setCfg("grant_minutes", "60");
	today_played = 30 * 60;
	CHECK(plugin.init(&fake) == 0);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	// a new grant starts from 0 today
	confirm_yes(NULL);
	keyboard_done(CODE, NULL);
	CHECK_STR(fakeCfgGet("grant_minutes", ""), "15");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(!code_logged);
}

static void testNoCode(void) {
	reset();
	setCfg("limit", "30");
	today_played = 30 * 60;
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(last_log, "daily limit 30 minutes, warning at 10, 15 more behind the code (no code set)");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(last_log, "refused Tetris.gb: 1800 seconds played today, the limit is 30 minutes");
	CHECK_STR(last_toast, "Time's up for today");
	CHECK(!confirm_yes);
}

static void testUnknownHistory(void) {
	reset();
	setCfg("limit", "30");
	today_played = MOSAIC_PLAY_UNKNOWN;
	CHECK(plugin.init(&fake) == 0);
	int before = logs;
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(strstr(last_log, "the play history can't be read") != NULL);
	CHECK(logs == before + 1);
	// logged once
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	plugin.tick(1000);
	plugin.tick(5000);
	CHECK(logs == before + 1);
	CHECK(plugin.header_width(500, 30) == 0);
	// once it can be read, the widget shows
	today_played = 0;
	plugin.tick(9000);
	CHECK(plugin.header_width(500, 30) > 0);
}

#define NO_BEDTIME "\nbedtime=0\nbedtime_start=\nbedtime_end="

static void testReply(void) {
	char out[256];
	CHECK(PL_reply(PL_OK, 60, 15, 1500, "25m left", 0, -1, -1, out, sizeof(out)) == 0);
	CHECK_STR(out, "state=ok\nlimit=60\ngranted=15\nleft=1500\ntext=25m left" NO_BEDTIME);
	CHECK(PL_reply(PL_OFF, 60, 15, 1500, "x", 0, -1, -1, out, sizeof(out)) == 0);
	CHECK_STR(out, "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=" NO_BEDTIME);
	CHECK(PL_reply(PL_UNKNOWN, 60, 0, -1, "x", 0, -1, -1, out, sizeof(out)) == 0);
	CHECK_STR(out, "state=unknown\nlimit=60\ngranted=0\nleft=-1\ntext=" NO_BEDTIME);
	CHECK(PL_reply(PL_UP, 30, 0, 0, "Time's up", 0, -1, -1, out, sizeof(out)) == 0);
	CHECK_STR(out, "state=up\nlimit=30\ngranted=0\nleft=0\ntext=Time's up" NO_BEDTIME);
	CHECK_STR(PL_stateName(PL_WARN), "warn");
	CHECK(PL_reply(PL_OK, 60, 0, 1, "", 0, -1, -1, out, 8) == -1 && out[0] == '\0');
	// bedtime's fields, added after the first ones
	CHECK(PL_reply(PL_OFF, 0, 0, -1, "", 1, 21 * 60, 7 * 60, out, sizeof(out)) == 0);
	CHECK_STR(out, "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=\nbedtime=1\nbedtime_start=21:00\nbedtime_end=07:00");
	CHECK(PL_reply(PL_OK, 60, 0, 60, "1m left", 0, 21 * 60, 7 * 60, out, sizeof(out)) == 0);
	CHECK_STR(out, "state=ok\nlimit=60\ngranted=0\nleft=60\ntext=1m left\nbedtime=0\nbedtime_start=21:00\nbedtime_end=07:00");
	// bedtime off is never on
	CHECK(PL_reply(PL_OFF, 0, 0, -1, "", 1, -1, -1, out, sizeof(out)) == 0);
	CHECK_STR(out, "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=" NO_BEDTIME);
}

// limit.left, called as another plugin would
static void callLeft(char* out, size_t size) {
	CHECK(fakeCall("limit.left", NULL, out, size) >= 0);
}

static void testService(void) {
	reset();
	fake_provider_count = 0;
	fake.provide = fakeProvide;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	char out[MOSAIC_CALL_MAX];
	// with no limit set it still answers
	CHECK(plugin.init(&fake) == 0);
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=" NO_BEDTIME);

	reset();
	fake_provider_count = 0;
	fake.provide = fakeProvide;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	setCfg("limit", "60");
	today_played = 25 * 60;
	CHECK(plugin.init(&fake) == 0);
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=ok\nlimit=60\ngranted=0\nleft=2100\ntext=35m left" NO_BEDTIME);
	today_played = 55 * 60;
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=warn\nlimit=60\ngranted=0\nleft=300\ntext=5m left" NO_BEDTIME);
	// today's grant, apart from the limit
	char day[PL_DAY_LEN];
	today(day);
	setCfg("grant_day", day);
	setCfg("grant_minutes", "30");
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=ok\nlimit=60\ngranted=30\nleft=2100\ntext=35m left" NO_BEDTIME);
	today_played = 2 * 3600;
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=up\nlimit=60\ngranted=30\nleft=0\ntext=Time's up" NO_BEDTIME);
	today_played = MOSAIC_PLAY_UNKNOWN;
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=unknown\nlimit=60\ngranted=30\nleft=-1\ntext=" NO_BEDTIME);

	// an older launcher, without providers: nothing provided
	reset();
	fake_provider_count = 0;
	CHECK(plugin.init(&fake) == 0);
	CHECK(fake_provider_count == 0);
}

static void testWeekend(void) {
	// an older cfg's single limit: every day
	reset();
	setCfg("limit", "30");
	today_played = 40 * 60;
	fake_now = MONDAY + 12 * HOUR;
	CHECK(plugin.init(&fake) == 0);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	fake_now = MONDAY + 5 * DAY + 12 * HOUR; // Saturday
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);

	// a weekend limit apart from it
	reset();
	setCfg("limit", "30");
	setCfg("weekend", "60");
	today_played = 40 * 60;
	CHECK(plugin.init(&fake) == 0);
	CHECK(logged("budget bar on"));
	CHECK(logged("weekday limit 30 minutes, weekend limit 60 minutes, warning at 10, 15 more behind the code (no code set)"));
	fake_now = MONDAY + 4 * DAY + 23 * HOUR; // Friday night
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(last_log, "refused Tetris.gb: 2400 seconds played today, the limit is 30 minutes");
	fake_now = MONDAY + 5 * DAY + HOUR; // Saturday, after midnight
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK_STR(last_log, "allowed Tetris.gb: 20 of 60 minutes left today");
	fake_now = MONDAY + 6 * DAY + HOUR; // Sunday
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	fake_now = MONDAY + 7 * DAY + HOUR; // Monday again
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);

	// no weekend limit: weekends aren't limited, and the service says so
	reset();
	fake_provider_count = 0;
	fake.provide = fakeProvide;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	setCfg("limit", "30");
	setCfg("weekend", "off");
	today_played = 5 * 3600;
	CHECK(plugin.init(&fake) == 0);
	fake_now = MONDAY + 5 * DAY + 12 * HOUR;
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	char out[MOSAIC_CALL_MAX];
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=" NO_BEDTIME);
	fake_now = MONDAY + 12 * HOUR;
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=up\nlimit=30\ngranted=0\nleft=0\ntext=Time's up" NO_BEDTIME);

	// only a weekend limit
	reset();
	setCfg("weekend", "60");
	CHECK(plugin.init(&fake) == 0);
	today_played = 2 * 3600;
	fake_now = MONDAY + 12 * HOUR;
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	fake_now = MONDAY + 6 * DAY + 12 * HOUR;
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
}

static void testInitLogs(void) {
	reset();
	setCfg("limit", "30");
	setCfg("weekend", "90");
	setCfg("bedtime", "21:00");
	setCfg("wake", "07:00");
	setCfg("bar", "off");
	CHECK(plugin.init(&fake) == 0);
	CHECK(logged("budget bar off"));
	CHECK(logged("weekday limit 30 minutes, weekend limit 90 minutes, warning at 10, 15 more behind the code (no code set)"));
	CHECK_STR(last_log, "bedtime from 21:00 to 07:00");
	reset();
	setCfg("weekend", "off");
	setCfg("limit", "45");
	setCfg("code", CODE);
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(last_log, "weekday limit 45 minutes, weekend limit off, warning at 10, 15 more behind the code");
	reset();
	setCfg("bedtime", "22:30");
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(last_log, "bedtime from 22:30 to 07:00");
	CHECK(logged("no daily limit set"));
	CHECK(!logged("budget bar on"));
	// a start and end at the same minute is no bedtime
	reset();
	setCfg("bedtime", "07:00");
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(last_log, "no daily limit set, so nothing is limited");
	CHECK(!code_logged);
}

static void testBedtime(void) {
	reset();
	fake_provider_count = 0;
	fake.provide = fakeProvide;
	fake.call = fakeCall;
	fake.event_value = fakeEventValue;
	setCfg("bedtime", "21:00");
	setCfg("wake", "07:00");
	setCfg("code", CODE);
	setCfg("extra", "30");
	today_played = MOSAIC_PLAY_UNKNOWN; // bedtime doesn't need the history
	fake_now = MONDAY + 20 * HOUR + 59 * 60;
	CHECK(plugin.init(&fake) == 0);
	CHECK_STR(last_log, "bedtime from 21:00 to 07:00");

	// before bedtime: allowed, and no widget without a limit
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	plugin.tick(1000);
	CHECK(plugin.header_width(500, 30) == 0);
	char out[MOSAIC_CALL_MAX];
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=\nbedtime=0\nbedtime_start=21:00\nbedtime_end=07:00");

	// at bedtime the widget says so, in the warning colour
	fake_now = MONDAY + 21 * HOUR;
	int before = redraws;
	plugin.tick(2000);
	CHECK(redraws == before + 1);
	CHECK(plugin.header_width(500, 30) > 0);
	MosaicRect rect = { 10, 15, 88, 30 };
	CHECK(plugin.header_widget(NULL, &rect) == 0);
	CHECK_STR(drawn, "Bedtime");
	CHECK(pill_rgb == WARN_RGB && ink_rgb == WARN_INK_RGB);
	callLeft(out, sizeof(out));
	CHECK_STR(out, "state=off\nlimit=0\ngranted=0\nleft=-1\ntext=\nbedtime=1\nbedtime_start=21:00\nbedtime_end=07:00");

	// refused, past midnight too, offering the code; paks still open
	fake_now = MONDAY + DAY + 2 * HOUR;
	CHECK(launch("/mnt/SDCARD/Tools/tg5040/Files.pak", MOSAIC_ENTRY_PAK) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(last_log, "refused Tetris.gb: bedtime from 21:00 to 07:00; offering extra time behind the code");
	CHECK_STR(confirm_message, "It's bedtime. Enter the code for 30 more minutes?");
	confirm_yes(NULL);
	keyboard_done("nope", NULL);
	CHECK_STR(last_toast, "Wrong code");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);

	// the code unlocks 30 minutes from now, without touching the day's grant
	keyboard_done(CODE, NULL);
	CHECK_STR(last_log, "bedtime unlocked for 30 minutes");
	CHECK_STR(last_toast, "30 more minutes today");
	char until[32];
	snprintf(until, sizeof(until), "%lld", MONDAY + DAY + 2 * HOUR + 30 * 60);
	CHECK_STR(fakeCfgGet("bedtime_until", ""), until);
	CHECK_STR(fakeCfgGet("grant_minutes", "none"), "none");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	plugin.tick(3000);
	CHECK(plugin.header_width(500, 30) == 0);
	fake_now += 29 * 60;
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	// until it runs out
	fake_now += 60;
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	plugin.post_return();
	CHECK_STR(last_toast, "It's bedtime");

	// an unlock from a clock that went back doesn't count
	setCfg("bedtime_until", "99999999999");
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);

	// it ends at the wake time
	fake_now = MONDAY + DAY + 7 * HOUR;
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(!code_logged);
}

static void testBedtimeAndLimit(void) {
	// both refusing: one code unlocks both
	reset();
	setCfg("limit", "30");
	setCfg("bedtime", "21:00");
	setCfg("code", CODE);
	today_played = 30 * 60;
	fake_now = MONDAY + 22 * HOUR;
	CHECK(plugin.init(&fake) == 0);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(last_log, "refused Tetris.gb: 1800 seconds played today, the limit is 30 minutes; offering extra time behind the code");
	CHECK_STR(confirm_message, "It's bedtime. Enter the code for 15 more minutes?");
	confirm_yes(NULL);
	keyboard_done(CODE, NULL);
	CHECK_STR(fakeCfgGet("grant_minutes", ""), "15");
	CHECK(fakeCfgGet("bedtime_until", "")[0] != '\0');
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);

	// only bedtime refusing: the day's grant is left alone
	reset();
	setCfg("limit", "30");
	setCfg("bedtime", "21:00");
	setCfg("code", CODE);
	today_played = 10 * 60;
	fake_now = MONDAY + 22 * HOUR;
	CHECK(plugin.init(&fake) == 0);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	confirm_yes(NULL);
	keyboard_done(CODE, NULL);
	CHECK_STR(fakeCfgGet("grant_minutes", "none"), "none");
	// without a code, a toast
	reset();
	setCfg("bedtime", "21:00");
	fake_now = MONDAY + 22 * HOUR;
	CHECK(plugin.init(&fake) == 0);
	CHECK(launch(GAME, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CANCEL);
	CHECK_STR(last_toast, "It's bedtime");
	CHECK(!confirm_yes);
}

static MosaicScreenView view(const char* screen) {
	MosaicScreenView v = { sizeof(v), { 0, 0, 640, 480 }, { 520, 20, 100, 60 }, { 0, 0, 0, 0 }, screen };
	return v;
}

static void testBarPlugin(void) {
	reset();
	setCfg("limit", "60");
	today_played = 15 * 60;
	fake_now = MONDAY + 12 * HOUR;
	CHECK(plugin.init(&fake) == 0);
	MosaicScreenView v = view("gamelist");
	// nothing before the first look
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 0);
	plugin.tick(1000);

	// on the root list: the track, and a quarter filled in the accent
	CHECK(plugin.overlay_render(NULL, &v) == 0);
	CHECK(fill_count == 2);
	CHECK(fills[0].x == 0 && fills[0].y == 0 && fills[0].w == 640 && fills[0].h == 4);
	CHECK(fill_alpha[0] == BAR_TRACK_ALPHA);
	CHECK(fills[1].w == 160 && fills[1].h == 4);
	CHECK(fill_rgb[1] == 0x112233 && fill_alpha[1] == 255);
	CHECK_STR(last_log, "bar over gamelist at 0,0 640x4: 160 px of 900 seconds against 60 minutes");

	// not in a folder, or over other screens
	fill_count = 0;
	path_now = "/mnt/SDCARD/Roms/Game Boy (GB)";
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 0);
	path_now = "/mnt/SDCARD";
	v = view("quickmenu");
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 0);

	// with a home screen, only there
	home_id = "home-dashboard";
	v = view("gamelist");
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 0);
	v = view("home-dashboard");
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 2);

	// a change in the fill alone redraws
	int before = redraws;
	today_played += 20;
	plugin.tick(2000);
	CHECK(redraws == before + 1);

	// the warning colour at Warn at
	today_played = 52 * 60;
	plugin.tick(3000);
	fill_count = 0;
	CHECK(plugin.overlay_render(NULL, &v) == 0);
	CHECK(fill_rgb[1] == WARN_RGB);
	CHECK(fills[1].w == 554);
	CHECK_STR(last_log, "bar over home-dashboard at 0,0 640x4: 554 px of 3120 seconds against 60 minutes, warning");

	// only as tall as the room above the status pill
	v.status.y = 2;
	fill_count = 0;
	CHECK(plugin.overlay_render(NULL, &v) == 0);
	CHECK(fills[0].h == 2);

	// off by its setting
	reset();
	setCfg("limit", "60");
	setCfg("bar", "off");
	CHECK(plugin.init(&fake) == 0);
	plugin.tick(1000);
	v = view("gamelist");
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 0);

	// no limit today, or an unknown history: no bar
	reset();
	setCfg("bedtime", "21:00");
	CHECK(plugin.init(&fake) == 0);
	plugin.tick(1000);
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 0);
	reset();
	setCfg("limit", "60");
	today_played = MOSAIC_PLAY_UNKNOWN;
	CHECK(plugin.init(&fake) == 0);
	plugin.tick(1000);
	CHECK(plugin.overlay_render(NULL, &v) == 0 && fill_count == 0);

	// an older launcher without overlays
	reset();
	setCfg("limit", "60");
	fake.open_screen = NULL;
	CHECK(plugin.init(&fake) == 0);
	CHECK(logged("budget bar off"));
}

int main(void) {
	setenv("TZ", "UTC0", 1);
	tzset();
	testParseMinutes();
	testDayKey();
	testGrants();
	testSecondsLeft();
	testState();
	testFormat();
	testSchedules();
	testClock();
	testWindow();
	testBar();
	testCode();
	testOffByDefault();
	testNoHistoryCall();
	testUnderAndOver();
	testGrantExpires();
	testNoCode();
	testUnknownHistory();
	testReply();
	testService();
	testWeekend();
	testInitLogs();
	testBedtime();
	testBedtimeAndLimit();
	testBarPlugin();
	CHECK_DONE();
}
