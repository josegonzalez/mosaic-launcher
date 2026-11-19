// The system info plugin: its model on its own, then the plugin against a
// fake host on a fixture card.

#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "words.h"
#include "../../plugins/system-info/system_info.c"
#include "fakeproviders.h"

#define CARD TEST_SDCARD_PATH
#define USERDATA CARD "/.userdata/desktop"

///////////////////////////////////////
// the model

static void testFormats(void) {
	char out[SI_TEXT_MAX];
	Si_formatSize(0, out, sizeof(out));
	CHECK_STR(out, "0.0 KB");
	Si_formatSize(512ULL * 1024, out, sizeof(out));
	CHECK_STR(out, "512 KB");
	Si_formatSize(1023ULL * 1024, out, sizeof(out));
	CHECK_STR(out, "1.0 MB");
	Si_formatSize(12900ULL * 1024 * 1024, out, sizeof(out));
	CHECK_STR(out, "12.6 GB");
	Si_formatSize(2ULL * 1024 * 1024 * 1024 * 1024, out, sizeof(out));
	CHECK_STR(out, "2.0 TB");

	Si_formatStorage(12ULL << 30, 59ULL << 30, out, sizeof(out));
	CHECK_STR(out, "12.0 GB free of 59.0 GB");
	Si_formatStorage(0, 0, out, sizeof(out));
	CHECK_STR(out, "unavailable");
	Si_formatMemory(120ULL << 20, 1ULL << 30, out, sizeof(out));
	CHECK_STR(out, "120 MB free of 1.0 GB");
	Si_formatMemory(0, 0, out, sizeof(out));
	CHECK_STR(out, "unavailable");

	Si_formatUptime(-1, out, sizeof(out));
	CHECK_STR(out, "unavailable");
	Si_formatUptime(59, out, sizeof(out));
	CHECK_STR(out, "0m");
	Si_formatUptime(12 * 60 + 5, out, sizeof(out));
	CHECK_STR(out, "12m");
	Si_formatUptime(4 * 3600 + 12 * 60, out, sizeof(out));
	CHECK_STR(out, "4h 12m");
	Si_formatUptime(3 * 86400 + 4 * 3600 + 12 * 60 + 59, out, sizeof(out));
	CHECK_STR(out, "3d 4h 12m");
	Si_formatUptime(86400, out, sizeof(out));
	CHECK_STR(out, "1d 0h 0m");

	MosaicWifi w;
	memset(&w, 0, sizeof(w));
	w.size = sizeof(w);
	Si_formatWifi(0, -1, -1, &w, out, sizeof(out));
	CHECK_STR(out, "unavailable");
	Si_formatWifi(0, -1, -1, NULL, out, sizeof(out));
	CHECK_STR(out, "unavailable");
	Si_formatWifi(1, 0, 0, &w, out, sizeof(out));
	CHECK_STR(out, "off");
	Si_formatWifi(1, 1, -1, &w, out, sizeof(out));
	CHECK_STR(out, "checking");
	Si_formatWifi(1, 1, 0, &w, out, sizeof(out));
	CHECK_STR(out, "not connected");
	snprintf(w.ssid, sizeof(w.ssid), "Home Net");
	snprintf(w.ip, sizeof(w.ip), "192.168.1.50");
	w.rssi = -65;
	w.bars = 2;
	Si_formatWifi(1, 1, 1, &w, out, sizeof(out));
	CHECK_STR(out, "Home Net, 192.168.1.50, -65 dBm");
	w.rssi = 0;
	w.ip[0] = '\0';
	Si_formatWifi(1, 1, 1, &w, out, sizeof(out));
	CHECK_STR(out, "Home Net, no address yet, 2 of 3 bars");
	w.bars = 0;
	w.ssid[0] = '\0';
	Si_formatWifi(1, 1, 1, &w, out, sizeof(out));
	CHECK_STR(out, "hidden network, no address yet");

	Si_formatBattery(-1, -1, out, sizeof(out));
	CHECK_STR(out, "unavailable");
	Si_formatBattery(85, 0, out, sizeof(out));
	CHECK_STR(out, "85%");
	Si_formatBattery(85, 1, out, sizeof(out));
	CHECK_STR(out, "85%, charging");
	Si_formatBattery(0, -1, out, sizeof(out));
	CHECK_STR(out, "0%");

	CHECK(Si_formatVersion("NextUI-20261013-0\n39a06de\n", out, sizeof(out)) == 0);
	CHECK_STR(out, "NextUI-20261013-0 (39a06de)");
	CHECK(Si_formatVersion("NextUI-20261013-0\r\n39a06de\r\n", out, sizeof(out)) == 0);
	CHECK_STR(out, "NextUI-20261013-0 (39a06de)");
	CHECK(Si_formatVersion("  NextUI-20261013-0  \n", out, sizeof(out)) == 0);
	CHECK_STR(out, "NextUI-20261013-0");
	CHECK(Si_formatVersion("", out, sizeof(out)) == -1);
	CHECK_STR(out, "unavailable");
	CHECK(Si_formatVersion(NULL, out, sizeof(out)) == -1);
	CHECK_STR(out, "unavailable");

	CHECK_STR(Si_label(SI_BATTERY), "Battery");
	CHECK_STR(Si_label(SI_VERSION), "Version");
	CHECK_STR(Si_label(SI_ROWS), "");
}

static void testProc(void) {
	unsigned long long total = 0, avail = 0;
	const char* modern = "MemTotal:        1015808 kB\nMemFree:          100000 kB\nMemAvailable:     512000 kB\n"
		"Buffers:           10000 kB\nCached:           200000 kB\n";
	CHECK(Si_parseMeminfo(modern, &total, &avail) == 0);
	CHECK(total == 1015808ULL * 1024);
	CHECK(avail == 512000ULL * 1024);
	// no MemAvailable: free plus buffers and cache
	const char* old = "MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 20 kB\nCached: 300 kB\n";
	CHECK(Si_parseMeminfo(old, &total, &avail) == 0);
	CHECK(total == 1000ULL * 1024 && avail == 420ULL * 1024);
	// more available than there is: capped
	CHECK(Si_parseMeminfo("MemTotal: 100 kB\nMemAvailable: 200 kB\n", &total, &avail) == 0);
	CHECK(avail == total);
	// a key that only starts like MemTotal isn't it
	CHECK(Si_parseMeminfo("MemTotalish: 5 kB\nMemFree: 1 kB\n", &total, &avail) == -1);
	CHECK(Si_parseMeminfo("MemTotal: 100 kB\n", &total, &avail) == -1);
	CHECK(Si_parseMeminfo("", &total, &avail) == -1);
	CHECK(Si_parseMeminfo(NULL, &total, &avail) == -1);

	CHECK(Si_parseUptime("12345.67 54321.00\n") == 12345);
	CHECK(Si_parseUptime("0.5 1.0") == 0);
	CHECK(Si_parseUptime("") == -1);
	CHECK(Si_parseUptime("x") == -1);
	CHECK(Si_parseUptime("-3 1") == -1);
	CHECK(Si_parseUptime(NULL) == -1);
}

