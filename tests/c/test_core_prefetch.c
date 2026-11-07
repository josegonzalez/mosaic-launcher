// The core prefetch: its model on its own, then the plugin against a fake
// host, on a fixture card, with a fake posix_fadvise and clock.

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"

#include "../../plugins/core-prefetch/core_prefetch.c"

#define CARD TEST_DIR "/card"
#define USERDATA CARD "/.userdata/test"
#define OUTSIDE TEST_DIR "/outside"
#define STOCK_SCRIPT "#!/bin/sh\n\nEMU_EXE=gpsp\n\n#####\nminarch.elf \"$CORES_PATH/${EMU_EXE}_libretro.so\" \"$ROM\" &> \"$LOGS_PATH/$EMU_TAG.txt\"\n"
#define PAK_SCRIPT "#!/bin/sh\nEMU_EXE='mgba'\nCORES_PATH=$(dirname \"$0\")\nminarch.elf \"$CORES_PATH/${EMU_EXE}_libretro.so\" \"$ROM\"\n"

///////////////////////////////////////
// fixtures

static void writeFile(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "can't write %s\n", path);
		exit(1);
	}
	fputs(text, f);
	fclose(f);
}

static void writeSized(const char* path, size_t size) {
	FILE* f = fopen(path, "w");
	if (!f) exit(1);
	for (size_t i = 0; i < size; i++) fputc('x', f);
	fclose(f);
}

static void run(const char* command) {
	if (system(command) != 0) {
		fprintf(stderr, "failed: %s\n", command);
		exit(1);
	}
}

static void makeCard(void) {
	run("rm -rf '" TEST_DIR "'");
	run("mkdir -p '" CARD "/Roms/Game Boy Advance (GBA)/Golden Sun' '" CARD "/Roms/Sony PlayStation (PS)/FF7' '" CARD "/Emus/test/MGBA.pak'"
		" '" CARD "/.system/test/paks/Emus/GBA.pak' '" CARD "/.system/test/cores' '" USERDATA "/launcher' '" OUTSIDE "'");
	writeFile(CARD "/.system/test/paks/Emus/GBA.pak/launch.sh", STOCK_SCRIPT);
	writeFile(CARD "/Emus/test/MGBA.pak/launch.sh", PAK_SCRIPT);
	writeSized(CARD "/.system/test/cores/gpsp_libretro.so", 3000);
	writeSized(CARD "/Emus/test/MGBA.pak/mgba_libretro.so", 4000);
	writeSized(CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba", 5000);
	writeSized(CARD "/Roms/Game Boy Advance (GBA)/Golden Sun/Golden Sun.gba", 6000);
	writeFile(CARD "/Roms/Game Boy Advance (GBA)/Golden Sun/Golden Sun.m3u", "Golden Sun.gba\n");
	writeFile(CARD "/Roms/Sony PlayStation (PS)/FF7/FF7.m3u", "\nFF7 (Disc 1).cue\r\nFF7 (Disc 2).cue\n");
	writeFile(CARD "/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 1).cue", "REM x\nFILE \"FF7 (Disc 1).bin\" BINARY\n  TRACK 01 MODE2/2352\n");
	writeSized(CARD "/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 1).bin", 7000);
	writeSized(OUTSIDE "/secret.gba", 100);
	run("ln -s '" OUTSIDE "/secret.gba' '" CARD "/Roms/Game Boy Advance (GBA)/Linked.gba'");
}

static long long inode(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 ? (long long)st.st_ino : -1;
}

///////////////////////////////////////
// fake advise and clock

#define MAX_CALLS 64
static struct {
	long long ino;
	long long offset;
	long long len;
} calls[MAX_CALLS];
static int call_count;
static int fail_advise;
static long long clock_ms;
static long long tick_ms; // the clock moves this much per advise

static int fakeAdvise(int fd, long long offset, long long len) {
	if (fail_advise) return 22;
	struct stat st;
	fstat(fd, &st);
	if (call_count < MAX_CALLS) {
		calls[call_count].ino = (long long)st.st_ino;
		calls[call_count].offset = offset;
		calls[call_count].len = len;
	}
	call_count++;
	clock_ms += tick_ms;
	return 0;
}
static long long fakeNow(void) { return clock_ms; }

static void resetCalls(void) {
	call_count = 0;
	fail_advise = 0;
	clock_ms = 1000;
	tick_ms = 0;
}

// The bytes advised for one file, and 1 when they ran from its start in order.
static long long advisedFor(const char* path, int* in_order) {
	long long ino = inode(path), total = 0, next = 0;
	*in_order = 1;
	for (int i = 0; i < call_count && i < MAX_CALLS; i++) {
		if (calls[i].ino != ino) continue;
		if (calls[i].offset != next) *in_order = 0;
		next = calls[i].offset + calls[i].len;
		total += calls[i].len;
	}
	return total;
}

///////////////////////////////////////
// fake host

static char logged[1024];
static char cfg_rom[8] = "", cfg_mb[8] = "", cfg_ms[8] = "";

static void fakeLog(const char* id, const char* m) { (void)id; snprintf(logged, sizeof(logged), "%s", m); }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (!strcmp(key, "rom") && cfg_rom[0]) return cfg_rom;
	if (!strcmp(key, "max_mb") && cfg_mb[0]) return cfg_mb;
	if (!strcmp(key, "max_ms") && cfg_ms[0]) return cfg_ms;
	return fallback;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = USERDATA,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
};

