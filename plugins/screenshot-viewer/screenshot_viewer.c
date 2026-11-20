// Screenshot viewer: browse the screenshots stock's minarch saves to
// <sd>/Screenshots (named <game>.<YYYY-MM-DD-HH-MM-SS>.png) on a screen of
// its own, newest first.
//
// The screenshot-viewer.open action opens it, from a Screenshots tile on
// the quick menu (the "tile" setting) or a game list button (the "button"
// setting), or button-remap or an FN button (action:screenshot-viewer.open).
// It's available while the folder has a screenshot.
//
// The screenshot-viewer.capture action saves what's on screen with the
// launcher's save_screenshot, as Screenshots/Launcher.<date>.png, or what
// a menu it runs from opened over. It binds no button itself: the user
// puts it on one, such as an FN button set to
// action:screenshot-viewer.capture.
//
// The screen starts on a grid of thumbnails, with the selected one's game
// and date on a pill in the top left; the d-pad moves through it, A shows
// the selected one across the screen and B closes it. In the full view
// left and right step to the next and previous, wrapping on a press, and
// B goes back to the grid. Y, in either, deletes the selected screenshot
// after the launcher's gate (gate_edit, as screenshot-viewer.delete, so
// kiosk mode asks for the PIN) and its confirm (the "delete" setting turns
// it off). Only
// regular files directly in the folder are listed or deleted, looked at
// without following links. It draws stock's hints with draw_hints.
//
// Images come from art_load, decoded off the main thread into the
// launcher's art cache, and each is copied once with surface_scale to the
// size it's drawn at. The copies, the plugin's own surfaces, sit in two
// small pools (thumbnails and full views) where the least recently drawn
// goes first, and are freed when the screen closes. Everything is drawn
// around the status pill and header widgets, which stay clear.
//
// The screenshot-viewer.media action, on Y on the game list (and the
// "media_button" setting's button), opens the game's media on a screen of
// its own: every image it has beside it and in the screenshots folder,
// then its clip, in media_model.h's order, left and right stepping through
// them and B going back, with the file each one is on the title pill. X on
// a shot in the grid opens its game's media, the game found by the name
// the shot has. The images use the full views' pool. The clip plays
// through video-previews' video.play service, on that plugin's own full
// screen over this one, while it's there; without it clips are left out.
// While the clip shows, input_filter takes left, right and B, as that
// screen takes no buttons, and the file it is shows as a toast.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "media_model.h"
#include "shot_model.h"

#define ID "screenshot-viewer"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
#define CAPTURE_ID ID ".capture"
#define MEDIA_ID ID ".media"
#define MEDIA_SCREEN_ID ID ".media-view"
#define DELETE_EDIT ID ".delete"
#define CLIP_SCREEN_ID "video-previews.saver"
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
static int viewing = 0; // 1 while the screen is open
static char pending[SV_NAME_MAX]; // the file the confirm is about
static char shown[SV_NAME_MAX + 128];

// a game's media
static SMList media;
static int media_selected = 0;
static int media_open = 0;
static char media_rom[SV_PATH_MAX]; // the game, or "" for a shot's name without one
static char media_game[SV_NAME_MAX]; // the name its screenshots have
static char media_shown[SV_PATH_MAX + 64];
static int clips = -1;     // 1 when video.play answers, -1 before it's asked
static int clip_due = 0;   // the selected clip plays on the next tick
static int clip_on = 0;    // it shows, on video-previews' screen over this one
static int clip_failed = 0;
// what the Media action's available said last, for the path it was asked about
static char available_path[SV_PATH_MAX];
static int available_said = -1;
static void forgetAvailable(void);
static void openShotMedia(const SVShot* shot);

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

