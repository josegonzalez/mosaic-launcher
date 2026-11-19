// Play time limit: a daily limit on games, from the play history stock's
// play tracker keeps (gametimectl's game_logs.sqlite). Once today's play
// across every game reaches the limit, launching a game is refused, and
// with a code set the refusal offers more minutes for today behind that
// code. A header widget shows the time left, in a warning colour at or
// under the warning threshold, and coming back from a game says so too.
// The limit setting is Monday to Friday's, and weekend Saturday and
// Sunday's, which follows it unless set apart.
//
// Bedtime refuses games from its start to its end, across midnight when
// the end comes first, the same way, with the code unlocking the extra
// minutes from then; the header widget says Bedtime meanwhile. A thin
// budget bar across the top of the home screen (the home slot's screen,
// else the root list) shows today's play against the limit, in the
// warning colour once the time left is at or under the threshold.
//
// The limit and bedtime are off by default, so turning the plugin on
// changes nothing until one is set. Only games are limited; paks such as
// Tools still open.
// When the history can't be read (no database before the first game, or
// no sqlite library) games launch as usual, the widget stays hidden, and
// the plugin logs why once.
//
// A game already running isn't stopped when the time runs out: that needs
// minarch to save and quit on a signal, which stock minarch doesn't do.
//
// Other plugins ask where today stands with the limit.left service,
// which takes no arguments: its reply is state=<off, unknown, ok, warn or
// up>, limit=<the daily limit in minutes, 0 while it's off>,
// granted=<extra minutes granted today>, left=<seconds left today, -1
// while the limit is off or the history unknown>, text=<the widget's
// text>, bedtime=<1 while bedtime refuses games, else 0> and
// bedtime_start and bedtime_end=<HH:MM, empty while bedtime is off>.
// limit is today's, weekday or weekend. home-dashboard shows it, and
// gamification leaves out quests that need more time than is left.
//
// The code lives only in this plugin's cfg, set from the Launcher pak's
// settings, and is never logged. A grant is kept as grant_day and
// grant_minutes in the same cfg, and counts only on that day; a bedtime
// unlock as bedtime_until, a Unix time.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "limit_model.h"
#include "mosaic_plugin.h"

#define ID "playtime-limit"
#define FONT MOSAIC_FONT_SMALL
#define LOOK_MS 1000
#define WARN_RGB 0xD64541
#define WARN_INK_RGB 0xFFFFFF
#define BAR_TRACK_ALPHA 64

// the clock, which the host tests set
#ifndef PL_TIME
#define PL_TIME() time(NULL)
#endif

static const MosaicHost* host;
static int weekday_limit = 0, weekend_limit = 0, warn = 0, extra = 0;
static int bedtime_start = -1, bedtime_end = -1; // minutes after midnight, -1 while off
static int bar_on = 1;
static int widget_supported = 0, bar_supported = 0;
static int unknown_logged = 0;

// Where today stands, from a look.
typedef struct Look {
	char day[PL_DAY_LEN];
	long long now;    // Unix time
	int limit;        // today's, weekday or weekend
	int grant;        // minutes granted today
	long long played; // seconds, below 0 for unknown
	long long left;   // seconds, as PL_secondsLeft gives them
	int state;        // enum PlState
	int bedtime;      // 1 while bedtime refuses games
} Look;

// what the widget and the bar show, from the last look
static Look shown;
static char shown_text[32];
static int looked = 0;
static unsigned long next_look = 0;

// what a refused launch's code unlocks
static int pending_limit = 0, pending_bedtime = 0;

