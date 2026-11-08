#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "infomodel.h"

static int isSpace(char c) {
	return c == ' ' || c == '\t';
}

static void trim(const char** start, const char** end) {
	while (*start < *end && isSpace(**start)) (*start)++;
	while (*end > *start && isSpace((*end)[-1])) (*end)--;
}

static int keyChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		|| c == '_' || c == '-' || c == '.';
}

// How many of n bytes of text fit in max bytes without cutting a UTF-8
// character.
static size_t fit(const char* text, size_t n, size_t max) {
	if (n <= max) return n;
	size_t cut = max;
	while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80) cut--;
	return cut;
}

typedef struct Span {
	const char *key, *key_end, *value, *value_end;
} Span;

InfoFile* Info_parse(const char* data, size_t len, const char** why) {
	const char* ignored;
	if (!why) why = &ignored;
	*why = NULL;
	if (!data) {
		*why = "it can't be read";
		return NULL;
	}
	if (len > INFO_FILE_MAX) {
		*why = "it's over 4096 bytes";
		return NULL;
	}
	if (memchr(data, '\0', len)) {
		*why = "it isn't text";
		return NULL;
	}

	Span spans[INFO_KEYS_MAX];
	int count = 0;
	const char* p = data;
	const char* end = data + len;
	while (p < end) {
		const char* eol = memchr(p, '\n', (size_t)(end - p));
		if (!eol) eol = end;
		const char* line = p;
		const char* line_end = eol;
		p = eol < end ? eol + 1 : end;
		if (line_end > line && line_end[-1] == '\r') line_end--;
		trim(&line, &line_end);
		if (line == line_end || *line == '#') continue;
		const char* eq = memchr(line, '=', (size_t)(line_end - line));
		if (!eq) continue;
		Span s = { line, eq, eq + 1, line_end };
		trim(&s.key, &s.key_end);
		trim(&s.value, &s.value_end);
		size_t key_len = (size_t)(s.key_end - s.key);
		if (key_len == 0 || key_len > INFO_KEY_MAX) continue;
		int ok = 1;
		for (const char* c = s.key; c < s.key_end && ok; c++) ok = keyChar(*c);
		if (!ok) continue;
		s.value_end = s.value + fit(s.value, (size_t)(s.value_end - s.value), INFO_VALUE_MAX);

		int i = 0;
		while (i < count && !((size_t)(spans[i].key_end - spans[i].key) == key_len
				&& !strncasecmp(spans[i].key, s.key, key_len))) i++;
		if (i < count) spans[i] = s;
		else if (count < INFO_KEYS_MAX) spans[count++] = s;
	}
	if (count == 0) {
		*why = "it has no keys";
		return NULL;
	}

	size_t bytes = sizeof(InfoFile);
	for (int i = 0; i < count; i++) {
		bytes += (size_t)(spans[i].key_end - spans[i].key) + 1 + (size_t)(spans[i].value_end - spans[i].value) + 1;
	}
	InfoFile* info = calloc(1, bytes);
	if (!info) {
		*why = "out of memory";
		return NULL;
	}
	char* out = (char*)(info + 1);
	for (int i = 0; i < count; i++) {
		info->keys[i] = out;
		for (const char* c = spans[i].key; c < spans[i].key_end; c++) {
			*out++ = (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a') : *c;
		}
		*out++ = '\0';
		info->values[i] = out;
		for (const char* c = spans[i].value; c < spans[i].value_end; c++) {
			unsigned char b = (unsigned char)*c;
			*out++ = (b < 0x20 || b == 0x7F) ? ' ' : *c;
		}
		*out++ = '\0';
	}
	info->count = count;
	return info;
}

const char* Info_get(const InfoFile* info, const char* key) {
	if (!info || !key) return NULL;
	for (int i = 0; i < info->count; i++) {
		if (!strcasecmp(info->keys[i], key)) return info->values[i];
	}
	return NULL;
}

void Info_path(const char* rom_path, char* out, size_t size) {
	if (!size) return;
	out[0] = '\0';
	if (!rom_path) return;
	const char* slash = strrchr(rom_path, '/');
	const char* name = slash ? slash + 1 : rom_path;
	int dir_len = slash ? (int)(slash - rom_path) : 1;
	const char* dir = slash ? rom_path : ".";
	const char* dot = strrchr(name, '.');
	int name_len = dot ? (int)(dot - name) : (int)strlen(name);
	snprintf(out, size, "%.*s/.media/%.*s.info", dir_len, dir, name_len, name);
}

int Info_copy(const char* value, char* out, size_t size) {
	if (!size) return value != NULL;
	if (!value) {
		out[0] = '\0';
		return 0;
	}
	size_t n = fit(value, strlen(value), size - 1);
	memcpy(out, value, n);
	out[n] = '\0';
	return 1;
}
