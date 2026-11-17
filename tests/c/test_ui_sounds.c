// ui-sounds' model: which sound a button or event plays, the theme's files
// over the bundled ones, reading WAV files and the volume.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "mosaic_plugin.h"
#include "ui_sounds_model.h"

#define BIT(button) MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_##button)

static void testNames(void) {
	CHECK_STR(UIS_name(UIS_MOVE), "move");
	CHECK_STR(UIS_name(UIS_MENU_OPEN), "menu_open");
	CHECK_STR(UIS_name(UIS_TOAST), "toast");
	CHECK_STR(UIS_name(UIS_NONE), "");
	CHECK_STR(UIS_name(UIS_COUNT), "");
	for (int i = 0; i < UIS_COUNT; i++) CHECK(UIS_byName(UIS_name(i)) == i);
	CHECK(UIS_byName("Move") == UIS_NONE);
	CHECK(UIS_byName("") == UIS_NONE);
	CHECK(UIS_byName(NULL) == UIS_NONE);
}

static void testInput(void) {
	CHECK(UIS_forInput(0, 0) == UIS_NONE);
	CHECK(UIS_forInput(BIT(A), BIT(A)) == UIS_SELECT);
	CHECK(UIS_forInput(BIT(B), BIT(B)) == UIS_BACK);
	CHECK(UIS_forInput(BIT(UP), BIT(UP)) == UIS_MOVE);
	// a held d-pad repeating, as the list moves
	CHECK(UIS_forInput(0, BIT(DOWN)) == UIS_MOVE);
	CHECK(UIS_forInput(0, BIT(LEFT)) == UIS_MOVE);
	CHECK(UIS_forInput(BIT(RIGHT), 0) == UIS_MOVE);
	CHECK(UIS_forInput(0, BIT(L1)) == UIS_MOVE);
	CHECK(UIS_forInput(BIT(R1), 0) == UIS_MOVE);
	// A held down doesn't select again as it repeats
	CHECK(UIS_forInput(0, BIT(A)) == UIS_NONE);
	CHECK(UIS_forInput(0, BIT(B)) == UIS_NONE);
	// select wins over back, and both over a move
	CHECK(UIS_forInput(BIT(A) | BIT(B) | BIT(UP), 0) == UIS_SELECT);
	CHECK(UIS_forInput(BIT(B) | BIT(UP), 0) == UIS_BACK);
	// other buttons play nothing
	CHECK(UIS_forInput(BIT(X) | BIT(Y) | BIT(START) | BIT(SELECT) | BIT(L2) | BIT(R2) | BIT(MENU), 0) == UIS_NONE);
	CHECK(UIS_forInput(BIT(POWER) | BIT(FN1), 0) == UIS_NONE);
}

static void testEvents(void) {
	CHECK(UIS_forEvent("launcher.launch") == UIS_LAUNCH);
	CHECK(UIS_forEvent("launcher.error") == UIS_ERROR);
	CHECK(UIS_forEvent("launcher.menu_open") == UIS_MENU_OPEN);
	CHECK(UIS_forEvent("launcher.menu_close") == UIS_MENU_CLOSE);
	CHECK(UIS_forEvent("launcher.toast") == UIS_TOAST);
	CHECK(UIS_forEvent("launcher.return") == UIS_NONE);
	CHECK(UIS_forEvent("launcher.theme_changed") == UIS_NONE);
	CHECK(UIS_forEvent("music.track") == UIS_NONE);
	CHECK(UIS_forEvent(NULL) == UIS_NONE);
}

static void testVolume(void) {
	CHECK(UIS_volume("50") == 50);
	CHECK(UIS_volume("0") == 0);
	CHECK(UIS_volume("100") == 100);
	CHECK(UIS_volume("101") == UIS_VOLUME_DEFAULT);
	CHECK(UIS_volume("-1") == UIS_VOLUME_DEFAULT);
	CHECK(UIS_volume("loud") == UIS_VOLUME_DEFAULT);
	CHECK(UIS_volume("40%") == UIS_VOLUME_DEFAULT);
	CHECK(UIS_volume("") == UIS_VOLUME_DEFAULT);
	CHECK(UIS_volume(NULL) == UIS_VOLUME_DEFAULT);

	// a system volume of 0 is muted; -1 (desktop) isn't
	CHECK(UIS_audible(50, 10));
	CHECK(UIS_audible(50, -1));
	CHECK(!UIS_audible(50, 0));
	CHECK(!UIS_audible(0, 20));
}

