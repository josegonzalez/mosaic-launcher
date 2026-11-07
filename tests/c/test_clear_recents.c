// The clear recents plugin against a fake host, without the launcher.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/clear-recents/clear_recents.c"

struct MosaicEntry {
	char path[512];
	int type;
};

#define CARD "/card"
#define RECENTS CARD "/Recently Played"
#define GB CARD "/Roms/GB"

// the fake's recents, newest first
static char recents[8][512];
static int recent_count = 0;

static MosaicEntry chosen;
static MosaicEntry* selected = NULL;
static char current[512];
static const char* button_setting = NULL;
static const char* tile_setting = NULL;

#define MAX_ACTIONS 4
static int action_count = 0;
static char action_id[MAX_ACTIONS][64];
static char action_label[MAX_ACTIONS][64];
static int (*action_available[MAX_ACTIONS])(void*);
static void (*action_run[MAX_ACTIONS])(void*);
static int bound_count = 0;
static int bound_button[4];
static char bound_action[4][64];
static int tile_count = 0, tile_row = -1;
static char tile_action[64];

static char asked[160], yes_label[32];
static void (*pending_yes)(void*) = NULL;
static int reloads = 0, clears = 0;
static char toasted[160];

static void fakeLog(const char* id, const char* m) { (void)id; (void)m; }
static void fakeToast(const char* m) { snprintf(toasted, sizeof(toasted), "%s", m); }
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	(void)id;
	if (button_setting && !strcmp(key, "button")) return button_setting;
	if (tile_setting && !strcmp(key, "tile")) return tile_setting;
	return fallback;
}
static const char* fakeEntryPath(const MosaicEntry* e) { return e->path; }
static int fakeEntryType(const MosaicEntry* e) { return e->type; }
static const MosaicEntry* fakeSelected(void) { return selected; }
static const char* fakeCurrentPath(void) { return current; }
static void fakeRedraw(void) {}
static int fakeRegister(const char* id, const char* label, int (*available)(void*), void (*run)(void*), void* data) {
	(void)data;
	if (action_count >= MAX_ACTIONS) return -1;
	snprintf(action_id[action_count], sizeof(action_id[0]), "%s", id);
	snprintf(action_label[action_count], sizeof(action_label[0]), "%s", label);
	action_available[action_count] = available;
	action_run[action_count] = run;
	action_count++;
	return 0;
}
static int fakeBind(int screen, int button, int trigger, const char* id) {
	if (screen != MOSAIC_SCREEN_GAMELIST || trigger != MOSAIC_TRIGGER_PRESS || bound_count >= 4) return -1;
	bound_button[bound_count] = button;
	snprintf(bound_action[bound_count], sizeof(bound_action[0]), "%s", id);
	bound_count++;
	return 0;
}
static int fakeQuickTile(const char* id, int row, const char* icon, const char* icon_off, int (*is_on)(void*)) {
	(void)icon; (void)icon_off;
	if (is_on) return -1;
	snprintf(tile_action, sizeof(tile_action), "%s", id);
	tile_row = row;
	tile_count++;
	return 0;
}
static void fakeConfirm(const char* message, const char* yes, void (*on_yes)(void*), void* data) {
	(void)data;
	snprintf(asked, sizeof(asked), "%s", message);
	snprintf(yes_label, sizeof(yes_label), "%s", yes);
	pending_yes = on_yes;
}
static int fakeRecentsCount(void) { return recent_count; }
static const char* fakeRecentPath(int index) {
	return index >= 0 && index < recent_count ? recents[index] : NULL;
}
static int fakeRecentRemove(int index) {
	if (index < 0 || index >= recent_count) return -1;
	for (int i = index; i < recent_count - 1; i++) memcpy(recents[i], recents[i + 1], sizeof(recents[0]));
	recent_count--;
	return 0;
}
static void fakeRecentsClear(void) { recent_count = 0; clears++; }
static void fakeReload(void) { reloads++; }

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
	.selected_entry = fakeSelected,
	.current_path = fakeCurrentPath,
	.request_redraw = fakeRedraw,
	.register_action = fakeRegister,
	.bind_button = fakeBind,
	.toast = fakeToast,
	.confirm = fakeConfirm,
	.recents_count = fakeRecentsCount,
	.recent_path = fakeRecentPath,
	.recent_remove = fakeRecentRemove,
	.recents_clear = fakeRecentsClear,
	.reload_lists = fakeReload,
	.quick_tile = fakeQuickTile,
};

static void setRecents(int count, const char* const* paths) {
	recent_count = count;
	for (int i = 0; i < count; i++) snprintf(recents[i], sizeof(recents[0]), "%s", paths[i]);
}

static void selectEntry(const char* path) {
	snprintf(chosen.path, sizeof(chosen.path), "%s", path);
	chosen.type = MOSAIC_ENTRY_ROM;
	selected = &chosen;
}

static int find(const char* id) {
	for (int i = 0; i < action_count; i++) {
		if (!strcmp(action_id[i], id)) return i;
	}
	return -1;
}

static void reset(void) {
	action_count = bound_count = tile_count = 0;
	tile_row = -1;
	tile_action[0] = '\0';
}

static void answer(int yes) {
	void (*on_yes)(void*) = pending_yes;
	pending_yes = NULL;
	if (yes && on_yes) on_yes(NULL);
}

