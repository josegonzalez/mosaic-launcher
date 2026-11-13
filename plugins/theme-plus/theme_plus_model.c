#include "theme_plus_model.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static void say(TPLogFn log, void* ctx, const char* format, ...) {
	if (!log) return;
	char line[320];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	log(ctx, line);
}

// Copies text into out, cut to fit size.
static void copyText(char* out, size_t size, const char* text) {
	size_t len = strlen(text);
	if (len >= size) len = size - 1;
	memcpy(out, text, len);
	out[len] = '\0';
}

// Copies text without the spaces and tabs around it, up to size.
static void trimmed(const char* text, size_t len, char* out, size_t size) {
	while (len && (*text == ' ' || *text == '\t')) {
		text++;
		len--;
	}
	while (len && (text[len - 1] == ' ' || text[len - 1] == '\t' || text[len - 1] == '\r')) len--;
	if (len >= size) len = size - 1;
	memcpy(out, text, len);
	out[len] = '\0';
}

int TP_parseColor(const char* text) {
	if (!text) return -1;
	const char* p = text;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
	else if (p[0] == '#') p++;
	size_t digits = 0;
	while (isxdigit((unsigned char)p[digits])) digits++;
	if (p[digits] != '\0' || (digits != 6 && digits != 8)) return -1;
	unsigned long value = strtoul(p, NULL, 16);
	if (digits == 8) value >>= 8;
	return (int)(value & 0xffffff);
}

int TP_parseContrast(const char* text) {
	if (!text || !isdigit((unsigned char)text[0])) return -1;
	int whole = 0;
	const char* p = text;
	int digits = 0;
	while (isdigit((unsigned char)*p)) {
		if (++digits > 2) return -1;
		whole = whole * 10 + (*p++ - '0');
	}
	int tenths = whole * 10;
	if (*p == '.') {
		p++;
		if (!isdigit((unsigned char)*p)) return -1;
		tenths += *p++ - '0';
	}
	if (*p != '\0' || tenths < 10 || tenths > 210) return -1;
	return tenths;
}

int TP_parseInt(const char* text, int lo, int hi, int* out) {
	if (!text) return -1;
	const char* p = text;
	int sign = 1;
	if (*p == '-' || *p == '+') sign = *p++ == '-' ? -1 : 1;
	if (!isdigit((unsigned char)*p)) return -1;
	int value = 0;
	int digits = 0;
	while (isdigit((unsigned char)*p)) {
		if (++digits > 4) return -1;
		value = value * 10 + (*p++ - '0');
	}
	if (*p != '\0') return -1;
	value *= sign;
	if (value < lo || value > hi) return -1;
	*out = value;
	return 0;
}

int TP_parseHintStyle(const char* text) {
	static const char* names[] = { "text", "abxy", "cardinals", "shapes" };
	for (int i = 0; text && i < 4; i++) {
		if (!strcasecmp(text, names[i])) return MOSAIC_HINT_STYLE_TEXT + i;
	}
	return -1;
}

int TP_relativePathOk(const char* path) {
	if (!path || !path[0] || path[0] == '/' || strlen(path) >= TP_PATH_MAX || strchr(path, '\\')) return 0;
	const char* part = path;
	for (;;) {
		const char* end = strchr(part, '/');
		size_t len = end ? (size_t)(end - part) : strlen(part);
		if (len == 0) return 0;
		if (len == 1 && part[0] == '.') return 0;
		if (len == 2 && part[0] == '.' && part[1] == '.') return 0;
		if (!end) return 1;
		part = end + 1;
	}
}

static void themeDefaults(TPTheme* out) {
	memset(out, 0, sizeof(*out));
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) out->colors[i] = MOSAIC_THEME_UNSET;
	out->text_step = TP_STEP_UNSET;
}

static int setPath(char* field, const char* key, const char* value, int line, TPLogFn log, void* ctx) {
	if (!TP_relativePathOk(value)) {
		say(log, ctx, "line %d: %s (%s) isn't a path inside the theme's folder, dropped", line, key, value);
		return -1;
	}
	memcpy(field, value, strlen(value) + 1);
	return 0;
}

static int setInt(int* field, const char* key, const char* value, int lo, int hi, int line, TPLogFn log, void* ctx) {
	if (TP_parseInt(value, lo, hi, field) != 0) {
		say(log, ctx, "line %d: %s %s isn't from %d to %d, dropped", line, key, value, lo, hi);
		return -1;
	}
	return 0;
}

