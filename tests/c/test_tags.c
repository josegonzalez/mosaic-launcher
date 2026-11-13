// The tags plugin: its model on its own, then the plugin against a fake
// host, on a card of its own under TEST_SDCARD_PATH.

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "fakesets.h"
#include "../../plugins/tags/tags.c"

#ifndef TEST_SDCARD_PATH
#define TEST_SDCARD_PATH "/tmp/tags-card"
#endif
#define CARD TEST_SDCARD_PATH
#define STORE CARD "/.userdata/desktop/launcher/tags.txt"

static void writeFile(const char* path, const char* text) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char* p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fputs(text, f);
	fclose(f);
}

static char* readText(const char* path) {
	static char text[8192];
	text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return text;
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	text[n] = '\0';
	fclose(f);
	return text;
}

static void testNames(void) {
	char out[TG_NAME_LEN];
	CHECK(TG_cleanName("  RPG  ", out, sizeof(out)));
	CHECK_STR(out, "RPG");
	CHECK(TG_cleanName("Co\top\nplay", out, sizeof(out)));
	CHECK_STR(out, "Co op play");
	CHECK(!TG_cleanName("", out, sizeof(out)));
	CHECK(!TG_cleanName("   ", out, sizeof(out)));
	CHECK(!TG_cleanName(NULL, out, sizeof(out)));
	CHECK(!TG_cleanName("a/b", out, sizeof(out)));
	CHECK(!TG_cleanName(".hidden", out, sizeof(out)));
	CHECK(!TG_cleanName("#note", out, sizeof(out)));
	CHECK(TG_cleanName("Pok\xc3\xa9mon", out, sizeof(out)));
	CHECK_STR(out, "Pok\xc3\xa9mon");

	// cut to TG_NAME_LEN - 1 bytes, never inside a character
	char long_name[200];
	memset(long_name, 'a', 62);
	strcpy(long_name + 62, "\xc3\xa9zzz");
	CHECK(TG_cleanName(long_name, out, sizeof(out)));
	CHECK(strlen(out) == 62);
	memset(long_name, 'a', 61);
	strcpy(long_name + 61, "\xc3\xa9zzz");
	CHECK(TG_cleanName(long_name, out, sizeof(out)));
	CHECK(strlen(out) == 63 && !strcmp(out + 61, "\xc3\xa9"));
	char small[4];
	CHECK(TG_cleanName("abcdef", small, sizeof(small)));
	CHECK_STR(small, "abc");

	CHECK(TG_validPath("/Roms/GB/Tetris.gb"));
	CHECK(!TG_validPath("Roms/GB/Tetris.gb"));
	CHECK(!TG_validPath("/"));
	CHECK(!TG_validPath("/Roms/../x.gb"));
	CHECK(!TG_validPath("/Roms/./x.gb"));
	CHECK(!TG_validPath("/Roms/a\tb.gb"));
	CHECK(TG_validPath("/Roms/..x/.y.gb"));
	CHECK(!TG_validPath(NULL));
}

