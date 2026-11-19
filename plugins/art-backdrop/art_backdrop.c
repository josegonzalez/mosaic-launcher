// Art backdrop: owns the background slot and draws the selected game's art
// behind the game list, dimmed, full-bleed or fading out as a gradient,
// over the top of the screen as far as its height setting says. Where a
// game has none, and for folders, paks and empty folders, stock's
// background stays.
//
// Art: the game's backdrop, then its screenshot, thumbnail and box, each
// only while its fallback setting is on. Only art files count, as art_find
// reports them, so generated art such as art-placeholder's cards, which are
// drawn small, isn't blown up behind the list.
//
// art_get's images are only good for the frame they're asked in, so
// they're never kept. Each one is copied once with surface_scale to cover
// the art's area, and dimmed on the copy, and the copies, which are the
// plugin's own surfaces, are kept across frames: up to BD_COPIES, the least
// recently drawn going first. A copy that can't be made (the plugin's
// surfaces are full) is drawn straight from the cache's image that frame
// instead, dimmed as it's drawn.
//
// With the fill setting at full, the art covers the whole screen and is
// darkened toward the side the game list is on (see BD_side and BD_shade),
// baked into the copy with the dimming, so it's drawn once each time the
// selection moves to a game, never each frame. The side is the layout
// slot owner's, found with plugin_loaded on the first frame, once every
// plugin has loaded, and for layout-fancy its public position setting.
//
// The dim setting at theme, the default, takes the theme slot owner's
// art-backdrop.dim key, read once the theme is in place and again on
// launcher.theme_changed; a new dim drops the copies, which have the old
// one in them.

#include <stdio.h>
#include <string.h>
#include "backdrop_model.h"
#include "mosaic_plugin.h"

#define ID "art-backdrop"
#define MAX_PATH_LEN 512
#define BD_COPIES 3 // the selected game's and one either side

static const MosaicHost* host;
static int style = BD_STYLE_GRADIENT;
static int fill = BD_FILL_TOP;
static int side = BD_SIDE_LEFT;
static int side_resolved = 0; // 0 until the layout is known, on the first frame
static int dim = 0; // alpha of the black over the art
static char dim_setting[16];
static int dim_resolved = 0; // 0 until the theme's dim is read, and again when the theme changes
static int divisor = 1;
static int kinds[BD_MAX_KINDS];
static int kind_count = 0;
static char none_path[MAX_PATH_LEN]; // the last game found with no art

///////////////////////////////////////
// scaled art, kept across frames

typedef struct Copy {
	char path[MAX_PATH_LEN]; // the entry's
	int w, h;
	MosaicSurface* surface; // NULL for a free slot
	unsigned long used;
} Copy;

static Copy copies[BD_COPIES];
static unsigned long frame = 0;

static void copyFree(Copy* c) {
	if (c->surface) host->surface_free(ID, c->surface);
	memset(c, 0, sizeof(*c));
}

static Copy* copyFind(const char* path, int w, int h) {
	for (int i = 0; i < BD_COPIES; i++) {
		Copy* c = &copies[i];
		if (c->surface && c->w == w && c->h == h && !strcmp(c->path, path)) return c;
	}
	return NULL;
}

// a free slot, or the least recently drawn one, freed
static Copy* copySlot(void) {
	Copy* oldest = NULL;
	for (int i = 0; i < BD_COPIES; i++) {
		Copy* c = &copies[i];
		if (!c->surface) return c;
		if (!oldest || c->used < oldest->used) oldest = c;
	}
	copyFree(oldest);
	return oldest;
}

///////////////////////////////////////
// drawing

static const MosaicColor black = { 0, 0, 0, 255 };

// the darkening toward the list over a w by h surface, with fill at full
static int shade(MosaicSurface* s, int w, int h) {
	int bands = BD_shades(w, h, side);
	for (int i = 0; i < bands; i++) {
		MosaicRect r;
		int alpha = 0;
		if (BD_shade(w, h, side, i, &r, &alpha) != 0 || r.w <= 0 || r.h <= 0 || alpha <= 0) continue;
		MosaicColor c = black;
		c.a = (unsigned char)alpha;
		if (host->fill_rect(s, &r, c) != 0) return -1;
	}
	return 0;
}

