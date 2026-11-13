// Cartridge shelf layout: owns the layout slot and draws the game list as
// cartridges standing on shelves, as many shelves as its setting asks for
// or fit the screen, each one's cartridges side by side on a wooden plank.
// The setting at theme, the default, takes the theme slot owner's
// layout-shelf.shelves key, or fits the screen without one, read once the
// theme is in place and again on launcher.theme_changed.
//
// Each entry's cartridge takes the shape of the system it's in (see
// LS_shapeOf): a Game Boy cartridge, nearly square with a ridged grip
// along its top, for GB, GBC and SGB folders and .gb, .gbc and .sgb
// games; a wider, landscape Game Boy Advance cartridge with a dark grip
// band for GBA and MGBA folders and .gba games; and for every other
// system, and folders, paks and toggles, a game case standing face out
// with its spine on the left. The columns are sized for the folder's
// shape, the one its system tag names, or else the one all of its first
// entries share, or else the case; a cartridge of another shape is fitted
// inside its column.
//
// The label shows the entry's box from the launcher's art cache, or else
// its thumbnail, filling the label, and while there's none (or it's still
// loading) the first letter of its name on a label of the theme's accent
// colour; art shows only while Show game art is on. Names show on tags
// along the plank under each cartridge when the launcher shows names, the
// selected one's on a pill in the theme's main colour, unless a plugin
// hid it and its art stands in for it. The selected cartridge stands
// lifted off its shelf, edged in the theme's main colour; with the animate
// setting on, it rises over LIFT_MS as the one before drops back,
// redrawing from tick while it runs.
//
// The d-pad moves the selection along and between the shelves with
// select_index (see LS_move for where it wraps); everything else, A, B, X,
// the shoulders and the letter jumps, stays the launcher's. The shelves
// in view follow the selection, moving only as far as it takes to show
// it.
//
// Art: art_get's images are only good for the frame they're asked in, so
// they're never kept. Each one is copied once with surface_scale to its
// label's size, and the copies, which are the plugin's own surfaces, are
// kept across frames: up to two screens of labels, the least recently
// drawn going first. A copy that can't be made (the plugin's surfaces are
// full) is drawn straight from the cache's image that frame instead.
//
// A plugin's art_replace surface, such as a video preview, stands in for
// the selected cartridge's label art, filling the label the same way. It's
// asked for with art_replacement each frame and drawn straight from the
// plugin's surface, never copied, since each of its frames is new.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "shelf_model.h"

#define ID "layout-shelf"
#define MAX_PATH_LEN 512
#define LIFT_MS 120
#define CACHE_MAX (2 * LS_MAX_COLUMNS * LS_MAX_SHELVES)
// entries looked at for a folder's shape when its path has no system tag
#define SHAPE_SAMPLE 16

static const MosaicHost* host;
static int shelves_setting = 0; // 0 for auto
static char shelves_value[16]; // the setting as set, theme for the theme's
static int themed = 0; // 0 until the theme's key is read, and again when the theme changes
static int lift_setting = 1;
static unsigned long now_ms = 0; // the last tick's

// the folder last drawn, and what's kept for it
static char folder_path[MAX_PATH_LEN];
static int folder_depth = -1;
static int folder_count = -1;
static int folder_shape = LS_SHAPE_CASE;
static int first_shelf = 0; // the first shelf in view

// the lift
static int lifted = -1;   // the entry standing lifted, the selected one
static int dropping = -1; // the one before, dropping back while the lift runs
static int lifting = 0;   // 1 while the lift runs
static unsigned long lift_start = 0;

///////////////////////////////////////
// scaled art, kept across frames

typedef struct Scaled {
	char path[MAX_PATH_LEN]; // the entry's
	int w, h;
	MosaicSurface* surface; // NULL for a free slot
	unsigned long used;
} Scaled;

static Scaled cache[CACHE_MAX];
static unsigned long frame = 0;

static void cacheFree(Scaled* s) {
	if (s->surface) host->surface_free(ID, s->surface);
	memset(s, 0, sizeof(*s));
}

static void cacheClear(void) {
	for (int i = 0; i < CACHE_MAX; i++) cacheFree(&cache[i]);
}

