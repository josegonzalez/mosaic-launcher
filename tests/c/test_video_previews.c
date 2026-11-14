// The video previews plugin's model.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/video-previews/vp_model.h"

static void testClipPath(void) {
	char out[40];
	CHECK(VP_clipPath("/mnt/SDCARD/Roms/Game Boy (GB)/Tetris (World).gb", out, sizeof(out)) == 0); // too long for out
	CHECK_STR(out, "");
	char big[256];
	CHECK(VP_clipPath("/mnt/SDCARD/Roms/Game Boy (GB)/Tetris (World).gb", big, sizeof(big)));
	CHECK_STR(big, "/mnt/SDCARD/Roms/Game Boy (GB)/.videos/Tetris (World).mp4");
	CHECK(VP_clipPath("/Roms/GBA/Metroid.Fusion.gba", big, sizeof(big)));
	CHECK_STR(big, "/Roms/GBA/.videos/Metroid.Fusion.mp4");
	CHECK(VP_clipPath("/Roms/PS/Game", big, sizeof(big)));
	CHECK_STR(big, "/Roms/PS/.videos/Game.mp4");
	CHECK(VP_clipPath("/Roms/PS/.hidden", big, sizeof(big)));
	CHECK_STR(big, "/Roms/PS/.videos/.hidden.mp4");
	CHECK(!VP_clipPath("/Roms/PS/", big, sizeof(big)));
	CHECK(!VP_clipPath("Tetris.gb", big, sizeof(big)));
	CHECK(!VP_clipPath("/Tetris.gb", big, sizeof(big)));
	CHECK(!VP_clipPath(NULL, big, sizeof(big)));
	CHECK_STR(big, "");
}

static void testDirAndLibs(void) {
	char out[64];
	CHECK(VP_dirOf("/a/b/plugin.so", out, sizeof(out)));
	CHECK_STR(out, "/a/b");
	CHECK(VP_dirOf("/plugin.so", out, sizeof(out)));
	CHECK_STR(out, "/");
	CHECK(!VP_dirOf("plugin.so", out, sizeof(out)));
	CHECK(!VP_dirOf("/a/b/plugin.so", out, 4));
	CHECK(VP_libName("avcodec", 60, 0, out, sizeof(out)));
	CHECK_STR(out, "libavcodec.so.60");
	CHECK(VP_libName("avutil", 61, 1, out, sizeof(out)));
	CHECK_STR(out, "libavutil.61.dylib");
	CHECK(!VP_libName("avformat", 60, 0, out, 8));
}

static void testSettings(void) {
	CHECK(VP_parseDelayMs("2", 9) == 2000);
	CHECK(VP_parseDelayMs("5", 9) == 5000);
	CHECK(VP_parseDelayMs("10", 9) == 10000);
	CHECK(VP_parseDelayMs("0", 9) == 9);
	CHECK(VP_parseDelayMs("11", 9) == 9);
	CHECK(VP_parseDelayMs("2s", 9) == 9);
	CHECK(VP_parseDelayMs("", 9) == 9);
	CHECK(VP_parseDelayMs(NULL, 9) == 9);
	CHECK(VP_parseToggle("on", 0) == 1);
	CHECK(VP_parseToggle("1", 0) == 1);
	CHECK(VP_parseToggle("off", 1) == 0);
	CHECK(VP_parseToggle("0", 1) == 0);
	CHECK(VP_parseToggle("maybe", 1) == 1);
	CHECK(VP_parseToggle(NULL, 0) == 0);
}

