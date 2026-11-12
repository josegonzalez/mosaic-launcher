// Tags: your own tags on games, each a folder on the root and a tile on the
// quick menu.
//
// Y on the game list (the "button" setting picks another), alongside other
// plugins' actions there, opens a checklist of every tag for the selected
// game: A ticks or clears a tag, A on New tag names one on the keyboard and
// puts it on the game, X deletes a tag no game on the card has, and B goes
// back. The action is tags.edit, so an FN button or button-remap can open it
// too.
//
// The root gets one Tags folder, shown while a tag has a game on the card,
// with a folder per tag in it, rather than a root row per tag, so the root
// keeps its consoles together however many tags there are. Tags with games
// also get a tile in the quick menu's top row that opens their folder (the
// first TILES_MAX by name, as the launcher starts).
//
// The tags are in .userdata/<platform>/launcher/tags.txt, as tags_model.h
// describes, with card-relative paths, and written atomically.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "tags_model.h"

#define ID "tags"
#define SCREEN_ID "tags.checklist"
#define FOLDER_NAME "Tags"
#define TILES_MAX 8

static const MosaicHost* host;
static char folder_path[TG_PATH_LEN]; // the virtual folder, <sd>/Tags
static char state_dir[TG_PATH_LEN];   // <userdata>/launcher
static char store_path[TG_PATH_LEN + 16];
static TGStore store;
static struct stat seen; // the file as last read or written
static int root_on = 1;
static int root_logged = -1; // the tag count the root last logged

// the checklist
static char game[TG_PATH_LEN];  // card-relative, the game it's for
static char title[TG_PATH_LEN]; // the game's name
static int on_card[TG_TAGS_MAX]; // each tag's games on the card
static int selected = 0, first = 0;
static int showing = 0, changed = 0;
static char deleting[TG_NAME_LEN]; // waiting on the confirm

static char tiles[TILES_MAX][TG_NAME_LEN];

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

///////////////////////////////////////
// the store

static void remember(void) {
	if (stat(store_path, &seen) != 0) memset(&seen, 0, sizeof(seen));
}

// Reads the file again if it changed since we last saw it.
static void refresh(void) {
	struct stat st;
	if (stat(store_path, &st) != 0) memset(&st, 0, sizeof(st));
	if (st.st_mtime == seen.st_mtime && st.st_size == seen.st_size && st.st_ino == seen.st_ino) return;
	if (TG_load(&store, store_path) != 0) say("couldn't read the tags file");
	seen = st;
}

// Saves, or reads the file back and says so when it can't.
static int save(void) {
	mkdir(state_dir, 0755);
	if (TG_save(&store, store_path) != 0) {
		say("couldn't save the tags file");
		TG_load(&store, store_path);
		remember();
		toast(tr("Couldn't save tags"));
		return -1;
	}
	remember();
	return 0;
}

static void fullPath(const char* rel, char* out, size_t size) {
	snprintf(out, size, "%s%s", host->sdcard_path, rel);
}

static int gamesOnCard(int tag) {
	int n = 0;
	for (int i = 0; i < store.tags[tag].count; i++) {
		char full[TG_PATH_LEN * 2];
		fullPath(store.tags[tag].games[i], full, sizeof(full));
		if (exists(full)) n++;
	}
	return n;
}

static int tagsOnCard(void) {
	int n = 0;
	for (int i = 0; i < store.count; i++) {
		if (gamesOnCard(i)) n++;
	}
	return n;
}

static void tagFolder(const char* name, char* out, size_t size) {
	snprintf(out, size, "%s/%s", folder_path, name);
}

// Has the launcher list the folders again, so the Tags folder follows.
static void reload(void) {
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	else host->request_redraw();
}

///////////////////////////////////////
// the action

static int selectedGame(char* out, size_t size) {
	const MosaicEntry* entry = host->selected_entry();
	if (!entry) return 0;
	return TG_gamePath(host->sdcard_path, host->entry_path(entry), host->entry_type(entry), out, size);
}

static int canEdit(void* data) {
	(void)data;
	char rel[TG_PATH_LEN];
	return !showing && selectedGame(rel, sizeof(rel));
}

static void edit(void* data) {
	(void)data;
	if (showing || !selectedGame(game, sizeof(game))) return;
	const char* name = host->entry_name(host->selected_entry());
	snprintf(title, sizeof(title), "%s", name && name[0] ? name : game);
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the checklist");
}

///////////////////////////////////////
// the checklist

static void countAll(void) {
	for (int i = 0; i < store.count; i++) on_card[i] = gamesOnCard(i);
}

// rows: the tags, then New tag
static int rowCount(void) {
	return store.count + 1;
}

static int isTagRow(int row) {
	return row >= 0 && row < store.count;
}

