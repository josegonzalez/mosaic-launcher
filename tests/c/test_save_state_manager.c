// The save state manager plugin: its model on its own, the states on a
// fixture card, then the plugin against a fake host.

#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/save-state-manager/save_state_manager.c"

#define CARD TEST_SDCARD_PATH
#define SHARED CARD "/.userdata/shared"
#define MINUI SHARED "/.minui"
#define GB_ROMS CARD "/Roms/Game Boy (GB)"
#define OUTSIDE CARD "/Outside"
#define T0 1762600000

static int meet(const MosaicRect* a, const MosaicRect* b) {
	return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static void put(const char* path, const char* text, time_t when) {
	char dir[SM_PATH_MAX];
	snprintf(dir, sizeof(dir), "%s", path);
	*strrchr(dir, '/') = '\0';
	char cmd[SM_PATH_MAX + 32];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
	CHECK(system(cmd) == 0);
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
	if (when) {
		struct timeval times[2] = { { when, 0 }, { when, 0 } };
		utimes(path, times);
	}
}

static int exists(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0;
}

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "'") == 0);
	put(GB_ROMS "/Tetris.gb", "", 0);
	put(GB_ROMS "/Kirby.gb", "", 0);
	put(GB_ROMS "/Dup.gb", "", 0);
	put(GB_ROMS "/Dup.zip", "", 0);
	put(CARD "/Roms/PlayStation (PS)/FF7/FF7.m3u", "FF7 (Disc 1).cue\n", 0);
	put(CARD "/Roms/PlayStation (PS)/FF7/FF7 (Disc 1).cue", "", 0);

	// Tetris: slots 1 and 3 and the sleep state, slot 3 from two cores
	put(SHARED "/GB-gambatte/Tetris.gb.st0", "state", T0 + 100);
	put(SHARED "/GB-gambatte/Tetris.gb.st2", "state", T0 + 200);
	put(SHARED "/GB-mgba/Tetris.gb.st2", "state", T0 + 250);
	put(SHARED "/GB-gambatte/Tetris.gb.st9", "state", T0 + 50);
	put(MINUI "/GB/Tetris.gb.txt", "0", 0);
	put(MINUI "/GB/Tetris.gb.0.bmp", "png", 0);
	put(MINUI "/GB/Tetris.gb.2.bmp", "png", 0);
	put(MINUI "/GB/Tetris.gb.2.txt", "disc", 0);
	put(MINUI "/GB/Tetris.gb.5.bmp", "png", 0); // a preview without a state

	// Kirby in the .srm format, under its name less its extension
	put(SHARED "/GB-gambatte/Kirby.state3", "state", T0 + 300);
	put(MINUI "/GB/Kirby.gb.3.bmp", "png", 0);

	// Dup.gb and Dup.zip both cut to Dup
	put(SHARED "/GB-gambatte/Dup.gb.st0", "state", T0 + 10);
	put(SHARED "/GB-gambatte/Dup.state.1", "state", T0 + 20);

	// another tag, whose .minui folder is a link
	put(SHARED "/GBC-gambatte/Zelda.gbc.st1", "state", T0 + 400);
	put(OUTSIDE "/GBC/Zelda.gbc.1.bmp", "png", 0);
	CHECK(symlink(OUTSIDE "/GBC", MINUI "/GBC") == 0);

	put(SHARED "/PS-pcsx_rearmed/FF7.m3u.st0", "state", T0 + 500);

	// links: a state file, and a whole core folder
	put(OUTSIDE "/Secret.st0", "secret", 0);
	CHECK(symlink(OUTSIDE "/Secret.st0", SHARED "/GB-gambatte/Link.gb.st0") == 0);
	put(OUTSIDE "/linked/Tetris.gb.st4", "state", 0);
	CHECK(symlink(OUTSIDE "/linked", SHARED "/GB-linked") == 0);
}

///////////////////////////////////////
// the model

static int fakeExists(const char* path, void* ctx) {
	(void)ctx;
	static const char* files[] = { "/r/PS/FF7/FF7.m3u", "/r/PS/Box/Box.cue", "/r/PS/Box/Box.m3u" };
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		if (strcmp(path, files[i]) == 0) return 1;
	}
	return 0;
}