static Scaled* cacheFind(const char* path, int w, int h) {
	for (int i = 0; i < CACHE_MAX; i++) {
		Scaled* s = &cache[i];
		if (s->surface && s->w == w && s->h == h && !strcmp(s->path, path)) return s;
	}
	return NULL;
}

// a slot for a new copy, freeing the least recently drawn when limit are in use
static Scaled* cacheSlot(int limit) {
	if (limit > CACHE_MAX) limit = CACHE_MAX;
	Scaled* free_slot = NULL;
	Scaled* oldest = NULL;
	int in_use = 0;
	for (int i = 0; i < CACHE_MAX; i++) {
		Scaled* s = &cache[i];
		if (!s->surface) {
			if (!free_slot) free_slot = s;
			continue;
		}
		in_use++;
		if (!oldest || s->used < oldest->used) oldest = s;
	}
	if (free_slot && in_use < limit) return free_slot;
	if (oldest) cacheFree(oldest);
	return oldest ? oldest : free_slot;
}

// the part of a w by h image that fills to, keeping its shape and cutting
// its edges
static MosaicRect coverSource(const MosaicRect* to, int w, int h) {
	MosaicRect r = { 0, 0, w, h };
	if (w <= 0 || h <= 0 || to->w <= 0 || to->h <= 0) return r;
	if ((long)w * to->h > (long)h * to->w) {
		r.w = (int)((long)h * to->w / to->h);
		r.x = (w - r.w) / 2;
	} else {
		r.h = (int)((long)w * to->h / to->w);
		r.y = (h - r.h) / 2;
	}
	return r;
}

// Draws the plugin's art_replace surface for the entry at index filling
// rect. Returns 1 when it drew, or 0 when there's none.
static int drawReplacement(MosaicSurface* screen, int index, const MosaicRect* rect) {
	const MosaicSurface* image = NULL;
	int w = 0, h = 0;
	if (!MOSAIC_HOST_HAS(host, art_replacement) || host->art_replacement(index, &image) != 1 || !image) return 0;
	if (host->surface_size(image, &w, &h) != 0) return 0;
	MosaicRect from = coverSource(rect, w, h);
	return host->blit(screen, image, &from, rect) == 0;
}

// Draws entry's art filling rect: its box, or else its thumbnail. Returns
// 1 when it drew, or 0 when there's none yet.
static int drawArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* rect, int limit) {
	const char* path = host->entry_path(entry);
	if (!path || rect->w <= 0 || rect->h <= 0) return 0;
	Scaled* hit = cacheFind(path, rect->w, rect->h);
	if (hit) {
		hit->used = frame;
		return host->blit(screen, hit->surface, NULL, rect) == 0;
	}

	const MosaicSurface* image = NULL;
	int state = host->art_get(entry, MOSAIC_ART_BOX, &image);
	if (state == MOSAIC_ART_MISSING) state = host->art_get(entry, MOSAIC_ART_THUMBNAIL, &image);
	if (state != MOSAIC_ART_READY || !image) return 0;

	if (strlen(path) < MAX_PATH_LEN) {
		MosaicSurface* copy = host->surface_scale(ID, image, rect->w, rect->h, MOSAIC_FIT_COVER);
		if (copy) {
			Scaled* s = cacheSlot(limit);
			if (s) {
				snprintf(s->path, sizeof(s->path), "%s", path);
				s->w = rect->w;
				s->h = rect->h;
				s->surface = copy;
				s->used = frame;
				return host->blit(screen, copy, NULL, rect) == 0;
			}
			host->surface_free(ID, copy);
		}
	}
	// no copy: this frame straight from the cache's image
	int w = 0, h = 0;
	if (host->surface_size(image, &w, &h) != 0) return 0;
	MosaicRect from = coverSource(rect, w, h);
	return host->blit(screen, image, &from, rect) == 0;
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static MosaicColor rgb(unsigned int value) {
	return mosaic_color(value, 255);
}

static int nameHeight(const MosaicListView* view, int scale) {
	return view->show_names ? host->text_height(MOSAIC_FONT_SMALL) + 4 * scale : 0;
}

// the folder's shape, worked out once each time it's listed
static void followFolder(const MosaicListView* view) {
	const char* path = view->path ? view->path : "";
	if (view->depth == folder_depth && view->count == folder_count && !strcmp(path, folder_path)) return;
	int changed = view->depth != folder_depth || strcmp(path, folder_path) != 0;
	folder_depth = view->depth;
	folder_count = view->count;
	snprintf(folder_path, sizeof(folder_path), "%s", path);
	if (changed) {
		first_shelf = 0;
		lifted = view->selected;
		dropping = -1;
		lifting = 0;
	}

	char tag[32];
	LS_systemTag(path, 1, tag, sizeof(tag));
	if (tag[0]) {
		folder_shape = LS_shapeForTag(tag);
		return;
	}
	int shapes[SHAPE_SAMPLE];
	int n = 0;
	for (int i = 0; i < view->count && n < SHAPE_SAMPLE; i++) {
		const MosaicEntry* entry = host->list_get(view->list, i);
		if (entry) shapes[n++] = LS_shapeOf(host->entry_path(entry), host->entry_type(entry) == MOSAIC_ENTRY_DIR);
	}
	folder_shape = LS_commonShape(shapes, n);
}

// the shelves setting, or at theme the theme's layout-shelf.shelves
static void resolveSettings(void) {
	const char* theme = MOSAIC_HOST_HAS(host, theme_value) ? host->theme_value(ID ".shelves", NULL) : NULL;
	shelves_setting = LS_count(LS_themed(shelves_value, theme), 1, LS_MAX_SHELVES);
	themed = 1;
}

// the theme's keys may have changed
static void themeChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	themed = 0;
}

