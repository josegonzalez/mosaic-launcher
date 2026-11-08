// The header indicators plugin's model, and the plugin against a fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/header-indicators/header_indicators.c"

///////////////////////////////////////
// the model

static const char* size(unsigned long long bytes) {
	static char out[16];
	Hi_formatSize(bytes, out, sizeof(out));
	return out;
}

#define K 1024ULL
#define M (K * K)
#define G (M * K)
#define T (G * K)

static void testFormatSize(void) {
	CHECK_STR(size(0), "0K");
	CHECK_STR(size(512), "0.5K");
	CHECK_STR(size(K), "1.0K");
	CHECK_STR(size(980 * M), "980M");
	CHECK_STR(size(99 * M), "99.0M");
	CHECK_STR(size(12 * G + 3 * G / 10), "12.3G");
	CHECK_STR(size(100 * G), "100G");
	// 99.96 rounds to 100, so it shows no decimal
	CHECK_STR(size(99 * G + 96 * G / 100), "100G");
	// 999.6M would show 1000M, so it moves on to G
	CHECK_STR(size(999 * M + 6 * M / 10), "1.0G");
	CHECK_STR(size(999 * M), "999M");
	CHECK_STR(size(1023 * M), "1.0G");
	CHECK_STR(size(T + T / 5), "1.2T");
	char small[4];
	Hi_formatSize(12 * G, small, sizeof(small));
	CHECK_STR(small, "12.");
	Hi_formatSize(12 * G, NULL, 4);
}

static void testFps(void) {
	HiFps f;
	HiFps_init(&f);
	CHECK(f.value == -1);
	// 50 frames a second, 20 ms apart: a reading once a second has passed
	unsigned long now = 1000;
	CHECK(HiFps_frame(&f, now) == 0);
	int changed = 0;
	for (int i = 0; i < 49; i++) changed += HiFps_frame(&f, now += 20);
	CHECK(changed == 0);
	CHECK(f.value == -1);
	CHECK(HiFps_frame(&f, now += 20) == 1);
	CHECK(f.value == 50);
	// a slower second is averaged with the one before
	for (int i = 0; i < 25; i++) HiFps_frame(&f, now += 40);
	CHECK(f.value == 38);
	// and it settles on the new rate
	for (int i = 0; i < 25 * 10; i++) HiFps_frame(&f, now += 40);
	CHECK(f.value == 25);
	// the same rate changes nothing
	int steady = 0;
	for (int i = 0; i < 25 * 3; i++) steady += HiFps_frame(&f, now += 40);
	CHECK(steady == 0);

	// a long gap, such as sleep, starts again and keeps the last reading
	now += 60000;
	CHECK(HiFps_frame(&f, now) == 0);
	CHECK(f.value == 25);
	CHECK(f.frames == 0);
	// so does the clock going back
	CHECK(HiFps_frame(&f, now - 10) == 0);
	CHECK(f.frames == 0);
	// readings stay under 1000
	HiFps_init(&f);
	for (int i = 0; i < 5000; i++) HiFps_frame(&f, 1000 + (unsigned long)i / 4);
	CHECK(f.value == 999);
}

static void testListening(void) {
	const char* tcp =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
		"   0: 00000000:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 1234 1\n"
		"   1: 0100007F:0016 0100007F:9C40 01 00000000:00000000 00:00000000 00000000     0        0 0 1\n";
	// port 22 only as a connection, and a listener on 8080
	CHECK(!Hi_listening(tcp, 22));
	CHECK(Hi_listening(tcp, 8080));
	const char* sshd =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
		"   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 1234 1";
	CHECK(Hi_listening(sshd, 22));
	const char* tcp6 =
		"  sl  local_address                         remote_address                        st tx_queue rx_queue\n"
		"   0: 00000000000000000000000000000000:0016 00000000000000000000000000000000:0000 0A 00000000:00000000\n";
	CHECK(Hi_listening(tcp6, 22));
	CHECK(!Hi_listening("", 22));
	CHECK(!Hi_listening(NULL, 22));
	CHECK(!Hi_listening("garbage\n0: zz:0016 x 0A\n   0: 00000000:00G6 00000000:0000 0A\n", 22));
}

