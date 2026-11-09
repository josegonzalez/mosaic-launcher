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

const int TP_STOCK_COLORS[MOSAIC_THEME_COLORS] = {
	0xffffff, 0x9b2257, 0x1e2329, 0xffffff, 0x000000, 0xffffff, 0x000000,
};

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
		char key[32];
		char value[TP_PATH_MAX + 32];
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
		else say(log, ctx, "line %d: unknown key %s, ignored", number, key);
	}
	return 0;
}

static void nameFromFile(const char* file_name, char* out, size_t size) {
	size_t len = strlen(file_name);
	if (len > 4 && !strcasecmp(file_name + len - 4, ".txt")) len -= 4;
	if (len >= size) len = size - 1;
	for (size_t i = 0; i < len; i++) out[i] = file_name[i] == '_' ? ' ' : file_name[i];
	out[len] = '\0';
}

int TP_parsePalette(const char* text, const char* file_name, TPPalette* out) {
	memset(out, 0, sizeof(*out));
	memcpy(out->colors, TP_STOCK_COLORS, sizeof(out->colors));
	const char* line = text ? text : "";
	while (*line) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		char whole[256];
		trimmed(line, len, whole, sizeof(whole));
		line = end ? end + 1 : line + len;
		char* eq = strchr(whole, '=');
		if (!eq) continue;
		*eq = '\0';
		const char* value = eq + 1;
		if (!strcmp(whole, "version")) {
			int version = 0;
			if (TP_parseInt(value, 0, 9999, &version) == 0 && version > TP_VERSION_MAX) return -1;
		}
		else if (!strcmp(whole, "name")) copyText(out->name, sizeof(out->name), value);
		else if (!strncmp(whole, "color", 5) && whole[5] >= '1' && whole[5] <= '7' && !whole[6]) {
			int color = TP_parseColor(value);
			if (color >= 0) out->colors[whole[5] - '1'] = color;
		}
	}
	if (!out->name[0]) nameFromFile(file_name ? file_name : "", out->name, sizeof(out->name));
	return out->name[0] ? 0 : -1;
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

int TP_resolve(const char* dir, const char* rel, char* out, size_t size, const char** why) {
	static const char* reason;
	if (!why) why = &reason;
	if (!TP_relativePathOk(rel)) {
		*why = "isn't a path inside the theme's folder";
		return -1;
	}
	char root[PATH_MAX];
	if (!dir || !realpath(dir, root)) {
		*why = "has no folder";
		return -1;
	}
	char path[PATH_MAX * 2];
	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	char real[PATH_MAX];
	if (!realpath(path, real)) {
		*why = "doesn't exist";
		return -1;
	}
	size_t len = strlen(root);
	if (strncmp(real, root, len) != 0 || real[len] != '/') {
		*why = "is outside the theme's folder";
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

static int compareEntries(const void* a, const void* b) {
	const TPEntry* x = a;
	const TPEntry* y = b;
	int order = strcasecmp(x->label, y->label);
	return order ? order : strcmp(x->id, y->id);
}

static int listed(const TPCatalog* catalog, const char* id) {
	if (!strcasecmp(id, "stock")) return 1;
	for (int i = 0; i < catalog->theme_count; i++) {
		if (!strcasecmp(catalog->themes[i].id, id)) return 1;
	}
	return 0;
}

static void addEntry(TPCatalog* catalog, int kind, const char* id, const char* label, const char* dir) {
	if (listed(catalog, id)) return;
	if (catalog->theme_count >= TP_THEMES_MAX) {
		catalog->dropped++;
		return;
	}
	TPEntry* entry = &catalog->themes[catalog->theme_count++];
	entry->kind = kind;
	copyText(entry->id, sizeof(entry->id), id);
	copyText(entry->label, sizeof(entry->label), label && label[0] ? label : id);
	copyText(entry->dir, sizeof(entry->dir), dir ? dir : "");
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

static void scanPalettes(TPCatalog* catalog, const char* dir_path) {
	DIR* dir = dir_path ? opendir(dir_path) : NULL;
	if (!dir) return;
	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		size_t len = strlen(ent->d_name);
		if (ent->d_name[0] == '.' || len < 5 || strcasecmp(ent->d_name + len - 4, ".txt")) continue;
		char file[TP_PATH_MAX];
		if ((size_t)snprintf(file, sizeof(file), "%s/%s", dir_path, ent->d_name) >= sizeof(file)) continue;
		char* text = TP_readFile(file);
		if (!text) continue;
		TPPalette palette;
		int ok = TP_parsePalette(text, ent->d_name, &palette) == 0;
		free(text);
		if (!ok) continue;
		if (catalog->palette_count >= TP_PALETTES_MAX) {
			catalog->dropped++;
			continue;
		}
		catalog->palettes[catalog->palette_count++] = palette;
	}
	closedir(dir);
}

void TP_catalog(TPCatalog* catalog, const char* themes_dir, const char* bundled_dir,
		const char* user_palettes, const char* builtin_palettes) {
	memset(catalog, 0, sizeof(*catalog));
	scanThemes(catalog, themes_dir, TP_KIND_CARD);
	scanThemes(catalog, bundled_dir, TP_KIND_BUNDLED);
	scanPalettes(catalog, user_palettes);
	scanPalettes(catalog, builtin_palettes);
	int first = catalog->theme_count;
	for (int i = 0; i < catalog->palette_count; i++) {
		addEntry(catalog, TP_KIND_PALETTE, catalog->palettes[i].name, catalog->palettes[i].name, NULL);
	}
	qsort(catalog->themes + first, (size_t)(catalog->theme_count - first), sizeof(TPEntry), compareEntries);
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
}
