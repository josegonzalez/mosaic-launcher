// Save state manager: browse, preview and delete the save states stock's
// minarch keeps under .userdata/shared, on a screen of its own.
//
// Two actions open it. save-state-manager.game opens the selected game's
// states, from a game list button (the "button" setting, Y by default,
// where it shares a menu with other plugins' actions on Y), button-remap or
// an FN button; it's available while the selected game has a state.
// save-state-manager.open opens every game with states, from a Save states
// tile on the quick menu (the "tile" setting), available while any game
// has one.
//
// Every game with states is a grid, the most recently saved first, each
// cell the newest preview with the game's name and how many states it has
// under it; A opens a game. A game's states are a grid in slot order with
// each slot's preview, name and time; A shows the preview across the
// screen, where left and right step through the slots, and B goes back.
// Y, on a game's states or the full view, deletes the selected state after
// the launcher's confirm (the "delete" setting turns it off): its state
// files, its preview and disc files, and the last-slot file only when it
// names that slot. Only regular files in real folders under
// .userdata/shared are ever deleted, looked at without following links.
// It draws stock's hints with draw_hints.
//
// Resuming from a chosen slot is left out: the launcher only resumes from
// the slot minarch last used, and this plugin doesn't write minarch's
// files to change it.
//
// Previews come from art_load, decoded off the main thread into the
// launcher's art cache, and each is copied once with surface_scale to the
// size it's drawn at. The copies, the plugin's own surfaces, sit in two
// small pools (grid cells and full views) where the least recently drawn
// goes first, and are freed when the screen closes.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "state_model.h"

#define ID "save-state-manager"
#define SCREEN_ID ID ".main"
#define OPEN_ID ID ".open"
#define GAME_ID ID ".game"
#define THUMB_COPIES 40
#define FULL_COPIES 3

static const MosaicHost* host;
static SMCard card;
static char roms[SM_PATH_MAX];
static int can_delete = 1;

static SMList list;   // every game with states, when opened from the tile
static SMGame single; // the one game, when opened for a game
static int all = 0;   // 1 when opened for every game
static int view = SM_VIEW_SLOTS;
static int game_sel = 0, game_row = 0;
static int slot_sel = 0, slot_row = 0;
static int cols = 1; // the grid's, for up and down
static int deleted = 0;
static struct {
	char tag[SM_NAME_MAX];
	char name[SM_NAME_MAX];
	int slot;
} pending; // the state the confirm is about
static char shown[SM_NAME_MAX * 2 + 128];

static void forget(void);

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}


static void say(const char* message) {
	host->log(ID, message);
}

// the game the slots and full view are about, or NULL
static SMGame* current(void) {
	if (!all) return &single;
	if (game_sel < 0 || game_sel >= list.count) return NULL;
	return &list.games[game_sel];
}

///////////////////////////////////////
// scaled copies

static SMCopy thumbs[THUMB_COPIES];
static SMCopy fulls[FULL_COPIES];
static unsigned long frame = 0;

static void copiesFree(SMCopy* copies, int count, const char* path) {
	for (int i = 0; i < count; i++) {
		if (!copies[i].surface || (path && strcmp(copies[i].path, path) != 0)) continue;
		host->surface_free(ID, copies[i].surface);
		memset(&copies[i], 0, sizeof(copies[i]));
	}
}

