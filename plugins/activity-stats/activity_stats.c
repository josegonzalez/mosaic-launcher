// Activity stats: the play history stock's Game Tracker keeps in
// game_logs.sqlite, on a screen of its own.
//
// The activity-stats.open action opens it, from an Activity tile on the
// quick menu (the "tile" setting) or a game list button (the "button"
// setting), or button-remap or an FN button (action:activity-stats.open).
//
// It has five tabs, which L1 and R1 (or left and right) step through: a
// bar chart of the time played each day over the last 7 days and one over
// the last 30, from play_time_on; All time, with the total time, sessions
// and games, the longest session, the time per system and a chart of play
// by hour of day naming the most active hour, from the games and every
// session (play_session_info); and two lists of every game in the
// history, from play_games_count and play_game_info: Games, most played
// first, with each game's time and sessions, and Launches, most sessions
// first. Games a plugin's filter hides, such as the hidden plugin's, are
// left out of All time, both lists and their totals, and the export
// (is_filtered); the day charts count every game. Up and down move
// through a list, and A opens the selected game with open_path while its
// path is on the card. B closes it. Sessions are the tracker's: waking
// from sleep starts a new one. When the history can't be read every tab
// says "No play history".
//
// Y runs activity-stats.export, which writes Reports/activity-<date>.csv,
// a row per session, and activity-<date>.html, a summary with the charts
// as tables, and toasts their names. X on a list runs activity-stats.reset
// on the selected game and X on All time activity-stats.reset_all: after
// a confirm, play_forget deletes the sessions from game_logs.sqlite, so
// stock's Game Tracker loses them too. Both are MOSAIC_ACTION_EDITS, run
// through run_action so an action_gate (simple mode's kiosk) asks first.
//
// The "folder" setting adds a Most Played folder to the first screen, the
// 25 games with the most time that are still on the card, leaving out
// orphans and what is_filtered hides; the activity-stats.most-played
// entry set holds the same games whether the folder shows or not.
//
// The games are read when the screen opens and freed when it closes; the
// days are asked on each render, which the launcher answers from its
// cache. It draws stock's hints with draw_hints, and everything around the
// status pill and header widgets, which stay clear.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "activity_model.h"
#include "activity_report.h"

#define ID "activity-stats"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
#define EXPORT_ID ID ".export"
#define RESET_ID ID ".reset"
#define RESET_ALL_ID ID ".reset_all"
#define SET_NAME ID ".most-played"

static const MosaicHost* host;
static ASGame* games = NULL;
static int game_count = -1; // -1 when the history can't be read
static int* order[2] = { NULL, NULL }; // the games and launches tabs' orders
static long long total_seconds = 0, total_sessions = 0;
static const char** hidden_paths = NULL; // the paths is_filtered hides, sorted, for the sessions
static int hidden_count = 0;
static ASAllTime all_time;
static ASSystem* systems = NULL;
static int system_count = 0;
static int screen_open = 0;
static int tab = AS_TAB_WEEK;
static int selected[2] = { 0, 0 };
static int first[2] = { 0, 0 };
static char shown[1024];
static char roms[AS_PATH_MAX];

// the Most Played folder and set: the games, worked out on the first ask
// after a change
static int folder_on = 0;
static char folder_path[AS_PATH_MAX];
static char top[AS_TOP][AS_PATH_MAX];
static int top_count = -1; // -1 until worked out

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}

static void say(const char* message) {
	host->log(ID, message);
}

///////////////////////////////////////
// reading

static void freeHidden(void) {
	for (int i = 0; i < hidden_count; i++) free((void*)hidden_paths[i]);
	free(hidden_paths);
	hidden_paths = NULL;
	hidden_count = 0;
}

static void freeGames(void) {
	free(games);
	free(order[0]);
	free(order[1]);
	free(systems);
	freeHidden();
	games = NULL;
	order[0] = order[1] = NULL;
	systems = NULL;
	system_count = 0;
	game_count = -1;
	memset(&all_time, 0, sizeof(all_time));
}