int main(void) {
	const MosaicPlugin* p = mosaic_plugin_get(&fake);
	CHECK(p->size == sizeof(MosaicPlugin));
	CHECK_STR(p->id, "clear-recents");
	CHECK(p->init(&fake) == 0);

	// a remove and a clear on Y, and a tile in the top row
	CHECK(action_count == 3);
	int remove = find("clear-recents.remove"), clear = find("clear-recents.clear"), tile = find("clear-recents.tile");
	CHECK(remove >= 0 && clear >= 0 && tile >= 0);
	if (remove < 0 || clear < 0 || tile < 0) CHECK_DONE();
	CHECK_STR(action_label[remove], "REMOVE");
	CHECK_STR(action_label[clear], "CLEAR ALL");
	CHECK_STR(action_label[tile], "Clear recents");
	CHECK(bound_count == 2 && bound_button[0] == MOSAIC_BUTTON_Y && bound_button[1] == MOSAIC_BUTTON_Y);
	CHECK_STR(bound_action[0], "clear-recents.remove");
	CHECK_STR(bound_action[1], "clear-recents.clear");
	CHECK(tile_count == 1 && tile_row == MOSAIC_QUICK_TOP);
	CHECK_STR(tile_action, "clear-recents.tile");

	// no recents: no tile, and nothing on the button anywhere
	snprintf(current, sizeof(current), "%s", RECENTS);
	selected = NULL;
	CHECK(!action_available[tile](NULL));
	CHECK(!action_available[clear](NULL));
	CHECK(!action_available[remove](NULL));
	action_run[tile](NULL);
	CHECK(pending_yes == NULL); // nothing to ask about

	const char* three[] = { GB "/Tetris.gb", GB "/Zelda.gb", CARD "/Tools/test/Clock.pak" };
	setRecents(3, three);

	// outside Recently Played the button is left alone, but the tile shows
	snprintf(current, sizeof(current), "%s", GB);
	selectEntry(GB "/Tetris.gb");
	CHECK(action_available[tile](NULL));
	CHECK(!action_available[clear](NULL));
	CHECK(!action_available[remove](NULL));
	snprintf(current, sizeof(current), "%s", CARD);
	CHECK(!action_available[clear](NULL));

	// in Recently Played both are on the button
	snprintf(current, sizeof(current), "%s", RECENTS);
	CHECK(action_available[clear](NULL));
	CHECK(action_available[remove](NULL));
	selectEntry(GB "/Other.gb"); // not a recent: only clearing
	CHECK(!action_available[remove](NULL));
	CHECK(action_available[clear](NULL));

	// removing the selected one keeps the rest in order
	selectEntry(GB "/Zelda.gb");
	reloads = 0;
	action_run[remove](NULL);
	CHECK(recent_count == 2);
	CHECK_STR(recents[0], GB "/Tetris.gb");
	CHECK_STR(recents[1], CARD "/Tools/test/Clock.pak");
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Removed from recents");
	CHECK(!action_available[remove](NULL));
	CHECK(pending_yes == NULL); // without asking

	// clearing asks first, and backing out keeps them
	toasted[0] = '\0';
	reloads = clears = 0;
	action_run[clear](NULL);
	CHECK(pending_yes != NULL);
	CHECK_STR(asked, "Clear all recently played games?");
	CHECK_STR(yes_label, "CLEAR");
	answer(0);
	CHECK(recent_count == 2 && clears == 0 && reloads == 0);
	CHECK_STR(toasted, "");

	// saying yes clears, lists again and says so
	action_run[clear](NULL);
	answer(1);
	CHECK(recent_count == 0 && clears == 1);
	CHECK(reloads == 1);
	CHECK_STR(toasted, "Recents cleared");
	CHECK(!action_available[clear](NULL));
	CHECK(!action_available[tile](NULL)); // the tile goes when the lists reload

	// the tile clears the same way, from anywhere
	setRecents(3, three);
	snprintf(current, sizeof(current), "%s", CARD);
	reloads = clears = 0;
	action_run[tile](NULL);
	CHECK_STR(asked, "Clear all recently played games?");
	answer(1);
	CHECK(recent_count == 0 && clears == 1 && reloads == 1);

	// the settings: another button, and no tile
	reset();
	button_setting = "L2";
	tile_setting = "off";
	CHECK(p->init(&fake) == 0);
	CHECK(action_count == 2 && find("clear-recents.tile") < 0);
	CHECK(tile_count == 0);
	CHECK(bound_count == 2 && bound_button[0] == MOSAIC_BUTTON_L2 && bound_button[1] == MOSAIC_BUTTON_L2);
	reset();
	button_setting = "nonsense";
	tile_setting = NULL;
	p->init(&fake);
	CHECK(bound_count == 2 && bound_button[0] == MOSAIC_BUTTON_Y);
	CHECK(tile_count == 1);

	// an older launcher without the recents calls: loads, does nothing
	reset();
	fake.size = offsetof(MosaicHost, recents_count);
	CHECK(p->init(&fake) == 0);
	CHECK(action_count == 0 && bound_count == 0 && tile_count == 0);
	fake.size = sizeof(MosaicHost);

	CHECK_DONE();
}