static void testNames(void) {
	char out[SM_PATH_MAX];
	CHECK(SM_tagOf("/r", "/r/Game Boy (GB)/Tetris.gb", out, sizeof(out)) && !strcmp(out, "GB"));
	CHECK(SM_tagOf("/r", "/r/GBA/Sub/Game.gba", out, sizeof(out)) && !strcmp(out, "GBA"));
	CHECK(!SM_tagOf("/r", "/r/Tetris.gb", out, sizeof(out)));
	CHECK(!SM_tagOf("/r", "/other/GB/Tetris.gb", out, sizeof(out)));

	CHECK(SM_gameFile("/r/GB/Tetris.gb", 0, fakeExists, NULL, out, sizeof(out)));
	CHECK_STR(out, "/r/GB/Tetris.gb");
	CHECK(SM_gameFile("/r/PS/FF7/FF7 (Disc 1).cue", 0, fakeExists, NULL, out, sizeof(out)));
	CHECK_STR(out, "/r/PS/FF7/FF7.m3u");
	CHECK(SM_gameFile("/r/PS/FF7", 1, fakeExists, NULL, out, sizeof(out)));
	CHECK_STR(out, "/r/PS/FF7/FF7.m3u");
	CHECK(SM_gameFile("/r/PS/Box/", 1, fakeExists, NULL, out, sizeof(out)));
	CHECK_STR(out, "/r/PS/Box/Box.m3u"); // the .cue, then its game's .m3u
	CHECK(!SM_gameFile("/r/PS/Empty", 1, fakeExists, NULL, out, sizeof(out)));

	SM_cut("Tetris.gb", out, sizeof(out));
	CHECK_STR(out, "Tetris");
	SM_cut("Game.p8.png", out, sizeof(out));
	CHECK_STR(out, "Game.p8");
	SM_cut("Long.extension", out, sizeof(out));
	CHECK_STR(out, "Long.extension");
	SM_cut("NoDot", out, sizeof(out));
	CHECK_STR(out, "NoDot");

	SM_displayName("Tetris (USA) [!].gb", out, sizeof(out));
	CHECK_STR(out, "Tetris");
	SM_displayName("Game.p8.png", out, sizeof(out));
	CHECK_STR(out, "Game");
	SM_displayName("(Proto).gb", out, sizeof(out));
	CHECK_STR(out, "(Proto)");

	SM_slotLabel(0, out, sizeof(out));
	CHECK_STR(out, "Slot 1");
	SM_slotLabel(7, out, sizeof(out));
	CHECK_STR(out, "Slot 8");
	SM_slotLabel(8, out, sizeof(out));
	CHECK_STR(out, "Default");
	SM_slotLabel(9, out, sizeof(out));
	CHECK_STR(out, "Auto");

	CHECK(SM_slotOf("2") == 2);
	CHECK(SM_slotOf(" 7\n") == 7);
	CHECK(SM_slotOf("") == 0);
	CHECK(SM_slotOf("x") == 0);
	CHECK(SM_slotOf("-1") == -1);

	SM_date(0, out, sizeof(out));
	CHECK_STR(out, "");
	SM_date(T0, out, sizeof(out));
	CHECK(strlen(out) == 16 && out[4] == '-' && out[10] == ' ' && out[13] == ':');
}

static void testGame(void) {
	makeCard();
	SMCard card_;
	SM_cardInit(&card_, CARD);
	CHECK_STR(card_.shared, SHARED);

	SMGame game;
	CHECK(SM_gameInit(&card_, "GB", "Tetris.gb", NULL, GB_ROMS, &game) == 0);
	CHECK_STR(game.display, "Tetris");
	CHECK(game.use_cut);
	CHECK(SM_gameScan(&card_, &game) == 3);
	CHECK(game.slots[0].slot == 0 && game.slots[1].slot == 2 && game.slots[2].slot == 9);
	CHECK(game.slots[0].saved == T0 + 100);
	CHECK(game.slots[1].files == 2 && game.slots[1].saved == T0 + 250); // the linked core folder isn't one
	CHECK(game.slots[0].has_preview && !strcmp(game.slots[0].preview, MINUI "/GB/Tetris.gb.0.bmp"));
	CHECK(game.slots[1].has_preview);
	CHECK(!game.slots[2].has_preview && !game.slots[2].preview[0]);
	CHECK(game.newest == T0 + 250);
	char files[SM_MAX_FILES][SM_PATH_MAX];
	CHECK(SM_stateFiles(&card_, &game, 2, files, SM_MAX_FILES) == 2);
	CHECK_STR(files[0], SHARED "/GB-gambatte/Tetris.gb.st2");
	CHECK_STR(files[1], SHARED "/GB-mgba/Tetris.gb.st2");

	// the .srm format's name
	CHECK(SM_gameInit(&card_, "GB", "Kirby.gb", "Kirby's", GB_ROMS, &game) == 0);
	CHECK_STR(game.display, "Kirby's");
	CHECK(SM_gameScan(&card_, &game) == 1 && game.slots[0].slot == 3 && game.slots[0].has_preview);

	// Dup.zip in the folder cuts the same, so only Dup.gb's own names count
	CHECK(SM_gameInit(&card_, "GB", "Dup.gb", NULL, GB_ROMS, &game) == 0);
	CHECK(!game.use_cut);
	CHECK(SM_gameScan(&card_, &game) == 1 && game.slots[0].slot == 0);
	// without the folder, nothing else known cuts to Dup
	CHECK(SM_gameInit(&card_, "GB", "Dup.gb", NULL, NULL, &game) == 0);
	CHECK(game.use_cut);
	CHECK(SM_gameScan(&card_, &game) == 2);

	// a linked .minui folder gives no previews
	CHECK(SM_gameInit(&card_, "GBC", "Zelda.gbc", NULL, NULL, &game) == 0);
	CHECK(SM_gameScan(&card_, &game) == 1 && !game.slots[0].has_preview);

	// a linked state isn't one
	CHECK(SM_gameInit(&card_, "GB", "Link.gb", NULL, NULL, &game) == 0);
	CHECK(SM_gameScan(&card_, &game) == 0);

	CHECK(SM_gameInit(&card_, "GB", "../x", NULL, NULL, &game) == -1);
	CHECK(SM_gameInit(&card_, "", "x", NULL, NULL, &game) == -1);
	CHECK(SM_gameInit(&card_, "..", "x", NULL, NULL, &game) == -1);
}

