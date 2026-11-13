#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "thememodel.h"

int ThemeModel_rows(int height, int pill, int padding) {
	if (pill <= 0) return 0;
	int rows = (height - 2 * padding - pill) / pill;
	return rows > 0 ? rows : 0;
}

int ThemeModel_padding(int height, int pill) {
	if (pill <= 0) return 0;
	int left = height % pill;
	int padding = left ? left / 3 : pill / 3;
	return padding >= pill / 8 ? padding : pill / 3;
}

int ThemeModel_pill(const ThemeSizes* sizes, const ThemeStock* stock) {
	return sizes->pill ? sizes->pill : stock->pill;
}

int ThemeModel_button(const ThemeSizes* sizes, const ThemeStock* stock) {
	return sizes->button ? sizes->button : stock->button;
}

// a pill of stock's own size changes nothing
static int customPill(const ThemeSizes* sizes, const ThemeStock* stock) {
	return sizes->pill && sizes->pill != stock->pill;
}

int ThemeModel_paddingFor(const ThemeSizes* sizes, const ThemeStock* stock) {
	if (sizes->padding) return sizes->padding;
	if (customPill(sizes, stock)) return ThemeModel_padding(stock->height, sizes->pill);
	return stock->padding;
}

int ThemeModel_rowsFor(const ThemeSizes* sizes, const ThemeStock* stock) {
	if (!sizes->padding && !customPill(sizes, stock)) return stock->rows;
	return ThemeModel_rows(stock->height, ThemeModel_pill(sizes, stock), ThemeModel_paddingFor(sizes, stock));
}

static void refuse(ThemeRefusals* refused, const char* format, ...) {
	if (!refused || refused->count >= THEME_REFUSED_MAX) return;
	va_list args;
	va_start(args, format);
	vsnprintf(refused->lines[refused->count], sizeof(refused->lines[0]), format, args);
	va_end(args);
	refused->count++;
}

// A size within its range, or 0 with a line in refused.
static int ranged(const char* name, int value, int lo, int hi, ThemeRefusals* refused) {
	if (!value) return 0;
	if (value >= lo && value <= hi) return value;
	refuse(refused, "%s %d isn't from %d to %d", name, value, lo, hi);
	return 0;
}

ThemeSizes ThemeModel_checkSizes(const MosaicTheme* theme, const ThemeStock* stock, ThemeRefusals* refused) {
	ThemeSizes sizes = {
		ranged("pill_size", theme->pill_size, THEME_PILL_MIN, THEME_PILL_MAX, refused),
		ranged("button_size", theme->button_size, THEME_BUTTON_MIN, THEME_PILL_MAX - 4, refused),
		ranged("padding", theme->padding, THEME_PADDING_MIN, THEME_PADDING_MAX, refused),
		ranged("text_step", theme->text_step, THEME_STEP_MIN, THEME_STEP_MAX, refused),
	};
	// Until the sizes fit together, refuse one: the text step before the
	// button, the button before the pill, and the padding before the pill.
	// Stock's sizes always fit, so this ends.
	for (;;) {
		int pill = ThemeModel_pill(&sizes, stock);
		int button = ThemeModel_button(&sizes, stock);
		int step = sizes.text_step;
		int rows = ThemeModel_rowsFor(&sizes, stock);
		if (rows < THEME_ROWS_MIN) {
			if (sizes.padding) {
				refuse(refused, "padding %d leaves the list %d rows", sizes.padding, rows);
				sizes.padding = 0;
			}
			else {
				refuse(refused, "pill_size %d leaves the list %d rows", sizes.pill, rows);
				sizes.pill = 0;
			}
		}
		else if (stock->font_micro + step < 5) {
			refuse(refused, "text_step %d makes text too small", step);
			sizes.text_step = 0;
		}
		else if (stock->font_large + step > pill - 6) {
			if (step) {
				refuse(refused, "text_step %d makes text too big for a %d pill", step, pill);
				sizes.text_step = 0;
			}
			else {
				refuse(refused, "pill_size %d is too small for stock's text", pill);
				sizes.pill = 0;
			}
		}
		else if (stock->font_medium + step > button - 2) {
			if (step) {
				refuse(refused, "text_step %d makes text too big for a %d button", step, button);
				sizes.text_step = 0;
			}
			else {
				refuse(refused, "button_size %d is too small for stock's text", button);
				sizes.button = 0;
			}
		}
		else if (button > pill - 4) {
			if (sizes.button) {
				refuse(refused, "button_size %d doesn't fit a %d pill", button, pill);
				sizes.button = 0;
			}
			else {
				refuse(refused, "pill_size %d is too small for a %d button", pill, button);
				sizes.pill = 0;
			}
		}
		else return sizes;
	}
}

