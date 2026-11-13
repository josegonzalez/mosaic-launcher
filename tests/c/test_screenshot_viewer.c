// The screenshot viewer plugin: its model on its own, the folder on a
// fixture card, then the plugin against a fake host.

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/screenshot-viewer/screenshot_viewer.c"

#define CARD TEST_SDCARD_PATH
#define SHOTS CARD "/Screenshots"
#define OUTSIDE CARD "/Outside"

static int rectIs(MosaicRect r, int x, int y, int w, int h) {
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

static int meet(const MosaicRect* a, const MosaicRect* b) {
	return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

///////////////////////////////////////
// the model

static void testNames(void) {
	CHECK(SV_isImage("Tetris.2026-11-08-14-03-05.png"));
	CHECK(SV_isImage("Shot.JPG"));
	CHECK(SV_isImage("Shot.jpeg"));
	CHECK(!SV_isImage("._Tetris.2026-11-08-14-03-05.png"));
	CHECK(!SV_isImage(".png"));
	CHECK(!SV_isImage("notes.txt"));
	CHECK(!SV_isImage("Shot.png.bak"));
	CHECK(!SV_isImage("a/b.png"));
	CHECK(!SV_isImage(""));
	CHECK(!SV_isImage(NULL));

	SVShot shot;
	CHECK(SV_parse("Tetris.2026-11-08-14-03-05.png", &shot) == 0);
	CHECK_STR(shot.file, "Tetris.2026-11-08-14-03-05.png");
	CHECK_STR(shot.game, "Tetris");
	CHECK(shot.has_date);
	CHECK(shot.year == 2026 && shot.month == 11 && shot.day == 8);
	CHECK(shot.hour == 14 && shot.minute == 3 && shot.second == 5);
	CHECK(shot.key == 20261108140305LL);

	char text[300];
	SV_date(&shot, text, sizeof(text));
	CHECK_STR(text, "2026-11-08 14:03");
	SV_title(&shot, text, sizeof(text));
	CHECK_STR(text, "Tetris \xc2\xb7 2026-11-08 14:03");

	// a game with dots in its name, as stock's names keep them
	CHECK(SV_parse("Dr. Mario.2025-01-31-23-59-59.png", &shot) == 0);
	CHECK_STR(shot.game, "Dr. Mario");
	CHECK(shot.key == 20250131235959LL);

	// no date, or not a real one: the name less its extension
	static const char* undated[] = {
		"Tetris.png",
		"Tetris.2026-13-08-14-03-05.png",
		"Tetris.2026-11-08-24-03-05.png",
		"Tetris.2026-11-08 14-03-05.png",
		"Tetris-2026-11-08-14-03-05.png",
		"2026-11-08-14-03-05.PNG",
	};
	for (size_t i = 0; i < sizeof(undated) / sizeof(undated[0]); i++) {
		CHECK(SV_parse(undated[i], &shot) == 0);
		CHECK(!shot.has_date);
		CHECK(shot.key == 0);
	}
	CHECK(SV_parse("Tetris.png", &shot) == 0);
	CHECK_STR(shot.game, "Tetris");
	SV_date(&shot, text, sizeof(text));
	CHECK_STR(text, "");
	SV_title(&shot, text, sizeof(text));
	CHECK_STR(text, "Tetris");
	CHECK(SV_parse("Tetris.2026-11-08-14-03-05.png.txt", &shot) == -1);
	CHECK(SV_parse("._Tetris.png", &shot) == -1);

	struct tm tm = { 0 };
	tm.tm_year = 2024 - 1900;
	tm.tm_mon = 1;
	tm.tm_mday = 29;
	tm.tm_hour = 7;
	tm.tm_min = 8;
	tm.tm_sec = 9;
	CHECK(SV_keyOf(&tm) == 20240229070809LL);
}

static void testSort(void) {
	SVList list;
	SV_listInit(&list);
	static const char* files[] = {
		"B.2026-01-01-00-00-00.png",
		"A.2026-06-01-00-00-00.png",
		"C.2026-01-01-00-00-00.png",
		"D.2025-12-31-23-59-59.png",
	};
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		SVShot shot;
		SV_parse(files[i], &shot);
		CHECK(SV_listAdd(&list, &shot) == 0);
	}
	SV_sort(&list);
	CHECK(list.count == 4);
	CHECK_STR(list.shots[0].game, "A");
	CHECK_STR(list.shots[1].game, "C"); // the same time: by name, last first
	CHECK_STR(list.shots[2].game, "B");
	CHECK_STR(list.shots[3].game, "D");
	SV_listFree(&list);
	CHECK(list.count == 0 && list.shots == NULL);
}

static void touch(const char* path, time_t when) {
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs("x", f);
	fclose(f);
	if (when) {
		struct timespec times[2] = { { when, 0 }, { when, 0 } };
		utimensat(AT_FDCWD, path, times, 0);
	}
}

static int exists(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0;
}

static void makeCard(void) {
	CHECK(system("rm -rf '" CARD "'") == 0);
	mkdir(CARD, 0755);
	mkdir(SHOTS, 0755);
	mkdir(OUTSIDE, 0755);
	touch(SHOTS "/Tetris.2026-11-08-14-03-05.png", 0);
	touch(SHOTS "/Kirby's Dream Land.2026-11-07-09-00-00.png", 0);
	touch(SHOTS "/Dr. Mario.2026-11-09-08-30-00.png", 0);
	// no date: by its mtime, local 2026-11-08 12:00
	struct tm tm = { 0 };
	tm.tm_year = 2026 - 1900;
	tm.tm_mon = 10;
	tm.tm_mday = 8;
	tm.tm_hour = 12;
	tm.tm_isdst = -1;
	touch(SHOTS "/Copied.png", mktime(&tm));
	// not screenshots
	touch(SHOTS "/._Tetris.2026-11-08-14-03-05.png", 0);
	touch(SHOTS "/notes.txt", 0);
	mkdir(SHOTS "/Folder.png", 0755);
	touch(OUTSIDE "/Secret.png", 0);
	CHECK(symlink(OUTSIDE "/Secret.png", SHOTS "/Link.2026-12-01-00-00-00.png") == 0);
}

static void testScan(void) {
	makeCard();
	SVList list;
	SV_listInit(&list);
	CHECK(SV_scan(SHOTS, &list) == 4);
	CHECK(list.count == 4);
	if (list.count == 4) {
		CHECK_STR(list.shots[0].game, "Dr. Mario");
		CHECK_STR(list.shots[1].game, "Tetris");
		CHECK_STR(list.shots[2].game, "Copied");
		CHECK(!list.shots[2].has_date);
		CHECK(list.shots[2].key == 20261108120000LL);
		CHECK_STR(list.shots[3].game, "Kirby's Dream Land");
	}
	CHECK(SV_any(SHOTS));

	// deleting: only the regular file, in the folder
	CHECK(SV_remove(SHOTS, &list, 1) == 0);
	CHECK(!exists(SHOTS "/Tetris.2026-11-08-14-03-05.png"));
	CHECK(list.count == 3);
	CHECK_STR(list.shots[1].game, "Copied");
	CHECK(SV_remove(SHOTS, &list, 3) == -1);
	CHECK(SV_remove(SHOTS, &list, -1) == -1);

	// a listed name that's become a link since is refused, and the link stays
	unlink(SHOTS "/Copied.png");
	CHECK(symlink(OUTSIDE "/Secret.png", SHOTS "/Copied.png") == 0);
	CHECK(SV_remove(SHOTS, &list, 1) == -1);
	CHECK(exists(SHOTS "/Copied.png"));
	CHECK(exists(OUTSIDE "/Secret.png"));
	CHECK(list.count == 3);

	// a name with a slash never reaches unlink
	snprintf(list.shots[1].file, sizeof(list.shots[1].file), "../Outside/Secret.png");
	CHECK(SV_remove(SHOTS, &list, 1) == -1);
	CHECK(exists(OUTSIDE "/Secret.png"));

	// a folder that's a link isn't listed or deleted from
	CHECK(symlink(SHOTS, CARD "/Linked") == 0);
	CHECK(SV_scan(CARD "/Linked", &list) == -1);
	CHECK(list.count == 0);
	CHECK(!SV_any(CARD "/Linked"));
	CHECK(SV_scan(CARD "/Missing", &list) == -1);
	CHECK(!SV_any(CARD "/Missing"));

	SV_scan(SHOTS, &list);
	CHECK(list.count == 2);
	SV_listFree(&list);
}

static void testMoves(void) {
	CHECK(SV_step(0, 5, 1, 1) == 1);
	CHECK(SV_step(4, 5, 1, 1) == 0);
	CHECK(SV_step(4, 5, 1, 0) == 4);
	CHECK(SV_step(0, 5, -1, 1) == 4);
	CHECK(SV_step(0, 5, -1, 0) == 0);
	CHECK(SV_step(0, 1, 1, 1) == 0);
	CHECK(SV_step(0, 0, 1, 1) == 0);

	// 4 columns, 10 shots: rows 0-3, 4-7, 8-9
	CHECK(SV_row(1, 10, 4, 1) == 5);
	CHECK(SV_row(5, 10, 4, 1) == 9);
	CHECK(SV_row(7, 10, 4, 1) == 9);
	CHECK(SV_row(9, 10, 4, 1) == 9);
	CHECK(SV_row(9, 10, 4, -1) == 5);
	CHECK(SV_row(2, 10, 4, -1) == 2);
	CHECK(SV_row(0, 0, 4, 1) == 0);

	CHECK(SV_afterRemove(1, 3) == 1);
	CHECK(SV_afterRemove(3, 3) == 2);
	CHECK(SV_afterRemove(0, 0) == 0);

	CHECK(SV_firstRow(0, 0, 20, 4, 2) == 0);
	CHECK(SV_firstRow(0, 8, 20, 4, 2) == 1);
	CHECK(SV_firstRow(1, 4, 20, 4, 2) == 1);
	CHECK(SV_firstRow(3, 2, 20, 4, 2) == 0);
	CHECK(SV_firstRow(4, 19, 20, 4, 2) == 3);
	CHECK(SV_firstRow(4, 2, 3, 4, 2) == 0);
	CHECK(SV_firstRow(0, 0, 0, 4, 2) == 0);
}

static void testHints(void) {
	MosaicHint left[2], right[2];
	int lc = -1, rc = -1;
	SV_hints(0, 0, 1, left, &lc, right, &rc);
	CHECK(lc == 0 && rc == 1 && right[0].button == MOSAIC_BUTTON_B);
	SV_hints(3, 0, 1, left, &lc, right, &rc);
	CHECK(lc == 1 && left[0].button == MOSAIC_BUTTON_B);
	CHECK_STR(left[0].label, "BACK");
	CHECK(rc == 2 && right[0].button == MOSAIC_BUTTON_Y && right[1].button == MOSAIC_BUTTON_A);
	CHECK_STR(right[0].label, "DELETE");
	CHECK_STR(right[1].label, "VIEW");
	SV_hints(3, 1, 1, left, &lc, right, &rc);
	CHECK(lc == 1 && rc == 1 && right[0].button == MOSAIC_BUTTON_Y);
	SV_hints(3, 0, 0, left, &lc, right, &rc);
	CHECK(rc == 1 && right[0].button == MOSAIC_BUTTON_A);
	SV_hints(3, 1, 0, left, &lc, right, &rc);
	CHECK(lc == 1 && rc == 0);
}

static const MosaicRect AREA = { 0, 0, 1024, 768 };
static const MosaicRect STATUS = { 794, 30, 200, 90 };
static const MosaicRect NONE = { 0, 0, 0, 0 };

static void testLayout(void) {
	MosaicRect parts[4];
	CHECK(SV_avoid(&AREA, &NONE, parts) == 1 && rectIs(parts[0], 0, 0, 1024, 768));
	int n = SV_avoid(&AREA, &STATUS, parts);
	CHECK(n == 4);
	for (int i = 0; i < n; i++) CHECK(!meet(&parts[i], &STATUS));
	CHECK(SV_avoid(&NONE, &STATUS, parts) == 0);

	MosaicRect header = { 700, 30, 80, 90 };
	CHECK(rectIs(SV_hole(&STATUS, &header), 700, 30, 294, 90));
	CHECK(rectIs(SV_hole(&STATUS, NULL), 794, 30, 200, 90));
	CHECK(rectIs(SV_hole(NULL, &NONE), 0, 0, 0, 0));

	MosaicRect box = { 0, 0, 200, 200 };
	CHECK(rectIs(SV_fit(1024, 768, &box), 0, 25, 200, 150));
	CHECK(rectIs(SV_fit(768, 1024, &box), 25, 0, 150, 200));
	CHECK(SV_fit(0, 10, &box).w == 0);
	MosaicRect dst = { 0, 0, 512, 384 };
	MosaicRect part = { 256, 0, 256, 192 };
	CHECK(rectIs(SV_source(&dst, &part, 1024, 768), 512, 0, 512, 384));

	CHECK(rectIs(SV_titlePill(&AREA, &STATUS, 3, 300), 30, 30, 372, 90));
	CHECK(rectIs(SV_titlePill(&AREA, &STATUS, 3, 2000), 30, 30, 734, 90));
	CHECK(SV_titlePill(&AREA, &STATUS, 3, 0).w == 0);

	// 1024x768 at scale 3: 4 columns of two rows below the status pill
	SVGrid grid;
	CHECK(SV_grid(&AREA, &STATUS, 3, 1024, 768, 30, &grid) == 0);
	CHECK(grid.cols == 4 && grid.rows == 2);
	CHECK(grid.cell_w == 218 && grid.thumb_h == 163 && grid.cell_h == 193);
	CHECK(grid.room.y >= STATUS.y + STATUS.h);
	CHECK(grid.room.y + grid.room.h <= 768 - 30 - 90 - 30);
	MosaicRect last = SV_cell(&grid, 7);
	CHECK(last.x + last.w <= 1024 - 30 && last.y + last.h <= grid.room.y + grid.room.h);
	CHECK(SV_cell(&grid, 8).w == 0);
	// 640x480 at scale 2
	MosaicRect small = { 0, 0, 640, 480 };
	MosaicRect small_status = { 480, 20, 140, 60 };
	CHECK(SV_grid(&small, &small_status, 2, 640, 480, 20, &grid) == 0);
	CHECK(grid.cols == 4 && grid.rows == 2);
	// short: one row, of the most columns
	MosaicRect wide = { 0, 0, 1024, 400 };
	CHECK(SV_grid(&wide, &STATUS, 3, 1024, 768, 30, &grid) == 0);
	CHECK(grid.rows == 1 && grid.cols == SV_MAX_COLS);
	CHECK(grid.thumb_h == 70 && grid.cell_h == 100);
	// too short for a whole row: one row of shorter thumbnails
	wide.h = 380;
	CHECK(SV_grid(&wide, &STATUS, 3, 1024, 768, 30, &grid) == 0);
	CHECK(grid.rows == 1 && grid.thumb_h == 50 && grid.cell_h == 80);
	wide.h = 300;
	CHECK(SV_grid(&wide, &STATUS, 3, 1024, 768, 30, &grid) == -1);
	MosaicRect tiny = { 0, 0, 100, 100 };
	CHECK(SV_grid(&tiny, &NONE, 3, 1024, 768, 30, &grid) == -1);
}

static void testCopies(void) {
	SVCopy copies[3];
	memset(copies, 0, sizeof(copies));
	MosaicSurface* fake = (MosaicSurface*)&copies; // only compared
	CHECK(SV_copyPick(copies, 3) == &copies[0]);
	copies[0] = (SVCopy){ "a.png", 10, 10, fake, 5 };
	copies[1] = (SVCopy){ "b.png", 10, 10, fake, 2 };
	CHECK(SV_copyPick(copies, 3) == &copies[2]);
	copies[2] = (SVCopy){ "c.png", 10, 10, fake, 9 };
	CHECK(SV_copyPick(copies, 3) == &copies[1]);
	CHECK(SV_copyFind(copies, 3, "c.png", 10, 10) == &copies[2]);
	CHECK(SV_copyFind(copies, 3, "c.png", 20, 10) == NULL);
	CHECK(SV_copyFind(copies, 3, "d.png", 10, 10) == NULL);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicSurface {
	int w, h;
	int owned;
};

static MosaicSurface screen_surface = { 1024, 768, 0 };
static MosaicSurface image_surface = { 1024, 768, 0 };

static MosaicHost fake_host;
static MosaicScreenHooks registered;
static char registered_id[64];
static char action_id[64], action_label[64], tile_id[64];
static int (*action_available)(const char*, void*);
static void (*action_run)(const char*, void*);
static int bound_button = -1;
static char last_log[512];
static int redraws = 0, live = 0, peak = 0, scales = 0;
static int blits_into_hole = 0, fills_into_hole = 0;
static int art_state = MOSAIC_ART_READY;
static int loads = 0;
static char opened_screen[64];
static char confirm_text[512];
static void (*confirm_yes)(void*);
static void* confirm_data;
static MosaicHint hints_right[2];
static int hints_right_count = 0;
static char toast_text[64];
static const char* tile_setting = "on";
static const char* button_setting = "none";
static const char* delete_setting = "on";
static MosaicRect hole_now;

static void fakeLog(const char* id, const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
}

static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (strcmp(key, "tile") == 0) return tile_setting;
	if (strcmp(key, "button") == 0) return button_setting;
	if (strcmp(key, "delete") == 0) return delete_setting;
	return fallback;
}

static void fakeRedraw(void) {
	redraws++;
}

static int fakeRegister(const char* id, const MosaicScreenHooks* hooks) {
	snprintf(registered_id, sizeof(registered_id), "%s", id);
	registered = *hooks;
	return 0;
}

static int fakeAction(const char* id, const char* label, int flags, int (*available)(const char*, void*), void (*run)(const char*, void*), void* data) {
	(void)flags;
	(void)data;
	snprintf(action_id, sizeof(action_id), "%s", id);
	snprintf(action_label, sizeof(action_label), "%s", label);
	action_available = available;
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
	return 0;
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
	(void)src;
	(void)fit;
	scales++;
	if (++live > peak) peak = live;
	MosaicSurface* s = calloc(1, sizeof(*s));
	s->w = w;
	s->h = h;
	s->owned = 1;
	return s;
}

static int fakeFree(const char* id, MosaicSurface* s) {
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
	loads++;
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
	fake_host.register_action = fakeAction;
	fake_host.bind_button = fakeBind;
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
	const MosaicPlugin* p = mosaic_plugin_get(&fake_host);
	CHECK(p->init(&fake_host) == 0);
	return p;
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

static int hold(int button) {
	MosaicInput in = { sizeof(MosaicInput), 0, MOSAIC_BUTTON_BIT(button), 0, MOSAIC_BUTTON_BIT(button) };
	return registered.input(&in, registered.data);
}

static void testInit(void) {
	start();
	CHECK_STR(registered_id, "screenshot-viewer.main");
	CHECK(registered.flags == 0);
	CHECK(registered.enter && registered.exit && registered.input && registered.render);
	CHECK_STR(action_id, "screenshot-viewer.open");
	CHECK_STR(action_label, "Screenshots");
	CHECK_STR(tile_id, "screenshot-viewer.open");
	CHECK(bound_button == -1);

	tile_setting = "off";
	button_setting = "L2";
	start();
	CHECK_STR(tile_id, "");
	CHECK(bound_button == MOSAIC_BUTTON_L2);
	tile_setting = "on";
	button_setting = "none";

	// an older launcher
	setupHost();
	fake_host.size = offsetof(MosaicHost, draw_hints);
	CHECK(mosaic_plugin_get(&fake_host)->init(&fake_host) != 0);
}

static void testAction(void) {
	makeCard();
	start();
	CHECK(action_available(NULL, NULL) == 1);
	action_run(NULL, NULL);
	CHECK_STR(opened_screen, "screenshot-viewer.main");
	CHECK(system("rm -rf '" SHOTS "'") == 0);
	CHECK(action_available(NULL, NULL) == 0);
	mkdir(SHOTS, 0755);
	CHECK(action_available(NULL, NULL) == 0);
}

static void testScreen(void) {
	makeCard();
	start();
	CHECK(registered.enter(registered.data) == 0);
	CHECK_STR(last_log, "4 screenshots in " SHOTS);

	// the grid, newest first
	CHECK(draw() == 0);
	CHECK_STR(last_log, "grid 0 of 4: Dr. Mario, 2026-11-09 08:30, 4 columns");
	CHECK(hints_right_count == 2 && hints_right[0].button == MOSAIC_BUTTON_Y && hints_right[1].button == MOSAIC_BUTTON_A);
	CHECK(scales == 4 && live == 4); // a thumbnail copy for each
	CHECK(fills_into_hole == 0 && blits_into_hole == 0);
	int loads_before = loads;
	draw();
	CHECK(loads == loads_before); // the copies are drawn, not asked for again
	CHECK(scales == 4);

	CHECK(press(MOSAIC_BUTTON_RIGHT) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK_STR(last_log, "grid 1 of 4: Tetris, 2026-11-08 14:03, 4 columns");
	press(MOSAIC_BUTTON_LEFT);
	press(MOSAIC_BUTTON_LEFT); // wraps on a press
	draw();
	CHECK_STR(last_log, "grid 3 of 4: Kirby's Dream Land, 2026-11-07 09:00, 4 columns");
	hold(MOSAIC_BUTTON_RIGHT); // a held repeat stays at the end
	draw();
	CHECK_STR(last_log, "grid 3 of 4: Kirby's Dream Land, 2026-11-07 09:00, 4 columns");
	press(MOSAIC_BUTTON_DOWN); // one row only
	draw();
	CHECK_STR(last_log, "grid 3 of 4: Kirby's Dream Land, 2026-11-07 09:00, 4 columns");

	// the full view, left and right stepping
	press(MOSAIC_BUTTON_LEFT);
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK_STR(last_log, "view 2 of 4: Copied, no date, image drawn");
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_Y);
	CHECK(fills_into_hole == 0 && blits_into_hole == 0);
	CHECK(live == 5);
	press(MOSAIC_BUTTON_RIGHT);
	draw();
	CHECK_STR(last_log, "view 3 of 4: Kirby's Dream Land, 2026-11-07 09:00, image drawn");
	press(MOSAIC_BUTTON_RIGHT);
	art_state = MOSAIC_ART_LOADING;
	draw();
	CHECK_STR(last_log, "view 0 of 4: Dr. Mario, 2026-11-09 08:30, image loading");
	art_state = MOSAIC_ART_MISSING;
	press(MOSAIC_BUTTON_RIGHT);
	draw();
	CHECK_STR(last_log, "view 1 of 4: Tetris, 2026-11-08 14:03, image none");
	art_state = MOSAIC_ART_READY;
	CHECK(live <= THUMB_COPIES + FULL_COPIES);

	// deleting asks first, then removes only that file
	CHECK(press(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_STAY);
	CHECK(strstr(confirm_text, "Delete this screenshot?") != NULL);
	CHECK(strstr(confirm_text, "Tetris") != NULL);
	CHECK(exists(SHOTS "/Tetris.2026-11-08-14-03-05.png"));
	confirm_yes(confirm_data);
	CHECK(!exists(SHOTS "/Tetris.2026-11-08-14-03-05.png"));
	CHECK_STR(last_log, "deleted Tetris.2026-11-08-14-03-05.png, 3 left");
	CHECK_STR(toast_text, "Screenshot deleted");
	draw();
	CHECK_STR(last_log, "view 1 of 3: Copied, no date, image drawn");

	// a file gone by the time the confirm comes back
	press(MOSAIC_BUTTON_Y);
	unlink(SHOTS "/Copied.png");
	confirm_yes(confirm_data);
	CHECK_STR(last_log, "can't delete Copied.png");
	CHECK(exists(OUTSIDE "/Secret.png"));

	// B goes back to the grid, then closes
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_STAY);
	draw();
	CHECK_STR(last_log, "grid 1 of 3: Copied, no date, 4 columns");
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
	CHECK(live == 0);

	// opening again lists the folder again
	CHECK(registered.enter(registered.data) == 0);
	CHECK_STR(last_log, "2 screenshots in " SHOTS);
	draw();
	CHECK_STR(last_log, "grid 0 of 2: Dr. Mario, 2026-11-09 08:30, 4 columns");
	registered.exit(registered.data);
	CHECK(live == 0);
}

static void testEmptyAndNoDelete(void) {
	makeCard();
	delete_setting = "off";
	start();
	registered.enter(registered.data);
	draw();
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_A);
	confirm_text[0] = '\0';
	press(MOSAIC_BUTTON_Y);
	CHECK_STR(confirm_text, "");
	registered.exit(registered.data);
	delete_setting = "on";

	CHECK(system("rm -rf '" SHOTS "'") == 0);
	start();
	registered.enter(registered.data);
	CHECK_STR(last_log, "0 screenshots in " SHOTS);
	draw();
	CHECK_STR(last_log, "no screenshots");
	CHECK(hints_right_count == 1 && hints_right[0].button == MOSAIC_BUTTON_B);
	CHECK(press(MOSAIC_BUTTON_A) == MOSAIC_SCREEN_STAY);
	CHECK(press(MOSAIC_BUTTON_Y) == MOSAIC_SCREEN_STAY);
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	registered.exit(registered.data);
	CHECK(live == 0);
}

// more shots than the thumbnail pool holds: copies stay bounded
static void testBounded(void) {
	makeCard();
	for (int i = 0; i < 60; i++) {
		char path[256];
		snprintf(path, sizeof(path), SHOTS "/Game %02d.2026-10-01-00-00-%02d.png", i, i);
		touch(path, 0);
	}
	start();
	registered.enter(registered.data);
	for (int i = 0; i < 70; i++) {
		draw();
		press(MOSAIC_BUTTON_RIGHT);
	}
	CHECK(peak <= THUMB_COPIES + FULL_COPIES);
	registered.exit(registered.data);
	CHECK(live == 0);
	fake_host.size = sizeof(MosaicHost);
	mosaic_plugin_get(&fake_host)->quit();
	CHECK(live == 0);
}

int main(void) {
	testNames();
	testSort();
	testScan();
	testMoves();
	testHints();
	testLayout();
	testCopies();
	testInit();
	testAction();
	testScreen();
	testEmptyAndNoDelete();
	testBounded();
	system("rm -rf '" CARD "'");
	CHECK_DONE();
}