int TP_parseTheme(const char* text, TPTheme* out, TPLogFn log, void* ctx) {
	themeDefaults(out);
	if (!text) return 0;
	int number = 0;
	const char* line = text;
	while (*line) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		const char* next = end ? end + 1 : line + len;
		number++;
		char whole[TP_PATH_MAX + 64];
		const char* start = line;
		line = next;
		if (len >= sizeof(whole)) {
			say(log, ctx, "line %d is too long, ignored", number);
			continue;
		}
		trimmed(start, len, whole, sizeof(whole));
		if (!whole[0] || whole[0] == '#') continue;
		char* eq = strchr(whole, '=');
		if (!eq) {
			say(log, ctx, "line %d isn't key=value, ignored", number);
			continue;
		}
		char key[sizeof(whole)];
		char value[sizeof(whole)];
		trimmed(whole, (size_t)(eq - whole), key, sizeof(key));
		trimmed(eq + 1, strlen(eq + 1), value, sizeof(value));

		if (!strcmp(key, "version")) {
			int version = 0;
			if (TP_parseInt(value, 1, 9999, &version) != 0) {
				say(log, ctx, "line %d: version %s isn't a number, dropped", number, value);
			}
			else if (version > TP_VERSION_MAX) {
				say(log, ctx, "version %d is newer than this plugin reads (%d), so the theme isn't used", version, TP_VERSION_MAX);
				return -1;
			}
		}
		else if (!strcmp(key, "name") || !strcmp(key, "palette")) {
			char* field = key[0] == 'n' ? out->name : out->palette;
			if (!value[0] || strlen(value) >= TP_NAME_MAX) {
				say(log, ctx, "line %d: %s isn't 1 to %d characters, dropped", number, key, TP_NAME_MAX - 1);
			}
			else copyText(field, TP_NAME_MAX, value);
		}
		else if (!strncmp(key, "color", 5) && key[5] >= '1' && key[5] <= '7' && !key[6]) {
			int color = TP_parseColor(value);
			if (color < 0) say(log, ctx, "line %d: %s %s isn't 0xRRGGBB or 0xRRGGBBAA, dropped", number, key, value);
			else out->colors[key[5] - '1'] = color;
		}
		else if (!strcmp(key, "font")) setPath(out->font, key, value, number, log, ctx);
		else if (!strcmp(key, "wallpaper")) setPath(out->wallpaper, key, value, number, log, ctx);
		else if (!strcmp(key, "wallpaper_list")) setPath(out->wallpaper_list, key, value, number, log, ctx);
		else if (!strcmp(key, "wallpaper_quick")) setPath(out->wallpaper_quick, key, value, number, log, ctx);
		else if (!strcmp(key, "fallback_font")) setPath(out->fallback_font, key, value, number, log, ctx);
		else if (!strcmp(key, "text_step")) {
			int step = TP_STEP_UNSET;
			if (setInt(&step, key, value, -2, 8, number, log, ctx) == 0) out->text_step = step;
		}
		else if (!strcmp(key, "contrast")) {
			int contrast = TP_parseContrast(value);
			if (contrast < 0) say(log, ctx, "line %d: contrast %s isn't a ratio from 1 to 21, dropped", number, value);
			else out->contrast = contrast;
		}
		else if (!strcmp(key, "pill_size")) setInt(&out->pill_size, key, value, 18, 60, number, log, ctx);
		else if (!strcmp(key, "button_size")) setInt(&out->button_size, key, value, 12, 56, number, log, ctx);
		else if (!strcmp(key, "padding")) setInt(&out->padding, key, value, 1, 30, number, log, ctx);
		else if (!strcmp(key, "hint_style")) {
			int style = TP_parseHintStyle(value);
			if (style < 0) say(log, ctx, "line %d: hint_style %s isn't text, abxy, cardinals or shapes, dropped", number, value);
			else out->hint_style = style;
		}
		else if (strchr(key, '.')) {
			// another plugin's, for the launcher to check and pass on
			size_t used = strlen(out->extra);
			size_t len = strlen(key) + 1 + strlen(value) + 1;
			if (used + len >= sizeof(out->extra)) {
				say(log, ctx, "line %d: %s doesn't fit in the %d bytes of keys for other plugins, dropped", number, key,
					(int)sizeof(out->extra) - 1);
			}
			else snprintf(out->extra + used, sizeof(out->extra) - used, "%s=%s\n", key, value);
		}
		else say(log, ctx, "line %d: unknown key %s, ignored", number, key);
	}
	return 0;
}

