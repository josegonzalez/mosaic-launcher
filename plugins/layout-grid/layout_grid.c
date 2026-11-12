// Grid layout: owns the layout slot and draws the game list as a grid of
// tiles, columns and rows set by its settings or fitted to the screen.
//
// Each tile shows the entry's art from the launcher's art cache, its
// thumbnail or else its box, fitted inside the tile, and while there's
// none (or it's still loading) the first letter of its name on a tile of
// the theme's accent colour. Names show under the tiles when the launcher
// shows names, and art only while Show game art is on. A name a plugin
// hides (MosaicListView's names_hidden) is left out of a tile that drew
// art, its strip left blank, and shows on a letter tile. The selected tile
// sits on a highlight in the theme's main colour.
//
// The d-pad moves the selection through the grid with select_index (see
// LG_move for where it wraps); everything else, A, B, X, the shoulders and
// the letter jumps, stays the launcher's. The rows in view follow the
// selection, moving only as far as it takes to show it.
//
// Art: art_get's images are only good for the frame they're asked in, so
// they're never kept. Each one is copied once with surface_scale to its
// tile's size, and the copies, which are the plugin's own surfaces, are kept
// across frames: up to two screens of tiles, the least recently drawn going
// first. A copy that can't be made (the plugin's surfaces are full) is
// drawn straight from the cache's image that frame instead.
//
// A plugin's art_replace surface, such as a video preview, stands in for
// the selected tile's art, fitted inside it the same way. It's asked for
// with art_replacement each frame and drawn straight from the plugin's
// surface, never copied, since each of its frames is new.

#include <stdio.h>
#include <string.h>
#include "grid_model.h"
#include "mosaic_plugin.h"

#define ID "layout-grid"
#define MAX_PATH_LEN 512
#define CACHE_MAX (2 * LG_MAX_COLUMNS * LG_MAX_ROWS)

static const MosaicHost* host;
static int columns_setting = 0; // 0 for auto
static int rows_setting = 0;
static int first_row = 0; // the first row in view
static char window_path[MAX_PATH_LEN]; // the folder first_row is for
static int window_depth = -1;

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

// Draws the plugin's art_replace surface for the entry at index in rect,
// fitted inside it. Returns 1 when it drew, or 0 when there's none.
static int drawReplacement(MosaicSurface* screen, int index, const MosaicRect* rect) {
	const MosaicSurface* image = NULL;
	int w = 0, h = 0;
	if (!MOSAIC_HOST_HAS(host, art_replacement) || host->art_replacement(index, &image) != 1 || !image) return 0;
	if (host->surface_size(image, &w, &h) != 0) return 0;
	MosaicRect to = contain(rect, w, h);
	return host->blit(screen, image, NULL, &to) == 0;
}

