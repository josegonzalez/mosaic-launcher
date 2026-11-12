// Hero and card rail layout: owns the layout slot and draws the game list
// as a row of cards along the bottom, under the selected entry's art across
// the top with its logo, or else its name, over it.
//
// The hero is the selected entry's backdrop, or else its screenshot,
// thumbnail or box, the first that has a file (as art_find reports them,
// so generated art, which is drawn small, isn't blown up), cover-fitted
// from the area's top to the middle of the cards, tinted toward the
// theme's accent colour by the tint setting and fading into the theme's
// background colour above the cards. The rest of the area below it is that
// colour too, so the cards' names read the same whatever the art. Where
// there's no hero art stock's background stays. The hero is drawn around
// the status pill and header widgets, which stay clear.
//
// The logo is the selected entry's logo art, fitted to the bottom left of
// the hero, or else its name in the large font. It fades in over the fade
// setting once the selection settles (selection_changed), redrawing from
// tick while it fades, and is gone while the selection moves. A hidden
// name (MosaicListView's names_hidden, or show_names off) doesn't show
// over hero art, but does where there's none.
//
// The cards show each entry's thumbnail, or else its box, fitted inside a
// 3:4 card, and while there's none (or it's still loading) a tile in the
// theme's accent colour with the name's first letter, or the whole name
// when no name strip shows. Names show under the cards when the launcher
// shows names, except hidden ones on cards with art. The selected card is
// raised and framed in the theme's main colour. Art only shows while Show
// game art is on.
//
// The d-pad moves the selection with select_index (see HR_move): left and
// right one card, up back a rail's worth and down on one. Everything else
// stays the launcher's. The rail moves only as far as it takes to keep the
// selected card in view with one either side.
//
// Art: art_get's images are only good for the frame they're asked in, so
// they're never kept. Each is copied once, the hero with its tint and fade
// baked in, the logo and cards scaled to fit, and the copies, which are
// the plugin's own surfaces, are kept across frames, the least recently
// drawn going first: three heroes and logos (the selected entry's and one
// either side) and two rails of cards. Each frame also asks for the hero
// art and logo of the entries either side of the selection, and the cards
// just outside the rail, so the art cache has them in before they show. A
// copy that can't be made (the plugin's surfaces are full) is drawn
// straight from the cache's image that frame instead.
//
// A plugin's art_replace surface, such as a video preview, stands in for
// the hero art, cover-fitted, tinted and faded the same way, but straight
// from the plugin's surface each frame, with the tint and fade drawn over
// it on the screen, since each of its frames is new. It's asked for with
// art_replacement. Art decorators aren't asked to draw under the hero, as
// the cards are there.

#include <stdio.h>
#include <string.h>
#include "hero_model.h"
#include "mosaic_plugin.h"

#define ID "layout-hero-rail"
#define MAX_PATH_LEN 512
#define HERO_COPIES 3
#define LOGO_COPIES 3
#define CARD_COPIES (2 * HR_MAX_CARDS)
#define FOUND_MAX 8

static const MosaicHost* host;
static int percent = 28;
static int tint = 0; // alpha of the accent over the hero
static int fade_ms = HR_FADE_DEFAULT;
static int first = 0; // the first card in view
static char window_path[MAX_PATH_LEN]; // the folder first is for
static int window_depth = -1;

// the logo's fade: the entry the selection last settled on, and when
static int settles = 0; // the launcher has selection_changed
static char settled_path[MAX_PATH_LEN];
static unsigned long settled_at = 0;
static unsigned long now_ms = 0;
static int fading = 0;

///////////////////////////////////////
// copies of art, kept across frames

typedef struct Copy {
	char path[MAX_PATH_LEN]; // the entry's
	int w, h;                // the size it was made for
	MosaicSurface* surface;  // NULL for a free slot
	unsigned long used;
} Copy;

static Copy heroes[HERO_COPIES];
static Copy logos[LOGO_COPIES];
static Copy cards[CARD_COPIES];
static unsigned long frame = 0;

