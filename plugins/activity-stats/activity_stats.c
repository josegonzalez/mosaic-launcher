// Activity stats: the play history stock's Game Tracker keeps in
// game_logs.sqlite, on a screen of its own.
//
// The activity-stats.open action opens it, from an Activity tile on the
// quick menu (the "tile" setting) or a game list button (the "button"
// setting), or button-remap or an FN button (action:activity-stats.open).
//
// It has four tabs, which L1 and R1 (or left and right) step through: a
// bar chart of the time played each day over the last 7 days and one over
// the last 30, from play_time_on, and two lists of every game in the
// history, from play_games_count and play_game_info: Games, most played
// first, with each game's time and sessions, and Launches, most sessions
// first. Up and down move through a list, and A opens the selected game
// with open_path while its path is on the card. B closes it. Sessions are
// the tracker's: waking from sleep starts a new one. When the history
// can't be read every tab says "No play history".
//
// The games are read when the screen opens and freed when it closes; the
// days are asked on each render, which the launcher answers from its
// cache. It draws stock's hints with draw_hints, and everything around the
// status pill and header widgets, which stay clear.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "activity_model.h"

#define ID "activity-stats"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"

static const MosaicHost* host;
static ASGame* games = NULL;
static int game_count = -1; // -1 when the history can't be read
static int* order[2] = { NULL, NULL }; // the games and launches tabs' orders
static long long total_seconds = 0, total_sessions = 0;
static int tab = AS_TAB_WEEK;
static int selected[2] = { 0, 0 };
static int first[2] = { 0, 0 };
static char shown[1024];

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void say(const char* message) {
	host->log(ID, message);
}

///////////////////////////////////////
// reading

static void freeGames(void) {
	free(games);
	free(order[0]);
	free(order[1]);
	games = NULL;
	order[0] = order[1] = NULL;
	game_count = -1;
}

static void readGames(void) {
	freeGames();
	int count = host->play_games_count();
	if (count < 0) return;
	games = calloc(count > 0 ? (size_t)count : 1, sizeof(ASGame));
	order[0] = calloc(count > 0 ? (size_t)count : 1, sizeof(int));
	order[1] = calloc(count > 0 ? (size_t)count : 1, sizeof(int));
	if (!games || !order[0] || !order[1]) {
		say("out of memory for the play history");
		freeGames();
		return;
	}
	int n = 0;
	for (int i = 0; i < count; i++) {
		MosaicPlayGame info;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (host->play_game_info(i, &info) != 0) continue;
		ASGame* game = &games[n++];
		snprintf(game->path, sizeof(game->path), "%s", info.orphan ? "" : info.path);
		AS_name(info.name, info.path, game->name, sizeof(game->name));
		game->seconds = info.seconds;
		game->last_played = info.last_played;
		game->count = info.count;
	}
	game_count = n;
	AS_rankPlayed(games, n, order[0]);
	AS_rankLaunched(games, n, order[1]);
	AS_totals(games, n, &total_seconds, &total_sessions);
}

// the type open_path takes for path, or -1 when it isn't on the card
static int typeOf(const char* path) {
	struct stat st;
	if (!path || !path[0] || stat(path, &st) != 0) return -1;
	if (S_ISREG(st.st_mode)) return MOSAIC_ENTRY_ROM;
	if (!S_ISDIR(st.st_mode)) return -1;
	size_t len = strlen(path);
	return len > 4 && strcmp(path + len - 4, ".pak") == 0 ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_DIR;
}

static const ASGame* selectedGame(void) {
	if (!AS_isList(tab) || game_count <= 0) return NULL;
	int list = tab == AS_TAB_LAUNCHES;
	if (selected[list] >= game_count) selected[list] = game_count - 1;
	return &games[order[list][selected[list]]];
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback, unsigned char alpha) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, alpha);
}

#define C_MAIN() themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF, 255)
#define C_TEXT() themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF, 255)
#define C_SELECTED() themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000, 255)
#define C_HINT() themeColor(MOSAIC_COLOR_HINT, 0x999999, 255)
#define C_ACCENT() themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257, 255)

static void fillAround(MosaicSurface* screen, const MosaicRect* rect, const MosaicRect* hole, MosaicColor color) {
	MosaicRect parts[4];
	int n = AS_avoid(rect, hole, parts);
	for (int i = 0; i < n; i++) host->fill_rect(screen, &parts[i], color);
}

