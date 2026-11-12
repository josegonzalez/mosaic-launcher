// The row badges plugin: its model on its own, then the plugin against a
// fake host and a fixture card.

#include <stdlib.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/row-badges/row_badges.c"

struct MosaicEntry {
	char path[512];
	char tier[16];
	int type;
};

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define PS ROMS "/Sony PlayStation (PS)"

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }

static char cfg_unplayed[8] = "off", cfg_tiers[8] = "off";
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "unplayed")) return cfg_unplayed;
	if (!strcmp(key, "tiers")) return cfg_tiers;
	return fallback;
}

static const char* recents[4];
static int recent_count = 0;
static int fakeRecentsCount(void) { return recent_count; }
static const char* fakeRecentPath(int i) { return i >= 0 && i < recent_count ? recents[i] : NULL; }

static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static int info_reads = 0;
static int fakeEntryInfo(const MosaicEntry* e, const char* key, char* out, size_t size) {
	info_reads++;
	if (strcmp(key, "tier") != 0 || !e->tier[0]) {
		out[0] = '\0';
		return 0;
	}
	snprintf(out, size, "%s", e->tier);
	return 1;
}
static int fakeScale(void) { return 2; }
static int fakeThemeColor(int index) {
	return index == MOSAIC_COLOR_ACCENT ? 0xAA0000 : index == MOSAIC_COLOR_LIST_TEXT ? 0xFFFFFF
		: index == MOSAIC_COLOR_BACKGROUND ? 0x123456 : -1;
}

// the dots drawn since the last reset, and the background rings around them
static MosaicRect drawn[8];
static unsigned int drawn_rgb[8];
static int drawn_count = 0;
static MosaicRect rings[8];
static int ring_count = 0;
static int fakeFillRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s;
	(void)radius;
	if ((c.r << 16 | c.g << 8 | c.b) == 0x123456) {
		if (ring_count < 8) rings[ring_count++] = *r;
		return 0;
	}
	if (drawn_count < 8) {
		drawn[drawn_count] = *r;
		drawn_rgb[drawn_count++] = (unsigned int)(c.r << 16 | c.g << 8 | c.b);
	}
	return 0;
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) {
	(void)s; (void)i; (void)r; (void)n;
	return -1;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = "test",
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/test",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_path = fakeEntryPath,
	.entry_type = fakeEntryType,
	.recents_count = fakeRecentsCount,
	.recent_path = fakeRecentPath,
	.theme_color = fakeThemeColor,
	.screen_scale = fakeScale,
	.fill_round_rect = fakeFillRound,
	.decorate_cell = fakeDecorateCell,
	.entry_info = fakeEntryInfo,
};

static void touch(const char* path) {
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
}

// Decorates a row for entry, 300 wide with its name 100 wide, or with no
// name when named is 0, and returns how many dots went on it.
static int dots(const char* path, int type, const char* tier, int named) {
	MosaicEntry e;
	snprintf(e.path, sizeof(e.path), "%s", path);
	snprintf(e.tier, sizeof(e.tier), "%s", tier);
	e.type = type;
	MosaicRow row = { sizeof(MosaicRow), 0, 0, { 10, 20, 300, 60 }, { 10, 20, named ? 100 : 0, 60 } };
	drawn_count = ring_count = 0;
	CHECK(decorateRow(NULL, &e, &row) == 0);
	CHECK(ring_count == drawn_count);
	return drawn_count;
}

static int hasKey(const char* tag, const char* file) {
	return RB_setHas(&played, tag, file);
}

