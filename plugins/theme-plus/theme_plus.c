// Theme plus: owns the theme slot. The theme setting names a theme: a
// folder <sd>/Themes/<name>/ with a theme.txt (and its own font and
// wallpapers if it likes), one of the folders this plugin ships in its
// themes/, or one of stock's palettes as a colours-only theme, which the
// host lists as stock's Settings does. "stock" changes nothing. The background, text_size, contrast, ui_size, margins
// and fallback_font settings override the theme's values, over stock's
// too.
//
// The theme-plus.pick action lists them with choose, from a Theme tile on
// the quick menu (the "tile" setting) or a game list button (the "button"
// setting), the palette the user picked in Settings marked; picking one
// saves the setting and reloads the lists, which asks for the theme
// again, so it shows at once. The theme-plus.setup service sets it the
// same way, for first-run. Stock's settings are never written.
//
// The rotate setting shows a random theme each start, or one a day, from
// every theme or the ones rotate_between names, in place of the theme
// setting's. art_path gives a system's icon and wallpapers from the icon
// and wallpaper packs (<sd>/Icons/<pack>, <sd>/Wallpapers/<pack>), else
// the theme folder's icons/ and wallpapers/, where stock's own files are
// missing. The picker is a screen of its own with a preview of the theme
// the cursor is on.
//
// The model (theme.txt, paths, the list, the merge) is in
// theme_plus_model.c, and rotation, systems and packs in
// theme_plus_packs.c.

#define _GNU_SOURCE // dladdr
#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "theme_plus_model.h"
#include "theme_plus_packs.h"

#define ID "theme-plus"
#define ACTION_ID ID ".pick"
#define CHOICES_MAX 64 // what choose takes, Stock among them

static const MosaicHost* host;
static TPCatalog catalog;
static char bundledDir[TP_PATH_MAX];
static char pickIds[CHOICES_MAX][TP_NAME_MAX];
static char pickLabels[CHOICES_MAX][TP_NAME_MAX + 32];
static char themeDir[TP_PATH_MAX]; // the folder of the theme in place, or "" for a palette or stock
static int rotatedStart;            // 1 once start's rotation has picked this run
static char rotatedId[TP_NAME_MAX]; // what start's rotation picked, or ""
static char sessionPick[TP_NAME_MAX]; // a theme picked while rotating, shown until the launcher restarts

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
	char line[400];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	host->log(ID, line);
}

// TPLogFn: ctx names what's being read
static void logLine(void* ctx, const char* message) {
	say("%s: %s", (const char*)ctx, message);
}

// The themes/ folder beside lib/<arch>/plugin.so, or "".
static void findBundled(void) {
	bundledDir[0] = '\0';
	Dl_info info;
	if (!dladdr((const void*)&findBundled, &info) || !info.dli_fname) return;
	char path[TP_PATH_MAX];
	if ((size_t)snprintf(path, sizeof(path), "%s", info.dli_fname) >= sizeof(path)) return;
	for (int up = 0; up < 3; up++) {
		char* slash = strrchr(path, '/');
		if (!slash) return;
		*slash = '\0';
	}
	if ((size_t)snprintf(bundledDir, sizeof(bundledDir), "%s/themes", path) >= sizeof(bundledDir)) bundledDir[0] = '\0';
}

// stock's palettes, as the host lists them
static TPPalette palettes[TP_PALETTES_MAX];

static void loadCatalog(void) {
	char themes[TP_PATH_MAX];
	snprintf(themes, sizeof(themes), "%s/Themes", host->sdcard_path);
	int count = 0;
	int selected = -1;
	if (MOSAIC_HOST_HAS(host, palette_selected)) {
		int listed = host->palettes_count();
		for (int i = 0; i < listed && count < TP_PALETTES_MAX; i++) {
			MosaicPalette p = {.size = sizeof(p)};
			if (host->palette_info(i, &p) != 0) break;
			snprintf(palettes[count].name, sizeof(palettes[count].name), "%s", p.name);
			memcpy(palettes[count].colors, p.colors, sizeof(palettes[count].colors));
			count++;
		}
		selected = host->palette_selected();
	}
	TP_catalog(&catalog, themes, bundledDir[0] ? bundledDir : NULL, palettes, count, selected);
	if (catalog.dropped) say("%d themes or palettes past the first %d are left out", catalog.dropped, TP_THEMES_MAX);
}