static void testFit(void) {
	int widths[HI_COUNT] = { 40, 30, 50, 6 };
	int shown[HI_COUNT];
	// all of them: 126 wide, 3 gaps of 4, 10 at each end
	CHECK(Hi_fit(widths, 10, 4, 500, shown) == 126 + 12 + 20);
	CHECK(shown[HI_PROFILE] && shown[HI_STORAGE] && shown[HI_FPS] && shown[HI_SSH]);
	CHECK(Hi_fit(widths, 10, 4, 158, shown) == 158);
	// the FPS goes first, then the profile, then free storage
	CHECK(Hi_fit(widths, 10, 4, 157, shown) == 76 + 8 + 20);
	CHECK(shown[HI_PROFILE] && shown[HI_STORAGE] && !shown[HI_FPS] && shown[HI_SSH]);
	CHECK(Hi_fit(widths, 10, 4, 100, shown) == 36 + 4 + 20);
	CHECK(!shown[HI_PROFILE] && shown[HI_STORAGE] && !shown[HI_FPS] && shown[HI_SSH]);
	CHECK(Hi_fit(widths, 10, 4, 59, shown) == 26);
	CHECK(!shown[HI_STORAGE] && shown[HI_SSH]);
	CHECK(Hi_fit(widths, 10, 4, 25, shown) == 0);
	CHECK(!shown[HI_SSH]);
	// ones that are off take no room and no gap
	int some[HI_COUNT] = { 0, 30, 0, 0 };
	CHECK(Hi_fit(some, 10, 4, 500, shown) == 50);
	CHECK(!shown[HI_PROFILE] && shown[HI_STORAGE] && !shown[HI_FPS] && !shown[HI_SSH]);
	int none[HI_COUNT] = { 0, 0, 0, 0 };
	CHECK(Hi_fit(none, 10, 4, 500, shown) == 0);
}

static void testSettings(void) {
	unsigned long next = 0;
	CHECK(Hi_due(&next, 0, 5000));
	CHECK(!Hi_due(&next, 4999, 5000));
	CHECK(Hi_due(&next, 5000, 5000));
	CHECK(next == 10000);

	CHECK(Hi_parseToggle("on", 0) == 1);
	CHECK(Hi_parseToggle(" off ", 1) == 0);
	CHECK(Hi_parseToggle("onn", 0) == 0);
	CHECK(Hi_parseToggle("", 1) == 1);
	CHECK(Hi_parseToggle(NULL, 0) == 0);
}

///////////////////////////////////////
// the plugin against a fake host

static const char* settings[8][2];
static const char* platform = "tg5040";
static int redraws = 0;
static int logs = 0;
static char last_log[256];
static int fills = 0, round_fills = 0, texts = 0;
static char drawn[4][64];
static MosaicRect drawn_at[4];
static unsigned int drawn_rgb[4];
static MosaicRect dot;

static void fakeLog(const char* id, const char* m) {
	CHECK(strcmp(id, ID) == 0);
	snprintf(last_log, sizeof(last_log), "%s", m);
	logs++;
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	CHECK(strcmp(id, ID) == 0);
	for (int i = 0; i < 8 && settings[i][0]; i++) {
		if (!strcmp(settings[i][0], key)) return settings[i][1];
	}
	return fallback;
}
static void fakeRedraw(void) { redraws++; }
static int fakeScale(void) { return 2; }
static int fakeTheme(int index) { return index == MOSAIC_COLOR_ACCENT ? 0x112233 : index == MOSAIC_COLOR_HINT ? 0xFFFFFF : 0; }
// every character is 5 wide and a line 12 tall
static int fakeTextWidth(int font, const char* text) { CHECK(font == FONT); return 5 * (int)strlen(text); }
static int fakeTextHeight(int font) { CHECK(font == FONT); return 12; }
static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	(void)font;
	size_t n = strlen(text);
	if ((int)n * 5 > max_width) n = (size_t)(max_width / 5);
	if (n >= size) n = size - 1;
	memcpy(out, text, n);
	out[n] = '\0';
	return 5 * (int)n;
}
static int fakeFillRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s; (void)radius;
	round_fills++;
	if (r->w < 20) dot = *r;
	(void)c;
	return 0;
}
static int fakeFill(MosaicSurface* s, const MosaicRect* r, MosaicColor c) { (void)s; (void)r; (void)c; fills++; return 0; }
static int fakeDrawText(MosaicSurface* s, int font, const char* text, int x, int y, MosaicColor c, int max_width) {
	(void)s; (void)font; (void)max_width;
	if (texts < 4) {
		snprintf(drawn[texts], sizeof(drawn[texts]), "%s", text);
		drawn_at[texts] = (MosaicRect){ x, y, 0, 0 };
		drawn_rgb[texts] = (unsigned)c.r << 16 | (unsigned)c.g << 8 | c.b;
	}
	texts++;
	return 5 * (int)strlen(text);
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) { (void)s; (void)i; (void)r; (void)n; return -1; }

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.sdcard_path = TEST_DIR,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.request_redraw = fakeRedraw,
	.theme_color = fakeTheme,
	.screen_scale = fakeScale,
	.fill_rect = fakeFill,
	.fill_round_rect = fakeFillRound,
	.text_width = fakeTextWidth,
	.text_height = fakeTextHeight,
	.text_truncate = fakeTruncate,
	.draw_text = fakeDrawText,
	.decorate_cell = fakeDecorateCell,
};

