// First run: a short wizard the first time the launcher starts with it
// on. Its pages are a welcome; stock's clock, battery percentage, haptics
// and volume and display notification toggles (stock_toggle_get and
// stock_toggle_set, saved at once); other plugins' settings, the home
// mode, the favorites collection, the theme and the language, through
// each plugin's <id>.setup service while that plugin is on; whether the
// launcher has a PIN; a few suggested plugins, each with whether it's on
// and what it does; what's on each button; and a last page.
//
// A goes on, B goes back, Y skips the rest, up and down pick a row or
// scroll, and left and right flip a toggle or change a plugin's setting.
// Finishing or skipping sets the "seen" setting, so it doesn't open by
// itself again; the first-run.open action, from a First run tile on the
// quick menu (the "tile" setting) or a game list button (the "button"
// setting), opens it again. The first-run.buttons action, from a Buttons
// tile or button, opens the buttons page on its own.
//
// Plugins can't set the PIN or turn plugins on, so those pages only say
// what's there and point to Tools > Launcher > Plugins. The plugins page
// shows On for a suggested plugin that's loaded, as plugin_loaded says,
// and Off otherwise, and the selected one's description, from plugin_info
// while it's loaded and otherwise from its plugin.txt beside this
// plugin's folder or in Tools, its summary= without a description=.
//
// The buttons page lists every action, from actions_count and
// action_info, under each button it's on, the FN buttons too (fn_name and
// fn_binding), and last the actions on no button.
//
// It opens from tick, as open_screen is refused from init: on the first
// frame showing the game list or the home slot's screen, which is where
// the launcher starts, so closing it goes back there. It waits while the
// quick menu, the game switcher, a dialog or another plugin's screen
// shows, and opens by itself at most once a run.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "wizard_model.h"

#define ID "first-run"
// English translated where it's drawn, for make pot
#define TR_MARK(text) (text)
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
#define BUTTONS_SCREEN_ID ID ".buttons"
#define BUTTONS_ACTION_ID ID ".buttons"
#define PADDING 10
#define PILL_SIZE 30
#define BUTTON_PADDING 12
#define TEXT_LINES 12
#define MANIFEST_MAX 16384
#define DEFAULT_SUGGEST "home-dashboard,switcher-plus,favorites,search"

static const MosaicHost* host;

// stock's toggles, in the order the page lists them
static const struct {
	const char* name;
	const char* label;
} toggles[] = {
	{ MOSAIC_TOGGLE_SHOW_CLOCK, TR_MARK("Show clock") },
	{ MOSAIC_TOGGLE_SHOW_BATTERY_PERCENT, TR_MARK("Show battery percentage") },
	{ MOSAIC_TOGGLE_HAPTICS, TR_MARK("Haptic feedback") },
	{ MOSAIC_TOGGLE_NOTIFY_ADJUSTMENTS, TR_MARK("Volume and display notifications") },
};
#define TOGGLE_COUNT ((int)(sizeof(toggles) / sizeof(toggles[0])))

// other plugins' settings the setup page sets, through their services
static const struct {
	const char* service;
	const char* key;
	const char* label;
} steps[] = {
	{ "home-dashboard.setup", "mode", TR_MARK("Home mode") },
	{ "favorites.setup", "collection", TR_MARK("Favorites collection") },
	{ "theme-plus.setup", "theme", TR_MARK("Theme") },
	{ "localization.setup", "language", TR_MARK("Language") },
};
#define STEP_COUNT ((int)(sizeof(steps) / sizeof(steps[0])))

static FrWizard wizard;
static int row_toggle[FR_TOGGLES_MAX]; // each row's index in toggles
static int row_on[FR_TOGGLES_MAX];
static FrSetup setups[STEP_COUNT];
static int setup_step[STEP_COUNT]; // each setup row's index in steps
static int setup_count = 0;
static int pin_set = 0;
static char suggest[FR_SUGGEST_MAX][FR_ID_MAX];
static int suggest_count = 0;
static int suggest_on[FR_SUGGEST_MAX];
static char suggest_name[FR_SUGGEST_MAX][128];
static char suggest_text[FR_SUGGEST_MAX][256];
static int suggest_read[FR_SUGGEST_MAX]; // its plugin.txt was found
static FrLine lines[FR_LINES_MAX];
static int line_count = 0;
static char fn_names[3][32];
static int seen = 0;
static int opened = 0; // opened by itself this run
static int tile_on = 1;
static int last_page = -1;