static void copyFree(Copy* c) {
	if (c->surface) host->surface_free(ID, c->surface);
	memset(c, 0, sizeof(*c));
}

static void poolClear(Copy* pool, int n) {
	for (int i = 0; i < n; i++) copyFree(&pool[i]);
}

static Copy* poolFind(Copy* pool, int n, const char* path, int w, int h) {
	for (int i = 0; i < n; i++) {
		Copy* c = &pool[i];
		if (c->surface && c->w == w && c->h == h && !strcmp(c->path, path)) return c;
	}
	return NULL;
}

// Keeps surface as path's copy at w by h, freeing the least recently drawn
// when limit are in use. Returns the slot, or NULL (freeing surface) when
// the path doesn't fit.
static Copy* poolPut(Copy* pool, int n, int limit, const char* path, int w, int h, MosaicSurface* surface) {
	if (strlen(path) >= MAX_PATH_LEN) {
		host->surface_free(ID, surface);
		return NULL;
	}
	if (limit > n) limit = n;
	Copy* free_slot = NULL;
	Copy* oldest = NULL;
	int in_use = 0;
	for (int i = 0; i < n; i++) {
		Copy* c = &pool[i];
		if (!c->surface) {
			if (!free_slot) free_slot = c;
			continue;
		}
		in_use++;
		if (!oldest || c->used < oldest->used) oldest = c;
	}
	Copy* slot = free_slot && in_use < limit ? free_slot : oldest;
	if (!slot) slot = free_slot;
	copyFree(slot);
	snprintf(slot->path, sizeof(slot->path), "%s", path);
	slot->w = w;
	slot->h = h;
	slot->surface = surface;
	slot->used = frame;
	return slot;
}

///////////////////////////////////////
// which art the hero uses

static const int hero_kinds[] = { MOSAIC_ART_BACKDROP, MOSAIC_ART_SCREENSHOT, MOSAIC_ART_THUMBNAIL, MOSAIC_ART_BOX };

// the kind each entry's hero uses, or -1 for none, so files are looked
// for once a folder
typedef struct Found {
	char path[MAX_PATH_LEN];
	int kind;
} Found;

static Found found[FOUND_MAX];
static int found_next = 0;

static void foundClear(void) {
	memset(found, 0, sizeof(found));
	found_next = 0;
}

