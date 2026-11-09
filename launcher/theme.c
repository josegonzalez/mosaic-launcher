#include <stdio.h>
#include <string.h>
#include "defines.h"
#include "api.h"
#include "config.h"
#include "utils.h"
#include "theme.h"

static int active = 0;
static ThemeSizes sizes;            // 0 for each left to stock
static int colors[MOSAIC_THEME_COLORS]; // 0xRRGGBB or MOSAIC_THEME_UNSET
static int contrast = 0;
static char font_path[512];          // "" for stock's
static char fallback_path[512];      // "" for none
static ThemeWalls walls;

static ThemeStock stockNow(void) {
	ThemeStock stock = {
		.height = FIXED_HEIGHT / FIXED_SCALE,
		.pill = UI_stockPill(),
		.button = UI_stockButton(),
		.padding = UI_stockPadding(),
		.rows = UI_stockRows(),
		.font_large = UI_stockFontLarge(),
		.font_medium = UI_stockFontMedium(),
		.font_micro = UI_stockFontMicro(),
	};
	return stock;
}

static int sized(void) {
	return active && (sizes.pill || sizes.button || sizes.padding);
}

int UI_pillSize(void) {
	return active && sizes.pill ? sizes.pill : UI_stockPill();
}

int UI_buttonSize(void) {
	return active && sizes.button ? sizes.button : UI_stockButton();
}

int UI_padding(void) {
	if (!sized()) return UI_stockPadding();
	ThemeStock stock = stockNow();
	return ThemeModel_paddingFor(&sizes, &stock);
}

int UI_rowCount(void) {
	if (!sized()) return UI_stockRows();
	ThemeStock stock = stockNow();
	return ThemeModel_rowsFor(&sizes, &stock);
}

int UI_textStep(void) {
	return active ? sizes.text_step : 0;
}

int Theme_uiSize(int which) {
	switch (which) {
		case MOSAIC_UI_PILL: return PILL_SIZE;
		case MOSAIC_UI_BUTTON: return BUTTON_SIZE;
		case MOSAIC_UI_PADDING: return PADDING;
		case MOSAIC_UI_ROWS: return MAIN_ROW_COUNT;
		case MOSAIC_UI_TEXT_STEP: return UI_textStep();
		default: return -1;
	}
}

const char* Theme_fallbackFont(void) {
	return active ? fallback_path : "";
}

const char* Theme_fontPath(const char* stock) {
	return active && font_path[0] ? font_path : stock;
}

void Theme_colors(void) {
	uint32_t now[MOSAIC_THEME_COLORS];
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) {
		now[i] = CFG_getColor(i + 1);
		if (active && colors[i] != MOSAIC_THEME_UNSET) now[i] = ThemeModel_stockColor(colors[i]);
	}
	if (active) ThemeModel_clampContrast(now, contrast);
	THEME_COLOR1_255 = now[0];
	THEME_COLOR2_255 = now[1];
	THEME_COLOR3_255 = now[2];
	THEME_COLOR4_255 = now[3];
	THEME_COLOR5_255 = now[4];
	THEME_COLOR6_255 = now[5];
	THEME_COLOR7_255 = now[6];
}

void Theme_blank(MosaicTheme* theme) {
	memset(theme, 0, sizeof(*theme));
	theme->size = sizeof(*theme);
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) theme->colors[i] = MOSAIC_THEME_UNSET;
}

static int refusals = 0;

static void refuse(const char* owner, const char* field, const char* why) {
	LOG_warn("plugins: %s's theme %s %s, so stock's is used\n", owner, field, why);
	refusals++;
}

// A path field: "" for none, or a regular file on the card.
static void cardFile(const char* owner, const char* field, const char* given, char* out, size_t size) {
	out[0] = '\0';
	if (!given[0]) return;
	char path[512];
	snprintf(path, sizeof(path), "%s", given);
	const char* why = NULL;
	if (ThemeModel_cardFile(SDCARD_PATH, path, out, size, &why) != 0) {
		out[0] = '\0';
		char line[600];
		snprintf(line, sizeof(line), "%s (%s)", field, path);
		refuse(owner, line, why);
	}
}