static void testAddrs(void) {
	SiAddrs a;
	char out[SI_TEXT_MAX];
	Si_addrsInit(&a);
	Si_formatAddrs(&a, out, sizeof(out));
	CHECK_STR(out, "none");
	CHECK(!Si_addrsAdd(&a, "lo", "127.0.0.1"));
	CHECK(!Si_addrsAdd(&a, "lo0", "::1"));
	CHECK(!Si_addrsAdd(&a, "eth9", "127.0.1.1"));
	CHECK(Si_addrsAdd(&a, "wlan0", "192.168.1.5"));
	CHECK(!Si_addrsAdd(&a, "wlan0", "192.168.1.6")); // one per interface
	CHECK(!Si_addrsAdd(&a, "wlan0", "2001:db8::5"));
	CHECK(!Si_addrsAdd(&a, "eth0", "fe80::1"));
	CHECK(!Si_addrsAdd(&a, "eth0", "FE80::1"));
	CHECK(Si_addrsAdd(&a, "eth0", "2001:db8::1"));
	CHECK(!Si_addrsAdd(&a, "eth1", ""));
	CHECK(!Si_addrsAdd(&a, NULL, "10.0.0.1"));
	Si_formatAddrs(&a, out, sizeof(out));
	CHECK_STR(out, "192.168.1.5 (wlan0), 2001:db8::1 (eth0)");
	CHECK(Si_addrsAdd(&a, "a", "10.0.0.1"));
	CHECK(Si_addrsAdd(&a, "b", "10.0.0.2"));
	CHECK(!Si_addrsAdd(&a, "c", "10.0.0.3")); // full
	CHECK(a.count == SI_ADDRS_MAX);
	// cut short, still a string
	char tiny[12];
	Si_formatAddrs(&a, tiny, sizeof(tiny));
	CHECK(strlen(tiny) == sizeof(tiny) - 1);
}

static void testRows(void) {
	SiSession s;
	CHECK(Si_parseRow("1762610585,3600,90,80,0\n", &s) == 0);
	CHECK(s.start == 1762610585 && s.seconds == 3600 && s.start_level == 90 && s.end_level == 80);
	CHECK(Si_parseRow("1762610585,60,50,55,1", &s) == 0);
	CHECK(s.seconds == 60 && s.start_level == 50 && s.end_level == 55);
	CHECK(Si_parseRow("0,60,50,49,0\r\n", &s) == 0);
	// not sessions
	CHECK(Si_parseRow("", &s) == -1);
	CHECK(Si_parseRow(NULL, &s) == -1);
	CHECK(Si_parseRow("1762610585,0,50,49,0", &s) == -1);
	CHECK(Si_parseRow("1762610585,60,101,49,0", &s) == -1);
	CHECK(Si_parseRow("1762610585,60,50,x,0", &s) == -1);
	CHECK(Si_parseRow("1762610585,60,50,49,2", &s) == -1);
	CHECK(Si_parseRow("1762610585,60,50", &s) == -1);
	CHECK(Si_parseRow("1762610585,60,50,49,0,7", &s) == -1);
	CHECK(Si_parseRow("-5,60,50,49,0", &s) == -1);
	CHECK(Si_parseRow("2026-11-08 14:03:05,A,B,60,50,49,1,60.0,no", &s) == -1);
}

// the values in the words they're given, then English again
static void testWords(void) {
	static const char* const words[] = { "%s free of %s", "%s libres de %s", "%lldh %lldm", "%lld h %lld min", "%lldm", "%lld min",
		"unavailable", "no disponible", "%i of 3 bars", "%i de 3 barras", "no address yet", "sin direcci\xc3\xb3n a\xc3\xban",
		"%i%%, charging", "%i%%, cargando", NULL };
	static const char* const plurals[] = { "Battery over the last %i game, %s of play",
		"Bater\xc3\xad" "a en la \xc3\xbaltima partida (%i), %s de juego",
		"Bater\xc3\xad" "a en las \xc3\xbaltimas %i partidas, %s de juego", NULL };
	fake_words = words;
	fake_plurals = plurals;
	SiWords set = { fakeTr, fakePlural };
	Si_setWords(&set);
	char out[SI_TEXT_MAX * 2];
	Si_formatStorage(12ULL << 30, 59ULL << 30, out, sizeof(out));
	CHECK_STR(out, "12.0 GB libres de 59.0 GB");
	Si_formatStorage(0, 0, out, sizeof(out));
	CHECK_STR(out, "no disponible");
	Si_formatUptime(4 * 3600 + 12 * 60, out, sizeof(out));
	CHECK_STR(out, "4 h 12 min");
	Si_formatBattery(80, 1, out, sizeof(out));
	CHECK_STR(out, "80%, cargando");
	MosaicWifi w;
	memset(&w, 0, sizeof(w));
	w.size = sizeof(w);
	snprintf(w.ssid, sizeof(w.ssid), "Home Net");
	w.bars = 2;
	Si_formatWifi(1, 1, 1, &w, out, sizeof(out));
	CHECK_STR(out, "Home Net, sin direcci\xc3\xb3n a\xc3\xban, 2 de 3 barras");
	SiHistory h;
	Si_historyInit(&h);
	Si_historyAdd(&h, "1000,300,100,70,0\n2000,100,50,40,0\n");
	Si_graphCaption(&h, out, sizeof(out));
	CHECK_STR(out, "Bater\xc3\xad" "a en las \xc3\xbaltimas 2 partidas, 6 min de juego");
	Si_setWords(NULL);
	Si_graphCaption(&h, out, sizeof(out));
	CHECK_STR(out, "Battery over the last 2 games, 6m of play");
	Si_formatBattery(80, 1, out, sizeof(out));
	CHECK_STR(out, "80%, charging");
}

static void testHistory(void) {
	SiHistory h;
	Si_historyInit(&h);
	char out[160];
	Si_graphCaption(&h, out, sizeof(out));
	CHECK_STR(out, "");
	int levels[8];
	CHECK(Si_graph(&h, 8, levels) == 0);

	// out of order and with a bad row: sorted by start
	const char* sessions = "2000,100,50,40,0\ngarbage\n1000,300,100,70,0\n";
	CHECK(Si_historyAdd(&h, sessions) == 2);
	CHECK(h.count == 2);
	CHECK(h.sessions[0].start_level == 100 && h.sessions[1].start_level == 50);
	CHECK(Si_historySeconds(&h) == 400);
	Si_graphCaption(&h, out, sizeof(out));
	CHECK_STR(out, "Battery over the last 2 games, 6m of play");

	// 4 columns of 100 play seconds each: three in A, one in B
	CHECK(Si_graph(&h, 4, levels) == 4);
	CHECK(levels[0] == 95); // 50 s into A: 100 - 30 * 50 / 300
	CHECK(levels[1] == 85);
	CHECK(levels[2] == 75);
	CHECK(levels[3] == 45); // 50 s into B
	CHECK(Si_graph(&h, 0, levels) == 0);

	// a charged session goes up
	Si_historyInit(&h);
	Si_historyAdd(&h, "1000,100,20,40,1\n");
	CHECK(Si_graph(&h, 2, levels) == 2);
	CHECK(levels[0] == 25 && levels[1] == 35);
	Si_graphCaption(&h, out, sizeof(out));
	CHECK_STR(out, "Battery over the last 1 game, 1m of play");

	// only the last SI_SESSIONS_MAX are kept, oldest dropped
	Si_historyInit(&h);
	char row[128];
	for (int i = SI_SESSIONS_MAX + 9; i >= 0; i--) {
		snprintf(row, sizeof(row), "%d,60,%d,1,0\n", 1000000 + i * 3600, 1 + i % 100);
		Si_historyAdd(&h, row);
	}
	CHECK(h.count == SI_SESSIONS_MAX);
	CHECK(h.sessions[0].start_level == 1 + 10);
	CHECK(h.sessions[SI_SESSIONS_MAX - 1].start_level == 1 + SI_SESSIONS_MAX + 9);
	for (int i = 1; i < h.count; i++) CHECK(h.sessions[i - 1].start < h.sessions[i].start);

	CHECK(Si_barHeight(0, 100) == 0);
	CHECK(Si_barHeight(-5, 100) == 0);
	CHECK(Si_barHeight(1, 10) == 1);
	CHECK(Si_barHeight(50, 101) == 51);
	CHECK(Si_barHeight(100, 90) == 90);
	CHECK(Si_barHeight(150, 90) == 90);
	CHECK(Si_barHeight(50, 0) == 0);
}