static const char* files[8];
static int isListed(const char* path) {
	for (int i = 0; i < 8 && files[i]; i++) {
		if (!strcmp(files[i], path)) return 1;
	}
	return 0;
}

static void testPick(void) {
	char out[UIS_PATH_MAX];
	files[0] = "/card/Themes/Night/sounds/move.wav";
	files[1] = "/res/sounds/move.wav";
	files[2] = "/res/sounds/select.wav";
	files[3] = NULL;
	// the theme's wins, the bundled one otherwise, and none without either
	CHECK(UIS_pickFile("/card/Themes/Night/sounds", "/res/sounds", UIS_MOVE, isListed, out, sizeof(out)) == UIS_FROM_THEME);
	CHECK_STR(out, "/card/Themes/Night/sounds/move.wav");
	CHECK(UIS_pickFile("/card/Themes/Night/sounds", "/res/sounds", UIS_SELECT, isListed, out, sizeof(out)) == UIS_FROM_BUNDLED);
	CHECK_STR(out, "/res/sounds/select.wav");
	CHECK(UIS_pickFile("/card/Themes/Night/sounds", "/res/sounds", UIS_BACK, isListed, out, sizeof(out)) == UIS_FROM_NONE);
	CHECK_STR(out, "");
	CHECK(UIS_pickFile("", "/res/sounds", UIS_MOVE, isListed, out, sizeof(out)) == UIS_FROM_BUNDLED);
	CHECK_STR(out, "/res/sounds/move.wav");
	CHECK(UIS_pickFile(NULL, "/res/sounds", UIS_MOVE, isListed, out, sizeof(out)) == UIS_FROM_BUNDLED);
	CHECK(UIS_pickFile(NULL, NULL, UIS_MOVE, isListed, out, sizeof(out)) == UIS_FROM_NONE);
	CHECK(UIS_pickFile(NULL, "/res/sounds", UIS_NONE, isListed, out, sizeof(out)) == UIS_FROM_NONE);
	CHECK(UIS_pickFile(NULL, "/res/sounds", UIS_COUNT, isListed, out, sizeof(out)) == UIS_FROM_NONE);
	// a path that doesn't fit isn't cut
	char small[12];
	CHECK(UIS_pickFile(NULL, "/res/sounds", UIS_MOVE, isListed, small, sizeof(small)) == UIS_FROM_NONE);
	CHECK_STR(small, "");

	CHECK(UIS_themeDirOk("/card/Themes/Night/sounds"));
	CHECK(!UIS_themeDirOk("sounds"));
	CHECK(!UIS_themeDirOk(""));
	CHECK(!UIS_themeDirOk(NULL));
}

// A WAV file's bytes, into buf: a fmt chunk of format, channels, rate and
// bits, with an extra chunk before it, and data bytes of data. Returns the
// size.
static size_t wav(unsigned char* buf, int format, int channels, int rate, int bits, const void* data, size_t data_size,
		int extensible) {
	unsigned char* p = buf;
#define PUT16(v) do { *p++ = (unsigned char)((v) & 0xFF); *p++ = (unsigned char)(((v) >> 8) & 0xFF); } while (0)
#define PUT32(v) do { PUT16((v) & 0xFFFF); PUT16(((unsigned long)(v) >> 16) & 0xFFFF); } while (0)
	memcpy(p, "RIFF", 4);
	p += 4;
	unsigned char* riff_size = p;
	p += 4;
	memcpy(p, "WAVE", 4);
	p += 4;
	// a chunk the reader skips, of an odd size and its pad byte
	memcpy(p, "LIST", 4);
	p += 4;
	PUT32(3);
	memcpy(p, "abc", 3);
	p += 4;
	memcpy(p, "fmt ", 4);
	p += 4;
	PUT32(extensible ? 40 : 16);
	PUT16(extensible ? 0xFFFE : format);
	PUT16(channels);
	PUT32(rate);
	PUT32(rate * channels * bits / 8);
	PUT16(channels * bits / 8);
	PUT16(bits);
	if (extensible) {
		PUT16(22);
		PUT16(bits);
		PUT32(channels == 2 ? 3 : 4);
		PUT16(format);
		memset(p, 0, 14);
		p += 14;
	}
	memcpy(p, "data", 4);
	p += 4;
	PUT32(data_size);
	memcpy(p, data, data_size);
	p += data_size;
	size_t size = (size_t)(p - buf);
	unsigned long riff = (unsigned long)size - 8;
	riff_size[0] = riff & 0xFF;
	riff_size[1] = (riff >> 8) & 0xFF;
	riff_size[2] = (riff >> 16) & 0xFF;
	riff_size[3] = (riff >> 24) & 0xFF;
	return size;
#undef PUT16
#undef PUT32
}