// what was last logged, so it's logged only when it changes
static MosaicRect logged_rect;
static char logged_text[sizeof(shown_text)];
static char logged_bar[160];

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[600];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static const char* nameOf(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int bedtimeSet(void) {
	return bedtime_start >= 0 && bedtime_end >= 0 && bedtime_start != bedtime_end;
}

// anything to limit at all, on some day or at some time
static int limiting(void) {
	return weekday_limit > 0 || weekend_limit > 0 || bedtimeSet();
}

// The day, today's limit, and whether it's bedtime, at the Unix time now.
static void clockLook(long long now, Look* out) {
	time_t t = (time_t)now;
	struct tm local;
	out->now = now;
	if (!localtime_r(&t, &local)) {
		out->day[0] = '\0';
		out->limit = weekday_limit;
		out->bedtime = 0;
		return;
	}
	PL_dayKey(&local, out->day);
	out->limit = PL_todayLimit(weekday_limit, weekend_limit, local.tm_wday);
	long long until = 0;
	sscanf(host->cfg_get("bedtime_until", "0"), "%lld", &until);
	out->bedtime = PL_inWindow(bedtime_start, bedtime_end, local.tm_hour * 60 + local.tm_min)
		&& !PL_unlocked(until, now);
}

static int grantToday(const char* day) {
	char grant_day[PL_DAY_LEN];
	snprintf(grant_day, sizeof(grant_day), "%s", host->cfg_get("grant_day", ""));
	return PL_grantToday(grant_day, host->cfg_get("grant_minutes", "0"), day);
}

// Where today stands. The play history is read only with a limit today.
static void look(Look* out) {
	memset(out, 0, sizeof(*out));
	clockLook((long long)PL_TIME(), out);
	out->grant = out->limit > 0 ? grantToday(out->day) : 0;
	out->played = out->limit > 0 ? host->play_time_today() : 0;
	out->left = PL_secondsLeft(out->limit, out->grant, out->played);
	out->state = PL_state(out->limit, out->played, out->left, warn);
	if (out->state == PL_UNKNOWN && !unknown_logged) {
		say("the play history can't be read (no game_logs.sqlite before the first game, or no sqlite library), "
			"so games launch as usual and no time left shows");
		unknown_logged = 1;
	}
}

// The widget's text for seconds left, translated.
static void formatLeft(long long left, char* out, size_t size) {
	const char* words[PL_WORDS_COUNT] = { tr("Time's up"), tr("%lldm left"), tr("%lldh %02lldm left") };
	PL_formatIn(left, words, out, size);
}

static int known(int state) {
	return state != PL_OFF && state != PL_UNKNOWN;
}

// The widget's text for a look: Bedtime, the time left, or none.
static void widgetText(const Look* l, char* out, size_t size) {
	out[0] = '\0';
	if (l->bedtime) snprintf(out, size, "%s", tr("Bedtime"));
	else if (known(l->state)) formatLeft(l->left, out, size);
}

static int barWidth(void) {
	return MOSAIC_HOST_HAS(host, screen_width) ? host->screen_width() : 0;
}

// Looks again, and redraws when what the widget or the bar shows has
// changed: the minute, the state, bedtime, the bar's fill, or the day, as
// at midnight.
static void refresh(void) {
	Look l;
	look(&l);
	char text[sizeof(shown_text)];
	widgetText(&l, text, sizeof(text));
	int width = barWidth();
	int changed = !looked || l.state != shown.state || l.bedtime != shown.bedtime || l.limit != shown.limit
		|| PL_shownMinutes(l.left) != PL_shownMinutes(shown.left) || strcmp(l.day, shown.day) != 0
		|| strcmp(text, shown_text) != 0
		|| (bar_on && bar_supported
			&& PL_barFill(l.played, l.limit, l.grant, width) != PL_barFill(shown.played, shown.limit, shown.grant, width));
	shown = l;
	snprintf(shown_text, sizeof(shown_text), "%s", text);
	looked = 1;
	if (changed) host->request_redraw();
}

static void tick(unsigned long now) {
	if (!limiting()) return;
	if (looked && now < next_look) return;
	next_look = now + LOOK_MS;
	refresh();
}

// limit.left: where today stands, with no arguments.
static int provideLeft(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	(void)args;
	Look l;
	look(&l);
	char text[sizeof(shown_text)] = "";
	if (known(l.state)) formatLeft(l.left, text, sizeof(text));
	return PL_reply(l.state, l.limit, l.grant, l.left, text, l.bedtime, bedtimeSet() ? bedtime_start : -1,
		bedtimeSet() ? bedtime_end : -1, out, size);
}

static void typedCode(const char* text, void* data) {
	(void)data;
	if (!text) return; // cancelled
	if (!PL_codeMatches(host->cfg_get("code", ""), text)) {
		say("wrong code");
		toast(tr("Wrong code"));
		return;
	}
	Look l;
	clockLook((long long)PL_TIME(), &l);
	if (pending_limit) {
		char grant_day[PL_DAY_LEN];
		snprintf(grant_day, sizeof(grant_day), "%s", host->cfg_get("grant_day", ""));
		int total = PL_addGrant(grant_day, host->cfg_get("grant_minutes", "0"), l.day, extra);
		char minutes[16];
		snprintf(minutes, sizeof(minutes), "%i", total);
		host->cfg_set("grant_day", l.day);
		host->cfg_set("grant_minutes", minutes);
		say("granted %i more minutes today, %i in all", extra, total);
	}
	if (pending_bedtime) {
		char until[32];
		snprintf(until, sizeof(until), "%lld", l.now + (long long)extra * 60);
		host->cfg_set("bedtime_until", until);
		say("bedtime unlocked for %i minutes", extra);
	}
	char message[256];
	snprintf(message, sizeof(message), tr("%i more minutes today"), extra);
	toast(message);
	looked = 0;
	refresh();
}

static void askCode(void* data) {
	(void)data;
	host->keyboard(tr("Code for extra time"), "", typedCode, NULL);
}

static int preLaunch(const MosaicLaunch* launch) {
	if (!limiting() || launch->type != MOSAIC_ENTRY_ROM) return MOSAIC_LAUNCH_CONTINUE;
	Look l;
	look(&l);
	int up = l.state == PL_UP;
	if (!up && !l.bedtime) {
		if (known(l.state))
			say("allowed %s: %lld of %i minutes left today", nameOf(launch->path), PL_shownMinutes(l.left), l.limit + l.grant);
		return MOSAIC_LAUNCH_CONTINUE;
	}

	int has_code = host->cfg_get("code", "")[0] != '\0';
	int can_ask = has_code && extra > 0 && MOSAIC_HOST_HAS(host, confirm) && MOSAIC_HOST_HAS(host, keyboard);
	const char* offer = can_ask ? "; offering extra time behind the code" : "";
	if (l.bedtime) {
		char start[PL_CLOCK_LEN], end[PL_CLOCK_LEN];
		PL_formatClock(bedtime_start, start);
		PL_formatClock(bedtime_end, end);
		say("refused %s: bedtime from %s to %s%s", nameOf(launch->path), start, end, offer);
	}
	if (up)
		say("refused %s: %lld seconds played today, the limit is %i minutes%s", nameOf(launch->path), l.played,
			l.limit + l.grant, offer);
	pending_limit = up;
	pending_bedtime = l.bedtime;
	if (can_ask) {
		char message[512];
		snprintf(message, sizeof(message),
			l.bedtime ? tr("It's bedtime. Enter the code for %i more minutes?")
					  : tr("Time's up for today. Enter the code for %i more minutes?"),
			extra);
		host->confirm(message, tr("CODE"), askCode, NULL);
	}
	else toast(l.bedtime ? tr("It's bedtime") : tr("Time's up for today"));
	return MOSAIC_LAUNCH_CANCEL;
}

// back from a game: a warning when time is short or up, or at bedtime
static void postReturn(void) {
	if (!limiting()) return;
	looked = 0;
	refresh();
	if (shown.bedtime) toast(tr("It's bedtime"));
	else if (shown.state == PL_UP) toast(tr("Time's up for today"));
	else if (shown.state == PL_WARN) {
		char message[256];
		snprintf(message, sizeof(message), tr("%s today"), shown_text);
		toast(message);
	}
}

// the day may have changed while the device slept
static void afterResume(void) {
	next_look = 0;
}

static int scale(void) {
	int s = host->screen_scale();
	return s > 0 ? s : 1;
}

static int pad(void) {
	return 12 * scale(); // stock's BUTTON_PADDING, as its pills use
}

static int headerWidth(int max_width, int height) {
	(void)height;
	if (!widget_supported || !looked || !shown_text[0]) return 0;
	int text = host->text_width(FONT, shown_text);
	if (text < 0) return -1;
	int width = text + pad() * 2;
	return width <= max_width ? width : 0;
}

static int warning(void) {
	return shown.bedtime || shown.state == PL_WARN || shown.state == PL_UP;
}

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	int warning_on = warning();
	int pill = warning_on ? WARN_RGB : host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = warning_on ? WARN_INK_RGB : host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	host->fill_round_rect(screen, rect, rect->h / 2, mosaic_color((unsigned int)pill, 255));
	int text_y = rect->y + (rect->h - host->text_height(FONT)) / 2;
	host->draw_text(screen, FONT, shown_text, rect->x + pad(), text_y, mosaic_color((unsigned int)ink, 255), rect->w - pad() * 2);

	if (memcmp(rect, &logged_rect, sizeof(*rect)) != 0 || strcmp(shown_text, logged_text) != 0) {
		say("widget at %i,%i %ix%i: %s%s", rect->x, rect->y, rect->w, rect->h, shown_text, warning_on ? ", warning" : "");
		logged_rect = *rect;
		snprintf(logged_text, sizeof(logged_text), "%s", shown_text);
	}
	return 0;
}

