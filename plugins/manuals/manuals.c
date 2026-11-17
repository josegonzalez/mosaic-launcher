// Manuals: a game's manual or guide, as pages of images or a .txt, on a
// screen of its own. A game's manual is in the .manuals folder beside it,
// as stock keeps art in .media (see manual_model.h for the names it's
// found under).
//
// The manuals.open action, which takes the game's path, is on Y on the
// game list, beside other plugins' actions there, and is available only
// for a game that has a manual; the "button" setting puts it on a second
// button too (none by default). Pages are read one at a time with
// image_load_fit, shrunk to fit the screen as they're decoded, so a large
// scan takes only its shrunk size; one smaller than the screen is enlarged
// to fit it. L1 and R1 turn pages, as do left and right while it fits; A
// zooms to twice the fitted size, read again from the file at that size,
// and the d-pad pans. A text manual is wrapped to the screen in the
// launcher's font (the theme's, with stock's as its fallback), and up and
// down scroll it, L1, R1, left and right a screen at a time. The page, or
// the text's first line, each game was left on is kept in
// .userdata/<platform>/launcher/manuals.txt and shown again next time.
//
// The "manual" entry set has the games with a manual, so row-badges can
// mark them: has asks the card (each folder's .manuals is read once, until
// a game returns or the card index changes) and list goes through the card
// index, announcing a change once the index is ready or read again.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "manual_model.h"
#include "mosaic_plugin.h"

#define ID "manuals"
#define SCREEN_ID ID ".view"
#define ACTION_ID ID ".open"
#define SET_NAME "manual"

static const MosaicHost* host;
static char roms[MN_PATH_MAX];
static char memory_file[MN_PATH_MAX];
static MNShelf shelf;
static MNMemory memory;
static int memory_loaded = 0;