// crop of src drawn over area of screen in the style, dimmed as it's drawn
// unless the dimming is already in src
static int draw(MosaicSurface* screen, const MosaicSurface* src, const MosaicRect* crop, const MosaicRect* area,
		int dimmed) {
	// Where stock's background is clear the theme's colour shows through,
	// but blending onto clear pixels would darken the art by its alpha
	// twice, so the area gets the theme's colour first.
	MosaicColor under;
	if (host->read_pixel(screen, area->x + area->w / 2, area->y + area->h - 1, &under) == 0 && under.a == 0) {
		int rgb = host->theme_color(MOSAIC_COLOR_BACKGROUND);
		if (host->fill_rect(screen, area, mosaic_color(rgb < 0 ? 0x000000 : (unsigned int)rgb, 255)) != 0) return -1;
	}
	MosaicColor shade_color = black;
	if (fill == BD_FILL_FULL) {
		if (host->blit(screen, src, crop, area) != 0) return -1;
		if (dimmed) return 0;
		shade_color.a = (unsigned char)dim;
		if (dim && host->fill_rect(screen, area, shade_color) != 0) return -1;
		return shade(screen, area->w, area->h);
	}
	if (style == BD_STYLE_FULL) {
		if (host->blit(screen, src, crop, area) != 0) return -1;
		shade_color.a = (unsigned char)dim;
		return dimmed || !dim ? 0 : host->fill_rect(screen, area, shade_color);
	}
	int bands = BD_bands(area->h);
	for (int i = 0; i < bands; i++) {
		int y = 0, h = 0, alpha = 0;
		if (BD_band(area->h, i, &y, &h, &alpha) != 0 || h <= 0) continue;
		MosaicRect to = { area->x, area->y + y, area->w, h };
		MosaicRect from = BD_srcBand(crop, area->h, y, h);
		int rc = alpha >= 255 ? host->blit(screen, src, &from, &to) : host->blit_alpha(screen, src, &from, &to, alpha);
		if (rc != 0) return -1;
		if (dimmed || !dim) continue;
		shade_color.a = (unsigned char)BD_mulAlpha(dim, alpha);
		if (host->fill_rect(screen, &to, shade_color) != 0) return -1;
	}
	return 0;
}

// The first art file entry has of the kinds, from the cache: READY with it
// in *image, LOADING while it's on its way, or MISSING when it has none.
static int pick(const MosaicEntry* entry, const MosaicSurface** image) {
	char file[MAX_PATH_LEN];
	for (int i = 0; i < kind_count; i++) {
		if (host->art_find(entry, kinds[i], file, sizeof(file)) != 1) continue;
		int state = host->art_get(entry, kinds[i], image);
		if (state == MOSAIC_ART_READY && *image) return MOSAIC_ART_READY;
		if (state == MOSAIC_ART_LOADING) return MOSAIC_ART_LOADING;
	}
	return MOSAIC_ART_MISSING;
}

// the dim setting, or the theme's
static void resolveDim(void) {
	const char* themed = MOSAIC_HOST_HAS(host, theme_value) ? host->theme_value(ID ".dim", NULL) : NULL;
	int now = BD_dim(BD_themed(dim_setting, themed));
	dim_resolved = 1;
	if (now == dim) return;
	for (int i = 0; i < BD_COPIES; i++) copyFree(&copies[i]);
	dim = now;
	char message[64];
	snprintf(message, sizeof(message), "dimmed by %i of 255 now", dim);
	host->log(ID, message);
}

// the side the list is on, from the layout slot's owner
static void resolveSide(void) {
	side_resolved = 1;
	const char* layout = NULL;
	if (MOSAIC_HOST_HAS(host, has_capability)) {
		for (int i = 0; i < BD_LAYOUT_COUNT && !layout; i++) {
			if (host->plugin_loaded(BD_layouts[i])) layout = BD_layouts[i];
		}
	}
	const char* position = NULL;
	if (layout && !strcmp(layout, "layout-fancy") && MOSAIC_HOST_HAS(host, cfg_read)) {
		position = host->cfg_read("layout-fancy", "position", "right");
	}
	side = BD_side(layout, position);
	char message[128];
	snprintf(message, sizeof(message), "darkened toward the %s, under %s", BD_sideName(side),
		layout ? layout : "the built-in list");
	host->log(ID, message);
}