static void copiesClear(void) {
	copiesFree(thumbs, THUMB_COPIES, NULL);
	copiesFree(fulls, FULL_COPIES, NULL);
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static void fillAround(MosaicSurface* screen, const MosaicRect* rect, const MosaicRect* hole, MosaicColor color) {
	MosaicRect parts[4];
	int n = SM_avoid(rect, hole, parts);
	for (int i = 0; i < n; i++) host->fill_rect(screen, &parts[i], color);
}

static void centred(MosaicSurface* screen, int font, const char* text, const MosaicRect* box, MosaicColor color) {
	char cut[256];
	int w = host->text_truncate(font, text, box->w, cut, sizeof(cut));
	int h = host->text_height(font);
	if (w < 0 || h < 0) return;
	host->draw_text(screen, font, cut, box->x + (box->w - w) / 2, box->y + (box->h - h) / 2, color, box->w);
}

// Draws the preview at path fitted inside box, around hole, from a copy in
// copies made for a box this size, or else from the art cache, copying it.
// Returns 1 when it's drawn, 0 while it loads, -1 when it can't be.
static int drawPreview(MosaicSurface* screen, const char* path, const MosaicRect* box, const MosaicRect* hole,
		SMCopy* copies, int count) {
	if (!path || !path[0]) return -1;
	const MosaicSurface* src = NULL;
	SMCopy* copy = SM_copyFind(copies, count, path, box->w, box->h);
	if (copy) {
		copy->used = frame;
		src = copy->surface;
	} else {
		const MosaicSurface* image = NULL;
		int state = host->art_load(path, &image);
		if (state == MOSAIC_ART_LOADING) return 0;
		int iw = 0, ih = 0;
		if (state != MOSAIC_ART_READY || !image || host->surface_size(image, &iw, &ih) != 0) return -1;
		MosaicRect to = SM_fit(iw, ih, box);
		if (to.w <= 0) return -1;
		src = image;
		MosaicSurface* scaled = host->surface_scale(ID, image, to.w, to.h, MOSAIC_FIT_STRETCH);
		if (scaled) {
			SMCopy* slot = SM_copyPick(copies, count);
			if (slot->surface) host->surface_free(ID, slot->surface);
			snprintf(slot->path, sizeof(slot->path), "%s", path);
			slot->w = box->w;
			slot->h = box->h;
			slot->surface = scaled;
			slot->used = frame;
			src = scaled;
		}
	}
	int sw = 0, sh = 0;
	if (host->surface_size(src, &sw, &sh) != 0) return -1;
	MosaicRect to = SM_fit(sw, sh, box);
	MosaicRect parts[4];
	int n = SM_avoid(&to, hole, parts);
	for (int i = 0; i < n; i++) {
		MosaicRect from = SM_source(&to, &parts[i], sw, sh);
		host->blit(screen, src, &from, &parts[i]);
	}
	return 1;
}

// the title pill in the top left
static void title(MosaicSurface* screen, const MosaicScreenView* v, const MosaicRect* hole, const char* text) {
	int scale = host->screen_scale();
	MosaicRect rect = SM_titlePill(&v->area, hole, scale, host->text_width(MOSAIC_FONT_LARGE, text));
	if (rect.w <= 0) return;
	int inner = 12 * scale;
	int h = host->text_height(MOSAIC_FONT_LARGE);
	host->fill_round_rect(screen, &rect, rect.h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	host->draw_text(screen, MOSAIC_FONT_LARGE, text, rect.x + inner, rect.y + (rect.h - h + 1) / 2,
		themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF), rect.w - 2 * inner);
}

// "Tetris · Slot 3", with the date too when with_date is 1
static void slotTitle(const SMGame* game, const SMSlot* slot, int with_date, char* out, size_t size) {
	char label[128], date[32];
	SM_slotLabel(slot->slot, label, sizeof(label));
	SM_date(slot->saved, date, sizeof(date));
	if (with_date && date[0]) snprintf(out, size, "%s \xc2\xb7 %s \xc2\xb7 %s", game->display, label, date);
	else snprintf(out, size, "%s \xc2\xb7 %s", game->display, label);
}

// the newest slot with a preview, for a game's cell
static const SMSlot* coverOf(const SMGame* game) {
	const SMSlot* pick = NULL;
	for (int i = 0; i < game->count; i++) {
		const SMSlot* s = &game->slots[i];
		if (s->has_preview && (!pick || s->saved > pick->saved)) pick = s;
	}
	return pick;
}

// asks the art cache for a slot's preview so it's in by the time it's stepped to
static void prefetch(const SMGame* game, int index, const MosaicRect* box) {
	if (index < 0 || index >= game->count || index == slot_sel || !game->slots[index].has_preview) return;
	if (SM_copyFind(fulls, FULL_COPIES, game->slots[index].preview, box->w, box->h)) return;
	const MosaicSurface* image = NULL;
	host->art_load(game->slots[index].preview, &image);
}

static int renderFull(MosaicSurface* screen, const MosaicScreenView* v, const MosaicRect* hole, const SMGame* game) {
	const SMSlot* slot = &game->slots[slot_sel];
	fillAround(screen, &v->area, hole, mosaic_color(0x000000, 255));
	int got = drawPreview(screen, slot->preview, &v->area, hole, fulls, FULL_COPIES);
	if (got < 0) centred(screen, MOSAIC_FONT_LARGE, tr("No preview"), &v->area, mosaic_color(0xFFFFFF, 255));
	prefetch(game, slot_sel + 1, &v->area);
	prefetch(game, slot_sel - 1, &v->area);
	char text[SM_NAME_MAX + 256];
	slotTitle(game, slot, 1, text, sizeof(text));
	title(screen, v, hole, text);
	return got;
}

// A grid of count cells, the cell at selected framed. Returns how many
// previews in view are drawn, or -1 when the grid doesn't fit.
static int renderGrid(MosaicSurface* screen, const MosaicScreenView* v, const MosaicRect* hole, int count,
		int selected, int* first_row) {
	int scale = host->screen_scale();
	int line_h = host->text_height(MOSAIC_FONT_SMALL) + 2 * scale;
	SMGrid grid;
	if (SM_grid(&v->area, hole, scale, host->screen_width(), host->screen_height(), 2 * line_h, &grid) != 0) return -1;
	cols = grid.cols;
	*first_row = SM_firstRow(*first_row, selected, count, grid.cols, grid.rows);
	MosaicColor card_color = themeColor(MOSAIC_COLOR_ACCENT2, 0x333333);
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor ring = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	const SMGame* game = current();
	int drawn = 0;
	int from = *first_row * grid.cols;
	for (int i = from; i < count && i < from + grid.cols * grid.rows; i++) {
		MosaicRect cell = SM_cell(&grid, i - from);
		MosaicRect box = { cell.x, cell.y, cell.w, grid.thumb_h };
		if (i == selected) {
			MosaicRect frame_rect = { box.x - 2 * scale, box.y - 2 * scale, box.w + 4 * scale, box.h + 4 * scale };
			host->fill_rect(screen, &frame_rect, ring);
		}
		host->fill_rect(screen, &box, card_color);
		char top[SM_NAME_MAX], bottom[64];
		const char* preview = NULL;
		if (view == SM_VIEW_GAMES) {
			const SMGame* g = &list.games[i];
			const SMSlot* cover = coverOf(g);
			preview = cover ? cover->preview : NULL;
			snprintf(top, sizeof(top), "%s", g->display);
			snprintf(bottom, sizeof(bottom), g->count == 1 ? "%d state" : "%d states", g->count);
		} else {
			const SMSlot* s = &game->slots[i];
			preview = s->has_preview ? s->preview : NULL;
			SM_slotLabel(s->slot, top, sizeof(top));
			SM_date(s->saved, bottom, sizeof(bottom));
		}
		int got = drawPreview(screen, preview, &box, NULL, thumbs, THUMB_COPIES);
		if (got > 0) drawn++;
		if (got < 0) centred(screen, MOSAIC_FONT_SMALL, tr("No preview"), &box, ink);
		MosaicColor color = i == selected ? ring : ink;
		host->draw_text(screen, MOSAIC_FONT_SMALL, top, cell.x, box.y + box.h + 2 * scale, color, cell.w);
		host->draw_text(screen, MOSAIC_FONT_SMALL, bottom, cell.x, box.y + box.h + 2 * scale + line_h, color, cell.w);
	}
	return drawn;
}

static void hints(MosaicSurface* screen, int count) {
	MosaicHint left[2], right[2];
	int left_count = 0, right_count = 0;
	SM_hints(view, count, can_delete, left, &left_count, right, &right_count);
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
}

// logs what shows whenever it changes, for tests and bug reports
static void logShown(int shown_view, int got) {
	char line[sizeof(shown)];
	const SMGame* game = current();
	if (shown_view == SM_VIEW_GAMES) {
		if (list.count <= 0) snprintf(line, sizeof(line), "no save states");
		else {
			snprintf(line, sizeof(line), "games %i of %i: %s, %i states", game_sel, list.count, game->display,
				game->count);
		}
	} else if (!game || game->count <= 0) {
		snprintf(line, sizeof(line), "no save states");
	} else {
		const SMSlot* s = &game->slots[slot_sel];
		char label[128], date[32];
		SM_slotLabel(s->slot, label, sizeof(label));
		SM_date(s->saved, date, sizeof(date));
		if (shown_view == SM_VIEW_FULL) {
			snprintf(line, sizeof(line), "view %i of %i: %s, %s, %s, preview %s", slot_sel, game->count, game->display,
				label, date, got > 0 ? "drawn" : got == 0 ? "loading" : "none");
		} else {
			snprintf(line, sizeof(line), "slots %i of %i: %s, %s, %s, %i columns", slot_sel, game->count,
				game->display, label, date, cols);
		}
	}
	if (strcmp(line, shown) == 0) return;
	snprintf(shown, sizeof(shown), "%s", line);
	say(line);
}

///////////////////////////////////////
// deleting

static void deleteConfirmed(void* data) {
	(void)data;
	SMGame* game = current();
	int at = -1;
	if (game && strcmp(game->tag, pending.tag) == 0 && strcmp(game->name, pending.name) == 0) {
		for (int i = 0; i < game->count; i++) {
			if (game->slots[i].slot == pending.slot) at = i;
		}
	}
	char label[128];
	SM_slotLabel(pending.slot, label, sizeof(label));
	char message[SM_NAME_MAX + 256];
	char preview[SM_PATH_MAX];
	snprintf(preview, sizeof(preview), "%s", at >= 0 ? game->slots[at].preview : "");
	int removed = at < 0 ? -1 : SM_remove(&card, game, at);
	if (removed < 0) {
		snprintf(message, sizeof(message), "can't delete %s of %s", label, pending.name);
		say(message);
		if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Couldn't delete that state"));
		return;
	}
	deleted = 1;
	forget();
	if (preview[0]) {
		copiesFree(thumbs, THUMB_COPIES, preview);
		copiesFree(fulls, FULL_COPIES, preview);
	}
	snprintf(message, sizeof(message), "deleted %s of %s: %i files, %i left", label, pending.name, removed,
		game->count);
	say(message);
	slot_sel = SM_afterRemove(at, game->count);
	if (game->count == 0) {
		view = all ? SM_VIEW_GAMES : SM_VIEW_SLOTS;
		if (all) {
			SM_listDrop(&list, game_sel);
			game_sel = SM_afterRemove(game_sel, list.count);
		}
	} else if (view == SM_VIEW_FULL && slot_sel >= game->count) {
		slot_sel = game->count - 1;
	}
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Save state deleted"));
	host->request_redraw();
}

static void askDelete(void) {
	SMGame* game = current();
	if (!game || slot_sel < 0 || slot_sel >= game->count) return;
	snprintf(pending.tag, sizeof(pending.tag), "%s", game->tag);
	snprintf(pending.name, sizeof(pending.name), "%s", game->name);
	pending.slot = game->slots[slot_sel].slot;
	char text[SM_NAME_MAX + 256];
	slotTitle(game, &game->slots[slot_sel], 1, text, sizeof(text));
	char question[sizeof(text) + 256];
	snprintf(question, sizeof(question), tr("Delete this save state?\n%s"), text);
	host->confirm(question, tr("DELETE"), deleteConfirmed, NULL);
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	slot_sel = slot_row = 0;
	game_sel = game_row = 0;
	deleted = 0;
	shown[0] = '\0';
	char message[SM_NAME_MAX + 256];
	if (all) {
		view = SM_VIEW_GAMES;
		int count = SM_scanAll(&card, &list);
		snprintf(message, sizeof(message), "%i games with save states", count);
	} else {
		view = SM_VIEW_SLOTS;
		int count = SM_gameScan(&card, &single);
		snprintf(message, sizeof(message), "%s: %i save states", single.display, count);
	}
	say(message);
	return 0;
}

static void leave(void* data) {
	(void)data;
	copiesClear();
	SM_listFree(&list);
	// so the list's resume and other plugins' badges see the change
	if (deleted && MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	deleted = 0;
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	unsigned int bit_a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A), bit_b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	unsigned int bit_y = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y);
	SMGame* game = current();
	int count = view == SM_VIEW_GAMES ? list.count : game ? game->count : 0;
	int* selected = view == SM_VIEW_GAMES ? &game_sel : &slot_sel;
	int before = *selected, was = view;

	if (in->pressed & bit_b) {
		if (view == SM_VIEW_FULL) view = SM_VIEW_SLOTS;
		else if (view == SM_VIEW_SLOTS && all) view = SM_VIEW_GAMES;
		else return MOSAIC_SCREEN_CLOSE;
	} else if (count > 0 && (in->pressed & bit_a) && view != SM_VIEW_FULL) {
		if (view == SM_VIEW_GAMES) {
			slot_sel = slot_row = 0;
			view = SM_VIEW_SLOTS;
		} else {
			view = SM_VIEW_FULL;
		}
	} else if (count > 0 && can_delete && view != SM_VIEW_GAMES && (in->pressed & bit_y)) {
		askDelete();
		return MOSAIC_SCREEN_STAY;
	} else {
		static const struct {
			int button, dx, dy;
		} steps[] = {
			{ MOSAIC_BUTTON_LEFT, -1, 0 },
			{ MOSAIC_BUTTON_RIGHT, 1, 0 },
			{ MOSAIC_BUTTON_UP, 0, -1 },
			{ MOSAIC_BUTTON_DOWN, 0, 1 },
		};
		for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
			unsigned int bit = MOSAIC_BUTTON_BIT(steps[i].button);
			if (!(in->repeated & bit) || (view == SM_VIEW_FULL && steps[i].dy)) continue;
			if (steps[i].dx) *selected = SM_step(*selected, count, steps[i].dx, (in->pressed & bit) != 0);
			else *selected = SM_row(*selected, count, cols, steps[i].dy);
			break; // one direction a frame
		}
	}
	if (*selected != before || view != was) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

