#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trmodel.h"

///////////////////////////////////////
// printf conversions

static int isFlag(char c) {
	return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'';
}
static int isDigit(char c) {
	return c >= '0' && c <= '9';
}

// Reads the conversion after a '%' at *p, adding what it takes to out.
// Returns 0, or -1 for one that isn't allowed.
static int conversion(const char** p, char* out, size_t size, size_t* n) {
	const char* s = *p;
#define TAKE(c) do { if (*n + 1 >= size) return -1; out[(*n)++] = (c); } while (0)
	// a position (%1$s) can reorder arguments, which the host can't check
	const char* d = s;
	while (isDigit(*d)) d++;
	if (d != s && *d == '$') return -1;

	while (isFlag(*s)) s++;
	if (*s == '*') {
		TAKE('*');
		s++;
	}
	else while (isDigit(*s)) s++;
	if (*s == '.') {
		s++;
		if (*s == '*') {
			TAKE('*');
			s++;
		}
		else while (isDigit(*s)) s++;
	}

	char size_class = 'i'; // int
	if (s[0] == 'h' && s[1] == 'h') { size_class = 'H'; s += 2; }
	else if (s[0] == 'h') { size_class = 'h'; s++; }
	else if (s[0] == 'l' && s[1] == 'l') { size_class = 'L'; s += 2; }
	else if (s[0] == 'l') { size_class = 'l'; s++; }
	else if (s[0] == 'q') { size_class = 'L'; s++; }
	else if (s[0] == 'j') { size_class = 'j'; s++; }
	else if (s[0] == 'z') { size_class = 'z'; s++; }
	else if (s[0] == 't') { size_class = 't'; s++; }
	else if (s[0] == 'L') { size_class = 'D'; s++; } // long double

	char c = *s;
	if (!c) return -1;
	s++;
	if (strchr("diouxXc", c)) {
		if (size_class == 'D') return -1;
		TAKE(size_class);
	}
	else if (strchr("eEfFgGaA", c)) {
		if (size_class == 'D') TAKE('F');
		else if (size_class == 'i' || size_class == 'l') TAKE('f');
		else return -1;
	}
	else if (c == 's' || c == 'p') {
		if (size_class != 'i') return -1;
		TAKE(c);
	}
	else return -1; // %n, or no conversion at all
#undef TAKE
	*p = s;
	return 0;
}

int Tr_conversions(const char* text, char* out, size_t size) {
	if (!text || !out || size == 0) return -1;
	size_t n = 0;
	for (const char* p = text; *p; ) {
		if (*p++ != '%') continue;
		if (*p == '%') {
			p++;
			continue;
		}
		if (conversion(&p, out, size, &n) < 0) {
			out[0] = '\0';
			return -1;
		}
	}
	out[n] = '\0';
	return (int)n;
}

// 1 when text has a conversion, or something that looks like one, other
// than "%%" anywhere.
static int hasConversion(const char* text) {
	for (const char* p = text; *p; ) {
		if (*p++ != '%') continue;
		if (*p == '%') {
			p++;
			continue;
		}
		char scratch[64];
		size_t n = 0;
		const char* q = p;
		if (conversion(&q, scratch, sizeof(scratch), &n) == 0) return 1;
		// a positional or %n one is refused as well
		const char* d = p;
		while (isDigit(*d)) d++;
		if (d != p && *d == '$') return 1;
		while (isFlag(*d) || isDigit(*d) || *d == '.' || *d == '*' || strchr("hlqjztL", *d)) {
			if (!*d) break;
			d++;
		}
		if (*d == 'n') return 1;
	}
	return 0;
}

///////////////////////////////////////
// checks