static void testRemove(void) {
	makeCard();
	SMCard card_;
	SM_cardInit(&card_, CARD);
	SMGame game;
	SM_gameInit(&card_, "GB", "Tetris.gb", NULL, GB_ROMS, &game);
	SM_gameScan(&card_, &game);

	// slot 3: both cores' states, its preview and disc file; the last-slot
	// file names slot 1, so it stays
	CHECK(SM_remove(&card_, &game, 1) == 4);
	CHECK(!exists(SHARED "/GB-gambatte/Tetris.gb.st2") && !exists(SHARED "/GB-mgba/Tetris.gb.st2"));
	CHECK(!exists(MINUI "/GB/Tetris.gb.2.bmp") && !exists(MINUI "/GB/Tetris.gb.2.txt"));
	CHECK(exists(MINUI "/GB/Tetris.gb.txt"));
	CHECK(game.count == 2 && game.slots[1].slot == 9 && game.newest == T0 + 100);

	// slot 1, which the last-slot file names
	CHECK(SM_remove(&card_, &game, 0) == 3);
	CHECK(!exists(SHARED "/GB-gambatte/Tetris.gb.st0") && !exists(MINUI "/GB/Tetris.gb.0.bmp"));
	CHECK(!exists(MINUI "/GB/Tetris.gb.txt"));
	CHECK(exists(MINUI "/GB/Tetris.gb.5.bmp"));
	CHECK(exists(OUTSIDE "/linked/Tetris.gb.st4"));

	// gone already: refused, and the game is left as it was
	unlink(SHARED "/GB-gambatte/Tetris.gb.st9");
	CHECK(SM_remove(&card_, &game, 0) == -1);
	CHECK(game.count == 1);
	CHECK(SM_remove(&card_, &game, 5) == -1);

	// a linked .minui folder's preview stays
	SM_gameInit(&card_, "GBC", "Zelda.gbc", NULL, NULL, &game);
	SM_gameScan(&card_, &game);
	CHECK(SM_remove(&card_, &game, 0) == 1);
	CHECK(exists(OUTSIDE "/GBC/Zelda.gbc.1.bmp"));

	// the .srm format's state
	SM_gameInit(&card_, "GB", "Kirby.gb", NULL, GB_ROMS, &game);
	SM_gameScan(&card_, &game);
	CHECK(SM_remove(&card_, &game, 0) == 2);
	CHECK(!exists(SHARED "/GB-gambatte/Kirby.state3"));
	CHECK(exists(SHARED "/GB-gambatte/Link.gb.st0") && exists(OUTSIDE "/Secret.st0"));
}

static void testScanAll(void) {
	makeCard();
	SMCard card_;
	SM_cardInit(&card_, CARD);
	SMList list_;
	SM_listInit(&list_);
	CHECK(SM_any(&card_));
	CHECK(SM_scanAll(&card_, &list_) == 5);
	CHECK_STR(list_.games[0].name, "FF7.m3u");
	CHECK_STR(list_.games[0].tag, "PS");
	CHECK_STR(list_.games[0].display, "FF7");
	CHECK_STR(list_.games[1].name, "Zelda.gbc");
	CHECK_STR(list_.games[2].name, "Kirby.gb");
	CHECK(list_.games[2].count == 1);
	CHECK_STR(list_.games[3].name, "Tetris.gb");
	CHECK(list_.games[3].count == 3);
	CHECK_STR(list_.games[4].name, "Dup.gb");
	SM_listDrop(&list_, 1);
	CHECK(list_.count == 4 && !strcmp(list_.games[1].name, "Kirby.gb"));
	SM_listFree(&list_);

	CHECK(system("rm -rf '" SHARED "'/*-*") == 0);
	CHECK(!SM_any(&card_));
	CHECK(SM_scanAll(&card_, &list_) == 0);
	CHECK(system("rm -rf '" CARD "/.userdata'") == 0);
	CHECK(SM_scanAll(&card_, &list_) == 0);
	SM_listFree(&list_);

	// a linked shared folder is refused whole
	makeCard();
	CHECK(system("mv '" SHARED "' '" OUTSIDE "/shared' && ln -s '" OUTSIDE "/shared' '" SHARED "'") == 0);
	CHECK(!SM_any(&card_));
}