static const char* const page_names[FR_PAGES] = { "welcome", "toggles", "setup", "pin", "plugins", "buttons", "done" };
static const char* const page_titles[FR_PAGES] = { TR_MARK("Welcome"), TR_MARK("Settings"), TR_MARK("Plugin settings"),
	TR_MARK("PIN"), TR_MARK("Plugins"), TR_MARK("Buttons"), TR_MARK("All set") };

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

// Another plugin's own text, such as its name, translated as its own.
static const char* trOf(const char* id, const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(id, NULL, text) : text;
}

static void say(const char* message) {
	host->log(ID, message);
}

static int setting(const char* key, const char* fallback) {
	return strcmp(host->cfg_get(key, fallback), "off") != 0;
}

///////////////////////////////////////
// what the pages show, read as the wizard opens

static int readToggles(void) {
	int rows = 0;
	if (!setting("toggles", "on") || !MOSAIC_HOST_HAS(host, stock_toggle_get)) return 0;
	for (int i = 0; i < TOGGLE_COUNT && rows < FR_TOGGLES_MAX; i++) {
		int on = host->stock_toggle_get(toggles[i].name);
		if (on < 0) continue;
		row_toggle[rows] = i;
		row_on[rows] = on;
		rows++;
	}
	return rows;
}

// A row for each step whose plugin is on and answers its service.
static void readSetups(void) {
	setup_count = 0;
	if (!setting("setup", "on") || !MOSAIC_HOST_HAS(host, call)) return;
	static char reply[MOSAIC_CALL_MAX];
	for (int i = 0; i < STEP_COUNT; i++) {
		if (host->call(steps[i].service, NULL, reply, sizeof(reply)) < 0) continue;
		if (FR_parseSetup(reply, steps[i].key, &setups[setup_count]) != 0) continue;
		setup_step[setup_count++] = i;
	}
}

// Name and text for the suggested plugins the plugin.txt files in dir's
// folders ending in suffix are about.
static void readManifests(const char* dir, const char* suffix) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	static char text[MANIFEST_MAX];
	struct dirent* dp;
	size_t suffix_len = strlen(suffix);
	while ((dp = readdir(dh)) != NULL) {
		size_t len = strlen(dp->d_name);
		if (dp->d_name[0] == '.' || len <= suffix_len || strcmp(dp->d_name + len - suffix_len, suffix) != 0) continue;
		char path[1024];
		if ((size_t)snprintf(path, sizeof(path), "%s/%s/plugin.txt", dir, dp->d_name) >= sizeof(path)) continue;
		FILE* f = fopen(path, "r");
		if (!f) continue;
		size_t n = fread(text, 1, sizeof(text) - 1, f);
		fclose(f);
		text[n] = '\0';
		for (int i = 0; i < suggest_count; i++) {
			char name[sizeof(suggest_name[0])], what[sizeof(suggest_text[0])];
			if (suggest_read[i] || !FR_manifest(text, suggest[i], name, sizeof(name), what, sizeof(what))) continue;
			suggest_read[i] = 1;
			snprintf(suggest_name[i], sizeof(suggest_name[i]), "%s", name);
			if (!suggest_text[i][0]) snprintf(suggest_text[i], sizeof(suggest_text[i]), "%s", what);
		}
	}
	closedir(dh);
}