static int render(MosaicSurface* screen, const MosaicScreenView* v, void* data) {
	(void)data;
	frame++;
	MosaicRect hole = SM_hole(&v->status, &v->header);
	if (game_sel >= list.count) game_sel = list.count > 0 ? list.count - 1 : 0;
	SMGame* game = current();
	if (game && slot_sel >= game->count) slot_sel = game->count > 0 ? game->count - 1 : 0;
	int count = view == SM_VIEW_GAMES ? list.count : game ? game->count : 0;
	int got = -1, shown_view = view;
	fillAround(screen, &v->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
	if (count <= 0) {
		title(screen, v, &hole, view == SM_VIEW_GAMES || !game ? tr("Save states") : game->display);
		centred(screen, MOSAIC_FONT_LARGE, tr("No save states"), &v->area, themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF));
	} else if (view == SM_VIEW_GAMES) {
		got = renderGrid(screen, v, &hole, count, game_sel, &game_row);
		title(screen, v, &hole, game ? game->display : tr("Save states"));
	} else if (view == SM_VIEW_SLOTS) {
		got = renderGrid(screen, v, &hole, count, slot_sel, &slot_row);
		if (got < 0) shown_view = SM_VIEW_FULL; // too small for a grid: the full view stands in
		else {
			char text[SM_NAME_MAX + 256];
			slotTitle(game, &game->slots[slot_sel], 0, text, sizeof(text));
			title(screen, v, &hole, text);
		}
	}
	if (count > 0 && shown_view == SM_VIEW_FULL) got = renderFull(screen, v, &hole, game);
	hints(screen, count);
	logShown(shown_view, got);
	return 0;
}