// text centred in box, cut to fit
static void centred(MosaicSurface* screen, int font, const char* text, const MosaicRect* box, MosaicColor color) {
	char cut[256];
	int w = host->text_truncate(font, text, box->w, cut, sizeof(cut));
	int h = host->text_height(font);
	if (w < 0 || h < 0) return;
	host->draw_text(screen, font, cut, box->x + (box->w - w) / 2, box->y + (box->h - h) / 2, color, box->w);
}

// The tab bar in room: L1, every tab with the selected one on a pill, R1;
// or, when they don't all fit, the selected one alone between L1 and R1.
static void drawTabs(MosaicSurface* screen, const MosaicRect* room) {
	if (room->w <= 0) return;
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	int l1_w = host->text_width(MOSAIC_FONT_SMALL, "L1"), r1_w = host->text_width(MOSAIC_FONT_SMALL, "R1");
	int widths[AS_TABS];
	for (int i = 0; i < AS_TABS; i++) widths[i] = host->text_width(MOSAIC_FONT_LARGE, tr(AS_tabLabel(i))) + 2 * pad;
	int all = AS_tabsFit(widths, AS_TABS, l1_w + r1_w + pad, room->w);
	int x = room->x, right = room->x + room->w;
	int w = host->draw_text(screen, MOSAIC_FONT_SMALL, "L1", x, room->y + (room->h - small_h) / 2, C_HINT(), right - x);
	if (w > 0) x += w + pad / 2;
	for (int i = 0; i < AS_TABS && x < right; i++) {
		if (!all && i != tab) continue;
		int tab_w = widths[i];
		if (x + tab_w > right - r1_w - pad / 2) tab_w = right - r1_w - pad / 2 - x;
		if (tab_w <= 2 * pad) break;
		MosaicRect pill = { x, room->y, tab_w, room->h };
		int on = i == tab;
		if (on) host->fill_round_rect(screen, &pill, room->h / 2, C_MAIN());
		host->draw_text(screen, MOSAIC_FONT_LARGE, tr(AS_tabLabel(i)), x + pad, room->y + (room->h - large_h) / 2,
			on ? C_SELECTED() : C_TEXT(), tab_w - 2 * pad);
		x += tab_w;
	}
	if (x + pad / 2 < right) {
		host->draw_text(screen, MOSAIC_FONT_SMALL, "R1", x + pad / 2, room->y + (room->h - small_h) / 2, C_HINT(),
			right - x - pad / 2);
	}
}

// A chart of the last days days into room. Writes what it shows to log.
static void drawChart(MosaicSurface* screen, const MosaicRect* room, int days, char* log, size_t size) {
	int scale = host->screen_scale();
	long long day[AS_DAYS];
	for (int i = 0; i < days; i++) day[i] = host->play_time_on(i);
	char caption[128];
	AS_chartCaption(day, days, caption, sizeof(caption));
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	int tiny_h = host->text_height(MOSAIC_FONT_TINY);
	host->draw_text(screen, MOSAIC_FONT_SMALL, caption, room->x, room->y, C_HINT(), room->w);

	long long axis = AS_axisMax(day, days);
	char top[32], middle[32];
	AS_duration(axis, top, sizeof(top));
	AS_duration(axis / 2, middle, sizeof(middle));
	int axis_w = host->text_width(MOSAIC_FONT_TINY, top);
	int middle_w = host->text_width(MOSAIC_FONT_TINY, middle);
	if (middle_w > axis_w) axis_w = middle_w;
	MosaicRect plot = AS_plot(room, scale, small_h, axis_w, tiny_h);

	size_t used = (size_t)snprintf(log, size, "%s: days", AS_tabLabel(tab));
	for (int i = 0; i < days && used < size; i++) used += (size_t)snprintf(log + used, size - used, " %lld", day[i]);
	if (used < size) snprintf(log + used, size - used, ", axis %lld, %s", axis, caption);
	if (plot.w <= 0) return;

	// the scale: lines at the top, the middle and the bottom
	MosaicColor line = themeColor(MOSAIC_COLOR_HINT, 0x999999, 96);
	const char* labels[] = { top, middle, NULL };
	for (int i = 0; i < 3; i++) {
		int y = plot.y + plot.h * i / 2;
		MosaicRect rule = { plot.x, y, plot.w, scale > 1 ? scale / 2 + 1 : 1 };
		if (i == 2) rule.y = plot.y + plot.h - rule.h;
		host->fill_rect(screen, &rule, line);
		if (!labels[i]) continue;
		int w = host->text_width(MOSAIC_FONT_TINY, labels[i]);
		host->draw_text(screen, MOSAIC_FONT_TINY, labels[i], room->x + axis_w - w, y - tiny_h / 2,
			C_HINT(), axis_w);
	}

	time_t now = time(NULL);
	for (int i = 0; i < days; i++) {
		MosaicRect slot = AS_slot(&plot, days, i);
		MosaicRect bar = AS_bar(&plot, days, i, day[i], axis);
		if (bar.w > 0) host->fill_rect(screen, &bar, i == 0 ? C_ACCENT() : C_MAIN());
		// the week's values over their bars, where they fit
		if (days <= AS_WEEK && day[i] > 0) {
			char value[32];
			AS_duration(day[i], value, sizeof(value));
			int w = host->text_width(MOSAIC_FONT_TINY, value);
			int y = bar.y - tiny_h;
			if (w > 0 && w <= slot.w && y >= plot.y) {
				host->draw_text(screen, MOSAIC_FONT_TINY, value, slot.x + (slot.w - w) / 2, y, C_TEXT(), slot.w);
			}
		}
		char label[16];
		AS_dayLabel(now, days, i, label, sizeof(label));
		if (!label[0]) continue;
		int w = host->text_width(MOSAIC_FONT_TINY, label);
		if (w <= 0) continue;
		int x = slot.x + (slot.w - w) / 2;
		if (x < room->x) x = room->x;
		host->draw_text(screen, MOSAIC_FONT_TINY, label, x, plot.y + plot.h + scale * 5, i == 0 ? C_TEXT() : C_HINT(),
			room->x + room->w - x);
	}
}