static void testStore(void) {
	TGStore s;
	TG_init(&s);
	int added = 0;
	CHECK(TG_add(&s, "rpg", &added) == 0 && added);
	CHECK(TG_add(&s, "Arcade", &added) == 0 && added); // sorted, ignoring case
	CHECK(TG_add(&s, "RPG", &added) == 1 && !added);  // the same tag
	CHECK(TG_add(&s, "Zelda-likes", NULL) == 2);
	CHECK(TG_add(&s, "a/b", &added) == -1 && !added);
	CHECK(s.count == 3);
	CHECK_STR(s.tags[1].name, "rpg");
	CHECK(TG_find(&s, "ARCADE") == 0);
	CHECK(TG_find(&s, "nope") == -1);

	CHECK(TG_set(&s, 1, "/Roms/GB/Zelda.gb", 1) == 1);
	CHECK(TG_set(&s, 1, "/Roms/GB/Tetris.gb", 1) == 1);
	CHECK(TG_set(&s, 1, "/Roms/GB/Zelda.gb", 1) == 0);
	CHECK(TG_has(&s, 1, "/Roms/GB/Zelda.gb"));
	CHECK(!TG_has(&s, 0, "/Roms/GB/Zelda.gb"));
	CHECK(!TG_has(&s, 9, "/Roms/GB/Zelda.gb"));
	CHECK(TG_set(&s, 1, "/Roms/GB/Gone.gb", 0) == 0);
	CHECK(TG_set(&s, 1, "/Roms/GB/Zelda.gb", 0) == 1);
	CHECK(!TG_has(&s, 1, "/Roms/GB/Zelda.gb"));
	CHECK(TG_set(&s, 1, "Roms/x.gb", 1) == -1);
	CHECK(TG_set(&s, 7, "/Roms/x.gb", 1) == -1);
	CHECK(TG_set(&s, 0, "/Roms/GB/Zelda.gb", 1) == 1);

	char* text = TG_format(&s);
	CHECK_STR(text, "Arcade\nArcade\t/Roms/GB/Zelda.gb\nrpg\nrpg\t/Roms/GB/Tetris.gb\nZelda-likes\n");
	free(text);

	CHECK(TG_remove(&s, 0) == 0);
	CHECK(TG_remove(&s, 5) == -1);
	CHECK(s.count == 2 && !strcmp(s.tags[0].name, "rpg"));
	TG_free(&s);
	CHECK(s.count == 0);

	// adding past the cap
	for (int i = 0; i < TG_TAGS_MAX; i++) {
		char name[16];
		snprintf(name, sizeof(name), "t%03i", i);
		CHECK(TG_add(&s, name, NULL) >= 0);
	}
	CHECK(TG_add(&s, "one more", NULL) == -1);
	CHECK(TG_add(&s, "t001", NULL) == 1); // an existing one still answers
	TG_free(&s);
}

static void testParse(void) {
	TGStore s;
	TG_init(&s);
	const char* text =
		"# my tags\r\n"
		"Puzzle\r\n"
		"\n"
		"rpg\t/Roms/GB/Zelda.gb\n"  // a game line declares its tag
		"RPG\t/Roms/GBA/Golden Sun.gba\n" // the same tag, ignoring case
		"Puzzle\t/Roms/GB/Tetris.gb\n"
		"Puzzle\t/Roms/GB/Tetris.gb\n" // twice is once
		"Bad\tRoms/no-slash.gb\n"
		"a/b\t/Roms/GB/x.gb\n"
		"\t/Roms/GB/x.gb\n"
		".dot\n"
		"Empty";
	CHECK(TG_parse(&s, text) == 0);
	CHECK(s.count == 3);
	CHECK_STR(s.tags[0].name, "Empty");
	CHECK_STR(s.tags[1].name, "Puzzle");
	CHECK_STR(s.tags[2].name, "rpg");
	CHECK(s.tags[1].count == 1 && s.tags[2].count == 2);
	CHECK_STR(s.tags[2].games[1], "/Roms/GBA/Golden Sun.gba");

	// a too-long name is skipped, not cut into another
	char line[200];
	memset(line, 'x', 80);
	strcpy(line + 80, "\t/Roms/GB/a.gb\n");
	CHECK(TG_parse(&s, line) == 0);
	CHECK(s.count == 0);
	CHECK(TG_parse(&s, NULL) == 0 && s.count == 0);

	// a round trip through the file
	CHECK(system("mkdir -p '" CARD "'") == 0);
	CHECK(TG_parse(&s, text) == 0);
	const char* file = CARD "/round.txt";
	CHECK(TG_save(&s, file) == 0);
	CHECK(access(CARD "/round.txt.tmp", F_OK) != 0);
	TGStore back;
	TG_init(&back);
	CHECK(TG_load(&back, file) == 0);
	char* a = TG_format(&s);
	char* b = TG_format(&back);
	CHECK_STR(a, b);
	CHECK_STR(readText(file), a);
	free(a);
	free(b);
	CHECK(TG_load(&back, CARD "/missing.txt") == 0 && back.count == 0);
	CHECK(TG_save(&s, CARD "/no/such/dir/tags.txt") == -1);
	TG_free(&s);
	TG_free(&back);
}

