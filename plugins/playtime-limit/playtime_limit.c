// Play time limit: a daily limit on games, from the play history stock's
// play tracker keeps (gametimectl's game_logs.sqlite). Once today's play
// across every game reaches the limit, launching a game is refused, and
// with a code set the refusal offers more minutes for today behind that
// code. A header widget shows the time left, in a warning colour at or
// under the warning threshold, and coming back from a game says so too.
//
// The limit is off by default, so turning the plugin on changes nothing
// until one is set. Only games are limited; paks such as Tools still open.
// When the history can't be read (no database before the first game, or
// no sqlite library) games launch as usual, the widget stays hidden, and
// the plugin logs why once.
//
// A game already running isn't stopped when the time runs out: that needs
// minarch to save and quit on a signal, which stock minarch doesn't do.
//
// The code lives only in this plugin's cfg, set from the Launcher pak's
// settings, and is never logged. A grant is kept as grant_day and
// grant_minutes in the same cfg, and counts only on that day.

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

static const MosaicHost* host;
static int limit = 0, warn = 0, extra = 0;
static int widget_supported = 0;
static int unknown_logged = 0;

// what the widget shows, from the last look
static int shown_state = PL_OFF;
static long long shown_left = -1;
static char shown_day[PL_DAY_LEN];
static char shown_text[32];
static int looked = 0;
static unsigned long next_look = 0;