// Draws the entry at index's art in rect: a plugin's replacement, its
// thumbnail, or else its box. Returns 1 when it drew, or 0 when there's
// none yet.
static int drawArt(MosaicSurface* screen, const MosaicEntry* entry, int index, const MosaicRect* rect, int limit) {
	const char* path = host->entry_path(entry);
	if (!path || rect->w <= 0 || rect->h <= 0) return 0;
	if (drawReplacement(screen, index, rect)) return 1;
	Scaled* hit = cacheFind(path, rect->w, rect->h);
	if (hit) {
		hit->used = frame;
		return host->blit(screen, hit->surface, NULL, rect) == 0;
	}

	const MosaicSurface* image = NULL;
	int state = host->art_get(entry, MOSAIC_ART_THUMBNAIL, &image);
	if (state == MOSAIC_ART_MISSING) state = host->art_get(entry, MOSAIC_ART_BOX, &image);
	if (state != MOSAIC_ART_READY || !image) return 0;

	if (strlen(path) < MAX_PATH_LEN) {
		MosaicSurface* copy = host->surface_scale(ID, image, rect->w, rect->h, MOSAIC_FIT_CONTAIN);
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
	MosaicRect to = contain(rect, w, h);
	return host->blit(screen, image, NULL, &to) == 0;
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static int nameHeight(const MosaicListView* view, int scale) {
	return view->show_names ? host->text_height(MOSAIC_FONT_SMALL) + 4 * scale : 0;
}

static int hiddenName(const MosaicListView* view, int index) {
	return MOSAIC_VIEW_HAS(view, names_hidden) && view->names_hidden && index >= 0 && index < view->count &&
		view->names_hidden[index];
}

static int layoutGrid(const MosaicListView* view, LGGrid* grid) {
	int scale = host->screen_scale();
	MosaicRect header = view->size >= offsetof(MosaicListView, header) + sizeof(view->header)
		? view->header
		: (MosaicRect){ 0, 0, 0, 0 };
	// the highlight and frame around the tile, and room for some art
	int min = 2 * (2 + 3) * scale + 16 * scale;
	return LG_grid(&view->area, &view->status, &header, scale, columns_setting, rows_setting, min,
		min + nameHeight(view, scale), grid);
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

// logs the grid each time its shape changes, such as when names hide
static void logGrid(const LGGrid* grid) {
	static LGGrid logged;
	if (logged.cols == grid->cols && logged.rows == grid->rows && logged.cell_w == grid->cell_w &&
		logged.cell_h == grid->cell_h) {
		return;
	}
	logged = *grid;
	char message[128];
	snprintf(message, sizeof(message), "grid of %ix%i cells, %i by %i", grid->cols, grid->rows, grid->cell_w,
		grid->cell_h);
	host->log(ID, message);
}

static int layoutInit(const MosaicListView* view) {
	LGGrid grid;
	if (layoutGrid(view, &grid) != 0) {
		host->log(ID, "the screen is too small for a grid, using the built-in list");
		return 1;
	}
	logGrid(&grid);
	return 0;
}

static int layoutInput(const MosaicInput* input, const MosaicListView* view, unsigned int* consumed) {
	static const int dirs[] = { MOSAIC_BUTTON_UP, MOSAIC_BUTTON_DOWN, MOSAIC_BUTTON_LEFT, MOSAIC_BUTTON_RIGHT };
	for (int i = 0; i < 4; i++) *consumed |= MOSAIC_BUTTON_BIT(dirs[i]);

	LGGrid grid;
	if (layoutGrid(view, &grid) != 0) return -1;
	for (int i = 0; i < 4; i++) {
		unsigned int bit = MOSAIC_BUTTON_BIT(dirs[i]);
		if (!(input->repeated & bit)) continue;
		int to = LG_move(view->selected, view->count, grid.cols, dirs[i], (input->pressed & bit) != 0);
		if (to != view->selected) host->select_index(to);
		break; // one direction a frame
	}
	return 0;
}

static int layoutRender(MosaicSurface* screen, const MosaicListView* view) {
	LGGrid grid;
	if (layoutGrid(view, &grid) != 0) return -1;
	logGrid(&grid);
	frame++;

	const char* path = view->path ? view->path : "";
	if (view->depth != window_depth || strcmp(path, window_path) != 0) {
		window_depth = view->depth;
		snprintf(window_path, sizeof(window_path), "%s", path);
		first_row = 0;
	}
	first_row = LG_window(first_row, view->selected, view->count, grid.cols, grid.rows);

	int scale = host->screen_scale();
	int name_h = nameHeight(view, scale);
	MosaicColor main_color = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	MosaicColor tile_color = themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257);
	MosaicColor text = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor text_selected = themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000);
	int limit = 2 * grid.cols * grid.rows;

	for (int slot = 0; slot < grid.cols * grid.rows; slot++) {
		int index = first_row * grid.cols + slot;
		if (index >= view->count) break;
		const MosaicEntry* entry = host->list_get(view->list, index);
		if (!entry) continue;
		int selected = index == view->selected;
		MosaicRect cell = LG_cell(&grid, slot);
		MosaicRect highlight, tile, art, name;
		LG_parts(&cell, 2 * scale, 3 * scale, name_h, &highlight, &tile, &art, &name);

		if (selected) host->fill_round_rect(screen, &highlight, 6 * scale, main_color);
		int drew = view->show_art && drawArt(screen, entry, index, &art, limit);
		if (!drew) {
			char letter[8];
			LG_letter(host->entry_name(entry), letter, sizeof(letter));
			host->fill_round_rect(screen, &art, 4 * scale, tile_color);
			MosaicRect unused;
			if (letter[0]) drawCentred(screen, MOSAIC_FONT_LARGE, letter, &art, text, &unused);
		}
		// a hidden name's strip stays blank under its art, so the tiles keep one size
		int named = name_h > 0 && LG_showName(view->show_names, hiddenName(view, index), drew);
		MosaicRect drawn = { 0, 0, 0, 0 };
		if (named) drawCentred(screen, MOSAIC_FONT_SMALL, host->entry_name(entry), &name, selected ? text_selected : text, &drawn);
		host->decorate_cell(screen, index, &tile, named ? &drawn : NULL);
	}
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, select_index) || !MOSAIC_HOST_HAS(host, art_get) ||
		!MOSAIC_HOST_HAS(host, decorate_cell)) {
		host->log(ID, "this launcher can't draw a layout");
		return 1;
	}
	columns_setting = LG_count(host->cfg_get("columns", "auto"), 1, LG_MAX_COLUMNS);
	rows_setting = LG_count(host->cfg_get("rows", "auto"), 1, LG_MAX_ROWS);
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
	.layout_init = layoutInit,
	.layout_input = layoutInput,
	.layout_render = layoutRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