static void testPaths(void) {
	writeFile(CARD "/Roms/GB/Tetris.gb", "");
	writeFile(CARD "/Roms/PS (PS)/FF7/FF7.m3u", "");
	writeFile(CARD "/Roms/PS (PS)/FF7/Disc 1.cue", "");
	writeFile(CARD "/Roms/PS (PS)/Crash/Crash.cue", "");
	writeFile(CARD "/Tools/desktop/Clock.pak/launch.sh", "");
	char out[TG_PATH_LEN];
	CHECK(TG_gamePath(CARD, CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM, out, sizeof(out)));
	CHECK_STR(out, "/Roms/GB/Tetris.gb");
	CHECK(TG_gamePath(CARD, CARD "/Roms/PS (PS)/FF7", MOSAIC_ENTRY_DIR, out, sizeof(out)));
	CHECK_STR(out, "/Roms/PS (PS)/FF7/FF7.m3u");
	CHECK(TG_gamePath(CARD, CARD "/Roms/PS (PS)/FF7/Disc 1.cue", MOSAIC_ENTRY_ROM, out, sizeof(out)));
	CHECK_STR(out, "/Roms/PS (PS)/FF7/FF7.m3u");
	CHECK(TG_gamePath(CARD, CARD "/Roms/PS (PS)/FF7/FF7.m3u", MOSAIC_ENTRY_ROM, out, sizeof(out)));
	CHECK_STR(out, "/Roms/PS (PS)/FF7/FF7.m3u");
	CHECK(TG_gamePath(CARD, CARD "/Roms/PS (PS)/Crash", MOSAIC_ENTRY_DIR, out, sizeof(out)));
	CHECK_STR(out, "/Roms/PS (PS)/Crash/Crash.cue");
	CHECK(TG_gamePath(CARD, CARD "/Tools/desktop/Clock.pak", MOSAIC_ENTRY_PAK, out, sizeof(out)));
	CHECK_STR(out, "/Tools/desktop/Clock.pak");
	CHECK(!TG_gamePath(CARD, CARD "/Roms/GB", MOSAIC_ENTRY_DIR, out, sizeof(out))); // a plain folder
	CHECK_STR(out, "");
	CHECK(!TG_gamePath(CARD, "/elsewhere/x.gb", MOSAIC_ENTRY_ROM, out, sizeof(out)));
	CHECK(!TG_gamePath(CARD, CARD "X/x.gb", MOSAIC_ENTRY_ROM, out, sizeof(out)));
	CHECK(!TG_gamePath(CARD, CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_TOGGLE, out, sizeof(out)));
	CHECK(!TG_gamePath(CARD, CARD "/Roms/../x.gb", MOSAIC_ENTRY_ROM, out, sizeof(out)));
	char tiny[8];
	CHECK(!TG_gamePath(CARD, CARD "/Roms/GB/Tetris.gb", MOSAIC_ENTRY_ROM, tiny, sizeof(tiny)));

	TGStore s;
	TG_init(&s);
	TG_add(&s, "RPG", NULL);
	TG_add(&s, "Arcade", NULL);
	CHECK(TG_folderTag(&s, CARD "/Tags", CARD "/Tags/RPG") == 1);
	CHECK(TG_folderTag(&s, CARD "/Tags", CARD "/Tags/rpg") == -1); // folders name it exactly
	CHECK(TG_folderTag(&s, CARD "/Tags", CARD "/Tags") == -1);
	CHECK(TG_folderTag(&s, CARD "/Tags", CARD "/Tags/RPG/x") == -1);
	CHECK(TG_folderTag(&s, CARD "/Tags", CARD "/TagsX/RPG") == -1);
	TG_free(&s);
}

static void testChecklist(void) {
	CHECK(TG_move(0, 0, 1, 1) == -1);
	CHECK(TG_move(0, 3, 1, 1) == 1);
	CHECK(TG_move(2, 3, 1, 1) == 0);
	CHECK(TG_move(2, 3, 1, 0) == 2);
	CHECK(TG_move(0, 3, -1, 1) == 2);
	CHECK(TG_move(0, 3, -1, 0) == 0);
	CHECK(TG_move(1, 9, 5, 0) == 6);
	CHECK(TG_move(6, 9, 5, 1) == 0);
	CHECK(TG_move(7, 3, 0, 0) == 2); // a stale selection comes back
	CHECK(TG_window(0, 10, 4, 0) == 0);
	CHECK(TG_window(4, 10, 4, 0) == 1);
	CHECK(TG_window(9, 10, 4, 0) == 6);
	CHECK(TG_window(2, 10, 4, 6) == 2);
	CHECK(TG_window(1, 3, 4, 2) == 0);
	CHECK(TG_window(3, 5, 4, 9) == 1);
	CHECK(TG_window(0, 5, 0, 3) == 0);
}

