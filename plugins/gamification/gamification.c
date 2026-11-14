// Gamification: daily and weekly quests and a play streak, from launch and
// return events and stock's play tracker.
//
// The launcher exits while a game runs, so pre_launch keeps the launch in
// a file in /tmp: the wall clock, the monotonic clock, the game, whether
// it was never played before, as row-badges' games.played service says
// just before it starts (the one definition of played; without it, as
// when row-badges is off, when the play history has no session of it),
// and whether it wasn't played for GM_REVISIT_DAYS, as the play history
// says. post_return, as the launcher starts again after it, counts the
// launch on its day and the time between the two monotonic readings as
// play, which leaves out time the device spent asleep. A launch another
// plugin cancels leaves the file behind until the next launch replaces
// it; a pak's launch removes it, and a reboot clears /tmp, so neither is
// counted.
//
// Each day's play is the most of the plugin's own count and the play
// history's (play_time_on), so it works without the tracker and isn't
// counted twice with it. The streak is the days in a row, up to today,
// with at least the streak_minutes setting's play; today joins it once it
// has them. Daily quests reset at local midnight and weekly ones on
// Monday. A quest done for the first time in its day or week counts
// towards the total, and post_return toasts it (the toasts setting), or
// else the streak, once a day.
//
// The quests are picked for their day or week the first time the plugin
// works out progress in it. While playtime-limit has a daily limit set,
// its limit.left service gives the time left today, and a play time quest
// that still needs more than that is left out until the next day or week:
// the daily one against today's time left, the weekly one against today's
// and the daily limit for each day left in the week. Picked quests keep
// running, whatever the time left is later. Without playtime-limit, or
// with no limit, every quest runs.
//
// The daily RomM quest, download a game from RomM, is offered only while
// romm is loaded with a server set, as its romm.synced service says. romm
// publishes romm.download_done when a download finishes, which counts
// towards it on its day whether or not it's offered, and toasts it when
// that completes it.
//
// The state is in .userdata/<platform>/launcher/gamification/state.txt.
// The gamification.open action opens the screen, from a Quests tile on
// the quick menu (the tile setting), a game list button (the button
// setting), or button-remap or an FN button (action:gamification.open).
// It shows the streak and the quests with their progress; up and down
// scroll when they don't fit, and B closes it.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "mosaic_plugin.h"
#include "gamification_model.h"

#define ID "gamification"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"

#ifndef GM_LAUNCH_FILE
#define GM_LAUNCH_FILE "/tmp/mosaic-gamification.launch"
#endif

#define STATE_MAX 16384

static const MosaicHost* host;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, singular, plural, n);
	return n == 1 ? singular : plural;
}
static GMState state;
static char state_dir[GM_PATH_MAX];
static char state_path[GM_PATH_MAX + 16];
static GMTargets targets;
static int toasts = 1;

// what the screen shows, from refresh
static int today = 0;
static long long played[GM_HISTORY_DAYS];
static int streak = 0, longest = 0;
static GMProgress progress[GM_QUESTS];
static int running[GM_QUESTS];    // the quests that run today, in order
static int running_count = GM_QUESTS;
static int first = 0;             // the first of them in view
static int fit_rows = GM_QUESTS;  // the rows the last render fit
static char shown[1024];

static long long monoNow(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
	return (long long)ts.tv_sec;
}
static long long (*mono_now)(void) = monoNow;

static time_t wallNow(void) {
	return time(NULL);
}
static time_t (*wall_now)(void) = wallNow;

static void say(const char* message) {
	host->log(ID, message);
}

static long setting(const char* key, const char* fallback, long lo, long hi) {
	const char* text = host->cfg_get(key, fallback);
	char* end;
	long v = strtol(text, &end, 10);
	if (end == text || *end || v < lo || v > hi) v = strtol(fallback, NULL, 10);
	return v;
}

static void readSettings(void) {
	targets.daily_minutes = (int)setting("daily_minutes", "30", 1, 24 * 60);
	targets.daily_games = (int)setting("daily_games", "3", 1, GM_TODAY_GAMES);
	targets.week_days = (int)setting("week_days", "5", 1, 7);
	targets.week_hours = (int)setting("week_hours", "5", 1, 7 * 24);
	targets.streak_seconds = setting("streak_minutes", "5", 1, 24 * 60) * 60;
	toasts = strcmp(host->cfg_get("toasts", "on"), "off") != 0;
}