// Draws the image at path fitted inside box, around hole, from a copy in
// copies, by key, made for a box this size, or else from the art cache,
// copying it. Returns 1 when it's drawn, 0 while it loads, -1 when it
// can't be.
static int drawImage(MosaicSurface* screen, const char* key, const char* path, const MosaicRect* box,
		const MosaicRect* hole, SVCopy* copies, int count) {
	const MosaicSurface* src = NULL;
	SVCopy* copy = SV_copyFind(copies, count, key, box->w, box->h);
	if (copy) {
		copy->used = frame;
		src = copy->surface;
	} else {
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
			snprintf(slot->file, sizeof(slot->file), "%s", key);
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

static int drawShot(MosaicSurface* screen, const SVShot* shot, const MosaicRect* box, const MosaicRect* hole,
		SVCopy* copies, int count) {
	char path[SV_PATH_MAX];
	pathOf(shot->file, path, sizeof(path));
	return drawImage(screen, shot->file, path, box, hole, copies, count);
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
	SV_hints(list.count, full, can_delete, 1, left, &left_count, right, &right_count);
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
	forgetAvailable();
	selected = SV_afterRemove(at, list.count);
	if (list.count == 0) full = 0;
	snprintf(message, sizeof(message), "deleted %s, %i left", pending, list.count);
	say(message);
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Screenshot deleted"));
	host->request_redraw();
}

static char question[SV_NAME_MAX + 32 + 256];

// The gate's answer: the confirm once it may go ahead.
static void deleteGated(int answer, void* data) {
	(void)data;
	char message[SV_NAME_MAX + 64];
	if (answer != MOSAIC_GATE_RUN || !viewing) {
		snprintf(message, sizeof(message), "not deleting %s: refused", pending);
		say(message);
		return;
	}
	host->confirm(question, tr("DELETE"), deleteConfirmed, NULL);
}

// Asks the launcher's gate first (kiosk mode's PIN), then confirms.
static void askDelete(void) {
	snprintf(pending, sizeof(pending), "%s", list.shots[selected].file);
	char text[SV_NAME_MAX + 32];
	SV_title(&list.shots[selected], text, sizeof(text));
	snprintf(question, sizeof(question), tr("Delete this screenshot?\n%s"), text);
	if (MOSAIC_HOST_HAS(host, gate_edit)) host->gate_edit(DELETE_EDIT, deleteGated, NULL);
	else deleteGated(MOSAIC_GATE_RUN, NULL);
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	int count = SV_scan(dir, &list);
	// the card index is read once a plugin asks: X finds a shot's game in it
	if (MOSAIC_HOST_HAS(host, index_refresh)) host->index_count(NULL);
	viewing = 1;
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
	viewing = 0;
	copiesClear();
	SV_listFree(&list);
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	unsigned int bit_a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A), bit_b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	unsigned int bit_y = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y), bit_x = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X);
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
	} else if (count > 0 && !full && (in->pressed & bit_x)) {
		openShotMedia(&list.shots[selected]);
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
// a game's media

static int isScreen(const char* id) {
	const char* now = host->current_screen();
	return now && strcmp(now, id) == 0;
}

// 1 when video.play answers, asked once (not from init, as video-previews
// may load after this plugin) and not over its own screen, which a call
// without a game closes
static int clipsPlay(void) {
	if (clips >= 0) return clips;
	if (!MOSAIC_HOST_HAS(host, call) || !host->has_capability("video.play")) return clips = 0;
	if (isScreen(CLIP_SCREEN_ID)) return 0;
	char reply[MOSAIC_CALL_MAX];
	clips = host->call("video.play", NULL, reply, sizeof(reply)) >= 0;
	return clips;
}

static int isDir(const char* path) {
	struct stat st;
	return path && path[0] && lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// the name the game list shows the game at path by, as its screenshots
// have it
static void gameName(const char* path, char* out, size_t size) {
	out[0] = '\0';
	if (MOSAIC_HOST_HAS(host, index_refresh)) {
		MosaicIndexEntry entry = { .size = sizeof(entry) };
		if (host->index_find(path, &entry) >= 0 && entry.name && entry.name[0]) {
			snprintf(out, size, "%s", entry.name);
			return;
		}
	}
	SM_listName(path, isDir(path), out, size);
}

// the game the index shows as name, into out, or "" when there's none
static void gameNamed(const char* name, char* out, size_t size) {
	out[0] = '\0';
	if (!MOSAIC_HOST_HAS(host, index_refresh)) return;
	int count = host->index_count(NULL);
	for (int i = 0; i < count; i++) {
		MosaicIndexEntry entry = { .size = sizeof(entry) };
		if (host->index_entry(NULL, i, &entry) != 0 || entry.kind != MOSAIC_INDEX_GAME) continue;
		if (entry.name && entry.path && strcmp(entry.name, name) == 0) {
			snprintf(out, size, "%s", entry.path);
			return;
		}
	}
}

static void forgetAvailable(void) {
	available_path[0] = '\0';
	available_said = -1;
}

// Opens the media of the game at rom ("" for none) with screenshots named
// game.
static void openMedia(const char* rom, const char* game) {
	snprintf(media_rom, sizeof(media_rom), "%s", rom ? rom : "");
	snprintf(media_game, sizeof(media_game), "%s", game ? game : "");
	if (host->open_screen(MEDIA_SCREEN_ID) != 0) say("couldn't open the media screen");
}

static const SMItem* mediaItem(void) {
	return media_selected >= 0 && media_selected < media.count ? &media.items[media_selected] : NULL;
}

// selected moved: its clip plays when it's one
static void mediaMoved(void) {
	const SMItem* item = mediaItem();
	clip_failed = 0;
	clip_due = item && item->kind == SM_CLIP;
	host->request_redraw();
}

// closes video-previews' screen, when the clip shows on it
static void stopClip(void) {
	if (!clip_on) return;
	clip_on = 0;
	char reply[MOSAIC_CALL_MAX];
	if (isScreen(CLIP_SCREEN_ID)) host->call("video.play", NULL, reply, sizeof(reply));
}

static void playClip(void) {
	clip_due = 0;
	const SMItem* item = mediaItem();
	if (!item || item->kind != SM_CLIP) return;
	char args[SV_PATH_MAX + 32], reply[MOSAIC_CALL_MAX], playing[8] = "";
	snprintf(args, sizeof(args), "path=%s\nclock=off\n", media_rom);
	if (host->call("video.play", args, reply, sizeof(reply)) >= 0) host->event_value(reply, "playing", playing, sizeof(playing));
	char message[SV_PATH_MAX + 64];
	if (strcmp(playing, "1") != 0) {
		clip_failed = 1;
		snprintf(message, sizeof(message), "can't play %s", SM_label(item));
		say(message);
		host->request_redraw();
		return;
	}
	clip_on = 1;
	snprintf(message, sizeof(message), "media %i of %i: %s, clip playing", media_selected, media.count, SM_label(item));
	say(message);
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(SM_label(item));
}

static int mediaEnter(void* data) {
	(void)data;
	int count = SM_find(media_rom, isDir(media_rom), media_game, dir, clipsPlay(), &media);
	media_open = 1;
	media_selected = 0;
	clip_on = clip_failed = 0;
	media_shown[0] = '\0';
	mediaMoved();
	char message[SV_PATH_MAX + SV_NAME_MAX + 64];
	snprintf(message, sizeof(message), "%i media for %s (%s)", count, media_game[0] ? media_game : "?",
		media_rom[0] ? media_rom : "no game");
	say(message);
	return 0;
}

static void mediaLeave(void* data) {
	(void)data;
	// video-previews' screen, over this one, closed with it
	clip_on = clip_due = 0;
	media_open = 0;
	copiesFree(fulls, FULL_COPIES, NULL);
	SM_listFree(&media);
	forgetAvailable();
}

// one step left or right, wrapping on a press; 1 when it moved
static int mediaStep(const MosaicInput* in) {
	unsigned int left = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT), right = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	int before = media_selected;
	if (in->repeated & left) media_selected = SV_step(media_selected, media.count, -1, (in->pressed & left) != 0);
	else if (in->repeated & right) media_selected = SV_step(media_selected, media.count, 1, (in->pressed & right) != 0);
	return media_selected != before;
}

static int mediaInput(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	const SMItem* item = mediaItem();
	if (item && item->kind == SM_CLIP && (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A))) {
		mediaMoved();
		return MOSAIC_SCREEN_STAY;
	}
	if (mediaStep(in)) mediaMoved();
	return MOSAIC_SCREEN_STAY;
}

