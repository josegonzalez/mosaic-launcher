#include "band_model.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char* const keys[MB_FIELD_COUNT] = { "year", "publisher", "genre", "developer", "players" };

const char* MB_key(int field) {
	return field >= 0 && field < MB_FIELD_COUNT ? keys[field] : NULL;
}

static int fieldOf(const char* key, size_t n) {
	for (int i = 0; i < MB_FIELD_COUNT; i++)
		if (strlen(keys[i]) == n && !strncasecmp(keys[i], key, n)) return i;
	return -1;
}

int MB_fields(const char* list, int* fields) {
	if (!list || !fields) return 0;
	int count = 0;
	const char* p = list;
	while (*p) {
		while (*p == ',' || isspace((unsigned char)*p)) p++;
		const char* start = p;
		while (*p && *p != ',') p++;
		const char* end = p;
		while (end > start && isspace((unsigned char)end[-1])) end--;
		if (end == start) continue;
		int field = fieldOf(start, (size_t)(end - start));
		if (field < 0) continue;
		int seen = 0;
		for (int i = 0; i < count; i++) seen |= fields[i] == field;
		if (!seen && count < MB_FIELD_COUNT) fields[count++] = field;
	}
	return count;
}

int MB_clean(int field, const char* value, char* out, size_t size) {
	if (!out || size == 0) return 0;
	out[0] = '\0';
	if (!value) return 0;
	while (isspace((unsigned char)*value)) value++;
	size_t n = strlen(value);
	while (n && isspace((unsigned char)value[n - 1])) n--;
	if (!n) return 0;
	if (field == MB_YEAR) {
		for (size_t i = 0; i + 4 <= n; i++) {
			if (isdigit((unsigned char)value[i]) && isdigit((unsigned char)value[i + 1])
				&& isdigit((unsigned char)value[i + 2]) && isdigit((unsigned char)value[i + 3])) {
				snprintf(out, size, "%.4s", value + i);
				return 1;
			}
		}
	}
	if (n >= size) {
		// cut between UTF-8 characters
		n = size - 1;
		while (n && ((unsigned char)value[n] & 0xC0) == 0x80) n--;
		while (n && isspace((unsigned char)value[n - 1])) n--;
	}
	memcpy(out, value, n);
	out[n] = '\0';
	return n > 0;
}

// Joins the first keep non-empty values into out.
static int joinFirst(const char* const* values, int count, const char* sep, int keep, char* out, size_t size) {
	size_t used = 0;
	int joined = 0;
	out[0] = '\0';
	for (int i = 0; i < count && joined < keep; i++) {
		if (!values[i] || !values[i][0]) continue;
		int n = snprintf(out + used, size - used, "%s%s", joined ? sep : "", values[i]);
		if (n < 0 || (size_t)n >= size - used) {
			// it didn't fit in out: keep what did, without a dangling separator
			if (!joined) return 1;
			out[used] = '\0';
			return joined;
		}
		used += (size_t)n;
		joined++;
	}
	return joined;
}

int MB_join(const char* const* values, int count, const char* sep, int max_width, MBMeasure measure, void* data,
	char* out, size_t size) {
	if (!values || count < 0 || !sep || !measure || !out || size == 0) return -1;
	int present = 0;
	for (int i = 0; i < count; i++) present += values[i] && values[i][0];
	out[0] = '\0';
	for (int keep = present; keep > 0; keep--) {
		int joined = joinFirst(values, count, sep, keep, out, size);
		if (keep == 1 || measure(out, data) <= max_width) return joined;
	}
	return 0;
}

///////////////////////////////////////
// the info.format service

void MB_infoPath(const char* path, char* out, size_t size) {
	if (!out || !size) return;
	out[0] = '\0';
	if (!path) return;
	const char* slash = strrchr(path, '/');
	const char* name = slash ? slash + 1 : path;
	int dir_len = slash ? (int)(slash - path) : 1;
	const char* dir = slash ? path : ".";
	const char* dot = strrchr(name, '.');
	int name_len = dot ? (int)(dot - name) : (int)strlen(name);
	snprintf(out, size, "%.*s/.media/%.*s.info", dir_len, dir, name_len, name);
}