static int heroKind(const MosaicEntry* entry, const char* path) {
	for (int i = 0; i < FOUND_MAX; i++) {
		if (found[i].path[0] && !strcmp(found[i].path, path)) return found[i].kind;
	}
	int kind = -1;
	char file[MAX_PATH_LEN];
	for (size_t i = 0; i < sizeof(hero_kinds) / sizeof(hero_kinds[0]); i++) {
		if (host->art_find(entry, hero_kinds[i], file, sizeof(file)) == 1) {
			kind = hero_kinds[i];
			break;
		}
	}
	if (strlen(path) < MAX_PATH_LEN) {
		Found* f = &found[found_next++ % FOUND_MAX];
		snprintf(f->path, sizeof(f->path), "%s", path);
		f->kind = kind;
	}
	return kind;
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

static int nameHeight(const MosaicListView* view, int scale) {
	return view->show_names ? host->text_height(MOSAIC_FONT_SMALL) + 2 * scale : 0;
}

static int layoutFor(const MosaicListView* view, HRLayout* layout) {
	int scale = host->screen_scale();
	MosaicRect header = MOSAIC_VIEW_HAS(view, header) ? view->header : (MosaicRect){ 0, 0, 0, 0 };
	return HR_layout(&view->area, &view->status, &header, scale, percent, nameHeight(view, scale), layout);
}

static int nameHidden(const MosaicListView* view, int index) {
	if (!view->show_names) return 1;
	if (!MOSAIC_VIEW_HAS(view, names_hidden) || !view->names_hidden) return 0;
	return index >= 0 && index < view->count && view->names_hidden[index];
}

// Draws src as the hero, around the status pill and header: a copy the
// hero's size, or with raw the crop of an image that's cover-fitted to it.
static void blitAround(MosaicSurface* screen, const MosaicSurface* src, const MosaicRect* crop, int raw,
		const HRLayout* layout) {
	const MosaicRect* hero = &layout->hero;
	MosaicRect parts[4];
	int n = HR_avoid(hero, &layout->hole, parts);
	for (int i = 0; i < n; i++) {
		MosaicRect* p = &parts[i];
		MosaicRect from = { p->x - hero->x, p->y - hero->y, p->w, p->h };
		if (raw) {
			from.x = crop->x + (int)((long)from.x * crop->w / hero->w);
			from.y = crop->y + (int)((long)from.y * crop->h / hero->h);
			from.w = (int)((long)p->w * crop->w / hero->w);
			from.h = (int)((long)p->h * crop->h / hero->h);
			if (from.w < 1) from.w = 1;
			if (from.h < 1) from.h = 1;
		}
		host->blit(screen, src, &from, p);
	}
}

// The hero copy of image: cover-fitted to w by h on the background colour,
// tinted and fading into the background colour from fade_y down.
static MosaicSurface* makeHero(const MosaicSurface* image, int w, int h, int fade_y) {
	int iw = 0, ih = 0;
	MosaicRect crop;
	if (host->surface_size(image, &iw, &ih) != 0 || HR_cover(iw, ih, w, h, &crop) != 0) return NULL;
	MosaicSurface* s = host->surface_new(ID, w, h);
	if (!s) return NULL;
	MosaicColor bg = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255);
	MosaicRect all = { 0, 0, w, h };
	host->fill_rect(s, NULL, bg);
	host->blit(s, image, &crop, &all);
	if (tint > 0) host->fill_rect(s, NULL, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, (unsigned char)tint));
	int bands = HR_bands(fade_y, h);
	for (int i = 0; i < bands; i++) {
		int y = 0, band_h = 0, alpha = 0;
		if (HR_band(fade_y, h, i, &y, &band_h, &alpha) != 0 || band_h <= 0) continue;
		MosaicRect band = { 0, y, w, band_h };
		bg.a = (unsigned char)alpha;
		host->fill_rect(s, &band, bg);
	}
	return s;
}

// Fills rect on the screen in color, around the status pill and header.
static void fillAround(MosaicSurface* screen, const MosaicRect* rect, const HRLayout* layout, MosaicColor color) {
	MosaicRect parts[4];
	int n = HR_avoid(rect, &layout->hole, parts);
	for (int i = 0; i < n; i++) host->fill_rect(screen, &parts[i], color);
}

// Draws a plugin's art_replace surface as the hero: on the background
// colour, cover-fitted, then tinted and faded as makeHero bakes it into a
// copy, on the screen. Returns 1 when it drew, or 0 when there's none.
static int drawReplacedHero(MosaicSurface* screen, const MosaicListView* view, const HRLayout* layout) {
	const MosaicSurface* image = NULL;
	const MosaicRect* hero = &layout->hero;
	int iw = 0, ih = 0;
	MosaicRect crop;
	if (!MOSAIC_HOST_HAS(host, art_replacement) || host->art_replacement(view->selected, &image) != 1 || !image) return 0;
	if (host->surface_size(image, &iw, &ih) != 0 || HR_cover(iw, ih, hero->w, hero->h, &crop) != 0) return 0;
	MosaicColor bg = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255);
	fillAround(screen, hero, layout, bg);
	blitAround(screen, image, &crop, 1, layout);
	if (tint > 0) fillAround(screen, hero, layout, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, (unsigned char)tint));
	int bands = HR_bands(layout->fade_y, hero->h);
	for (int i = 0; i < bands; i++) {
		int y = 0, band_h = 0, alpha = 0;
		if (HR_band(layout->fade_y, hero->h, i, &y, &band_h, &alpha) != 0 || band_h <= 0) continue;
		MosaicRect band = { hero->x, hero->y + y, hero->w, band_h };
		bg.a = (unsigned char)alpha;
		fillAround(screen, &band, layout, bg);
	}
	bg.a = 255;
	fillAround(screen, &layout->below, layout, bg);
	return 1;
}