///////////////////////////////////////
// the plugin against a fake host

struct MosaicEntry {
	char path[512];
	char name[256];
	int type;
};
struct MosaicList {
	MosaicEntry* items[64];
	int count;
};

static char logged[8192];
static void fakeLog(const char* id, const char* m) {
	snprintf(logged + strlen(logged), sizeof(logged) - strlen(logged), "%s\n", m);
}
static const char* cfg_root = "on";
static const char* fakeCfgGet(const char* key, const char* fallback) {
	if (!strcmp(key, "root")) return cfg_root;
	return fallback;
}
static MosaicEntry* fakeEntryNew(const char* path, int type, const char* name) {
	MosaicEntry* e = calloc(1, sizeof(MosaicEntry));
	snprintf(e->path, sizeof(e->path), "%s", path);
	const char* slash = strrchr(path, '/');
	snprintf(e->name, sizeof(e->name), "%s", name ? name : slash ? slash + 1 : path);
	e->type = type;
	return e;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static const char* fakeEntryName(const MosaicEntry* e) { return e->name; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static MosaicList* fakeListNew(void) { return calloc(1, sizeof(MosaicList)); }
static int fakeListCount(const MosaicList* l) { return l->count; }
static MosaicEntry* fakeListGet(const MosaicList* l, int i) { return i >= 0 && i < l->count ? l->items[i] : NULL; }
static void fakeListPush(MosaicList* l, MosaicEntry* e) {
	if (l->count < 64) l->items[l->count++] = e;
	else free(e);
}
static void listFree(MosaicList* l) {
	if (!l) return;
	for (int i = 0; i < l->count; i++) free(l->items[i]);
	free(l);
}

static MosaicEntry* picked = NULL;
static const MosaicEntry* fakeSelected(void) { return picked; }
static void fakeRedraw(void) {}
static char toasted[128];
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static int reloads = 0;
static void fakeReload(void) { reloads++; }

#define ACTIONS 16
static struct {
	char id[32];
	char label[64];
	int (*available)(void*);
	void (*run)(void*);
	void* data;
} actions[ACTIONS];
static int action_count = 0;
static int fakeRegisterAction(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) return -1;
	}
	if (action_count >= ACTIONS) return -1;
	snprintf(actions[action_count].id, sizeof(actions[0].id), "%s", id);
	snprintf(actions[action_count].label, sizeof(actions[0].label), "%s", label);
	actions[action_count].available = available;
	actions[action_count].run = run;
	actions[action_count].data = data;
	action_count++;
	return 0;
}
static int findAction(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(actions[i].id, id)) return i;
	}
	return -1;
}
static int bound_button = -1;
static int fakeBind(int screen, int button, int trigger, const char* id) {
	(void)screen; (void)trigger; (void)id;
	bound_button = button;
	return 0;
}
static char tile_ids[16][32];
static int tile_count = 0;
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)row; (void)icon; (void)icon_off; (void)is_on;
	if (findAction(id) < 0 || tile_count >= 16) return -1;
	snprintf(tile_ids[tile_count++], sizeof(tile_ids[0]), "%s", id);
	return 0;
}
static char opened[512];
static int fakeOpenPath(const char* path, int type) {
	(void)type;
	snprintf(opened, sizeof(opened), "%s", path);
	return 1;
}
static MosaicScreenHooks registered;
static int fakeRegisterScreen(const char* id, const MosaicScreenHooks* hooks) {
	CHECK_STR(id, SCREEN_ID);
	registered = *hooks;
	return 0;
}
static int screen_open = 0;
static int fakeOpenScreen(const char* id) {
	if (strcmp(id, SCREEN_ID) || screen_open) return -1;
	if (registered.enter(registered.data) != 0) return -1;
	screen_open = 1;
	return 0;
}
static void (*keyboard_done)(const char*, void*);
static char keyboard_title[64];
static void fakeKeyboard(const char* t, const char* initial, void (*done)(const char*, void*), void* data) {
	(void)initial; (void)data;
	snprintf(keyboard_title, sizeof(keyboard_title), "%s", t);
	keyboard_done = done;
}
static void (*confirm_yes)(void*);
static char confirm_message[128];
static void fakeConfirm(const char* m, const char* yes, void (*on_yes)(void*), void* data) {
	(void)yes; (void)data;
	snprintf(confirm_message, sizeof(confirm_message), "%s", m);
	confirm_yes = on_yes;
}
static int fakeDrawHints(MosaicSurface* s, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	(void)s; (void)group; (void)hints; (void)count; (void)drawn;
	return -1;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.sdcard_path = CARD,
	.userdata_path = CARD "/.userdata/desktop",
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.entry_new = fakeEntryNew,
	.entry_path = fakeEntryPath,
	.entry_name = fakeEntryName,
	.entry_type = fakeEntryType,
	.list_new = fakeListNew,
	.list_count = fakeListCount,
	.list_get = fakeListGet,
	.list_push = fakeListPush,
	.selected_entry = fakeSelected,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegisterAction,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.confirm = fakeConfirm,
	.keyboard = fakeKeyboard,
	.open_path = fakeOpenPath,
	.reload_lists = fakeReload,
	.quick_tile = fakeQuickTile,
	.register_screen = fakeRegisterScreen,
	.open_screen = fakeOpenScreen,
	.draw_hints = fakeDrawHints,
	.set_register = fakeSetRegister,
	.set_unregister = fakeSetUnregister,
	.is_filtered = fakeIsFiltered,
};