static int checkEnter(void* data) {
	(void)data;
	refresh();
	countAll();
	selected = 0;
	first = 0;
	changed = 0;
	showing = 1;
	char message[TG_PATH_LEN + 64];
	snprintf(message, sizeof(message), "checklist for %s: %i tags", game, store.count);
	say(message);
	return 0;
}

static void checkExit(void* data) {
	(void)data;
	showing = 0;
	if (changed) reload();
	changed = 0;
}

static void toggle(int row) {
	if (!isTagRow(row)) return;
	int on = !TG_has(&store, row, game);
	if (TG_set(&store, row, game, on) < 0) {
		toast(tr("Couldn't tag that"));
		return;
	}
	char name[TG_NAME_LEN];
	snprintf(name, sizeof(name), "%s", store.tags[row].name);
	if (save() != 0) {
		countAll();
		return;
	}
	on_card[row] += on ? 1 : -1;
	changed = 1;
	char message[TG_PATH_LEN + TG_NAME_LEN + 32];
	snprintf(message, sizeof(message), on ? "tagged %s with %s" : "untagged %s from %s", game, name);
	say(message);
}

static void named(const char* text, void* data) {
	(void)data;
	if (!text || !showing) return; // cancelled
	int added = 0;
	int tag = TG_add(&store, text, &added);
	if (tag < 0) {
		toast(store.count >= TG_TAGS_MAX ? tr("Too many tags") : tr("Not a tag name"));
		return;
	}
	int tagged = TG_set(&store, tag, game, 1);
	char name[TG_NAME_LEN];
	snprintf(name, sizeof(name), "%s", store.tags[tag].name);
	if (tagged < 0 || save() != 0) {
		if (tagged < 0) TG_load(&store, store_path);
		countAll();
		if (tagged < 0) toast(tr("Couldn't tag that"));
		return;
	}
	countAll();
	selected = TG_find(&store, name);
	if (selected < 0) selected = 0;
	changed = 1;
	char message[TG_PATH_LEN + TG_NAME_LEN + 32];
	if (added) {
		snprintf(message, sizeof(message), "added tag %s", name);
		say(message);
	}
	if (tagged) {
		snprintf(message, sizeof(message), "tagged %s with %s", game, name);
		say(message);
	}
	host->request_redraw();
}

static void deleteConfirmed(void* data) {
	(void)data;
	if (!showing) return;
	refresh();
	int tag = TG_find(&store, deleting);
	if (tag < 0 || gamesOnCard(tag)) return;
	TG_remove(&store, tag);
	if (save() == 0) {
		char message[TG_NAME_LEN + 32];
		snprintf(message, sizeof(message), "deleted tag %s", deleting);
		say(message);
		changed = 1;
	}
	countAll();
	selected = TG_move(selected, rowCount(), 0, 0);
	deleting[0] = '\0';
	host->request_redraw();
}

static int pressedBit(unsigned int bits, int button) {
	return (bits & MOSAIC_BUTTON_BIT(button)) != 0;
}