int main(void) {
	char out[256];

	// tags, as stock files saves and states under them
	CHECK(RB_tagOf("/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, "GB");
	CHECK(RB_tagOf("/sd/Roms", "/sd/Roms/Game Boy (GB)/Puzzle/Tetris.gb", out, sizeof(out)) == 1);
	CHECK_STR(out, "GB");
	CHECK(RB_tagOf("/sd/Roms", "/sd/Roms/Doom/doom.wad", out, sizeof(out)) == 1);
	CHECK_STR(out, "Doom");
	CHECK(RB_tagOf("/sd/Roms", "/sd/Roms/Odd (A) (B)/x.bin", out, sizeof(out)) == 1);
	CHECK_STR(out, "B");
	CHECK(RB_tagOf("/sd/Roms", "/sd/Roms/Game Boy (GB)", out, sizeof(out)) == 0);
	CHECK_STR(out, "");
	CHECK(RB_tagOf("/sd/Roms", "/sd/Roms/Game Boy (GB)/", out, sizeof(out)) == 0);
	CHECK(RB_tagOf("/sd/Roms", "/sd/Roms/Empty ()/x.gb", out, sizeof(out)) == 0);
	CHECK(RB_tagOf("/sd/Roms", "/sd/Tools/desktop/Files.pak", out, sizeof(out)) == 0);
	CHECK(RB_tagOf("/sd/Roms", "/sd/RomsX/Game Boy (GB)/Tetris.gb", out, sizeof(out)) == 0);

	// the set
	RBSet set;
	RB_setInit(&set);
	CHECK(RB_setAdd(&set, "GB", "Tetris.gb") == 0);
	CHECK(RB_setAdd(&set, "PS", "Final Fantasy VII.m3u") == 0);
	CHECK(RB_setAdd(&set, "GB", "Tetris.gb") == 0);
	CHECK(RB_setAdd(&set, "GB", "") == -1);
	CHECK(RB_setAdd(&set, "", "Tetris.gb") == -1);
	CHECK(RB_setAdd(&set, "GB", "a/b") == -1);
	RB_setFinish(&set);
	CHECK(set.count == 2);
	CHECK(RB_setHas(&set, "GB", "Tetris.gb"));
	CHECK(!RB_setHas(&set, "GBC", "Tetris.gb"));
	CHECK(!RB_setHas(&set, "GB", "Tetris.gbc"));
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris.gb") == 1);
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Game Boy (GB)/Puzzle/Tetris.gb") == 1);
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Game Boy (GB)/Dr. Mario.gb") == 0);
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Game Boy Color (GBC)/Tetris.gb") == 0);
	// a disc of a multi-disc game, played by its .m3u
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/Final Fantasy VII/FF7 (Disc 2).cue") == 1);
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/Final Fantasy VIII/FF8 (Disc 1).cue") == 0);
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Game Boy (GB)") == -1);
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Tools/desktop/Files.pak") == -1);
	RB_setFree(&set);
	CHECK(set.count == 0 && !RB_setHas(&set, "GB", "Tetris.gb"));
	CHECK(RB_played(&set, "/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris.gb") == 0);

	// the files played games leave
	CHECK(RB_slotFile("Tetris.gb.txt", out, sizeof(out)) == 1);
	CHECK_STR(out, "Tetris.gb");
	CHECK(RB_slotFile("Tetris.gb.3.txt", out, sizeof(out)) == 1);
	CHECK_STR(out, "Tetris.gb");
	CHECK(RB_slotFile("Tetris.gb.10.bmp", out, sizeof(out)) == 1);
	CHECK_STR(out, "Tetris.gb");
	CHECK(RB_slotFile("Final Fantasy VII.m3u.txt", out, sizeof(out)) == 1);
	CHECK_STR(out, "Final Fantasy VII.m3u");
	CHECK(RB_slotFile("Tetris.gb.bmp", out, sizeof(out)) == 0);
	CHECK(RB_slotFile("Tetris.gb.png", out, sizeof(out)) == 0);
	CHECK(RB_slotFile(".txt", out, sizeof(out)) == 0);
	CHECK(RB_slotFile("._Tetris.gb.txt", out, sizeof(out)) == 0);
	CHECK(RB_slotFile(".3.txt", out, sizeof(out)) == 0);
	CHECK(RB_stateFile("Tetris.gb.st0", out, sizeof(out)) == 1);
	CHECK_STR(out, "Tetris.gb");
	CHECK(RB_stateFile("Tetris.gb.st9", out, sizeof(out)) == 1);
	CHECK_STR(out, "Tetris.gb");
	CHECK(RB_stateFile("Tetris.gb.st", out, sizeof(out)) == 0);
	CHECK(RB_stateFile("Tetris.gb.sav", out, sizeof(out)) == 0);
	CHECK(RB_stateFile(".st0", out, sizeof(out)) == 0);
	CHECK(RB_saveFile("Tetris.gb.sav", out, sizeof(out)) == 1);
	CHECK_STR(out, "Tetris.gb");
	CHECK(RB_saveFile(".sav", out, sizeof(out)) == 0);
	CHECK(RB_saveFile("Tetris.gb.rtc", out, sizeof(out)) == 0);
	CHECK(RB_stateDir("GB-gambatte", out, sizeof(out)) == 1);
	CHECK_STR(out, "GB");
	CHECK(RB_stateDir("PS-pcsx_rearmed", out, sizeof(out)) == 1);
	CHECK_STR(out, "PS");
	CHECK(RB_stateDir(".minui", out, sizeof(out)) == 0);
	CHECK(RB_stateDir("GB-", out, sizeof(out)) == 0);
	CHECK(RB_stateDir("-gambatte", out, sizeof(out)) == 0);
	CHECK(RB_stateDir("Collections", out, sizeof(out)) == 0);
	char tiny[4];
	CHECK(RB_saveFile("Tetris.gb.sav", tiny, sizeof(tiny)) == 0);

	// tiers
	CHECK(RB_tier("3") == 3);
	CHECK(RB_tier(" 1 ") == 1);
	CHECK(RB_tier("5") == 5);
	CHECK(RB_tier("05") == 5);
	CHECK(RB_tier("6") == 0);
	CHECK(RB_tier("0") == 0);
	CHECK(RB_tier("") == 0);
	CHECK(RB_tier("B") == 0);
	CHECK(RB_tier("3 stars") == 0);
	CHECK(RB_tier("-2") == 0);
	CHECK(RB_tier("100003") == 0);
	CHECK(RB_tier(NULL) == 0);

	// dots after the name
	MosaicRect rect = { 10, 20, 300, 60 }, name = { 10, 20, 100, 60 }, none = { 10, 20, 0, 60 }, at[1 + RB_MAX_TIER];
	CHECK(RB_place(&rect, &name, 8, 4, 1, 2, at) == 3);
	CHECK(at[0].x == 118 && at[0].y == 46 && at[0].w == 8 && at[0].h == 8);
	CHECK(at[1].x == 134 && at[2].x == 146 && at[2].y == 46);
	CHECK(RB_place(&rect, &name, 8, 4, 0, 2, at) == 2);
	CHECK(at[0].x == 118 && at[1].x == 130);
	CHECK(RB_place(&rect, &name, 8, 4, 1, 0, at) == 1);
	CHECK(RB_place(&rect, &name, 8, 4, 0, 0, at) == 0);
	CHECK(RB_place(&rect, &name, 8, 4, 0, 9, at) == RB_MAX_TIER);
	// a name filling the row: the top right corner
	MosaicRect wide = { 10, 20, 295, 60 };
	CHECK(RB_place(&rect, &wide, 8, 4, 1, 0, at) == 1);
	CHECK(at[0].x == 10 + 300 - 8 - 8 && at[0].y == 28);
	// just fitting after it stays there
	MosaicRect fits = { 10, 20, 300 - 8 - 8 - 8, 60 };
	CHECK(RB_place(&rect, &fits, 8, 4, 1, 0, at) == 1);
	CHECK(at[0].x == 10 + 300 - 8 - 8 && at[0].y == 46);
	// no name: the top right corner
	CHECK(RB_place(&rect, &none, 8, 4, 1, 1, at) == 2);
	CHECK(at[1].x + at[1].w == 10 + 300 - 8 && at[0].y == 28 && at[0].x == at[1].x - 16);
	CHECK(RB_place(&rect, NULL, 8, 4, 1, 0, at) == 1);
	CHECK(at[0].x == 294 && at[0].y == 28);
	// too small for them
	MosaicRect small = { 0, 0, 20, 60 };
	CHECK(RB_place(&small, NULL, 8, 4, 1, 3, at) == 0);
	MosaicRect flat = { 0, 0, 300, 10 };
	CHECK(RB_place(&flat, NULL, 8, 4, 1, 0, at) == 0);
	CHECK(RB_place(&rect, &name, 0, 4, 1, 0, at) == 0);

	// the plugin, on a card with a game played each way
	CHECK(system("rm -rf '" CARD "' && mkdir -p '" GB "/Puzzle' '" PS "/Final Fantasy VII' '" CARD "/Saves/GB' "
		"'" CARD "/.userdata/shared/GB-gambatte' '" CARD "/.userdata/shared/.minui/GB' '" CARD "/.userdata/shared/.minui/PS'") == 0);
	touch(CARD "/Saves/GB/Saved.gb.sav");
	touch(CARD "/.userdata/shared/GB-gambatte/State.gb.st9");
	touch(CARD "/.userdata/shared/GB-gambatte/Other.gb.srm");
	touch(CARD "/.userdata/shared/.minui/GB/Slot.gb.txt");
	touch(CARD "/.userdata/shared/.minui/GB/Preview.gb.2.bmp");
	touch(CARD "/.userdata/shared/.minui/PS/Final Fantasy VII.m3u.txt");
	touch(CARD "/.userdata/shared/.minui/recent.txt");
	recents[0] = GB "/Recent.gb";
	recents[1] = CARD "/Tools/test/Files.pak";
	recent_count = 2;

	// both off by default: loads, draws nothing, reads nothing
	CHECK(init(&fake) == 0);
	CHECK(played.count == 0);
	info_reads = 0;
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "3", 1) == 0);
	CHECK(info_reads == 0);
	quit();

	strcpy(cfg_unplayed, "on");
	CHECK(init(&fake) == 0);
	CHECK(hasKey("GB", "Saved.gb") && hasKey("GB", "State.gb") && hasKey("GB", "Slot.gb") && hasKey("GB", "Preview.gb"));
	CHECK(hasKey("PS", "Final Fantasy VII.m3u") && hasKey("GB", "Recent.gb"));
	CHECK(!hasKey("GB", "Other.gb") && played.count == 6);
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "3", 1) == 1);
	CHECK(drawn_rgb[0] == 0xAA0000);
	// after the name, centred, at the plugin's size for scale 2
	CHECK(drawn[0].x == 10 + 100 + 8 && drawn[0].y == 20 + (60 - 8) / 2 && drawn[0].w == 8);
	// with a background ring a scale wide around it
	CHECK(rings[0].x == drawn[0].x - 2 && rings[0].y == drawn[0].y - 2 && rings[0].w == 12 && rings[0].h == 12);
	CHECK(dots(GB "/Puzzle/Fresh.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	CHECK(dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	CHECK(dots(GB "/State.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	CHECK(dots(GB "/Slot.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	CHECK(dots(GB "/Preview.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	CHECK(dots(GB "/Recent.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	CHECK(dots(GB "/Other.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	CHECK(dots(PS "/Final Fantasy VII/FF7 (Disc 2).cue", MOSAIC_ENTRY_ROM, "", 1) == 0);
	// folders, paks and games outside Roms get none
	CHECK(dots(PS "/Final Fantasy VII", MOSAIC_ENTRY_DIR, "", 1) == 0);
	CHECK(dots(GB, MOSAIC_ENTRY_DIR, "", 1) == 0);
	CHECK(dots(CARD "/Tools/test/Other.pak", MOSAIC_ENTRY_PAK, "", 1) == 0);
	CHECK(dots(CARD "/Badger/Apple.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	// tiers stay off
	CHECK(dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "3", 1) == 0);

	// a game played while the launcher runs counts once a list is built
	touch(CARD "/Saves/GB/Fresh.gb.sav");
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	listReady(NULL, GB);
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	// and recents cleared count after it too, as after a game
	recent_count = 0;
	CHECK(dots(GB "/Recent.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	postReturn();
	CHECK(dots(GB "/Recent.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	unlink(CARD "/Saves/GB/Fresh.gb.sav");
	quit();
	CHECK(played.count == 0);

	// tier dots, from the sidecar, on games and system folders
	strcpy(cfg_tiers, "on");
	CHECK(init(&fake) == 0);
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "3", 1) == 4);
	CHECK(drawn_rgb[0] == 0xAA0000 && drawn_rgb[1] == 0xFFFFFF && drawn_rgb[3] == 0xFFFFFF);
	CHECK(drawn[1].x == drawn[0].x + 8 + 4 + 4 && drawn[2].x == drawn[1].x + 8 + 4);
	CHECK(dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "2", 1) == 2);
	CHECK(drawn_rgb[0] == 0xFFFFFF && drawn[0].x == 10 + 100 + 8);
	CHECK(dots(GB, MOSAIC_ENTRY_DIR, "5", 1) == 5);
	CHECK(dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "high", 1) == 0);
	// under a layout that gives no name: the cell's top right corner
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "1", 0) == 2);
	CHECK(drawn[1].x + drawn[1].w == 10 + 300 - 8 && drawn[0].y == 20 + 8);
	quit();

	// only tiers
	strcpy(cfg_unplayed, "off");
	CHECK(init(&fake) == 0);
	CHECK(played.count == 0);
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "1", 1) == 1);
	CHECK(drawn_rgb[0] == 0xFFFFFF);
	quit();

	// a launcher without .info sidecars keeps the unplayed dot only
	strcpy(cfg_unplayed, "on");
	fake.entry_info = NULL;
	CHECK(init(&fake) == 0);
	CHECK(!show_tiers && show_unplayed);
	CHECK(dots(GB "/Fresh2.gb", MOSAIC_ENTRY_ROM, "3", 1) == 1);
	quit();
	// and one that can't decorate rows declines
	fake.decorate_cell = NULL;
	CHECK(init(&fake) != 0);

	CHECK(system("rm -rf '" CARD "'") == 0);
	CHECK_DONE();
}