static const char* kindName(int kind) {
	return kind == TP_KIND_CARD ? "card" : kind == TP_KIND_BUNDLED ? "bundled" : "palette";
}

// The theme a rotation shows now, or NULL when it's off or has nothing
// to pick from, so the theme setting's is used. Start picks once a run,
// never the one it picked the run before when it can help it; daily
// picks the same one all day.
static const char* rotation(void) {
	int mode = TP_rotateMode(host->cfg_get("rotate", "off"));
	if (mode == TP_ROTATE_OFF) return NULL;
	if (sessionPick[0]) return sessionPick;
	if (mode == TP_ROTATE_START && rotatedStart) return rotatedId[0] ? rotatedId : NULL;
	int candidates[TP_THEMES_MAX];
	int count = TP_rotateCandidates(&catalog, host->cfg_get("rotate_between", ""), candidates, TP_THEMES_MAX, logLine,
		(void*)"settings");
	if (mode == TP_ROTATE_START) rotatedStart = 1;
	if (count == 0) {
		say("rotate: no themes to pick from, so the theme setting's is used");
		return NULL;
	}
	time_t now = time(NULL);
	struct tm local;
	long day = localtime_r(&now, &local) ? TP_dayKey(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) : 0;
	int last = -1;
	if (mode == TP_ROTATE_START) {
		const char* before = host->cfg_get("rotated", "");
		for (int i = 0; i < count && last < 0; i++) {
			if (!strcmp(catalog.themes[candidates[i]].id, before)) last = i;
		}
	}
	unsigned long random = (unsigned long)now ^ ((unsigned long)getpid() << 16) ^ (unsigned long)clock();
	int picked = TP_rotatePick(mode, count, day, random, last);
	if (picked < 0) return NULL;
	const char* id = catalog.themes[candidates[picked]].id;
	if (mode == TP_ROTATE_START) {
		snprintf(rotatedId, sizeof(rotatedId), "%s", id);
		host->cfg_set("rotated", id);
		say("rotate: %s, of %d", id, count);
		return rotatedId;
	}
	static char daily[TP_NAME_MAX];
	if (strcmp(daily, id) != 0) say("rotate: %s for %ld, of %d", id, day, count);
	snprintf(daily, sizeof(daily), "%s", id);
	return daily;
}

// Logs the wallpaper and icon packs the settings name, and any that
// can't be used.
static void checkPacks(void) {
	static const char* const settings[2][2] = { { "wallpaper_pack", "Wallpapers" }, { "icon_pack", "Icons" } };
	for (int i = 0; i < 2; i++) {
		const char* pack = host->cfg_get(settings[i][0], "");
		if (!pack[0]) continue;
		char dir[TP_PATH_MAX];
		struct stat st;
		snprintf(dir, sizeof(dir), "%s/%s/%s", host->sdcard_path, settings[i][1], pack);
		if (!TP_packNameOk(pack)) say("%s %s isn't a folder's name, so none is used", settings[i][0], pack);
		else if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) say("%s: %s isn't a folder, so none is used", settings[i][0], dir);
		else say("%s %s", settings[i][0], pack);
	}
}