static int checkInput(const MosaicInput* input, void* data) {
	(void)data;
	unsigned int pressed = input->pressed;
	if (pressedBit(pressed, MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;

	int before = selected;
	if (pressedBit(input->repeated, MOSAIC_BUTTON_UP)) {
		selected = TG_move(selected, rowCount(), -1, pressedBit(pressed, MOSAIC_BUTTON_UP));
	}
	if (pressedBit(input->repeated, MOSAIC_BUTTON_DOWN)) {
		selected = TG_move(selected, rowCount(), 1, pressedBit(pressed, MOSAIC_BUTTON_DOWN));
	}
	if (pressedBit(input->repeated, MOSAIC_BUTTON_LEFT)) selected = TG_move(selected, rowCount(), -5, 0);
	if (pressedBit(input->repeated, MOSAIC_BUTTON_RIGHT)) selected = TG_move(selected, rowCount(), 5, 0);
	if (selected != before) host->request_redraw();

	if (pressedBit(pressed, MOSAIC_BUTTON_A)) {
		if (isTagRow(selected)) {
			toggle(selected);
			host->request_redraw();
		}
		else host->keyboard(tr("New tag"), "", named, NULL);
	}
	else if (pressedBit(pressed, MOSAIC_BUTTON_X) && isTagRow(selected)) {
		if (on_card[selected]) toast(tr("Untag its games to delete it"));
		else {
			snprintf(deleting, sizeof(deleting), "%s", store.tags[selected].name);
			char question[TG_NAME_LEN + 256];
			snprintf(question, sizeof(question), tr("Delete the tag %s?"), deleting);
			host->confirm(question, tr("DELETE"), deleteConfirmed, NULL);
		}
	}
	return MOSAIC_SCREEN_STAY;
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

#define C_MAIN() themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255)
#define C_TEXT() themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 255)
#define C_SELECTED() themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000, 255)
#define C_HINT() themeColor(MOSAIC_COLOR_HINT, 0x999999, 255)
#define C_BACKGROUND() themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255)

// The hints, and the y they start at.
static int drawHints(MosaicSurface* screen) {
	int scale = host->screen_scale();
	int top = host->screen_height() - 40 * scale;
	MosaicRect drawn;
	int tag = isTagRow(selected);
	MosaicHint left[] = { { MOSAIC_BUTTON_B, "BACK" }, { MOSAIC_BUTTON_X, "DELETE" } };
	int deletable = tag && !on_card[selected];
	if (host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, deletable ? 2 : 1, &drawn) == 0
			|| (deletable && host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, 1, &drawn) == 0)) {
		top = drawn.y;
	}
	MosaicHint a[] = { { MOSAIC_BUTTON_A, !tag ? "NEW" : TG_has(&store, selected, game) ? "UNTAG" : "TAG" } };
	if (host->draw_hints(screen, MOSAIC_HINTS_RIGHT, a, 1, &drawn) == 0 && drawn.y < top) top = drawn.y;
	return top;
}

static void drawBox(MosaicSurface* screen, const MosaicRect* box, int on, int row_selected) {
	int scale = host->screen_scale();
	MosaicColor ink = row_selected ? C_SELECTED() : C_TEXT();
	host->fill_round_rect(screen, box, 2 * scale, ink);
	if (on) return;
	int line = scale > 1 ? scale : 1;
	MosaicRect inner = { box->x + line, box->y + line, box->w - 2 * line, box->h - 2 * line };
	host->fill_round_rect(screen, &inner, scale, row_selected ? C_MAIN() : C_BACKGROUND());
}

static int checkRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int row_h = 30 * scale;
	int hints_y = drawHints(screen);

	const MosaicRect* area = &view->area;
	int margin = view->status.w ? area->x + area->w - (view->status.x + view->status.w) : pad;
	int bar_y = view->status.w ? view->status.y : margin;
	int bar_h = view->status.w ? view->status.h : row_h;
	int right = view->header.w ? view->header.x : view->status.w ? view->status.x : area->x + area->w - margin;
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	host->draw_text(screen, MOSAIC_FONT_LARGE, title, area->x + margin + pad, bar_y + (bar_h - large_h) / 2, C_TEXT(),
		right - pad - (area->x + margin + pad));

	MosaicRect list = { area->x + margin, bar_y + bar_h + margin / 2, area->w - 2 * margin, 0 };
	list.h = hints_y - margin / 2 - list.y;
	int fit = list.h / row_h;
	if (fit < 1) return 0;
	first = TG_window(selected, rowCount(), fit, first);
	int box_size = large_h * 2 / 3;
	for (int i = first; i < rowCount() && i < first + fit; i++) {
		MosaicRect r = { list.x, list.y + (i - first) * row_h, list.w, row_h };
		int on = i == selected;
		if (on) host->fill_round_rect(screen, &r, row_h / 2, C_MAIN());
		int x = r.x + pad;
		if (!isTagRow(i)) {
			host->draw_text(screen, MOSAIC_FONT_LARGE, tr("New tag"), x, r.y + (row_h - large_h) / 2, on ? C_SELECTED() : C_HINT(),
				r.w - 2 * pad);
			continue;
		}
		MosaicRect box = { x, r.y + (row_h - box_size) / 2, box_size, box_size };
		drawBox(screen, &box, TG_has(&store, i, game), on);
		x += box_size + pad;

		char count[128];
		if (on_card[i]) snprintf(count, sizeof(count), trPlural("%i game", "%i games", (unsigned long)on_card[i]), on_card[i]);
		else snprintf(count, sizeof(count), "%s", tr("unused"));
		int count_w = host->text_width(MOSAIC_FONT_SMALL, count);
		if (count_w > r.w / 3) count_w = r.w / 3;
		if (count_w > 0) {
			host->draw_text(screen, MOSAIC_FONT_SMALL, count, r.x + r.w - pad - count_w, r.y + (row_h - small_h) / 2,
				on ? C_SELECTED() : C_HINT(), count_w);
		}
		int name_w = r.x + r.w - pad - (count_w > 0 ? count_w + pad : 0) - x;
		if (name_w > 0) {
			host->draw_text(screen, MOSAIC_FONT_LARGE, store.tags[i].name, x, r.y + (row_h - large_h) / 2,
				on ? C_SELECTED() : C_TEXT(), name_w);
		}
	}
	return 0;
}

///////////////////////////////////////
// folders