// Draws the selected entry's hero. Returns 1 when it drew, or 0 when
// there's none yet.
static int drawHero(MosaicSurface* screen, const MosaicListView* view, const MosaicEntry* entry,
		const HRLayout* layout) {
	if (!view->show_art || !entry) return 0;
	const char* path = host->entry_path(entry);
	const MosaicRect* hero = &layout->hero;
	if (!path || hero->w <= 0 || hero->h <= 0) return 0;
	if (drawReplacedHero(screen, view, layout)) return 1;

	const MosaicSurface* src = NULL;
	MosaicRect crop = { 0, 0, hero->w, hero->h };
	int raw = 0; // straight from the cache's image, cropped to crop
	Copy* c = poolFind(heroes, HERO_COPIES, path, hero->w, hero->h);
	if (c) {
		c->used = frame;
		src = c->surface;
	} else {
		int kind = heroKind(entry, path);
		const MosaicSurface* image = NULL;
		if (kind < 0 || host->art_get(entry, kind, &image) != MOSAIC_ART_READY || !image) return 0;
		MosaicSurface* copy = makeHero(image, hero->w, hero->h, layout->fade_y);
		if (copy) c = poolPut(heroes, HERO_COPIES, HERO_COPIES, path, hero->w, hero->h, copy);
		if (c) {
			src = c->surface;
		} else {
			int iw = 0, ih = 0;
			if (host->surface_size(image, &iw, &ih) != 0 || HR_cover(iw, ih, hero->w, hero->h, &crop) != 0) return 0;
			src = image;
			raw = 1;
		}
	}

	blitAround(screen, src, &crop, raw, layout);
	fillAround(screen, &layout->below, layout, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255));
	return 1;
}

static void blitFaded(MosaicSurface* screen, const MosaicSurface* src, const MosaicRect* to, int alpha) {
	if (alpha >= 255) host->blit(screen, src, NULL, to);
	else if (alpha > 0 && MOSAIC_HOST_HAS(host, blit_alpha)) host->blit_alpha(screen, src, NULL, to, alpha);
}

// Draws the selected entry's logo at alpha. Returns 1 when it has one, or
// 0 when there's none yet. Asks for it even at alpha 0, so it's in by the
// time it shows.
static int drawLogo(MosaicSurface* screen, const MosaicListView* view, const MosaicEntry* entry,
		const HRLayout* layout, int alpha) {
	if (!view->show_art || !entry) return 0;
	const char* path = host->entry_path(entry);
	const MosaicRect* box = &layout->logo;
	if (!path || box->w <= 0 || box->h <= 0) return 0;
	Copy* c = poolFind(logos, LOGO_COPIES, path, box->w, box->h);
	int w = 0, h = 0;
	if (c) {
		c->used = frame;
		if (host->surface_size(c->surface, &w, &h) != 0) return 0;
		MosaicRect to = { box->x, box->y + box->h - h, w, h };
		blitFaded(screen, c->surface, &to, alpha);
		return 1;
	}
	const MosaicSurface* image = NULL;
	if (host->art_get(entry, MOSAIC_ART_LOGO, &image) != MOSAIC_ART_READY || !image) return 0;
	if (host->surface_size(image, &w, &h) != 0) return 0;
	MosaicRect to = HR_fit(w, h, box);
	if (to.w <= 0 || to.h <= 0) return 0;
	MosaicSurface* copy = host->surface_scale(ID, image, to.w, to.h, MOSAIC_FIT_STRETCH);
	if (copy) c = poolPut(logos, LOGO_COPIES, LOGO_COPIES, path, box->w, box->h, copy);
	blitFaded(screen, c ? c->surface : image, &to, alpha);
	return 1;
}

