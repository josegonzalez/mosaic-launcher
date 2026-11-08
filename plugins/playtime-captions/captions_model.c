#include "captions_model.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define SEPARATOR " \xC2\xB7 " // " · "
#define CAPTION_MAX 160

static const char* months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov",
	"Dec" };

int PC_formatOf(const char* name) {
	if (name && !strcmp(name, "compact")) return PC_FORMAT_COMPACT;
	if (name && !strcmp(name, "date")) return PC_FORMAT_DATE;
	return PC_FORMAT_RELATIVE;
}

void PC_duration(long long seconds, int format, char* out, size_t size) {
	if (!out || !size) return;
	if (seconds < 60) {
		snprintf(out, size, "<1m");
		return;
	}
	long long hours = seconds / 3600, minutes = seconds / 60 % 60;
	if (!hours) snprintf(out, size, "%lldm", minutes);
	else if (format == PC_FORMAT_COMPACT || !minutes) snprintf(out, size, "%lldh", hours);
	else snprintf(out, size, "%lldh %lldm", hours, minutes);
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

// "1 week ago" or "2 weeks ago"
static void ago(int n, const char* unit, char* out, size_t size) {
	snprintf(out, size, "%i %s%s ago", n, unit, n == 1 ? "" : "s");
}

void PC_lastPlayed(long long when, long long now, int format, char* out, size_t size) {
	if (!out || !size) return;
	int days = PC_daysBetween(when, now);
	if (days == 0) {
		snprintf(out, size, "today");
		return;
	}
	if (format == PC_FORMAT_COMPACT) {
		if (days < 7) snprintf(out, size, "%id", days);
		else if (days < 30) snprintf(out, size, "%iw", days / 7);
		else if (days < 365) snprintf(out, size, "%imo", days / 30);
		else snprintf(out, size, "%iy", days / 365);
		return;
	}
	if (days == 1) {
		snprintf(out, size, "yesterday");
		return;
	}
	if (format == PC_FORMAT_DATE) {
		struct tm then, today;
		if (!localDate(when, &then) || !localDate(now, &today)) {
			out[0] = '\0';
			return;
		}
		if (then.tm_year == today.tm_year) snprintf(out, size, "%s %i", months[then.tm_mon], then.tm_mday);
		else snprintf(out, size, "%s %i, %i", months[then.tm_mon], then.tm_mday, then.tm_year + 1900);
		return;
	}
	if (days < 7) ago(days, "day", out, size);
	else if (days < 30) ago(days / 7, "week", out, size);
	else if (days < 365) ago(days / 30, "month", out, size);
	else ago(days / 365, "year", out, size);
}

void PC_sessions(int count, int format, char* out, size_t size) {
	if (!out || !size) return;
	if (format == PC_FORMAT_COMPACT) snprintf(out, size, "%ix", count);
	else snprintf(out, size, "%i session%s", count, count == 1 ? "" : "s");
}

int PC_build(const PCStats* stats, long long now, int parts, int format, int never, int max_parts, char* out,
	size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!stats || stats->time < 0 || stats->last < 0 || stats->count < 0) return 0;
	if (stats->count == 0 && stats->time == 0 && stats->last == 0) {
		if (!never || max_parts < 1) return 0;
		snprintf(out, size, "Never played");
		return 1;
	}
	int written = 0;
	size_t used = 0;
	static const int order[] = { PC_PART_TIME, PC_PART_LAST, PC_PART_SESSIONS };
	for (int i = 0; i < 3 && written < max_parts; i++) {
		if (!(parts & order[i])) continue;
		char part[64];
		if (order[i] == PC_PART_TIME) PC_duration(stats->time, format, part, sizeof(part));
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
	int all = PC_build(stats, now, options->parts, options->format, options->never, 3, text, sizeof(text));
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