// what was last logged, so it's logged only when it changes
static MosaicRect logged_rect;
static char logged_text[sizeof(shown_text)];

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[600];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static const char* nameOf(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void today(char out[PL_DAY_LEN]) {
	time_t now = time(NULL);
	struct tm local;
	if (!localtime_r(&now, &local)) {
		out[0] = '\0';
		return;
	}
	PL_dayKey(&local, out);
}

static int grantToday(const char* day) {
	char grant_day[PL_DAY_LEN];
	snprintf(grant_day, sizeof(grant_day), "%s", host->cfg_get(ID, "grant_day", ""));
	return PL_grantToday(grant_day, host->cfg_get(ID, "grant_minutes", "0"), day);
}

// Where today stands: an enum PlState, with the seconds left in *left.
static int look(char day[PL_DAY_LEN], long long* left, long long* played) {
	today(day);
	*played = host->play_time_today();
	*left = PL_secondsLeft(limit, grantToday(day), *played);
	int state = PL_state(limit, *played, *left, warn);
	if (state == PL_UNKNOWN && !unknown_logged) {
		say("the play history can't be read (no game_logs.sqlite before the first game, or no sqlite library), "
			"so games launch as usual and no time left shows");
		unknown_logged = 1;
	}
	return state;
}

// Looks again, and redraws when what the widget shows has changed: the
// minute, the state, or the day, as at midnight.
static void refresh(void) {
	char day[PL_DAY_LEN];
	long long left, played;
	int state = look(day, &left, &played);
	char text[sizeof(shown_text)] = "";
	if (state != PL_OFF && state != PL_UNKNOWN) PL_format(left, text, sizeof(text));
	int changed = !looked || state != shown_state || PL_shownMinutes(left) != PL_shownMinutes(shown_left)
		|| strcmp(day, shown_day) != 0 || strcmp(text, shown_text) != 0;
	shown_state = state;
	shown_left = left;
	snprintf(shown_day, sizeof(shown_day), "%s", day);
	snprintf(shown_text, sizeof(shown_text), "%s", text);
	looked = 1;
	if (changed) host->request_redraw();
}

static void tick(unsigned long now) {
	if (limit <= 0) return;
	if (looked && now < next_look) return;
	next_look = now + LOOK_MS;
	refresh();
}

static void typedCode(const char* text, void* data) {
	(void)data;
	if (!text) return; // cancelled
	if (!PL_codeMatches(host->cfg_get(ID, "code", ""), text)) {
		say("wrong code");
		toast("Wrong code");
		return;
	}
	char day[PL_DAY_LEN];
	today(day);
	char grant_day[PL_DAY_LEN];
	snprintf(grant_day, sizeof(grant_day), "%s", host->cfg_get(ID, "grant_day", ""));
	int total = PL_addGrant(grant_day, host->cfg_get(ID, "grant_minutes", "0"), day, extra);
	char minutes[16];
	snprintf(minutes, sizeof(minutes), "%i", total);
	host->cfg_set(ID, "grant_day", day);
	host->cfg_set(ID, "grant_minutes", minutes);
	say("granted %i more minutes today, %i in all", extra, total);
	char message[64];
	snprintf(message, sizeof(message), "%i more minutes today", extra);
	toast(message);
	looked = 0;
	refresh();
}

static void askCode(void* data) {
	(void)data;
	host->keyboard("Code for extra time", "", typedCode, NULL);
}

static int preLaunch(const MosaicLaunch* launch) {
	if (limit <= 0 || launch->type != MOSAIC_ENTRY_ROM) return MOSAIC_LAUNCH_CONTINUE;
	char day[PL_DAY_LEN];
	long long left, played;
	int state = look(day, &left, &played);
	if (state == PL_UNKNOWN) return MOSAIC_LAUNCH_CONTINUE;
	if (state != PL_UP) {
		say("allowed %s: %lld of %i minutes left today", nameOf(launch->path), PL_shownMinutes(left), limit + grantToday(day));
		return MOSAIC_LAUNCH_CONTINUE;
	}

	int has_code = host->cfg_get(ID, "code", "")[0] != '\0';
	int can_ask = has_code && extra > 0 && MOSAIC_HOST_HAS(host, confirm) && MOSAIC_HOST_HAS(host, keyboard);
	say("refused %s: %lld seconds played today, the limit is %i minutes%s", nameOf(launch->path), played,
		limit + grantToday(day), can_ask ? "; offering extra time behind the code" : "");
	if (can_ask) {
		char message[96];
		snprintf(message, sizeof(message), "Time's up for today. Enter the code for %i more minutes?", extra);
		host->confirm(message, "CODE", askCode, NULL);
	}
	else toast("Time's up for today");
	return MOSAIC_LAUNCH_CANCEL;
}

// back from a game: a warning when time is short or up
static void postReturn(void) {
	if (limit <= 0) return;
	looked = 0;
	refresh();
	if (shown_state == PL_UP) toast("Time's up for today");
	else if (shown_state == PL_WARN) {
		char message[48];
		snprintf(message, sizeof(message), "%s today", shown_text);
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

static int headerWidget(MosaicSurface* screen, const MosaicRect* rect) {
	int warning = shown_state == PL_WARN || shown_state == PL_UP;
	int pill = warning ? WARN_RGB : host->theme_color(MOSAIC_COLOR_ACCENT);
	int ink = warning ? WARN_INK_RGB : host->theme_color(MOSAIC_COLOR_HINT);
	if (pill < 0 || ink < 0) return -1;
	host->fill_round_rect(screen, rect, rect->h / 2, mosaic_color((unsigned int)pill, 255));
	int text_y = rect->y + (rect->h - host->text_height(FONT)) / 2;
	host->draw_text(screen, FONT, shown_text, rect->x + pad(), text_y, mosaic_color((unsigned int)ink, 255), rect->w - pad() * 2);

	if (memcmp(rect, &logged_rect, sizeof(*rect)) != 0 || strcmp(shown_text, logged_text) != 0) {
		say("widget at %i,%i %ix%i: %s%s", rect->x, rect->y, rect->w, rect->h, shown_text, warning ? ", warning" : "");
		logged_rect = *rect;
		snprintf(logged_text, sizeof(logged_text), "%s", shown_text);
	}
	return 0;
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, play_time_today)) {
		say("unsupported: this launcher has no play history");
		return 1;
	}
	limit = PL_parseMinutes(host->cfg_get(ID, "limit", "off"), 0);
	warn = PL_parseMinutes(host->cfg_get(ID, "warn", "10"), 0);
	extra = PL_parseMinutes(host->cfg_get(ID, "extra", "15"), 0);
	widget_supported = MOSAIC_HOST_HAS(host, decorate_cell) && MOSAIC_HOST_HAS(host, draw_text)
		&& MOSAIC_HOST_HAS(host, theme_color);

	unknown_logged = 0;
	looked = 0;
	next_look = 0;
	shown_state = PL_OFF;
	shown_left = -1;
	shown_day[0] = shown_text[0] = logged_text[0] = '\0';
	memset(&logged_rect, 0, sizeof(logged_rect));

	if (limit <= 0) say("no daily limit set, so nothing is limited");
	else say("daily limit %i minutes, warning at %i, %i more behind the code%s", limit, warn, extra,
		host->cfg_get(ID, "code", "")[0] ? "" : " (no code set)");
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
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