static int meet(const MosaicRect* a, const MosaicRect* b) {
	return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static void testLayout(void) {
	MosaicRect area = { 0, 0, 1024, 768 };
	MosaicRect status = { 800, 30, 194, 90 };
	MosaicRect hole = Si_hole(&status, NULL);
	SiLayout l;
	CHECK(Si_layout(&area, &hole, 3, 200, SI_ROWS, 150, 42, 30, &l) == 0);
	CHECK(l.title.x == 30 && l.title.y == 30 && l.title.h == 90 && l.title.w == 200 + 72);
	CHECK(l.rows[0].y == 150);
	for (int i = 0; i < SI_ROWS; i++) {
		CHECK(l.rows[i].w > 0);
		CHECK(!meet(&l.rows[i], &hole));
		if (i) CHECK(l.rows[i].y == l.rows[i - 1].y + 42);
	}
	CHECK(l.label_w == 150 + 60);
	CHECK(l.graph.w > 0 && l.caption.w > 0);
	CHECK(l.caption.y >= l.rows[SI_ROWS - 1].y + 42);
	CHECK(l.graph.y >= l.caption.y + 30);
	CHECK(l.graph.y + l.graph.h <= 768 - 130); // above the hints
	CHECK(!meet(&l.graph, &hole));

	// a long title stops short of the status pill
	CHECK(Si_layout(&area, &hole, 3, 2000, SI_ROWS, 150, 42, 30, &l) == 0);
	CHECK(l.title.x + l.title.w <= status.x - 30);

	// a taller hole pushes the rows down
	MosaicRect header = { 600, 30, 180, 150 };
	hole = Si_hole(&status, &header);
	CHECK(hole.x == 600 && hole.y == 30 && hole.w == 394 && hole.h == 150);
	CHECK(Si_layout(&area, &hole, 3, 200, SI_ROWS, 150, 42, 30, &l) == 0);
	CHECK(l.rows[0].y == 210);

	// small: rows that don't fit and no graph
	MosaicRect small = { 0, 0, 640, 480 };
	MosaicRect pill = { 500, 20, 120, 60 };
	CHECK(Si_layout(&small, &pill, 2, 100, SI_ROWS, 100, 40, 20, &l) == 0);
	CHECK(l.rows[0].w > 0);
	CHECK(l.rows[SI_ROWS - 1].w == 0);
	CHECK(l.graph.w == 0 && l.caption.w == 0);
	// at the smaller font everything fits
	CHECK(Si_layout(&small, &pill, 2, 100, SI_ROWS, 100, 24, 20, &l) == 0);
	CHECK(l.rows[SI_ROWS - 1].w > 0 && l.graph.w > 0);

	// nothing fits
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(Si_layout(&tiny, NULL, 2, 100, SI_ROWS, 100, 40, 20, &l) == -1);
	CHECK(Si_layout(&area, NULL, 0, 100, SI_ROWS, 100, 40, 20, &l) == -1);

	MosaicRect none = Si_hole(NULL, NULL);
	CHECK(none.w == 0);
	hole = Si_hole(NULL, &header);
	CHECK(hole.x == header.x && hole.w == header.w);
}

static void testDue(void) {
	unsigned long next = 0;
	CHECK(Si_due(&next, 5, 1000));
	CHECK(next == 1005);
	CHECK(!Si_due(&next, 1004, 1000));
	CHECK(Si_due(&next, 1005, 1000));
	CHECK(next == 2005);

	SiValues a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	CHECK(!Si_changed(&a, &b));
	snprintf(b.text[SI_UPTIME], SI_TEXT_MAX, "1m");
	CHECK(Si_changed(&a, &b));
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
};

static MosaicSurface screen_surface = { 1024, 768 };
static MosaicHost fake_host;
static MosaicScreenHooks registered;
static char registered_id[64], action_id[64], action_label[64], tile_id[64];
static void (*action_run)(const char*, void*);
static int bound_button = -1;
static char last_log[2048];
static char all_log[16384];
static int redraws = 0, fills_into_hole = 0, bars = 0, hints = 0;
static char opened_screen[64];
static const char* tile_setting = "on";
static const char* button_setting = "none";
static int level = 85, charging = 0;
static MosaicRect hole_now;

// battery-logger's battery.history, with two sessions
static int fakeHistory(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	snprintf(out, size, "count=2\nseconds=1200\nsession.0=1761991200,600,100,90,0\nsession.1=1762077600,600,80,70,0");
	return 0;
}

static void fakeLog(const char* id, const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
	size_t used = strlen(all_log);
	snprintf(all_log + used, sizeof(all_log) - used, "%s\n", message);
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (strcmp(key, "tile") == 0) return tile_setting;
	if (strcmp(key, "button") == 0) return button_setting;
	return fallback;
}

static void fakeRedraw(void) {
	redraws++;
}

static MosaicScreenHooks storage_screen, tester_screen;
static char current[64] = "gamelist";

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	if (!strcmp(id, STORAGE_ID)) storage_screen = *hooks;
	else if (!strcmp(id, INPUT_ID)) tester_screen = *hooks;
	else {
		snprintf(registered_id, sizeof(registered_id), "%s", id);
		registered = *hooks;
	}
	return 0;
}

static int fakeAction(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)data;
	(void)available;
	snprintf(action_id, sizeof(action_id), "%s", id);
	snprintf(action_label, sizeof(action_label), "%s", label);
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
	snprintf(opened_screen, sizeof(opened_screen), "%s", id);
	snprintf(current, sizeof(current), "%s", id);
	const MosaicScreenHooks* h = !strcmp(id, STORAGE_ID) ? &storage_screen : !strcmp(id, INPUT_ID) ? &tester_screen : &registered;
	return h->enter ? h->enter(h->data) : 0;
}

static const char* fakeCurrent(void) {
	return current;
}

static int fakeHoldSwitch(void) {
	return 0;
}

static const char* fakeFnName(int button) {
	return button == MOSAIC_BUTTON_FN3 ? "HOME" : "";
}

static int stick_left = 1; // a left stick, at stick_x and stick_y
static int stick_x = 12000, stick_y = -3000;
static int fakeStick(int which, int* x, int* y) {
	if (which != MOSAIC_STICK_LEFT || !stick_left) return -1;
	*x = stick_x;
	*y = stick_y;
	return 0;
}

static int fakeSetNames(void (*each)(const char*, void*), void* data) {
	(void)each;
	(void)data;
	return 0;
}

static int subscribed = 0;
static void (*card_changed)(const char*, const char*, void*);
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)name;
	(void)data;
	subscribed++;
	card_changed = on_event;
	return 0;
}

static int fakeThemeColor(int index) {
	return 0x101010 * index;
}
static int fakeScale(void) {
	return 3;
}
static int fakeBattery(void) {
	return level;
}
static int fakeCharging(void) {
	return charging;
}