static void drawName(MosaicSurface* screen, const MosaicListView* view, const MosaicEntry* entry,
		const HRLayout* layout, int alpha) {
	const char* name = host->entry_name(entry);
	int h = host->text_height(MOSAIC_FONT_LARGE);
	if (!name || !*name || h <= 0 || alpha <= 0) return;
	int scale = host->screen_scale();
	int x = layout->logo.x;
	int y = layout->logo.y + layout->logo.h - h;
	if (y < layout->logo.y) y = layout->logo.y;
	int max_w = view->area.x + view->area.w - x - (layout->card_x - view->area.x);
	if (max_w < layout->logo.w) max_w = layout->logo.w;
	MosaicColor shadow = { 0, 0, 0, (unsigned char)(alpha * 3 / 4) };
	host->draw_text(screen, MOSAIC_FONT_LARGE, name, x + scale, y + scale, shadow, max_w);
	host->draw_text(screen, MOSAIC_FONT_LARGE, name, x, y, themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, (unsigned char)alpha),
		max_w);
}

// the card art to ask for: the thumbnail, or else the box
static int cardArt(const MosaicEntry* entry, const MosaicSurface** image) {
	int state = host->art_get(entry, MOSAIC_ART_THUMBNAIL, image);
	if (state == MOSAIC_ART_MISSING) state = host->art_get(entry, MOSAIC_ART_BOX, image);
	return state;
}

// Draws entry's card art in rect, on a tile of the background colour.
// Returns 1 when it drew, or 0 when there's none yet.
static int drawCardArt(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* rect, int radius,
		int limit) {
	const char* path = host->entry_path(entry);
	if (!path || rect->w <= 0 || rect->h <= 0) return 0;
	MosaicColor backing = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255);
	Copy* c = poolFind(cards, CARD_COPIES, path, rect->w, rect->h);
	if (c) {
		c->used = frame;
		host->fill_round_rect(screen, rect, radius, backing);
		return host->blit(screen, c->surface, NULL, rect) == 0;
	}
	const MosaicSurface* image = NULL;
	if (cardArt(entry, &image) != MOSAIC_ART_READY || !image) return 0;
	host->fill_round_rect(screen, rect, radius, backing);
	MosaicSurface* copy = host->surface_scale(ID, image, rect->w, rect->h, MOSAIC_FIT_CONTAIN);
	if (copy) c = poolPut(cards, CARD_COPIES, limit, path, rect->w, rect->h, copy);
	if (c) return host->blit(screen, c->surface, NULL, rect) == 0;
	// no copy: this frame straight from the cache's image
	int w = 0, h = 0;
	if (host->surface_size(image, &w, &h) != 0) return 0;
	MosaicRect box = HR_fit(w, h, rect);
	box.x += (rect->w - box.w) / 2;
	box.y -= (rect->h - box.h) / 2;
	return host->blit(screen, image, NULL, &box) == 0;
}

// text centred in rect, truncated to it; writes where it went to drawn
static void drawCentred(MosaicSurface* screen, int font, const char* text, const MosaicRect* rect, MosaicColor color,
		MosaicRect* drawn) {
	char shown[256];
	int w = host->text_truncate(font, text, rect->w, shown, sizeof(shown));
	int h = host->text_height(font);
	if (w < 0 || h < 0) {
		*drawn = (MosaicRect){ rect->x, rect->y, 0, rect->h };
		return;
	}
	int x = rect->x + (rect->w - w) / 2;
	int y = rect->y + (rect->h - h) / 2;
	host->draw_text(screen, font, shown, x, y, color, rect->w);
	*drawn = (MosaicRect){ x, rect->y, w, rect->h };
}

