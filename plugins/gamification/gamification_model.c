// Gamification's model: see gamification_model.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gamification_model.h"

static GMWords words; // none set: English

void GM_setWords(const GMWords* set) {
	if (set) words = *set;
	else memset(&words, 0, sizeof(words));
}

// Fixed text and a count's form in the caller's words, for make pot
static const char* tr(const char* text) {
	return words.tr ? words.tr(text) : text;
}

static const char* trPlural(const char* singular, const char* plural, unsigned long n) {
	if (words.tr_plural) return words.tr_plural(singular, plural, n);
	return n == 1 ? singular : plural;
}

// stock's sizes, in its units
#define PADDING 10
#define PILL_SIZE 30

static int empty(const MosaicRect* r) {
	return !r || r->w <= 0 || r->h <= 0;
}

///////////////////////////////////////
// days

// days since 1970-01-01 for a proleptic Gregorian date, and back
static int daysFromCivil(int y, int m, int d) {
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	int yoe = y - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static void civilFromDays(int z, int* y, int* m, int* d) {
	z += 719468;
	int era = (z >= 0 ? z : z - 146096) / 146097;
	int doe = z - era * 146097;
	int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = yoe + era * 400 + (*m <= 2);
}

int GM_dayOf(time_t t) {
	struct tm tm;
	if (!localtime_r(&t, &tm)) return 0;
	return daysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

time_t GM_dayStart(int day) {
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	int y, m, d;
	civilFromDays(day, &y, &m, &d);
	tm.tm_year = y - 1900;
	tm.tm_mon = m - 1;
	tm.tm_mday = d;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

int GM_weekday(int day) {
	// 1 January 1970 was a Thursday
	int w = (day + 3) % 7;
	return w < 0 ? w + 7 : w;
}

int GM_weekOf(int day) {
	int shifted = day + 3;
	return shifted >= 0 ? shifted / 7 : -((-shifted + 6) / 7);
}

///////////////////////////////////////
// the state

static const char* const quest_ids[GM_QUESTS] = { "play", "games", "fresh", "revisit", "download", "week_days",
	"week_hours" };

const char* GM_questId(int quest) {
	return quest >= 0 && quest < GM_QUESTS ? quest_ids[quest] : NULL;
}

int GM_weekly(int quest) {
	return quest == GM_Q_WEEK_DAYS || quest == GM_Q_WEEK_HOURS;
}

int GM_timed(int quest) {
	return quest == GM_Q_PLAY || quest == GM_Q_WEEK_HOURS;
}

int GM_period(int quest, int day) {
	return GM_weekly(quest) ? GM_weekOf(day) : day;
}

void GM_init(GMState* state) {
	memset(state, 0, sizeof(*state));
	state->games_day = -1;
	state->cheered = -1;
	for (int i = 0; i < GM_QUESTS; i++) state->done[i] = state->picked[i] = state->left_out[i] = -1;
}

const GMDay* GM_day(const GMState* state, int day) {
	for (int i = 0; i < state->day_count; i++) {
		if (state->days[i].day == day) return &state->days[i];
	}
	return NULL;
}

// The log's day, added in its place (newest first) when it has none,
// dropping the oldest when the log is full. NULL for a day older than every
// day in a full log.
static GMDay* dayFor(GMState* state, int day) {
	int at = 0;
	while (at < state->day_count && state->days[at].day > day) at++;
	if (at < state->day_count && state->days[at].day == day) return &state->days[at];
	if (at >= GM_LOG_DAYS) return NULL;
	int keep = state->day_count < GM_LOG_DAYS ? state->day_count : GM_LOG_DAYS - 1;
	memmove(&state->days[at + 1], &state->days[at], (size_t)(keep - at) * sizeof(GMDay));
	if (state->day_count < GM_LOG_DAYS) state->day_count++;
	memset(&state->days[at], 0, sizeof(GMDay));
	state->days[at].day = day;
	return &state->days[at];
}

static unsigned int hashOf(const char* text) {
	unsigned int h = 2166136261u;
	for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
		h ^= *p;
		h *= 16777619u;
	}
	return h;
}

void GM_launch(GMState* state, int day, const char* path, int fresh, int revisit) {
	GMDay* d = dayFor(state, day);
	if (!d) return;
	d->launches++;
	if (fresh) d->fresh++;
	if (revisit) d->revisits++;
	if (state->games_day != day) {
		// a new day: only its games count, and an older day's count stays
		if (state->games_day > day) {
			d->games++;
			return;
		}
		state->games_day = day;
		state->game_count = 0;
	}
	unsigned int h = hashOf(path ? path : "");
	for (int i = 0; i < state->game_count; i++) {
		if (state->games[i] == h) return;
	}
	if (state->game_count < GM_TODAY_GAMES) state->games[state->game_count++] = h;
	d->games++;
}

void GM_download(GMState* state, int day) {
	GMDay* d = dayFor(state, day);
	if (d) d->downloads++;
}

void GM_play(GMState* state, time_t start, long long seconds) {
	if (seconds < 1 || seconds > GM_SESSION_MAX) return;
	long long at = (long long)start, end = at + seconds;
	while (at < end) {
		int day = GM_dayOf((time_t)at);
		long long next = (long long)GM_dayStart(day + 1);
		if (next <= at) next = at + 1; // a clock that can't say: all of it on this day
		long long part = (next < end ? next : end) - at;
		GMDay* d = dayFor(state, day);
		if (d) d->seconds += part;
		at += part;
	}
}

int GM_format(const GMState* state, char* out, size_t size) {
	size_t used = 0;
#define PUT(...) do { \
	int n = snprintf(out + used, size - used, __VA_ARGS__); \
	if (n < 0 || (size_t)n >= size - used) return -1; \
	used += (size_t)n; \
} while (0)
	if (!size) return -1;
	out[0] = '\0';
	PUT("best=%i\n", state->best_streak);
	PUT("completed=%i\n", state->completed);
	PUT("cheered=%i\n", state->cheered);
	for (int i = 0; i < GM_QUESTS; i++) {
		if (state->done[i] >= 0) PUT("done=%s %i\n", quest_ids[i], state->done[i]);
	}
	for (int i = 0; i < GM_QUESTS; i++) {
		if (state->picked[i] >= 0) PUT("picked=%s %i\n", quest_ids[i], state->picked[i]);
		if (state->left_out[i] >= 0) PUT("left_out=%s %i\n", quest_ids[i], state->left_out[i]);
	}
	for (int i = 0; i < state->day_count; i++) {
		const GMDay* d = &state->days[i];
		PUT("day=%i %lld %i %i %i %i %i\n", d->day, d->seconds, d->launches, d->games, d->fresh, d->revisits, d->downloads);
	}
	if (state->games_day >= 0) {
		PUT("games=%i", state->games_day);
		for (int i = 0; i < state->game_count; i++) PUT(" %08x", state->games[i]);
		PUT("\n");
	}
#undef PUT
	return (int)used;
}

int GM_parse(GMState* state, const char* text) {
	const char* line = text;
	while (line && *line) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		char buf[GM_TODAY_GAMES * 10 + 64];
		if (len < sizeof(buf)) {
			memcpy(buf, line, len);
			buf[len] = '\0';
			int a, b, c, d, e, f = 0;
			long long s;
			char id[32];
			if (sscanf(buf, "best=%i", &a) == 1 && a >= 0) {
				state->best_streak = a;
			} else if (sscanf(buf, "completed=%i", &a) == 1 && a >= 0) {
				state->completed = a;
			} else if (sscanf(buf, "cheered=%i", &a) == 1) {
				state->cheered = a;
			} else if (sscanf(buf, "done=%31s %i", id, &a) == 2) {
				for (int i = 0; i < GM_QUESTS; i++) {
					if (strcmp(id, quest_ids[i]) == 0) state->done[i] = a;
				}
			} else if (sscanf(buf, "picked=%31s %i", id, &a) == 2) {
				for (int i = 0; i < GM_QUESTS; i++) {
					if (strcmp(id, quest_ids[i]) == 0) state->picked[i] = a;
				}
			} else if (sscanf(buf, "left_out=%31s %i", id, &a) == 2) {
				for (int i = 0; i < GM_QUESTS; i++) {
					if (strcmp(id, quest_ids[i]) == 0) state->left_out[i] = a;
				}
			} else if (sscanf(buf, "day=%i %lld %i %i %i %i %i", &a, &s, &b, &c, &d, &e, &f) >= 6) {
				// a day from before downloads were counted has six numbers
				GMDay* day = dayFor(state, a);
				if (day && s >= 0 && b >= 0 && c >= 0 && d >= 0 && e >= 0 && f >= 0) {
					*day = (GMDay){ a, s, b, c, d, e, f };
				}
			} else if (strncmp(buf, "games=", 6) == 0) {
				char* p = buf + 6;
				char* next;
				long v = strtol(p, &next, 10);
				if (next != p) {
					state->games_day = (int)v;
					state->game_count = 0;
					p = next;
					while (state->game_count < GM_TODAY_GAMES) {
						unsigned long h = strtoul(p, &next, 16);
						if (next == p) break;
						state->games[state->game_count++] = (unsigned int)h;
						p = next;
					}
				}
			}
		}
		line = end ? end + 1 : NULL;
	}
	return 0;
}