// desktop's answers until a test changes them
static int wifi_supported = 0, wifi_enabled = MOSAIC_WIFI_UNAVAILABLE, wifi_rc = MOSAIC_WIFI_UNAVAILABLE;
static int fakeWifiSupported(void) {
	return wifi_supported;
}
static int fakeWifiEnabled(void) {
	return wifi_enabled;
}
static int fakeWifiConnection(MosaicWifi* out) {
	if (!out) return MOSAIC_WIFI_UNAVAILABLE;
	if (wifi_rc == 1) {
		snprintf(out->ssid, sizeof(out->ssid), "Home Net");
		snprintf(out->ip, sizeof(out->ip), "192.168.1.50");
		out->rssi = -65;
		out->bars = 2;
	}
	return wifi_rc;
}

static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)target;
	if (!rect || (hole_now.w > 0 && meet(rect, &hole_now))) fills_into_hole++;
	if (color.r == 0x20 && color.a == 255 && rect && rect->w == 6) bars++; // the accent colour, a bar wide
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
static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)target;
	(void)color;
	MosaicRect r = { x, y, fakeTextWidth(font, text) < max_width ? fakeTextWidth(font, text) : max_width, fakeTextHeight(font) };
	if (hole_now.w > 0 && meet(&r, &hole_now)) fills_into_hole++;
	return r.w;
}

static char hint_text[128];

static int fakeDrawHints(MosaicSurface* screen, int group, const MosaicHint* h, int count, MosaicRect* drawn) {
	(void)screen;
	(void)drawn;
	for (int i = 0; i < count; i++) {
		size_t used = strlen(hint_text);
		snprintf(hint_text + used, sizeof(hint_text) - used, "%s%i %s", group == MOSAIC_HINTS_LEFT ? "L:" : "R:", h[i].button,
			h[i].label);
	}
	if (group == MOSAIC_HINTS_LEFT && count == 1 && h[0].button == MOSAIC_BUTTON_B && !strcmp(h[0].label, "BACK"))
		hints++;
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
	fake_host.request_redraw = fakeRedraw;
	fake_host.register_action = fakeAction;
	fake_host.bind_button = fakeBind;
	fake_host.quick_tile = fakeTile;
	fake_host.register_screen = fakeRegister;
	fake_host.open_screen = fakeOpenScreen;
	fake_host.theme_color = fakeThemeColor;
	fake_host.screen_scale = fakeScale;
	fake_host.battery_level = fakeBattery;
	fake_host.is_charging = fakeCharging;
	fake_host.wifi_supported = fakeWifiSupported;
	fake_host.wifi_enabled = fakeWifiEnabled;
	fake_host.wifi_connection = fakeWifiConnection;
	fake_host.fill_rect = fakeFill;
	fake_host.fill_round_rect = fakeFillRound;
	fake_host.text_width = fakeTextWidth;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.draw_hints = fakeDrawHints;
	fake_host.current_screen = fakeCurrent;
	fake_host.hold_switch = fakeHoldSwitch;
	fake_host.fn_name = fakeFnName;
	fake_host.subscribe = fakeSubscribe;
	fake_host.set_names = fakeSetNames;
	fake_host.stick = fakeStick;
}

static void writeFile(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "'") == 0);
	CHECK(system("mkdir -p '" CARD "/.system' '" USERDATA "/launcher'") == 0);
	writeFile(CARD "/.system/version.txt", "NextUI-20261013-0\n39a06de\n");
}

static int logged(const char* text) {
	return strstr(all_log, text) != NULL;
}

static void renderOnce(void) {
	MosaicScreenView view = { sizeof(view), { 0, 0, 1024, 768 }, { 800, 30, 194, 90 }, { 0, 0, 0, 0 }, SCREEN_ID };
	hole_now = view.status;
	CHECK(registered.render(&screen_surface, &view, registered.data) == 0);
}

static void testPlugin(void) {
	makeCard();
	setupHost();
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 0);
	CHECK_STR(registered_id, SCREEN_ID);
	CHECK_STR(action_id, ACTION_ID);
	CHECK_STR(action_label, "System info");
	CHECK_STR(tile_id, ACTION_ID);
	CHECK(bound_button == -1);

	// nothing is read while it isn't shown
	redraws = 0;
	tick(5000);
	CHECK(redraws == 0);

	// no battery history yet
	all_log[0] = '\0';
	action_run(NULL, NULL);
	CHECK_STR(opened_screen, SCREEN_ID);
	CHECK(logged("opened, 0 games of battery history"));
	CHECK(logged("Battery: 85%;"));
	CHECK(logged("Wi-Fi: unavailable;"));
	CHECK(logged("Platform: desktop;"));
	CHECK(logged("Version: NextUI-20261013-0 (39a06de)"));
	CHECK(logged("Storage: ") && !logged("Storage: unavailable"));
	CHECK(redraws == 1);
	fills_into_hole = hints = 0;
	renderOnce();
	CHECK(fills_into_hole == 0);
	CHECK(hints == 1);
	CHECK(logged("no battery history"));

	// once a second, a redraw only when something changed
	redraws = 0;
	tick(10000);
	tick(10500);
	CHECK(redraws <= 1); // uptime may tick over
	int before = redraws;
	charging = 1;
	tick(10900);
	CHECK(redraws == before);
	tick(11000);
	CHECK(redraws == before + 1);
	CHECK(strstr(last_log, "Battery: 85%, charging;") != NULL);
	// Wi-Fi: checking until the host has looked, then the network, then off
	wifi_supported = 1;
	wifi_enabled = 1;
	tick(12000);
	CHECK(strstr(last_log, "Wi-Fi: checking;") != NULL);
	wifi_rc = 1;
	tick(13000);
	CHECK(strstr(last_log, "Wi-Fi: Home Net, 192.168.1.50, -65 dBm;") != NULL);
	wifi_enabled = 0;
	wifi_rc = 0;
	tick(14000);
	CHECK(strstr(last_log, "Wi-Fi: off;") != NULL);
	wifi_enabled = 1;
	tick(15000);
	CHECK(strstr(last_log, "Wi-Fi: not connected;") != NULL);
	wifi_supported = 0;
	wifi_enabled = wifi_rc = MOSAIC_WIFI_UNAVAILABLE;
	registered.exit(registered.data);
	CHECK_STR(last_log, "closed");
	redraws = 0;
	charging = 0;
	tick(20000);
	CHECK(redraws == 0);

	// with battery-logger's sessions, from battery.history
	fakeProvide("battery.history", fakeHistory);
	fake_host.call = fakeCall;
	fake_host.event_value = fakeEventValue;
	all_log[0] = '\0';
	action_run(NULL, NULL);
	CHECK(logged("opened, 2 games of battery history"));
	bars = fills_into_hole = 0;
	renderOnce();
	CHECK(fills_into_hole == 0);
	CHECK(bars > 0);
	CHECK(logged("graph of 2 games over "));
	CHECK(logged("columns, from 100% to 70%"));
	registered.exit(registered.data);

	// no battery and no version.txt: unavailable
	remove(CARD "/.system/version.txt");
	level = -1;
	all_log[0] = '\0';
	action_run(NULL, NULL);
	CHECK(logged("Battery: unavailable;"));
	CHECK(logged("Version: unavailable"));
	registered.exit(registered.data);
	level = 85;

	// a smaller screen: the graph gets a page of its own, which A moves
	// to and back from and B leaves
	MosaicScreenView view = { sizeof(view), { 0, 0, 640, 480 }, { 500, 20, 120, 60 }, { 0, 0, 0, 0 }, SCREEN_ID };
	MosaicInput press_a, press_b;
	memset(&press_a, 0, sizeof(press_a));
	memset(&press_b, 0, sizeof(press_b));
	press_a.pressed = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A);
	press_b.pressed = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	hole_now = view.status;
	fills_into_hole = 0;
	all_log[0] = '\0';
	action_run(NULL, NULL);
	hint_text[0] = '\0';
	CHECK(registered.render(&screen_surface, &view, registered.data) == 0);
	CHECK(fills_into_hole == 0);
	CHECK(logged("showing the info page"));
	CHECK(!logged("graph of"));
	CHECK(strstr(hint_text, "L:") && strstr(hint_text, " BACK") && strstr(hint_text, " GRAPH"));
	redraws = 0;
	CHECK(registered.input(&press_a, registered.data) == MOSAIC_SCREEN_STAY);
	CHECK(redraws == 1);
	hint_text[0] = '\0';
	bars = 0;
	CHECK(registered.render(&screen_surface, &view, registered.data) == 0);
	CHECK(logged("showing the graph's page"));
	CHECK(logged("graph of 2 games over "));
	CHECK(bars > 0);
	CHECK(strstr(hint_text, " INFO") != NULL);
	CHECK(fills_into_hole == 0);
	// B goes back to the info, then closes
	CHECK(registered.input(&press_b, registered.data) == MOSAIC_SCREEN_STAY);
	CHECK(registered.render(&screen_surface, &view, registered.data) == 0);
	CHECK_STR(last_log, "showing the info page");
	CHECK(registered.input(&press_b, registered.data) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);

	// on the big screen A does nothing and B closes
	all_log[0] = '\0';
	action_run(NULL, NULL);
	renderOnce();
	CHECK(logged("showing the graph under the rows"));
	redraws = 0;
	CHECK(registered.input(&press_a, registered.data) == MOSAIC_SCREEN_STAY);
	CHECK(redraws == 0);
	CHECK(registered.input(&press_b, registered.data) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
}