static void drawCard(MosaicSurface* screen, const MosaicListView* view, int index, int slot,
		const HRLayout* layout) {
	const MosaicEntry* entry = host->list_get(view->list, index);
	if (!entry) return;
	int scale = host->screen_scale();
	int selected = index == view->selected;
	MosaicRect card = HR_card(layout, slot, selected);
	if (selected) {
		int f = layout->frame;
		MosaicRect highlight = { card.x - f, card.y - f, card.w + 2 * f, card.h + 2 * f };
		host->fill_round_rect(screen, &highlight, 6 * scale, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255));
	}
	MosaicColor text = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 255);
	int art = view->show_art && drawCardArt(screen, entry, &card, 4 * scale, 2 * layout->cards);
	const char* name = host->entry_name(entry);
	int named = layout->name_h > 0 && (!nameHidden(view, index) || !art);
	if (!art) {
		host->fill_round_rect(screen, &card, 4 * scale, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, 255));
		MosaicRect unused;
		if (named) {
			char letter[8];
			HR_letter(name, letter, sizeof(letter));
			if (letter[0]) drawCentred(screen, MOSAIC_FONT_LARGE, letter, &card, text, &unused);
		} else if (name) {
			MosaicRect inside = { card.x + 2 * scale, card.y, card.w - 4 * scale, card.h };
			drawCentred(screen, MOSAIC_FONT_SMALL, name, &inside, text, &unused);
		}
	}
	MosaicRect drawn = { 0, 0, 0, 0 };
	MosaicRect strip = HR_nameStrip(layout, &card);
	if (named && name) drawCentred(screen, MOSAIC_FONT_SMALL, name, &strip, text, &drawn);
	MosaicRect cell = { card.x, card.y, card.w, card.h + layout->name_h };
	host->decorate_cell(screen, index, &cell, named ? &drawn : NULL);
}

// Asks for art about to show, so the cache has it in: the hero and logo of
// the entries either side of the selection, and the cards either side of
// the rail.
static void prefetch(const MosaicListView* view, const HRLayout* layout) {
	if (!view->show_art) return;
	const MosaicSurface* image = NULL;
	for (int d = -1; d <= 1; d += 2) {
		int i = view->selected + d;
		const MosaicEntry* entry = i >= 0 && i < view->count ? host->list_get(view->list, i) : NULL;
		const char* path = entry ? host->entry_path(entry) : NULL;
		if (!path) continue;
		if (!poolFind(heroes, HERO_COPIES, path, layout->hero.w, layout->hero.h)) {
			int kind = heroKind(entry, path);
			if (kind >= 0) host->art_get(entry, kind, &image);
		}
		if (!poolFind(logos, LOGO_COPIES, path, layout->logo.w, layout->logo.h)) {
			host->art_get(entry, MOSAIC_ART_LOGO, &image);
		}
	}
	int edges[2] = { first - 1, first + layout->cards };
	for (int k = 0; k < 2; k++) {
		int i = edges[k];
		const MosaicEntry* entry = i >= 0 && i < view->count ? host->list_get(view->list, i) : NULL;
		const char* path = entry ? host->entry_path(entry) : NULL;
		if (path && !poolFind(cards, CARD_COPIES, path, layout->card_w, layout->card_h)) cardArt(entry, &image);
	}
}

// The logo's alpha for the selected entry: nothing until the selection
// settles on it, then fading in.
static int logoAlpha(const MosaicEntry* entry) {
	if (!settles) return 255;
	const char* path = entry ? host->entry_path(entry) : NULL;
	if (!path || strcmp(path, settled_path) != 0) return 0;
	return HR_fade((long)(now_ms - settled_at), fade_ms);
}

// logs the layout each time its shape changes, such as when names hide
static void logLayout(const HRLayout* layout) {
	static HRLayout logged;
	if (!memcmp(&logged, layout, sizeof(logged))) return;
	logged = *layout;
	char message[200];
	snprintf(message, sizeof(message), "hero %ix%i, %i cards of %i by %i, logo in %i,%i %ix%i", layout->hero.w,
		layout->hero.h, layout->cards, layout->card_w, layout->card_h, layout->logo.x, layout->logo.y, layout->logo.w,
		layout->logo.h);
	host->log(ID, message);
}