static int theme(MosaicTheme* t) {
	loadCatalog();
	checkPacks();
	themeDir[0] = '\0';
	const char* name = rotation();
	if (!name) name = host->cfg_get("theme", "stock");
	TPOverrides overrides = TP_overrides(host->cfg_get("background", "theme"), host->cfg_get("text_size", "theme"),
		host->cfg_get("contrast", "theme"), host->cfg_get("ui_size", "theme"), host->cfg_get("margins", "theme"),
		logLine, (void*)"settings");
	TP_overrideFallback(&overrides, host->sdcard_path, host->cfg_get("fallback_font", ""), logLine, (void*)"settings");
	const TPEntry* entry = NULL;
	if (strcmp(name, "stock") != 0) {
		entry = TP_find(&catalog, name);
		if (!entry) say("no theme or palette is called %s, so stock's is used", name);
	}
	if (!entry) {
		TP_apply(t, NULL, NULL, NULL, &overrides, logLine, (void*)"settings");
		say("theme stock");
		return 0;
	}
	if (entry->kind == TP_KIND_PALETTE) {
		TP_apply(t, NULL, NULL, TP_findPalette(&catalog, entry->id), &overrides, logLine, (void*)"settings");
		say("theme %s (palette)", entry->id);
		return 0;
	}
	char file[TP_PATH_MAX + 16];
	snprintf(file, sizeof(file), "%s/theme.txt", entry->dir);
	char* text = TP_readFile(file);
	TPTheme parsed;
	int ok = text && TP_parseTheme(text, &parsed, logLine, file) == 0;
	free(text);
	if (!ok) {
		say("%s couldn't be read, so stock's theme is used", file);
		TP_apply(t, NULL, NULL, NULL, &overrides, logLine, (void*)"settings");
		return 0;
	}
	const TPPalette* palette = NULL;
	if (parsed.palette[0]) {
		palette = TP_findPalette(&catalog, parsed.palette);
		if (!palette) say("%s: no palette is called %s, so it's left out", file, parsed.palette);
	}
	TP_apply(t, &parsed, entry->dir, palette, &overrides, logLine, file);
	snprintf(themeDir, sizeof(themeDir), "%s", entry->dir);
	say("theme %s (%s%s%s)", entry->id, kindName(entry->kind), palette ? ", palette " : "", palette ? palette->name : "");
	return 0;
}

// art_path: a system's wallpaper or icon from the picked pack, else the
// theme's own folder, where the card has none of stock's.
static int artPath(const MosaicEntry* entry, int kind, char* out, size_t size) {
	if (!MOSAIC_HOST_HAS(host, entry_path)) return 0;
	const char* path = host->entry_path(entry);
	int type = host->entry_type(entry);
	if (!path) return 0;
	char roms[TP_PATH_MAX];
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	char tag[TP_NAME_MAX];
	int where = TP_systemOf(roms, path, tag, sizeof(tag));
	int art = -1;
	if (kind == MOSAIC_ART_THUMBNAIL && type == MOSAIC_ENTRY_DIR && where == 0) art = TP_ART_ICON;
	else if (kind == MOSAIC_ART_FOLDER_BG && type == MOSAIC_ENTRY_DIR && where >= 0) art = TP_ART_FOLDER_BG;
	else if (kind == MOSAIC_ART_LIST_BG && type != MOSAIC_ENTRY_DIR && where == 1) art = TP_ART_LIST_BG;
	if (art < 0) return 0;
	const char* setting = art == TP_ART_ICON ? "icon_pack" : "wallpaper_pack";
	const char* pack = host->cfg_get(setting, "");
	char packDir[TP_PATH_MAX];
	packDir[0] = '\0';
	if (pack[0] && TP_packNameOk(pack)) {
		snprintf(packDir, sizeof(packDir), "%s/%s/%s", host->sdcard_path, art == TP_ART_ICON ? "Icons" : "Wallpapers", pack);
	}
	char ownDir[TP_PATH_MAX + 16];
	ownDir[0] = '\0';
	if (themeDir[0]) snprintf(ownDir, sizeof(ownDir), "%s/%s", themeDir, art == TP_ART_ICON ? "icons" : "wallpapers");
	if (!packDir[0] && !ownDir[0]) return 0;
	char stock[2][TP_PATH_MAX];
	int stock_count = TP_stockArt(art, host->sdcard_path, path, stock, 2);
	return TP_systemArt(stock, stock_count, packDir, ownDir, tag, out, size);
}