// The longest session and play by hour, from every session but the ones
// of games a filter hides.
static void readSessions(void) {
	if (!MOSAIC_HOST_HAS(host, play_forget)) return;
	int count = host->play_sessions_count();
	for (int i = 0; i < count; i++) {
		MosaicPlaySession session;
		memset(&session, 0, sizeof(session));
		session.size = sizeof(session);
		if (host->play_session_info(i, &session) != 0) continue;
		if (!session.orphan && AS_pathIn(hidden_paths, hidden_count, session.path)) continue;
		char name[AS_NAME_MAX];
		AS_name(session.name, session.orphan ? "" : session.path, name, sizeof(name));
		AS_allTimeAdd(&all_time, session.start, session.seconds, name);
	}
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
		// what a plugin's filter hides, such as the hidden plugin's games
		if (!info.orphan && MOSAIC_HOST_HAS(host, is_filtered) && host->is_filtered(info.path)) {
			const char** grown = realloc(hidden_paths, (size_t)(hidden_count + 1) * sizeof(char*));
			char* copy = strdup(info.path);
			if (grown) hidden_paths = grown;
			if (grown && copy) hidden_paths[hidden_count++] = copy;
			else free(copy);
			continue;
		}
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
	AS_sortPaths(hidden_paths, hidden_count);
	AS_allTimeStart(&all_time, games, n);
	readSessions();
	systems = calloc((size_t)n + 1, sizeof(ASSystem));
	system_count = systems ? AS_systems(games, n, roms, systems) : 0;
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

// The All time tab into room: the totals and the longest session, then
// the time per system on the left and play by hour of day on the right.
// Writes what it shows to log.
static void drawAllTime(MosaicSurface* screen, const MosaicRect* room, char* log, size_t size) {
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	int tiny_h = host->text_height(MOSAIC_FONT_TINY);
	char caption[192], longest[AS_NAME_MAX + 64], busiest[64];
	AS_allTimeCaption(&all_time, caption, sizeof(caption));
	AS_longestCaption(&all_time, longest, sizeof(longest));
	AS_hourCaption(all_time.hours, busiest, sizeof(busiest));
	host->draw_text(screen, MOSAIC_FONT_SMALL, caption, room->x, room->y, C_TEXT(), room->w);
	if (longest[0]) host->draw_text(screen, MOSAIC_FONT_SMALL, longest, room->x, room->y + small_h, C_HINT(), room->w);

	size_t used = (size_t)snprintf(log, size, "%s: %s, %s, %s, hours", AS_tabLabel(tab), caption,
		longest[0] ? longest : "no sessions", busiest);
	for (int h = 0; h < AS_HOURS && used < size; h++) used += (size_t)snprintf(log + used, size - used, " %lld", all_time.hours[h]);
	for (int i = 0; i < system_count && used < size; i++) {
		char played[32];
		AS_duration(systems[i].seconds, played, sizeof(played));
		used += (size_t)snprintf(log + used, size - used, "%s %s %s", i ? "," : ", systems", systems[i].label, played);
	}

	MosaicRect left, right;
	AS_allTimeRooms(room, scale, small_h, &left, &right);
	if (left.w <= 0) return;

	// the time per system, most played first, as many as fit
	int row_h = small_h;
	for (int i = 0; i < system_count && (i + 1) * row_h <= left.h; i++) {
		char played[32];
		AS_duration(systems[i].seconds, played, sizeof(played));
		int y = left.y + i * row_h;
		int w = host->text_width(MOSAIC_FONT_SMALL, played);
		if (w > left.w / 2) w = left.w / 2;
		if (w > 0) host->draw_text(screen, MOSAIC_FONT_SMALL, played, left.x + left.w - w, y, C_HINT(), w);
		host->draw_text(screen, MOSAIC_FONT_SMALL, systems[i].label, left.x, y, C_TEXT(), left.w - w - pad / 2);
	}

	// play by hour of day, with the most active hour named over it
	host->draw_text(screen, MOSAIC_FONT_TINY, busiest, right.x, right.y, C_HINT(), right.w);
	MosaicRect plot = AS_hourPlot(&right, scale, tiny_h, tiny_h);
	if (plot.w <= 0) return;
	long long axis = AS_axisMax(all_time.hours, AS_HOURS);
	int busiest_hour = AS_busiestHour(all_time.hours);
	MosaicColor line = themeColor(MOSAIC_COLOR_HINT, 0x999999, 96);
	MosaicRect rule = { plot.x, plot.y + plot.h - (scale > 1 ? scale / 2 + 1 : 1), plot.w, scale > 1 ? scale / 2 + 1 : 1 };
	host->fill_rect(screen, &rule, line);
	for (int h = 0; h < AS_HOURS; h++) {
		// AS_bar counts back from the right, so hour 0 is on the left
		MosaicRect bar = AS_bar(&plot, AS_HOURS, AS_HOURS - 1 - h, all_time.hours[h], axis);
		if (bar.w > 0) host->fill_rect(screen, &bar, h == busiest_hour ? C_ACCENT() : C_MAIN());
		if (h % 6) continue;
		MosaicRect slot = AS_slot(&plot, AS_HOURS, AS_HOURS - 1 - h);
		char label[8];
		snprintf(label, sizeof(label), "%i", h);
		host->draw_text(screen, MOSAIC_FONT_TINY, label, slot.x, plot.y + plot.h + scale * 5, C_HINT(),
			right.x + right.w - slot.x);
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

// What the hints offer on this tab.
static int offers(void) {
	if (game_count < 0) return 0;
	// every session, for the export and a reset, is the newest of these
	int edits = MOSAIC_HOST_HAS(host, play_forget);
	int offer = edits ? AS_HINT_EXPORT : 0;
	const ASGame* game = selectedGame();
	if (game && typeOf(game->path) >= 0) offer |= AS_HINT_OPEN;
	if (edits && game && AS_resetPath(game)) offer |= AS_HINT_RESET;
	if (edits && tab == AS_TAB_ALL && all_time.sessions > 0) offer |= AS_HINT_RESET_ALL;
	return offer;
}

static void drawHints(MosaicSurface* screen) {
	MosaicHint left[2], right[2];
	int left_count = 0, right_count = 0;
	AS_hints(offers(), left, &left_count, right, &right_count);
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	screen_open = 1;
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
	screen_open = 0;
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
	// through run_action, so an action_gate is asked first
	int offer = offers();
	if ((in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_Y)) && (offer & AS_HINT_EXPORT)) {
		host->run_action(EXPORT_ID, NULL);
		return MOSAIC_SCREEN_STAY;
	}
	if ((in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X)) && (offer & (AS_HINT_RESET | AS_HINT_RESET_ALL))) {
		if (offer & AS_HINT_RESET) host->run_action(RESET_ID, AS_resetPath(selectedGame()));
		else host->run_action(RESET_ALL_ID, NULL);
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
	} else if (tab == AS_TAB_ALL) {
		drawAllTime(screen, &room, line, sizeof(line));
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

static int canOpen(const char* path, void* data) {
	(void)path;
	(void)data;
	return 1;
}

static void openStats(const char* path, void* data) {
	(void)path;
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the activity screen");
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

///////////////////////////////////////
// the Most Played folder and set

// Works out the games in top again on the next ask.
static void forgetTop(void) {
	top_count = -1;
}

static int onCard(const char* path) {
	struct stat st;
	return path && path[0] && stat(path, &st) == 0;
}

// The AS_TOP most played still on the card, leaving out orphans and what
// a filter hides. The history is most played first, so it reads only as
// far as it takes to find them.
static void findTop(void) {
	if (top_count >= 0) return;
	top_count = 0;
	int count = host->play_games_count();
	if (count <= 0) return;
	ASGame* candidates = calloc((size_t)count, sizeof(ASGame));
	unsigned char* keep = calloc((size_t)count, 1);
	int* ranked = calloc((size_t)count, sizeof(int));
	int picked[AS_TOP];
	if (!candidates || !keep || !ranked) {
		say("out of memory for the most played games");
		free(candidates);
		free(keep);
		free(ranked);
		return;
	}
	int n = 0, kept = 0;
	for (int i = 0; i < count && kept < AS_TOP; i++) {
		MosaicPlayGame info;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (host->play_game_info(i, &info) != 0) continue;
		ASGame* game = &candidates[n];
		snprintf(game->path, sizeof(game->path), "%s", info.orphan ? "" : info.path);
		game->seconds = info.seconds;
		game->last_played = info.last_played;
		game->count = info.count;
		keep[n] = !info.orphan && onCard(info.path)
			&& !(MOSAIC_HOST_HAS(host, is_filtered) && host->is_filtered(info.path));
		kept += keep[n];
		n++;
	}
	int got = AS_pickTop(candidates, n, keep, AS_TOP, ranked, picked);
	for (int i = 0; i < got; i++) snprintf(top[i], sizeof(top[i]), "%s", candidates[picked[i]].path);
	top_count = got;
	free(candidates);
	free(keep);
	free(ranked);
}

static int setHas(const char* name, const char* path) {
	(void)name;
	if (!path) return 0;
	findTop();
	for (int i = 0; i < top_count; i++) {
		if (!strcmp(top[i], path)) return 1;
	}
	return 0;
}

static void setList(const char* name, void (*add)(const char* path, void* ctx), void* ctx) {
	(void)name;
	findTop();
	for (int i = 0; i < top_count; i++) add(top[i], ctx);
}

// After the history changed: the folder and the set again.
static void historyChanged(void) {
	forgetTop();
	if (MOSAIC_HOST_HAS(host, set_names)) host->set_changed(SET_NAME);
}

static void rootEntries(MosaicList* root) {
	if (!folder_on) return;
	host->list_push(root, host->entry_new(folder_path, MOSAIC_ENTRY_DIR, tr("Most Played")));
}

static MosaicList* virtualDir(const char* path) {
	if (!folder_on || !path || strcmp(path, folder_path) != 0) return NULL;
	findTop();
	MosaicList* list = host->list_new();
	for (int i = 0; i < top_count; i++) {
		int type = typeOf(top[i]);
		if (type == MOSAIC_ENTRY_ROM || type == MOSAIC_ENTRY_PAK) host->list_push(list, host->entry_new(top[i], type, NULL));
	}
	char message[64];
	snprintf(message, sizeof(message), "most played: %i games", top_count);
	say(message);
	return list;
}

static void postReturn(void) {
	historyChanged();
}

static void setChanged(const char* event, const char* payload, void* data) {
	(void)event;
	(void)data;
	char name[128] = "";
	if (payload) host->event_value(payload, "name", name, sizeof(name));
	// another set changed, such as hidden's: what's filtered may have too
	if (strcmp(name, SET_NAME) != 0) forgetTop();
}

///////////////////////////////////////
// export

// Writes to path, by way of a temporary file put in place once it's all
// written, so a full card leaves no half-written file. 0, or -1.
static int writeFile(const char* path, void (*write)(FILE* f, void* data), void* data) {
	char temp[AS_PATH_MAX + 128];
	snprintf(temp, sizeof(temp), "%s.tmp", path);
	FILE* f = fopen(temp, "w");
	if (!f) return -1;
	write(f, data);
	int failed = ferror(f);
	if (fclose(f) != 0) failed = 1;
	if (failed || rename(temp, path) != 0) {
		unlink(temp);
		return -1;
	}
	return 0;
}

static void writeCsv(FILE* f, void* data) {
	(void)data;
	AR_csvHeader(f);
	int count = MOSAIC_HOST_HAS(host, play_forget) ? host->play_sessions_count() : 0;
	for (int i = 0; i < count; i++) {
		MosaicPlaySession session;
		memset(&session, 0, sizeof(session));
		session.size = sizeof(session);
		if (host->play_session_info(i, &session) != 0) continue;
		if (!session.orphan && AS_pathIn(hidden_paths, hidden_count, session.path)) continue;
		char name[AS_NAME_MAX], folder[AS_NAME_MAX], system[AS_NAME_MAX];
		AS_name(session.name, session.orphan ? "" : session.path, name, sizeof(name));
		AS_systemFolder(roms, session.path, folder, sizeof(folder));
		AS_systemLabel(folder, system, sizeof(system));
		AR_csvRow(f, session.start, session.seconds, name, system, session.path);
	}
}

static void writeHtml(FILE* f, void* data) {
	(void)data;
	long long days[AS_DAYS];
	for (int i = 0; i < AS_DAYS; i++) days[i] = host->play_time_on(i);
	ARSummary summary = {
		.now = time(NULL),
		.all = &all_time,
		.systems = systems,
		.system_count = system_count,
		.games = games,
		.order = order[0],
		.game_count = game_count,
		.days = days,
		.day_count = AS_DAYS,
	};
	AR_htmlWrite(f, &summary);
}

static int canExport(const char* path, void* data) {
	(void)path;
	(void)data;
	return host->play_games_count() >= 0;
}

static void exportStats(const char* path, void* data) {
	(void)path;
	(void)data;
	// the games as the screen has them, or read for this alone
	int was_read = game_count >= 0;
	if (!was_read) readGames();
	char reports[AS_PATH_MAX], stamp[32], csv[AS_PATH_MAX + 64], html[AS_PATH_MAX + 64];
	snprintf(reports, sizeof(reports), "%s/Reports", host->sdcard_path);
	AR_stamp(time(NULL), stamp, sizeof(stamp));
	snprintf(csv, sizeof(csv), "%s/activity-%s.csv", reports, stamp);
	snprintf(html, sizeof(html), "%s/activity-%s.html", reports, stamp);
	int ok = game_count >= 0 && (mkdir(reports, 0755) == 0 || errno == EEXIST)
		&& writeFile(csv, writeCsv, NULL) == 0 && writeFile(html, writeHtml, NULL) == 0;
	if (!ok) unlink(csv);
	char message[sizeof(csv) + sizeof(html) + 64];
	snprintf(message, sizeof(message), ok ? "exported %s and %s" : "couldn't export to %s and %s", csv, html);
	say(message);
	char text[256];
	if (ok) snprintf(text, sizeof(text), tr("Saved Reports/activity-%s.csv and .html"), stamp);
	else snprintf(text, sizeof(text), "%s", tr("Couldn't save the activity report"));
	toast(text);
	if (!was_read) freeGames();
}

///////////////////////////////////////
// reset

static char reset_path[AS_PATH_MAX]; // the game a confirm is about, or "" for everything

// After the history changed under the screen: read it again.
static void readAgain(void) {
	if (screen_open) {
		readGames();
		host->request_redraw();
	}
	historyChanged();
	if (folder_on && MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
}

static void resetConfirmed(void* data) {
	(void)data;
	const char* path = reset_path[0] ? reset_path : NULL;
	int deleted = host->play_forget(path);
	char message[AS_PATH_MAX + 64];
	snprintf(message, sizeof(message), "reset %s: %i", path ? path : "everything", deleted);
	say(message);
	toast(deleted < 0 ? tr("Couldn't reset the play time. Try again after a moment.") : tr("Play time reset"));
	readAgain();
}

// The name a confirm shows for path: the tracker's, else the file's.
static void nameOf(const char* path, char* out, size_t size) {
	int count = host->play_games_count();
	for (int i = 0; i < count; i++) {
		MosaicPlayGame info;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (host->play_game_info(i, &info) == 0 && !info.orphan && !strcmp(info.path, path)) {
			AS_name(info.name, info.path, out, size);
			return;
		}
	}
	AS_name(NULL, path, out, size);
}

static int canReset(const char* path, void* data) {
	(void)data;
	return path && host->play_count(path) > 0;
}

static void resetGame(const char* path, void* data) {
	(void)data;
	if (!path) return;
	snprintf(reset_path, sizeof(reset_path), "%s", path);
	char name[AS_NAME_MAX], question[AS_NAME_MAX + 512];
	nameOf(path, name, sizeof(name));
	AS_resetQuestion(name, question, sizeof(question));
	host->confirm(question, tr("RESET"), resetConfirmed, NULL);
}

static int canResetAll(const char* path, void* data) {
	(void)path;
	(void)data;
	return host->play_games_count() > 0;
}

static void resetAll(const char* path, void* data) {
	(void)path;
	(void)data;
	reset_path[0] = '\0';
	char question[512];
	AS_resetAllQuestion(question, sizeof(question));
	host->confirm(question, tr("RESET"), resetConfirmed, NULL);
}

static int init(const MosaicHost* h) {
	host = h;
	static const ASWords words = { tr, trPlural };
	AS_setWords(&words);
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
	if (host->register_action(ACTION_ID, "Activity", 0, canOpen, openStats, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	snprintf(roms, sizeof(roms), "%s/Roms", host->sdcard_path);
	snprintf(folder_path, sizeof(folder_path), "%s/Most Played", host->sdcard_path);
	folder_on = !strcmp(host->cfg_get("folder", "off"), "on");
	forgetTop();
	if (host->register_action(EXPORT_ID, "EXPORT", 0, canExport, exportStats, NULL) != 0) say("couldn't register " EXPORT_ID);
	if (MOSAIC_HOST_HAS(host, play_forget)) {
		if (host->register_action(RESET_ID, "RESET", MOSAIC_ACTION_PATH | MOSAIC_ACTION_EDITS, canReset, resetGame, NULL) != 0
				|| host->register_action(RESET_ALL_ID, "RESET ALL", MOSAIC_ACTION_EDITS, canResetAll, resetAll, NULL) != 0) {
			say("couldn't register the reset actions");
		}
	}
	if (MOSAIC_HOST_HAS(host, is_filtered) && host->set_register(SET_NAME, setHas, setList) != 0) {
		say("couldn't register the " SET_NAME " set");
	}
	if (MOSAIC_HOST_HAS(host, set_names)) host->subscribe("launcher.set_changed", setChanged, NULL);
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
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
	const char* name = host->cfg_get("button", "none");
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
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