///////////////////////////////////////
// the pending launch

int GM_formatLaunch(const GMLaunch* launch, char* out, size_t size) {
	int n = snprintf(out, size, "%lld %lld %i %i %s\n", launch->started, launch->mono, launch->fresh ? 1 : 0,
		launch->revisit ? 1 : 0, launch->path);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

int GM_parseLaunch(const char* line, GMLaunch* out) {
	memset(out, 0, sizeof(*out));
	int used = 0;
	if (!line || sscanf(line, "%lld %lld %i %i %n", &out->started, &out->mono, &out->fresh, &out->revisit, &used) < 4
			|| used <= 0) {
		return -1;
	}
	const char* path = line + used;
	size_t len = strcspn(path, "\n");
	if (len == 0 || len >= sizeof(out->path)) return -1;
	memcpy(out->path, path, len);
	out->path[len] = '\0';
	return 0;
}

long long GM_session(const GMLaunch* launch, long long mono_now) {
	long long seconds = mono_now - launch->mono;
	return seconds < 0 || seconds > GM_SESSION_MAX ? -1 : seconds;
}

///////////////////////////////////////
// streaks and quests

void GM_played(const GMState* state, int today, const long long* tracker, int days, long long* played) {
	for (int i = 0; i < days; i++) {
		const GMDay* d = GM_day(state, today - i);
		long long own = d ? d->seconds : 0;
		long long theirs = tracker && tracker[i] > 0 ? tracker[i] : 0;
		played[i] = own > theirs ? own : theirs;
	}
}

static int counts(long long seconds, long long need) {
	return seconds > 0 && seconds >= need;
}

int GM_streak(const long long* played, int days, long long need) {
	if (days <= 0) return 0;
	int i = counts(played[0], need) ? 0 : 1;
	int streak = 0;
	while (i < days && counts(played[i], need)) {
		streak++;
		i++;
	}
	return streak;
}

int GM_longest(const long long* played, int days, long long need) {
	int best = 0, run = 0;
	for (int i = 0; i < days; i++) {
		run = counts(played[i], need) ? run + 1 : 0;
		if (run > best) best = run;
	}
	return best;
}

void GM_progress(const GMState* state, int today, const long long* played, int days, const GMTargets* targets,
	GMProgress out[GM_QUESTS]) {
	const GMDay* d = GM_day(state, today);
	long long today_s = days > 0 ? played[0] : 0;
	int into_week = GM_weekday(today);
	long long week_s = 0;
	int week_days = 0;
	for (int i = 0; i <= into_week && i < days; i++) {
		week_s += played[i];
		if (counts(played[i], targets->streak_seconds)) week_days++;
	}
	out[GM_Q_PLAY] = (GMProgress){ today_s / 60, targets->daily_minutes, 0 };
	out[GM_Q_GAMES] = (GMProgress){ d ? d->games : 0, targets->daily_games, 0 };
	out[GM_Q_FRESH] = (GMProgress){ d ? d->fresh : 0, 1, 0 };
	out[GM_Q_REVISIT] = (GMProgress){ d ? d->revisits : 0, 1, 0 };
	out[GM_Q_DOWNLOAD] = (GMProgress){ d ? d->downloads : 0, 1, 0 };
	out[GM_Q_WEEK_DAYS] = (GMProgress){ week_days, targets->week_days, 0 };
	out[GM_Q_WEEK_HOURS] = (GMProgress){ week_s / 60, (long long)targets->week_hours * 60, 0 };
	for (int i = 0; i < GM_QUESTS; i++) {
		if (out[i].target < 1) out[i].target = 1;
		out[i].done = out[i].value >= out[i].target;
	}
}

long long GM_weekLeft(int today, long long left_today, int limit_minutes) {
	if (left_today < 0) return -1;
	int days_after = 6 - GM_weekday(today);
	return left_today + (limit_minutes > 0 ? (long long)limit_minutes * 60 * days_after : 0);
}

int GM_pick(GMState* state, int today, const GMProgress* progress, long long left_today, long long left_week,
	int left[GM_QUESTS], int* left_count) {
	int picked = 0;
	*left_count = 0;
	for (int i = 0; i < GM_QUESTS; i++) {
		int period = GM_period(i, today);
		if (state->picked[i] == period) continue;
		state->picked[i] = period;
		picked++;
		long long time_left = GM_weekly(i) ? left_week : left_today;
		if (!GM_timed(i) || time_left < 0 || progress[i].done) continue;
		long long need = (progress[i].target - progress[i].value) * 60; // minutes
		if (need <= time_left) continue;
		state->left_out[i] = period;
		left[(*left_count)++] = i;
	}
	return picked;
}

int GM_running(const GMState* state, int today, int quest) {
	return quest >= 0 && quest < GM_QUESTS && !state->off[quest] && state->left_out[quest] != GM_period(quest, today);
}

int GM_settle(GMState* state, int today, const GMProgress* progress, int streak, int newly[GM_QUESTS]) {
	int n = 0;
	for (int i = 0; i < GM_QUESTS; i++) {
		int period = GM_period(i, today);
		if (!progress[i].done || state->done[i] == period || !GM_running(state, today, i)) continue;
		state->done[i] = period;
		state->completed++;
		newly[n++] = i;
	}
	if (streak > state->best_streak) state->best_streak = streak;
	return n;
}

///////////////////////////////////////
// text

void GM_duration(long long minutes, char* out, size_t size) {
	if (minutes < 0) minutes = 0;
	long long h = minutes / 60, m = minutes % 60;
	if (h == 0) snprintf(out, size, tr("%lldm"), m);
	else if (m == 0) snprintf(out, size, tr("%lldh"), h);
	else snprintf(out, size, tr("%lldh %lldm"), h, m);
}

// Where English reads the same for one as for more ("Play 1 different
// games today"), both forms are that English, so it doesn't change, and a
// translation can still have its own forms.
void GM_title(int quest, const GMTargets* targets, char* out, size_t size) {
	int n;
	switch (quest) {
	case GM_Q_PLAY:
		n = targets->daily_minutes;
		if (n % 60) snprintf(out, size, trPlural("Play for %i minutes today", "Play for %i minutes today", (unsigned long)n), n);
		else snprintf(out, size, trPlural("Play for %i hour today", "Play for %i hours today", (unsigned long)(n / 60)), n / 60);
		break;
	case GM_Q_GAMES:
		n = targets->daily_games;
		snprintf(out, size, trPlural("Play %i different games today", "Play %i different games today", (unsigned long)n), n);
		break;
	case GM_Q_FRESH:
		snprintf(out, size, "%s", tr("Start a game you've never played"));
		break;
	case GM_Q_REVISIT:
		snprintf(out, size, tr("Go back to a game you haven't played in %i days"), GM_REVISIT_DAYS);
		break;
	case GM_Q_DOWNLOAD:
		snprintf(out, size, "%s", tr("Download a game from RomM"));
		break;
	case GM_Q_WEEK_DAYS:
		n = targets->week_days;
		snprintf(out, size, trPlural("Play on %i days this week", "Play on %i days this week", (unsigned long)n), n);
		break;
	case GM_Q_WEEK_HOURS:
		n = targets->week_hours;
		snprintf(out, size, trPlural("Play for %i hour this week", "Play for %i hours this week", (unsigned long)n), n);
		break;
	default:
		if (size) out[0] = '\0';
	}
}

void GM_value(int quest, const GMProgress* progress, char* out, size_t size) {
	if (progress->done) {
		snprintf(out, size, "%s", tr("Done"));
	} else if (quest == GM_Q_PLAY || quest == GM_Q_WEEK_HOURS) {
		char value[32], target[32];
		GM_duration(progress->value, value, sizeof(value));
		GM_duration(progress->target, target, sizeof(target));
		snprintf(out, size, "%s / %s", value, target);
	} else {
		snprintf(out, size, "%lld / %lld", progress->value, progress->target);
	}
}

void GM_streakText(int streak, char* out, size_t size) {
	if (streak <= 0) snprintf(out, size, "%s", tr("No streak"));
	else snprintf(out, size, trPlural("%i day streak", "%i day streak", (unsigned long)streak), streak);
}

void GM_summary(int best, int completed, char* out, size_t size) {
	char days[128], quests[128];
	snprintf(days, sizeof(days), trPlural("Best %i day", "Best %i days", (unsigned long)best), best);
	snprintf(quests, sizeof(quests), trPlural("%i quest done", "%i quests done", (unsigned long)completed), completed);
	snprintf(out, size, "%s \xc2\xb7 %s", days, quests);
}

void GM_toast(const int* newly, int count, const GMTargets* targets, int streak, int cheer, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (count == 1) {
		char title[256];
		GM_title(newly[0], targets, title, sizeof(title));
		snprintf(out, size, tr("Quest done: %s"), title);
	} else if (count > 1) {
		snprintf(out, size, trPlural("%i quest done", "%i quests done", (unsigned long)count), count);
	} else if (cheer && streak > 0) {
		GM_streakText(streak, out, size);
	}
}

///////////////////////////////////////
// layout

int GM_scroll(int first, int by, int fit, int count) {
	int last = count - (fit > 0 ? fit : 1);
	if (last < 0) last = 0;
	first += by;
	if (first > last) first = last;
	return first < 0 ? 0 : first;
}

int GM_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]) {
	if (empty(r)) return 0;
	int x1 = r->x, y1 = r->y, x2 = r->x + r->w, y2 = r->y + r->h;
	if (empty(hole) || hole->x >= x2 || hole->y >= y2 || hole->x + hole->w <= x1 || hole->y + hole->h <= y1) {
		out[0] = *r;
		return 1;
	}
	int hx1 = hole->x > x1 ? hole->x : x1, hy1 = hole->y > y1 ? hole->y : y1;
	int hx2 = hole->x + hole->w < x2 ? hole->x + hole->w : x2, hy2 = hole->y + hole->h < y2 ? hole->y + hole->h : y2;
	int n = 0;
	if (hy1 > y1) out[n++] = (MosaicRect){ x1, y1, r->w, hy1 - y1 };
	if (hx1 > x1) out[n++] = (MosaicRect){ x1, hy1, hx1 - x1, hy2 - hy1 };
	if (hx2 < x2) out[n++] = (MosaicRect){ hx2, hy1, x2 - hx2, hy2 - hy1 };
	if (hy2 < y2) out[n++] = (MosaicRect){ x1, hy2, r->w, y2 - hy2 };
	return n;
}

