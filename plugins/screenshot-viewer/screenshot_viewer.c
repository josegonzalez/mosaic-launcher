// Screenshot viewer: browse the screenshots stock's minarch saves to
// <sd>/Screenshots (named <game>.<YYYY-MM-DD-HH-MM-SS>.png) on a screen of
// its own, newest first.
//
// The screenshot-viewer.open action opens it, from a Screenshots tile on
// the quick menu (the "tile" setting) or a game list button (the "button"
// setting), or button-remap or an FN button (action:screenshot-viewer.open).
// It's available while the folder has a screenshot.
//
// The screen starts on a grid of thumbnails, with the selected one's game
// and date on a pill in the top left; the d-pad moves through it, A shows
// the selected one across the screen and B closes it. In the full view
// left and right step to the next and previous, wrapping on a press, and
// B goes back to the grid. Y, in either, deletes the selected screenshot
// after the launcher's confirm (the "delete" setting turns it off). Only
// regular files directly in the folder are listed or deleted, looked at
// without following links. It draws stock's hints with draw_hints.
//
// Images come from art_load, decoded off the main thread into the
// launcher's art cache, and each is copied once with surface_scale to the
// size it's drawn at. The copies, the plugin's own surfaces, sit in two
// small pools (thumbnails and full views) where the least recently drawn
// goes first, and are freed when the screen closes. Everything is drawn
// around the status pill and header widgets, which stay clear.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "shot_model.h"

#define ID "screenshot-viewer"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
#define THUMB_COPIES 40
#define FULL_COPIES 3

static const MosaicHost* host;
static char dir[SV_DIR_MAX];
static int can_delete = 1;
static SVList list;
static int selected = 0;
static int first_row = 0;
static int cols = 1; // the grid's, for up and down
static int full = 0; // 1 in the full view
static char pending[SV_NAME_MAX]; // the file the confirm is about
static char shown[SV_NAME_MAX + 128];

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

///////////////////////////////////////
// scaled copies

static SVCopy thumbs[THUMB_COPIES];
static SVCopy fulls[FULL_COPIES];
static unsigned long frame = 0;