// The games or launches list into room. Writes what it shows to log.
static void drawList(MosaicSurface* screen, const MosaicRect* room, char* log, size_t size) {
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int list = tab == AS_TAB_LAUNCHES;
	if (game_count == 0) {
		centred(screen, MOSAIC_FONT_LARGE, tr("Nothing played yet"), room, C_HINT());
		snprintf(log, size, "%s: nothing played yet", AS_tabLabel(tab));
		return;
	}
	char caption[128];
	AS_listCaption(game_count, total_seconds, total_sessions, caption, sizeof(caption));
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	host->draw_text(screen, MOSAIC_FONT_SMALL, caption, room->x, room->y, C_HINT(), room->w);

	int row_h = 30 * scale;
	int top = room->y + small_h + pad / 2;
	int fit = (room->y + room->h - top) / row_h;
	const ASGame* on_game = selectedGame();
	char right[96];
	AS_rowRight(on_game, list, right, sizeof(right));
	snprintf(log, size, "%s: %i of %i: %s, %s, %s", AS_tabLabel(tab), selected[list], game_count, on_game->name, right,
		typeOf(on_game->path) >= 0 ? "can open" : "can't open");
	if (fit < 1) return;
	first[list] = AS_window(selected[list], game_count, fit, first[list]);
	for (int i = first[list]; i < game_count && i < first[list] + fit; i++) {
		const ASGame* game = &games[order[list][i]];
		MosaicRect r = { room->x, top + (i - first[list]) * row_h, room->w, row_h };
		int on = i == selected[list];
		if (on) host->fill_round_rect(screen, &r, row_h / 2, C_MAIN());
		AS_rowRight(game, list, right, sizeof(right));
		int right_w = host->text_width(MOSAIC_FONT_SMALL, right);
		if (right_w > r.w / 2) right_w = r.w / 2;
		if (right_w > 0) {
			host->draw_text(screen, MOSAIC_FONT_SMALL, right, r.x + r.w - pad - right_w, r.y + (row_h - small_h) / 2,
				on ? C_SELECTED() : C_HINT(), right_w);
		}
		int name_w = r.w - 2 * pad - (right_w > 0 ? right_w + pad : 0);
		host->draw_text(screen, MOSAIC_FONT_LARGE, game->name, r.x + pad, r.y + (row_h - large_h) / 2,
			on ? C_SELECTED() : C_TEXT(), name_w);
	}
}