static void testMoves(void) {
	CHECK(SM_step(0, 5, 1, 1) == 1);
	CHECK(SM_step(4, 5, 1, 1) == 0);
	CHECK(SM_step(4, 5, 1, 0) == 4);
	CHECK(SM_step(0, 5, -1, 1) == 4);
	CHECK(SM_step(0, 5, -1, 0) == 0);
	CHECK(SM_row(1, 5, 3, 1) == 4);
	CHECK(SM_row(2, 5, 3, 1) == 4);
	CHECK(SM_row(4, 5, 3, 1) == 4);
	CHECK(SM_row(4, 5, 3, -1) == 1);
	CHECK(SM_afterRemove(2, 2) == 1);
	CHECK(SM_afterRemove(0, 0) == 0);
	CHECK(SM_firstRow(0, 9, 12, 3, 2) == 2);
	CHECK(SM_firstRow(2, 0, 12, 3, 2) == 0);

	MosaicHint left[2], right[2];
	int lc, rc;
	SM_hints(SM_VIEW_GAMES, 3, 1, left, &lc, right, &rc);
	CHECK(lc == 1 && left[0].button == MOSAIC_BUTTON_B && rc == 1 && right[0].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "OPEN");
	SM_hints(SM_VIEW_SLOTS, 3, 1, left, &lc, right, &rc);
	CHECK(rc == 2 && right[0].button == MOSAIC_BUTTON_Y && right[1].button == MOSAIC_BUTTON_A);
	SM_hints(SM_VIEW_SLOTS, 3, 0, left, &lc, right, &rc);
	CHECK(rc == 1 && right[0].button == MOSAIC_BUTTON_A);
	SM_hints(SM_VIEW_FULL, 3, 1, left, &lc, right, &rc);
	CHECK(rc == 1 && right[0].button == MOSAIC_BUTTON_Y);
	SM_hints(SM_VIEW_SLOTS, 0, 1, left, &lc, right, &rc);
	CHECK(lc == 0 && rc == 1 && right[0].button == MOSAIC_BUTTON_B);
}

static const MosaicRect AREA = { 0, 0, 1024, 768 };
static const MosaicRect STATUS = { 794, 30, 200, 90 };

static void testLayout(void) {
	SMGrid grid;
	CHECK(SM_grid(&AREA, &STATUS, 3, 1024, 768, 2 * 36, &grid) == 0);
	CHECK(grid.cols >= 3 && grid.rows >= 1);
	CHECK(grid.cell_h == grid.thumb_h + 72);
	MosaicRect first = SM_cell(&grid, 0);
	CHECK(!meet(&first, &STATUS));
	CHECK(first.y >= STATUS.y + STATUS.h);
	MosaicRect last = SM_cell(&grid, grid.cols * grid.rows - 1);
	CHECK(last.x + last.w <= AREA.w && last.y + last.h <= AREA.h);
	CHECK(SM_cell(&grid, grid.cols * grid.rows).w == 0);
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(SM_grid(&tiny, NULL, 3, 4, 3, 72, &grid) == -1);

	MosaicRect fit = SM_fit(640, 480, &AREA);
	CHECK(fit.w == 1024 && fit.h == 768);
	MosaicRect parts[4];
	CHECK(SM_avoid(&AREA, &STATUS, parts) == 4);
	for (int i = 0; i < 4; i++) CHECK(!meet(&parts[i], &STATUS));
	MosaicRect pill = SM_titlePill(&AREA, &STATUS, 3, 2000);
	CHECK(pill.w > 0 && pill.x + pill.w <= STATUS.x);
}

static void testCopies(void) {
	SMCopy copies[2];
	memset(copies, 0, sizeof(copies));
	MosaicSurface* a = (MosaicSurface*)&copies; // any non-NULL pointer
	CHECK(SM_copyPick(copies, 2) == &copies[0]);
	copies[0] = (SMCopy){ "x", 10, 10, a, 5 };
	copies[1] = (SMCopy){ "y", 10, 10, a, 3 };
	CHECK(SM_copyFind(copies, 2, "x", 10, 10) == &copies[0]);
	CHECK(SM_copyFind(copies, 2, "x", 10, 11) == NULL);
	CHECK(SM_copyPick(copies, 2) == &copies[1]);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
	int owned;
};