static void readPlugins(void) {
	char own[700] = "";
	for (int i = 0; i < suggest_count; i++) {
		suggest_on[i] = FR_pluginLoaded(host, suggest[i]);
		suggest_read[i] = 0;
		snprintf(suggest_name[i], sizeof(suggest_name[i]), "%s", suggest[i]);
		suggest_text[i][0] = '\0';
	}
	// loaded plugins' descriptions, and where this plugin is
	int count = MOSAIC_HOST_HAS(host, plugin_info) ? host->plugins_count() : 0;
	for (int p = 0; p < count; p++) {
		MosaicPluginInfo info = { .size = sizeof(info) };
		if (host->plugin_info(p, &info) != 0) break;
		if (!strcmp(info.id, ID)) snprintf(own, sizeof(own), "%s", info.res);
		for (int i = 0; i < suggest_count; i++) {
			if (!strcmp(info.id, suggest[i])) snprintf(suggest_text[i], sizeof(suggest_text[i]), "%s", info.description);
		}
	}
	// the rest from their plugin.txt: beside this plugin's folder, or in Tools
	char* slash = strrchr(own, '/'); // <folder>/res
	if (slash) *slash = '\0';
	slash = strrchr(own, '/');
	if (slash) {
		*slash = '\0';
		readManifests(own[0] ? own : "/", ".plugin");
	}
	char tools[600];
	snprintf(tools, sizeof(tools), "%s/Tools/%s", host->sdcard_path, host->platform);
	readManifests(tools, ".pak");
}

static const char* screenName(int screen) {
	return screen == MOSAIC_SCREEN_GAMELIST ? "gamelist"
		: screen == MOSAIC_SCREEN_SWITCHER  ? "switcher"
		: screen == MOSAIC_SCREEN_QUICKMENU ? "quickmenu"
											: "any";
}

// A heading's button as the device names it.
static const char* buttonName(int button) {
	if (button >= MOSAIC_BUTTON_FN1 && button <= MOSAIC_BUTTON_FN3) return fn_names[button - MOSAIC_BUTTON_FN1];
	const char* name = FR_buttonName(button);
	return name ? name : "";
}

static void readButtons(void) {
	line_count = 0;
	if (!MOSAIC_HOST_HAS(host, action_info)) return;
	int count = host->actions_count();
	MosaicActionInfo* actions = count > 0 ? calloc((size_t)count, sizeof(MosaicActionInfo)) : NULL;
	int got = 0;
	for (int i = 0; actions && i < count; i++) {
		actions[got].size = sizeof(MosaicActionInfo);
		if (host->action_info(i, &actions[got]) == 0) got++;
	}
	MosaicFnBinding fn[3];
	int fn_there[3] = { 0, 0, 0 };
	memset(fn, 0, sizeof(fn));
	for (int i = 0; i < 3; i++) {
		fn_names[i][0] = '\0';
		fn[i].size = sizeof(fn[i]);
		if (!MOSAIC_HOST_HAS(host, fn_release)) continue;
		const char* name = host->fn_name(MOSAIC_BUTTON_FN1 + i);
		if (!name || !name[0]) continue;
		snprintf(fn_names[i], sizeof(fn_names[i]), "%s", name);
		fn_there[i] = host->fn_binding(MOSAIC_BUTTON_FN1 + i, &fn[i]) == 0;
	}
	line_count = FR_buttonLines(actions, got, fn, fn_there, lines, FR_LINES_MAX);
	free(actions);
}

static void logRows(void) {
	char line[512];
	size_t used = (size_t)snprintf(line, sizeof(line), "toggles:");
	for (int r = 0; r < wizard.rows[FR_TOGGLES] && used < sizeof(line); r++) {
		int n = snprintf(line + used, sizeof(line) - used, "%s %s %s", r ? "," : "", toggles[row_toggle[r]].name,
			row_on[r] ? "on" : "off");
		if (n < 0) break;
		used += (size_t)n;
	}
	say(wizard.rows[FR_TOGGLES] ? line : "toggles: none");
}

static void logSetups(void) {
	char line[768];
	size_t used = (size_t)snprintf(line, sizeof(line), "setup:");
	for (int r = 0; r < setup_count && used < sizeof(line); r++) {
		int n = snprintf(line + used, sizeof(line) - used, "%s %s %s", r ? "," : "", steps[setup_step[r]].service,
			setups[r].value);
		if (n < 0) break;
		used += (size_t)n;
	}
	say(setup_count ? line : "setup: none");
}

