#include "battery_model.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int BL_validReading(int level, int charging) {
	return level >= 1 && level <= 100 && (charging == 0 || charging == 1);
}

int BL_formatStart(const BLStart* start, char* out, size_t size) {
	if (strpbrk(start->path, "\t\r\n")) return -1;
	int n = snprintf(out, size, "1\t%lld\t%lld\t%d\t%d\t%s\n",
		start->mono, start->wall, start->level, start->charging, start->path);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

// A whole number in text up to a tab, moving *p past the tab.
static int field(const char** p, long long* out) {
	char* end;
	errno = 0;
	long long v = strtoll(*p, &end, 10);
	if (errno || end == *p || *end != '\t') return -1;
	*out = v;
	*p = end + 1;
	return 0;
}

int BL_parseStart(const char* text, BLStart* out) {
	if (!text || strncmp(text, "1\t", 2) != 0) return -1;
	const char* p = text + 2;
	long long mono, wall, level, charging;
	if (field(&p, &mono) || field(&p, &wall) || field(&p, &level) || field(&p, &charging)) return -1;
	size_t len = strcspn(p, "\r\n");
	if (len == 0 || len >= sizeof(out->path) || strchr(p, '\t')) return -1;
	if (mono < 0 || level < 1 || level > 100 || (charging != 0 && charging != 1)) return -1;
	out->mono = mono;
	out->wall = wall;
	out->level = (int)level;
	out->charging = (int)charging;
	memcpy(out->path, p, len);
	out->path[len] = '\0';
	return 0;
}

int BL_session(const BLStart* start, int end_level, int end_charging, long long end_mono,
		long long min_seconds, int log_charged, BLSession* out) {
	if (!BL_validReading(start->level, start->charging) || !BL_validReading(end_level, end_charging)) return BL_BAD_READING;
	if (end_mono < start->mono) return BL_BAD_TIME;
	long long seconds = end_mono - start->mono;
	if (seconds < min_seconds || seconds == 0) return BL_TOO_SHORT;
	int charged = start->charging || end_charging || end_level > start->level;
	if (charged && !log_charged) return BL_CHARGED;
	out->start = *start;
	out->seconds = seconds;
	out->end_level = end_level;
	out->charged = charged;
	return BL_OK;
}

void BL_gameName(const char* path, char* out, size_t size) {
	if (!size) return;
	const char* base = strrchr(path, '/');
	base = base ? base + 1 : path;
	const char* dot = strrchr(base, '.');
	size_t len = dot && dot != base ? (size_t)(dot - base) : strlen(base);
	if (len >= size) len = size - 1;
	memcpy(out, base, len);
	out[len] = '\0';
}

int BL_csvField(const char* in, char* out, size_t size) {
	size_t len = strlen(in);
	int quote = strpbrk(in, ",\"\r\n") != NULL || (len && (in[0] == ' ' || in[len - 1] == ' '));
	size_t o = 0;
	if (quote) {
		if (o + 1 >= size) return -1;
		out[o++] = '"';
	}
	for (const char* c = in; *c; c++) {
		char ch = *c == '\r' || *c == '\n' ? ' ' : *c;
		if (ch == '"') {
			if (o + 1 >= size) return -1;
			out[o++] = '"';
		}
		if (o + 1 >= size) return -1;
		out[o++] = ch;
	}
	if (quote) {
		if (o + 1 >= size) return -1;
		out[o++] = '"';
	}
	if (o >= size) return -1;
	out[o] = '\0';
	return 0;
}

int BL_formatRow(const BLSession* s, const char* sdcard, char* out, size_t size) {
	char date[32] = "";
	time_t wall = (time_t)s->start.wall;
	struct tm tm;
	if (localtime_r(&wall, &tm)) strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);

	const char* path = s->start.path;
	size_t card = sdcard ? strlen(sdcard) : 0;
	if (card && !strncmp(path, sdcard, card) && path[card] == '/') path += card + 1;

	char name[512], game[1100], where[2100];
	BL_gameName(s->start.path, name, sizeof(name));
	if (BL_csvField(name, game, sizeof(game)) || BL_csvField(path, where, sizeof(where))) return -1;

	char drain[16] = "", rate[32] = "";
	if (!s->charged) {
		int d = s->start.level - s->end_level;
		snprintf(drain, sizeof(drain), "%d", d);
		// tenths of a percent per hour, rounded
		long long tenths = ((long long)d * 36000 + s->seconds / 2) / s->seconds;
		snprintf(rate, sizeof(rate), "%lld.%lld", tenths / 10, tenths % 10);
	}

	int n = snprintf(out, size, "%s,%s,%s,%lld,%d,%d,%s,%s,%s\n", date, game, where, s->seconds,
		s->start.level, s->end_level, drain, rate, s->charged ? "yes" : "no");
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

// Cuts the file back to just after its last newline, or to nothing.
static int cutPartialRow(int fd, off_t size, off_t* new_size) {
	char buf[512];
	off_t end = size;
	while (end > 0) {
		off_t start = end > (off_t)sizeof(buf) ? end - (off_t)sizeof(buf) : 0;
		ssize_t n = pread(fd, buf, (size_t)(end - start), start);
		if (n != end - start) return -1;
		for (ssize_t i = n - 1; i >= 0; i--) {
			if (buf[i] != '\n') continue;
			off_t keep = start + i + 1;
			if (keep != size && ftruncate(fd, keep) != 0) return -1;
			*new_size = keep;
			return 0;
		}
		end = start;
	}
	if (ftruncate(fd, 0) != 0) return -1;
	*new_size = 0;
	return 0;
}

static int writeAll(int fd, const char* text, size_t len) {
	while (len) {
		ssize_t n = write(fd, text, len);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return -1;
		text += n;
		len -= (size_t)n;
	}
	return 0;
}

int BL_append(const char* path, const char* old_path, const char* row, long max_bytes) {
	int fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd < 0) return -1;
	struct stat st;
	off_t size = 0;
	if (fstat(fd, &st) != 0 || cutPartialRow(fd, st.st_size, &size) != 0) {
		close(fd);
		return -1;
	}

	size_t row_len = strlen(row);
	if (size > (off_t)strlen(BL_HEADER) && size + (off_t)row_len > max_bytes) {
		close(fd);
		if (rename(path, old_path) != 0) return -1;
		fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) return -1;
		size = 0;
	}

	char buf[sizeof(BL_HEADER) + BL_ROW_LEN];
	const char* text = row;
	size_t len = row_len;
	if (size == 0) {
		if (row_len >= BL_ROW_LEN) {
			close(fd);
			return -1;
		}
		len = (size_t)snprintf(buf, sizeof(buf), "%s%s", BL_HEADER, row);
		text = buf;
	}

	int ok = lseek(fd, 0, SEEK_END) >= 0 && writeAll(fd, text, len) == 0 && fsync(fd) == 0;
	if (close(fd) != 0) ok = 0;
	return ok ? 0 : -1;
}