// A font field: a file on the card, as cardFile, that opens as a font.
static void cardFont(const char* owner, const char* field, const char* given, char* out, size_t size) {
	cardFile(owner, field, given, out, size);
	if (!out[0]) return;
	// one that won't open would leave the launcher with no font
	TTF_Font* test = TTF_OpenFont(out, SCALE1(UI_stockFontLarge() + sizes.text_step));
	if (test) TTF_CloseFont(test);
	else {
		char line[600];
		snprintf(line, sizeof(line), "%s (%s)", field, out);
		refuse(owner, line, "won't open");
		out[0] = '\0';
	}
}

int Theme_set(const char* owner, const MosaicTheme* theme) {
	refusals = 0;
	// the owner may have left a string unterminated
	MosaicTheme t = *theme;
	t.font[sizeof(t.font) - 1] = '\0';
	t.wallpaper[sizeof(t.wallpaper) - 1] = '\0';
	t.wallpaper_list[sizeof(t.wallpaper_list) - 1] = '\0';
	t.wallpaper_quick[sizeof(t.wallpaper_quick) - 1] = '\0';
	t.fallback_font[sizeof(t.fallback_font) - 1] = '\0';

	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) {
		colors[i] = t.colors[i];
		if (!ThemeModel_colorOk(colors[i])) {
			char field[32];
			snprintf(field, sizeof(field), "color%d 0x%x", i + 1, (unsigned)colors[i]);
			refuse(owner, field, "isn't 0xRRGGBB");
			colors[i] = MOSAIC_THEME_UNSET;
		}
	}
	contrast = t.contrast;
	if (contrast && (contrast < THEME_CONTRAST_MIN || contrast > THEME_CONTRAST_MAX)) {
		char field[32];
		snprintf(field, sizeof(field), "contrast %d", contrast);
		refuse(owner, field, "isn't from 10 to 210");
		contrast = 0;
	}

	ThemeRefusals refused = {0};
	ThemeStock stock = stockNow();
	sizes = ThemeModel_checkSizes(&t, &stock, &refused);
	for (int i = 0; i < refused.count; i++) {
		LOG_warn("plugins: %s's theme %s, so stock's is used\n", owner, refused.lines[i]);
		refusals++;
	}

	cardFont(owner, "font", t.font, font_path, sizeof(font_path));
	cardFont(owner, "fallback_font", t.fallback_font, fallback_path, sizeof(fallback_path));
	cardFile(owner, "wallpaper", t.wallpaper, walls.main, sizeof(walls.main));
	cardFile(owner, "wallpaper_list", t.wallpaper_list, walls.list, sizeof(walls.list));
	cardFile(owner, "wallpaper_quick", t.wallpaper_quick, walls.quick, sizeof(walls.quick));

	active = 1;
	GFX_themeChanged();
	LOG_info("plugins: %s supplies the theme: pill %d, button %d, padding %d, %d rows, text step %d, font %s\n",
		owner, PILL_SIZE, BUTTON_SIZE, PADDING, MAIN_ROW_COUNT, UI_textStep(), font_path[0] ? font_path : "stock's");
	return refusals;
}

void Theme_clear(void) {
	int was = active;
	active = 0;
	memset(&sizes, 0, sizeof(sizes));
	font_path[0] = '\0';
	fallback_path[0] = '\0';
	memset(&walls, 0, sizeof(walls));
	contrast = 0;
	if (was) GFX_themeChanged();
}

int Theme_active(void) {
	return active;
}

int Theme_fillWallpaper(int spot, char* path, size_t size) {
	if (!active || exists(path)) return 0;
	const char* wall = ThemeModel_wallpaper(&walls, spot);
	if (!wall) return 0;
	snprintf(path, size, "%s", wall);
	return 1;
}