static void picked(int index, void* data) {
	(void)data;
	if (index < 0 || index >= CHOICES_MAX) return;
	host->cfg_set("theme", pickIds[index]);
	// a rotation shows the pick until the launcher restarts
	if (TP_rotateMode(host->cfg_get("rotate", "off")) != TP_ROTATE_OFF) {
		snprintf(sessionPick, sizeof(sessionPick), "%s", pickIds[index]);
	}
	say("picked %s", pickIds[index]);
	host->reload_lists();
}

// Fills pickIds and pickLabels with Stock and the themes, as the picker
// lists them, and returns how many; *cursor is the current theme's.
static int listThemes(int* cursor) {
	loadCatalog();
	const char* current = rotation();
	if (!current) current = host->cfg_get("theme", "stock");
	const TPEntry* selected = TP_find(&catalog, current);
	int count = 0;
	*cursor = 0;
	snprintf(pickIds[count], TP_NAME_MAX, "stock");
	snprintf(pickLabels[count], sizeof(pickLabels[count]), "%s", tr("Stock"));
	count++;
	for (int i = 0; i < catalog.theme_count && count < CHOICES_MAX; i++) {
		if (&catalog.themes[i] == selected) *cursor = count;
		snprintf(pickIds[count], TP_NAME_MAX, "%s", catalog.themes[i].id);
		if (catalog.themes[i].settings) {
			// the user's palette from stock's Settings, marked as Settings shows it
			snprintf(pickLabels[count], sizeof(pickLabels[count]), tr("%s (in Settings)"), catalog.themes[i].label);
		} else {
			snprintf(pickLabels[count], sizeof(pickLabels[count]), "%s", catalog.themes[i].label);
		}
		count++;
	}
	return count;
}

///////////////////////////////////////
// the picker: the themes on the left, and a preview of the one the cursor
// is on (its colours, wallpaper and a sample row) on the right

#define PICKER_ID ID ".picker"

static int pickerCount;
static int pickerCursor;
static int pickerFirst;
static int previewFor = -1;            // the row the preview is of, or -1 for none yet
static int previewColors[MOSAIC_THEME_COLORS];
static MosaicSurface* previewWall;     // its wallpaper, shrunk to the preview, or NULL
static int previewW, previewH;         // the size previewWall was shrunk to fit

static void freePreview(void) {
	if (previewWall) host->surface_free(ID, previewWall);
	previewWall = NULL;
	previewFor = -1;
}

// The colours in use without a theme: the palette the user picked in
// stock's Settings, else what the launcher shows now.
static void baseColors(int* out) {
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++) {
		int rgb = host->theme_color(i + MOSAIC_COLOR_MAIN);
		out[i] = rgb < 0 ? 0 : rgb;
	}
	for (int i = 0; i < catalog.theme_count; i++) {
		if (!catalog.themes[i].settings) continue;
		const TPPalette* own = TP_findPalette(&catalog, catalog.themes[i].id);
		if (own) memcpy(out, own->colors, sizeof(own->colors));
	}
}

// Reads the theme at the picker's row into the preview, its wallpaper
// shrunk to w by h, freeing the one before.
static void loadPreview(int row, int w, int h) {
	freePreview();
	previewFor = row;
	int base[MOSAIC_THEME_COLORS];
	baseColors(base);
	memcpy(previewColors, base, sizeof(base));
	const TPEntry* entry = row > 0 ? TP_find(&catalog, pickIds[row]) : NULL;
	if (!entry) return;
	if (entry->kind == TP_KIND_PALETTE) {
		TP_previewColors(NULL, TP_findPalette(&catalog, entry->id), base, previewColors);
		return;
	}
	char file[TP_PATH_MAX + 16];
	snprintf(file, sizeof(file), "%s/theme.txt", entry->dir);
	char* text = TP_readFile(file);
	TPTheme parsed;
	int ok = text && TP_parseTheme(text, &parsed, NULL, NULL) == 0;
	free(text);
	if (!ok) return;
	const TPPalette* palette = parsed.palette[0] ? TP_findPalette(&catalog, parsed.palette) : NULL;
	TP_previewColors(&parsed, palette, base, previewColors);
	const char* rel = parsed.wallpaper_list[0] ? parsed.wallpaper_list : parsed.wallpaper;
	char wall[TP_PATH_MAX];
	if (rel[0] && w > 0 && h > 0 && MOSAIC_HOST_HAS(host, image_load_fit)
			&& TP_resolve(entry->dir, rel, wall, sizeof(wall), NULL) == 0) {
		previewWall = host->image_load_fit(ID, wall, w, h);
		if (previewWall) host->surface_size(previewWall, &previewW, &previewH);
	}
}