struct MosaicEntry {
	char path[SM_PATH_MAX];
	char name[SM_NAME_MAX];
	int type;
};

static MosaicSurface screen_surface = { 1024, 768, 0 };
static MosaicSurface image_surface = { 640, 480, 0 };
static MosaicEntry selected_game;

static MosaicHost fake_host;
static MosaicScreenHooks registered;
static char registered_id[64];
static struct {
	char id[64], label[64];
	int (*available)(void*);
	void (*run)(void*);
} actions[4];
static int action_count = 0;
static char tile_id[64];
static int bound_button = -1;
static char bound_action[64];
static char last_log[512];
static int redraws = 0, live = 0, peak = 0, reloads = 0, selected_asks = 0;
static int blits_into_hole = 0, fills_into_hole = 0;
static int art_state = MOSAIC_ART_READY;
static char opened_screen[64];
static char confirm_text[512];
static void (*confirm_yes)(void*);
static void* confirm_data;
static MosaicHint hints_right[2];
static int hints_right_count = 0;
static char toast_text[64];
static const char* tile_setting = "on";
static const char* button_setting = NULL;
static const char* delete_setting = "on";
static MosaicRect hole_now;

static void fakeLog(const char* id, const char* message) {
	(void)id;
	snprintf(last_log, sizeof(last_log), "%s", message);
}

static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (strcmp(key, "tile") == 0) return tile_setting;
	if (strcmp(key, "button") == 0) return button_setting ? button_setting : fallback;
	if (strcmp(key, "delete") == 0) return delete_setting;
	return fallback;
}

static void fakeRedraw(void) {
	redraws++;
}

static void fakeReload(void) {
	reloads++;
}

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(registered_id, sizeof(registered_id), "%s", id);
	registered = *hooks;
	return 0;
}

static int fakeAction(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)data;
	if (action_count >= 4) return -1;
	snprintf(actions[action_count].id, sizeof(actions[0].id), "%s", id);
	snprintf(actions[action_count].label, sizeof(actions[0].label), "%s", label);
	actions[action_count].available = available;
	actions[action_count].run = run;
	action_count++;
	return 0;
}

static int actionIndex(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (strcmp(actions[i].id, id) == 0) return i;
	}
	return -1;
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
	snprintf(bound_action, sizeof(bound_action), "%s", id);
	return 0;
}

static int fakeOpenScreen(const char* id) {
	snprintf(opened_screen, sizeof(opened_screen), "%s", id);
	return registered.enter(registered.data) == 0 ? 0 : -1;
}

static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)yes;
	snprintf(confirm_text, sizeof(confirm_text), "%s", message);
	confirm_yes = on_yes;
	confirm_data = data;
}

static void fakeToast(const char* message) {
	snprintf(toast_text, sizeof(toast_text), "%s", message);
}

static const MosaicEntry* fakeSelected(void) {
	selected_asks++;
	return selected_game.path[0] ? &selected_game : NULL;
}
static const char* fakeEntryPath(const MosaicEntry* e) {
	return e->path;
}
static const char* fakeEntryName(const MosaicEntry* e) {
	return e->name;
}
static int fakeEntryType(const MosaicEntry* e) {
	return e->type;
}

static int fakeThemeColor(int index) {
	return 0x101010 * index;
}
static int fakeScreenWidth(void) {
	return 1024;
}
static int fakeScreenHeight(void) {
	return 768;
}
static int fakeScale(void) {
	return 3;
}

static MosaicSurface* fakeScaleSurface(const char* id, const MosaicSurface* src, int w, int h, int fit) {
	(void)id;
	(void)src;
	(void)fit;
	if (++live > peak) peak = live;
	MosaicSurface* s = calloc(1, sizeof(*s));
	s->w = w;
	s->h = h;
	s->owned = 1;
	return s;
}

static int fakeFree(const char* id, MosaicSurface* s) {
	(void)id;
	if (!s || !s->owned) return -1;
	live--;
	free(s);
	return 0;
}

static int fakeSize(const MosaicSurface* s, int* w, int* h) {
	if (!s) return -1;
	*w = s->w;
	*h = s->h;
	return 0;
}

static int fakeFill(MosaicSurface* target, const MosaicRect* rect, MosaicColor color) {
	(void)target;
	(void)color;
	if (!rect || (hole_now.w > 0 && meet(rect, &hole_now))) fills_into_hole++;
	return 0;
}

static int fakeFillRound(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color) {
	(void)radius;
	return fakeFill(target, rect, color);
}

static int fakeBlit(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst) {
	(void)target;
	(void)src;
	(void)src_rect;
	if (!dst || (hole_now.w > 0 && meet(dst, &hole_now))) blits_into_hole++;
	return 0;
}