static unsigned char buf[400000];

static void testParse(void) {
	UISClip c;
	char why[96];

	// 16-bit stereo
	short s16[] = { 1000, -1000, 32767, -32768 };
	size_t n = wav(buf, 1, 2, 22050, 16, s16, sizeof(s16), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == 0);
	CHECK(c.count == 2 && c.channels == 2 && c.rate == 22050 && !c.cut);
	CHECK(c.frames[0] == 1000 && c.frames[1] == -1000 && c.frames[2] == 32767 && c.frames[3] == -32768);
	UIS_freeClip(&c);
	CHECK(c.frames == NULL && c.count == 0);

	// 8-bit unsigned mono
	unsigned char u8[] = { 128, 255, 0 };
	n = wav(buf, 1, 1, 8000, 8, u8, sizeof(u8), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == 0);
	CHECK(c.count == 3 && c.channels == 1);
	CHECK(c.frames[0] == 0 && c.frames[1] == 127 * 256 && c.frames[2] == -32768);
	UIS_freeClip(&c);

	// 24-bit, its top two bytes
	unsigned char s24[] = { 0x11, 0x34, 0x12, 0x00, 0x00, 0x80 };
	n = wav(buf, 1, 1, 48000, 24, s24, sizeof(s24), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == 0);
	CHECK(c.count == 2 && c.frames[0] == 0x1234 && c.frames[1] == -32768);
	UIS_freeClip(&c);

	// 32-bit, and extensible PCM
	unsigned char s32[] = { 0xFF, 0xFF, 0x34, 0x12 };
	n = wav(buf, 1, 1, 44100, 32, s32, sizeof(s32), 1);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == 0);
	CHECK(c.count == 1 && c.frames[0] == 0x1234);
	UIS_freeClip(&c);

	// 32-bit floats, clamped, NaN as silence
	float f32[] = { 0.5f, -1.0f, 2.0f, NAN };
	n = wav(buf, 3, 2, 44100, 32, f32, sizeof(f32), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == 0);
	CHECK(c.count == 2 && c.frames[0] == 16383 && c.frames[1] == -32767 && c.frames[2] == 32767 && c.frames[3] == 0);
	UIS_freeClip(&c);

	// a data chunk cut short plays its whole frames
	n = wav(buf, 1, 2, 22050, 16, s16, sizeof(s16), 0);
	CHECK(UIS_parseWav(buf, n - 3, &c, why, sizeof(why)) == 0);
	CHECK(c.count == 1);
	UIS_freeClip(&c);

	// longer than the launcher plays is cut to it, and says so
	static short longer[8000 * 3];
	memset(longer, 0, sizeof(longer));
	n = wav(buf, 1, 1, 8000, 16, longer, sizeof(longer), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == 0);
	CHECK(c.count == 8000 * UIS_MAX_MS / 1000 && c.cut);
	UIS_freeClip(&c);

	// what it refuses, leaving the clip empty
	n = wav(buf, 1, 3, 22050, 16, s16, sizeof(s16) - 2, 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't mono or stereo");
	CHECK(c.frames == NULL && c.count == 0);
	n = wav(buf, 1, 1, 4000, 16, s16, sizeof(s16), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't 8000 to 96000 Hz");
	n = wav(buf, 1, 1, 22050, 12, s16, sizeof(s16), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't 8, 16, 24 or 32 bits");
	n = wav(buf, 2, 1, 22050, 16, s16, sizeof(s16), 0); // ADPCM
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't PCM or 32-bit float");
	n = wav(buf, 3, 1, 22050, 16, s16, sizeof(s16), 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't PCM or 32-bit float");
	n = wav(buf, 1, 1, 22050, 16, s16, 0, 0);
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "has no sound");
	n = wav(buf, 1, 1, 22050, 16, s16, sizeof(s16), 0);
	buf[n - sizeof(s16) - 8] = 'x'; // the data chunk's id
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "has no data chunk");
	CHECK(UIS_parseWav((const unsigned char*)"RIFF\0\0\0\0WAVE", 12, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "has no fmt chunk");
	CHECK(UIS_parseWav((const unsigned char*)"RIFF\0\0\0\0AVI ", 12, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't a WAV file");
	CHECK(UIS_parseWav(NULL, 0, &c, why, sizeof(why)) == -1);
	// a block size that doesn't match
	n = wav(buf, 1, 2, 22050, 16, s16, sizeof(s16), 0);
	buf[12 + 12 + 8 + 12] = 2;
	CHECK(UIS_parseWav(buf, n, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "has a block size that doesn't match its format");
	// a fmt chunk past the end
	n = wav(buf, 1, 1, 22050, 16, s16, sizeof(s16), 0);
	CHECK(UIS_parseWav(buf, 12 + 12 + 8 + 10, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "has a short fmt chunk");
	// no why is fine
	CHECK(UIS_parseWav(NULL, 0, &c, NULL, 0) == -1);
}

static void testScale(void) {
	UISClip c;
	short s16[] = { 1000, -1000, 32767, -32768 };
	size_t n = wav(buf, 1, 2, 22050, 16, s16, sizeof(s16), 0);
	CHECK(UIS_parseWav(buf, n, &c, NULL, 0) == 0);
	UIS_scale(&c, 50);
	CHECK(c.frames[0] == 500 && c.frames[1] == -500 && c.frames[2] == 16383 && c.frames[3] == -16384);
	UIS_scale(&c, 100);
	CHECK(c.frames[0] == 500);
	UIS_scale(&c, 0);
	CHECK(c.frames[0] == 0 && c.frames[3] == 0);
	UIS_freeClip(&c);
	UISClip empty = { 0 };
	UIS_scale(&empty, 50);
}

static void testLoad(void) {
	UISClip c;
	char why[96];
	// the bundled set reads, each short and quiet enough
	const char* dir = UIS_TEST_RES "/sounds";
	for (int i = 0; i < UIS_COUNT; i++) {
		char path[UIS_PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s.wav", dir, UIS_name(i));
		int rc = UIS_loadWav(path, &c, why, sizeof(why));
		if (rc != 0) fprintf(stderr, "%s: %s\n", path, why);
		CHECK(rc == 0);
		CHECK(c.rate == 22050 && c.channels == 1 && !c.cut);
		CHECK(c.count > 0 && c.count < 22050 / 2);
		UIS_freeClip(&c);
	}

	const char* tmp = UIS_TEST_TMP;
	CHECK(system("mkdir -p '" UIS_TEST_TMP "'") == 0);
	char path[256];
	snprintf(path, sizeof(path), "%s/big.wav", tmp);
	FILE* f = fopen(path, "wb");
	CHECK(f != NULL);
	if (f) {
		static unsigned char zeros[UIS_FILE_MAX + 1];
		fwrite(zeros, 1, sizeof(zeros), f);
		fclose(f);
	}
	CHECK(UIS_loadWav(path, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "is over the 1024 KiB a sound can be");
	snprintf(path, sizeof(path), "%s/none.wav", tmp);
	CHECK(UIS_loadWav(path, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't a file");
	CHECK(UIS_loadWav(tmp, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't a file");
	snprintf(path, sizeof(path), "%s/text.wav", tmp);
	f = fopen(path, "wb");
	if (f) {
		fputs("not a sound\n", f);
		fclose(f);
	}
	CHECK(UIS_loadWav(path, &c, why, sizeof(why)) == -1);
	CHECK_STR(why, "isn't a WAV file");
	CHECK(system("rm -rf '" UIS_TEST_TMP "'") == 0);
}

int main(void) {
	testNames();
	testInput();
	testEvents();
	testVolume();
	testPick();
	testParse();
	testScale();
	testLoad();
	CHECK_DONE();
}