static void logSuggestions(void) {
	char line[768];
	size_t used = (size_t)snprintf(line, sizeof(line), "suggesting");
	for (int i = 0; i < suggest_count && used < sizeof(line); i++) {
		int n = snprintf(line + used, sizeof(line) - used, "%s %s %s", i ? "," : "", suggest[i],
			suggest_on[i] ? "on" : "off");
		if (n < 0) break;
		used += (size_t)n;
	}
	say(line);
	for (int i = 0; i < suggest_count; i++) {
		char about[512];
		snprintf(about, sizeof(about), "about %s: %s", suggest[i], suggest_text[i][0] ? suggest_text[i] : "nothing");
		say(about);
	}
}

static void logButtons(void) {
	for (int i = 0; i < line_count; i++) {
		if (lines[i].heading) continue;
		char line[256];
		snprintf(line, sizeof(line), "on %s: %s (%s)", lines[i].button < 0 ? "no button" : buttonName(lines[i].button),
			lines[i].label, screenName(lines[i].screen));
		say(line);
	}
	if (!line_count) say("buttons: none");
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static int empty(const MosaicRect* r) {
	return r->w <= 0 || r->h <= 0;
}

// The status pill and the header together, which the screen leaves clear.
static MosaicRect hole(const MosaicScreenView* view) {
	const MosaicRect* s = &view->status;
	const MosaicRect* h = &view->header;
	if (empty(s)) return *h;
	if (empty(h)) return *s;
	int x1 = s->x < h->x ? s->x : h->x, y1 = s->y < h->y ? s->y : h->y;
	int x2 = s->x + s->w > h->x + h->w ? s->x + s->w : h->x + h->w;
	int y2 = s->y + s->h > h->y + h->h ? s->y + s->h : h->y + h->h;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

static void fillAround(MosaicSurface* screen, const MosaicRect* rect, const MosaicRect* gap, MosaicColor color) {
	int x1 = rect->x, y1 = rect->y, x2 = rect->x + rect->w, y2 = rect->y + rect->h;
	if (empty(gap) || gap->x >= x2 || gap->y >= y2 || gap->x + gap->w <= x1 || gap->y + gap->h <= y1) {
		host->fill_rect(screen, rect, color);
		return;
	}
	int hx1 = gap->x > x1 ? gap->x : x1, hy1 = gap->y > y1 ? gap->y : y1;
	int hx2 = gap->x + gap->w < x2 ? gap->x + gap->w : x2, hy2 = gap->y + gap->h < y2 ? gap->y + gap->h : y2;
	MosaicRect parts[4] = {
		{ x1, y1, x2 - x1, hy1 - y1 },
		{ x1, hy2, x2 - x1, y2 - hy2 },
		{ x1, hy1, hx1 - x1, hy2 - hy1 },
		{ hx2, hy1, x2 - hx2, hy2 - hy1 },
	};
	for (int i = 0; i < 4; i++) {
		if (parts[i].w > 0 && parts[i].h > 0) host->fill_rect(screen, &parts[i], color);
	}
}

static void text(MosaicSurface* screen, int font, const char* s, int x, int y, int max_w, MosaicColor color) {
	char cut[FR_LINE_MAX + 64];
	if (max_w <= 0 || host->text_truncate(font, s, max_w, cut, sizeof(cut)) < 0) return;
	host->draw_text(screen, font, cut, x, y, color, max_w);
}

static int smallWidth(const char* line, void* data) {
	(void)data;
	return host->text_width(MOSAIC_FONT_SMALL, line);
}

// Draws paragraph wrapped from y within x..x+w, stopping at bottom.
// Returns the y under it.
static int paragraph(MosaicSurface* screen, const char* s, int x, int y, int w, int bottom, MosaicColor color) {
	char wrapped[TEXT_LINES][FR_LINE_MAX];
	int line_h = host->text_height(MOSAIC_FONT_SMALL);
	int room = line_h > 0 ? (bottom - y) / line_h : 0;
	if (room > TEXT_LINES) room = TEXT_LINES;
	int n = FR_wrap(s, w, smallWidth, NULL, wrapped, room);
	for (int i = 0; i < n; i++) text(screen, MOSAIC_FONT_SMALL, wrapped[i], x, y + i * line_h, w, color);
	return y + n * line_h;
}

// A row: label on the left, value on the right, on a pill when selected.
static void drawRow(MosaicSurface* screen, const MosaicRect* r, const char* label, const char* value, int selected) {
	int scale = host->screen_scale();
	int inner = BUTTON_PADDING * scale;
	int th = host->text_height(MOSAIC_FONT_SMALL);
	MosaicColor ink = themeColor(selected ? MOSAIC_COLOR_LIST_TEXT_SELECTED : MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	if (selected) host->fill_round_rect(screen, r, r->h / 2, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF));
	int value_w = value ? host->text_width(MOSAIC_FONT_SMALL, value) : 0;
	// a long value keeps half the row, so the label shows too
	if (value_w > (r->w - 3 * inner) / 2) value_w = (r->w - 3 * inner) / 2;
	int y = r->y + (r->h - th) / 2;
	text(screen, MOSAIC_FONT_SMALL, label, r->x + inner, y, r->w - 3 * inner - value_w, ink);
	if (value) text(screen, MOSAIC_FONT_SMALL, value, r->x + r->w - inner - value_w, y, value_w, ink);
}

static void drawTitle(MosaicSurface* screen, const MosaicRect* r, const char* title) {
	int scale = host->screen_scale();
	int inner = BUTTON_PADDING * scale;
	int h = host->text_height(MOSAIC_FONT_LARGE);
	host->fill_round_rect(screen, r, r->h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	text(screen, MOSAIC_FONT_LARGE, title, r->x + inner, r->y + (r->h - h + 1) / 2, r->w - 2 * inner,
		themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF));
}

// A button's heading on the buttons page: its name on a small pill.
static void drawHeading(MosaicSurface* screen, const MosaicRect* r, const char* name) {
	int scale = host->screen_scale();
	int inner = BUTTON_PADDING * scale;
	int th = host->text_height(MOSAIC_FONT_SMALL);
	int w = host->text_width(MOSAIC_FONT_SMALL, name) + 2 * inner;
	if (w > r->w) w = r->w;
	MosaicRect pill = { r->x, r->y, w, r->h };
	host->fill_round_rect(screen, &pill, pill.h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	text(screen, MOSAIC_FONT_SMALL, name, r->x + inner, r->y + (r->h - th) / 2, w - 2 * inner, themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF));
}

static const char* screenLabel(int screen) {
	return screen == MOSAIC_SCREEN_GAMELIST ? tr("Game list")
		: screen == MOSAIC_SCREEN_SWITCHER  ? tr("Game switcher")
		: screen == MOSAIC_SCREEN_QUICKMENU ? tr("Quick menu")
											: tr("Anywhere");
}

static void logPage(void) {
	if (wizard.page == last_page) return;
	last_page = wizard.page;
	char line[96];
	snprintf(line, sizeof(line), "showing %s, %i of %i", page_names[wizard.page], FR_pageNumber(&wizard, wizard.page),
		FR_pageCount(&wizard));
	say(line);
}

static void drawHints(MosaicSurface* screen) {
	MosaicHint left[2], right[1];
	int left_count = 0, right_count = 0;
	FR_hints(&wizard, left, &left_count, right, &right_count);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
	// two pills with long labels can collide on a narrow screen: keep Back
	if (left_count && host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL) != 0 && left_count > 1) {
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, 1, NULL);
	}
}