static void setting(const char* key, const char* value) {
	int i = 0;
	while (i < 7 && settings[i][0] && strcmp(settings[i][0], key)) i++;
	settings[i][0] = key;
	settings[i][1] = value;
}

static void start(void) {
	fake.platform = platform;
	redraws = logs = 0;
	last_log[0] = '\0';
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->header_width == headerWidth && p->header_widget == headerWidget);
	CHECK(p->init(&fake) == 0);
}

static void draw(const MosaicRect* rect) {
	fills = round_fills = texts = 0;
	memset(drawn, 0, sizeof(drawn));
	memset(&dot, 0, sizeof(dot));
	CHECK(headerWidget(NULL, rect) == 0);
}

static void testStorage(void) {
	memset(settings, 0, sizeof(settings));
	start();
	// nothing to show before the first tick
	CHECK(headerWidth(300, 60) == 0);
	tick(100);
	CHECK(redraws == 1);
	CHECK(storage[0] != '\0');
	// pad 24 at each end, scale 2
	int w = headerWidth(300, 60);
	CHECK(w == 5 * (int)strlen(storage) + 48);
	MosaicRect rect = { 100, 10, w, 60 };
	draw(&rect);
	CHECK(round_fills == 1);
	CHECK(texts == 1);
	CHECK_STR(drawn[0], storage);
	CHECK(drawn_at[0].x == 124 && drawn_at[0].y == 10 + (60 - 12) / 2);
	CHECK(drawn_rgb[0] == 0xFFFFFF);
	CHECK(strstr(last_log, "widget at 100,10 ") && strstr(last_log, ": storage"));
	// logged once, not every frame
	int logged = logs;
	draw(&rect);
	CHECK(logs == logged);

	// looked at again only every few seconds, and redrawn only on a change
	tick(200);
	tick(4000);
	CHECK(redraws == 1);
	CHECK(next_storage == 5100);
	tick(5200);
	CHECK(next_storage == 10200);
	// no room
	CHECK(headerWidth(40, 60) == 0);
}

static void testProfileAndFps(void) {
	memset(settings, 0, sizeof(settings));
	setting("storage", "off");
	setting("fps", "on");
	setting("profile", "Kid");
	start();
	unsigned long now = 1000;
	tick(now);
	CHECK(headerWidth(300, 60) == 15 + 48);
	for (int i = 0; i < 50; i++) tick(now += 20);
	CHECK(redraws == 1);
	CHECK(fps.value == 50);
	CHECK_STR(last_log, "fps first 50");
	// "50 FPS" is 30 wide, a gap of 12 after Kid
	int w = headerWidth(300, 60);
	CHECK(w == 15 + 12 + 30 + 48);
	MosaicRect rect = { 0, 0, w, 60 };
	draw(&rect);
	CHECK(texts == 2);
	CHECK_STR(drawn[0], "Kid");
	CHECK_STR(drawn[1], "50 FPS");
	CHECK(drawn_at[1].x == 24 + 15 + 12);
	// a long profile is cut to the room there is, and the FPS dropped
	setting("profile", "A very long profile name");
	start();
	now = 1000;
	for (int i = 0; i < 51; i++) tick(now += 20);
	CHECK(fps.value == 50);
	// 10 characters fit in 100 less the ends, and the FPS doesn't beside them
	w = headerWidth(100, 60);
	CHECK(w == 50 + 48);
	CHECK(shown[HI_PROFILE] && !shown[HI_FPS]);
	CHECK(strlen(profile_cut) == 10);
}

static void testSsh(void) {
	memset(settings, 0, sizeof(settings));
	setting("storage", "off");
	setting("ssh", "on");
	platform = "desktop";
	start();
	CHECK(strstr(last_log, "unsupported on desktop") != NULL);
	tick(0);
	CHECK(headerWidth(300, 60) == 0);
	platform = "tg5040";
	start();
	CHECK(logs == 0);
	// on a device the dot shows only while port 22 listens; force it here
	ssh_on = 1;
	int w = headerWidth(300, 60);
	CHECK(w == 4 + 48);
	MosaicRect rect = { 10, 0, w, 60 };
	draw(&rect);
	CHECK(round_fills == 2);
	CHECK(dot.x == 34 && dot.w == 4 && dot.y == 28);
}

int main(void) {
	testFormatSize();
	testFps();
	testListening();
	testFit();
	testSettings();
	testStorage();
	testProfileAndFps();
	testSsh();
	CHECK_DONE();
}