char* TP_readFile(const char* path) {
	struct stat st;
	if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > TP_FILE_MAX) return NULL;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	char* text = malloc((size_t)st.st_size + 1);
	size_t got = text ? fread(text, 1, (size_t)st.st_size, f) : 0;
	fclose(f);
	if (!text) return NULL;
	text[got] = '\0';
	return text;
}

// A regular file at path whose real path is inside root_dir's real path,
// which why calls place.
static int inside(const char* root_dir, const char* path, const char* place, char* out, size_t size, const char** why) {
	static char outside[64];
	char root[PATH_MAX];
	if (!root_dir || !realpath(root_dir, root)) {
		*why = "has no folder";
		return -1;
	}
	char real[PATH_MAX];
	if (!realpath(path, real)) {
		*why = "doesn't exist";
		return -1;
	}
	size_t len = strlen(root);
	if (strncmp(real, root, len) != 0 || real[len] != '/') {
		snprintf(outside, sizeof(outside), "is outside %s", place);
		*why = outside;
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
	copyText(out, size, real);
	return 0;
}

int TP_resolve(const char* dir, const char* rel, char* out, size_t size, const char** why) {
	static const char* reason;
	if (!why) why = &reason;
	if (!TP_relativePathOk(rel)) {
		*why = "isn't a path inside the theme's folder";
		return -1;
	}
	char path[PATH_MAX * 2];
	snprintf(path, sizeof(path), "%s/%s", dir ? dir : "", rel);
	return inside(dir, path, "the theme's folder", out, size, why);
}

int TP_cardFile(const char* card, const char* path, char* out, size_t size, const char** why) {
	static const char* reason;
	if (!why) why = &reason;
	if (!path || !path[0] || strlen(path) >= TP_PATH_MAX) {
		*why = "isn't a path on the card";
		return -1;
	}
	char full[PATH_MAX * 2];
	if (path[0] == '/') snprintf(full, sizeof(full), "%s", path);
	else snprintf(full, sizeof(full), "%s/%s", card ? card : "", path);
	return inside(card, full, "the card", out, size, why);
}

static int compareEntries(const void* a, const void* b) {
	const TPEntry* x = a;
	const TPEntry* y = b;
	int order = strcasecmp(x->label, y->label);
	return order ? order : strcmp(x->id, y->id);
}

// 1 when id is "stock" or an earlier entry's, which the setting would
// name instead: ignoring case for a theme's folder, exactly for a palette,
// as TP_find looks for an exact id before one ignoring case
static int listed(const TPCatalog* catalog, const char* id, int exact) {
	if (!strcasecmp(id, "stock")) return 1;
	for (int i = 0; i < catalog->theme_count; i++) {
		if (exact ? !strcmp(catalog->themes[i].id, id) : !strcasecmp(catalog->themes[i].id, id)) return 1;
	}
	return 0;
}

static TPEntry* addEntry(TPCatalog* catalog, int kind, const char* id, const char* label, const char* dir) {
	if (listed(catalog, id, kind == TP_KIND_PALETTE)) return NULL;
	if (catalog->theme_count >= TP_THEMES_MAX) {
		catalog->dropped++;
		return NULL;
	}
	TPEntry* entry = &catalog->themes[catalog->theme_count++];
	entry->kind = kind;
	copyText(entry->id, sizeof(entry->id), id);
	copyText(entry->label, sizeof(entry->label), label && label[0] ? label : id);
	copyText(entry->dir, sizeof(entry->dir), dir ? dir : "");
	return entry;
}

static void scanThemes(TPCatalog* catalog, const char* dir_path, int kind) {
	DIR* dir = dir_path ? opendir(dir_path) : NULL;
	if (!dir) return;
	int first = catalog->theme_count;
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.' || strlen(ent->d_name) >= TP_NAME_MAX) continue;
		char folder[TP_PATH_MAX];
		if ((size_t)snprintf(folder, sizeof(folder), "%s/%s", dir_path, ent->d_name) >= sizeof(folder)) continue;
		struct stat st;
		if (lstat(folder, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		char file[TP_PATH_MAX + 16];
		snprintf(file, sizeof(file), "%s/theme.txt", folder);
		char* text = TP_readFile(file);
		if (!text) continue;
		TPTheme theme;
		int ok = TP_parseTheme(text, &theme, NULL, NULL) == 0;
		free(text);
		if (ok) addEntry(catalog, kind, ent->d_name, theme.name, folder);
	}
	closedir(dir);
	qsort(catalog->themes + first, (size_t)(catalog->theme_count - first), sizeof(TPEntry), compareEntries);
}

void TP_catalog(TPCatalog* catalog, const char* themes_dir, const char* bundled_dir,
		const TPPalette* palettes, int palette_count, int selected) {
	memset(catalog, 0, sizeof(*catalog));
	scanThemes(catalog, themes_dir, TP_KIND_CARD);
	scanThemes(catalog, bundled_dir, TP_KIND_BUNDLED);
	for (int i = 0; palettes && i < palette_count; i++) {
		if (catalog->palette_count >= TP_PALETTES_MAX) {
			catalog->dropped++;
			continue;
		}
		catalog->palettes[catalog->palette_count++] = palettes[i];
		TPEntry* entry = addEntry(catalog, TP_KIND_PALETTE, palettes[i].name, palettes[i].name, NULL);
		if (entry && i == selected) entry->settings = 1;
	}
}

const TPEntry* TP_find(const TPCatalog* catalog, const char* id) {
	if (!id || !id[0]) return NULL;
	for (int i = 0; i < catalog->theme_count; i++) {
		if (!strcmp(catalog->themes[i].id, id)) return &catalog->themes[i];
	}
	for (int i = 0; i < catalog->theme_count; i++) {
		if (!strcasecmp(catalog->themes[i].id, id)) return &catalog->themes[i];
	}
	return NULL;
}

const TPPalette* TP_findPalette(const TPCatalog* catalog, const char* name) {
	if (!name || !name[0]) return NULL;
	for (int i = 0; i < catalog->palette_count; i++) {
		if (!strcmp(catalog->palettes[i].name, name)) return &catalog->palettes[i];
	}
	for (int i = 0; i < catalog->palette_count; i++) {
		if (!strcasecmp(catalog->palettes[i].name, name)) return &catalog->palettes[i];
	}
	return NULL;
}

static int keepsTheme(const char* value) {
	return !value || !value[0] || !strcmp(value, "theme");
}

TPOverrides TP_overrides(const char* background, const char* text_size, const char* contrast,
		const char* ui_size, const char* margins, TPLogFn log, void* ctx) {
	TPOverrides out = { -1, TP_STEP_UNSET, -1, -1, -1 };
	if (!keepsTheme(background)) {
		out.background = TP_parseColor(background);
		if (out.background < 0) say(log, ctx, "background %s isn't 0xRRGGBB, so the theme's is used", background);
	}
	if (!keepsTheme(text_size)) {
		static const struct { const char* name; int step; } steps[] = {
			{ "small", -2 }, { "stock", 0 }, { "large", 2 }, { "larger", 4 }, { "largest", 6 },
		};
		for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
			if (!strcmp(text_size, steps[i].name)) out.text_step = steps[i].step;
		}
		if (out.text_step == TP_STEP_UNSET) say(log, ctx, "text_size %s isn't known, so the theme's is used", text_size);
	}
	if (!keepsTheme(contrast)) {
		out.contrast = !strcmp(contrast, "off") ? 0 : TP_parseContrast(contrast);
		if (out.contrast < 0) say(log, ctx, "contrast %s isn't off or a ratio from 1 to 21, so the theme's is used", contrast);
	}
	if (!keepsTheme(ui_size)) {
		static const struct { const char* name; int pill; } sizes[] = {
			{ "stock", 0 }, { "compact", 24 }, { "large", 36 }, { "larger", 42 },
		};
		for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
			if (!strcmp(ui_size, sizes[i].name)) out.ui = sizes[i].pill;
		}
		if (out.ui < 0) say(log, ctx, "ui_size %s isn't known, so the theme's is used", ui_size);
	}
	if (!keepsTheme(margins)) {
		if (!strcmp(margins, "auto")) out.padding = 0;
		else if (TP_parseInt(margins, 1, 30, &out.padding) != 0) {
			out.padding = -1;
			say(log, ctx, "margins %s isn't auto or from 1 to 30, so the theme's is used", margins);
		}
	}
	return out;
}