static void testHints(void) {
	MosaicHint left[2], right[2];
	int lc = -1, rc = -1;
	Si_hints(0, 0, left, &lc, right, &rc);
	CHECK(lc == 1 && rc == 2);
	CHECK(left[0].button == MOSAIC_BUTTON_B && !strcmp(left[0].label, "BACK"));
	CHECK(right[0].button == MOSAIC_BUTTON_X && !strcmp(right[0].label, "INPUT"));
	CHECK(right[1].button == MOSAIC_BUTTON_Y && !strcmp(right[1].label, "CARD"));
	Si_hints(1, 0, left, &lc, right, &rc);
	CHECK(lc == 2 && rc == 2);
	CHECK(left[0].button == MOSAIC_BUTTON_B && !strcmp(left[0].label, "BACK"));
	CHECK(left[1].button == MOSAIC_BUTTON_A && !strcmp(left[1].label, "GRAPH"));
	Si_hints(1, 1, left, &lc, right, &rc);
	CHECK(lc == 1 && rc == 1);
	CHECK(right[0].button == MOSAIC_BUTTON_A && !strcmp(right[0].label, "INFO"));

	// the graph's own page: the graph takes what's under the title
	MosaicRect area = { 0, 0, 640, 480 };
	SiLayout l;
	CHECK(Si_layout(&area, NULL, 2, 100, 0, 0, 0, 20, &l) == 0);
	CHECK(l.rows[0].w == 0);
	CHECK(l.caption.y == 100 && l.graph.y == 124);
	CHECK(l.graph.y + l.graph.h == 380);
	CHECK(l.graph.x == 44 && l.graph.w == 640 - 88);
}

static void testSettings(void) {
	setupHost();
	tile_setting = "off";
	button_setting = "L2";
	tile_id[0] = '\0';
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 0);
	CHECK_STR(tile_id, "");
	CHECK(bound_button == MOSAIC_BUTTON_L2);
	tile_setting = "on";
	button_setting = "none";

	// an older launcher without screens
	setupHost();
	fake_host.size = offsetof(MosaicHost, open_screen);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 1);
}

///////////////////////////////////////
// the Storage page's model