static int layoutShelves(const MosaicListView* view, LSShelves* shelves) {
	// the theme is put in place after every plugin's init
	if (!themed) resolveSettings();
	int scale = host->screen_scale();
	MosaicRect header = MOSAIC_VIEW_HAS(view, header) ? view->header : (MosaicRect){ 0, 0, 0, 0 };
	return LS_layout(&view->area, &view->status, &header, scale, shelves_setting, nameHeight(view, scale),
		folder_shape, 24 * scale, shelves);
}

// logs the shelves each time their shape changes, such as when names hide
static void logShelves(const LSShelves* s) {
	static LSShelves logged;
	if (logged.shelves == s->shelves && logged.cols == s->cols && logged.shape == s->shape &&
		logged.cart_h == s->cart_h && logged.col_w == s->col_w) {
		return;
	}
	logged = *s;
	MosaicRect cart = LS_cart(s, 0, s->shape, 0);
	char message[128];
	snprintf(message, sizeof(message), "%i %s of %i %s, %i by %i", s->shelves, s->shelves == 1 ? "shelf" : "shelves",
		s->cols, LS_shapeName(s->shape), cart.w, cart.h);
	host->log(ID, message);
}

static MosaicRect inflate(const MosaicRect* r, int by) {
	return (MosaicRect){ r->x - by, r->y - by, r->w + 2 * by, r->h + 2 * by };
}

static MosaicRect around(const MosaicRect* a, const MosaicRect* b) {
	if (b->w <= 0 || b->h <= 0) return *a;
	int x0 = a->x < b->x ? a->x : b->x, y0 = a->y < b->y ? a->y : b->y;
	int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
	int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
}

static void drawPlank(MosaicSurface* screen, const MosaicRect* plank, int scale) {
	host->fill_rect(screen, plank, rgb(0x7A5230));
	MosaicRect edge = { plank->x, plank->y, plank->w, scale };
	host->fill_rect(screen, &edge, rgb(0xA8784A));
	edge.y = plank->y + plank->h - scale;
	host->fill_rect(screen, &edge, rgb(0x553820));
}