static void copiesFree(SVCopy* copies, int count, const char* file) {
	for (int i = 0; i < count; i++) {
		if (!copies[i].surface || (file && strcmp(copies[i].file, file) != 0)) continue;
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
	int n = SV_avoid(rect, hole, parts);
	for (int i = 0; i < n; i++) host->fill_rect(screen, &parts[i], color);
}

static void centred(MosaicSurface* screen, int font, const char* text, const MosaicRect* box, MosaicColor color) {
	char cut[256];
	int w = host->text_truncate(font, text, box->w, cut, sizeof(cut));
	int h = host->text_height(font);
	if (w < 0 || h < 0) return;
	host->draw_text(screen, font, cut, box->x + (box->w - w) / 2, box->y + (box->h - h) / 2, color, box->w);
}

static void pathOf(const char* file, char* out, size_t size) {
	snprintf(out, size, "%s/%s", dir, file);
}

// Draws the shot's image fitted inside box, around hole, from a copy in
// copies made for a box this size, or else from the art cache, copying it.
// Returns 1 when it's drawn, 0 while it loads, -1 when it can't be.
static int drawShot(MosaicSurface* screen, const SVShot* shot, const MosaicRect* box, const MosaicRect* hole,
		SVCopy* copies, int count) {
	const MosaicSurface* src = NULL;
	SVCopy* copy = SV_copyFind(copies, count, shot->file, box->w, box->h);
	if (copy) {
		copy->used = frame;
		src = copy->surface;
	} else {
		char path[SV_PATH_MAX];
		pathOf(shot->file, path, sizeof(path));
		const MosaicSurface* image = NULL;
		int state = host->art_load(path, &image);
		if (state == MOSAIC_ART_LOADING) return 0;
		int iw = 0, ih = 0;
		if (state != MOSAIC_ART_READY || !image || host->surface_size(image, &iw, &ih) != 0) return -1;
		MosaicRect to = SV_fit(iw, ih, box);
		if (to.w <= 0) return -1;
		src = image;
		MosaicSurface* scaled = host->surface_scale(ID, image, to.w, to.h, MOSAIC_FIT_STRETCH);
		if (scaled) {
			SVCopy* slot = SV_copyPick(copies, count);
			if (slot->surface) host->surface_free(ID, slot->surface);
			snprintf(slot->file, sizeof(slot->file), "%s", shot->file);
			slot->w = box->w;
			slot->h = box->h;
			slot->surface = scaled;
			slot->used = frame;
			src = scaled;
		}
	}
	int sw = 0, sh = 0;
	if (host->surface_size(src, &sw, &sh) != 0) return -1;
	MosaicRect to = SV_fit(sw, sh, box);
	MosaicRect parts[4];
	int n = SV_avoid(&to, hole, parts);
	for (int i = 0; i < n; i++) {
		MosaicRect from = SV_source(&to, &parts[i], sw, sh);
		host->blit(screen, src, &from, &parts[i]);
	}
	return 1;
}

// asks the art cache for a shot so it's in by the time it's stepped to
static void prefetch(int index, const MosaicRect* box) {
	if (index < 0 || index >= list.count || index == selected) return;
	if (SV_copyFind(fulls, FULL_COPIES, list.shots[index].file, box->w, box->h)) return;
	char path[SV_PATH_MAX];
	pathOf(list.shots[index].file, path, sizeof(path));
	const MosaicSurface* image = NULL;
	host->art_load(path, &image);
}

// the title pill in the top left
static void title(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole, const char* text) {
	int scale = host->screen_scale();
	MosaicRect rect = SV_titlePill(&view->area, hole, scale, host->text_width(MOSAIC_FONT_LARGE, text));
	if (rect.w <= 0) return;
	int inner = 12 * scale;
	int h = host->text_height(MOSAIC_FONT_LARGE);
	host->fill_round_rect(screen, &rect, rect.h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	host->draw_text(screen, MOSAIC_FONT_LARGE, text, rect.x + inner, rect.y + (rect.h - h + 1) / 2,
		themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF), rect.w - 2 * inner);
}

static void selectedTitle(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole) {
	char text[SV_NAME_MAX + 32];
	SV_title(&list.shots[selected], text, sizeof(text));
	title(screen, view, hole, text);
}

static int renderFull(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole) {
	fillAround(screen, &view->area, hole, mosaic_color(0x000000, 255));
	int got = drawShot(screen, &list.shots[selected], &view->area, hole, fulls, FULL_COPIES);
	if (got < 0) centred(screen, MOSAIC_FONT_LARGE, tr("Can't show this screenshot"), &view->area, mosaic_color(0xFFFFFF, 255));
	prefetch(selected + 1, &view->area);
	prefetch(selected - 1, &view->area);
	selectedTitle(screen, view, hole);
	return got;
}

// Returns how many thumbnails in view are drawn, or -1 when the grid
// doesn't fit (and the full view is drawn).
static int renderGrid(MosaicSurface* screen, const MosaicScreenView* view, const MosaicRect* hole) {
	int scale = host->screen_scale();
	int name_h = host->text_height(MOSAIC_FONT_SMALL) + 2 * scale;
	SVGrid grid;
	if (SV_grid(&view->area, hole, scale, host->screen_width(), host->screen_height(), name_h, &grid) != 0) return -1;
	cols = grid.cols;
	first_row = SV_firstRow(first_row, selected, list.count, grid.cols, grid.rows);
	MosaicColor card = themeColor(MOSAIC_COLOR_ACCENT2, 0x333333);
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor ring = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	int drawn = 0;
	int from = first_row * grid.cols;
	for (int i = from; i < list.count && i < from + grid.cols * grid.rows; i++) {
		MosaicRect cell = SV_cell(&grid, i - from);
		MosaicRect box = { cell.x, cell.y, cell.w, grid.thumb_h };
		if (i == selected) {
			MosaicRect frame_rect = { box.x - 2 * scale, box.y - 2 * scale, box.w + 4 * scale, box.h + 4 * scale };
			host->fill_rect(screen, &frame_rect, ring);
		}
		host->fill_rect(screen, &box, card);
		int got = drawShot(screen, &list.shots[i], &box, NULL, thumbs, THUMB_COPIES);
		if (got > 0) drawn++;
		if (got < 0) centred(screen, MOSAIC_FONT_SMALL, "?", &box, ink);
		host->draw_text(screen, MOSAIC_FONT_SMALL, list.shots[i].game, cell.x, box.y + box.h + 2 * scale,
			i == selected ? ring : ink, cell.w);
	}
	selectedTitle(screen, view, hole);
	return drawn;
}

static void hints(MosaicSurface* screen) {
	MosaicHint left[2], right[2];
	int left_count = 0, right_count = 0;
	SV_hints(list.count, full, can_delete, left, &left_count, right, &right_count);
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
}

// logs what shows whenever it changes, for tests and bug reports
static void logShown(int got, int is_full) {
	char line[sizeof(shown)];
	if (list.count <= 0) {
		snprintf(line, sizeof(line), "no screenshots");
	} else {
		const SVShot* shot = &list.shots[selected];
		char date[32];
		SV_date(shot, date, sizeof(date));
		if (is_full) {
			snprintf(line, sizeof(line), "view %i of %i: %s, %s, image %s", selected, list.count, shot->game,
				date[0] ? date : "no date", got > 0 ? "drawn" : got == 0 ? "loading" : "none");
		} else {
			snprintf(line, sizeof(line), "grid %i of %i: %s, %s, %i columns", selected, list.count, shot->game,
				date[0] ? date : "no date", cols);
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
	int at = -1;
	for (int i = 0; i < list.count; i++) {
		if (strcmp(list.shots[i].file, pending) == 0) at = i;
	}
	char message[SV_NAME_MAX + 64];
	if (at < 0 || SV_remove(dir, &list, at) != 0) {
		snprintf(message, sizeof(message), "can't delete %s", pending);
		say(message);
		if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Couldn't delete that"));
		return;
	}
	copiesFree(thumbs, THUMB_COPIES, pending);
	copiesFree(fulls, FULL_COPIES, pending);
	selected = SV_afterRemove(at, list.count);
	if (list.count == 0) full = 0;
	snprintf(message, sizeof(message), "deleted %s, %i left", pending, list.count);
	say(message);
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Screenshot deleted"));
	host->request_redraw();
}

static void askDelete(void) {
	snprintf(pending, sizeof(pending), "%s", list.shots[selected].file);
	char text[SV_NAME_MAX + 32];
	SV_title(&list.shots[selected], text, sizeof(text));
	char question[sizeof(text) + 256];
	snprintf(question, sizeof(question), tr("Delete this screenshot?\n%s"), text);
	host->confirm(question, tr("DELETE"), deleteConfirmed, NULL);
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	int count = SV_scan(dir, &list);
	selected = 0;
	first_row = 0;
	full = 0;
	shown[0] = '\0';
	char message[SV_PATH_MAX + 64];
	snprintf(message, sizeof(message), "%i screenshots in %s", count < 0 ? 0 : count, dir);
	say(message);
	return 0;
}

static void leave(void* data) {
	(void)data;
	copiesClear();
	SV_listFree(&list);
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	unsigned int bit_a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A), bit_b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	unsigned int bit_y = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y);
	int count = list.count;
	int before = selected, was_full = full;

	if (in->pressed & bit_b) {
		if (!full) return MOSAIC_SCREEN_CLOSE;
		full = 0;
	} else if (count > 0 && (in->pressed & bit_a)) {
		full = 1;
	} else if (count > 0 && can_delete && (in->pressed & bit_y)) {
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
			if (!(in->repeated & bit) || (full && steps[i].dy)) continue;
			if (steps[i].dx) selected = SV_step(selected, count, steps[i].dx, (in->pressed & bit) != 0);
			else selected = SV_row(selected, count, cols, steps[i].dy);
			break; // one direction a frame
		}
	}
	if (selected != before || full != was_full) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	frame++;
	MosaicRect hole = SV_hole(&view->status, &view->header);
	int got = -1, is_full = full;
	if (selected >= list.count) selected = list.count > 0 ? list.count - 1 : 0;
	if (list.count <= 0) {
		fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
		title(screen, view, &hole, tr("Screenshots"));
		centred(screen, MOSAIC_FONT_LARGE, tr("No screenshots"), &view->area, themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF));
	} else if (!full) {
		fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
		got = renderGrid(screen, view, &hole);
		// too small for a grid: the full view stands in
		if (got < 0) is_full = 1;
	}
	if (list.count > 0 && is_full) got = renderFull(screen, view, &hole);
	hints(screen);
	logShown(got, is_full);
	return 0;
}

///////////////////////////////////////
// the action

static int canOpen(void* data) {
	(void)data;
	return SV_any(dir);
}

static void openViewer(void* data) {
	(void)data;
	int rc = host->open_screen(SCREEN_ID);
	if (rc != 0) say("couldn't open the screenshots screen");
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(dir, sizeof(dir), "%s/Screenshots", host->sdcard_path);
	SV_listInit(&list);
	memset(thumbs, 0, sizeof(thumbs));
	memset(fulls, 0, sizeof(fulls));
	if (!MOSAIC_HOST_HAS(host, draw_hints) || !MOSAIC_HOST_HAS(host, art_get) || !MOSAIC_HOST_HAS(host, open_screen)) {
		say("this launcher has no plugin screens or button hints");
		return 1;
	}
	can_delete = strcmp(host->cfg_get("delete", "on"), "off") != 0 && MOSAIC_HOST_HAS(host, confirm);
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the screenshots screen");
		return 1;
	}
	if (host->register_action(ACTION_ID, "Screenshots", canOpen, openViewer, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the Screenshots tile");
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
	const char* name = host->cfg_get("button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
			say("couldn't bind the screenshots button");
		}
	}
	return 0;
}

static void quit(void) {
	copiesClear();
	SV_listFree(&list);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
