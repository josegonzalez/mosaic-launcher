// The battery logger: its model on its own, then the plugin against a fake
// host, from the reading in pre_launch to the row post_return writes.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "fakeproviders.h"

#define BL_START_FILE TEST_DIR "/battery-logger.start"
#include "../../plugins/battery-logger/battery_logger.c"

#define CARD "/mnt/SDCARD"
#define USERDATA TEST_DIR "/battery-userdata"
#define CSV USERDATA "/launcher/battery-logger.csv"
#define OLD USERDATA "/launcher/battery-logger.old.csv"

static int level = 80, charging = 0;
static long long mono = 1000;
static char cfg_min[8] = "", cfg_max[8] = "", cfg_charged[8] = "";
static char logged[256];

static void fakeLog(const char* id, const char* m) { (void)id; snprintf(logged, sizeof(logged), "%s", m); }
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "min_minutes") && cfg_min[0]) return cfg_min;
	if (!strcmp(key, "max_kb") && cfg_max[0]) return cfg_max;
	if (!strcmp(key, "charged") && cfg_charged[0]) return cfg_charged;
	return fallback;
}
static int fakeLevel(void) { return level; }
static int fakeCharging(void) { return charging; }
static long long fakeMono(void) { return mono; }

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = USERDATA,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.battery_level = fakeLevel,
	.is_charging = fakeCharging,
	.event_value = fakeEventValue,
	.provide = fakeProvide,
	.call = fakeCall,
};