// the manual open on the screen
static int kind = MN_NONE;
static char key[MN_PATH_MAX];
static char title[MN_NAME_MAX];
static char where[MN_PATH_MAX]; // its folder of pages, or its .txt
static MNPages pages;
static int page = 0;
static MosaicSurface* fit_image = NULL; // the page fitted to the screen
static int fit_page = -1;               // which page it is, -1 for none
static MosaicSurface* zoom_image = NULL; // the page at twice that
static int zoom_page = -1;
static int failed_page = -1;            // a page that can't be read
static int zoomed = 0;
static int pan_x = 0, pan_y = 0;
static MNText text;
static int top = 0;      // the text's first line on screen
static int rows = 1;     // its lines a screen
static int wrapped_width = 0;
static char shown[MN_PATH_MAX + 128];

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text_in) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text_in) : text_in;
}

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static long long millis(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

///////////////////////////////////////
// finding a game's manual

static int isDir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// The name the game list shows for path, from the card index, or "".
static void shownName(const char* path, char* out, size_t size) {
	out[0] = '\0';
	if (!MOSAIC_HOST_HAS(host, index_refresh)) return;
	MosaicIndexEntry e = { .size = sizeof(e) };
	if (host->index_find(path, &e) >= 0 && e.name) snprintf(out, size, "%s", e.name);
}

// The kind of manual the game at path has, with its folder or .txt.
static int findManual(const char* path, int is_dir, const char* name, char* out, size_t size) {
	MNWhere w;
	if (!path || !MN_where(roms, path, is_dir, name, &w)) return MN_NONE;
	return MN_find(&shelf, &w, out, size);
}

static int manualOf(const char* path, char* out, size_t size) {
	if (!path) return MN_NONE;
	char name[MN_NAME_MAX];
	shownName(path, name, sizeof(name));
	return findManual(path, isDir(path), name, out, size);
}

///////////////////////////////////////
// the manual set

static int setHas(const char* name, const char* path) {
	(void)name;
	return manualOf(path, NULL, 0) != MN_NONE;
}

static void setList(const char* name, void (*add)(const char* path, void* ctx), void* ctx) {
	(void)name;
	if (!MOSAIC_HOST_HAS(host, index_refresh)) return;
	int count = host->index_count(NULL);
	for (int i = 0; i < count; i++) {
		MosaicIndexEntry e = { .size = sizeof(e) };
		if (host->index_entry(NULL, i, &e) != 0 || e.kind != MOSAIC_INDEX_GAME) continue;
		if (findManual(e.path, e.type == MOSAIC_ENTRY_DIR, e.name, NULL, 0) != MN_NONE) add(e.path, ctx);
	}
}

// The card changed: read the .manuals folders again, and tell the set's
// readers.
static void cardChanged(void) {
	MN_shelfClear(&shelf);
	if (MOSAIC_HOST_HAS(host, set_names)) host->set_changed(SET_NAME);
}

static void onIndex(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	cardChanged();
}

static void postReturn(void) {
	cardChanged();
}

///////////////////////////////////////
// the last page read

static void remember(void) {
	if (!key[0]) return;
	int at = kind == MN_TEXT ? top : page;
	if (MN_memorySet(&memory, key, at) == 1 && MN_memorySave(&memory, memory_file) != 0) {
		say("couldn't save the last pages read");
	}
}

///////////////////////////////////////
// pages

static void freeImages(void) {
	if (fit_image) host->surface_free(ID, fit_image);
	if (zoom_image) host->surface_free(ID, zoom_image);
	fit_image = zoom_image = NULL;
	fit_page = zoom_page = -1;
}

static void pagePath(int at, char* out, size_t size) {
	snprintf(out, size, "%s/%s", where, pages.files[at]);
}

// The page at, shrunk to fit box_w by box_h as it's read, then enlarged
// to fit up_w by up_h when it's smaller. NULL when it can't be read.
static MosaicSurface* loadPage(int at, int box_w, int box_h, int up_w, int up_h) {
	char path[MN_PATH_MAX + MN_NAME_MAX];
	pagePath(at, path, sizeof(path));
	long long started = millis();
	MosaicSurface* image = host->image_load_fit(ID, path, box_w, box_h);
	int w = 0, h = 0;
	if (!image || host->surface_size(image, &w, &h) != 0) return NULL;
	char message[MN_NAME_MAX + 96];
	snprintf(message, sizeof(message), "read %s as %ix%i in %lli ms", pages.files[at], w, h, millis() - started);
	say(message);
	int fw = 0, fh = 0;
	if (MN_fit(w, h, up_w, up_h, &fw, &fh) != 0 || (fw <= w && fh <= h)) return image;
	MosaicSurface* larger = host->surface_scale(ID, image, fw, fh, MOSAIC_FIT_STRETCH);
	host->surface_free(ID, image);
	return larger;
}

static int fitImage(void) {
	if (fit_page == page && fit_image) return 0;
	freeImages();
	int sw = host->screen_width(), sh = host->screen_height();
	fit_image = loadPage(page, sw, sh, sw, sh);
	fit_page = fit_image ? page : -1;
	return fit_image ? 0 : -1;
}

static int zoomImage(void) {
	if (zoom_page == page && zoom_image) return 0;
	if (fitImage() != 0) return -1;
	int fw = 0, fh = 0;
	host->surface_size(fit_image, &fw, &fh);
	int zw = fw * 2 > 4096 ? 4096 : fw * 2, zh = fh * 2 > 4096 ? 4096 : fh * 2;
	if (zoom_image) host->surface_free(ID, zoom_image);
	zoom_image = loadPage(page, zw, zh, zw, zh);
	zoom_page = zoom_image ? page : -1;
	return zoom_image ? 0 : -1;
}

///////////////////////////////////////
// text

static int char_widths[128];

static int charWidth(const char* s, int len, void* ctx) {
	(void)ctx;
	unsigned char c = (unsigned char)s[0];
	if (len == 1 && c < 128) {
		if (char_widths[c] < 0) {
			char one[2] = { (char)c, '\0' };
			int w = host->text_width(MOSAIC_FONT_SMALL, one);
			char_widths[c] = w < 0 ? 0 : w;
		}
		return char_widths[c];
	}
	char one[8];
	if (len >= (int)sizeof(one)) return 0;
	memcpy(one, s, len);
	one[len] = '\0';
	int w = host->text_width(MOSAIC_FONT_SMALL, one);
	return w < 0 ? 0 : w;
}

// Where the text goes: below the header, above the hints.
static MNRect textArea(const MosaicScreenView* view) {
	int scale = host->screen_scale();
	int padding = (MOSAIC_HOST_HAS(host, ui_size) ? host->ui_size(MOSAIC_UI_PADDING) : 10) * scale;
	int pill = (MOSAIC_HOST_HAS(host, ui_size) ? host->ui_size(MOSAIC_UI_PILL) : 30) * scale;
	int y0 = view->status.y + view->status.h + padding / 2;
	if (view->status.h <= 0) y0 = padding;
	int y1 = host->screen_height() - padding - pill - padding / 2;
	MNRect area = { padding, y0, host->screen_width() - 2 * padding, y1 - y0 };
	return area;
}

static void wrapText(int width) {
	if (width == wrapped_width) return;
	wrapped_width = width;
	MN_wrap(&text, width, charWidth, NULL);
}

///////////////////////////////////////
// drawing

static void hints(MosaicSurface* screen) {
	MosaicHint left[2] = { { MOSAIC_BUTTON_B, tr("BACK") } };
	int left_count = 1;
	if (kind == MN_PAGES && pages.count > 0) left[left_count++] = (MosaicHint){ MOSAIC_BUTTON_A, zoomed ? tr("FIT") : tr("ZOOM") };
	host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	int many = kind == MN_PAGES ? pages.count > 1 : text.line_count > rows;
	if (!many) return;
	MosaicHint right[2] = { { MOSAIC_BUTTON_L1, tr("PREV") }, { MOSAIC_BUTTON_R1, tr("NEXT") } };
	host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, 2, NULL);
}