///////////////////////////////////////
// the battery.history service

void BL_historyInit(BLHistory* h) {
	memset(h, 0, sizeof(*h));
}

// The CSV field at *p, unquoted, into out, moving *p past it and its
// comma. Returns 0, or -1 at the end of the row or for one too long.
static int nextField(const char** p, char* out, size_t size) {
	const char* s = *p;
	size_t o = 0;
	if (!*s || *s == '\r' || *s == '\n') return -1;
	if (*s == '"') {
		for (s++;; s++) {
			if (!*s || *s == '\n') return -1;
			if (*s == '"' && *++s != '"') break;
			if (o + 1 >= size) return -1;
			out[o++] = *s;
		}
	} else {
		for (; *s && *s != ',' && *s != '\r' && *s != '\n'; s++) {
			if (o + 1 >= size) return -1;
			out[o++] = *s;
		}
	}
	out[o] = '\0';
	if (*s == ',') s++;
	else if (*s && *s != '\r' && *s != '\n') return -1;
	*p = s;
	return 0;
}

static int number(const char* text, long long lo, long long hi, long long* out) {
	char* end;
	errno = 0;
	long long v = strtoll(text, &end, 10);
	if (errno || end == text || *end || v < lo || v > hi) return -1;
	*out = v;
	return 0;
}