static char* readAll(const char* path) {
	static char buf[8192];
	buf[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return NULL;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static void writeAll_(const char* path, const char* text, size_t len) {
	FILE* f = fopen(path, "w");
	fwrite(text, 1, len, f);
	fclose(f);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static void testReadings(void) {
	CHECK(BL_validReading(1, 0));
	CHECK(BL_validReading(100, 1));
	CHECK(!BL_validReading(0, 0));   // a failed sysfs read
	CHECK(!BL_validReading(-1, -1)); // no battery
	CHECK(!BL_validReading(101, 0));
	CHECK(!BL_validReading(50, -1));
	CHECK(!BL_validReading(50, 2));
}

static void testStart(void) {
	BLStart s = { .mono = 1234, .wall = 1700000000, .level = 77, .charging = 1 };
	snprintf(s.path, sizeof(s.path), "%s", CARD "/Roms/Game Boy (GB)/Tetris, \"DX\".gb");
	char line[2048];
	CHECK(BL_formatStart(&s, line, sizeof(line)) == 0);
	BLStart back;
	CHECK(BL_parseStart(line, &back) == 0);
	CHECK(back.mono == 1234 && back.wall == 1700000000 && back.level == 77 && back.charging == 1);
	CHECK_STR(back.path, s.path);

	CHECK(BL_formatStart(&s, line, 10) == -1);
	snprintf(s.path, sizeof(s.path), "bad\tpath");
	CHECK(BL_formatStart(&s, line, sizeof(line)) == -1);

	CHECK(BL_parseStart("", &back) == -1);
	CHECK(BL_parseStart("2\t1\t1\t50\t0\t/a\n", &back) == -1);    // another version
	CHECK(BL_parseStart("1\t1\t1\t0\t0\t/a\n", &back) == -1);     // bad level
	CHECK(BL_parseStart("1\t1\t1\t50\t3\t/a\n", &back) == -1);    // bad charging
	CHECK(BL_parseStart("1\t-1\t1\t50\t0\t/a\n", &back) == -1);   // bad clock
	CHECK(BL_parseStart("1\t1\t1\t50\t0\t\n", &back) == -1);      // no path
	CHECK(BL_parseStart("1\t1\t1\t50\t0\t/a\tb\n", &back) == -1); // a tab in the path
	CHECK(BL_parseStart("1\t1\t1\t5x\t0\t/a\n", &back) == -1);
	CHECK(BL_parseStart("1\t1\t1\t50\t0\t/a", &back) == 0); // cut before its newline is still whole
}

static void testSession(void) {
	BLStart s = { .mono = 100, .wall = 0, .level = 80, .charging = 0, .path = "/a.gb" };
	BLSession out;
	CHECK(BL_session(&s, 70, 0, 100 + 3600, 60, 0, &out) == BL_OK);
	CHECK(out.seconds == 3600 && out.end_level == 70 && !out.charged);
	CHECK(BL_session(&s, 0, 0, 3700, 60, 0, &out) == BL_BAD_READING);
	CHECK(BL_session(&s, 70, -1, 3700, 60, 0, &out) == BL_BAD_READING);
	CHECK(BL_session(&s, 70, 0, 99, 60, 0, &out) == BL_BAD_TIME);
	CHECK(BL_session(&s, 80, 0, 159, 60, 0, &out) == BL_TOO_SHORT);
	CHECK(BL_session(&s, 80, 0, 100, 0, 0, &out) == BL_TOO_SHORT); // no time at all
	CHECK(BL_session(&s, 80, 0, 101, 0, 0, &out) == BL_OK);
	CHECK(BL_session(&s, 70, 1, 3700, 60, 0, &out) == BL_CHARGED); // charging on return
	CHECK(BL_session(&s, 85, 0, 3700, 60, 0, &out) == BL_CHARGED); // charged and unplugged
	CHECK(BL_session(&s, 85, 0, 3700, 60, 1, &out) == BL_OK && out.charged);
	s.charging = 1;
	CHECK(BL_session(&s, 70, 0, 3700, 60, 0, &out) == BL_CHARGED); // charging at launch
	s.level = 0;
	CHECK(BL_session(&s, 70, 0, 3700, 60, 1, &out) == BL_BAD_READING);
}

static void testFields(void) {
	char out[64];
	BL_gameName("/r/Game Boy (GB)/Tetris (USA).gb", out, sizeof(out));
	CHECK_STR(out, "Tetris (USA)");
	BL_gameName("/r/x/.hidden", out, sizeof(out));
	CHECK_STR(out, ".hidden");
	BL_gameName("Plain", out, sizeof(out));
	CHECK_STR(out, "Plain");
	BL_gameName("/r/Long name.gb", out, 5);
	CHECK_STR(out, "Long");

	CHECK(BL_csvField("Tetris", out, sizeof(out)) == 0);
	CHECK_STR(out, "Tetris");
	CHECK(BL_csvField("Mario, Luigi", out, sizeof(out)) == 0);
	CHECK_STR(out, "\"Mario, Luigi\"");
	CHECK(BL_csvField("The \"Best\"", out, sizeof(out)) == 0);
	CHECK_STR(out, "\"The \"\"Best\"\"\"");
	CHECK(BL_csvField("two\nlines\r", out, sizeof(out)) == 0);
	CHECK_STR(out, "\"two lines \"");
	CHECK(BL_csvField(" padded", out, sizeof(out)) == 0);
	CHECK_STR(out, "\" padded\"");
	CHECK(BL_csvField("", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(BL_csvField("abc", out, 4) == 0);
	CHECK(BL_csvField("abcd", out, 4) == -1);
	CHECK(BL_csvField("a,b", out, 5) == -1); // needs 6 with quotes
	CHECK(BL_csvField("a,b", out, 6) == 0);
}

static void testRow(void) {
	BLSession s = { .start = { .mono = 0, .wall = 1700000000, .level = 80, .charging = 0,
		.path = CARD "/Roms/Game Boy (GB)/Mario, \"Super\" Land.gb" }, .seconds = 2700, .end_level = 74 };
	char row[BL_ROW_LEN];
	CHECK(BL_formatRow(&s, CARD, row, sizeof(row)) == 0);
	CHECK_STR(row, "2023-11-14 22:13:20,\"Mario, \"\"Super\"\" Land\",\"Roms/Game Boy (GB)/Mario, \"\"Super\"\" Land.gb\","
		"2700,80,74,6,8.0,no\n");

	s.seconds = 7 * 60;
	s.end_level = 79; // 1% in 7 minutes is 8.57 an hour
	CHECK(BL_formatRow(&s, CARD, row, sizeof(row)) == 0);
	CHECK(strstr(row, ",420,80,79,1,8.6,no\n") != NULL);

	s.end_level = 80; // nothing used
	CHECK(BL_formatRow(&s, CARD, row, sizeof(row)) == 0);
	CHECK(strstr(row, ",420,80,80,0,0.0,no\n") != NULL);

	s.charged = 1;
	s.end_level = 90;
	snprintf(s.start.path, sizeof(s.start.path), "/elsewhere/a.gb");
	CHECK(BL_formatRow(&s, CARD, row, sizeof(row)) == 0);
	CHECK(strstr(row, ",a,/elsewhere/a.gb,420,80,90,,,yes\n") != NULL);

	CHECK(BL_formatRow(&s, CARD, row, 20) == -1);
}

static void testAppend(void) {
	const char* path = TEST_DIR "/bl-append.csv";
	const char* old = TEST_DIR "/bl-append.old.csv";
	unlink(path);
	unlink(old);

	CHECK(BL_append(path, old, "r1\n", 1024) == 0);
	CHECK_STR(readAll(path), BL_HEADER "r1\n");
	CHECK(BL_append(path, old, "r2\n", 1024) == 0);
	CHECK_STR(readAll(path), BL_HEADER "r1\nr2\n");

	// a power cut mid-row leaves it without a newline, or with zeros
	char text[512];
	int n = snprintf(text, sizeof(text), "%sr1\nr2\npart", BL_HEADER);
	writeAll_(path, text, (size_t)n);
	CHECK(BL_append(path, old, "r3\n", 1024) == 0);
	CHECK_STR(readAll(path), BL_HEADER "r1\nr2\nr3\n");
	n = snprintf(text, sizeof(text), "%sr1\n", BL_HEADER);
	memset(text + n, 0, 4);
	writeAll_(path, text, (size_t)n + 4);
	CHECK(BL_append(path, old, "r2\n", 1024) == 0);
	CHECK_STR(readAll(path), BL_HEADER "r1\nr2\n");

	// a header cut short starts over
	writeAll_(path, "date,ga", 7);
	CHECK(BL_append(path, old, "r1\n", 1024) == 0);
	CHECK_STR(readAll(path), BL_HEADER "r1\n");

	// past max_bytes the file moves to old and a new one starts
	long max = (long)strlen(BL_HEADER) + 6;
	unlink(path);
	CHECK(BL_append(path, old, "r1\n", max) == 0);
	CHECK(BL_append(path, old, "r2\n", max) == 0);
	CHECK(!exists(old));
	CHECK(BL_append(path, old, "r3\n", max) == 0);
	CHECK_STR(readAll(old), BL_HEADER "r1\nr2\n");
	CHECK_STR(readAll(path), BL_HEADER "r3\n");
	CHECK(BL_append(path, old, "r4\n", max) == 0);
	CHECK(BL_append(path, old, "r5\n", max) == 0);
	CHECK_STR(readAll(old), BL_HEADER "r3\nr4\n"); // the older one is replaced
	CHECK_STR(readAll(path), BL_HEADER "r5\n");

	// a header alone never moves, however small max_bytes is
	unlink(path);
	CHECK(BL_append(path, old, "r1\n", 1) == 0);
	CHECK_STR(readAll(path), BL_HEADER "r1\n");

	CHECK(BL_append(TEST_DIR "/no-such-dir/x.csv", old, "r1\n", 1024) == -1);
}

static void reset(void) {
	level = 80;
	charging = 0;
	mono = 1000;
	cfg_min[0] = cfg_max[0] = cfg_charged[0] = '\0';
	logged[0] = '\0';
	unlink(CSV);
	unlink(OLD);
	unlink(BL_START_FILE);
	fake.battery_level = fakeLevel;
	fake.is_charging = fakeCharging;
	fake.size = sizeof(MosaicHost);
	mono_now = fakeMono;
	CHECK(plugin.init(&fake) == 0);
}

static void play(const char* path, int type, int end_level, int end_charging, long long seconds) {
	MosaicLaunch launch = { .path = path, .type = type };
	CHECK(plugin.pre_launch(&launch) == MOSAIC_LAUNCH_CONTINUE);
	level = end_level;
	charging = end_charging;
	mono += seconds;
	plugin.post_return();
}

#define TETRIS CARD "/Roms/Game Boy (GB)/Tetris.gb"

static void testPlugin(void) {
	mkdir(USERDATA, 0755);
	mkdir(USERDATA "/launcher", 0755);

	reset();
	play(TETRIS, MOSAIC_ENTRY_ROM, 74, 0, 45 * 60);
	char* csv = readAll(CSV);
	CHECK(csv && !strncmp(csv, BL_HEADER, strlen(BL_HEADER)));
	CHECK(csv && strstr(csv, ",Tetris,Roms/Game Boy (GB)/Tetris.gb,2700,80,74,6,8.0,no\n") != NULL);
	CHECK(!exists(BL_START_FILE));

	// a second return without a launch logs nothing more
	plugin.post_return();
	CHECK_STR(readAll(CSV), csv);

	// paks aren't logged, and drop a start a cancelled launch left
	reset();
	MosaicLaunch game = { .path = TETRIS, .type = MOSAIC_ENTRY_ROM };
	plugin.pre_launch(&game);
	CHECK(exists(BL_START_FILE));
	play(CARD "/Tools/test/Files.pak", MOSAIC_ENTRY_PAK, 70, 0, 3600);
	CHECK(!exists(CSV));
	CHECK(!exists(BL_START_FILE));

	// bad readings before or after
	reset();
	level = 0;
	play(TETRIS, MOSAIC_ENTRY_ROM, 70, 0, 3600);
	CHECK(!exists(CSV));
	CHECK(strstr(logged, "before it was bad") != NULL);
	reset();
	play(TETRIS, MOSAIC_ENTRY_ROM, 0, 0, 3600);
	CHECK(!exists(CSV));
	CHECK(strstr(logged, "after it was bad") != NULL);

	// too short, by the default and by the setting
	reset();
	play(TETRIS, MOSAIC_ENTRY_ROM, 80, 0, 59);
	CHECK(!exists(CSV));
	reset();
	snprintf(cfg_min, sizeof(cfg_min), "5");
	play(TETRIS, MOSAIC_ENTRY_ROM, 79, 0, 4 * 60);
	CHECK(!exists(CSV));
	reset();
	snprintf(cfg_min, sizeof(cfg_min), "0");
	play(TETRIS, MOSAIC_ENTRY_ROM, 80, 0, 30);
	CHECK(exists(CSV));

	// charged sessions are skipped, or logged with the setting
	reset();
	charging = 1;
	play(TETRIS, MOSAIC_ENTRY_ROM, 90, 1, 3600);
	CHECK(!exists(CSV));
	CHECK(strstr(logged, "charged") != NULL);
	reset();
	snprintf(cfg_charged, sizeof(cfg_charged), "on");
	play(TETRIS, MOSAIC_ENTRY_ROM, 90, 0, 3600);
	csv = readAll(CSV);
	CHECK(csv && strstr(csv, ",3600,80,90,,,yes\n") != NULL);

	// a small max_kb rotates the log
	reset();
	snprintf(cfg_max, sizeof(cfg_max), "1");
	for (int i = 0; i < 20; i++) play(TETRIS, MOSAIC_ENTRY_ROM, 80, 0, 3600);
	CHECK(exists(OLD));
	struct stat st;
	CHECK(stat(CSV, &st) == 0 && st.st_size <= 1024);

	// unsupported: no battery, or a launcher without the calls
	reset();
	level = -1;
	charging = -1;
	CHECK(plugin.init(&fake) == 0);
	CHECK(strstr(logged, "unsupported") != NULL);
	play(TETRIS, MOSAIC_ENTRY_ROM, -1, -1, 3600);
	CHECK(!exists(BL_START_FILE));
	CHECK(!exists(CSV));
	reset();
	fake.size = offsetof(MosaicHost, battery_level);
	CHECK(plugin.init(&fake) == 0);
	CHECK(strstr(logged, "unsupported") != NULL);
	play(TETRIS, MOSAIC_ENTRY_ROM, 70, 0, 3600);
	CHECK(!exists(CSV));
}

static void testHistory(void) {
	// rows as BL_formatRow writes them, in UTC
	BLLogged row;
	CHECK(BL_parseRow("2026-11-08 14:03:00,Tetris,Roms/Game Boy (GB)/Tetris.gb,2700,80,74,6,8.0,no\n", &row) == 0);
	CHECK(row.start == 1794146580 && row.seconds == 2700 && row.start_level == 80 && row.end_level == 74 && !row.charged);
	CHECK(BL_parseRow("2026-11-08 15:00:00,\"Mario, \"\"Bros\"\"\",\"Roms/NES/Mario, Bros.nes\",3600,80,90,,,yes", &row) == 0);
	CHECK(row.start == 1794150000 && row.end_level == 90 && row.charged);
	CHECK(BL_parseRow(BL_HEADER, &row) == -1);
	CHECK(BL_parseRow("2026-11-08 14:03:00,Tetris,x,2700,80,74,6,8.0\n", &row) == -1);
	CHECK(BL_parseRow("2026-11-08 14:03:00,Tetris,x,2700,180,74,6,8.0,no\n", &row) == -1);
	CHECK(BL_parseRow("2026-11-08,Tetris,x,2700,80,74,6,8.0,no\n", &row) == -1);
	CHECK(BL_parseRow("", &row) == -1);

	// kept in start order, the last BL_HISTORY_MAX
	BLHistory h;
	BL_historyInit(&h);
	CHECK(BL_historyAdd(&h, BL_HEADER "2026-11-08 15:00:00,B,b,60,50,40,10,600.0,no\n"
		"2026-11-08 14:00:00,A,a,120,90,80,10,300.0,no\nbroken\n") == 2);
	CHECK(h.count == 2 && h.sessions[0].seconds == 120 && h.sessions[1].seconds == 60);
	char out[MOSAIC_CALL_MAX];
	CHECK(BL_reply(&h, 0, out, sizeof(out)) == 0);
	CHECK_STR(out, "count=2\nseconds=180\nsession.0=1794146400,120,90,80,0\nsession.1=1794150000,60,50,40,0");
	CHECK(BL_reply(&h, 1, out, sizeof(out)) == 0);
	CHECK_STR(out, "count=1\nseconds=60\nsession.0=1794150000,60,50,40,0");
	CHECK(BL_reply(&h, 5, out, sizeof(out)) == 0);
	CHECK(!strncmp(out, "count=2\n", 8));
	CHECK(BL_reply(&h, 0, out, 40) == -1);
	CHECK_STR(out, "");
	BL_historyInit(&h);
	CHECK(BL_reply(&h, 0, out, sizeof(out)) == 0);
	CHECK_STR(out, "count=0\nseconds=0");
	// every one of the most, which fits a call's reply
	char text[BL_HISTORY_MAX * 2 * 80];
	size_t used = 0;
	for (int i = 0; i < BL_HISTORY_MAX * 2; i++) {
		used += (size_t)snprintf(text + used, sizeof(text) - used, "2026-11-%02d %02d:00:00,G,g,%i,100,100,0,0.0,no\n",
			1 + i / 24, i % 24, 100000 + i);
	}
	CHECK(BL_historyAdd(&h, text) == BL_HISTORY_MAX * 2);
	CHECK(h.count == BL_HISTORY_MAX && h.sessions[0].seconds == 100000 + BL_HISTORY_MAX);
	CHECK(BL_reply(&h, 0, out, sizeof(out)) == 0);

	// through the plugin, from both logs, read again when one changes
	reset();
	CHECK(fakeCall("battery.history", NULL, out, sizeof(out)) > 0);
	CHECK_STR(out, "count=0\nseconds=0");
	const char* old_log = BL_HEADER "2026-11-08 14:00:00,A,a,120,90,80,10,300.0,no\n";
	const char* new_log = BL_HEADER "2026-11-08 15:00:00,B,b,60,50,40,10,600.0,no\n";
	writeAll_(OLD, old_log, strlen(old_log));
	writeAll_(CSV, new_log, strlen(new_log));
	CHECK(fakeCall("battery.history", "", out, sizeof(out)) > 0);
	CHECK_STR(out, "count=2\nseconds=180\nsession.0=1794146400,120,90,80,0\nsession.1=1794150000,60,50,40,0");
	play(TETRIS, MOSAIC_ENTRY_ROM, 74, 0, 45 * 60);
	CHECK(fakeCall("battery.history", "count=1", out, sizeof(out)) > 0);
	CHECK(!strncmp(out, "count=1\nseconds=2700\nsession.0=", 31));
	CHECK(fakeCall("battery.history", "count=0", out, sizeof(out)) == MOSAIC_CALL_FAILED);
	CHECK(fakeCall("battery.history", "count=many", out, sizeof(out)) == MOSAIC_CALL_FAILED);
	// unsupported platforms still give what was logged
	level = -1;
	CHECK(plugin.init(&fake) == 0);
	CHECK(fakeCall("battery.history", NULL, out, sizeof(out)) > 0);
	CHECK(!strncmp(out, "count=3\n", 8));
	level = 80;
}

int main(void) {
	setenv("TZ", "UTC0", 1);
	tzset();
	mkdir(TEST_DIR, 0755);
	testReadings();
	testStart();
	testSession();
	testFields();
	testRow();
	testAppend();
	testPlugin();
	testHistory();
	CHECK(mosaic_plugin_get(&fake) == &plugin);
	CHECK_DONE();
}
