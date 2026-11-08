// Carousel layout: owns the layout slot and draws the game list as a row
// of covers, the selected one large in the middle with its name under it,
// and its neighbours smaller to either side, as many as fit (see
// LC_strip).
//
// Each cover shows the entry's box from the launcher's art cache, or else
// its thumbnail, fitted inside the cover, and while there's none (or it's
// still loading) the first letter of its name on a card of the theme's
// accent colour. The selected entry's name shows on a pill in the theme's
// main colour when the launcher shows names, unless a plugin hid it and
// its art stands in for it; art shows only while Show game art is on.
//
// Left and right move the selection by one and up and down by a page, as
// many covers as are in view, with select_index (see LC_move for where it
// stops and wraps); everything else, A, B, X, the shoulders and the letter
// jumps, stays the launcher's. The selected cover is always the middle
// one. With the slide setting on, a move of a cover or two slides the
// covers into their new places over SLIDE_MS, redrawing from tick while
// it runs; longer jumps, such as a page, a wrap or a letter, don't slide.
//
// Art: art_get's images are only good for the frame they're asked in, so
// they're never kept. Each one is copied once with surface_scale to its
// cover's size, and the copies, which are the plugin's own surfaces, are
// kept across frames: up to three for each cover in view, the least
// recently drawn going first. During a slide the copies are stretched to
// the covers' sizes along the way rather than copied again. A copy that
// can't be made (the plugin's surfaces are full) is drawn straight from the
// cache's image that frame instead.

#include <stdio.h>
#include <string.h>
#include "carousel_model.h"
#include "mosaic_plugin.h"

#define ID "layout-carousel"
#define MAX_PATH_LEN 512
#define SLIDE_MS 150
#define CACHE_MAX (3 * (2 * LC_MAX_SIDES + 1))

static const MosaicHost* host;
static int slide_setting = 1;
static unsigned long now_ms = 0; // the last tick's
static int last_selected = -1;   // the selection the covers were last drawn around
static char last_path[MAX_PATH_LEN]; // the folder last_selected is in
static int last_depth = -1;
static int slide_from = 0; // where the slide started, in LC_UNIT steps; 0 for none
static unsigned long slide_start = 0;

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