static void testSaver(void) {
	CHECK(VP_parseClock("24h") == VP_CLOCK_24H);
	CHECK(VP_parseClock("12h") == VP_CLOCK_12H);
	CHECK(VP_parseClock("off") == VP_CLOCK_OFF);
	CHECK(VP_parseClock("analog") == VP_CLOCK_OFF);
	CHECK(VP_parseClock(NULL) == VP_CLOCK_OFF);
	char out[16];
	VP_formatClock(9, 5, VP_CLOCK_24H, "AM", "PM", out, sizeof(out));
	CHECK_STR(out, "09:05");
	VP_formatClock(21, 7, VP_CLOCK_12H, "AM", "PM", out, sizeof(out));
	CHECK_STR(out, "9:07 PM");
	VP_formatClock(0, 0, VP_CLOCK_12H, "a. m.", "p. m.", out, sizeof(out));
	CHECK_STR(out, "12:00 a. m.");
	VP_formatClock(9, 5, VP_CLOCK_OFF, "AM", "PM", out, sizeof(out));
	CHECK_STR(out, "");
	VP_formatClock(24, 0, VP_CLOCK_24H, "AM", "PM", out, sizeof(out));
	CHECK_STR(out, "");

	// music plays on after a clip only when the clip paused it
	VpMusic m = { 0 };
	VpMusic_paused(&m, 0); // it wasn't playing
	CHECK(!VpMusic_resume(&m));
	VpMusic_paused(&m, 1);
	VpMusic_seen(&m, "paused");
	CHECK(VpMusic_resume(&m));
	CHECK(!VpMusic_resume(&m)); // once
	// played on by something else, such as X on the screensaver
	VpMusic_paused(&m, 1);
	VpMusic_seen(&m, "playing");
	CHECK(!VpMusic_resume(&m));
	// not after a sleep
	VpMusic_paused(&m, 1);
	VpMusic_forget(&m);
	CHECK(!VpMusic_resume(&m));
}

static void testArm(void) {
	VpArm arm;
	VP_disarm(&arm);
	CHECK(!VP_due(&arm, 0));
	CHECK(!VP_isFor(&arm, "/Roms/a.gb"));
	VP_arm(&arm, "/Roms/a.gb", "/Roms/.videos/a.mp4", 1000, 2000);
	CHECK(VP_isFor(&arm, "/Roms/a.gb"));
	CHECK(!VP_isFor(&arm, "/Roms/b.gb"));
	CHECK(!VP_isFor(&arm, NULL));
	CHECK(!VP_due(&arm, 2999));
	CHECK(VP_due(&arm, 3000));
	CHECK(!VP_due(&arm, 3001)); // once
	CHECK(VP_isFor(&arm, "/Roms/a.gb")); // still the game playing
	CHECK_STR(arm.clip, "/Roms/.videos/a.mp4");

	// refusals wait twice as long each time, up to the most
	VP_retry(&arm, 5000, 2000);
	CHECK(!VP_due(&arm, 6999));
	CHECK(VP_due(&arm, 7000));
	VP_retry(&arm, 7000, 2000);
	CHECK(!VP_due(&arm, 10999));
	CHECK(VP_due(&arm, 11000));
	VP_retry(&arm, 11000, 2000);
	CHECK(VP_due(&arm, 19000));
	VP_retry(&arm, 19000, 2000);
	CHECK(!VP_due(&arm, 19000 + VP_RETRY_MS_MAX - 1));
	CHECK(VP_due(&arm, 19000 + VP_RETRY_MS_MAX));
	VP_retry(&arm, 50000, 2000);
	CHECK(VP_due(&arm, 50000 + VP_RETRY_MS_MAX));

	// a new game starts the waits over
	VP_arm(&arm, "/Roms/b.gb", "/Roms/.videos/b.mp4", 60000, 2000);
	VP_retry(&arm, 62000, 2000);
	CHECK(VP_due(&arm, 64000));

	// nothing armed, nothing to retry
	VP_disarm(&arm);
	VP_retry(&arm, 0, 2000);
	CHECK(!VP_due(&arm, 100000));

	// the clock wrapping round
	VP_arm(&arm, "/Roms/a.gb", "/Roms/.videos/a.mp4", (unsigned long)-500, 2000);
	CHECK(!VP_due(&arm, (unsigned long)-1));
	CHECK(VP_due(&arm, 1500));
}