static void mediaLog(int got) {
	char line[sizeof(media_shown)];
	const SMItem* item = mediaItem();
	if (!item) snprintf(line, sizeof(line), "no media");
	else if (item->kind == SM_CLIP) {
		snprintf(line, sizeof(line), "media %i of %i: %s, clip %s", media_selected, media.count, SM_label(item),
			clip_failed ? "can't play" : "due");
	} else {
		snprintf(line, sizeof(line), "media %i of %i: %s, image %s", media_selected, media.count, SM_label(item),
			got > 0 ? "drawn" : got == 0 ? "loading" : "none");
	}
	if (strcmp(line, media_shown) == 0) return;
	snprintf(media_shown, sizeof(media_shown), "%s", line);
	say(line);
}

static int mediaRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	frame++;
	MosaicRect hole = SV_hole(&view->status, &view->header);
	fillAround(screen, &view->area, &hole, mosaic_color(0x000000, 255));
	const SMItem* item = mediaItem();
	int got = -1;
	MosaicColor ink = mosaic_color(0xFFFFFF, 255);
	if (!item) {
		title(screen, view, &hole, tr("Media"));
		centred(screen, MOSAIC_FONT_LARGE, tr("No media"), &view->area, themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF));
	} else {
		if (item->kind == SM_IMAGE) {
			got = drawImage(screen, item->path, item->path, &view->area, &hole, fulls, FULL_COPIES);
			if (got < 0) centred(screen, MOSAIC_FONT_LARGE, tr("Can't show this image"), &view->area, ink);
			// the next and previous, so they're in by the time they're stepped to
			for (int by = -1; by <= 1; by += 2) {
				int at = SV_step(media_selected, media.count, by, 1);
				if (at == media_selected || media.items[at].kind != SM_IMAGE) continue;
				if (SV_copyFind(fulls, FULL_COPIES, media.items[at].path, view->area.w, view->area.h)) continue;
				const MosaicSurface* image = NULL;
				host->art_load(media.items[at].path, &image);
			}
		} else if (clip_failed) {
			centred(screen, MOSAIC_FONT_LARGE, tr("Can't play this clip"), &view->area, ink);
		}
		char text[SV_PATH_MAX + 32];
		SM_title(&media, media_selected, text, sizeof(text));
		title(screen, view, &hole, text);
	}
	MosaicHint left[1], right[1];
	int left_count = 0, right_count = 0;
	SM_hints(&media, media_selected, left, &left_count, right, &right_count);
	host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
	mediaLog(got);
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (!media_open) return;
	if (clip_on && isScreen(MEDIA_SCREEN_ID)) {
		// video-previews closed its screen: the clip couldn't play
		clip_on = 0;
		clip_failed = 1;
		host->request_redraw();
	}
	if (clip_due && isScreen(MEDIA_SCREEN_ID)) playClip();
}