// the copy of path at w by h, or with exact 0, the closest in size to it,
// preferring a bigger one
static Scaled* cacheFind(const char* path, int w, int h, int exact) {
	Scaled* best = NULL;
	for (int i = 0; i < CACHE_MAX; i++) {
		Scaled* s = &cache[i];
		if (!s->surface || strcmp(s->path, path) != 0) continue;
		if (s->w == w && s->h == h) return s;
		if (exact) continue;
		if (!best) {
			best = s;
			continue;
		}
		int big = s->w >= w, best_big = best->w >= w;
		if (big != best_big ? big : (big ? s->w < best->w : s->w > best->w)) best = s;
	}
	return best;
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

// what's left of to after fitting a w by h image inside it, keeping its shape
static MosaicRect contain(const MosaicRect* to, int w, int h) {
	if (w <= 0 || h <= 0) return *to;
	MosaicRect r = *to;
	if ((long)to->w * h > (long)to->h * w) {
		r.w = (int)((long)to->h * w / h);
		r.x += (to->w - r.w) / 2;
	} else {
		r.h = (int)((long)to->w * h / w);
		r.y += (to->h - r.h) / 2;
	}
	return r;
}

// Draws entry's art in rect: its box, or else its thumbnail. At rest (copy
// is 1) it keeps a copy at rect's size; partway through a slide it
// stretches the closest copy it has. Returns 1 when it drew, or 0 when
// there's none yet.
static int drawArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* rect, int copy, int limit) {
	const char* path = host->entry_path(entry);
	if (!path || rect->w <= 0 || rect->h <= 0) return 0;
	Scaled* hit = cacheFind(path, rect->w, rect->h, copy);
	if (hit) {
		hit->used = frame;
		return host->blit(screen, hit->surface, NULL, rect) == 0;
	}

	const MosaicSurface* image = NULL;
	int state = host->art_get(entry, MOSAIC_ART_BOX, &image);
	if (state == MOSAIC_ART_MISSING) state = host->art_get(entry, MOSAIC_ART_THUMBNAIL, &image);
	if (state != MOSAIC_ART_READY || !image) return 0;

	if (copy && strlen(path) < MAX_PATH_LEN) {
		MosaicSurface* scaled = host->surface_scale(ID, image, rect->w, rect->h, MOSAIC_FIT_CONTAIN);
		if (scaled) {
			Scaled* s = cacheSlot(limit);
			if (s) {
				snprintf(s->path, sizeof(s->path), "%s", path);
				s->w = rect->w;
				s->h = rect->h;
				s->surface = scaled;
				s->used = frame;
				return host->blit(screen, scaled, NULL, rect) == 0;
			}
			host->surface_free(ID, scaled);
		}
	}
	// no copy: this frame straight from the cache's image
	int w = 0, h = 0;
	if (host->surface_size(image, &w, &h) != 0) return 0;
	MosaicRect to = contain(rect, w, h);
	return host->blit(screen, image, NULL, &to) == 0;
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static int layoutStrip(const MosaicListView* view, LCStrip* strip) {
	int scale = host->screen_scale();
	MosaicRect header = MOSAIC_VIEW_HAS(view, header) ? view->header : (MosaicRect){ 0, 0, 0, 0 };
	int name_h = view->show_names ? host->text_height(MOSAIC_FONT_LARGE) + 4 * scale : 0;
	return LC_strip(&view->area, &view->status, &header, scale, name_h, 16 * scale, strip);
}

// logs the strip each time its shape changes, such as when names hide
static void logStrip(const LCStrip* strip) {
	static LCStrip logged;
	if (logged.sides == strip->sides && logged.size[0] == strip->size[0] && logged.name.h == strip->name.h) return;
	logged = *strip;
	char message[128];
	snprintf(message, sizeof(message), "strip of %i covers, %i by %i", 2 * strip->sides + 1, strip->size[0],
		strip->size[0]);
	host->log(ID, message);
}

static int inside(const MosaicRect* r, const MosaicRect* room) {
	return r->w > 0 && r->h > 0 && r->x >= room->x && r->y >= room->y && r->x + r->w <= room->x + room->w &&
		r->y + r->h <= room->y + room->h;
}

static MosaicRect around(const MosaicRect* a, const MosaicRect* b) {
	if (b->w <= 0 || b->h <= 0) return *a;
	int x0 = a->x < b->x ? a->x : b->x, y0 = a->y < b->y ? a->y : b->y;
	int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
	int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
	return (MosaicRect){ x0, y0, x1 - x0, y1 - y0 };
}

// the selected entry's name on a pill centred in strip's name strip; writes
// where the pill went to drawn
static void drawName(MosaicSurface* screen, const LCStrip* strip, const char* name, MosaicRect* drawn) {
	int scale = host->screen_scale();
	int pad = 6 * scale;
	char shown[256];
	int w = host->text_truncate(MOSAIC_FONT_LARGE, name, strip->name.w - 2 * pad, shown, sizeof(shown));
	int h = host->text_height(MOSAIC_FONT_LARGE);
	*drawn = (MosaicRect){ 0, 0, 0, 0 };
	if (w < 0 || h < 0) return;
	MosaicRect pill = { strip->cx - w / 2 - pad, strip->name.y, w + 2 * pad, strip->name.h };
	if (pill.x < strip->name.x) pill.x = strip->name.x;
	host->fill_round_rect(screen, &pill, pill.h / 2, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF));
	host->draw_text(screen, MOSAIC_FONT_LARGE, shown, pill.x + pad, pill.y + (pill.h - h) / 2,
		themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000), pill.w - 2 * pad);
	*drawn = pill;
}

// the letter card for an entry with no art
static void drawCard(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* rect) {
	int scale = host->screen_scale();
	host->fill_round_rect(screen, rect, 4 * scale, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	char letter[8];
	LC_letter(host->entry_name(entry), letter, sizeof(letter));
	if (!letter[0]) return;
	char shown[16];
	int w = host->text_truncate(MOSAIC_FONT_LARGE, letter, rect->w, shown, sizeof(shown));
	int h = host->text_height(MOSAIC_FONT_LARGE);
	if (w < 0 || h < 0) return;
	host->draw_text(screen, MOSAIC_FONT_LARGE, shown, rect->x + (rect->w - w) / 2, rect->y + (rect->h - h) / 2,
		themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF), rect->w);
}