int Tr_utf8(const char* text) {
	const unsigned char* s = (const unsigned char*)text;
	while (*s) {
		unsigned c = *s;
		int more;
		unsigned cp;
		if (c < 0x80) {
			s++;
			continue;
		}
		else if (c >= 0xC2 && c <= 0xDF) { more = 1; cp = c & 0x1F; }
		else if (c >= 0xE0 && c <= 0xEF) { more = 2; cp = c & 0x0F; }
		else if (c >= 0xF0 && c <= 0xF4) { more = 3; cp = c & 0x07; }
		else return 0;
		s++;
		for (int i = 0; i < more; i++, s++) {
			if ((*s & 0xC0) != 0x80) return 0;
			cp = (cp << 6) | (*s & 0x3F);
		}
		if (more == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
		if (more == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
	}
	return 1;
}

int Tr_check(const char* english, const char* english2, const char* translation) {
	if (!translation) return TR_NONE;
	if (!Tr_utf8(translation)) return TR_UTF8;
	if (!translation[0]) return TR_EMPTY;
	size_t len = strlen(translation);
	if (len > TR_MAX_BYTES) return TR_LONG;
	int lines = 1;
	size_t line = 0;
	for (const char* p = translation; *p; p++) {
		if (*p == '\n') {
			lines++;
			line = 0;
			continue;
		}
		if (++line > TR_MAX_LINE) return TR_LINE;
	}
	if (lines > TR_MAX_LINES) return TR_LINES;

	char want[64], want2[64], got[64];
	int wants = english ? Tr_conversions(english, want, sizeof(want)) : 0;
	int wants2 = english2 ? Tr_conversions(english2, want2, sizeof(want2)) : -1;
	// English with none, or none the host can check, is plain text
	if (wants <= 0 && wants2 <= 0) return hasConversion(translation) ? TR_FORMAT : TR_OK;
	if (Tr_conversions(translation, got, sizeof(got)) < 0) return TR_FORMAT;
	if (wants >= 0 && !strcmp(got, want)) return TR_OK;
	if (wants2 >= 0 && !strcmp(got, want2)) return TR_OK;
	return TR_FORMAT;
}

const char* Tr_reason(int check) {
	switch (check) {
		case TR_OK: return "it's fine";
		case TR_NONE: return "there's none";
		case TR_EMPTY: return "it's empty";
		case TR_LONG: return "it's over 1023 bytes";
		case TR_LINE: return "a line is over 255 bytes";
		case TR_LINES: return "it has more than 8 lines";
		case TR_FORMAT: return "its printf conversions differ from the English";
		case TR_UTF8: return "it isn't valid UTF-8";
		default: return "?";
	}
}

const char* Tr_english(const char* singular, const char* plural, unsigned long n) {
	return n == 1 ? singular : plural;
}

int Tr_key(const char* plugin_id, const char* context, const char* text, const char* plural, unsigned long n, char* out,
		size_t size) {
	if (!text || !out || size == 0) return -1;
	int len;
	// \x01 stands for no plugin (the launcher's own) and for no context
	const char* who = plugin_id ? plugin_id : "\x01";
	const char* what = context ? context : "\x01";
	if (plural) len = snprintf(out, size, "%s\x02%s\x04%s\x1f%s\x1f%lu", who, what, text, plural, n);
	else len = snprintf(out, size, "%s\x02%s\x04%s", who, what, text);
	return len < 0 || (size_t)len >= size ? -1 : 0;
}

///////////////////////////////////////
// the cache

static unsigned long hashKey(const char* key) {
	unsigned long h = 2166136261u;
	for (const unsigned char* p = (const unsigned char*)key; *p; p++) h = (h ^ *p) * 16777619u;
	return h;
}

static int slotFor(const TrCache* cache, const char* key) {
	unsigned long i = hashKey(key) % TR_CACHE_SLOTS;
	while (cache->entries[i].key && strcmp(cache->entries[i].key, key)) i = (i + 1) % TR_CACHE_SLOTS;
	return (int)i;
}

const TrCacheEntry* TrCache_find(const TrCache* cache, const char* key) {
	if (!cache || !key) return NULL;
	const TrCacheEntry* e = &cache->entries[slotFor(cache, key)];
	return e->key ? e : NULL;
}

const TrCacheEntry* TrCache_put(TrCache* cache, const char* key, const char* value) {
	if (!cache || !key) return NULL;
	TrCacheEntry* e = &cache->entries[slotFor(cache, key)];
	if (e->key) return e; // asked already; the first answer stays, as pointers to it may be out
	if (cache->count >= TR_CACHE_MAX) return NULL;
	char* k = strdup(key);
	char* v = value ? strdup(value) : NULL;
	if (!k || (value && !v)) {
		free(k);
		free(v);
		return NULL;
	}
	e->key = k;
	e->value = v;
	cache->count++;
	return e;
}

void TrCache_clear(TrCache* cache) {
	if (!cache) return;
	for (int i = 0; i < TR_CACHE_SLOTS; i++) {
		free(cache->entries[i].key);
		free(cache->entries[i].value);
		cache->entries[i].key = NULL;
		cache->entries[i].value = NULL;
	}
	cache->count = 0;
}