void TP_overrideFallback(TPOverrides* overrides, const char* card, const char* value, TPLogFn log, void* ctx) {
	overrides->fallback_font[0] = '\0';
	if (!value || !value[0]) return;
	const char* why = "";
	if (TP_cardFile(card, value, overrides->fallback_font, sizeof(overrides->fallback_font), &why) != 0) {
		overrides->fallback_font[0] = '\0';
		say(log, ctx, "fallback_font %s %s, so the theme's is used", value, why);
	}
}

int TP_buttonFor(int pill) {
	return pill > 0 ? pill * 2 / 3 : 0;
}

static void applyPath(char* field, size_t size, const char* key, const char* dir, const char* rel,
		TPLogFn log, void* ctx) {
	if (!rel[0]) return;
	const char* why = "";
	char real[TP_PATH_MAX];
	if (TP_resolve(dir, rel, real, sizeof(real), &why) != 0) {
		say(log, ctx, "%s (%s) %s, dropped", key, rel, why);
		return;
	}
	copyText(field, size, real);
}

void TP_apply(MosaicTheme* theme, const TPTheme* parsed, const char* dir, const TPPalette* palette,
		const TPOverrides* overrides, TPLogFn log, void* ctx) {
	if (MOSAIC_THEME_HAS(theme, colors)) {
		if (palette) memcpy(theme->colors, palette->colors, sizeof(theme->colors));
		for (int i = 0; parsed && i < MOSAIC_THEME_COLORS; i++) {
			if (parsed->colors[i] != MOSAIC_THEME_UNSET) theme->colors[i] = parsed->colors[i];
		}
		if (overrides && overrides->background >= 0) theme->colors[MOSAIC_THEME_COLORS - 1] = overrides->background;
	}
	if (parsed && dir) {
		if (MOSAIC_THEME_HAS(theme, font)) applyPath(theme->font, sizeof(theme->font), "font", dir, parsed->font, log, ctx);
		if (MOSAIC_THEME_HAS(theme, wallpaper)) applyPath(theme->wallpaper, sizeof(theme->wallpaper), "wallpaper", dir, parsed->wallpaper, log, ctx);
		if (MOSAIC_THEME_HAS(theme, wallpaper_list)) {
			applyPath(theme->wallpaper_list, sizeof(theme->wallpaper_list), "wallpaper_list", dir, parsed->wallpaper_list, log, ctx);
		}
		if (MOSAIC_THEME_HAS(theme, wallpaper_quick)) {
			applyPath(theme->wallpaper_quick, sizeof(theme->wallpaper_quick), "wallpaper_quick", dir, parsed->wallpaper_quick, log, ctx);
		}
	}
	if (MOSAIC_THEME_HAS(theme, fallback_font)) {
		if (overrides && overrides->fallback_font[0]) {
			copyText(theme->fallback_font, sizeof(theme->fallback_font), overrides->fallback_font);
		}
		else if (parsed && dir) {
			applyPath(theme->fallback_font, sizeof(theme->fallback_font), "fallback_font", dir, parsed->fallback_font, log, ctx);
		}
	}
	int step = parsed ? parsed->text_step : TP_STEP_UNSET;
	if (overrides && overrides->text_step != TP_STEP_UNSET) step = overrides->text_step;
	if (step != TP_STEP_UNSET && MOSAIC_THEME_HAS(theme, text_step)) theme->text_step = step;

	int contrast = parsed ? parsed->contrast : 0;
	if (overrides && overrides->contrast >= 0) contrast = overrides->contrast;
	if (MOSAIC_THEME_HAS(theme, contrast)) theme->contrast = contrast;

	int pill = parsed ? parsed->pill_size : 0;
	int button = parsed ? parsed->button_size : 0;
	int padding = parsed ? parsed->padding : 0;
	if (overrides && overrides->ui >= 0) {
		// the theme's own padding and button were picked for its pill
		pill = overrides->ui;
		button = TP_buttonFor(pill);
		padding = 0;
	}
	if (overrides && overrides->padding >= 0) padding = overrides->padding;
	if (MOSAIC_THEME_HAS(theme, pill_size)) theme->pill_size = pill;
	if (MOSAIC_THEME_HAS(theme, button_size)) theme->button_size = button;
	if (MOSAIC_THEME_HAS(theme, padding)) theme->padding = padding;

	if (parsed && parsed->hint_style && MOSAIC_THEME_HAS(theme, hint_style)) theme->hint_style = parsed->hint_style;
	if (parsed && MOSAIC_THEME_HAS(theme, extra)) copyText(theme->extra, sizeof(theme->extra), parsed->extra);
}