// where the covers are this frame, in LC_UNIT steps from their places
static int slideShift(void) {
	if (!slide_from) return 0;
	int done = LC_ease(now_ms - slide_start, SLIDE_MS);
	if (done >= LC_UNIT) {
		slide_from = 0;
		return 0;
	}
	return (int)((long)slide_from * (LC_UNIT - done) / LC_UNIT);
}

// follows the selection: a new folder starts at rest, and a move starts a
// slide from where the covers are
static void follow(const MosaicListView* view, const LCStrip* strip) {
	const char* path = view->path ? view->path : "";
	if (view->depth != last_depth || strcmp(path, last_path) != 0 || last_selected < 0 || last_selected >= view->count) {
		last_depth = view->depth;
		snprintf(last_path, sizeof(last_path), "%s", path);
		last_selected = view->selected;
		slide_from = 0;
		return;
	}
	if (view->selected == last_selected) return;
	int shift = slideShift();
	slide_from = slide_setting ? LC_slideFrom(shift, view->selected - last_selected, strip->sides) : 0;
	slide_start = now_ms;
	last_selected = view->selected;
}

static int hiddenName(const MosaicListView* view, int index) {
	return MOSAIC_VIEW_HAS(view, names_hidden) && view->names_hidden && index >= 0 && index < view->count &&
		view->names_hidden[index];
}

static int layoutInit(const MosaicListView* view) {
	LCStrip strip;
	if (layoutStrip(view, &strip) != 0) {
		host->log(ID, "the screen is too small for a carousel, using the built-in list");
		return 1;
	}
	logStrip(&strip);
	return 0;
}

static int layoutInput(const MosaicInput* input, const MosaicListView* view, unsigned int* consumed) {
	static const int dirs[] = { MOSAIC_BUTTON_LEFT, MOSAIC_BUTTON_RIGHT, MOSAIC_BUTTON_UP, MOSAIC_BUTTON_DOWN };
	for (int i = 0; i < 4; i++) *consumed |= MOSAIC_BUTTON_BIT(dirs[i]);

	LCStrip strip;
	if (layoutStrip(view, &strip) != 0) return -1;
	for (int i = 0; i < 4; i++) {
		unsigned int bit = MOSAIC_BUTTON_BIT(dirs[i]);
		if (!(input->repeated & bit)) continue;
		int to = LC_move(view->selected, view->count, dirs[i], (input->pressed & bit) != 0, 2 * strip.sides + 1);
		if (to != view->selected) host->select_index(to);
		break; // one direction a frame
	}
	return 0;
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	LCStrip strip;
	if (layoutStrip(view, &strip) != 0) return -1;
	logStrip(&strip);
	frame++;
	follow(view, &strip);
	int shift = slideShift();
	int limit = 3 * (2 * strip.sides + 1);

	// outside in, so the selected cover and its name go on last; a slide
	// can bring covers from as far as twice the sides in
	for (int d = 2 * strip.sides + 1; d >= 0; d--) {
		for (int side = -1; side <= 1; side += 2) {
			if (d == 0 && side > 0) break;
			int index = view->selected + side * d;
			if (index < 0 || index >= view->count) continue;
			MosaicRect rect = LC_slot(&strip, side * d * LC_UNIT + shift);
			if (!inside(&rect, &strip.room)) continue;
			const MosaicEntry* entry = host->list_get(view->list, index);
			if (!entry) continue;

			int drew = view->show_art && drawArt(screen, entry, &rect, shift == 0, limit);
			if (!drew) drawCard(screen, entry, &rect);
			MosaicRect name = { 0, 0, 0, 0 };
			if (d == 0 && strip.name.h > 0 && !(drew && hiddenName(view, index))) {
				drawName(screen, &strip, host->entry_name(entry), &name);
			}
			MosaicRect cell = around(&rect, &name);
			host->decorate_cell(screen, index, &cell, name.w > 0 ? &name : NULL);
		}
	}
	return 0;
}

static void tick(unsigned long now) {
	now_ms = now;
	if (!slide_from) return;
	host->request_redraw();
	// the game list isn't being drawn, such as under the quick menu: stop
	// asking, and the covers are at rest when it's back
	if (now - slide_start > 2 * SLIDE_MS) slide_from = 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, select_index) || !MOSAIC_HOST_HAS(host, art_get) ||
		!MOSAIC_HOST_HAS(host, decorate_cell)) {
		host->log(ID, "this launcher can't draw a layout");
		return 1;
	}
	slide_setting = strcmp(host->cfg_get(ID, "slide", "on"), "off") != 0;
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