// The budget bar: on the home slot's screen, else on the root list.
static int onHome(const MosaicScreenView* view) {
	if (!view->screen) return 0;
	const char* home = MOSAIC_HOST_HAS(host, home_screen) ? host->home_screen() : NULL;
	if (home) return !strcmp(view->screen, home);
	const char* path = host->current_path ? host->current_path() : NULL;
	return !strcmp(view->screen, "gamelist") && path && host->sdcard_path && !strcmp(path, host->sdcard_path);
}

// A thin bar across the top, in the margin above the status pill: the
// track in the main colour, faint, and today's play over it in the
// accent, or the warning colour.
static int overlayRender(MosaicSurface* overlay, const MosaicScreenView* view) {
	if (!bar_on || !bar_supported || !looked || !known(shown.state) || !onHome(view)) return 0;
	int h = 2 * scale();
	int room = view->status.w > 0 ? view->status.y - view->area.y : h;
	if (room < h) h = room;
	if (h < 1) h = 1;
	MosaicRect track = { view->area.x, view->area.y, view->area.w, h };
	int fill = PL_barFill(shown.played, shown.limit, shown.grant, track.w);
	if (fill < 0) return 0;
	int main = host->theme_color(MOSAIC_COLOR_MAIN);
	int ink = warning() ? WARN_RGB : host->theme_color(MOSAIC_COLOR_ACCENT);
	if (main < 0 || ink < 0) return -1;
	host->fill_rect(overlay, &track, mosaic_color((unsigned int)main, BAR_TRACK_ALPHA));
	MosaicRect done = { track.x, track.y, fill, h };
	if (fill > 0) host->fill_rect(overlay, &done, mosaic_color((unsigned int)ink, 255));

	char line[sizeof(logged_bar)];
	snprintf(line, sizeof(line), "bar over %s at %i,%i %ix%i: %i px of %lld seconds against %i minutes%s", view->screen,
		track.x, track.y, track.w, track.h, fill, shown.played, shown.limit + shown.grant, warning() ? ", warning" : "");
	if (strcmp(line, logged_bar) != 0) {
		say("%s", line);
		snprintf(logged_bar, sizeof(logged_bar), "%s", line);
	}
	return 0;
}

