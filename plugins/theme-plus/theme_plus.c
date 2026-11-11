// Theme plus: owns the theme slot. The theme setting names a theme: a
// folder <sd>/Themes/<name>/ with a theme.txt (and its own font and
// wallpapers if it likes), one of the folders this plugin ships in its
// themes/, or one of stock's palettes as a colours-only theme. "stock"
// changes nothing. The background, text_size, contrast, ui_size, margins
// and fallback_font settings override the theme's values, over stock's
// too.
//
// The theme-plus.pick action lists them with choose, from a Theme tile on
// the quick menu (the "tile" setting) or a game list button (the "button"
// setting); picking one saves the setting and reloads the lists, which
// asks for the theme again, so it shows at once.
//
// The model (theme.txt and palette files, paths, the list, the merge) is
// in theme_plus_model.c.

#define _GNU_SOURCE // dladdr
#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "theme_plus_model.h"

#define ID "theme-plus"
#define ACTION_ID ID ".pick"
#define CHOICES_MAX 64 // what choose takes, Stock among them

static const MosaicHost* host;
static TPCatalog catalog;
static char bundledDir[TP_PATH_MAX];
static char pickIds[CHOICES_MAX][TP_NAME_MAX];

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

static void loadCatalog(void) {
	char themes[TP_PATH_MAX], user[TP_PATH_MAX], builtin[TP_PATH_MAX];
	snprintf(themes, sizeof(themes), "%s/Themes", host->sdcard_path);
	snprintf(user, sizeof(user), "%s/Palettes", host->sdcard_path);
	snprintf(builtin, sizeof(builtin), "%s/.system/res/palettes", host->sdcard_path);
	TP_catalog(&catalog, themes, bundledDir[0] ? bundledDir : NULL, user, builtin);
	if (catalog.dropped) say("%d themes or palettes past the first %d are left out", catalog.dropped, TP_THEMES_MAX);
}

static const char* kindName(int kind) {
	return kind == TP_KIND_CARD ? "card" : kind == TP_KIND_BUNDLED ? "bundled" : "palette";
}

static int theme(MosaicTheme* t) {
	loadCatalog();
	const char* name = host->cfg_get(ID, "theme", "stock");
	TPOverrides overrides = TP_overrides(host->cfg_get(ID, "background", "theme"), host->cfg_get(ID, "text_size", "theme"),
		host->cfg_get(ID, "contrast", "theme"), host->cfg_get(ID, "ui_size", "theme"), host->cfg_get(ID, "margins", "theme"),
		logLine, (void*)"settings");
	TP_overrideFallback(&overrides, host->sdcard_path, host->cfg_get(ID, "fallback_font", ""), logLine, (void*)"settings");
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
	say("theme %s (%s%s%s)", entry->id, kindName(entry->kind), palette ? ", palette " : "", palette ? palette->name : "");
	return 0;
}

static void picked(int index, void* data) {
	(void)data;
	if (index < 0 || index >= CHOICES_MAX) return;
	host->cfg_set(ID, "theme", pickIds[index]);
	say("picked %s", pickIds[index]);
	host->reload_lists();
}

static void pick(void* data) {
	(void)data;
	loadCatalog();
	const char* labels[CHOICES_MAX];
	const char* current = host->cfg_get(ID, "theme", "stock");
	const TPEntry* selected = TP_find(&catalog, current);
	int count = 0;
	int cursor = 0;
	snprintf(pickIds[count], TP_NAME_MAX, "stock");
	labels[count++] = tr("Stock");
	for (int i = 0; i < catalog.theme_count && count < CHOICES_MAX; i++) {
		if (&catalog.themes[i] == selected) cursor = count;
		snprintf(pickIds[count], TP_NAME_MAX, "%s", catalog.themes[i].id);
		labels[count++] = catalog.themes[i].label;
	}
	if (catalog.theme_count >= CHOICES_MAX) say("only the first %d themes are listed", CHOICES_MAX - 1);
	if (host->choose(tr("Theme"), labels, count, cursor, picked, NULL) != 0) say("couldn't list the themes");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, ui_size)) {
		say("this launcher has no theme slot");
		return 1;
	}
	findBundled();
	if (host->register_action(ACTION_ID, "Theme", NULL, pick, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 0;
	}
	if (strcmp(host->cfg_get(ID, "tile", "on"), "off") != 0
			&& host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0) {
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
	const char* name = host->cfg_get(ID, "button", "none");
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
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