// The buttons page's lines from the first one shown, as many as fit
// between y and bottom, a heading never last without its first item.
static void drawButtons(MosaicSurface* screen, int left, int right, int y, int bottom, int row_h, MosaicColor ink) {
	int scale = host->screen_scale();
	int inner = BUTTON_PADDING * scale;
	if (!line_count) {
		paragraph(screen, tr("Nothing is on a button."), left + inner, y, right - left - 2 * inner, bottom, ink);
		return;
	}
	int fit = row_h > 0 ? (bottom - y) / row_h : 0;
	FR_setVisible(&wizard, fit);
	for (int i = wizard.row, shown = 0; i < line_count && shown < fit; i++, shown++) {
		const FrLine* l = &lines[i];
		MosaicRect r = { left, y, right - left, row_h - 2 * scale };
		if (l->heading) {
			if (shown == fit - 1 && i + 1 < line_count) break;
			drawHeading(screen, &r, l->button < 0 ? tr("Not on a button") : buttonName(l->button));
		}
		else {
			r.x += inner;
			r.w -= inner;
			drawRow(screen, &r, l->label, l->button < 0 ? NULL : screenLabel(l->screen), 0);
		}
		y += row_h;
	}
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	MosaicRect gap = hole(view);
	fillAround(screen, &view->area, &gap, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
	logPage();

	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int pad = PADDING * scale, pill = PILL_SIZE * scale, inner = BUTTON_PADDING * scale;
	const MosaicRect* a = &view->area;
	int left = a->x + pad, right = a->x + a->w - pad;

	// the title, stopping short of the status pill on its line
	const char* title = tr(page_titles[wizard.page]);
	int title_right = right;
	if (!empty(&gap) && gap.y < a->y + pad + pill && gap.y + gap.h > a->y + pad && gap.x - pad < title_right)
		title_right = gap.x - pad;
	char step[128] = "";
	if (!wizard.alone) snprintf(step, sizeof(step), tr("%i of %i"), FR_pageNumber(&wizard, wizard.page), FR_pageCount(&wizard));
	int step_w = step[0] ? host->text_width(MOSAIC_FONT_TINY, step) : 0;
	int tw = host->text_width(MOSAIC_FONT_LARGE, title) + 2 * inner;
	if (tw > title_right - left - step_w - pad) tw = title_right - left - step_w - pad;
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor hint = themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF);
	if (tw > 2 * inner) {
		MosaicRect t = { left, a->y + pad, tw, pill };
		drawTitle(screen, &t, title);
		int sh = host->text_height(MOSAIC_FONT_TINY);
		if (step[0]) text(screen, MOSAIC_FONT_TINY, step, left + tw + pad, t.y + (pill - sh) / 2, step_w, hint);
	}

	int top = a->y + pad + pill + pad;
	if (!empty(&gap) && gap.y + gap.h + pad > top) top = gap.y + gap.h + pad;
	int bottom = a->y + a->h - pad - pill - pad; // above the hints
	int x = left + inner, w = right - left - 2 * inner;
	int row_h = host->text_height(MOSAIC_FONT_SMALL) + 4 * scale;
	if (w <= 0 || bottom <= top) {
		drawHints(screen);
		return 0;
	}

	int y = top;
	switch (wizard.page) {
		case FR_WELCOME:
			y = paragraph(screen,
				tr("This goes over a few settings and plugins. A goes on, B goes back, and Y skips the rest."), x, y, w,
				bottom, ink);
			paragraph(screen,
				tile_on ? tr("Once it's done or skipped it won't open by itself again. The First run tile on the quick menu opens it.")
						: tr("Once it's done or skipped it won't open by itself again."),
				x, y + pad, w, bottom, ink);
			break;
		case FR_TOGGLES:
			for (int r = 0; r < wizard.rows[FR_TOGGLES] && y + row_h <= bottom; r++) {
				MosaicRect rr = { left, y, right - left, row_h };
				drawRow(screen, &rr, tr(toggles[row_toggle[r]].label), row_on[r] ? tr("On") : tr("Off"), r == wizard.row);
				y += row_h;
			}
			paragraph(screen, tr("Left and right change the selected setting, saved at once. Settings has them too."), x,
				y + pad, w, bottom, hint);
			break;
		case FR_SETUP: {
			int restart = 0;
			for (int r = 0; r < setup_count && y + row_h <= bottom; r++) {
				MosaicRect rr = { left, y, right - left, row_h };
				drawRow(screen, &rr, tr(steps[setup_step[r]].label), FR_setupLabel(&setups[r]), r == wizard.row);
				restart |= setups[r].restart;
				y += row_h;
			}
			paragraph(screen,
				restart ? tr("Left and right change it, saved at once. Some show from the next start.")
						: tr("Left and right change the selected setting, saved at once."),
				x, y + pad, w, bottom, hint);
			break;
		}
		case FR_PIN:
			y = paragraph(screen, pin_set ? tr("The launcher has a PIN.") : tr("The launcher has no PIN."), x, y, w, bottom, ink);
			paragraph(screen,
				tr("A PIN keeps others out of simple mode and protected plugin settings. Set or change it on the PIN row in Tools > Launcher > Plugins."),
				x, y + pad, w, bottom, ink);
			break;
		case FR_PLUGINS:
			for (int i = 0; i < suggest_count && y + row_h <= bottom; i++) {
				MosaicRect rr = { left, y, right - left, row_h };
				drawRow(screen, &rr, trOf(suggest[i], suggest_name[i]), suggest_on[i] ? tr("On") : tr("Off"), i == wizard.row);
				y += row_h;
			}
			// what the selected one does, then where to turn them on, in the hint
			// colour, close under the rows so both fit under four of them
			y += 4 * scale;
			if (wizard.row >= 0 && wizard.row < suggest_count && suggest_text[wizard.row][0]) {
				y = paragraph(screen, trOf(suggest[wizard.row], suggest_text[wizard.row]), x, y, w, bottom, ink);
			}
			paragraph(screen, tr("Turn them on in Tools > Launcher > Plugins."), x, y, w, bottom, hint);
			break;
		case FR_BUTTONS:
			drawButtons(screen, left, right, y, bottom, row_h, ink);
			break;
		default:
			paragraph(screen,
				tile_on ? tr("That's it. The First run tile on the quick menu opens this again.")
						: tr("That's it."),
				x, y, w, bottom, ink);
			break;
	}
	drawHints(screen);
	return 0;
}