static void testClock(void) {
	VpClock clock;
	// no sound: the wall clock
	VP_clockStart(&clock, 0, 1000);
	CHECK(VP_clockMs(&clock, 0, 1000) == 0);
	CHECK(VP_clockMs(&clock, 0, 1250) == 250);
	CHECK(VP_queuedMs(&clock, 4800) == 0);

	// sound: what has reached the device, past its own buffer
	VP_clockStart(&clock, 48000, 0);
	VP_clockPushed(&clock, 4800);
	CHECK(VP_clockMs(&clock, 4800, 0) == 0);
	CHECK(VP_queuedMs(&clock, 4800) == 100);
	CHECK(VP_clockMs(&clock, 4800 - VP_DEVICE_FRAMES, 0) == 0);
	CHECK(VP_clockMs(&clock, 0, 0) == (4800 - VP_DEVICE_FRAMES) * 1000 / 48000);
	VP_clockPushed(&clock, 48000);
	CHECK(VP_clockMs(&clock, 4800, 0) == (48000 - VP_DEVICE_FRAMES) * 1000 / 48000);
	VP_clockPushed(&clock, -5); // ignored
	CHECK(clock.pushed == 52800);

	// the sound ends: wall time from where it got to, once the queue empties
	VP_clockAudioDone(&clock);
	CHECK(VP_clockMs(&clock, 100, 5000) == (52700 - VP_DEVICE_FRAMES) * 1000 / 48000);
	long long held = VP_clockMs(&clock, 0, 5000);
	CHECK(held == (52800 - VP_DEVICE_FRAMES) * 1000 / 48000);
	CHECK(VP_clockMs(&clock, 0, 5300) == held + 300);
	CHECK(VP_clockMs(&clock, 700, 5400) == held + 400); // held from then on

	// the device goes: the same
	VP_clockStart(&clock, 22050, 0);
	VP_clockPushed(&clock, 22050 + VP_DEVICE_FRAMES);
	CHECK(VP_clockMs(&clock, 0, 100) == 1000);
	CHECK(VP_clockMs(&clock, -1, 200) == 1000);
	CHECK(VP_clockMs(&clock, -1, 700) == 1500);
}

static void testPace(void) {
	int wait = -1;
	CHECK(VP_pace(100, 0, -1, &wait) == VP_PACE_WAIT && wait == VP_SLICE_MS);
	CHECK(VP_pace(100, 97, -1, &wait) == VP_PACE_WAIT && wait == 3);
	CHECK(VP_pace(100, 100, -1, &wait) == VP_PACE_SHOW && wait == 0);
	CHECK(VP_pace(100, 500, 67, &wait) == VP_PACE_SHOW); // late still shows
	// 60 fps: every other frame
	CHECK(VP_pace(16, 20, 0, &wait) == VP_PACE_DROP);
	CHECK(VP_pace(33, 40, 0, &wait) == VP_PACE_SHOW);
	CHECK(VP_pace(50, 60, 33, &wait) == VP_PACE_DROP);
	CHECK(VP_pace(66, 70, 33, &wait) == VP_PACE_SHOW);
	// 30 fps rounding to whole milliseconds
	CHECK(VP_pace(66, 70, 33, &wait) == VP_PACE_SHOW);
	CHECK(VP_pace(99, 100, 66, &wait) == VP_PACE_SHOW);
	CHECK(VP_pace(10, 10, 0, NULL) == VP_PACE_DROP);

	VpLate late = { 0, 0 };
	for (int i = 0; i < VP_LATE_IN_A_ROW - 1; i++) CHECK(!VP_late(&late, 0, VP_LATE_MS + 1));
	CHECK(!VP_late(&late, 0, VP_LATE_MS)); // on time breaks the run
	for (int i = 0; i < VP_LATE_IN_A_ROW - 1; i++) CHECK(!VP_late(&late, 0, 500));
	CHECK(VP_late(&late, 0, 500));
	CHECK(!VP_late(&late, 0, 500)); // once
	CHECK(late.degraded);
}