int BL_parseRow(const char* line, BLLogged* out) {
	if (!line || !out) return -1;
	char date[32], skip[2200], field[32];
	const char* p = line;
	if (nextField(&p, date, sizeof(date)) || nextField(&p, skip, sizeof(skip)) || nextField(&p, skip, sizeof(skip))) return -1;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	char tail;
	if (sscanf(date, "%4d-%2d-%2d %2d:%2d:%2d%c", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
			&tm.tm_sec, &tail) != 6)
		return -1;
	if (tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 || tm.tm_mday > 31) return -1;
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	tm.tm_isdst = -1;
	time_t when = mktime(&tm);
	if (when == (time_t)-1) return -1;
	long long seconds, start_level, end_level;
	if (nextField(&p, field, sizeof(field)) || number(field, 0, 100LL * 24 * 3600, &seconds)) return -1;
	if (nextField(&p, field, sizeof(field)) || number(field, 0, 100, &start_level)) return -1;
	if (nextField(&p, field, sizeof(field)) || number(field, 0, 100, &end_level)) return -1;
	// drain_percent and percent_per_hour, empty for a session that charged
	if (nextField(&p, skip, sizeof(skip)) || nextField(&p, skip, sizeof(skip))) return -1;
	if (nextField(&p, field, sizeof(field)) || (strcmp(field, "yes") && strcmp(field, "no"))) return -1;
	out->start = (long long)when;
	out->seconds = seconds;
	out->start_level = (int)start_level;
	out->end_level = (int)end_level;
	out->charged = !strcmp(field, "yes");
	return 0;
}

int BL_historyAdd(BLHistory* h, const char* text) {
	int added = 0;
	for (const char* line = text; line && *line;) {
		BLLogged s;
		if (BL_parseRow(line, &s) == 0) {
			// in start order, the oldest going first when it's full
			int at = h->count;
			while (at > 0 && h->sessions[at - 1].start > s.start) at--;
			int keep = 1;
			if (h->count == BL_HISTORY_MAX) {
				if (at == 0) keep = 0;
				else {
					memmove(&h->sessions[0], &h->sessions[1], sizeof(BLLogged) * (size_t)(at - 1));
					at--;
				}
			} else {
				memmove(&h->sessions[at + 1], &h->sessions[at], sizeof(BLLogged) * (size_t)(h->count - at));
				h->count++;
			}
			if (keep) {
				h->sessions[at] = s;
				added++;
			}
		}
		line = strchr(line, '\n');
		if (line) line++;
	}
	return added;
}

int BL_reply(const BLHistory* h, int count, char* out, size_t size) {
	if (!out || !size) return -1;
	out[0] = '\0';
	if (!h) return -1;
	if (count < 1 || count > h->count) count = h->count;
	int first = h->count - count;
	long long seconds = 0;
	for (int i = first; i < h->count; i++) seconds += h->sessions[i].seconds;
	size_t used = 0;
	int n = snprintf(out, size, "count=%i\nseconds=%lld", count, seconds);
	for (int i = first; n >= 0 && (size_t)n < size - used && i < h->count; i++) {
		used += (size_t)n;
		const BLLogged* s = &h->sessions[i];
		n = snprintf(out + used, size - used, "\nsession.%i=%lld,%lld,%i,%i,%i", i - first, s->start, s->seconds,
			s->start_level, s->end_level, s->charged);
	}
	if (n < 0 || (size_t)n >= size - used) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}