static int layoutInit(const MosaicListView* view) {
	HRLayout layout;
	if (layoutFor(view, &layout) != 0) {
		host->log(ID, "the screen is too small for a card rail, using the built-in list");
		return 1;
	}
	logLayout(&layout);
	return 0;
}

static int layoutInput(const MosaicInput* input, const MosaicListView* view, unsigned int* consumed) {
	static const int dirs[] = { MOSAIC_BUTTON_UP, MOSAIC_BUTTON_DOWN, MOSAIC_BUTTON_LEFT, MOSAIC_BUTTON_RIGHT };
	for (int i = 0; i < 4; i++) *consumed |= MOSAIC_BUTTON_BIT(dirs[i]);

	HRLayout layout;
	if (layoutFor(view, &layout) != 0) return -1;
	for (int i = 0; i < 4; i++) {
		unsigned int bit = MOSAIC_BUTTON_BIT(dirs[i]);
		if (!(input->repeated & bit)) continue;
		int to = HR_move(view->selected, view->count, layout.cards, dirs[i], (input->pressed & bit) != 0);
		if (to != view->selected) host->select_index(to);
		break; // one direction a frame
	}
	return 0;
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	HRLayout layout;
	if (layoutFor(view, &layout) != 0) return -1;
	logLayout(&layout);
	frame++;

	const char* path = view->path ? view->path : "";
	if (view->depth != window_depth || strcmp(path, window_path) != 0) {
		window_depth = view->depth;
		snprintf(window_path, sizeof(window_path), "%s", path);
		first = 0;
		foundClear();
	}
	first = HR_window(first, view->selected, view->count, layout.cards);

	const MosaicEntry* selected = host->list_get(view->list, view->selected);
	int hero = drawHero(screen, view, selected, &layout);
	int alpha = logoAlpha(selected);
	if (selected && !drawLogo(screen, view, selected, &layout, alpha) &&
		(!nameHidden(view, view->selected) || !hero)) {
		drawName(screen, view, selected, &layout, alpha);
	}
	for (int slot = 0; slot < layout.cards; slot++) {
		int index = first + slot;
		if (index >= view->count) break;
		drawCard(screen, view, index, slot, &layout);
	}
	prefetch(view, &layout);
	return 0;
}

static int selectionChanged(const MosaicEntry* entry) {
	const char* path = entry ? host->entry_path(entry) : NULL;
	snprintf(settled_path, sizeof(settled_path), "%s", path && strlen(path) < MAX_PATH_LEN ? path : "");
	settled_at = now_ms;
	fading = settled_path[0] != '\0';
	host->request_redraw();
	return 0;
}

static void tick(unsigned long now) {
	now_ms = now;
	if (!fading) return;
	// one more frame once it's done, at full strength
	if ((long)(now - settled_at) >= fade_ms) fading = 0;
	host->request_redraw();
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, select_index) || !MOSAIC_HOST_HAS(host, art_get) ||
		!MOSAIC_HOST_HAS(host, decorate_cell)) {
		host->log(ID, "this launcher can't draw a layout");
		return 1;
	}
	const char* size = host->cfg_get("cards", "medium");
	percent = HR_cardPercent(size);
	tint = HR_tint(host->cfg_get("tint", "20"));
	fade_ms = HR_fadeMs(host->cfg_get("fade", "300"));
	settles = MOSAIC_HOST_HAS(host, selection_settle_ms);
	settled_path[0] = '\0';
	fading = 0;
	first = 0;
	window_depth = -1;
	window_path[0] = '\0';
	foundClear();
	char message[160];
	snprintf(message, sizeof(message), "cards %i%% of the height, tinted by %i of 255, logo fading in over %i ms%s",
		percent, tint, fade_ms, settles ? "" : " (no selection events, so it shows at once)");
	host->log(ID, message);
	return 0;
}

static void quit(void) {
	poolClear(heroes, HERO_COPIES);
	poolClear(logos, LOGO_COPIES);
	poolClear(cards, CARD_COPIES);
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
	.selection_changed = selectionChanged,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
