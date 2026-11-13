// First run: a short wizard the first time the launcher starts with it
// on. Its pages are a welcome; stock's clock, battery percentage, haptics
// and volume and display notification toggles (stock_toggle_get and
// stock_toggle_set, saved at once); whether the launcher has a PIN; a few
// suggested plugins and whether each is on; and a last page.
//
// A goes on, B goes back, Y skips the rest, and up, down, left and right
// pick and flip the toggles. Finishing or skipping sets the "seen" setting,
// so it doesn't open by itself again; the first-run.open action, from a
// First run tile on the quick menu (the "tile" setting) or a game list
// button (the "button" setting), opens it again.
//
// Plugins can't set the PIN or turn plugins on, so those pages only say
// what's there and point to Tools > Launcher > Plugins. The plugins page
// shows On for a suggested plugin that's loaded, as plugin_loaded says,
// and Off otherwise.
//
// It opens from tick, as open_screen is refused from init: on the first
// frame showing the game list or the home slot's screen, which is where
// the launcher starts, so closing it goes back there. It waits while the
// quick menu, the game switcher, a dialog or another plugin's screen
// shows, and opens by itself at most once a run.

#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"
#include "wizard_model.h"

#define ID "first-run"
// English translated where it's drawn, for make pot
#define TR_MARK(text) (text)
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
#define PADDING 10
#define PILL_SIZE 30
#define BUTTON_PADDING 12
#define TEXT_LINES 12
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

static FrWizard wizard;
static int row_toggle[FR_TOGGLES_MAX]; // each row's index in toggles
static int row_on[FR_TOGGLES_MAX];
static int pin_set = 0;
static char suggest[FR_SUGGEST_MAX][FR_ID_MAX];
static int suggest_count = 0;
static int suggest_on[FR_SUGGEST_MAX];
static int seen = 0;
static int opened = 0; // opened by itself this run
static int tile_on = 1;
static int last_page = -1;

static const char* const page_names[FR_PAGES] = { "welcome", "toggles", "pin", "plugins", "done" };
static const char* const page_titles[FR_PAGES] = { TR_MARK("Welcome"), TR_MARK("Settings"), TR_MARK("PIN"), TR_MARK("Plugins"),
	TR_MARK("All set") };

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
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

static void readPlugins(void) {
	for (int i = 0; i < suggest_count; i++) suggest_on[i] = FR_pluginLoaded(host, suggest[i]);
}

static void logRows(void) {
	char line[512];
	size_t used = (size_t)snprintf(line, sizeof(line), "toggles:");
	for (int r = 0; r < wizard.rows && used < sizeof(line); r++) {
		int n = snprintf(line + used, sizeof(line) - used, "%s %s %s", r ? "," : "", toggles[row_toggle[r]].name,
			row_on[r] ? "on" : "off");
		if (n < 0) break;
		used += (size_t)n;
	}
	say(wizard.rows ? line : "toggles: none");
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
	char lines[TEXT_LINES][FR_LINE_MAX];
	int line_h = host->text_height(MOSAIC_FONT_SMALL);
	int room = line_h > 0 ? (bottom - y) / line_h : 0;
	if (room > TEXT_LINES) room = TEXT_LINES;
	int n = FR_wrap(s, w, smallWidth, NULL, lines, room);
	for (int i = 0; i < n; i++) text(screen, MOSAIC_FONT_SMALL, lines[i], x, y + i * line_h, w, color);
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
	char step[128];
	snprintf(step, sizeof(step), tr("%i of %i"), FR_pageNumber(&wizard, wizard.page), FR_pageCount(&wizard));
	int step_w = host->text_width(MOSAIC_FONT_TINY, step);
	int tw = host->text_width(MOSAIC_FONT_LARGE, title) + 2 * inner;
	if (tw > title_right - left - step_w - pad) tw = title_right - left - step_w - pad;
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor hint = themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF);
	if (tw > 2 * inner) {
		MosaicRect t = { left, a->y + pad, tw, pill };
		drawTitle(screen, &t, title);
		int sh = host->text_height(MOSAIC_FONT_TINY);
		text(screen, MOSAIC_FONT_TINY, step, left + tw + pad, t.y + (pill - sh) / 2, step_w, hint);
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
			for (int r = 0; r < wizard.rows && y + row_h <= bottom; r++) {
				MosaicRect rr = { left, y, right - left, row_h };
				drawRow(screen, &rr, tr(toggles[row_toggle[r]].label), row_on[r] ? tr("On") : tr("Off"), r == wizard.row);
				y += row_h;
			}
			paragraph(screen, tr("Left and right change the selected setting, saved at once. Settings has them too."), x,
				y + pad, w, bottom, hint);
			break;
		case FR_PIN:
			y = paragraph(screen, pin_set ? tr("The launcher has a PIN.") : tr("The launcher has no PIN."), x, y, w, bottom, ink);
			paragraph(screen,
				tr("A PIN keeps others out of simple mode and protected plugin settings. Set or change it on the PIN row in Tools > Launcher > Plugins."),
				x, y + pad, w, bottom, ink);
			break;
		case FR_PLUGINS:
			y = paragraph(screen, tr("Plugins to try:"), x, y, w, bottom, ink);
			for (int i = 0; i < suggest_count && y + row_h <= bottom; i++) {
				char label[FR_ID_MAX + 256];
				const char* what = FR_describe(suggest[i]);
				if (what) snprintf(label, sizeof(label), "%.63s - %.180s", suggest[i], tr(what));
				else snprintf(label, sizeof(label), "%.63s", suggest[i]);
				MosaicRect rr = { left, y, right - left, row_h };
				drawRow(screen, &rr, label, suggest_on[i] ? tr("On") : tr("Off"), 0);
				y += row_h;
			}
			paragraph(screen, tr("Turn them on in Tools > Launcher > Plugins. They start the next time the launcher does."), x,
				y + pad, w, bottom, hint);
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
	if (r < 0 || r >= wizard.rows) return;
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

static int input(const MosaicInput* in, void* data) {
	(void)data;
	switch (FR_input(&wizard, in->pressed)) {
		case FR_FLIP:
			flip();
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
		default:
			break;
	}
	return MOSAIC_SCREEN_STAY;
}

static int enter(void* data) {
	(void)data;
	int rows = readToggles();
	pin_set = MOSAIC_HOST_HAS(host, ask_pin) && host->has_pin() == 1;
	int pin_page = setting("pin", "on") && MOSAIC_HOST_HAS(host, ask_pin);
	readPlugins();
	FR_init(&wizard, rows, pin_page, suggest_count > 0);
	last_page = -1;
	say("opened");
	logRows();
	if (pin_page) say(pin_set ? "pin: set" : "pin: not set");
	if (suggest_count) logSuggestions();
	return 0;
}

static void leave(void* data) {
	(void)data;
	say("closed");
}

///////////////////////////////////////
// opening

static void openWizard(void* data) {
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the wizard");
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
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the wizard's screen");
		return 1;
	}
	if (host->register_action(ACTION_ID, "First run", NULL, openWizard, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	if (tile_on && (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the First run tile");
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
			say("couldn't bind the first run button");
		}
	}
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