// the cartridge's body, without its label
static void drawBody(MosaicSurface* screen, int shape, const MosaicRect* r, const MosaicRect* label, int scale) {
	switch (shape) {
	case LS_SHAPE_GB: {
		host->fill_round_rect(screen, r, r->w / 14, rgb(0xB4B4BC));
		// the grip's ridges across its top
		int line = r->h / 80 > 0 ? r->h / 80 : 1;
		for (int i = 0; i < 3; i++) {
			MosaicRect ridge = { r->x + r->w * 8 / 100, r->y + r->h * (4 + 5 * i) / 100, r->w * 84 / 100, line };
			host->fill_rect(screen, &ridge, rgb(0x8E8E98));
		}
		break;
	}
	case LS_SHAPE_GBA: {
		int radius = r->h / 8;
		host->fill_round_rect(screen, r, radius, rgb(0x5A5A66));
		MosaicRect grip = { r->x, r->y, r->w, r->h * 22 / 100 };
		host->fill_round_rect(screen, &grip, radius, rgb(0x3E3E48));
		MosaicRect square = { r->x, r->y + grip.h / 2, r->w, grip.h - grip.h / 2 };
		host->fill_rect(screen, &square, rgb(0x3E3E48));
		break;
	}
	default: {
		host->fill_round_rect(screen, r, r->w / 24, rgb(0x26262C));
		MosaicRect spine = { r->x + r->w * 2 / 100, r->y + r->h * 2 / 100, r->w * 6 / 100, r->h * 96 / 100 };
		host->fill_rect(screen, &spine, rgb(0x44444E));
		return;
	}
	}
	// the label's recess
	MosaicRect recess = inflate(label, scale);
	host->fill_rect(screen, &recess, rgb(0x2E2E36));
}

// text centred in rect, truncated to it, on a pill of pill_color when
// pill is 1; writes where it went to drawn
static void drawTag(MosaicSurface* screen, const char* text, const MosaicRect* rect, MosaicColor color, int pill,
		MosaicColor pill_color, MosaicRect* drawn) {
	int scale = host->screen_scale();
	int pad = pill ? 4 * scale : 0;
	char shown[256];
	int w = host->text_truncate(MOSAIC_FONT_SMALL, text, rect->w - 2 * pad, shown, sizeof(shown));
	int h = host->text_height(MOSAIC_FONT_SMALL);
	*drawn = (MosaicRect){ 0, 0, 0, 0 };
	if (w < 0 || h < 0) return;
	MosaicRect box = { rect->x + (rect->w - w) / 2 - pad, rect->y, w + 2 * pad, rect->h };
	if (pill) host->fill_round_rect(screen, &box, box.h / 2, pill_color);
	host->draw_text(screen, MOSAIC_FONT_SMALL, shown, box.x + pad, rect->y + (rect->h - h) / 2, color,
		rect->w - 2 * pad);
	*drawn = box;
}

// the letter label for an entry with no art
static void drawLetter(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* label, int scale) {
	host->fill_round_rect(screen, label, 2 * scale, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	char letter[8];
	LS_letter(host->entry_name(entry), letter, sizeof(letter));
	if (!letter[0]) return;
	char shown[16];
	int w = host->text_truncate(MOSAIC_FONT_LARGE, letter, label->w, shown, sizeof(shown));
	int h = host->text_height(MOSAIC_FONT_LARGE);
	if (w < 0 || h < 0) return;
	host->draw_text(screen, MOSAIC_FONT_LARGE, shown, label->x + (label->w - w) / 2, label->y + (label->h - h) / 2,
		themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF), label->w);
}

// how far the lift has got, from 0 to LS_UNIT
static int liftProgress(void) {
	if (!lifting) return LS_UNIT;
	int done = LS_ease(now_ms - lift_start, LIFT_MS);
	if (done >= LS_UNIT) {
		lifting = 0;
		dropping = -1;
	}
	return done;
}

// follows the selection: a move starts a lift from where it was
static void followSelection(const MosaicListView* view) {
	if (view->selected == lifted) return;
	dropping = lifting ? -1 : lifted;
	lifted = view->selected;
	lifting = lift_setting;
	lift_start = now_ms;
}

static int hiddenName(const MosaicListView* view, int index) {
	return MOSAIC_VIEW_HAS(view, names_hidden) && view->names_hidden && index >= 0 && index < view->count &&
		view->names_hidden[index];
}

static int layoutInit(const MosaicListView* view) {
	followFolder(view);
	LSShelves shelves;
	if (layoutShelves(view, &shelves) != 0) {
		host->log(ID, "the screen is too small for shelves, using the built-in list");
		return 1;
	}
	logShelves(&shelves);
	return 0;
}