static void limitText(int minutes, char* out, size_t size) {
	if (minutes > 0) snprintf(out, size, "%i minutes", minutes);
	else snprintf(out, size, "off");
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, play_time_today)) {
		say("unsupported: this launcher has no play history");
		return 1;
	}
	// an older cfg's single limit is the weekday one, and the weekend
	// follows it until it's set apart
	weekday_limit = PL_parseMinutes(host->cfg_get("limit", "off"), 0);
	weekend_limit = PL_weekendLimit(host->cfg_get("weekend", "same"), weekday_limit);
	warn = PL_parseMinutes(host->cfg_get("warn", "10"), 0);
	extra = PL_parseMinutes(host->cfg_get("extra", "15"), 0);
	bedtime_start = PL_parseClock(host->cfg_get("bedtime", "off"), -1);
	bedtime_end = bedtime_start >= 0 ? PL_parseClock(host->cfg_get("wake", "07:00"), -1) : -1;
	bar_on = strcmp(host->cfg_get("bar", "on"), "off") != 0;
	widget_supported = MOSAIC_HOST_HAS(host, decorate_cell) && MOSAIC_HOST_HAS(host, draw_text)
		&& MOSAIC_HOST_HAS(host, theme_color);
	bar_supported = MOSAIC_HOST_HAS(host, open_screen) && MOSAIC_HOST_HAS(host, theme_color);

	unknown_logged = 0;
	looked = 0;
	next_look = 0;
	pending_limit = pending_bedtime = 0;
	memset(&shown, 0, sizeof(shown));
	shown.left = -1;
	shown_text[0] = logged_text[0] = logged_bar[0] = '\0';
	memset(&logged_rect, 0, sizeof(logged_rect));

	if (MOSAIC_HOST_HAS(host, call) && host->provide("limit.left", provideLeft) != 0) say("couldn't provide limit.left");
	if (weekday_limit > 0 || weekend_limit > 0) say("budget bar %s", bar_on && bar_supported ? "on" : "off");
	const char* no_code = host->cfg_get("code", "")[0] ? "" : " (no code set)";
	if (!limiting()) say("no daily limit set, so nothing is limited");
	else if (weekday_limit == weekend_limit && weekday_limit > 0)
		say("daily limit %i minutes, warning at %i, %i more behind the code%s", weekday_limit, warn, extra, no_code);
	else if (weekday_limit != weekend_limit) {
		char weekday[24], weekend[24];
		limitText(weekday_limit, weekday, sizeof(weekday));
		limitText(weekend_limit, weekend, sizeof(weekend));
		say("weekday limit %s, weekend limit %s, warning at %i, %i more behind the code%s", weekday, weekend, warn, extra,
			no_code);
	}
	else say("no daily limit set");
	if (bedtimeSet()) {
		char start[PL_CLOCK_LEN], end[PL_CLOCK_LEN];
		PL_formatClock(bedtime_start, start);
		PL_formatClock(bedtime_end, end);
		say("bedtime from %s to %s", start, end);
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.tick = tick,
	.after_resume = afterResume,
	.header_width = headerWidth,
	.header_widget = headerWidget,
	.overlay_render = overlayRender,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