///////////////////////////////////////
// the screen

static void markSeen(const char* how) {
	seen = 1;
	host->cfg_set("seen", "on");
	say(how);
}

static void flip(void) {
	int r = wizard.row;
	if (r < 0 || r >= wizard.rows[FR_TOGGLES]) return;
	const char* name = toggles[row_toggle[r]].name;
	int want = !row_on[r];
	char line[96];
	if (host->stock_toggle_set(name, want) != 0) {
		snprintf(line, sizeof(line), "couldn't set %s", name);
		say(line);
		return;
	}
	int now = host->stock_toggle_get(name);
	row_on[r] = now < 0 ? want : now;
	snprintf(line, sizeof(line), "%s now %s", name, row_on[r] ? "on" : "off");
	say(line);
}

// Sets the selected setup row to its next or previous choice, through its
// plugin's service, and shows what the service says it is now.
static void change(void) {
	int r = wizard.row;
	if (r < 0 || r >= setup_count) return;
	FrSetup* s = &setups[r];
	int choice = FR_setupStep(s, wizard.dir);
	if (choice < 0) return;
	const char* service = steps[setup_step[r]].service;
	const char* key = steps[setup_step[r]].key;
	char args[FR_VALUE_MAX + 64], line[FR_VALUE_MAX + 128], error[64];
	snprintf(args, sizeof(args), "%s=%s", key, s->values[choice]);
	static char reply[MOSAIC_CALL_MAX];
	FrSetup now;
	if (host->call(service, args, reply, sizeof(reply)) < 0 || FR_parseSetup(reply, key, &now) != 0) {
		snprintf(line, sizeof(line), "couldn't call %s", service);
		say(line);
		return;
	}
	*s = now;
	if (FR_value(reply, "error", error, sizeof(error)) > 0) snprintf(line, sizeof(line), "%s refused %s", service, args);
	else snprintf(line, sizeof(line), "%s now %s", service, s->value);
	say(line);
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	switch (FR_input(&wizard, in->pressed)) {
		case FR_FLIP:
			flip();
			host->request_redraw();
			break;
		case FR_CHANGE:
			change();
			host->request_redraw();
			break;
		case FR_REDRAW:
			host->request_redraw();
			break;
		case FR_FINISH:
			markSeen("finished");
			return MOSAIC_SCREEN_CLOSE;
		case FR_SKIP:
			markSeen("skipped");
			return MOSAIC_SCREEN_CLOSE;
		case FR_CLOSE:
			return MOSAIC_SCREEN_CLOSE;
		default:
			break;
	}
	return MOSAIC_SCREEN_STAY;
}