static int layoutInput(const MosaicInput* input, const MosaicListView* view, unsigned int* consumed) {
	static const int dirs[] = { MOSAIC_BUTTON_UP, MOSAIC_BUTTON_DOWN, MOSAIC_BUTTON_LEFT, MOSAIC_BUTTON_RIGHT };
	for (int i = 0; i < 4; i++) *consumed |= MOSAIC_BUTTON_BIT(dirs[i]);

	followFolder(view);
	LSShelves shelves;
	if (layoutShelves(view, &shelves) != 0) return -1;
	for (int i = 0; i < 4; i++) {
		unsigned int bit = MOSAIC_BUTTON_BIT(dirs[i]);
		if (!(input->repeated & bit)) continue;
		int to = LS_move(view->selected, view->count, shelves.cols, dirs[i], (input->pressed & bit) != 0);
		if (to != view->selected) host->select_index(to);
		break; // one direction a frame
	}
	return 0;
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	followFolder(view);
	LSShelves s;
	if (layoutShelves(view, &s) != 0) return -1;
	logShelves(&s);
	frame++;
	followSelection(view);
	int progress = liftProgress();
	first_shelf = LS_window(first_shelf, view->selected, view->count, s.cols, s.shelves);

	int scale = host->screen_scale();
	int name_h = nameHeight(view, scale);
	MosaicColor main_color = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	MosaicColor text = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor text_selected = themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000);
	int limit = 2 * s.cols * s.shelves;

	for (int shelf = 0; shelf < s.shelves; shelf++) {
		int first = (first_shelf + shelf) * s.cols;
		if (first >= view->count) break;
		MosaicRect plank = LS_plank(&s, shelf);
		drawPlank(screen, &plank, scale);
		for (int col = 0; col < s.cols; col++) {
			int index = first + col;
			if (index >= view->count) break;
			const MosaicEntry* entry = host->list_get(view->list, index);
			if (!entry) continue;
			int slot = shelf * s.cols + col;
			int selected = index == view->selected;
			int shape = LS_shapeOf(host->entry_path(entry), host->entry_type(entry) == MOSAIC_ENTRY_DIR);
			int up = selected ? progress : index == dropping ? LS_UNIT - progress : 0;
			MosaicRect cart = LS_cart(&s, slot, shape, up);
			MosaicRect label = LS_label(shape, &cart);

			if (selected) {
				MosaicRect edge = inflate(&cart, scale);
				host->fill_round_rect(screen, &edge, cart.w / 14 + scale, main_color);
			}
			drawBody(screen, shape, &cart, &label, scale);
			int drew = view->show_art && (drawReplacement(screen, index, &label) || drawArt(screen, entry, &label, limit));
			if (!drew) drawLetter(screen, entry, &label, scale);

			MosaicRect name = { 0, 0, 0, 0 };
			MosaicRect tag = LS_tag(&s, slot, name_h);
			if (tag.h > 0 && !(drew && hiddenName(view, index))) {
				drawTag(screen, host->entry_name(entry), &tag, selected ? text_selected : text, selected, main_color,
					&name);
			}
			MosaicRect cell = around(&cart, &name);
			host->decorate_cell(screen, index, &cell, name.w > 0 ? &name : NULL);
		}
	}
	return 0;
}

static void tick(unsigned long now) {
	now_ms = now;
	if (!lifting) return;
	host->request_redraw();
	// the game list isn't being drawn, such as under the quick menu: stop
	// asking, and the selection is lifted when it's back
	if (now - lift_start > 2 * LIFT_MS) {
		lifting = 0;
		dropping = -1;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, select_index) || !MOSAIC_HOST_HAS(host, art_get) ||
		!MOSAIC_HOST_HAS(host, decorate_cell)) {
		host->log(ID, "this launcher can't draw a layout");
		return 1;
	}
	snprintf(shelves_value, sizeof(shelves_value), "%s", host->cfg_get("shelves", "theme"));
	themed = 0;
	lift_setting = strcmp(host->cfg_get("animate", "on"), "off") != 0;
	if (MOSAIC_HOST_HAS(host, subscribe) && host->subscribe("launcher.theme_changed", themeChanged, NULL) != 0) {
		host->log(ID, "couldn't follow theme changes");
	}
	return 0;
}

static void quit(void) {
	cacheClear();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.layout_init = layoutInit,
	.layout_input = layoutInput,
	.layout_render = layoutRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