// the page number, or how far through the text, in a pill in the top left
static void counter(MosaicSurface* screen, const char* label) {
	int scale = host->screen_scale();
	int padding = (MOSAIC_HOST_HAS(host, ui_size) ? host->ui_size(MOSAIC_UI_PADDING) : 10) * scale;
	int pill = (MOSAIC_HOST_HAS(host, ui_size) ? host->ui_size(MOSAIC_UI_PILL) : 30) * scale;
	int inner = 10 * scale;
	int w = host->text_width(MOSAIC_FONT_SMALL, label);
	if (w <= 0) return;
	int h = host->text_height(MOSAIC_FONT_SMALL);
	MosaicRect rect = { padding, padding, w + 2 * inner, pill };
	host->fill_round_rect(screen, &rect, rect.h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	host->draw_text(screen, MOSAIC_FONT_SMALL, label, rect.x + inner, rect.y + (rect.h - h + 1) / 2,
		themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF), 0);
}

static void centred(MosaicSurface* screen, const char* message) {
	int w = host->text_width(MOSAIC_FONT_LARGE, message);
	int h = host->text_height(MOSAIC_FONT_LARGE);
	if (w < 0 || h < 0) return;
	int sw = host->screen_width(), sh = host->screen_height();
	host->draw_text(screen, MOSAIC_FONT_LARGE, message, (sw - w) / 2 > 0 ? (sw - w) / 2 : 0, (sh - h) / 2,
		themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF), sw);
}

static MNRect toMN(MosaicRect r) {
	return (MNRect){ r.x, r.y, r.w, r.h };
}

// The status pill and header widgets, which the screen leaves clear.
static MNRect holeOf(const MosaicScreenView* view) {
	MNRect status = toMN(view->status), header = toMN(view->header);
	return MN_hole(&status, &header);
}

// Fills the screen around the status pill and header.
static void fillAround(MosaicSurface* screen, const MosaicScreenView* view, MosaicColor color) {
	MNRect all = { 0, 0, host->screen_width(), host->screen_height() }, hole = holeOf(view), parts[4];
	int n = MN_avoid(&all, &hole, parts);
	for (int i = 0; i < n; i++) {
		MosaicRect r = { parts[i].x, parts[i].y, parts[i].w, parts[i].h };
		host->fill_rect(screen, &r, color);
	}
}

// Draws src of image to dst, around the status pill and header.
static void blitAround(MosaicSurface* screen, const MosaicSurface* image, const MNRect* src, const MNRect* dst,
		const MosaicScreenView* view) {
	MNRect hole = holeOf(view), parts[4];
	int n = MN_avoid(dst, &hole, parts);
	for (int i = 0; i < n; i++) {
		MosaicRect to = { parts[i].x, parts[i].y, parts[i].w, parts[i].h };
		MosaicRect from = { src->x + to.x - dst->x, src->y + to.y - dst->y, to.w, to.h };
		host->blit(screen, image, &from, &to);
	}
}

static void logShown(const char* line) {
	if (!strcmp(line, shown)) return;
	snprintf(shown, sizeof(shown), "%s", line);
	say(line);
}