static void themeChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)payload;
	(void)data;
	resolveDim();
}

static int backgroundRender(MosaicSurface* screen, const MosaicEntry* selected) {
	frame++;
	// the theme is put in place after every plugin's init
	if (!dim_resolved) resolveDim();
	if (fill == BD_FILL_FULL && !side_resolved) resolveSide();
	if (!selected || host->entry_type(selected) != MOSAIC_ENTRY_ROM) return 0;
	const char* path = host->entry_path(selected);
	if (!path || strlen(path) >= MAX_PATH_LEN) return 0;
	int w = 0, h = 0;
	if (host->surface_size(screen, &w, &h) != 0) return -1;
	MosaicRect area = BD_area(w, h, fill == BD_FILL_FULL ? 1 : divisor);
	if (area.w <= 0 || area.h <= 0) return 0;

	Copy* hit = copyFind(path, area.w, area.h);
	if (hit) {
		hit->used = frame;
		MosaicRect all = { 0, 0, area.w, area.h };
		return draw(screen, hit->surface, &all, &area, 1);
	}
	if (!strcmp(none_path, path)) return 0;

	const MosaicSurface* image = NULL;
	int state = pick(selected, &image);
	if (state == MOSAIC_ART_LOADING) return 0; // stock's until it's in, when the launcher redraws
	if (state != MOSAIC_ART_READY) {
		snprintf(none_path, sizeof(none_path), "%s", path);
		return 0;
	}

	MosaicSurface* copy = host->surface_scale(ID, image, area.w, area.h, MOSAIC_FIT_COVER);
	if (copy) {
		MosaicColor dimming = black;
		dimming.a = (unsigned char)dim;
		if (dim) host->fill_rect(copy, NULL, dimming);
		if (fill == BD_FILL_FULL) shade(copy, area.w, area.h);
		Copy* c = copySlot();
		snprintf(c->path, sizeof(c->path), "%s", path);
		c->w = area.w;
		c->h = area.h;
		c->surface = copy;
		c->used = frame;
		MosaicRect all = { 0, 0, area.w, area.h };
		return draw(screen, copy, &all, &area, 1);
	}
	// no copy: this frame straight from the cache's image
	int iw = 0, ih = 0;
	MosaicRect crop;
	if (host->surface_size(image, &iw, &ih) != 0 || BD_cover(iw, ih, area.w, area.h, &crop) != 0) return 0;
	return draw(screen, image, &crop, &area, 0);
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, plugin_art) || !MOSAIC_HOST_HAS(host, blit_alpha)) {
		host->log(ID, "this launcher can't draw a background");
		return 1;
	}
	const char* style_name = host->cfg_get("style", "gradient");
	const char* height = host->cfg_get("height", "full");
	style = BD_style(style_name);
	fill = BD_fill(host->cfg_get("fill", "top"));
	side = BD_SIDE_LEFT;
	side_resolved = 0;
	snprintf(dim_setting, sizeof(dim_setting), "%s", host->cfg_get("dim", "theme"));
	dim = BD_dim(BD_themed(dim_setting, NULL));
	dim_resolved = 0;
	divisor = BD_height(height);
	kind_count = BD_kinds(strcmp(host->cfg_get("screenshot", "on"), "off") != 0,
		strcmp(host->cfg_get("thumbnail", "on"), "off") != 0, strcmp(host->cfg_get("box", "on"), "off") != 0,
		kinds);
	none_path[0] = '\0';
	char message[128];
	if (fill == BD_FILL_FULL) {
		snprintf(message, sizeof(message), "over the whole screen, dimmed by %i of 255, from %i kinds of art", dim,
			kind_count);
	}
	else {
		snprintf(message, sizeof(message), "%s, dimmed by %i of 255, over 1/%i of the screen, from %i kinds of art",
			style == BD_STYLE_FULL ? "full-bleed" : "gradient", dim, divisor, kind_count);
	}
	host->log(ID, message);
	if (MOSAIC_HOST_HAS(host, subscribe) && host->subscribe("launcher.theme_changed", themeChanged, NULL) != 0) {
		host->log(ID, "couldn't follow theme changes");
	}
	return 0;
}

static void quit(void) {
	for (int i = 0; i < BD_COPIES; i++) copyFree(&copies[i]);
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.background_render = backgroundRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