static void testStorageNames(void) {
	char out[ST_NAME_MAX];
	St_tag("Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, "GB");
	St_tag("01) Super Nintendo (SFC)", out, sizeof(out));
	CHECK_STR(out, "SFC");
	St_tag("Ports", out, sizeof(out));
	CHECK_STR(out, "Ports");
	St_tag("Odd (name) here", out, sizeof(out));
	CHECK_STR(out, "Odd (name) here");
	St_tag("Empty ()", out, sizeof(out));
	CHECK_STR(out, "Empty ()");

	St_name("01) Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, "Game Boy");
	St_name("Game Boy (GB)", out, sizeof(out));
	CHECK_STR(out, "Game Boy");
	St_name("Ports", out, sizeof(out));
	CHECK_STR(out, "Ports");
	St_name("(GB)", out, sizeof(out));
	CHECK_STR(out, "(GB)");
	St_name("2048 (P8)", out, sizeof(out));
	CHECK_STR(out, "2048");

	CHECK(St_stateTag("GB-gambatte", out, sizeof(out)) == 0);
	CHECK_STR(out, "GB");
	CHECK(St_stateTag("PS-pcsx_rearmed", out, sizeof(out)) == 0);
	CHECK_STR(out, "PS");
	CHECK(St_stateTag(".minui", out, sizeof(out)) == -1);
	CHECK(St_stateTag("-core", out, sizeof(out)) == -1);
	CHECK(St_stateTag("GB-", out, sizeof(out)) == -1);
}

static void testStorageRows(void) {
	static StTotals t;
	St_init(&t);
	CHECK(St_row(&t, "", "Nothing") == -1);
	CHECK(St_row(&t, "MD", NULL) == 0);
	CHECK(St_row(&t, "GB", "Game Boy") == 1);
	CHECK(St_row(&t, "MD", "Mega Drive") == 0);
	CHECK_STR(t.rows[0].name, "Mega Drive");
	CHECK(St_row(&t, "GB", "Game Boy Hacks") == 1);
	CHECK_STR(t.rows[1].name, "Game Boy");
	CHECK(St_row(&t, "GBA", NULL) == 2);
	t.rows[0].bytes[ST_GAMES] = 100;
	t.rows[1].bytes[ST_GAMES] = 50;
	t.rows[1].bytes[ST_SAVES] = 40;
	t.rows[1].bytes[ST_ART] = 20;
	t.rows[2].bytes[ST_ART] = 100;
	t.bios = 5;
	t.screenshots = 7;
	CHECK(St_rowTotal(&t.rows[1]) == 110);
	CHECK(St_total(&t) == 322);
	St_sort(&t);
	// largest first, then by name: Game Boy, then the tied GBA (named by
	// its tag) and Mega Drive
	CHECK_STR(t.rows[0].tag, "GB");
	CHECK_STR(t.rows[1].tag, "GBA");
	CHECK_STR(t.rows[2].tag, "MD");
	CHECK(St_find(&t, "MD") == 2 && St_find(&t, "PS") == -1);
	for (int i = t.count; i < ST_ROWS_MAX; i++) {
		char tag[16];
		snprintf(tag, sizeof(tag), "T%i", i);
		CHECK(St_row(&t, tag, NULL) == i);
	}
	CHECK(St_row(&t, "ONE MORE", NULL) == -1);

	StSeen seen;
	St_seenInit(&seen);
	CHECK(St_seenAdd(&seen, 1, 2) == 1);
	CHECK(St_seenAdd(&seen, 1, 3) == 1);
	CHECK(St_seenAdd(&seen, 1, 2) == 0);
	for (unsigned long long i = 0; i < 200; i++) CHECK(St_seenAdd(&seen, 2, i) == 1);
	CHECK(St_seenAdd(&seen, 2, 150) == 0);
	St_seenFree(&seen);

	char out[192];
	St_formatCount(0, out, sizeof(out));
	CHECK_STR(out, "0");
	St_formatCount(999, out, sizeof(out));
	CHECK_STR(out, "999");
	St_formatCount(1234, out, sizeof(out));
	CHECK_STR(out, "1,234");
	St_formatCount(1234567, out, sizeof(out));
	CHECK_STR(out, "1,234,567");
	St_formatProgress("Game Boy", 3, 12, 1234, out, sizeof(out));
	CHECK_STR(out, "Counting Game Boy, 3 of 12 systems, 1,234 files");
	St_formatProgress("Game Boy", 1, 1, 1, out, sizeof(out));
	CHECK_STR(out, "Counting Game Boy, 1 of 1 system, 1 file");
	St_formatProgress(NULL, 13, 12, 20, out, sizeof(out));
	CHECK_STR(out, "Counting BIOS, 12 of 12 systems, 20 files");
	St_formatProgress(NULL, 14, 12, 20, out, sizeof(out));
	CHECK_STR(out, "Counting Screenshots, 12 of 12 systems, 20 files");
}

static void testStorageCache(void) {
	StCache c;
	St_cacheInit(&c);
	CHECK(St_cacheStale(&c));
	// the first opening counts, and a second while it runs doesn't
	CHECK(St_cacheStart(&c) == 1);
	CHECK(St_cacheStart(&c) == 0);
	St_cacheDone(&c, 1);
	CHECK(!St_cacheStale(&c));
	CHECK(St_cacheStart(&c) == 0);
	// a game changes the card: counted again
	St_cacheChanged(&c);
	CHECK(St_cacheStale(&c));
	CHECK(St_cacheStart(&c) == 1);
	// and changes again while it counts: still stale once it's done
	St_cacheChanged(&c);
	St_cacheDone(&c, 1);
	CHECK(St_cacheStale(&c));
	CHECK(St_cacheStart(&c) == 1);
	// a cancelled count keeps what was there
	St_cacheDone(&c, 0);
	CHECK(St_cacheStale(&c) && c.have);
	CHECK(St_cacheStart(&c) == 1);
	St_cacheDone(&c, 1);
	CHECK(!St_cacheStale(&c));
}

static void writeBytes(const char* path, size_t n) {
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	for (size_t i = 0; i < n; i++) fputc('x', f);
	fclose(f);
}

static int progress_calls = 0;
static char progress_last[64];
static void testProgress(void* ctx, const char* name, int done, int total, unsigned long long files) {
	(void)ctx;
	(void)files;
	progress_calls++;
	snprintf(progress_last, sizeof(progress_last), "%s %i/%i", name ? name : "-", done, total);
}

#define SCARD CARD "/storage"

static void makeStorageCard(void) {
	CHECK(system("rm -rf '" SCARD "' && mkdir -p '" SCARD "/Roms/01) Game Boy (GB)/.media' '" SCARD
		"/Roms/01) Game Boy (GB)/Sub/.media' '" SCARD "/Roms/Game Boy Hacks (GB)' '" SCARD "/Roms/.hidden (HID)' '" SCARD
		"/Roms/Mega Drive (MD)' '" SCARD "/Saves/GB' '" SCARD "/Saves/PS' '" SCARD "/.userdata/shared/GB-gambatte' '" SCARD
		"/.userdata/shared/GBA-mgba' '" SCARD "/.userdata/shared/.minui' '" SCARD "/Bios/GBA' '" SCARD "/Screenshots' '" CARD
		"/outside'") == 0);
	writeBytes(SCARD "/Roms/01) Game Boy (GB)/Tetris.gb", 100);
	writeBytes(SCARD "/Roms/01) Game Boy (GB)/.media/Tetris.png", 50);
	writeBytes(SCARD "/Roms/01) Game Boy (GB)/Sub/Zelda.gb", 60);
	writeBytes(SCARD "/Roms/01) Game Boy (GB)/Sub/.media/Zelda.png", 10);
	writeBytes(SCARD "/Roms/Game Boy Hacks (GB)/Hack.gb", 30);
	writeBytes(SCARD "/Roms/.hidden (HID)/Secret.bin", 1000);
	writeBytes(SCARD "/Roms/Mega Drive (MD)/Sonic.md", 500);
	writeBytes(SCARD "/Saves/GB/Tetris.sav", 8);
	writeBytes(SCARD "/Saves/PS/Card.mcd", 4);
	writeBytes(SCARD "/.userdata/shared/GB-gambatte/Tetris.st0", 16);
	writeBytes(SCARD "/.userdata/shared/GBA-mgba/Nothing.st0", 2000);
	writeBytes(SCARD "/Bios/GBA/gba_bios.bin", 20);
	writeBytes(SCARD "/Screenshots/Tetris.png", 5);
	writeBytes(CARD "/outside/Big.bin", 5000);
	// a link to a big file outside counts as the link, and its folder isn't
	// followed; a hard link is counted once
	CHECK(symlink(CARD "/outside/Big.bin", SCARD "/Roms/Mega Drive (MD)/Big.md") == 0);
	CHECK(symlink(CARD "/outside", SCARD "/Roms/Mega Drive (MD)/Outside") == 0);
	CHECK(link(SCARD "/Roms/Mega Drive (MD)/Sonic.md", SCARD "/Roms/Mega Drive (MD)/Sonic Copy.md") == 0);
}

static void testStorageCount(void) {
	makeStorageCard();
	static StTotals t;
	StWalk w;
	St_walkInit(&w);
	w.progress = testProgress;
	progress_calls = 0;
	CHECK(St_count(SCARD, &t, &w) == 0);
	St_walkFree(&w);
	struct stat link_st;
	CHECK(lstat(SCARD "/Roms/Mega Drive (MD)/Big.md", &link_st) == 0);
	unsigned long long links = (unsigned long long)link_st.st_size;
	CHECK(lstat(SCARD "/Roms/Mega Drive (MD)/Outside", &link_st) == 0);
	links += (unsigned long long)link_st.st_size;
	// MD, GB, PS from Saves: largest first
	CHECK(t.count == 3);
	CHECK_STR(t.rows[0].tag, "MD");
	CHECK_STR(t.rows[0].name, "Mega Drive");
	CHECK(t.rows[0].bytes[ST_GAMES] == 500 + links);
	CHECK_STR(t.rows[1].tag, "GB");
	CHECK_STR(t.rows[1].name, "Game Boy");
	CHECK(t.rows[1].bytes[ST_GAMES] == 100 + 60 + 30);
	CHECK(t.rows[1].bytes[ST_ART] == 60);
	CHECK(t.rows[1].bytes[ST_SAVES] == 8 + 16);
	CHECK_STR(t.rows[2].tag, "PS");
	CHECK_STR(t.rows[2].name, "");
	CHECK(t.rows[2].bytes[ST_SAVES] == 4);
	CHECK(t.bios == 20);
	CHECK(t.screenshots == 5);
	CHECK(t.files == 13);
	// each system, then BIOS and Screenshots
	CHECK(progress_calls == 5);
	CHECK_STR(progress_last, "- 5/3");

	// cancelled: stops at once
	volatile int stop = 1;
	St_walkInit(&w);
	w.cancel = &stop;
	CHECK(St_count(SCARD, &t, &w) == -1);
	St_walkFree(&w);

	// an empty card
	CHECK(system("mkdir -p '" CARD "/empty'") == 0);
	St_walkInit(&w);
	CHECK(St_count(CARD "/empty", &t, &w) == 0);
	St_walkFree(&w);
	CHECK(t.count == 0 && St_total(&t) == 0);
}

///////////////////////////////////////
// the input tester's model

static void testInputModel(void) {
	CHECK_STR(It_name(MOSAIC_BUTTON_A), "A");
	CHECK_STR(It_name(MOSAIC_BUTTON_POWER), "POWER");
	CHECK_STR(It_name(MOSAIC_BUTTON_FN1), "");
	CHECK_STR(It_name(-1), "");
	CHECK(It_shown(MOSAIC_BUTTON_MENU, 0));
	CHECK(!It_shown(MOSAIC_BUTTON_FN1, 0));
	CHECK(It_shown(MOSAIC_BUTTON_FN3, 4) && !It_shown(MOSAIC_BUTTON_FN2, 4));
	CHECK(!It_shown(IT_BUTTONS, 7));

	ItHistory h;
	It_historyInit(&h);
	CHECK(It_historyPress(&h, 0) == 0);
	CHECK(It_historyPress(&h, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A)) == 1);
	CHECK(It_historyPress(&h, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) == 2);
	CHECK(h.count == 3);
	CHECK(h.buttons[0] == MOSAIC_BUTTON_UP && h.buttons[1] == MOSAIC_BUTTON_B && h.buttons[2] == MOSAIC_BUTTON_A);
	for (int i = 0; i < 10; i++) It_historyPress(&h, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X));
	It_historyPress(&h, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER));
	CHECK(h.count == IT_HISTORY);
	CHECK(h.buttons[0] == MOSAIC_BUTTON_POWER && h.buttons[IT_HISTORY - 1] == MOSAIC_BUTTON_X);

	unsigned int both = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_START);
	ItHold hold;
	It_holdInit(&hold);
	CHECK(It_holdStep(&hold, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B), 100) == 0);
	CHECK(It_holdPercent(&hold, 100) == 0);
	CHECK(It_holdStep(&hold, both, 1000) == 0);
	CHECK(It_holdPercent(&hold, 1500) == 50);
	CHECK(It_holdStep(&hold, both | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A), 1999) == 0);
	// letting go starts over
	CHECK(It_holdStep(&hold, MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_START), 2000) == 0);
	CHECK(It_holdStep(&hold, both, 2100) == 0);
	CHECK(It_holdStep(&hold, both, 3099) == 0);
	CHECK(It_holdStep(&hold, both, 3100) == 1);
	CHECK(It_holdPercent(&hold, 3500) == 100);

	CHECK(It_stickOffset(0, 20) == 0);
	CHECK(It_stickOffset(IT_STICK_MAX, 20) == 20);
	CHECK(It_stickOffset(-32768, 20) == -20);
	CHECK(It_stickOffset(IT_STICK_MAX / 2, 20) == 9);
	CHECK(It_stickOffset(100, 0) == 0);
	char out[48];
	It_formatStick(12000, -3000, out, sizeof(out));
	CHECK_STR(out, "12000, -3000");

	// every button shown inside the area and apart from the others
	MosaicRect area = { 10, 20, 640, 330 };
	ItLayout l;
	CHECK(It_layout(&area, 7, 3, &l) == 0);
	CHECK(l.unit == 20);
	for (int a = 0; a < IT_BUTTONS; a++) {
		CHECK(l.buttons[a].w > 0);
		CHECK(l.buttons[a].x >= area.x && l.buttons[a].x + l.buttons[a].w <= area.x + area.w);
		CHECK(l.buttons[a].y >= area.y && l.buttons[a].y + l.buttons[a].h <= area.y + area.h);
		for (int b = a + 1; b < IT_BUTTONS; b++) CHECK(!meet(&l.buttons[a], &l.buttons[b]));
		for (int s = 0; s < 2; s++) CHECK(!meet(&l.buttons[a], &l.sticks[s]) && !meet(&l.buttons[a], &l.stick_text[s]));
	}
	CHECK(!meet(&l.sticks[0], &l.sticks[1]));
	CHECK(It_layout(&area, 0, 0, &l) == 0);
	CHECK(l.buttons[MOSAIC_BUTTON_FN1].w == 0 && l.sticks[0].w == 0 && l.sticks[1].w == 0);
	// one FN button, centred
	CHECK(It_layout(&area, 4, 1, &l) == 0);
	CHECK(l.buttons[MOSAIC_BUTTON_FN3].w > 0 && l.buttons[MOSAIC_BUTTON_FN1].w == 0);
	CHECK(l.sticks[0].w > 0 && l.sticks[1].w == 0);
	MosaicRect tiny = { 0, 0, 40, 20 };
	CHECK(It_layout(&tiny, 0, 0, &l) == -1);
}