static int fakeTextWidth(int font, const char* text) {
	(void)font;
	return 20 * (int)strlen(text);
}
static int fakeTextHeight(int font) {
	(void)font;
	return 30;
}
static int fakeTruncate(int font, const char* text, int max_width, char* out, size_t size) {
	snprintf(out, size, "%s", text);
	int w = fakeTextWidth(font, out);
	return w < max_width ? w : max_width;
}
static int fakeDrawText(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width) {
	(void)target;
	(void)color;
	(void)x;
	(void)y;
	int w = fakeTextWidth(font, text);
	return w < max_width ? w : max_width;
}

static int fakeArtLoad(const char* path, const MosaicSurface** out) {
	(void)path;
	if (art_state != MOSAIC_ART_READY) return art_state;
	*out = &image_surface;
	return MOSAIC_ART_READY;
}

static int fakeArtGet(const MosaicEntry* entry, int kind, const MosaicSurface** out) {
	(void)entry;
	(void)kind;
	(void)out;
	return MOSAIC_ART_MISSING;
}

static int fakeDrawHints(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	(void)screen;
	(void)drawn;
	if (group != MOSAIC_HINTS_RIGHT) return 0;
	for (int i = 0; i < count && i < 2; i++) hints_right[i] = hints[i];
	hints_right_count = count;
	return 0;
}

static void setupHost(void) {
	memset(&fake_host, 0, sizeof(fake_host));
	fake_host.size = sizeof(MosaicHost);
	fake_host.api_version = MOSAIC_API_VERSION;
	fake_host.sdcard_path = CARD;
	fake_host.log = fakeLog;
	fake_host.cfg_get = fakeCfgGet;
	fake_host.request_redraw = fakeRedraw;
	fake_host.reload_lists = fakeReload;
	fake_host.register_action = fakeAction;
	fake_host.bind_button = fakeBind;
	fake_host.selected_entry = fakeSelected;
	fake_host.entry_path = fakeEntryPath;
	fake_host.entry_name = fakeEntryName;
	fake_host.entry_type = fakeEntryType;
	fake_host.toast = fakeToast;
	fake_host.confirm = fakeConfirm;
	fake_host.quick_tile = fakeTile;
	fake_host.register_screen = fakeRegister;
	fake_host.open_screen = fakeOpenScreen;
	fake_host.theme_color = fakeThemeColor;
	fake_host.screen_width = fakeScreenWidth;
	fake_host.screen_height = fakeScreenHeight;
	fake_host.screen_scale = fakeScale;
	fake_host.surface_scale = fakeScaleSurface;
	fake_host.surface_free = fakeFree;
	fake_host.surface_size = fakeSize;
	fake_host.fill_rect = fakeFill;
	fake_host.fill_round_rect = fakeFillRound;
	fake_host.blit = fakeBlit;
	fake_host.text_width = fakeTextWidth;
	fake_host.text_height = fakeTextHeight;
	fake_host.text_truncate = fakeTruncate;
	fake_host.draw_text = fakeDrawText;
	fake_host.art_get = fakeArtGet;
	fake_host.art_load = fakeArtLoad;
	fake_host.draw_hints = fakeDrawHints;
}

static const MosaicPlugin* start(void) {
	setupHost();
	tile_id[0] = '\0';
	bound_button = -1;
	action_count = 0;
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	return p;
}

static void selectGame(const char* path, const char* name, int type) {
	snprintf(selected_game.path, sizeof(selected_game.path), "%s", path);
	snprintf(selected_game.name, sizeof(selected_game.name), "%s", name);
	selected_game.type = type;
}

static int draw(void) {
	MosaicScreenView v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	v.area = AREA;
	v.status = STATUS;
	v.screen = SCREEN_ID;
	hole_now = STATUS;
	hints_right_count = 0;
	return registered.render(&screen_surface, &v, registered.data);
}

static int press(int button) {
	MosaicInput in = { sizeof(MosaicInput), MOSAIC_BUTTON_BIT(button), MOSAIC_BUTTON_BIT(button), 0,
		MOSAIC_BUTTON_BIT(button) };
	return registered.input(&in, registered.data);
}

// the log line, up to its date
static int logStarts(const char* start_text) {
	return strncmp(last_log, start_text, strlen(start_text)) == 0;
}

static void testInit(void) {
	start();
	CHECK_STR(registered_id, "save-state-manager.main");
	CHECK(registered.flags == 0);
	CHECK(registered.enter && registered.exit && registered.input && registered.render);
	CHECK(action_count == 2);
	CHECK(actionIndex("save-state-manager.game") >= 0 && actionIndex("save-state-manager.open") >= 0);
	CHECK_STR(actions[actionIndex("save-state-manager.game")].label, "SAVE STATES");
	CHECK_STR(actions[actionIndex("save-state-manager.open")].label, "Save states");
	CHECK_STR(tile_id, "save-state-manager.open");
	CHECK(bound_button == MOSAIC_BUTTON_Y);
	CHECK_STR(bound_action, "save-state-manager.game");

	tile_setting = "off";
	button_setting = "none";
	start();
	CHECK_STR(tile_id, "");
	CHECK(bound_button == -1);
	button_setting = "R2";
	start();
	CHECK(bound_button == MOSAIC_BUTTON_R2);
	tile_setting = "on";
	button_setting = NULL;

	// an older launcher
	setupHost();
	fake_host.size = offsetof(MosaicHost, draw_hints);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) != 0);
}

