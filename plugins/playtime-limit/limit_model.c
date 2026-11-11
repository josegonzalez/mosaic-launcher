#include <stdio.h>
#include <string.h>
#include "limit_model.h"

static int isBlank(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int PL_parseMinutes(const char* setting, int fallback) {
	if (!setting) return fallback;
	while (isBlank(*setting)) setting++;
	if (*setting < '0' || *setting > '9') return fallback;
	long value = 0;
	while (*setting >= '0' && *setting <= '9') {
		value = value * 10 + (*setting++ - '0');
		if (value > PL_MINUTES_MAX) return fallback;
	}
	while (isBlank(*setting)) setting++;
	if (*setting || value < 1) return fallback;
	return (int)value;
}

void PL_dayKey(const struct tm* local, char out[PL_DAY_LEN]) {
	// the year is kept to four digits so it always fits
	int year = (local->tm_year + 1900) % 10000;
	if (year < 0) year = 0;
	int month = local->tm_mon + 1, day = local->tm_mday;
	if (month < 1 || month > 12) month = 1;
	if (day < 1 || day > 31) day = 1;
	snprintf(out, PL_DAY_LEN, "%04d-%02d-%02d", year, month, day);
}

int PL_grantToday(const char* grant_day, const char* grant_minutes, const char* today) {
	if (!grant_day || !today || !today[0] || strcmp(grant_day, today) != 0) return 0;
	return PL_parseMinutes(grant_minutes, 0);
}

int PL_addGrant(const char* grant_day, const char* grant_minutes, const char* today, int extra) {
	int total = PL_grantToday(grant_day, grant_minutes, today);
	if (extra > 0) total += extra;
	return total > PL_MINUTES_MAX ? PL_MINUTES_MAX : total;
}

long long PL_secondsLeft(int limit, int grant, long long played) {
	if (limit <= 0 || played < 0) return -1;
	if (grant < 0) grant = 0;
	long long left = ((long long)limit + grant) * 60 - played;
	return left > 0 ? left : 0;
}

int PL_state(int limit, long long played, long long left, int warn) {
	if (limit <= 0) return PL_OFF;
	if (played < 0 || left < 0) return PL_UNKNOWN;
	if (left == 0) return PL_UP;
	if (warn > 0 && left <= (long long)warn * 60) return PL_WARN;
	return PL_OK;
}

long long PL_shownMinutes(long long left) {
	if (left <= 0) return 0;
	return (left + 59) / 60;
}

void PL_format(long long left, char* out, size_t size) {
	static const char* const english[PL_WORDS_COUNT] = { "Time's up", "%lldm left", "%lldh %02lldm left" };
	PL_formatIn(left, english, out, size);
}

void PL_formatIn(long long left, const char* const words[PL_WORDS_COUNT], char* out, size_t size) {
	if (!out || !size) return;
	long long minutes = PL_shownMinutes(left);
	if (minutes == 0) snprintf(out, size, "%s", words[PL_WORDS_UP]);
	else if (minutes < 60) snprintf(out, size, words[PL_WORDS_MINUTES], minutes);
	else snprintf(out, size, words[PL_WORDS_HOURS], minutes / 60, minutes % 60);
}

static void trim(const char* in, char out[PL_CODE_LEN]) {
	out[0] = '\0';
	if (!in) return;
	while (isBlank(*in)) in++;
	size_t len = strlen(in);
	while (len > 0 && isBlank(in[len - 1])) len--;
	if (len > PL_CODE_LEN - 1) len = PL_CODE_LEN - 1;
	memcpy(out, in, len);
	out[len] = '\0';
}

int PL_codeMatches(const char* code, const char* typed) {
	if (!typed) return 0;
	char want[PL_CODE_LEN], got[PL_CODE_LEN];
	trim(code, want);
	trim(typed, got);
	return want[0] && strcmp(want, got) == 0;
}