static void drawHints(MosaicSurface* screen) {
	const ASGame* game = selectedGame();
	MosaicHint left[1], right[1];
	int left_count = 0, right_count = 0;
	AS_hints(game && typeOf(game->path) >= 0, left, &left_count, right, &right_count);
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	readGames();
	shown[0] = '\0';
	char message[160];
	if (game_count < 0) snprintf(message, sizeof(message), "no play history");
	else snprintf(message, sizeof(message), "history: %i games, %lld sessions, %lld seconds", game_count, total_sessions, total_seconds);
	say(message);
	return 0;
}

static void leave(void* data) {
	(void)data;
	freeGames();
}

static void openSelected(void) {
	const ASGame* game = selectedGame();
	int type = game ? typeOf(game->path) : -1;
	if (type < 0) return;
	int opened = host->open_path(game->path, type);
	char message[AS_PATH_MAX + 32];
	snprintf(message, sizeof(message), "open %s %i", game->path, opened);
	say(message);
	if (!opened && MOSAIC_HOST_HAS(host, toast)) {
		char text[AS_NAME_MAX + 256];
		snprintf(text, sizeof(text), tr("Couldn't open %s"), game->name);
		host->toast(text);
	}
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	unsigned int bit_b = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B), bit_a = MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A);
	int before_tab = tab, list = tab == AS_TAB_LAUNCHES, before = selected[list];
	if (in->pressed & bit_b) return MOSAIC_SCREEN_CLOSE;
	if (in->pressed & bit_a) {
		openSelected();
		return MOSAIC_SCREEN_STAY;
	}
	static const struct {
		int button, tabs, rows;
	} steps[] = {
		{ MOSAIC_BUTTON_L1, -1, 0 },
		{ MOSAIC_BUTTON_R1, 1, 0 },
		{ MOSAIC_BUTTON_LEFT, -1, 0 },
		{ MOSAIC_BUTTON_RIGHT, 1, 0 },
		{ MOSAIC_BUTTON_UP, 0, -1 },
		{ MOSAIC_BUTTON_DOWN, 0, 1 },
	};
	for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		unsigned int bit = MOSAIC_BUTTON_BIT(steps[i].button);
		if (steps[i].tabs && (in->pressed & bit)) {
			tab = AS_tabStep(tab, steps[i].tabs);
			break;
		}
		if (steps[i].rows && (in->repeated & bit)) {
			if (AS_isList(tab) && game_count > 0) {
				selected[list] = AS_step(selected[list], game_count, steps[i].rows, (in->pressed & bit) != 0);
			}
			break;
		}
	}
	if (tab != before_tab || selected[list] != before) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	MosaicRect hole = AS_hole(&view->status, &view->header);
	fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255));
	MosaicRect tabs = AS_tabRoom(&view->area, &hole, scale);
	drawTabs(screen, &tabs);
	MosaicRect room = AS_room(&view->area, &hole, scale);
	char line[sizeof(shown)];
	if (game_count < 0) {
		centred(screen, MOSAIC_FONT_LARGE, tr("No play history"), room.w > 0 ? &room : &view->area, C_HINT());
		snprintf(line, sizeof(line), "%s: no play history", AS_tabLabel(tab));
	} else if (room.w <= 0) {
		snprintf(line, sizeof(line), "%s: no room", AS_tabLabel(tab));
	} else if (AS_isList(tab)) {
		drawList(screen, &room, line, sizeof(line));
	} else {
		drawChart(screen, &room, tab == AS_TAB_WEEK ? AS_WEEK : AS_DAYS, line, sizeof(line));
	}
	drawHints(screen);
	// what shows, whenever it changes, for tests and bug reports
	if (strcmp(line, shown) != 0) {
		snprintf(shown, sizeof(shown), "%s", line);
		char message[sizeof(line) + 16];
		snprintf(message, sizeof(message), "showing %s", line);
		say(message);
	}
	return 0;
}

///////////////////////////////////////
// the action

static int canOpen(void* data) {
	(void)data;
	return 1;
}

static void openStats(void* data) {
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the activity screen");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, play_time_on) || !MOSAIC_HOST_HAS(host, draw_hints)) {
		say("this launcher has no play history list or button hints");
		return 1;
	}
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enter,
		.exit = leave,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the activity screen");
		return 1;
	}
	if (host->register_action(ACTION_ID, "Activity", canOpen, openStats, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	if (strcmp(host->cfg_get(ID, "tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the Activity tile");
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
			say("couldn't bind the activity button");
		}
	}
	return 0;
}

static void quit(void) {
	freeGames();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
