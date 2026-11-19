// The row badges plugin: its model on its own, then the plugin against a
// fake host and a fixture card.

#include <stdlib.h>
#include <unistd.h>
#include "check.h"
#include "fakeproviders.h"
#include "fakesets.h"
#include "../../plugins/row-badges/row_badges.c"

struct MosaicEntry {
	char path[512];
	char tier[16];
	int type;
};

struct MosaicList {
	MosaicEntry* items[8];
	int count;
};

#define CARD TEST_SDCARD_PATH
#define ROMS CARD "/Roms"
#define GB ROMS "/Game Boy (GB)"
#define PS ROMS "/Sony PlayStation (PS)"

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }

static char cfg_unplayed[8] = "off", cfg_tiers[8] = "off", cfg_favorites[8] = "off", cfg_tagged[8] = "off";
static char cfg_manual[8] = "off", cfg_beaten[8] = "off", cfg_broken[8] = "off", cfg_perfect[8] = "off";
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "beaten")) return cfg_beaten;
	if (!strcmp(key, "broken")) return cfg_broken;
	if (!strcmp(key, "perfect")) return cfg_perfect;
	if (!strcmp(key, "unplayed")) return cfg_unplayed;
	if (!strcmp(key, "favorites")) return cfg_favorites;
	if (!strcmp(key, "tagged")) return cfg_tagged;
	if (!strcmp(key, "manual")) return cfg_manual;
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
// the play history has a session of anything with "Tracked" in its path
static long long fakePlayTime(const char* path) { return strstr(path, "Tracked") ? 60 : 0; }
static int fakePlayCount(const char* path) { return strstr(path, "Tracked") ? 1 : 0; }
static int fakeThemeColor(int index) {
	return index == MOSAIC_COLOR_ACCENT ? 0xAA0000 : index == MOSAIC_COLOR_LIST_TEXT ? 0xFFFFFF
		: index == MOSAIC_COLOR_ACCENT2 ? 0x00AA00 : index == MOSAIC_COLOR_HINT ? 0x0000AA
		: index == MOSAIC_COLOR_BACKGROUND ? 0x123456 : index == MOSAIC_COLOR_MAIN ? 0x777777 : -1;
}

// the dots drawn since the last reset, and the background rings around them
static MosaicRect drawn[8];
static unsigned int drawn_rgb[8];
static int drawn_count = 0;
static MosaicRect rings[8];
static int ring_count = 0;
// the icons' squares, and the cells drawn in them
static MosaicRect boxes[8];
static int box_count = 0;
static int cell_count = 0;
static unsigned int cell_rgb = 0;
static int fakeFillRound(MosaicSurface* s, const MosaicRect* r, int radius, MosaicColor c) {
	(void)s;
	if ((c.r << 16 | c.g << 8 | c.b) == 0x123456 && radius == 2) {
		if (box_count < 8) boxes[box_count++] = *r;
		return 0;
	}
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
static int fakeFillRect(MosaicSurface* s, const MosaicRect* r, MosaicColor c) {
	(void)s;
	(void)r;
	cell_count++;
	cell_rgb = (unsigned int)(c.r << 16 | c.g << 8 | c.b);
	return 0;
}
static int fakeDecorateCell(MosaicSurface* s, int i, const MosaicRect* r, const MosaicRect* n) {
	(void)s; (void)i; (void)r; (void)n;
	return -1;
}

static int fakeSetListCall(const char* name, void (*each)(const char*, void*), void* data) {
	int i = fakeSetFind(name);
	if (i < 0) return -1;
	if (each) fake_set_list[i](name, each, data);
	return 0;
}
static void (*set_changed_fn)(const char*, const char*, void*);
static int fakeSubscribe(const char* name, void (*on_event)(const char*, const char*, void*), void* data) {
	(void)data;
	if (!strcmp(name, "launcher.set_changed")) set_changed_fn = on_event;
	return 0;
}
static int redraws = 0;
static void fakeRedraw(void) { redraws++; }

// favorites, and two tags
static const char* fav_extra = NULL;
static void favList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(GB "/Fav.gb", ctx);
	add(GB "/Both.gb", ctx);
	add(CARD "/Tools/test/Clock.pak", ctx);
	if (fav_extra) add(fav_extra, ctx);
}
static void rpgList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(GB "/Both.gb", ctx);
	add(PS "/Final Fantasy VII/Final Fantasy VII.m3u", ctx);
}
static void puzzleList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(GB "/Tagged.gb", ctx);
}
// the manuals plugin's set
static const char* manual_extra = NULL;
static void manualList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(GB "/Both.gb", ctx);
	add(GB "/Manual.gb", ctx);
	if (manual_extra) add(manual_extra, ctx);
}
// tags' status sets
static const char* beaten_extra = NULL;
static void beatenList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(GB "/Beaten.gb", ctx);
	add(GB "/Both.gb", ctx);
	add(PS "/Final Fantasy VII/Final Fantasy VII.m3u", ctx);
	if (beaten_extra) add(beaten_extra, ctx);
}
static void brokenList(const char* name, void (*add)(const char*, void*), void* ctx) {
	(void)name;
	add(GB "/Both.gb", ctx);
}