int ThemeModel_colorOk(int color) {
	return color == MOSAIC_THEME_UNSET || (color >= 0 && color <= 0xffffff);
}

uint32_t ThemeModel_stockColor(int rgb) {
	return ((uint32_t)rgb << 8) | 0xff;
}

// WCAG relative luminance of a stock colour
static double luminance(uint32_t color) {
	double channel[3];
	for (int i = 0; i < 3; i++) {
		double c = ((color >> (24 - 8 * i)) & 0xff) / 255.0;
		channel[i] = c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
	}
	return 0.2126 * channel[0] + 0.7152 * channel[1] + 0.0722 * channel[2];
}

int ThemeModel_contrast(uint32_t a, uint32_t b) {
	double la = luminance(a), lb = luminance(b);
	double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
	return (int)((hi + 0.05) / (lo + 0.05) * 10.0 + 1e-9);
}

// color moved part of the way (of 32) to target, keeping its alpha
static uint32_t mix(uint32_t color, uint32_t target, int part) {
	uint32_t out = color & 0xff;
	for (int shift = 24; shift >= 8; shift -= 8) {
		int c = (color >> shift) & 0xff, t = (target >> shift) & 0xff;
		out |= (uint32_t)(c + (t - c) * part / 32) << shift;
	}
	return out;
}

static int clampOne(uint32_t* text, uint32_t backdrop, int min_tenths) {
	if (ThemeModel_contrast(*text, backdrop) >= min_tenths) return 0;
	uint32_t white = 0xffffff00 | (*text & 0xff), black = *text & 0xff;
	uint32_t target = ThemeModel_contrast(white, backdrop) >= ThemeModel_contrast(black, backdrop) ? white : black;
	uint32_t moved = *text;
	for (int part = 1; part <= 32; part++) {
		moved = mix(*text, target, part);
		if (ThemeModel_contrast(moved, backdrop) >= min_tenths) break;
	}
	*text = moved;
	return 1;
}

int ThemeModel_clampContrast(uint32_t colors[7], int min_tenths) {
	if (min_tenths <= 0) return 0;
	int moved = 0;
	moved += clampOne(&colors[3], colors[6], min_tenths);
	moved += clampOne(&colors[4], colors[0], min_tenths);
	moved += clampOne(&colors[5], colors[1], min_tenths);
	return moved;
}

int ThemeModel_cardFile(const char* card_root, const char* path, char* out, size_t size, const char** why) {
	const char* unused;
	if (!why) why = &unused;
	if (!path || path[0] != '/') {
		*why = "isn't a full path";
		return -1;
	}
	char root[PATH_MAX], real[PATH_MAX];
	if (!realpath(card_root, root)) {
		*why = "has no card to be on";
		return -1;
	}
	if (!realpath(path, real)) {
		*why = "doesn't exist";
		return -1;
	}
	size_t n = strlen(root);
	if (strncmp(real, root, n) != 0 || real[n] != '/') {
		*why = "isn't on the card";
		return -1;
	}
	struct stat st;
	if (stat(real, &st) != 0 || !S_ISREG(st.st_mode)) {
		*why = "isn't a regular file";
		return -1;
	}
	if (strlen(real) >= size) {
		*why = "is too long";
		return -1;
	}
	strcpy(out, real);
	return 0;
}

const char* ThemeModel_wallpaper(const ThemeWalls* walls, int spot) {
	const char* path = NULL;
	switch (spot) {
		case THEME_WALL_MAIN: path = walls->main; break;
		case THEME_WALL_LIST: path = walls->list[0] ? walls->list : walls->main; break;
		case THEME_WALL_QUICK: path = walls->quick; break;
		default: break;
	}
	return path && path[0] ? path : NULL;
}