// video-previews' screen takes no buttons, so these are taken while the
// clip shows on it: B goes back, and left and right step on
static int inputFilter(const MosaicInput* in, unsigned int* consumed) {
	if (!clip_on || !isScreen(CLIP_SCREEN_ID)) return 0;
	unsigned int bit_b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B);
	unsigned int steps = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT);
	if (in->pressed & bit_b) {
		*consumed = bit_b;
		clip_on = 0;
		host->close_screen(MEDIA_SCREEN_ID);
		return 0;
	}
	if (!(in->repeated & steps)) return 0;
	*consumed = in->repeated & steps;
	if (mediaStep(in)) {
		stopClip();
		mediaMoved();
	}
	return 0;
}

static int canMedia(const char* path, void* data) {
	(void)data;
	if (!path || !path[0]) return 0;
	if (available_said >= 0 && strcmp(path, available_path) == 0) return available_said;
	char game[SV_NAME_MAX];
	gameName(path, game, sizeof(game));
	snprintf(available_path, sizeof(available_path), "%s", path);
	available_said = SM_any(path, isDir(path), game, dir, clipsPlay());
	return available_said;
}

static void runMedia(const char* path, void* data) {
	(void)data;
	if (!path || !path[0]) return;
	char game[SV_NAME_MAX];
	gameName(path, game, sizeof(game));
	openMedia(path, game);
}