///////////////////////////////////////
// the actions

static int fileExists(const char* path, void* ctx) {
	(void)ctx;
	struct stat st;
	return stat(path, &st) == 0;
}

// Sets out up for the selected game. Returns 0, or -1 when it isn't a game
// in a system folder.
static int selectedGame(SMGame* out) {
	const MosaicEntry* entry = host->selected_entry();
	if (!entry) return -1;
	int type = host->entry_type(entry);
	if (type != MOSAIC_ENTRY_ROM && type != MOSAIC_ENTRY_DIR) return -1;
	char file[SM_PATH_MAX], tag[SM_NAME_MAX], dir[SM_PATH_MAX];
	if (!SM_gameFile(host->entry_path(entry), type == MOSAIC_ENTRY_DIR, fileExists, NULL, file, sizeof(file))) return -1;
	if (!SM_tagOf(roms, file, tag, sizeof(tag))) return -1;
	snprintf(dir, sizeof(dir), "%s", file);
	char* slash = strrchr(dir, '/');
	if (!slash) return -1;
	*slash = '\0';
	return SM_gameInit(&card, tag, slash + 1, host->entry_name(entry), dir, out);
}

// The game list asks whether the action is available as it draws its
// hints, so the answer is kept for the selected path until a game returns
// or a state is deleted.
static char asked_path[SM_PATH_MAX];
static int asked_answer = -1;
static int any_answer = -1;