static int infoKeyChar(char c) {
	return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.';
}

static void trimSpan(const char** start, const char** end) {
	while (*start < *end && isspace((unsigned char)**start)) (*start)++;
	while (*end > *start && isspace((unsigned char)(*end)[-1])) (*end)--;
}

int MB_infoValue(const char* text, size_t len, const char* key, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	if (!text || !key || !out || !size || len > MB_INFO_FILE_MAX || memchr(text, '\0', len)) return 0;
	size_t want = strlen(key);
	const char* found = NULL;
	const char* found_end = NULL;
	const char* p = text;
	const char* end = text + len;
	while (p < end) {
		const char* eol = memchr(p, '\n', (size_t)(end - p));
		if (!eol) eol = end;
		const char* line = p;
		const char* line_end = eol;
		p = eol < end ? eol + 1 : end;
		trimSpan(&line, &line_end);
		if (line == line_end || *line == '#') continue;
		const char* eq = memchr(line, '=', (size_t)(line_end - line));
		if (!eq) continue;
		const char* k = line;
		const char* k_end = eq;
		trimSpan(&k, &k_end);
		size_t k_len = (size_t)(k_end - k);
		if (!k_len || k_len > 31) continue;
		int ok = 1;
		for (const char* c = k; c < k_end && ok; c++) ok = infoKeyChar(*c);
		if (!ok || k_len != want || strncasecmp(k, key, want) != 0) continue;
		found = eq + 1;
		found_end = line_end;
		trimSpan(&found, &found_end);
	}
	if (!found) return 0;
	size_t n = (size_t)(found_end - found);
	if (n >= size) {
		// cut between UTF-8 characters
		n = size - 1;
		while (n && ((unsigned char)found[n] & 0xC0) == 0x80) n--;
	}
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)found[i];
		out[i] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
	}
	out[n] = '\0';
	return 1;
}

static int noWidth(const char* text, void* data) {
	(void)text;
	(void)data;
	return 0;
}

int MB_reply(const int* fields, int count, const char* const* raw, const char* sep, int max_width, MBMeasure measure,
		void* data, char* out, size_t size) {
	if (!out || !size) return -1;
	out[0] = '\0';
	if (!fields || !raw || !sep || count < 0 || count > MB_FIELD_COUNT || (max_width >= 0 && !measure)) return -1;
	char values[MB_FIELD_COUNT][MB_VALUE_SIZE];
	const char* joined[MB_FIELD_COUNT];
	int present = 0;
	for (int i = 0; i < count; i++) {
		present += MB_clean(fields[i], raw[i], values[i], sizeof(values[i]));
		joined[i] = values[i];
	}
	size_t used = 0;
	int n = snprintf(out, size, "count=%i", present);
	for (int i = 0; i < count && n >= 0 && (size_t)n < size - used; i++) {
		if (!values[i][0]) continue;
		used += (size_t)n;
		n = snprintf(out + used, size - used, "\n%s=%s", MB_key(fields[i]), values[i]);
	}
	if (n < 0 || (size_t)n >= size - used) {
		out[0] = '\0';
		return -1;
	}
	used += (size_t)n;
	char line[MB_FIELD_COUNT * (MB_VALUE_SIZE + 8)];
	if (max_width >= 0) MB_join(joined, count, sep, max_width, measure, data, line, sizeof(line));
	else MB_join(joined, count, sep, INT_MAX, noWidth, NULL, line, sizeof(line));
	n = snprintf(out + used, size - used, "\nline=%s", line);
	if (n < 0 || (size_t)n >= size - used) {
		out[0] = '\0';
		return -1;
	}
	return 0;
}