// RetroAchievements: Perfect has all 3 unlocked, Partial 2 of 3, None has
// a set with none, and Pending isn't known until pending_known
static int pending_known = 0;
static int ra_asks = 0;
static int fakeAchievements(const char* path, MosaicAchievements* out) {
	ra_asks++;
	out->total = out->unlocked = 0;
	if (strstr(path, "Pending") && !pending_known) return MOSAIC_ACHIEVEMENTS_UNKNOWN;
	if (strstr(path, "Perfect") || strstr(path, "Pending")) out->total = out->unlocked = 3;
	else if (strstr(path, "Partial")) out->total = 3, out->unlocked = 2;
	else if (strstr(path, "None")) out->total = 3;
	else return 0;
	return 1;
}
static int fakeListCount(const MosaicList* l) { return l->count; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) { return i >= 0 && i < l->count ? l->items[i] : NULL; }

static int noHas(const char* name, const char* path) {
	(void)name;
	(void)path;
	return 0;
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
	.fill_rect = fakeFillRect,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.achievements = fakeAchievements,
	.decorate_cell = fakeDecorateCell,
	.entry_info = fakeEntryInfo,
	.play_time = fakePlayTime,
	.play_count = fakePlayCount,
	.event_value = fakeEventValue,
	.provide = fakeProvide,
	.call = fakeCall,
	.set_list = fakeSetListCall,
	.set_names = fakeSetNames,
	.subscribe = fakeSubscribe,
	.request_redraw = fakeRedraw,
};

// what games.played says about path
static int askPlayed(const char* path) {
	char args[600], reply[64];
	snprintf(args, sizeof(args), "path=%s", path);
	if (fakeCall("games.played", args, reply, sizeof(reply)) < 0) return -9;
	if (!strcmp(reply, "played=1")) return 1;
	if (!strcmp(reply, "played=0")) return 0;
	return -8;
}

// the history the model is asked about, by the files it's given
static char asked_history[1024];
static int history(const char* path, void* ctx) {
	size_t n = strlen(asked_history);
	snprintf(asked_history + n, sizeof(asked_history) - n, "%s;", path);
	return ctx && strstr(path, (const char*)ctx) != NULL;
}

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
	drawn_count = ring_count = box_count = cell_count = 0;
	CHECK(decorateRow(NULL, &e, &row) == 0);
	CHECK(ring_count == drawn_count);
	return drawn_count;
}

// The icons decorateRow drew on the row dots last decorated.
static int icons(const char* path, int type) {
	dots(path, type, "", 1);
	return box_count;
}

