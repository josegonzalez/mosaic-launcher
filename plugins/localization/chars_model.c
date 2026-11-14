#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chars_model.h"

#define CODE_POINTS 0x110000

// The UTF-8 character at s: its length, and its code point in *cp, or 0
// for a malformed or cut off one (one byte).
static size_t decode(const unsigned char* s, uint32_t* cp) {
	size_t len;
	uint32_t c, least;
	if (s[0] < 0x80) {
		*cp = s[0];
		return 1;
	}
	if ((s[0] & 0xE0) == 0xC0) { len = 2; c = s[0] & 0x1F; least = 0x80; }
	else if ((s[0] & 0xF0) == 0xE0) { len = 3; c = s[0] & 0x0F; least = 0x800; }
	else if ((s[0] & 0xF8) == 0xF0) { len = 4; c = s[0] & 0x07; least = 0x10000; }
	else {
		*cp = 0;
		return 1;
	}
	for (size_t i = 1; i < len; i++) {
		if ((s[i] & 0xC0) != 0x80) {
			*cp = 0;
			return 1;
		}
		c = (c << 6) | (s[i] & 0x3F);
	}
	if (c < least || (c >= 0xD800 && c <= 0xDFFF) || c >= CODE_POINTS) {
		*cp = 0;
		return 1;
	}
	*cp = c;
	return len;
}

int Cm_add(CmChars* chars, const char* text) {
	if (!text) return 0;
	if (!chars->seen && !(chars->seen = calloc(CODE_POINTS / 8, 1))) return -1;
	const unsigned char* s = (const unsigned char*)text;
	while (*s) {
		uint32_t cp;
		size_t n = decode(s, &cp);
		const unsigned char* at = s;
		s += n;
		if (cp < 0x20 || cp == 0x7F || (chars->seen[cp >> 3] & (1u << (cp & 7)))) continue;
		if (chars->len + n + 1 > chars->cap) {
			size_t cap = chars->cap ? chars->cap * 2 : 256;
			char* grown = realloc(chars->text, cap);
			if (!grown) return -1;
			chars->text = grown;
			chars->cap = cap;
		}
		chars->seen[cp >> 3] |= (unsigned char)(1u << (cp & 7));
		memcpy(chars->text + chars->len, at, n);
		chars->len += n;
		chars->text[chars->len] = '\0';
	}
	return 0;
}

int Cm_addCatalog(CmChars* chars, const PoCatalog* catalog) {
	if (!catalog) return 0;
	for (int i = 0; i < catalog->count; i++) {
		const PoEntry* e = &catalog->entries[i];
		for (int k = 0; k < e->strs && k < PO_MAX_PLURALS; k++) {
			if (Cm_add(chars, e->str[k]) != 0) return -1;
		}
	}
	return 0;
}

void Cm_free(CmChars* chars) {
	free(chars->text);
	free(chars->seen);
	memset(chars, 0, sizeof(*chars));
}

void Cm_key(const char* code, int count, const char* missing, char* out, size_t size) {
	// FNV-1a
	uint32_t hash = 2166136261u;
	for (const unsigned char* s = (const unsigned char*)(missing ? missing : ""); *s; s++) {
		hash ^= *s;
		hash *= 16777619u;
	}
	snprintf(out, size, "%s:%d:%08x", code, count, (unsigned)hash);
}

int Cm_warnDue(int count, const char* key, const char* saved) {
	return count > 0 && strcmp(key, saved ? saved : "") != 0;
}