///////////////////////////////////////
// the Storage page and the input tester in the plugin

static void renderOn(const MosaicScreenHooks* h, const char* id) {
	MosaicScreenView view = { sizeof(view), { 0, 0, 1024, 768 }, { 800, 30, 194, 90 }, { 0, 0, 0, 0 }, id };
	hole_now = view.status;
	CHECK(h->render(&screen_surface, &view, h->data) == 0);
}

// Ticks until the count is done, for up to five seconds.
static void waitCount(void) {
	for (int i = 0; i < 500 && worker_running; i++) {
		struct timespec ms = { 0, 10 * 1000 * 1000 };
		nanosleep(&ms, NULL);
		tick(100000 + (unsigned long)i);
	}
	CHECK(!worker_running);
}

static void testPluginStorage(void) {
	makeCard();
	makeStorageCard();
	setupHost();
	fake_host.sdcard_path = SCARD;
	subscribed = 0;
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 0);
	CHECK(storage_screen.render != NULL);
	CHECK(subscribed == 1);
	all_log[0] = '\0';
	CHECK(fakeOpenScreen(STORAGE_ID) == 0);
	CHECK(logged("storage: counting"));
	fills_into_hole = 0;
	renderOn(&storage_screen, STORAGE_ID);
	waitCount();
	CHECK(logged("storage: 3 systems, 13 files, "));
	CHECK(logged("storage: Mega Drive (MD): games "));
	CHECK(logged("storage: Game Boy (GB): games 0.2 KB, saves 0.0 KB, art 0.1 KB, total 0.3 KB"));
	CHECK(logged("storage: PS (PS): games 0.0 KB, saves 0.0 KB, art 0.0 KB, total 0.0 KB"));
	CHECK(logged("storage: BIOS 0.0 KB, Screenshots 0.0 KB"));
	hint_text[0] = '\0';
	hints = 0;
	renderOn(&storage_screen, STORAGE_ID);
	CHECK(fills_into_hole == 0);
	CHECK(hints == 1);
	// the rows: three systems, BIOS, Screenshots and everything
	CHECK(storageRows() == 6);
	MosaicInput down;
	memset(&down, 0, sizeof(down));
	down.repeated = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN);
	CHECK(storage_screen.input(&down, storage_screen.data) == MOSAIC_SCREEN_STAY);
	CHECK(storage_first == 0); // they all fit
	MosaicInput press_b;
	memset(&press_b, 0, sizeof(press_b));
	press_b.pressed = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	CHECK(storage_screen.input(&press_b, storage_screen.data) == MOSAIC_SCREEN_CLOSE);
	storage_screen.exit(storage_screen.data);

	// opened again: what was counted
	all_log[0] = '\0';
	CHECK(fakeOpenScreen(STORAGE_ID) == 0);
	CHECK(logged("storage: opened with the sizes counted before"));
	CHECK(!worker_running);
	storage_screen.exit(storage_screen.data);

	// a game ran: counted again on the next opening, not before
	writeBytes(SCARD "/Saves/GB/Zelda.sav", 2000);
	card_changed("launcher.return", "path=x\ntype=rom", NULL);
	CHECK(logged("storage: launcher.return, counting again next time"));
	tick(200000);
	CHECK(!worker_running);
	all_log[0] = '\0';
	CHECK(fakeOpenScreen(STORAGE_ID) == 0);
	CHECK(logged("storage: counting"));
	// the old sizes show meanwhile
	CHECK(storageRows() == 6);
	waitCount();
	CHECK(logged("storage: Game Boy (GB): games 0.2 KB, saves 2.0 KB, art 0.1 KB, total 2.2 KB"));
	storage_screen.exit(storage_screen.data);

	// quitting stops a count
	card_changed("launcher.return", "path=y\ntype=pak", NULL);
	CHECK(fakeOpenScreen(STORAGE_ID) == 0);
	mosaic_plugin_get(&fake_host)->quit();
	CHECK(!worker_running);
	storage_screen.exit(storage_screen.data);

	// from the info page, Y opens it and X the input tester
	all_log[0] = '\0';
	action_run(NULL, NULL);
	MosaicInput press_y, press_x;
	memset(&press_y, 0, sizeof(press_y));
	memset(&press_x, 0, sizeof(press_x));
	press_y.pressed = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y);
	press_x.pressed = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X);
	CHECK(registered.input(&press_y, registered.data) == MOSAIC_SCREEN_STAY);
	CHECK_STR(opened_screen, STORAGE_ID);
	waitCount();
	storage_screen.exit(storage_screen.data);
	CHECK(registered.input(&press_x, registered.data) == MOSAIC_SCREEN_STAY);
	CHECK_STR(opened_screen, INPUT_ID);
	tester_screen.exit(tester_screen.data);
	registered.exit(registered.data);
	snprintf(current, sizeof(current), "gamelist");
}

