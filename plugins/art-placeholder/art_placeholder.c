// Placeholder art: a thumbnail and a box for games with no art, made with
// art_surface, which the launcher asks only when no file answers. The
// style setting picks a title card in the theme's colours or a pattern
// seeded from the rom's path; placeholder_model.h has their sizes,
// colours and layout. A game with any real art of the kinds the built-in
// list shows (thumbnail, box, 3D box, screenshot) gets none, so a layout
// that asks for the thumbnail first still falls back to a real box.

#include "placeholder_model.h"
#include "mosaic_plugin.h"

#define ID "art-placeholder"
#define FONT MOSAIC_FONT_LARGE

static const MosaicHost* host;
static int style = PA_STYLE_TITLE;
static int scale = 1;

static MosaicColor opaque(unsigned int rgb) {
	return mosaic_color(rgb, 255);
}

static unsigned int theme(int index, unsigned int fallback) {
	return PA_themeOr(host->theme_color(index), fallback);
}

// True when a file answers any kind the built-in list would show.
static int hasArt(const MosaicEntry* entry) {
	static const int kinds[] = { MOSAIC_ART_THUMBNAIL, MOSAIC_ART_BOX, MOSAIC_ART_BOX_3D, MOSAIC_ART_SCREENSHOT };
	for (int i = 0; i < (int)(sizeof(kinds) / sizeof(kinds[0])); i++) {
		char path[1024];
		if (host->art_find(entry, kinds[i], path, sizeof(path))) return 1;
	}
	return 0;
}

static int measure(const char* text, void* data) {
	(void)data;
	return host->text_width(FONT, text);
}

static void drawTitle(MosaicSurface* card, const char* name, int w, int h) {
	// stock's default theme colours when the theme has none
	unsigned int accent = theme(MOSAIC_COLOR_ACCENT, 0x9B2257);
	unsigned int text = PA_contrast(accent, theme(MOSAIC_COLOR_MAIN, 0xFFFFFF),
		theme(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000));
	host->fill_rect(card, NULL, opaque(theme(MOSAIC_COLOR_ACCENT2, 0x1E2329)));
	PATitle layout;
	PA_titleLayout(w, h, scale, &layout);
	MosaicRect panel = { layout.margin, layout.margin, w - 2 * layout.margin, h - 2 * layout.margin };
	host->fill_round_rect(card, &panel, layout.radius, opaque(accent));

	char lines[PA_LINES][PA_LINE_LEN];
	int count = PA_wrap(name, layout.text_w, measure, NULL, lines);
	int line_h = host->text_height(FONT);
	int top = PA_textTop(h, count, line_h);
	for (int i = 0; i < count; i++) {
		char shown[PA_LINE_LEN];
		int shown_w = host->text_truncate(FONT, lines[i], layout.text_w, shown, sizeof(shown));
		if (shown_w < 0) continue;
		host->draw_text(card, FONT, shown, (w - shown_w) / 2, top + i * line_h, opaque(text), 0);
	}
}

static void drawPattern(MosaicSurface* card, const char* path, int w, int h) {
	PAPattern pattern;
	PA_pattern(path, w, h, &pattern);
	host->fill_rect(card, NULL, opaque(pattern.bg));
	for (int row = 0; row < pattern.rows; row++) {
		for (int col = 0; col < PA_COLS; col++) {
			int tile = pattern.tiles[row][col];
			MosaicRect rect;
			if (!tile || !PA_tile(w, h, pattern.rows, col, row, &rect.x, &rect.y, &rect.w)) continue;
			rect.h = rect.w;
			host->fill_round_rect(card, &rect, rect.w / 4, opaque(pattern.colors[tile - 1]));
		}
	}
}

static MosaicSurface* artSurface(const MosaicEntry* entry, int kind) {
	int w, h;
	if (host->entry_type(entry) != MOSAIC_ENTRY_ROM) return NULL;
	if (!PA_size(kind, host->screen_width(), host->screen_height(), &w, &h)) return NULL;
	const char* path = host->entry_path(entry);
	if (!path || hasArt(entry)) return NULL;
	MosaicSurface* card = host->surface_new(ID, w, h);
	if (!card) return NULL;
	if (style == PA_STYLE_PATTERN) {
		drawPattern(card, path, w, h);
	}
	else {
		const char* name = host->entry_name(entry);
		drawTitle(card, name ? name : "", w, h);
	}
	return card;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, plugin_art) || !MOSAIC_HOST_HAS(host, draw_text) || !MOSAIC_HOST_HAS(host, art_get)
		|| !MOSAIC_HOST_HAS(host, theme_color)) {
		host->log(ID, "this launcher can't take generated art, not loading");
		return 1;
	}
	style = PA_style(host->cfg_get("style", "title"));
	scale = host->screen_scale();
	return 0;
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.art_surface = artSurface,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