static int enter(void* data) {
	(void)data;
	int rows = readToggles();
	readSetups();
	pin_set = MOSAIC_HOST_HAS(host, ask_pin) && host->has_pin() == 1;
	int pin_page = setting("pin", "on") && MOSAIC_HOST_HAS(host, ask_pin);
	readPlugins();
	if (setting("buttons", "on")) readButtons();
	else line_count = 0;
	FR_init(&wizard, rows, setup_count, pin_page, suggest_count, line_count);
	last_page = -1;
	say("opened");
	logRows();
	logSetups();
	if (pin_page) say(pin_set ? "pin: set" : "pin: not set");
	if (suggest_count) logSuggestions();
	logButtons();
	return 0;
}

static int enterButtons(void* data) {
	(void)data;
	readButtons();
	FR_initButtons(&wizard, line_count);
	last_page = -1;
	say("buttons opened");
	logButtons();
	return 0;
}

static void leave(void* data) {
	(void)data;
	say("closed");
}

///////////////////////////////////////
// opening

static void openWizard(const char* path, void* data) {
	(void)path;
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the wizard");
}

static void openButtons(const char* path, void* data) {
	(void)path;
	(void)data;
	if (host->open_screen(BUTTONS_SCREEN_ID) != 0) say("couldn't open the buttons page");
}