static int press(int button) {
	MosaicInput input = { sizeof(MosaicInput), MOSAIC_BUTTON_BIT(button), MOSAIC_BUTTON_BIT(button), 0, MOSAIC_BUTTON_BIT(button) };
	int result = registered.input(&input, registered.data);
	if (result == MOSAIC_SCREEN_CLOSE) {
		registered.exit(registered.data);
		screen_open = 0;
	}
	return result;
}

static void start(void) {
	logged[0] = '\0';
	action_count = 0;
	tile_count = 0;
	reloads = 0;
	screen_open = 0;
	CHECK(init(&fake) == 0);
}

static void pick(const char* path, int type, const char* name) {
	free(picked);
	picked = fakeEntryNew(path, type, name);
}

static void testPlugin(void) {
	writeFile(CARD "/Roms/GB/Zelda.gb", "");
	writeFile(STORE, "Puzzle\nPuzzle\t/Roms/GB/Tetris.gb\nPuzzle\t/Roms/GB/Gone.gb\nUnused\nOld\t/Roms/GB/Gone.gb\n");

	start();
	CHECK(strstr(logged, "3 tags, 1 with games, root on") != NULL);
	CHECK(strstr(logged, "1 quick menu tiles") != NULL);
	// a set per tag, by full path, with the games it records
	CHECK(fakeSetFind("tags:Puzzle") >= 0 && fakeSetFind("tags:Unused") >= 0 && fakeSetFind("tags:Old") >= 0);
	CHECK(fakeSetHas("tags:Puzzle", CARD "/Roms/GB/Tetris.gb") == 1);
	CHECK(fakeSetHas("tags:Puzzle", CARD "/Roms/GB/Zelda.gb") == 0);
	CHECK(fakeSetHas("tags:Puzzle", "/Roms/GB/Tetris.gb") == 0);
	CHECK_STR(fakeSetList("tags:Puzzle"), CARD "/Roms/GB/Tetris.gb\n" CARD "/Roms/GB/Gone.gb\n");
	CHECK_STR(fakeSetList("tags:Unused"), "");
	CHECK_STR(fakeSetList("tags:puzzle"), "none");
	CHECK(bound_button == MOSAIC_BUTTON_Y);
	CHECK(tile_count == 1 && !strcmp(tile_ids[0], "tags.tile.0"));
	int tile = findAction("tags.tile.0");
	CHECK(tile >= 0 && !strcmp(actions[tile].label, "Puzzle"));
	CHECK(actions[tile].available(actions[tile].data));
	actions[tile].run(actions[tile].data);
	CHECK_STR(opened, CARD "/Tags/Puzzle");

	// the root's folder lists the tags with games; each lists its games on the card
	MosaicList* root = fakeListNew();
	rootEntries(root);
	CHECK(root->count == 1 && !strcmp(root->items[0]->path, CARD "/Tags") && root->items[0]->type == MOSAIC_ENTRY_DIR);
	CHECK(strstr(logged, "root: 1 tags with games") != NULL);
	listFree(root);
	MosaicList* list = virtualDir(CARD "/Tags");
	CHECK(list && list->count == 1 && !strcmp(list->items[0]->path, CARD "/Tags/Puzzle")
		&& !strcmp(list->items[0]->name, "Puzzle"));
	listFree(list);
	list = virtualDir(CARD "/Tags/Puzzle");
	CHECK(list && list->count == 1 && !strcmp(list->items[0]->path, CARD "/Roms/GB/Tetris.gb")
		&& list->items[0]->type == MOSAIC_ENTRY_ROM);
	listFree(list);
	list = virtualDir(CARD "/Tags/Nope");
	CHECK(list && list->count == 0);
	listFree(list);
	CHECK(virtualDir(CARD "/Roms/GB") == NULL);

	// the action, on a game, a plain folder and nothing
	int edit_at = findAction("tags.edit");
	CHECK(edit_at >= 0);
	pick(CARD "/Roms/GB", MOSAIC_ENTRY_DIR, "GB");
	CHECK(!actions[edit_at].available(NULL));
	free(picked);
	picked = NULL;
	CHECK(!actions[edit_at].available(NULL));
	pick(CARD "/Roms/GB/Zelda.gb", MOSAIC_ENTRY_ROM, "Zelda");
	CHECK(actions[edit_at].available(NULL));
	actions[edit_at].run(NULL);
	CHECK(screen_open);
	CHECK(strstr(logged, "checklist for /Roms/GB/Zelda.gb: 3 tags") != NULL);
	CHECK_STR(title, "Zelda");
	CHECK(!actions[edit_at].available(NULL)); // not again while it shows

	// rows: Old, Puzzle, Unused, New tag. A on Puzzle tags Zelda, and the file has it
	CHECK(on_card[0] == 0 && on_card[1] == 1 && on_card[2] == 0);
	press(MOSAIC_BUTTON_DOWN);
	CHECK(selected == 1);
	press(MOSAIC_BUTTON_A);
	CHECK(strstr(logged, "tagged /Roms/GB/Zelda.gb with Puzzle") != NULL);
	CHECK(on_card[1] == 2);
	CHECK(strstr(readText(STORE), "Puzzle\t/Roms/GB/Zelda.gb\n") != NULL);
	press(MOSAIC_BUTTON_A);
	CHECK(strstr(logged, "untagged /Roms/GB/Zelda.gb from Puzzle") != NULL);
	CHECK(strstr(readText(STORE), "Zelda") == NULL);
	press(MOSAIC_BUTTON_A);

	// X on a tag with a game on the card says so; on an unused one it asks, then deletes
	confirm_yes = NULL;
	press(MOSAIC_BUTTON_X);
	CHECK(confirm_yes == NULL);
	CHECK_STR(toasted, "Untag its games to delete it");
	press(MOSAIC_BUTTON_UP); // Old: its game is gone from the card, so it's unused
	press(MOSAIC_BUTTON_X);
	CHECK_STR(confirm_message, "Delete the tag Old?");
	CHECK(confirm_yes != NULL);
	if (confirm_yes) confirm_yes(NULL);
	CHECK(strstr(logged, "deleted tag Old") != NULL);
	CHECK(strstr(readText(STORE), "Old") == NULL);
	CHECK(store.count == 2);

	// New tag: up from the top wraps to it; the keyboard's name goes on the game
	press(MOSAIC_BUTTON_UP);
	CHECK(selected == 2);
	press(MOSAIC_BUTTON_A);
	CHECK_STR(keyboard_title, "New tag");
	CHECK(keyboard_done != NULL);
	if (keyboard_done) keyboard_done(NULL, NULL); // cancelled
	CHECK(store.count == 2);
	if (keyboard_done) keyboard_done("a/b", NULL);
	CHECK_STR(toasted, "Not a tag name");
	if (keyboard_done) keyboard_done("  Action  ", NULL);
	CHECK(strstr(logged, "added tag Action") != NULL);
	CHECK(strstr(logged, "tagged /Roms/GB/Zelda.gb with Action") != NULL);
	CHECK(store.count == 3 && selected == 0 && on_card[0] == 1);
	// typing an existing tag, in another case, just tags the game with it
	if (keyboard_done) keyboard_done("unused", NULL);
	CHECK(store.count == 3 && TG_has(&store, 2, "/Roms/GB/Zelda.gb"));
	CHECK_STR(readText(STORE),
		"Action\nAction\t/Roms/GB/Zelda.gb\n"
		"Puzzle\nPuzzle\t/Roms/GB/Tetris.gb\nPuzzle\t/Roms/GB/Gone.gb\nPuzzle\t/Roms/GB/Zelda.gb\n"
		"Unused\nUnused\t/Roms/GB/Zelda.gb\n");

	// B closes, and the lists reload once for the changes
	CHECK(press(MOSAIC_BUTTON_B) == MOSAIC_SCREEN_CLOSE);
	CHECK(reloads == 1);
	CHECK(actions[edit_at].available(NULL));

	// opening and closing without a change doesn't reload
	actions[edit_at].run(NULL);
	press(MOSAIC_BUTTON_B);
	CHECK(reloads == 1);

	// a disc of a multi-disc game is tagged as the game's .m3u
	pick(CARD "/Roms/PS (PS)/FF7/Disc 1.cue", MOSAIC_ENTRY_ROM, "Disc 1");
	actions[edit_at].run(NULL);
	CHECK(strstr(logged, "checklist for /Roms/PS (PS)/FF7/FF7.m3u") != NULL);
	press(MOSAIC_BUTTON_A);
	press(MOSAIC_BUTTON_B);
	list = virtualDir(CARD "/Tags/Action");
	CHECK(list && list->count == 2 && !strcmp(list->items[1]->path, CARD "/Roms/PS (PS)/FF7/FF7.m3u"));
	listFree(list);

	// the file changed by hand is read again
	writeFile(STORE, "Hand\nHand\t/Roms/GB/Zelda.gb\nHand\t/Tools/desktop/Clock.pak\n");
	list = virtualDir(CARD "/Tags/Hand");
	CHECK(list && list->count == 2 && list->items[1]->type == MOSAIC_ENTRY_PAK);
	listFree(list);
	CHECK(!actions[tile].available(actions[tile].data)); // Puzzle is gone

	// no tag with a game on the card: no root folder
	writeFile(STORE, "Hand\nHand\t/Roms/GB/Gone.gb\n");
	root = fakeListNew();
	rootEntries(root);
	CHECK(root->count == 0);
	CHECK(strstr(logged, "root: 0 tags with games") != NULL);
	listFree(root);

	// the root setting off leaves the root alone, but the folders still list
	writeFile(STORE, "Hand\nHand\t/Roms/GB/Zelda.gb\n");
	cfg_root = "off";
	start();
	CHECK(strstr(logged, "root off") != NULL);
	root = fakeListNew();
	rootEntries(root);
	CHECK(root->count == 0);
	listFree(root);
	list = virtualDir(CARD "/Tags/Hand");
	CHECK(list && list->count == 1);
	listFree(list);
	cfg_root = "on";
	CHECK(fakeSetHas("tags:Hand", CARD "/Roms/GB/Zelda.gb") == 1);
	// a tag edited out of the file loses its set, and a new one gets one
	writeFile(STORE, "Fresh\nFresh\t/Roms/GB/Tetris.gb\n");
	CHECK(fakeSetHas("tags:Hand", CARD "/Roms/GB/Zelda.gb") == 0);
	CHECK(fakeSetFind("tags:Hand") < 0);
	CHECK(fakeSetHas("tags:Fresh", CARD "/Roms/GB/Tetris.gb") == 1);

	// an older launcher, without draw_hints, is declined
	MosaicHost old = fake;
	old.size = offsetof(MosaicHost, draw_hints);
	logged[0] = '\0';
	CHECK(init(&old) != 0);
	quit();
	free(picked);
	picked = NULL;
}

int main(void) {
	CHECK(system("rm -rf '" TEST_SDCARD_PATH "'") == 0);
	testNames();
	testStore();
	testParse();
	testPaths();
	testChecklist();
	testPlugin();
	CHECK_DONE();
}