static void rootEntries(MosaicList* root) {
	if (!root_on) return;
	refresh();
	int tags = tagsOnCard();
	if (tags != root_logged) {
		char message[64];
		snprintf(message, sizeof(message), "root: %i tags with games", tags);
		say(message);
		root_logged = tags;
	}
	if (!tags) return;
	host->list_push(root, host->entry_new(folder_path, MOSAIC_ENTRY_DIR, tr("Tags")));
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, folder_path)) {
		refresh();
		MosaicList* list = host->list_new();
		for (int i = 0; i < store.count; i++) {
			if (!gamesOnCard(i)) continue;
			char dir[TG_PATH_LEN + TG_NAME_LEN];
			tagFolder(store.tags[i].name, dir, sizeof(dir));
			host->list_push(list, host->entry_new(dir, MOSAIC_ENTRY_DIR, store.tags[i].name));
		}
		return list;
	}

	size_t len = strlen(folder_path);
	if (strncmp(path, folder_path, len) != 0 || path[len] != '/') return NULL;
	refresh();
	MosaicList* list = host->list_new();
	int tag = TG_folderTag(&store, folder_path, path);
	if (tag < 0) return list;
	for (int i = 0; i < store.tags[tag].count; i++) {
		char full[TG_PATH_LEN * 2];
		fullPath(store.tags[tag].games[i], full, sizeof(full));
		if (!exists(full)) continue; // kept in the file in case the card comes back
		host->list_push(list, host->entry_new(full, endsWith(full, ".pak") ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM, NULL));
	}
	return list;
}

///////////////////////////////////////
// quick menu tiles

static int tileAvailable(void* data) {
	const char* name = tiles[(intptr_t)data];
	refresh();
	int tag = TG_find(&store, name);
	return tag >= 0 && gamesOnCard(tag) > 0;
}

static void tileOpen(void* data) {
	char dir[TG_PATH_LEN + TG_NAME_LEN];
	tagFolder(tiles[(intptr_t)data], dir, sizeof(dir));
	if (!host->open_path(dir, MOSAIC_ENTRY_DIR)) say("couldn't open a tag's folder");
}

static void addTiles(void) {
	if (!MOSAIC_HOST_HAS(host, quick_tile)) {
		say("this launcher has no quick menu tiles for plugins");
		return;
	}
	int count = 0;
	for (int i = 0; i < store.count && count < TILES_MAX; i++) {
		if (!gamesOnCard(i)) continue;
		snprintf(tiles[count], TG_NAME_LEN, "%s", store.tags[i].name);
		char id[32];
		snprintf(id, sizeof(id), "tags.tile.%i", count);
		if (host->register_action(id, tiles[count], tileAvailable, tileOpen, (void*)(intptr_t)count) != 0
				|| host->quick_tile(id, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0) {
			say("couldn't add a quick menu tile");
			break;
		}
		count++;
	}
	char message[48];
	snprintf(message, sizeof(message), "%i quick menu tiles", count);
	say(message);
}

///////////////////////////////////////
// hooks

static void postReturn(void) {
	refresh();
}

static void quit(void) {
	TG_free(&store);
}

static int setting(const char* key) {
	return strcmp(host->cfg_get(key, "on"), "off") != 0;
}

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "Y", MOSAIC_BUTTON_Y },
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
};

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_hints) || !MOSAIC_HOST_HAS(host, keyboard) || !MOSAIC_HOST_HAS(host, confirm)) {
		say("needs a launcher with plugin screens, draw_hints, the keyboard and confirm");
		return -1;
	}
	snprintf(folder_path, sizeof(folder_path), "%s/" FOLDER_NAME, host->sdcard_path);
	snprintf(state_dir, sizeof(state_dir), "%s/launcher", host->userdata_path);
	snprintf(store_path, sizeof(store_path), "%s/tags.txt", state_dir);
	TG_free(&store);
	memset(&seen, 0, sizeof(seen));
	seen.st_size = -1; // so the first refresh reads the file
	refresh();
	root_on = setting("root");
	root_logged = -1;
	showing = 0;

	MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = checkEnter,
		.exit = checkExit,
		.input = checkInput,
		.render = checkRender,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("couldn't register the checklist");
		return -1;
	}

	const char* name = host->cfg_get("button", "Y");
	int button = MOSAIC_BUTTON_Y;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (!strcmp(name, buttons[i].name)) button = buttons[i].button;
	}
	if (host->register_action("tags.edit", "TAGS", canEdit, edit, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, "tags.edit") != 0) {
		say("couldn't bind a button; tags.edit can still go on an FN button");
	}
	if (setting("tiles")) addTiles();

	char message[96];
	snprintf(message, sizeof(message), "%i tags, %i with games, root %s", store.count, tagsOnCard(), root_on ? "on" : "off");
	say(message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