///////////////////////////////////////
// the model

static void testEmuTag(void) {
	char tag[CP_NAME_LEN];
	CHECK(CP_emuTag(CARD, CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba", tag, sizeof(tag)));
	CHECK_STR(tag, "GBA");
	CHECK(CP_emuTag(CARD, CARD "/Roms/GBA/a/b.gba", tag, sizeof(tag)));
	CHECK_STR(tag, "GBA");
	CHECK(CP_emuTag(CARD, CARD "/Roms/Hacks (x) (SFC)/a.sfc", tag, sizeof(tag)));
	CHECK_STR(tag, "SFC");
	CHECK(!CP_emuTag(CARD, CARD "/Roms/GBA", tag, sizeof(tag)));
	CHECK(!CP_emuTag(CARD, CARD "/Tools/x.pak", tag, sizeof(tag)));
	CHECK(!CP_emuTag(CARD, CARD "/Roms/Empty ()/a.gba", tag, sizeof(tag)));
}

static void testPakLaunch(void) {
	char out[CP_PATH_LEN];
	char want[PATH_MAX];
	// not in Emus/, so stock's own paks
	CHECK(CP_pakLaunch(CARD, "test", "GBA", out, sizeof(out)));
	CHECK(realpath(CARD "/.system/test/paks/Emus/GBA.pak/launch.sh", want));
	CHECK_STR(out, want);
	CHECK(CP_pakLaunch(CARD, "test", "MGBA", out, sizeof(out)));
	CHECK(!CP_pakLaunch(CARD, "test", "SFC", out, sizeof(out)));
	CHECK(!CP_pakLaunch(CARD, "test", "../GBA", out, sizeof(out)));
	CHECK(!CP_pakLaunch(CARD, "../test", "GBA", out, sizeof(out)));
	// a pak whose launch.sh leads off the card
	run("mkdir -p '" CARD "/Emus/test/OUT.pak' && ln -s '" OUTSIDE "/secret.gba' '" CARD "/Emus/test/OUT.pak/launch.sh'");
	CHECK(!CP_pakLaunch(CARD, "test", "OUT", out, sizeof(out)));
}

static void testScriptCore(void) {
	char out[CP_PATH_LEN];
	CHECK(CP_scriptCore(STOCK_SCRIPT, "/c/Emus/t/GBA.pak", "/c", "t", out, sizeof(out)));
	CHECK_STR(out, "/c/.system/t/cores/gpsp_libretro.so");
	CHECK(CP_scriptCore(PAK_SCRIPT, "/c/Emus/t/MGBA.pak", "/c", "t", out, sizeof(out)));
	CHECK_STR(out, "/c/Emus/t/MGBA.pak/mgba_libretro.so");
	CHECK(CP_scriptCore("EMU_EXE=\"snes9x\"\r\nminarch.elf \"${CORES_PATH}/${EMU_EXE}_libretro.so\" x\r\n", "/p", "/c", "t", out, sizeof(out)));
	CHECK_STR(out, "/c/.system/t/cores/snes9x_libretro.so");
	// the last EMU_EXE wins, as in the shell
	CHECK(CP_scriptCore("EMU_EXE=a\nEMU_EXE=b\n$CORES_PATH/${EMU_EXE}_libretro.so\n", "/p", "/c", "t", out, sizeof(out)));
	CHECK_STR(out, "/c/.system/t/cores/b_libretro.so");
	CHECK(!CP_scriptCore("minarch.elf \"$CORES_PATH/${EMU_EXE}_libretro.so\"\n", "/p", "/c", "t", out, sizeof(out)));
	CHECK(!CP_scriptCore("EMU_EXE=$(cat x)\n$CORES_PATH/${EMU_EXE}_libretro.so\n", "/p", "/c", "t", out, sizeof(out)));
	CHECK(!CP_scriptCore("EMU_EXE=gpsp\nCORES_PATH=/tmp\n$CORES_PATH/${EMU_EXE}_libretro.so\n", "/p", "/c", "t", out, sizeof(out)));
	CHECK(!CP_scriptCore("EMU_EXE=gpsp\nretroarch -L gpsp.so\n", "/p", "/c", "t", out, sizeof(out)));
	CHECK(!CP_scriptCore("EMU_EXE=../x\n$CORES_PATH/${EMU_EXE}_libretro.so\n", "/p", "/c", "t", out, sizeof(out)));
}

static void testOverride(void) {
	CHECK(!CP_overrideOn(NULL));
	CHECK(!CP_overrideOn("favorites=on\n"));
	CHECK(CP_overrideOn("favorites=on\n emulator-override = on \n"));
	CHECK(!CP_overrideOn("emulator-override=on\nemulator-override=off\n"));
	CHECK(!CP_overrideOn("emulator-override-x=on\n"));
	CHECK(CP_overrideOn("emulator-override=on\r\n"));

	char key[CP_PATH_LEN];
	CHECK(CP_overrideKey(CARD, CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba", key, sizeof(key)));
	CHECK_STR(key, "/Roms/Game Boy Advance (GBA)/Metroid.gba");
	CHECK(CP_overrideKey(CARD, CARD "/Roms/Game Boy Advance (GBA)/Golden Sun/Golden Sun.gba", key, sizeof(key)));
	CHECK_STR(key, "/Roms/Game Boy Advance (GBA)/Golden Sun/Golden Sun.m3u");
	CHECK(!CP_overrideKey(CARD, CARD "/Roms/GBA/../x.gba", key, sizeof(key)));
	CHECK(!CP_overrideKey(CARD, "/elsewhere/Roms/GBA/x.gba", key, sizeof(key)));

	const char* file = USERDATA "/launcher/emulator-override.txt";
	writeFile(file, "/Roms/A/x.gba\tMGBA\nno tab here\n/Roms/A/x.gba\t../bad\n/Roms/A/y.gba\tGBA\n/Roms/A/x.gba\tSGB\r\n");
	char pak[CP_NAME_LEN];
	CHECK(CP_overridePak(file, "/Roms/A/x.gba", 1 << 20, pak, sizeof(pak)));
	CHECK_STR(pak, "SGB");
	CHECK(CP_overridePak(file, "/Roms/A/y.gba", 1 << 20, pak, sizeof(pak)));
	CHECK_STR(pak, "GBA");
	CHECK(!CP_overridePak(file, "/Roms/A/z.gba", 1 << 20, pak, sizeof(pak)));
	CHECK(!CP_overridePak(file, "/Roms/A/x.gb", 1 << 20, pak, sizeof(pak)));
	// only as far as max_bytes
	CHECK(CP_overridePak(file, "/Roms/A/x.gba", 20, pak, sizeof(pak)));
	CHECK_STR(pak, "MGBA");
	CHECK(!CP_overridePak(USERDATA "/missing.txt", "/Roms/A/x.gba", 1 << 20, pak, sizeof(pak)));
	unlink(file);
}

static void testRomFiles(void) {
	char files[CP_MAX_FILES][CP_PATH_LEN];
	CHECK(CP_romFiles(CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba", files, CP_MAX_FILES) == 1);
	CHECK_STR(files[0], CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba");
	CHECK(CP_romFiles(CARD "/Roms/Sony PlayStation (PS)/FF7/FF7.m3u", files, CP_MAX_FILES) == 2);
	CHECK_STR(files[0], CARD "/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 1).cue");
	CHECK_STR(files[1], CARD "/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 1).bin");
	CHECK(CP_romFiles(CARD "/Roms/Sony PlayStation (PS)/FF7/FF7.m3u", files, 1) == 1);
	CHECK(CP_romFiles(CARD "/Roms/Sony PlayStation (PS)/FF7/missing.m3u", files, CP_MAX_FILES) == 0);
}

static void testCardFile(void) {
	char card[PATH_MAX];
	char out[CP_PATH_LEN];
	CHECK(realpath(CARD, card));
	CHECK(CP_cardFile(card, CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba", out, sizeof(out)));
	CHECK(!CP_cardFile(card, CARD "/Roms/Game Boy Advance (GBA)/Linked.gba", out, sizeof(out)));
	CHECK(!CP_cardFile(card, CARD "/../outside/secret.gba", out, sizeof(out)));
	CHECK(!CP_cardFile(card, CARD "/Roms", out, sizeof(out)));
	CHECK(!CP_cardFile(card, CARD "/Roms/missing.gba", out, sizeof(out)));
}

static void testWarm(void) {
	char files[CP_MAX_FILES][CP_PATH_LEN];
	snprintf(files[0], CP_PATH_LEN, "%s", CARD "/.system/test/cores/gpsp_libretro.so");
	snprintf(files[1], CP_PATH_LEN, "%s", CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba");
	CPBudget budget = { .max_bytes = 1 << 20, .max_ms = 20, .chunk = 1024, .start_ms = 1000,
		.advise = fakeAdvise, .now_ms = fakeNow };
	CPResult r;
	int in_order;

	// every byte of both, a chunk at a time, the core first
	resetCalls();
	CP_warm(files, 2, &budget, &r);
	CHECK(r.bytes == 8000 && r.files == 2 && !r.capped && !r.timed_out && !r.failed);
	CHECK(call_count == 3 + 5);
	CHECK(calls[0].ino == inode(files[0]));
	CHECK(advisedFor(files[0], &in_order) == 3000 && in_order);
	CHECK(advisedFor(files[1], &in_order) == 5000 && in_order);

	// the byte cap ends inside the game
	resetCalls();
	budget.max_bytes = 4500;
	CP_warm(files, 2, &budget, &r);
	CHECK(r.bytes == 4500 && r.capped && r.files == 2);
	CHECK(advisedFor(files[1], &in_order) == 1500 && in_order);

	// the cap used up by the core leaves the game out
	resetCalls();
	budget.max_bytes = 3000;
	CP_warm(files, 2, &budget, &r);
	CHECK(r.bytes == 3000 && r.capped && r.files == 1);

	// no chunk starts once max_ms has passed
	resetCalls();
	budget.max_bytes = 1 << 20;
	tick_ms = 5;
	CP_warm(files, 2, &budget, &r);
	CHECK(r.timed_out && call_count == 4 && r.bytes == 4024);
	CHECK(clock_ms - budget.start_ms == 20);

	// a clock already past it reads nothing
	resetCalls();
	clock_ms = 1020;
	CP_warm(files, 2, &budget, &r);
	CHECK(r.timed_out && call_count == 0 && r.bytes == 0);

	// a failed advise moves on to the next file; a missing file is counted
	resetCalls();
	fail_advise = 1;
	CP_warm(files, 2, &budget, &r);
	CHECK(r.failed == 2 && r.bytes == 0 && r.files == 0);
	resetCalls();
	snprintf(files[0], CP_PATH_LEN, "%s", CARD "/missing.so");
	CP_warm(files, 2, &budget, &r);
	CHECK(r.failed == 1 && r.files == 1 && r.bytes == 5000);
}

///////////////////////////////////////
// the plugin

static int launchGame(const char* path, int type) {
	MosaicLaunch launch = { .path = path, .type = type };
	resetCalls();
	logged[0] = '\0';
	return plugin.pre_launch(&launch);
}

static void testPlugin(void) {
	advise = fakeAdvise;
	now_ms = fakeNow;
	const char* metroid = CARD "/Roms/Game Boy Advance (GBA)/Metroid.gba";
	const char* gpsp = CARD "/.system/test/cores/gpsp_libretro.so";
	const char* mgba = CARD "/Emus/test/MGBA.pak/mgba_libretro.so";
	int in_order;

	// desktop loads, says it's unsupported, and does nothing
	fake.platform = "desktop";
	CHECK(mosaic_plugin_get(&fake)->init(&fake) == 0);
	CHECK(strstr(logged, "unsupported") != NULL);
	CHECK(launchGame(metroid, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE && call_count == 0);

	// without posix_fadvise too
	fake.platform = "test";
	advise = NULL;
	CHECK(plugin.init(&fake) == 0 && !supported);
	advise = fakeAdvise;
	CHECK(plugin.init(&fake) == 0 && supported);

	// the stock pak's core, then the game
	CHECK(launchGame(metroid, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(calls[0].ino == inode(gpsp));
	CHECK(advisedFor(gpsp, &in_order) == 3000);
	CHECK(advisedFor(metroid, &in_order) == 5000);
	CHECK(strstr(logged, "warmed 7 KB in 2 files (the core from GBA.pak)") != NULL);

	// the game setting off leaves the game out
	strcpy(cfg_rom, "off");
	CHECK(launchGame(metroid, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(gpsp, &in_order) == 3000 && advisedFor(metroid, &in_order) == 0);
	cfg_rom[0] = '\0';

	// a multi-disc game: the core, the first disc's cue and its track
	CHECK(launchGame(CARD "/Roms/Sony PlayStation (PS)/FF7/FF7.m3u", MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(CARD "/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 1).bin", &in_order) == 7000);
	CHECK(strstr(logged, "no emulator pak PS") == NULL);
	CHECK(strstr(logged, "the game only, no core from PS.pak") != NULL);

	// a game linked off the card isn't opened
	CHECK(launchGame(CARD "/Roms/Game Boy Advance (GBA)/Linked.gba", MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(OUTSIDE "/secret.gba", &in_order) == 0);
	CHECK(advisedFor(gpsp, &in_order) == 3000);

	// emulator-override's choice, only while it's on
	writeFile(USERDATA "/launcher/emulator-override.txt", "/Roms/Game Boy Advance (GBA)/Metroid.gba\tMGBA\n");
	CHECK(launchGame(metroid, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(gpsp, &in_order) == 3000 && advisedFor(mgba, &in_order) == 0);
	writeFile(USERDATA "/launcher/plugins.txt", "emulator-override=on\n");
	CHECK(launchGame(metroid, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(gpsp, &in_order) == 0 && advisedFor(mgba, &in_order) == 4000);
	CHECK(strstr(logged, "the core from MGBA.pak") != NULL);
	// a choice that isn't a pak on the card falls back to the default, as the override does
	writeFile(USERDATA "/launcher/emulator-override.txt", "/Roms/Game Boy Advance (GBA)/Metroid.gba\tGONE\n");
	CHECK(launchGame(metroid, MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(gpsp, &in_order) == 3000);
	unlink(USERDATA "/launcher/plugins.txt");

	// the time and byte settings
	strcpy(cfg_mb, "1");
	strcpy(cfg_ms, "10");
	writeSized(CARD "/Roms/Game Boy Advance (GBA)/Big.gba", 3 * 1024 * 1024);
	CHECK(launchGame(CARD "/Roms/Game Boy Advance (GBA)/Big.gba", MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(CARD "/Roms/Game Boy Advance (GBA)/Big.gba", &in_order) == 1024 * 1024 - 3000);
	CHECK(strstr(logged, "stopped at max_mb") != NULL);
	strcpy(cfg_mb, "999"); // out of range is the default
	resetCalls();
	tick_ms = 4;
	MosaicLaunch big = { .path = CARD "/Roms/Game Boy Advance (GBA)/Big.gba", .type = MOSAIC_ENTRY_ROM };
	logged[0] = '\0';
	CHECK(plugin.pre_launch(&big) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(call_count == 3 && strstr(logged, "stopped at max_ms") != NULL);
	cfg_mb[0] = cfg_ms[0] = '\0';

	// paks and games outside a system folder are left alone
	CHECK(launchGame(CARD "/Tools/test/Clock.pak", MOSAIC_ENTRY_PAK) == MOSAIC_LAUNCH_CONTINUE && call_count == 0);
	CHECK(launchGame(CARD "/Roms/Game Boy Advance (GBA)", MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE && call_count == 0);
	CHECK(plugin.pre_launch(NULL) == MOSAIC_LAUNCH_CONTINUE);

	// a system with no pak still warms the game
	run("mkdir -p '" CARD "/Roms/Nowhere (NOPE)'");
	writeSized(CARD "/Roms/Nowhere (NOPE)/a.rom", 10);
	CHECK(launchGame(CARD "/Roms/Nowhere (NOPE)/a.rom", MOSAIC_ENTRY_ROM) == MOSAIC_LAUNCH_CONTINUE);
	CHECK(advisedFor(CARD "/Roms/Nowhere (NOPE)/a.rom", &in_order) == 10);
}

int main(void) {
	makeCard();
	testEmuTag();
	testPakLaunch();
	testScriptCore();
	testOverride();
	testRomFiles();
	testCardFile();
	testWarm();
	testPlugin();
	CHECK_DONE();
}