int ThemeModel_hintStyleOk(int style) {
	return style >= MOSAIC_HINT_STYLE_USER && style <= MOSAIC_HINT_STYLE_SHAPES;
}

int ThemeModel_hintStock(int theme_style, int stock) {
	if (theme_style < MOSAIC_HINT_STYLE_TEXT || theme_style > MOSAIC_HINT_STYLE_SHAPES) return stock;
	return theme_style - MOSAIC_HINT_STYLE_TEXT + THEME_HINT_STOCK_TEXT;
}

int ThemeModel_hintStyle(int stock) {
	if (stock < THEME_HINT_STOCK_TEXT || stock > THEME_HINT_STOCK_SHAPES) return MOSAIC_HINT_STYLE_TEXT;
	return stock - THEME_HINT_STOCK_TEXT + MOSAIC_HINT_STYLE_TEXT;
}

int ThemeModel_extraKeyOk(const char* key) {
	if (!key) return 0;
	size_t len = strlen(key);
	if (len < 3 || len >= THEME_EXTRA_KEY_MAX || key[0] == '.' || key[len - 1] == '.' || !strchr(key, '.')) return 0;
	for (size_t i = 0; i < len; i++) {
		char c = key[i];
		int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
		if (!ok) return 0;
	}
	return 1;
}

// The slot for key: its own when it's there already, else a new one, or
// -1 when they're all taken.
static int extraSlot(ThemeExtras* extras, const char* key) {
	for (int i = 0; i < extras->count; i++) {
		if (!strcmp(extras->keys[i], key)) return i;
	}
	return extras->count < MOSAIC_THEME_EXTRA_KEYS ? extras->count++ : -1;
}

int ThemeModel_extras(const char* text, ThemeExtras* out, ThemeRefusals* refused) {
	memset(out, 0, sizeof(*out));
	int count = 0;
	int number = 0;
	for (const char* line = text ? text : ""; *line;) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		const char* start = line;
		line = end ? end + 1 : line + len;
		number++;
		if (!len) continue;
		// for the log: the line, cut short
		char shown[48];
		snprintf(shown, sizeof(shown), "%.*s%s", len > 40 ? 40 : (int)len, start, len > 40 ? "..." : "");
		for (char* c = shown; *c; c++) {
			if ((unsigned char)*c < 0x20 || *c == 0x7f) *c = '?';
		}
		const char* eq = memchr(start, '=', len);
		size_t key_len = eq ? (size_t)(eq - start) : 0;
		size_t value_len = eq ? len - key_len - 1 : 0;
		char key[THEME_EXTRA_KEY_MAX];
		const char* why = NULL;
		if (!eq) why = "isn't key=value";
		else if (key_len >= sizeof(key)) why = "has a key over 63 bytes";
		else {
			memcpy(key, start, key_len);
			key[key_len] = '\0';
			if (!ThemeModel_extraKeyOk(key)) why = "has no <plugin>.<name> key";
			else if (value_len > MOSAIC_THEME_EXTRA_VALUE_MAX) why = "has a value over 255 bytes";
			else {
				for (size_t i = 0; i < value_len && !why; i++) {
					unsigned char c = (unsigned char)eq[1 + i];
					if (c < 0x20 || c == 0x7f) why = "has a control character in its value";
				}
			}
		}
		int slot = why ? -1 : extraSlot(out, key);
		if (!why && slot < 0) why = "is past the first 64 keys";
		if (why) {
			refuse(refused, "extra line %d (%s) %s", number, shown, why);
			count++;
			continue;
		}
		snprintf(out->keys[slot], sizeof(out->keys[slot]), "%s", key);
		memcpy(out->values[slot], eq + 1, value_len);
		out->values[slot][value_len] = '\0';
	}
	return count;
}

const char* ThemeModel_extraValue(const ThemeExtras* extras, const char* key) {
	if (!extras || !key) return NULL;
	for (int i = 0; i < extras->count; i++) {
		if (!strcmp(extras->keys[i], key)) return extras->values[i];
	}
	return NULL;
}