static void testYuv(void) {
	CHECK(VP_yuvMode(1, 1, 0, 1080) == VP_YUV_FULL);
	CHECK(VP_yuvMode(0, 1, 0, 480) == VP_YUV_BT709);
	CHECK(VP_yuvMode(0, 0, 1, 1080) == VP_YUV_BT601);
	CHECK(VP_yuvMode(0, 0, 0, 720) == VP_YUV_BT709);
	CHECK(VP_yuvMode(0, 0, 0, 480) == VP_YUV_BT601);

	// a 3x2 picture (odd width) with padded rows: black, white, red, green,
	// blue and grey in limited range BT.601
	unsigned char y[2][4] = { { 16, 235, 81, 0 }, { 145, 41, 126, 0 } };
	unsigned char u[1][3] = { { 128, 90, 240 } };
	unsigned char v[1][3] = { { 128, 240, 110 } };
	unsigned char out[2][16];
	memset(out, 7, sizeof(out));
	// one chroma sample covers two pixels, so test each pair on its own
	VP_yuvToRgba(&y[0][0], 4, &u[0][0], 3, &v[0][0], 3, 2, 1, VP_YUV_BT601, &out[0][0], 16);
	CHECK(out[0][0] == 0 && out[0][1] == 0 && out[0][2] == 0 && out[0][3] == 255);
	CHECK(out[0][4] == 255 && out[0][5] == 255 && out[0][6] == 255 && out[0][7] == 255);
	CHECK(out[0][8] == 7); // past w

	unsigned char red_y = 81, red_u = 90, red_v = 240;
	unsigned char px[4];
	VP_yuvToRgba(&red_y, 1, &red_u, 1, &red_v, 1, 1, 1, VP_YUV_BT601, px, 4);
	CHECK(px[0] >= 250 && px[1] <= 5 && px[2] <= 5 && px[3] == 255);
	unsigned char green_y = 145, green_u = 54, green_v = 34;
	VP_yuvToRgba(&green_y, 1, &green_u, 1, &green_v, 1, 1, 1, VP_YUV_BT601, px, 4);
	CHECK(px[0] <= 5 && px[1] >= 250 && px[2] <= 5);
	unsigned char blue_y = 41, blue_u = 240, blue_v = 110;
	VP_yuvToRgba(&blue_y, 1, &blue_u, 1, &blue_v, 1, 1, 1, VP_YUV_BT601, px, 4);
	CHECK(px[0] <= 5 && px[1] <= 5 && px[2] >= 250);

	// full range: 0 and 255 are black and white
	unsigned char fy[2] = { 0, 255 }, fu = 128, fv = 128;
	unsigned char fo[8];
	VP_yuvToRgba(fy, 2, &fu, 1, &fv, 1, 2, 1, VP_YUV_FULL, fo, 8);
	CHECK(fo[0] == 0 && fo[1] == 0 && fo[2] == 0);
	CHECK(fo[4] == 255 && fo[5] == 255 && fo[6] == 255);
	// limited range clips below 16 and above 235
	VP_yuvToRgba(fy, 2, &fu, 1, &fv, 1, 2, 1, VP_YUV_BT601, fo, 8);
	CHECK(fo[0] == 0 && fo[4] == 255);
	// BT.709 red differs from BT.601's reading of the same bytes
	unsigned char r7y = 63, r7u = 102, r7v = 240;
	VP_yuvToRgba(&r7y, 1, &r7u, 1, &r7v, 1, 1, 1, VP_YUV_BT709, px, 4);
	CHECK(px[0] >= 250 && px[1] <= 5 && px[2] <= 5);

	// the second row reads the same chroma row as the first
	VP_yuvToRgba(&y[0][0], 4, &u[0][0], 3, &v[0][0], 3, 3, 2, VP_YUV_BT601, &out[0][0], 16);
	CHECK(out[1][3] == 255 && out[1][8] != 7 && out[1][11] == 255 && out[1][12] == 7);
	// an unknown mode reads as BT.601
	unsigned char a[4], b[4];
	VP_yuvToRgba(&red_y, 1, &red_u, 1, &red_v, 1, 1, 1, 42, a, 4);
	VP_yuvToRgba(&red_y, 1, &red_u, 1, &red_v, 1, 1, 1, VP_YUV_BT601, b, 4);
	CHECK(!memcmp(a, b, 4));
}