static void testActions(void) {
	makeCard();
	const MosaicPlugin* p = start();
	int game_action = actionIndex(GAME_ID), all_action = actionIndex(OPEN_ID);
	selectGame(GB_ROMS "/Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM);
	CHECK(actions[game_action].available(NULL) == 1);
	CHECK(actions[all_action].available(NULL) == 1);
	// asked again for the same game: the answer is kept
	CHECK(system("rm -rf '" SHARED "/GB-gambatte' '" SHARED "/GB-mgba'") == 0);
	CHECK(actions[game_action].available(NULL) == 1);
	// until a game returns
	p->post_return();
	CHECK(actions[game_action].available(NULL) == 0);
	selectGame(CARD "/Roms/PlayStation (PS)/FF7", "FF7", MOSAIC_ENTRY_DIR);
	CHECK(actions[game_action].available(NULL) == 1);
	selectGame(CARD "/Roms/PlayStation (PS)/FF7/FF7 (Disc 1).cue", "FF7 (Disc 1)", MOSAIC_ENTRY_ROM);
	CHECK(actions[game_action].available(NULL) == 1);
	selectGame(CARD "/Roms/Game Boy (GB)", "Game Boy", MOSAIC_ENTRY_DIR);
	CHECK(actions[game_action].available(NULL) == 0);
	selectGame("", "", MOSAIC_ENTRY_ROM);
	CHECK(actions[game_action].available(NULL) == 0);
	CHECK(system("rm -rf '" SHARED "'") == 0);
	p->post_return();
	CHECK(actions[all_action].available(NULL) == 0);
}

static void testGameScreen(void) {
	makeCard();
	start();
	selectGame(GB_ROMS "/Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM);
	actions[actionIndex(GAME_ID)].run(NULL);
	CHECK_STR(opened_screen, "save-state-manager.main");
	CHECK_STR(last_log, "Tetris: 3 save states");

	// the slots, in slot order
	CHECK(draw() == 0);
	CHECK(logStarts("slots 0 of 3: Tetris, Slot 1, "));
	SMGrid grid;
	CHECK(SM_grid(&AREA, &STATUS, 3, 1024, 768, 2 * 36, &grid) == 0);
	char columns[32];
	snprintf(columns, sizeof(columns), ", %d columns", grid.cols);
	CHECK(strstr(last_log, columns) != NULL);
	CHECK(hints_right_count == 2 && hints_right[0].button == MOSAIC_BUTTON_Y && hints_right[1].button == MOSAIC_BUTTON_A);
	CHECK(live == 2); // a copy of each preview; the sleep state has none
	CHECK(fills_into_hole == 0 && blits_into_hole == 0);
	press(MOSAIC_BUTTON_LEFT); // wraps on a press
	draw();
	CHECK(logStarts("slots 2 of 3: Tetris, Auto, "));

	// the full view, left and right stepping
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK(logStarts("view 2 of 3: Tetris, Auto, ") && strstr(last_log, "preview none"));
	press(MOSAIC_BUTTON_RIGHT);
	draw();
	CHECK(logStarts("view 0 of 3: Tetris, Slot 1, ") && strstr(last_log, "preview drawn"));
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_Y);
	CHECK(fills_into_hole == 0 && blits_into_hole == 0);
	press(MOSAIC_BUTTON_RIGHT);
	art_state = MOSAIC_ART_LOADING;
	draw();
	CHECK(logStarts("view 1 of 3: Tetris, Slot 3, ") && strstr(last_log, "preview loading"));
	art_state = MOSAIC_ART_READY;

	// deleting asks first, then removes the slot's files
	CHECK(press(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_STAY);
	CHECK(strstr(confirm_text, "Delete this save state?\nTetris \xc2\xb7 Slot 3 \xc2\xb7 ") != NULL);
	CHECK(exists(SHARED "/GB-mgba/Tetris.gb.st2"));
	confirm_yes(confirm_data);
	CHECK_STR(last_log, "deleted Slot 3 of Tetris.gb: 4 files, 2 left");
	CHECK_STR(toast_text, "Save state deleted");
	CHECK(!exists(SHARED "/GB-mgba/Tetris.gb.st2") && !exists(MINUI "/GB/Tetris.gb.2.bmp"));
	CHECK(exists(MINUI "/GB/Tetris.gb.txt"));
	draw();
	CHECK(logStarts("view 1 of 2: Tetris, Auto, "));

	// a file gone by the time the confirm comes back
	press(MOSAIC_BUTTON_Y);
	unlink(SHARED "/GB-gambatte/Tetris.gb.st9");
	confirm_yes(confirm_data);
	CHECK_STR(last_log, "can't delete Auto of Tetris.gb");

	// B goes back to the slots, then closes, and the lists reload
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK(logStarts("slots 1 of 2: Tetris, Auto, "));
	reloads = 0;
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
	CHECK(live == 0);
	CHECK(reloads == 1);

	// the last state: the screen says so
	actions[actionIndex(GAME_ID)].run(NULL);
	CHECK_STR(last_log, "Tetris: 1 save states");
	draw();
	press(MOSAIC_BUTTON_Y);
	confirm_yes(confirm_data);
	CHECK_STR(last_log, "deleted Slot 1 of Tetris.gb: 3 files, 0 left");
	CHECK(!exists(MINUI "/GB/Tetris.gb.txt"));
	draw();
	CHECK_STR(last_log, "no save states");
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_B);
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
	CHECK(live == 0);
}

static void testAllScreen(void) {
	makeCard();
	start();
	actions[actionIndex(OPEN_ID)].run(NULL);
	CHECK_STR(last_log, "5 games with save states");
	draw();
	CHECK_STR(last_log, "games 0 of 5: FF7, 1 states");
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_A);
	CHECK(fills_into_hole == 0 && blits_into_hole == 0);
	confirm_text[0] = '\0';
	press(MOSAIC_BUTTON_Y); // nothing to delete on the games
	CHECK_STR(confirm_text, "");
	press(MOSAIC_BUTTON_RIGHT);
	press(MOSAIC_BUTTON_RIGHT);
	draw();
	CHECK_STR(last_log, "games 2 of 5: Kirby, 1 states");

	// a game's slots; deleting its only one drops it from the games
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logStarts("slots 0 of 1: Kirby, Slot 4, "));
	press(MOSAIC_BUTTON_Y);
	confirm_yes(confirm_data);
	CHECK_STR(last_log, "deleted Slot 4 of Kirby.gb: 2 files, 0 left");
	draw();
	CHECK_STR(last_log, "games 2 of 4: Tetris, 3 states");
	press(MOSAIC_BUTTON_A);
	draw();
	CHECK(logStarts("slots 0 of 3: Tetris, Slot 1, "));
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK_STR(last_log, "games 2 of 4: Tetris, 3 states");
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
	CHECK(live == 0);

	// no games
	CHECK(system("rm -rf '" SHARED "'") == 0);
	actions[actionIndex(OPEN_ID)].run(NULL);
	CHECK_STR(last_log, "0 games with save states");
	draw();
	CHECK_STR(last_log, "no save states");
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
}