static void renderPages(MosaicSurface* screen, const MosaicScreenView* view) {
	fillAround(screen, view, mosaic_color(0x000000, 255));
	char line[sizeof(shown)];
	if (pages.count <= 0) {
		centred(screen, tr("This manual has no pages"));
		logShown("no pages");
		return;
	}
	MNRect all = { 0, 0, host->screen_width(), host->screen_height() };
	int got = failed_page == page ? -1 : zoomed ? zoomImage() : fitImage();
	if (got != 0) failed_page = page;
	const MosaicSurface* image = zoomed ? zoom_image : fit_image;
	char number[32];
	snprintf(number, sizeof(number), "%i / %i", page + 1, pages.count);
	if (got != 0 || !image) {
		centred(screen, tr("Can't show this page"));
		snprintf(line, sizeof(line), "page %i of %i: %s, can't show it", page + 1, pages.count, pages.files[page]);
	} else {
		int w = 0, h = 0;
		host->surface_size(image, &w, &h);
		MN_panClamp(&all, w, h, &pan_x, &pan_y);
		MNRect src, dst;
		MN_place(&all, w, h, pan_x, pan_y, &src, &dst);
		blitAround(screen, image, &src, &dst, view);
		if (zoomed) {
			snprintf(line, sizeof(line), "page %i of %i: %s, zoomed to %ix%i at %i,%i", page + 1, pages.count,
				pages.files[page], w, h, pan_x, pan_y);
		} else {
			snprintf(line, sizeof(line), "page %i of %i: %s, fitted to %ix%i", page + 1, pages.count, pages.files[page], w, h);
		}
	}
	counter(screen, number);
	logShown(line);
}

static void renderText(MosaicSurface* screen, const MosaicScreenView* view) {
	fillAround(screen, view, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
	MNRect area = textArea(view);
	wrapText(area.w);
	int line_h = host->text_height(MOSAIC_FONT_SMALL);
	rows = line_h > 0 ? area.h / line_h : 1;
	if (rows < 1) rows = 1;
	top = MN_scroll(top, 0, text.line_count, rows);
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	for (int i = 0; i < rows && top + i < text.line_count; i++) {
		const MNLine* l = &text.lines[top + i];
		if (l->len <= 0) continue;
		char buf[2048];
		int n = l->len < (int)sizeof(buf) - 1 ? l->len : (int)sizeof(buf) - 1;
		memcpy(buf, text.text + l->start, n);
		buf[n] = '\0';
		host->draw_text(screen, MOSAIC_FONT_SMALL, buf, area.x, area.y + i * line_h, ink, area.w);
	}
	char number[48];
	int percent = text.line_count > rows ? (int)((long long)top * 100 / MN_lastTop(text.line_count, rows)) : 100;
	snprintf(number, sizeof(number), "%i%%", percent);
	if (text.line_count > rows) counter(screen, number);
	char line[sizeof(shown)];
	snprintf(line, sizeof(line), "text %s: lines %i to %i of %i", title, top + 1,
		top + rows < text.line_count ? top + rows : text.line_count, text.line_count);
	logShown(line);
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	shown[0] = '\0';
	zoomed = 0;
	pan_x = pan_y = 0;
	failed_page = -1;
	wrapped_width = 0;
	// measured again, as the theme's font may have changed
	for (int i = 0; i < 128; i++) char_widths[i] = -1;
	if (!memory_loaded) {
		MN_memoryLoad(&memory, memory_file);
		memory_loaded = 1;
	}
	int at = MN_memoryGet(&memory, key);
	char message[MN_PATH_MAX + 64];
	if (kind == MN_PAGES) {
		int count = MN_pagesScan(where, &pages);
		page = count > 0 && at < count ? at : 0;
		snprintf(message, sizeof(message), "%i pages in %s, from page %i", count < 0 ? 0 : count, where, page + 1);
	} else if (kind == MN_TEXT) {
		if (MN_textLoad(where, &text) != 0) {
			say("can't read the text manual");
			return 1;
		}
		top = at;
		snprintf(message, sizeof(message), "text %s, %i bytes%s, from line %i", where, text.size,
			text.truncated ? " (cut short)" : "", top + 1);
	} else {
		return 1;
	}
	say(message);
	return 0;
}

static void leave(void* data) {
	(void)data;
	remember();
	freeImages();
	MN_pagesFree(&pages);
	MN_textFree(&text);
	kind = MN_NONE;
	key[0] = '\0';
}

static int turnPage(int delta) {
	int next = page + delta;
	if (next < 0 || next >= pages.count) return 0;
	page = next;
	pan_x = pan_y = 0;
	return 1;
}

static int pagesInput(const MosaicInput* in) {
	unsigned int bit_a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A);
	int changed = 0;
	if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_L1)) changed = turnPage(-1);
	else if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_R1)) changed = turnPage(1);
	else if ((in->pressed & bit_a) && pages.count > 0) {
		int w0 = 0, h0 = 0, w1 = 0, h1 = 0;
		const MosaicSurface* before = zoomed ? zoom_image : fit_image;
		if (before) host->surface_size(before, &w0, &h0);
		zoomed = !zoomed;
		// a page that won't read at twice the size stays fitted
		if (zoomed && zoomImage() != 0) zoomed = 0;
		const MosaicSurface* after = zoomed ? zoom_image : fit_image;
		if (after && before) {
			host->surface_size(after, &w1, &h1);
			MNRect all = { 0, 0, host->screen_width(), host->screen_height() };
			MN_panZoom(&all, w0, h0, pan_x, pan_y, w1, h1, &pan_x, &pan_y);
		}
		changed = 1;
	} else if (!zoomed) {
		if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT)) changed = turnPage(-1);
		else if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT)) changed = turnPage(1);
	} else {
		// a sixth of the screen a step
		int sx = host->screen_width() / 6, sy = host->screen_height() / 6;
		int x = pan_x, y = pan_y;
		if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT)) x -= sx;
		if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT)) x += sx;
		if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) y -= sy;
		if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) y += sy;
		if (zoom_image) {
			int w = 0, h = 0;
			host->surface_size(zoom_image, &w, &h);
			MNRect all = { 0, 0, host->screen_width(), host->screen_height() };
			MN_panClamp(&all, w, h, &x, &y);
		}
		changed = x != pan_x || y != pan_y;
		pan_x = x;
		pan_y = y;
	}
	return changed;
}

