#include "band_model.h"

#include <ctype.h>
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