static void testNoDelete(void) {
	makeCard();
	delete_setting = "off";
	start();
	selectGame(GB_ROMS "/Tetris.gb", "Tetris", MOSAIC_ENTRY_ROM);
	actions[actionIndex(GAME_ID)].run(NULL);
	draw();
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_A);
	confirm_text[0] = '\0';
	press(MOSAIC_BUTTON_Y);
	CHECK_STR(confirm_text, "");
	registered.exit(registered.data);
	delete_setting = "on";
}

// more games than the preview pool holds: copies stay bounded
static void testBounded(void) {
	makeCard();
	for (int i = 0; i < 60; i++) {
		char path[256];
		snprintf(path, sizeof(path), SHARED "/GB-gambatte/Game %02d.gb.st0", i);
		put(path, "state", T0 + 1000 + i);
		snprintf(path, sizeof(path), MINUI "/GB/Game %02d.gb.0.bmp", i);
		put(path, "png", 0);
	}
	start();
	actions[actionIndex(OPEN_ID)].run(NULL);
	CHECK_STR(last_log, "65 games with save states");
	peak = live;
	for (int i = 0; i < 70; i++) {
		draw();
		press(MOSAIC_BUTTON_RIGHT);
	}
	CHECK(peak <= THUMB_COPIES + FULL_COPIES);
	registered.exit(registered.data);
	CHECK(live == 0);
	mosaic_plugin_get(&fake_host)->quit();
	CHECK(live == 0);
}

int main(void) {
	setenv("TZ", "UTC", 1);
	tzset();
	testNames();
	testGame();
	testRemove();
	testScanAll();
	testMoves();
	testLayout();
	testCopies();
	testInit();
	testActions();
	testGameScreen();
	testAllScreen();
	testNoDelete();
	testBounded();
	CHECK(system("rm -rf '" CARD "'") == 0);
	CHECK_DONE();
}