static unsigned int filterFrame(unsigned int pressed, unsigned int released, unsigned int held) {
	MosaicInput in;
	memset(&in, 0, sizeof(in));
	in.size = sizeof(in);
	in.pressed = pressed;
	in.repeated = pressed;
	in.released = released;
	in.held = held;
	unsigned int consumed = 0;
	CHECK(inputFilter(&in, &consumed) == 0);
	return consumed;
}

static void testPluginTester(void) {
	makeCard();
	setupHost();
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) == 0);
	CHECK(tester_screen.render != NULL);
	unsigned int a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A), b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	unsigned int start = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_START), power = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_POWER);
	unsigned int menu = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_MENU), fn3 = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_FN3);
	// closed, it takes nothing
	CHECK(filterFrame(a | menu, 0, a | menu) == 0);

	all_log[0] = '\0';
	tick(1000);
	CHECK(fakeOpenScreen(INPUT_ID) == 0);
	CHECK(logged("input tester: opened"));
	CHECK(logged("input tester: left stick 12000, -3000; right stick none"));
	CHECK(fn_mask == 4 && stick_mask == 1);
	// every button but power is taken
	CHECK(filterFrame(a | menu | fn3 | power, 0, a | menu | fn3 | power) == (a | menu | fn3));
	CHECK(logged("input tester: pressed A"));
	CHECK(logged("input tester: pressed MENU"));
	CHECK(logged("input tester: pressed POWER"));
	CHECK(logged("input tester: pressed HOME"));
	CHECK(held_now == (a | menu | fn3 | power));
	CHECK(filterFrame(0, a | menu | fn3 | power, 0) == (a | menu | fn3));
	CHECK(presses.count == 4 && presses.buttons[0] == MOSAIC_BUTTON_FN3);
	// a dialog over it gets its buttons
	snprintf(current, sizeof(current), "dialog");
	CHECK(filterFrame(a, 0, a) == 0);
	snprintf(current, sizeof(current), INPUT_ID);
	CHECK(filterFrame(0, a, 0) == a);

	// the sticks move: a redraw
	redraws = 0;
	stick_x = -32768;
	tick(1100);
	CHECK(redraws == 1 && sticks[0][0] == -32768);
	tick(1200);
	CHECK(redraws == 1);
	fills_into_hole = 0;
	renderOn(&tester_screen, INPUT_ID);
	CHECK(fills_into_hole == 0);

	// B and START, held for a second, leave
	MosaicInput none;
	memset(&none, 0, sizeof(none));
	tick(2000);
	CHECK(filterFrame(b, 0, b) == b);
	CHECK(filterFrame(start, 0, b | start) == start);
	CHECK(tester_screen.animating(tester_screen.data) == 1);
	CHECK(tester_screen.input(&none, tester_screen.data) == MOSAIC_SCREEN_STAY);
	tick(2999);
	CHECK(filterFrame(0, 0, b | start) == 0);
	CHECK(tester_screen.input(&none, tester_screen.data) == MOSAIC_SCREEN_STAY);
	tick(3000);
	CHECK(filterFrame(0, 0, b | start) == 0);
	CHECK(logged("input tester: B and START held, leaving"));
	CHECK(tester_screen.input(&none, tester_screen.data) == MOSAIC_SCREEN_CLOSE);
	tester_screen.exit(tester_screen.data);
	snprintf(current, sizeof(current), SCREEN_ID);
	CHECK(logged("input tester: closed"));
	// their releases are still taken, and then nothing; a new press in the
	// same frame as a release gets through
	CHECK(filterFrame(0, b, start) == b);
	CHECK(filterFrame(b, start, b) == start);
	CHECK(filterFrame(a, 0, a) == 0);
	CHECK(filterFrame(b, 0, b) == 0);

	// without sticks or FN buttons
	stick_left = 0;
	fake_host.fn_name = NULL;
	fake_host.size = offsetof(MosaicHost, stick);
	CHECK(fakeOpenScreen(INPUT_ID) == 0);
	CHECK(logged("input tester: left stick none; right stick none"));
	CHECK(fn_mask == 0 && stick_mask == 0);
	renderOn(&tester_screen, INPUT_ID);
	tester_screen.exit(tester_screen.data);
	stick_left = 1;
	stick_x = 12000;
	snprintf(current, sizeof(current), "gamelist");

	// an older launcher without input filters: no tester
	setupHost();
	memset(&tester_screen, 0, sizeof(tester_screen));
	fake_host.hold_switch = NULL;
	all_log[0] = '\0';
	mosaic_plugin_get(&fake_host)->init(&fake_host);
	CHECK(tester_screen.render == NULL);
	CHECK(logged("this launcher has no input filters, so no input tester"));
}

int main(void) {
	testFormats();
	testWords();
	testProc();
	testAddrs();
	testRows();
	testHistory();
	testLayout();
	testHints();
	testDue();
	testPlugin();
	testSettings();
	testStorageNames();
	testStorageRows();
	testStorageCache();
	testStorageCount();
	testInputModel();
	testPluginStorage();
	testPluginTester();
	system("rm -rf '" CARD "'");
	CHECK_DONE();
}