MosaicRect GM_hole(const MosaicRect* status, const MosaicRect* header) {
	MosaicRect none = { 0, 0, 0, 0 };
	int has_status = !empty(status), has_header = !empty(header);
	if (!has_status && !has_header) return none;
	if (!has_header) return *status;
	if (!has_status) return *header;
	int x1 = status->x < header->x ? status->x : header->x;
	int y1 = status->y < header->y ? status->y : header->y;
	int x2 = status->x + status->w > header->x + header->w ? status->x + status->w : header->x + header->w;
	int y2 = status->y + status->h > header->y + header->h ? status->y + status->h : header->y + header->h;
	return (MosaicRect){ x1, y1, x2 - x1, y2 - y1 };
}

MosaicRect GM_topRoom(const MosaicRect* area, const MosaicRect* hole, int scale) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(area) || scale < 1) return none;
	int pad = PADDING * scale;
	int x = area->x + pad, y = area->y + pad, h = PILL_SIZE * scale;
	int right = area->x + area->w - pad;
	if (!empty(hole) && hole->y < y + h && hole->y + hole->h > y && hole->x - pad < right) right = hole->x - pad;
	if (right <= x) return none;
	return (MosaicRect){ x, y, right - x, h };
}

MosaicRect GM_room(const MosaicRect* area, const MosaicRect* hole, int scale) {
	MosaicRect none = { 0, 0, 0, 0 };
	if (empty(area) || scale < 1) return none;
	int pad = PADDING * scale, pill = PILL_SIZE * scale;
	int top = area->y + pad + pill + pad;
	if (!empty(hole) && hole->y + hole->h + pad > top) top = hole->y + hole->h + pad;
	int bottom = area->y + area->h - pad - pill - pad;
	MosaicRect room = { area->x + pad, top, area->w - 2 * pad, bottom - top };
	return empty(&room) ? none : room;
}

int GM_barWidth(long long value, long long target, int w) {
	if (w <= 0 || value <= 0 || target <= 0) return 0;
	if (value >= target) return w;
	int part = (int)(value * w / target);
	return part < 1 ? 1 : part;
}