static void tick(unsigned long now) {
	(void)now;
	const char* home = MOSAIC_HOST_HAS(host, home_screen) ? host->home_screen() : NULL;
	const char* current = host->current_screen();
	if (!FR_shouldOpen(seen, opened, current, home)) return;
	opened = 1;
	char under[80], line[160];
	snprintf(under, sizeof(under), "%s", current); // current_screen's answer changes as it opens
	int rc = host->open_screen(SCREEN_ID);
	snprintf(line, sizeof(line), rc == 0 ? "opened by itself over %s" : "couldn't open by itself over %s", under);
	say(line);
}

// Puts action on the game list button the setting key names, if any.
static void bindSetting(const char* key, const char* action) {
	static const struct {
		const char* name;
		int button;
	} buttons[] = {
		{ "Y", MOSAIC_BUTTON_Y },
		{ "L2", MOSAIC_BUTTON_L2 },
		{ "R2", MOSAIC_BUTTON_R2 },
		{ "SELECT", MOSAIC_BUTTON_SELECT },
	};
	const char* name = host->cfg_get(key, "none");
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (strcmp(name, buttons[i].name) != 0) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, action) != 0) {
			char line[128];
			snprintf(line, sizeof(line), "couldn't bind %s's button", action);
			say(line);
		}
	}
}

static void addTile(const char* action) {
	if (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(action, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0) {
		char line[128];
		snprintf(line, sizeof(line), "couldn't add %s's tile", action);
		say(line);
	}
}

static int init(const MosaicHost* h) {
	host = h;
	opened = 0;
	if (!MOSAIC_HOST_HAS(host, draw_hints) || !MOSAIC_HOST_HAS(host, open_screen)) {
		say("this launcher has no plugin screens or button hints");
		return 1;
	}
	seen = strcmp(host->cfg_get("seen", "off"), "on") == 0;
	tile_on = setting("tile", "on");
	suggest_count = FR_parseIds(host->cfg_get("suggest", DEFAULT_SUGGEST), suggest, FR_SUGGEST_MAX);
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	static const MosaicScreenHooks buttons_hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enterButtons,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0 || host->register_screen(BUTTONS_SCREEN_ID, &buttons_hooks) != 0) {
		say("can't register the wizard's screens");
		return 1;
	}
	if (host->register_action(ACTION_ID, "First run", 0, NULL, openWizard, NULL) != 0
		|| host->register_action(BUTTONS_ACTION_ID, "Buttons", 0, NULL, openButtons, NULL) != 0) {
		say("couldn't register " ACTION_ID " or " BUTTONS_ACTION_ID);
		return 1;
	}
	if (tile_on) addTile(ACTION_ID);
	if (setting("buttons_tile", "on")) addTile(BUTTONS_ACTION_ID);
	bindSetting("button", ACTION_ID);
	bindSetting("buttons_button", BUTTONS_ACTION_ID);
	say(seen ? "already seen" : "not seen yet");
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