///////////////////////////////////////
// the state file

static void loadState(void) {
	GM_init(&state);
	FILE* f = fopen(state_path, "rb");
	if (!f) return;
	static char text[STATE_MAX];
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[n] = '\0';
	GM_parse(&state, text);
}

static int saveState(void) {
	static char text[STATE_MAX];
	int len = GM_format(&state, text, sizeof(text));
	if (len < 0) return -1;
	if (mkdir(state_dir, 0755) != 0 && errno != EEXIST) return -1;
	char tmp[sizeof(state_path) + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", state_path);
	FILE* f = fopen(tmp, "wb");
	if (!f) return -1;
	int ok = fwrite(text, 1, (size_t)len, f) == (size_t)len;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, state_path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

static void save(void) {
	if (saveState() != 0) say("couldn't save the state");
}

///////////////////////////////////////
// the pending launch

static int keepLaunch(const GMLaunch* launch) {
	char line[GM_PATH_MAX + 128];
	if (GM_formatLaunch(launch, line, sizeof(line)) != 0) return -1;
	char tmp[sizeof(GM_LAUNCH_FILE) + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", GM_LAUNCH_FILE);
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	int ok = fputs(line, f) >= 0;
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, GM_LAUNCH_FILE) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

// Reads and removes the launch a game left, so it's counted at most once.
static int takeLaunch(GMLaunch* out) {
	char line[GM_PATH_MAX + 128];
	FILE* f = fopen(GM_LAUNCH_FILE, "r");
	if (!f) return -1;
	char* got = fgets(line, sizeof(line), f);
	fclose(f);
	unlink(GM_LAUNCH_FILE);
	return got && GM_parseLaunch(line, out) == 0 ? 0 : -1;
}

///////////////////////////////////////
// progress

// A service's reply, or "" when nobody provides it or it can't answer.
static int ask(const char* name, const char* args, char* reply, size_t size) {
	reply[0] = '\0';
	return MOSAIC_HOST_HAS(host, call) && host->call(name, args, reply, size) >= 0;
}

// The number at key in a reply, or fallback.
static long long replyNumber(const char* reply, const char* key, long long fallback) {
	char text[32];
	if (host->event_value(reply, key, text, sizeof(text)) <= 0) return fallback;
	char* end;
	long long v = strtoll(text, &end, 10);
	return end == text || *end ? fallback : v;
}

// The seconds playtime-limit's limit.left says are left today, and its
// daily limit in minutes, or -1 with no limit, an unknown history, or
// without the service.
static long long timeLeft(int* limit_minutes) {
	char reply[256];
	*limit_minutes = 0;
	if (!ask("limit.left", NULL, reply, sizeof(reply))) return -1;
	*limit_minutes = (int)replyNumber(reply, "limit", 0);
	return replyNumber(reply, "left", -1);
}

// Picks the quests for a new day or week, leaving out the play time ones
// the limit leaves no time for, and lists the ones that run.
static void pickQuests(void) {
	int limit_minutes;
	long long left = timeLeft(&limit_minutes);
	int left_now[GM_QUESTS], left_count = 0;
	if (GM_pick(&state, today, progress, left, GM_weekLeft(today, left, limit_minutes), left_now, &left_count) > 0) {
		for (int i = 0; i < left_count; i++) {
			char message[160];
			snprintf(message, sizeof(message), "left out %s: it needs %lld minutes, and the limit leaves %lld",
				GM_questId(left_now[i]), progress[left_now[i]].target - progress[left_now[i]].value,
				(GM_weekly(left_now[i]) ? GM_weekLeft(today, left, limit_minutes) : left) / 60);
			say(message);
		}
		save();
	}
	running_count = 0;
	for (int i = 0; i < GM_QUESTS; i++) {
		if (GM_running(&state, today, i)) running[running_count++] = i;
	}
}

// 1 while romm is loaded with a server set, so the RomM quest can be done
static int rommReady(void) {
	char reply[64];
	return ask("romm.synced", NULL, reply, sizeof(reply)) && replyNumber(reply, "server", 0) == 1;
}

// Works out the streak and the quests' progress for now.
static void refresh(void) {
	today = GM_dayOf(wall_now());
	state.off[GM_Q_DOWNLOAD] = !rommReady();
	static long long tracker[GM_HISTORY_DAYS];
	int known = MOSAIC_HOST_HAS(host, play_time_on);
	for (int i = 0; i < GM_HISTORY_DAYS; i++) tracker[i] = known ? host->play_time_on(i) : MOSAIC_PLAY_UNKNOWN;
	GM_played(&state, today, tracker, GM_HISTORY_DAYS, played);
	streak = GM_streak(played, GM_HISTORY_DAYS, targets.streak_seconds);
	longest = GM_longest(played, GM_HISTORY_DAYS, targets.streak_seconds);
	GM_progress(&state, today, played, GM_HISTORY_DAYS, &targets, progress);
	pickQuests();
}

// 1 when the game at path was played before, as games.played says, or
// else the play history; -1 when neither can say.
static int playedBefore(const char* path) {
	char args[GM_PATH_MAX + 8], reply[64];
	snprintf(args, sizeof(args), "path=%s", path);
	if (ask("games.played", args, reply, sizeof(reply))) {
		long long played = replyNumber(reply, "played", -1);
		if (played == 0 || played == 1) return (int)played;
	}
	if (!MOSAIC_HOST_HAS(host, play_time)) return -1;
	int count = host->play_count(path);
	return count < 0 ? -1 : count > 0;
}

// Marks quests done since the last look and keeps the best streak, saving
// when either changed. Returns how many quests, into newly.
static int settle(int newly[GM_QUESTS]) {
	int best = state.best_streak;
	int n = GM_settle(&state, today, progress, longest > streak ? longest : streak, newly);
	for (int i = 0; i < n; i++) {
		char title[256], message[320];
		GM_title(newly[i], &targets, title, sizeof(title));
		snprintf(message, sizeof(message), "quest done: %s (%s)", GM_questId(newly[i]), title);
		say(message);
	}
	if (n > 0 || state.best_streak != best) save();
	return n;
}

///////////////////////////////////////
// launching

static int preLaunch(const MosaicLaunch* launch) {
	if (!launch || !launch->path || launch->type != MOSAIC_ENTRY_ROM) {
		unlink(GM_LAUNCH_FILE); // paks aren't counted
		return MOSAIC_LAUNCH_CONTINUE;
	}
	GMLaunch pending;
	memset(&pending, 0, sizeof(pending));
	pending.started = (long long)wall_now();
	pending.mono = mono_now();
	pending.fresh = playedBefore(launch->path) == 0;
	if (MOSAIC_HOST_HAS(host, play_time)) {
		long long last = host->last_played(launch->path);
		pending.revisit = last > 0 && pending.started - last >= (long long)GM_REVISIT_DAYS * 24 * 60 * 60;
	}
	snprintf(pending.path, sizeof(pending.path), "%s", launch->path);
	char message[GM_PATH_MAX + 64];
	if (pending.mono < 0 || keepLaunch(&pending) != 0) {
		snprintf(message, sizeof(message), "couldn't keep the launch of %s", launch->path);
	} else {
		snprintf(message, sizeof(message), "launch %s: fresh %i, revisit %i", launch->path, pending.fresh,
			pending.revisit);
	}
	say(message);
	return MOSAIC_LAUNCH_CONTINUE;
}

static void postReturn(void) {
	GMLaunch launch;
	int had = takeLaunch(&launch) == 0;
	time_t now = wall_now();
	char message[GM_PATH_MAX + 64];
	if (had && (launch.started > (long long)now || (long long)now - launch.started > 2LL * GM_SESSION_MAX)) {
		snprintf(message, sizeof(message), "dropped the launch of %s: its clock is off", launch.path);
		say(message);
		had = 0;
	}
	if (had) {
		long long seconds = GM_session(&launch, mono_now());
		GM_launch(&state, GM_dayOf((time_t)launch.started), launch.path, launch.fresh, launch.revisit);
		GM_play(&state, (time_t)launch.started, seconds);
		snprintf(message, sizeof(message), "return %s: %lld seconds", launch.path, seconds < 0 ? 0 : seconds);
		say(message);
		save();
	}
	refresh();
	int newly[GM_QUESTS];
	int n = settle(newly);
	int cheer = had && streak > 0 && played[0] >= targets.streak_seconds && state.cheered != today;
	char text[512];
	GM_toast(newly, n, &targets, streak, cheer, text, sizeof(text));
	if (!text[0]) return;
	if (n == 0 && cheer) {
		state.cheered = today;
		save();
	}
	snprintf(message, sizeof(message), "toast: %s", text);
	say(message);
	if (toasts && MOSAIC_HOST_HAS(host, toast)) host->toast(text);
}

// romm.download_done: a game downloaded from RomM, for the RomM quest
static void onDownload(const char* name, const char* payload, void* data) {
	(void)name;
	(void)data;
	char path[GM_PATH_MAX], message[GM_PATH_MAX + 64];
	if (host->event_value(payload, "path", path, sizeof(path)) <= 0) snprintf(path, sizeof(path), "?");
	GM_download(&state, GM_dayOf(wall_now()));
	save();
	snprintf(message, sizeof(message), "download %s", path);
	say(message);
	refresh();
	int newly[GM_QUESTS];
	int n = settle(newly);
	char text[512];
	GM_toast(newly, n, &targets, streak, 0, text, sizeof(text));
	if (!text[0]) return;
	snprintf(message, sizeof(message), "toast: %s", text);
	say(message);
	if (toasts && MOSAIC_HOST_HAS(host, toast)) host->toast(text);
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
	int n = GM_avoid(rect, hole, parts);
	for (int i = 0; i < n; i++) host->fill_rect(screen, &parts[i], color);
}

// The streak on a pill, then the best streak and the quests done, in room.
static void drawTop(MosaicSurface* screen, const MosaicRect* room) {
	if (room->w <= 0) return;
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	char text[128], summary[256];
	GM_streakText(streak, text, sizeof(text));
	GM_summary(state.best_streak, state.completed, summary, sizeof(summary));
	int w = host->text_width(MOSAIC_FONT_LARGE, text) + 2 * pad;
	if (w > room->w) w = room->w;
	if (w <= 2 * pad) return;
	MosaicRect pill = { room->x, room->y, w, room->h };
	host->fill_round_rect(screen, &pill, room->h / 2, C_MAIN());
	host->draw_text(screen, MOSAIC_FONT_LARGE, text, room->x + pad, room->y + (room->h - large_h) / 2, C_SELECTED(),
		w - 2 * pad);
	int x = room->x + w + pad, right = room->x + room->w;
	if (x < right) {
		host->draw_text(screen, MOSAIC_FONT_SMALL, summary, x, room->y + (room->h - small_h) / 2, C_HINT(), right - x);
	}
}

// One quest in r: its title and progress, over a bar of how far along it is.
static void drawQuest(MosaicSurface* screen, const MosaicRect* r, int quest) {
	int scale = host->screen_scale();
	int pad = 10 * scale;
	int small_h = host->text_height(MOSAIC_FONT_SMALL);
	int large_h = host->text_height(MOSAIC_FONT_LARGE);
	const GMProgress* p = &progress[quest];
	char title[256], cut[256], value[128];
	GM_title(quest, &targets, title, sizeof(title));
	GM_value(quest, p, value, sizeof(value));
	int bar_h = 2 * scale;
	int text_h = r->h - bar_h - scale;
	int value_w = host->text_width(MOSAIC_FONT_SMALL, value);
	if (value_w > r->w / 3) value_w = r->w / 3;
	if (value_w > 0) {
		host->draw_text(screen, MOSAIC_FONT_SMALL, value, r->x + r->w - value_w, r->y + (text_h - small_h) / 2,
			p->done ? C_ACCENT() : C_HINT(), value_w);
	}
	int title_w = r->w - (value_w > 0 ? value_w + pad : 0);
	if (title_w > 0 && host->text_truncate(MOSAIC_FONT_LARGE, title, title_w, cut, sizeof(cut)) >= 0) {
		host->draw_text(screen, MOSAIC_FONT_LARGE, cut, r->x, r->y + (text_h - large_h) / 2,
			p->done ? C_HINT() : C_TEXT(), title_w);
	}
	MosaicRect track = { r->x, r->y + r->h - bar_h, r->w, bar_h };
	host->fill_round_rect(screen, &track, bar_h / 2, themeColor(MOSAIC_COLOR_HINT, 0x999999, 64));
	MosaicRect bar = track;
	bar.w = GM_barWidth(p->value, p->target, track.w);
	if (bar.w > 0) host->fill_round_rect(screen, &bar, bar_h / 2, p->done ? C_ACCENT() : C_MAIN());
}

static void drawQuests(MosaicSurface* screen, const MosaicRect* room) {
	int scale = host->screen_scale();
	int row_h = 30 * scale;
	int fit = room->h / row_h;
	fit_rows = fit;
	if (fit < 1) return;
	first = GM_scroll(first, 0, fit, running_count);
	for (int i = first; i < running_count && i < first + fit; i++) {
		MosaicRect r = { room->x, room->y + (i - first) * row_h, room->w, row_h };
		drawQuest(screen, &r, running[i]);
	}
}

static void drawHints(MosaicSurface* screen) {
	MosaicHint back = { MOSAIC_BUTTON_B, "BACK" };
	host->draw_hints(screen, MOSAIC_HINTS_LEFT, &back, 1, NULL);
}

// What the screen shows, for tests and bug reports.
static void describe(char* out, size_t size) {
	char text[128], summary[256];
	GM_streakText(streak, text, sizeof(text));
	GM_summary(state.best_streak, state.completed, summary, sizeof(summary));
	size_t used = (size_t)snprintf(out, size, "%s, %s;", text, summary);
	int listed = 0;
	for (int i = 0; i < GM_QUESTS && used < size; i++) {
		if (state.off[i]) continue; // not offered, such as the RomM quest without romm
		used += (size_t)snprintf(out + used, size - used, "%s %s %lld/%lld%s", listed++ ? "," : "", GM_questId(i), progress[i].value,
			progress[i].target, !GM_running(&state, today, i) ? " left out" : progress[i].done ? " done" : "");
	}
}

///////////////////////////////////////
// the screen

static int enter(void* data) {
	(void)data;
	first = 0;
	shown[0] = '\0';
	readSettings();
	refresh();
	int newly[GM_QUESTS];
	settle(newly);
	return 0;
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	int before = first;
	if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) first = GM_scroll(first, -1, fit_rows, running_count);
	if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) first = GM_scroll(first, 1, fit_rows, running_count);
	if (first != before) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	if (GM_dayOf(wall_now()) != today) {
		refresh(); // past midnight: a new day, and maybe a new week
		int newly[GM_QUESTS];
		settle(newly);
	}
	MosaicRect hole = GM_hole(&view->status, &view->header);
	fillAround(screen, &view->area, &hole, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000, 255));
	MosaicRect top = GM_topRoom(&view->area, &hole, scale);
	drawTop(screen, &top);
	MosaicRect room = GM_room(&view->area, &hole, scale);
	if (room.w > 0) drawQuests(screen, &room);
	drawHints(screen);
	char line[sizeof(shown)];
	describe(line, sizeof(line));
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

static void openQuests(const char* path, void* data) {
	(void)path;
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the quests screen");
}

static int init(const MosaicHost* h) {
	host = h;
	static const GMWords words = { tr, trPlural };
	GM_setWords(&words);
	if (!MOSAIC_HOST_HAS(host, draw_hints)) {
		say("this launcher has no plugin screens with button hints");
		return 1;
	}
	snprintf(state_dir, sizeof(state_dir), "%s/launcher/" ID, host->userdata_path);
	snprintf(state_path, sizeof(state_path), "%s/state.txt", state_dir);
	readSettings();
	loadState();
	static const MosaicScreenHooks hooks = {
		.size = sizeof(MosaicScreenHooks),
		.enter = enter,
		.input = input,
		.render = render,
	};
	if (host->register_screen(SCREEN_ID, &hooks) != 0) {
		say("can't register the quests screen");
		return 1;
	}
	if (host->register_action(ACTION_ID, "Quests", 0, canOpen, openQuests, NULL) != 0) {
		say("couldn't register " ACTION_ID);
		return 1;
	}
	if (strcmp(host->cfg_get("tile", "on"), "off") != 0
			&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
		say("couldn't add the Quests tile");
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
			say("couldn't bind the quests button");
		}
	}
	if (MOSAIC_HOST_HAS(host, set_names) && host->subscribe("romm.download_done", onDownload, NULL) != 0) {
		say("couldn't subscribe to romm.download_done");
	}
	char message[160];
	snprintf(message, sizeof(message), "quests: play %i minutes, %i games, %i days and %i hours a week; streak at %lld minutes",
		targets.daily_minutes, targets.daily_games, targets.week_days, targets.week_hours, targets.streak_seconds / 60);
	say(message);
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