static void forget(void) {
	asked_path[0] = '\0';
	asked_answer = -1;
	any_answer = -1;
}

static int canOpenGame(void* data) {
	(void)data;
	const MosaicEntry* entry = host->selected_entry();
	const char* path = entry ? host->entry_path(entry) : "";
	if (asked_answer >= 0 && strcmp(path, asked_path) == 0) return asked_answer;
	static SMGame game;
	asked_answer = selectedGame(&game) == 0 && SM_gameScan(&card, &game) > 0;
	snprintf(asked_path, sizeof(asked_path), "%s", path);
	return asked_answer;
}

static void openGame(void* data) {
	(void)data;
	if (selectedGame(&single) != 0) {
		say("the selected entry isn't a game");
		return;
	}
	all = 0;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the save states screen");
}

static int canOpenAll(void* data) {
	(void)data;
	if (any_answer < 0) any_answer = SM_any(&card);
	return any_answer;
}

static void postReturn(void) {
	forget();
}

static void openAll(void* data) {
	(void)data;
	all = 1;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the save states screen");
}

static int init(const MosaicHost* h) {
	host = h;
	static const SMWords words = { tr, NULL };
	SM_setWords(&words);
	SM_cardInit(&card, host->sdcard_path);
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	SM_listInit(&list);
	forget();
	memset(&single, 0, sizeof(single));
	memset(thumbs, 0, sizeof(thumbs));
	memset(fulls, 0, sizeof(fulls));
	if (!MOSAIC_HOST_HAS(host, draw_hints) || !MOSAIC_HOST_HAS(host, art_get) || !MOSAIC_HOST_HAS(host, open_screen)) {
		say("this launcher has no plugin screens or button hints");
		return 1;
	}
	can_delete = strcmp(host->cfg_get(ID, "delete", "on"), "off") != 0 && MOSAIC_HOST_HAS(host, confirm);
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the save states screen");
		return 1;
	}
	if (host->register_action(GAME_ID, "SAVE STATES", canOpenGame, openGame, NULL) != 0
			|| host->register_action(OPEN_ID, "Save states", canOpenAll, openAll, NULL) != 0) {
		say("couldn't register the save states actions");
		return 1;
	}
	if (strcmp(host->cfg_get(ID, "tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(OPEN_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the Save states tile");
	}
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* name = host->cfg_get(ID, "button", "Y");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, GAME_ID) != 0) {
			say("couldn't bind the save states button");
		}
	}
	return 0;
}

static void quit(void) {
	copiesClear();
	SM_listFree(&list);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
