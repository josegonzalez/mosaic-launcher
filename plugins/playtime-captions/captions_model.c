#include "captions_model.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

// English the caller's words translate, for make pot
#define TR_MARK(text) (text)

#define SEPARATOR " \xC2\xB7 " // " · "
#define CAPTION_MAX 256

static const char* months[12] = { TR_MARK("Jan"), TR_MARK("Feb"), TR_MARK("Mar"), TR_MARK("Apr"), TR_MARK("May"),
	TR_MARK("Jun"), TR_MARK("Jul"), TR_MARK("Aug"), TR_MARK("Sep"), TR_MARK("Oct"), TR_MARK("Nov"), TR_MARK("Dec") };

static PCWords words; // none set: English

void PC_setWords(const PCWords* set) {
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

int PC_formatOf(const char* name) {
	if (name && !strcmp(name, "compact")) return PC_FORMAT_COMPACT;
	if (name && !strcmp(name, "date")) return PC_FORMAT_DATE;
	return PC_FORMAT_RELATIVE;
}

void PC_duration(long long seconds, int format, char* out, size_t size) {
	if (!out || !size) return;
	if (seconds < 60) {
		snprintf(out, size, "%s", tr("<1m"));
		return;
	}
	long long hours = seconds / 3600, minutes = seconds / 60 % 60;
	if (!hours) snprintf(out, size, tr("%lldm"), minutes);
	else if (format == PC_FORMAT_COMPACT || !minutes) snprintf(out, size, tr("%lldh"), hours);
	else snprintf(out, size, tr("%lldh %lldm"), hours, minutes);
}

// Days since 1970-01-01 of a date in the proleptic Gregorian calendar.
static long long civilDays(int year, int month, int day) {
	year -= month <= 2;
	long long era = (year >= 0 ? year : year - 399) / 400;
	long long yoe = year - era * 400;
	long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

static int localDate(long long when, struct tm* out) {
	time_t t = (time_t)when;
	return localtime_r(&t, out) != NULL;
}

int PC_daysBetween(long long when, long long now) {
	struct tm a, b;
	if (!localDate(when, &a) || !localDate(now, &b)) return 0;
	long long days = civilDays(b.tm_year + 1900, b.tm_mon + 1, b.tm_mday) -
		civilDays(a.tm_year + 1900, a.tm_mon + 1, a.tm_mday);
	return days > 0 ? (int)days : 0;
}

// "1 week ago" or "2 weeks ago", in the unit days are counted in
static void ago(int days, char* out, size_t size) {
	const char* text;
	int n;
	if (days < 7) {
		n = days;
		text = trPlural("%i day ago", "%i days ago", (unsigned long)n);
	} else if (days < 30) {
		n = days / 7;
		text = trPlural("%i week ago", "%i weeks ago", (unsigned long)n);
	} else if (days < 365) {
		n = days / 30;
		text = trPlural("%i month ago", "%i months ago", (unsigned long)n);
	} else {
		n = days / 365;
		text = trPlural("%i year ago", "%i years ago", (unsigned long)n);
	}
	snprintf(out, size, text, n);
}

void PC_lastPlayed(long long when, long long now, int format, char* out, size_t size) {
	if (!out || !size) return;
	int days = PC_daysBetween(when, now);
	if (days == 0) {
		snprintf(out, size, "%s", tr("today"));
		return;
	}
	if (format == PC_FORMAT_COMPACT) {
		if (days < 7) snprintf(out, size, tr("%id"), days);
		else if (days < 30) snprintf(out, size, tr("%iw"), days / 7);
		else if (days < 365) snprintf(out, size, tr("%imo"), days / 30);
		else snprintf(out, size, tr("%iy"), days / 365);
		return;
	}
	if (days == 1) {
		snprintf(out, size, "%s", tr("yesterday"));
		return;
	}
	if (format == PC_FORMAT_DATE) {
		struct tm then, today;
		if (!localDate(when, &then) || !localDate(now, &today)) {
			out[0] = '\0';
			return;
		}
		// a month's name and its day, then the year when it isn't this one's
		const char* month = tr(months[then.tm_mon]);
		if (then.tm_year == today.tm_year) snprintf(out, size, tr("%s %i"), month, then.tm_mday);
		else snprintf(out, size, tr("%s %i, %i"), month, then.tm_mday, then.tm_year + 1900);
		return;
	}
	ago(days, out, size);
}

void PC_sessions(int count, int format, char* out, size_t size) {
	if (!out || !size) return;
	if (format == PC_FORMAT_COMPACT) snprintf(out, size, tr("%ix"), count);
	else snprintf(out, size, trPlural("%i session", "%i sessions", (unsigned long)count), count);
}

void PC_achievements(int unlocked, int total, int format, char* out, size_t size) {
	if (!out || !size) return;
	if (format == PC_FORMAT_COMPACT) snprintf(out, size, "%i/%i", unlocked, total);
	else snprintf(out, size, tr("%i/%i achievements"), unlocked, total);
}

int PC_build(const PCStats* stats, long long now, int parts, int format, int never, int max_parts, char* out,
	size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!stats) return 0;
	int known = stats->time >= 0 && stats->last >= 0 && stats->count >= 0;
	int unseen = known && stats->count == 0 && stats->time == 0 && stats->last == 0;
	if (!known || unseen) parts &= PC_PART_ACHIEVEMENTS;
	if (stats->cheevos_total <= 0 || stats->cheevos < 0) parts &= ~PC_PART_ACHIEVEMENTS;
	int written = 0;
	size_t used = 0;
	if (unseen && never && max_parts >= 1) {
		int n = snprintf(out, size, "%s", tr("Never played"));
		if (n < 0 || (size_t)n >= size) {
			out[0] = '\0';
			return 0;
		}
		used = (size_t)n;
		written = 1;
	}
	static const int order[] = { PC_PART_TIME, PC_PART_LAST, PC_PART_SESSIONS, PC_PART_ACHIEVEMENTS };
	for (int i = 0; i < 4 && written < max_parts; i++) {
		if (!(parts & order[i])) continue;
		char part[128];
		if (order[i] == PC_PART_TIME) PC_duration(stats->time, format, part, sizeof(part));
		else if (order[i] == PC_PART_ACHIEVEMENTS)
			PC_achievements(stats->cheevos, stats->cheevos_total, format, part, sizeof(part));
		else if (order[i] == PC_PART_LAST) {
			// a session that hasn't got a start time says nothing about when
			if (stats->last <= 0) continue;
			PC_lastPlayed(stats->last, now, format, part, sizeof(part));
		}
		else PC_sessions(stats->count, format, part, sizeof(part));
		if (!part[0]) continue;
		int n = snprintf(out + used, size - used, "%s%s", written ? SEPARATOR : "", part);
		if (n < 0 || (size_t)n >= size - used) {
			out[used] = '\0';
			break;
		}
		used += (size_t)n;
		written++;
	}
	return written;
}

int PC_fit(const PCStats* stats, long long now, const PCOptions* options, int max_width,
	int (*measure)(const char* text, void* ctx), void* ctx, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!options || !measure || max_width <= 0) return 0;
	int formats[2] = { options->format, PC_FORMAT_COMPACT };
	int tries = options->format == PC_FORMAT_COMPACT ? 1 : 2;
	char text[CAPTION_MAX];
	int all = PC_build(stats, now, options->parts, options->format, options->never, 4, text, sizeof(text));
	for (int n = all; n > 0; n--) {
		for (int f = 0; f < tries; f++) {
			PC_build(stats, now, options->parts, formats[f], options->never, n, text, sizeof(text));
			int w = measure(text, ctx);
			if (w > 0 && w <= max_width) {
				snprintf(out, size, "%s", text);
				return w;
			}
		}
	}
	return 0;
}

int PC_room(const MosaicRect* rect, const MosaicRect* name, int reserve, int corner, int margin, int text_h,
	MosaicRect* out) {
	if (!rect || !out || text_h <= 0) return 0;
	if (reserve < 0) reserve = 0;
	if (corner < 0) corner = 0;
	if (margin < 0) margin = 0;
	int right = rect->x + rect->w - margin;
	int left, y;
	if (name && name->w > 0) {
		left = name->x + name->w + reserve;
		y = name->y + (name->h - text_h) / 2;
	} else {
		left = rect->x + margin;
		y = rect->y + rect->h - margin - text_h;
		if (y < rect->y + corner) return 0;
	}
	if (left < rect->x) left = rect->x;
	if (y < rect->y || y + text_h > rect->y + rect->h || right <= left) return 0;
	*out = (MosaicRect){ left, y, right - left, text_h };
	return 1;
}

int PC_mute(int fg, int bg, int percent) {
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	int rgb = 0;
	for (int shift = 16; shift >= 0; shift -= 8) {
		int a = fg >> shift & 0xFF, b = bg >> shift & 0xFF;
		rgb |= (a + (b - a) * percent / 100) << shift;
	}
	return rgb;
}