// X on a shot in the grid: its game's media, the game found by its name
static void openShotMedia(const SVShot* shot) {
	char rom[SV_PATH_MAX];
	gameNamed(shot->game, rom, sizeof(rom));
	openMedia(rom, shot->game);
}

///////////////////////////////////////
// the action

static int canOpen(const char* path, void* data) {
	(void)path;
	(void)data;
	return SV_any(dir);
}

static void openViewer(const char* path, void* data) {
	(void)path;
	(void)data;
	int rc = host->open_screen(SCREEN_ID);
	if (rc != 0) say("couldn't open the screenshots screen");
}

static void capture(const char* action_path, void* data) {
	(void)action_path;
	(void)data;
	char path[SV_PATH_MAX];
	if (host->save_screenshot(path, sizeof(path)) != 0) {
		say("couldn't save a screenshot");
		if (MOSAIC_HOST_HAS(host, toast)) host->toast(tr("Couldn't save a screenshot"));
		return;
	}
	char message[SV_PATH_MAX + 16];
	snprintf(message, sizeof(message), "saved %s", path);
	say(message);
	// a capture of the viewer itself shows in it, on the same selection
	if (!viewing) return;
	char was[SV_NAME_MAX] = "";
	if (selected < list.count) snprintf(was, sizeof(was), "%s", list.shots[selected].file);
	SV_scan(dir, &list);
	selected = 0;
	for (int i = 0; i < list.count; i++) {
		if (!strcmp(list.shots[i].file, was)) selected = i;
	}
	host->request_redraw();
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(dir, sizeof(dir), "%s/Screenshots", host->sdcard_path);
	SV_listInit(&list);
	SM_listInit(&media);
	media_open = clip_on = clip_due = clip_failed = 0;
	clips = -1;
	forgetAvailable();
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
	if (host->register_action(ACTION_ID, "Screenshots", 0, canOpen, openViewer, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	static const MosaicScreenHooks media_hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = mediaEnter,
		.exit = mediaLeave,
		.input = mediaInput,
		.render = mediaRender,
	};
	if (host->register_screen(MEDIA_SCREEN_ID, &media_hooks) != 0
			|| host->register_action(MEDIA_ID, "Media", MOSAIC_ACTION_PATH, canMedia, runMedia, NULL) != 0) {
		say("couldn't register " MEDIA_ID);
		return 1;
	}
	// on Y, shared with other plugins' actions there
	if (host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, MEDIA_ID) != 0) {
		say("couldn't bind the media to Y");
	}
	if (MOSAIC_HOST_HAS(host, save_screenshot)
			&& host->register_action(CAPTURE_ID, "Take screenshot", 0, NULL, capture, NULL) != 0) {
		say("couldn't register " CAPTURE_ID);
	}
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, "quick/screenshots.png", NULL, NULL) != 0)) {
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
	const char* media_name = host->cfg_get("media_button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) == 0
				&& host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
			say("couldn't bind the screenshots button");
		}
		// Y has it already
		if (buttons[i].button != MOSAIC_BUTTON_Y && strcmp(media_name, buttons[i].name) == 0
				&& host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, MEDIA_ID) != 0) {
			say("couldn't bind the media button");
		}
	}
	return 0;
}

static void quit(void) {
	copiesClear();
	SV_listFree(&list);
	SM_listFree(&media);
	media_open = clip_on = clip_due = 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.input_filter = inputFilter,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