static int pickerEnter(void* data) {
	(void)data;
	pickerCount = listThemes(&pickerCursor);
	if (catalog.theme_count >= CHOICES_MAX) say("only the first %d themes are listed", CHOICES_MAX - 1);
	pickerFirst = 0;
	freePreview();
	return 0;
}

static void pickerExit(void* data) {
	(void)data;
	freePreview();
}

static int bit(unsigned int bits, int button) {
	return (bits & MOSAIC_BUTTON_BIT(button)) != 0;
}

static int pickerInput(const MosaicInput* input, void* data) {
	(void)data;
	if (bit(input->pressed, MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	int before = pickerCursor;
	if (bit(input->repeated, MOSAIC_BUTTON_UP)) {
		pickerCursor = TP_move(pickerCursor, pickerCount, -1, bit(input->pressed, MOSAIC_BUTTON_UP));
	}
	if (bit(input->repeated, MOSAIC_BUTTON_DOWN)) {
		pickerCursor = TP_move(pickerCursor, pickerCount, 1, bit(input->pressed, MOSAIC_BUTTON_DOWN));
	}
	if (bit(input->repeated, MOSAIC_BUTTON_LEFT)) pickerCursor = TP_move(pickerCursor, pickerCount, -5, 0);
	if (bit(input->repeated, MOSAIC_BUTTON_RIGHT)) pickerCursor = TP_move(pickerCursor, pickerCount, 5, 0);
	if (pickerCursor != before) host->request_redraw();
	if (bit(input->pressed, MOSAIC_BUTTON_A) && pickerCursor >= 0) {
		picked(pickerCursor, NULL);
		return MOSAIC_SCREEN_CLOSE;
	}
	return MOSAIC_SCREEN_STAY;
}

static int pickerButtons(MosaicHint* out, int max, void* data) {
	(void)data;
	MosaicHint hints[] = { { MOSAIC_BUTTON_B, tr("BACK") }, { MOSAIC_BUTTON_A, tr("SELECT") } };
	int count = 0;
	for (; count < max && count < 2; count++) out[count] = hints[count];
	return count;
}

static MosaicColor rgb(int value) {
	return mosaic_color((unsigned int)value, 255);
}

// The preview of the theme at the cursor in box: its background colour or
// wallpaper, a selected and a plain sample row, and its seven colours.
static void drawPreview(MosaicSurface* screen, const MosaicRect* box, int scale) {
	if (previewFor != pickerCursor) loadPreview(pickerCursor, box->w, box->h);
	const int* c = previewColors;
	host->fill_round_rect(screen, box, 4 * scale, rgb(c[MOSAIC_COLOR_BACKGROUND - 1]));
	if (previewWall) {
		MosaicRect at = { box->x + (box->w - previewW) / 2, box->y + (box->h - previewH) / 2, previewW, previewH };
		host->blit(screen, previewWall, NULL, &at);
	}
	int pad = 8 * scale;
	int row_h = host->text_height(MOSAIC_FONT_LARGE) + 6 * scale;
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	MosaicRect row = { box->x + pad, box->y + pad, box->w - 2 * pad, row_h };
	host->fill_round_rect(screen, &row, row_h / 2, rgb(c[MOSAIC_COLOR_MAIN - 1]));
	host->draw_text(screen, MOSAIC_FONT_LARGE, pickLabels[pickerCursor], row.x + pad, row.y + (row_h - large_h) / 2,
		rgb(c[MOSAIC_COLOR_LIST_TEXT_SELECTED - 1]), row.w - 2 * pad);
	row.y += row_h;
	host->draw_text(screen, MOSAIC_FONT_LARGE, tr("Sample row"), row.x + pad, row.y + (row_h - large_h) / 2,
		rgb(c[MOSAIC_COLOR_LIST_TEXT - 1]), row.w - 2 * pad);
	// the swatches along the bottom, each outlined in the hint colour
	int gap = 2 * scale;
	int side = (box->w - 2 * pad - (MOSAIC_THEME_COLORS - 1) * gap) / MOSAIC_THEME_COLORS;
	if (side > 20 * scale) side = 20 * scale;
	if (side < 2) return;
	int x = box->x + pad;
	int y = box->y + box->h - pad - side;
	for (int i = 0; i < MOSAIC_THEME_COLORS; i++, x += side + gap) {
		MosaicRect outer = { x, y, side, side };
		host->fill_round_rect(screen, &outer, 2 * scale, rgb(c[MOSAIC_COLOR_HINT - 1]));
		MosaicRect inner = { x + scale, y + scale, side - 2 * scale, side - 2 * scale };
		host->fill_round_rect(screen, &inner, scale, rgb(c[i]));
	}
}

static MosaicColor shown(int index, unsigned int fallback) {
	int value = host->theme_color(index);
	return mosaic_color(value < 0 ? fallback : (unsigned int)value, 255);
}

static int pickerRender(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	int scale = host->screen_scale();
	int pad = 10 * scale;
	const MosaicRect* area = &view->area;
	int top = area->y + area->h - 40 * scale;
	MosaicRect drawn;
	MosaicHint back[] = { { MOSAIC_BUTTON_B, "BACK" } };
	if (host->draw_hints(screen, MOSAIC_HINTS_LEFT, back, 1, &drawn) == 0) top = drawn.y;
	MosaicHint select[] = { { MOSAIC_BUTTON_A, "SELECT" } };
	if (host->draw_hints(screen, MOSAIC_HINTS_RIGHT, select, 1, &drawn) == 0 && drawn.y < top) top = drawn.y;

	// the title, beside the status pill
	int margin = view->status.w ? area->x + area->w - (view->status.x + view->status.w) : pad;
	int bar_y = view->status.w ? view->status.y : margin;
	int bar_h = view->status.w ? view->status.h : 30 * scale;
	int right = view->header.w ? view->header.x : view->status.w ? view->status.x : area->x + area->w - margin;
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	host->draw_text(screen, MOSAIC_FONT_LARGE, tr("Theme"), area->x + margin + pad, bar_y + (bar_h - large_h) / 2,
		shown(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF), right - pad - (area->x + margin + pad));

	int y = bar_y + bar_h + margin / 2;
	int h = top - margin / 2 - y;
	int w = area->w - 2 * margin;
	if (h <= 0 || w <= 0 || pickerCount <= 0) return 0;
	int list_w = w * 11 / 20;
	MosaicRect box = { area->x + margin + list_w + pad, y, w - list_w - pad, h };
	if (box.w > 0) drawPreview(screen, &box, scale);

	int pill = host->ui_size(MOSAIC_UI_PILL);
	int row_h = (pill > 0 ? pill : 30) * scale;
	if (row_h < large_h) row_h = large_h;
	int fit = h / row_h;
	if (fit < 1) return 0;
	pickerFirst = TP_window(pickerCursor, pickerCount, fit, pickerFirst);
	for (int i = pickerFirst; i < pickerCount && i < pickerFirst + fit; i++) {
		MosaicRect r = { area->x + margin, y + (i - pickerFirst) * row_h, list_w, row_h };
		int on = i == pickerCursor;
		if (on) host->fill_round_rect(screen, &r, row_h / 2, shown(MOSAIC_COLOR_MAIN, 0xFFFFFF));
		host->draw_text(screen, MOSAIC_FONT_LARGE, pickLabels[i], r.x + pad, r.y + (row_h - large_h) / 2,
			on ? shown(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000) : shown(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF), r.w - 2 * pad);
	}
	return 0;
}

static int pickerReady; // 1 once the picker screen is registered

static void pick(const char* path, void* data) {
	(void)path;
	(void)data;
	if (pickerReady && host->open_screen(PICKER_ID) == 0) return;
	// a launcher without plugin screens gets the plain list
	const char* labels[CHOICES_MAX];
	int cursor = 0;
	int count = listThemes(&cursor);
	for (int i = 0; i < count; i++) labels[i] = pickLabels[i];
	if (catalog.theme_count >= CHOICES_MAX) say("only the first %d themes are listed", CHOICES_MAX - 1);
	if (host->choose(tr("Theme"), labels, count, cursor, picked, NULL) != 0) say("couldn't list the themes");
}

// theme-plus.setup: theme=<stock or a theme the picker lists> saves it and
// reloads the lists, as picking it does, so it shows at once. Gives the
// theme now and the picker's themes as its choices.
static int provideSetup(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char wanted[TP_NAME_MAX + 1];
	int refused = 0;
	int cursor = 0;
	int count = listThemes(&cursor);
	int len = host->event_value(args, "theme", wanted, sizeof(wanted));
	if (len >= 0) {
		int found = -1;
		for (int i = 0; len < (int)sizeof(wanted) && i < count && found < 0; i++) {
			if (!strcmp(pickIds[i], wanted)) found = i;
		}
		if (found < 0) refused = 1;
		else {
			host->cfg_set("theme", pickIds[found]);
			say("setup: theme %s", pickIds[found]);
			host->reload_lists();
			cursor = found;
		}
	}
	size_t used = 0;
	int listed = 0;
	int n = snprintf(out, size, "theme=%s\n", pickIds[cursor]);
	if (n < 0 || (size_t)n >= size) return -1;
	used = (size_t)n;
	for (int i = 0; i < count; i++) {
		// what's left must hold the last lines too
		n = snprintf(out + used, size - used, "theme.%i=%s\ntheme.label.%i=%s\n", i, pickIds[i], i, pickLabels[i]);
		if (n < 0 || (size_t)n >= size - used || size - used - (size_t)n < 64) break;
		used += (size_t)n;
		listed++;
	}
	n = snprintf(out + used, size - used, "theme.count=%i\nrestart=0%s", listed, refused ? "\nerror=theme" : "");
	return n < 0 || (size_t)n >= size - used;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, ui_size)) {
		say("this launcher has no theme slot");
		return 1;
	}
	findBundled();
	if (MOSAIC_HOST_HAS(host, open_screen) && MOSAIC_HOST_HAS(host, draw_hints) && MOSAIC_HOST_HAS(host, image_load_fit)) {
		MosaicScreenHooks hooks = {
			.size = sizeof(MosaicScreenHooks),
			.enter = pickerEnter,
			.exit = pickerExit,
			.input = pickerInput,
			.render = pickerRender,
			.buttons = pickerButtons,
		};
		pickerReady = host->register_screen(PICKER_ID, &hooks) == 0;
		if (!pickerReady) say("couldn't register the picker, so it's a plain list");
	}
	if (MOSAIC_HOST_HAS(host, call) && host->provide(ID ".setup", provideSetup) != 0) say("couldn't provide " ID ".setup");
	if (host->register_action(ACTION_ID, "Theme", MOSAIC_ACTION_EDITS, NULL, pick, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 0;
	}
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, "quick/theme.png", NULL, NULL) != 0) {
		say("couldn't add the Theme tile");
	}
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* name = host->cfg_get("button", "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, ACTION_ID) != 0) {
			say("couldn't bind the theme button");
		}
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.theme = theme,
	.art_path = artPath,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