// A list of ROMs, as list_ready gets it.
static void listOf(MosaicList* list, MosaicEntry* entries, const char* const* paths, int count) {
	list->count = count;
	for (int i = 0; i < count; i++) {
		snprintf(entries[i].path, sizeof(entries[i].path), "%s", paths[i]);
		entries[i].tier[0] = '\0';
		entries[i].type = MOSAIC_ENTRY_ROM;
		list->items[i] = &entries[i];
	}
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
	// the files a game goes by
	char files[RB_GAME_FILES][RB_PATH_MAX];
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris.gb", 0, files, RB_GAME_FILES) == 1);
	CHECK_STR(files[0], "/sd/Roms/Game Boy (GB)/Tetris.gb");
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 2).cue", 0, files, RB_GAME_FILES) == 2);
	CHECK_STR(files[0], "/sd/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 2).cue");
	CHECK_STR(files[1], "/sd/Roms/Sony PlayStation (PS)/FF7/FF7.m3u");
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/FF7/FF7.m3u", 0, files, RB_GAME_FILES) == 1);
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/FF7", 1, files, RB_GAME_FILES) == 2);
	CHECK_STR(files[0], "/sd/Roms/Sony PlayStation (PS)/FF7/FF7.m3u");
	CHECK_STR(files[1], "/sd/Roms/Sony PlayStation (PS)/FF7/FF7.cue");
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/FF7/", 1, files, RB_GAME_FILES) == 2);
	CHECK_STR(files[1], "/sd/Roms/Sony PlayStation (PS)/FF7/FF7.cue");
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/FF7/FF7 (Disc 2).cue", 0, files, 1) == 1);
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Roms/Game Boy (GB)", 1, files, RB_GAME_FILES) == 0);
	CHECK(RB_gameFiles("/sd/Roms", "/sd/Tools/desktop/Files.pak", 1, files, RB_GAME_FILES) == 0);
	// played by the set, or by the history, for any of them
	CHECK(RB_isPlayed(&set, "/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/Final Fantasy VII", 1, NULL, NULL) == 1);
	CHECK(RB_isPlayed(&set, "/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/Final Fantasy VIII", 1, NULL, NULL) == 0);
	asked_history[0] = '\0';
	CHECK(RB_isPlayed(&set, "/sd/Roms", "/sd/Roms/Sony PlayStation (PS)/FF8/FF8 (Disc 1).cue", 0, history, "FF8.m3u") == 1);
	CHECK_STR(asked_history, "/sd/Roms/Sony PlayStation (PS)/FF8/FF8 (Disc 1).cue;/sd/Roms/Sony PlayStation (PS)/FF8/FF8.m3u;");
	CHECK(RB_isPlayed(&set, "/sd/Roms", "/sd/Roms/Game Boy (GB)/Dr. Mario.gb", 0, history, "Mario") == 1);
	CHECK(RB_isPlayed(&set, "/sd/Roms", "/sd/Roms/Game Boy (GB)/Dr. Mario.gb", 0, history, "Zelda") == 0);
	CHECK(RB_isPlayed(NULL, "/sd/Roms", "/sd/Roms/Game Boy (GB)/Dr. Mario.gb", 0, history, "Mario") == 1);
	asked_history[0] = '\0';
	CHECK(RB_isPlayed(&set, "/sd/Roms", "/sd/Tools/desktop/Files.pak", 0, history, "Files") == -1);
	CHECK_STR(asked_history, "");
	// and the reply
	CHECK(RB_reply(1, out, sizeof(out)) == 0);
	CHECK_STR(out, "played=1");
	CHECK(RB_reply(0, out, sizeof(out)) == 0);
	CHECK_STR(out, "played=0");
	CHECK(RB_reply(-1, out, sizeof(out)) == 0);
	CHECK_STR(out, "played=0");
	CHECK(RB_reply(1, out, 8) == -1);
	CHECK_STR(out, "");
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
	MosaicRect rect = { 10, 20, 300, 60 }, name = { 10, 20, 100, 60 }, none = { 10, 20, 0, 60 }, at[RB_MAX_MARKS + RB_MAX_TIER];
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
	// three marks, a gap apart, then the tiers twice the gap after them
	CHECK(RB_place(&rect, &name, 8, 4, 3, 2, at) == 5);
	CHECK(at[0].x == 118 && at[1].x == 130 && at[2].x == 142 && at[3].x == 158 && at[4].x == 170);
	CHECK(RB_place(&rect, &name, 8, 4, 9, 0, at) == RB_MAX_MARKS);
	CHECK(RB_place(&rect, &name, 8, 4, -1, 0, at) == 0);
	// icons between the marks and the tiers, centred on the same line
	MosaicRect all[RB_MAX_BADGES];
	CHECK(RB_layout(&rect, &name, 8, 14, 4, 1, 2, 1, all) == 4);
	CHECK(all[0].x == 118 && all[0].y == 46 && all[0].w == 8);
	CHECK(all[1].x == 118 + 8 + 8 && all[1].y == 43 && all[1].w == 14 && all[1].h == 14);
	CHECK(all[2].x == all[1].x + 14 + 4 && all[2].w == 14);
	CHECK(all[3].x == all[2].x + 14 + 8 && all[3].y == 46 && all[3].w == 8);
	CHECK(RB_layout(&rect, &name, 8, 14, 4, 0, 1, 0, all) == 1);
	CHECK(all[0].x == 118 && all[0].y == 43);
	CHECK(RB_layout(&rect, &name, 8, 14, 4, 0, 9, 0, all) == RB_ICONS);
	// in the corner, the line is as tall as the icons
	CHECK(RB_layout(&rect, &none, 8, 14, 4, 1, 1, 0, all) == 2);
	CHECK(all[1].x + all[1].w == 10 + 300 - 8 && all[1].y == 28 && all[0].y == 31);
	// too tall for the row
	MosaicRect thin = { 10, 20, 300, 12 }, thin_name = { 10, 20, 100, 12 };
	CHECK(RB_layout(&thin, &thin_name, 8, 14, 4, 0, 1, 0, all) == 0);
	CHECK(RB_layout(&thin, &thin_name, 8, 14, 4, 1, 0, 0, all) == 1); // a dot fits
	// RB_place is RB_layout with no icons
	CHECK(RB_layout(&rect, &name, 8, 14, 4, 3, 0, 2, all) == 5 && all[3].x == 158 && all[4].x == 170);

	// the icons' pictures
	CHECK(RB_iconCell(RB_ICON_COMPLETED, 3, 0) && !RB_iconCell(RB_ICON_COMPLETED, 0, 0));
	CHECK(RB_iconCell(RB_ICON_BROKEN, 0, 0) && RB_iconCell(RB_ICON_BROKEN, 3, 3) && !RB_iconCell(RB_ICON_BROKEN, 3, 0));
	CHECK(!RB_iconCell(RB_ICON_TROPHY, 7, 0) && !RB_iconCell(RB_ICON_TROPHY, -1, 0) && !RB_iconCell(RB_ICONS, 0, 0));
	for (int icon = 0; icon < RB_ICONS; icon++) {
		int cells = 0;
		for (int y = 0; y < RB_ICON_CELLS; y++)
			for (int x = 0; x < RB_ICON_CELLS; x++) cells += RB_iconCell(icon, x, y);
		CHECK(cells >= 12); // each reads as a shape, not a dot
		// the same picture as rects, a run a row
		MosaicRect cell_rects[RB_ICON_RECTS];
		int n = RB_iconRects(icon, 100, 200, 2, cell_rects, RB_ICON_RECTS);
		int area = 0;
		for (int i = 0; i < n; i++) {
			area += cell_rects[i].w * cell_rects[i].h;
			CHECK(cell_rects[i].x >= 100 && cell_rects[i].x + cell_rects[i].w <= 100 + 14);
			CHECK(cell_rects[i].y >= 200 && cell_rects[i].y + cell_rects[i].h <= 200 + 14 && cell_rects[i].h == 2);
		}
		CHECK(area == cells * 4);
	}
	MosaicRect cross[RB_ICON_RECTS];
	CHECK(RB_iconRects(RB_ICON_BROKEN, 0, 0, 1, cross, RB_ICON_RECTS) == 11);
	CHECK(cross[0].x == 0 && cross[0].w == 2 && cross[1].x == 5 && cross[1].w == 2);
	CHECK(RB_iconRects(RB_ICON_BROKEN, 0, 0, 1, cross, 3) == 3);
	CHECK(RB_iconRects(RB_ICON_BROKEN, 0, 0, 0, cross, RB_ICON_RECTS) == 0);

	// the flags' sets
	CHECK_STR(RB_flagSet(RB_ICON_BEATEN), "tags.status.beaten");
	CHECK(RB_flagSet(RB_ICON_TROPHY) == NULL && RB_flagSet(-1) == NULL);
	CHECK(RB_flagOf("tags.status.backlog") == RB_ICON_BACKLOG && RB_flagOf("tags.status.other") == -1 && RB_flagOf(NULL) == -1);
	CHECK(RB_flagOf("tags:beaten") == -1);

	// 100%
	CHECK(RB_perfect(1, 3, 3) == 1);
	CHECK(RB_perfect(1, 3, 2) == 0);
	CHECK(RB_perfect(1, 0, 0) == 0);
	CHECK(RB_perfect(0, 0, 0) == 0);
	CHECK(RB_perfect(MOSAIC_ACHIEVEMENTS_UNKNOWN, 0, 0) == -1);

	// paths other plugins' sets hold
	RBSet paths;
	RB_setInit(&paths);
	CHECK(!RB_pathsHas(&paths, "/a"));
	CHECK(RB_pathsAdd(&paths, "/r/PS/FF7/FF7.m3u") == 0 && RB_pathsAdd(&paths, "/r/GB/Tetris.gb") == 0);
	CHECK(RB_pathsAdd(&paths, "/r/GB/Tetris.gb") == 0 && RB_pathsAdd(&paths, "/r/PS/Crash/Crash.cue") == 0);
	CHECK(RB_pathsAdd(&paths, "") == -1 && RB_pathsAdd(&paths, NULL) == -1);
	RB_setFinish(&paths);
	CHECK(paths.count == 3);
	CHECK(RB_pathsHas(&paths, "/r/GB/Tetris.gb") && !RB_pathsHas(&paths, "/r/GB/Tetris"));
	CHECK(RB_hasGame(&paths, "/r/GB/Tetris.gb", 0));
	CHECK(RB_hasGame(&paths, "/r/PS/FF7", 1) && RB_hasGame(&paths, "/r/PS/Crash", 1));
	CHECK(!RB_hasGame(&paths, "/r/PS/FF7", 0) && !RB_hasGame(&paths, "/r/PS/Other", 1));
	RB_setFree(&paths);
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

	// games.played, with the dot off: the card is read on the first call
	fake_provider_count = 0;
	strcpy(cfg_unplayed, "off");
	strcpy(cfg_tiers, "off");
	CHECK(init(&fake) == 0);
	CHECK(played.count == 0);
	CHECK(askPlayed(GB "/Saved.gb") == 1);
	CHECK(played.count > 0);
	CHECK(askPlayed(GB "/Fresh.gb") == 0);
	CHECK(askPlayed(GB "/Tracked.gb") == 1);
	CHECK(askPlayed(CARD "/Tools/test/Other.pak") == 0);
	// read again after a list is built
	touch(CARD "/Saves/GB/Fresh.gb.sav");
	CHECK(askPlayed(GB "/Fresh.gb") == 0);
	listReady(NULL, GB);
	CHECK(askPlayed(GB "/Fresh.gb") == 1);
	unlink(CARD "/Saves/GB/Fresh.gb.sav");
	// a game folder goes by its .m3u
	CHECK(system("mkdir -p '" PS "/Final Fantasy VII'") == 0);
	CHECK(askPlayed(PS "/Final Fantasy VII") == 1);
	CHECK(fakeCall("games.played", "", out, sizeof(out)) == MOSAIC_CALL_FAILED);
	CHECK(fakeCall("games.played", "path=", out, sizeof(out)) == MOSAIC_CALL_FAILED);
	quit();
	// with the dot on, the history counts for it too
	strcpy(cfg_unplayed, "on");
	CHECK(init(&fake) == 0);
	CHECK(dots(GB "/Tracked.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	CHECK(dots(GB "/Fresh.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	quit();

	// favorite and tag marks, after the unplayed dot and before the tiers
	fake_set_count = 0;
	fakeSetRegister("favorites", noHas, favList);
	fakeSetRegister("tags:RPG", noHas, rpgList);
	fakeSetRegister("tags:Puzzle", noHas, puzzleList);
	fakeSetRegister("hidden", noHas, favList); // not a tag
	strcpy(cfg_unplayed, "off");
	strcpy(cfg_favorites, "on");
	CHECK(init(&fake) == 0);
	CHECK(dots(GB "/Fav.gb", MOSAIC_ENTRY_ROM, "", 1) == 1 && drawn_rgb[0] == 0x00AA00);
	CHECK(dots(GB "/Tagged.gb", MOSAIC_ENTRY_ROM, "", 1) == 0); // the tag mark is off
	CHECK(dots(CARD "/Tools/test/Clock.pak", MOSAIC_ENTRY_PAK, "", 1) == 1); // paks too
	quit();
	strcpy(cfg_unplayed, "on");
	strcpy(cfg_tagged, "on");
	strcpy(cfg_tiers, "on");
	set_changed_fn = NULL;
	CHECK(init(&fake) == 0);
	CHECK(set_changed_fn != NULL);
	CHECK(dots(GB "/Both.gb", MOSAIC_ENTRY_ROM, "2", 1) == 5);
	CHECK(drawn_rgb[0] == 0xAA0000 && drawn_rgb[1] == 0x00AA00 && drawn_rgb[2] == 0x0000AA && drawn_rgb[3] == 0xFFFFFF);
	CHECK(drawn[1].x == drawn[0].x + 8 + 4 && drawn[3].x == drawn[2].x + 8 + 4 + 4);
	CHECK(dots(GB "/Tagged.gb", MOSAIC_ENTRY_ROM, "", 1) == 2 && drawn_rgb[1] == 0x0000AA);
	CHECK(dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	// a game folder by its .m3u
	CHECK(dots(PS "/Final Fantasy VII", MOSAIC_ENTRY_DIR, "", 1) == 1 && drawn_rgb[0] == 0x0000AA);
	// a changed set is read again, and other sets' changes are left alone
	fav_extra = GB "/Saved.gb";
	redraws = 0;
	set_changed_fn("launcher.set_changed", "name=hidden", NULL);
	CHECK(redraws == 0 && dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	set_changed_fn("launcher.set_changed", "name=favorites", NULL);
	CHECK(redraws == 1 && dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	fake_set_count = 0; // tags off
	set_changed_fn("launcher.set_changed", "name=tags:Puzzle", NULL);
	CHECK(redraws == 2 && dots(GB "/Tagged.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	CHECK(dots(GB "/Fav.gb", MOSAIC_ENTRY_ROM, "", 1) == 1); // the unplayed dot alone
	fav_extra = NULL;
	quit();
	// the manual mark, after the others, off by default
	fake_set_count = 0;
	fakeSetRegister("favorites", noHas, favList);
	fakeSetRegister("manual", noHas, manualList);
	strcpy(cfg_unplayed, "off");
	strcpy(cfg_tiers, "off");
	CHECK(init(&fake) == 0);
	CHECK(dots(GB "/Manual.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	quit();
	strcpy(cfg_favorites, "on");
	strcpy(cfg_manual, "on");
	CHECK(init(&fake) == 0);
	CHECK(dots(GB "/Manual.gb", MOSAIC_ENTRY_ROM, "", 1) == 1 && drawn_rgb[0] == 0x777777);
	CHECK(dots(GB "/Both.gb", MOSAIC_ENTRY_ROM, "", 1) == 2 && drawn_rgb[0] == 0x00AA00 && drawn_rgb[1] == 0x777777);
	// read again when the set changes
	manual_extra = GB "/Saved.gb";
	redraws = 0;
	CHECK(dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "", 1) == 0);
	set_changed_fn("launcher.set_changed", "name=manual", NULL);
	CHECK(redraws == 1 && dots(GB "/Saved.gb", MOSAIC_ENTRY_ROM, "", 1) == 1);
	manual_extra = NULL;
	quit();
	strcpy(cfg_manual, "off");
	strcpy(cfg_favorites, "off");

	// status icons, off by default, each by itself, after the dots
	fake_set_count = 0;
	fakeSetRegister("tags.status.beaten", noHas, beatenList);
	fakeSetRegister("tags.status.broken", noHas, brokenList);
	CHECK(init(&fake) == 0);
	CHECK(icons(GB "/Both.gb", MOSAIC_ENTRY_ROM) == 0);
	quit();
	strcpy(cfg_beaten, "on");
	set_changed_fn = NULL;
	CHECK(init(&fake) == 0);
	CHECK(set_changed_fn != NULL);
	CHECK(icons(GB "/Both.gb", MOSAIC_ENTRY_ROM) == 1 && cell_rgb == 0xFFFFFF);
	CHECK(boxes[0].w == 14 + 4 && boxes[0].x == 10 + 100 + 8 - 2);
	CHECK(cell_count == 7); // the tick's rows
	CHECK(icons(GB "/Fav.gb", MOSAIC_ENTRY_ROM) == 0);
	CHECK(icons(PS "/Final Fantasy VII", MOSAIC_ENTRY_DIR) == 1); // by its .m3u
	strcpy(cfg_broken, "on");
	quit();
	CHECK(init(&fake) == 0);
	CHECK(icons(GB "/Both.gb", MOSAIC_ENTRY_ROM) == 2);
	CHECK(boxes[1].x == boxes[0].x + 14 + 4);
	CHECK(icons(GB "/Beaten.gb", MOSAIC_ENTRY_ROM) == 1);
	// with the favorite mark: the dot first, then the icons
	quit();
	strcpy(cfg_favorites, "on");
	fakeSetRegister("favorites", noHas, favList);
	CHECK(init(&fake) == 0);
	CHECK(icons(GB "/Both.gb", MOSAIC_ENTRY_ROM) == 2 && drawn_count == 1);
	CHECK(boxes[0].x == drawn[0].x + 8 + 8 - 2);
	// a changed flag's set is read again
	beaten_extra = GB "/Saved.gb";
	redraws = 0;
	CHECK(icons(GB "/Saved.gb", MOSAIC_ENTRY_ROM) == 0);
	set_changed_fn("launcher.set_changed", "name=tags.status.completed", NULL);
	CHECK(redraws == 0);
	set_changed_fn("launcher.set_changed", "name=tags.status.beaten", NULL);
	CHECK(redraws == 1 && icons(GB "/Saved.gb", MOSAIC_ENTRY_ROM) == 1);
	beaten_extra = NULL;
	quit();
	strcpy(cfg_favorites, "off");
	strcpy(cfg_beaten, "off");
	strcpy(cfg_broken, "off");

	// the trophy, off by default, from the games in a list as it's built
	MosaicList ra_list;
	MosaicEntry ra_entries[8];
	const char* const ra_paths[] = { GB "/Perfect.gb", GB "/Partial.gb", GB "/None.gb", GB "/Pending.gb", GB "/Plain.gb" };
	listOf(&ra_list, ra_entries, ra_paths, 5);
	CHECK(system("mkdir -p '" CARD "/.userdata/shared/.ra/offline/cache'") == 0);
	touch(CARD "/.userdata/shared/.ra/offline/cache/achievementsets_0123.bin");
	ra_asks = 0;
	CHECK(init(&fake) == 0);
	listReady(&ra_list, GB);
	CHECK(ra_asks == 0 && icons(GB "/Perfect.gb", MOSAIC_ENTRY_ROM) == 0);
	quit();
	strcpy(cfg_perfect, "on");
	pending_known = 0;
	CHECK(init(&fake) == 0);
	CHECK(ra_asks == 0); // not at start, before a list
	listReady(&ra_list, GB);
	CHECK(ra_asks == 5 && perfect.count == 1 && waiting_count == 1);
	ra_asks = 0;
	CHECK(icons(GB "/Perfect.gb", MOSAIC_ENTRY_ROM) == 1 && cell_count == 11); // the trophy's runs
	CHECK(icons(GB "/Partial.gb", MOSAIC_ENTRY_ROM) == 0);
	CHECK(icons(GB "/None.gb", MOSAIC_ENTRY_ROM) == 0);
	CHECK(icons(GB "/Pending.gb", MOSAIC_ENTRY_ROM) == 0);
	CHECK(ra_asks == 0); // never while drawing
	// tick asks about the unknown one again, every half second, until it's known
	redraws = 0;
	tick(1000);
	CHECK(ra_asks == 1 && waiting_count == 1 && redraws == 0);
	tick(1200);
	CHECK(ra_asks == 1);
	pending_known = 1;
	tick(1600);
	CHECK(ra_asks == 2 && waiting_count == 0 && redraws == 1);
	CHECK(icons(GB "/Pending.gb", MOSAIC_ENTRY_ROM) == 1);
	tick(3000);
	CHECK(ra_asks == 2);
	// the next list doesn't ask about what's known to be 100%
	listReady(&ra_list, GB);
	CHECK(ra_asks == 2 + 3 && perfect.count == 2);
	// with a status icon: the flag first, then the trophy
	quit();
	strcpy(cfg_beaten, "on");
	beaten_extra = GB "/Perfect.gb";
	fake_set_count = 0;
	fakeSetRegister("tags.status.beaten", noHas, beatenList);
	CHECK(init(&fake) == 0);
	listReady(&ra_list, GB);
	CHECK(icons(GB "/Perfect.gb", MOSAIC_ENTRY_ROM) == 2 && cell_count == 7 + 11);
	CHECK(icons(GB "/Beaten.gb", MOSAIC_ENTRY_ROM) == 1 && cell_count == 7);
	beaten_extra = NULL;
	quit();
	strcpy(cfg_beaten, "off");
	// a card with no achievement sets cached asks about nothing
	unlink(CARD "/.userdata/shared/.ra/offline/cache/achievementsets_0123.bin");
	listOf(&ra_list, ra_entries, ra_paths, 5);
	ra_asks = 0;
	CHECK(init(&fake) == 0);
	listReady(&ra_list, GB);
	CHECK(ra_asks == 0);
	quit();
	// nor a launcher that can't read the progress
	touch(CARD "/.userdata/shared/.ra/offline/cache/patch_0123.bin");
	fake.size = offsetof(MosaicHost, achievements);
	CHECK(init(&fake) == 0);
	CHECK(!show_perfect);
	listReady(&ra_list, GB);
	CHECK(ra_asks == 0);
	quit();
	fake.size = sizeof(MosaicHost);
	CHECK(init(&fake) == 0);
	listReady(&ra_list, GB);
	CHECK(ra_asks == 5);
	quit();
	strcpy(cfg_perfect, "off");
	fake_set_count = 0;

	// a launcher without sets: no marks
	fake.size = offsetof(MosaicHost, set_register);
	strcpy(cfg_manual, "on");
	CHECK(init(&fake) == 0);
	CHECK(!show_favorites && !show_tagged && !show_manual);
	strcpy(cfg_manual, "off");
	quit();
	fake.size = sizeof(MosaicHost);
	strcpy(cfg_favorites, "off");
	strcpy(cfg_tagged, "off");
	strcpy(cfg_tiers, "off");

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