static void testPcm(void) {
	CHECK(VP_outChannels(1) == 1);
	CHECK(VP_outChannels(2) == 2);
	CHECK(VP_outChannels(6) == 2);

	short out[16];
	// planar float, clamped, NaN as silence
	float left[3] = { 0.5f, -2.0f, 1.0f }, right[3] = { -0.5f, __builtin_nanf(""), 0.0f };
	const unsigned char* fltp[2] = { (const unsigned char*)left, (const unsigned char*)right };
	CHECK(VP_pcmToS16(VP_PCM_FLTP, 2, fltp, 3, out) == 3);
	CHECK(out[0] == 16383 && out[1] == -16383);
	CHECK(out[2] == -32768 && out[3] == 0);
	CHECK(out[4] == 32767 && out[5] == 0);

	// interleaved 16-bit, 6 channels down to the first two
	short six[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
	const unsigned char* s16[1] = { (const unsigned char*)six };
	CHECK(VP_pcmToS16(VP_PCM_S16, 6, s16, 2, out) == 2);
	CHECK(out[0] == 1 && out[1] == 2 && out[2] == 7 && out[3] == 8);

	// mono stays mono
	short mono[2] = { -100, 200 };
	const unsigned char* m[1] = { (const unsigned char*)mono };
	CHECK(VP_pcmToS16(VP_PCM_S16P, 1, m, 2, out) == 2);
	CHECK(out[0] == -100 && out[1] == 200);

	// 32-bit keeps the top 16 bits
	int wide[2] = { 0x7fff0000, (int)0x80000000 };
	const unsigned char* w[1] = { (const unsigned char*)wide };
	CHECK(VP_pcmToS16(VP_PCM_S32, 2, w, 1, out) == 1);
	CHECK(out[0] == 32767 && out[1] == -32768);
	int wl[1] = { 0x00010000 }, wr[1] = { -0x00010000 };
	const unsigned char* wp[2] = { (const unsigned char*)wl, (const unsigned char*)wr };
	CHECK(VP_pcmToS16(VP_PCM_S32P, 2, wp, 1, out) == 1);
	CHECK(out[0] == 1 && out[1] == -1);
	float il[2] = { 0.25f, -0.25f };
	const unsigned char* fl[1] = { (const unsigned char*)il };
	CHECK(VP_pcmToS16(VP_PCM_FLT, 2, fl, 1, out) == 1);
	CHECK(out[0] == 8191 && out[1] == -8191);

	// what it can't take
	CHECK(VP_pcmToS16(99, 2, fltp, 1, out) == -1);
	CHECK(VP_pcmToS16(VP_PCM_FLTP, 0, fltp, 1, out) == -1);
	const unsigned char* missing[2] = { (const unsigned char*)left, NULL };
	CHECK(VP_pcmToS16(VP_PCM_FLTP, 2, missing, 1, out) == -1);
	CHECK(VP_pcmToS16(VP_PCM_FLTP, 2, NULL, 1, out) == -1);
	CHECK(VP_pcmToS16(VP_PCM_FLTP, 2, fltp, -1, out) == -1);
	CHECK(VP_pcmToS16(VP_PCM_FLTP, 2, fltp, 0, out) == 0);
}

int main(void) {
	testClipPath();
	testDirAndLibs();
	testSettings();
	testSaver();
	testArm();
	testClock();
	testPace();
	testYuv();
	testPcm();
	CHECK_DONE();
}