static int textInput(const MosaicInput* in) {
	int before = top, screen_rows = rows > 1 ? rows - 1 : 1;
	if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) top = MN_scroll(top, -1, text.line_count, rows);
	else if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) top = MN_scroll(top, 1, text.line_count, rows);
	else if (in->repeated & (MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_L1) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_LEFT))) {
		top = MN_scroll(top, -screen_rows, text.line_count, rows);
	} else if (in->repeated & (MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_R1) | MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_RIGHT))) {
		top = MN_scroll(top, screen_rows, text.line_count, rows);
	}
	return top != before;
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	int changed = kind == MN_PAGES ? pagesInput(in) : textInput(in);
	if (changed) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	if (kind == MN_PAGES) renderPages(screen, view);
	else renderText(screen, view);
	hints(screen);
	return 0;
}

///////////////////////////////////////
// the action

static int canOpen(const char* path, void* data) {
	(void)data;
	return manualOf(path, NULL, 0) != MN_NONE;
}

static void openManual(const char* path, void* data) {
	(void)data;
	kind = manualOf(path, where, sizeof(where));
	if (kind == MN_NONE) return;
	char message[2 * MN_PATH_MAX + 32];
	snprintf(message, sizeof(message), "manual for %s: %s", path, where);
	say(message);
	MN_memoryKey(host->sdcard_path, path, key, sizeof(key));
	const char* slash = strrchr(where, '/');
	if (snprintf(title, sizeof(title), "%s", slash ? slash + 1 : where) >= (int)sizeof(title)) title[0] = '\0';
	if (host->open_screen(SCREEN_ID) != 0) {
		say("couldn't open the manual");
		kind = MN_NONE;
		key[0] = '\0';
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, image_load_fit) || !MOSAIC_HOST_HAS(host, open_screen) || !MOSAIC_HOST_HAS(host, draw_hints)
			|| !MOSAIC_HOST_HAS(host, run_action)) {
		say("this launcher can't show manuals");
		return 1;
	}
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	snprintf(memory_file, sizeof(memory_file), "%s/launcher/manuals.txt", host->userdata_path);
	MN_shelfInit(&shelf);
	MN_memoryInit(&memory);
	memory_loaded = 0;
	memset(&pages, 0, sizeof(pages));
	memset(&text, 0, sizeof(text));
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the manual screen");
		return 1;
	}
	if (host->register_action(ACTION_ID, "READ", MOSAIC_ACTION_PATH, canOpen, openManual, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	if (host->bind_button(MOSAIC_SCREEN_GAMELIST, MOSAIC_BUTTON_Y, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
		say("couldn't put the manual on Y");
	}
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* name = host->cfg_get("button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
			say("couldn't bind the manual button");
		}
	}
	if (MOSAIC_HOST_HAS(host, set_names)) {
		if (host->set_register(SET_NAME, setHas, setList) != 0) say("couldn't register the manual set");
		if (MOSAIC_HOST_HAS(host, index_refresh)) host->subscribe("launcher.index_ready", onIndex, NULL);
	}
	return 0;
}

static void quit(void) {
	freeImages();
	MN_pagesFree(&pages);
	MN_textFree(&text);
	MN_shelfClear(&shelf);
	MN_memoryFree(&memory);
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
