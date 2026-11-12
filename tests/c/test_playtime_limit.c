// The play time limit plugin's model, and the plugin against a fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/playtime-limit/playtime_limit.c"

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

static void fakeLog(const char* id, const char* m) {
	snprintf(last_log, sizeof(last_log), "%s", m);
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

static MosaicHost fake;

static void reset(void) {
	key_count = 0;
	today_played = 0;
	redraws = logs = code_logged = 0;
	last_log[0] = last_toast[0] = confirm_message[0] = drawn[0] = '\0';
	confirm_yes = NULL;
	keyboard_done = NULL;
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
	plugin.tick(1500);
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

int main(void) {
	testParseMinutes();
	testDayKey();
	testGrants();
	testSecondsLeft();
	testState();
	testFormat();
	testCode();
	testOffByDefault();
	testNoHistoryCall();
	testUnderAndOver();
	testGrantExpires();
	testNoCode();
	testUnknownHistory();
	CHECK_DONE();
}
