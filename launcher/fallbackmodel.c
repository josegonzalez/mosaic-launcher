#include "fallbackmodel.h"

size_t FallbackModel_decode(const char* s, size_t n, uint32_t* cp) {
	const unsigned char* u = (const unsigned char*)s;
	size_t len;
	uint32_t c;
	uint32_t least;
	if (u[0] < 0x80) {
		*cp = u[0];
		return 1;
	}
	if ((u[0] & 0xE0) == 0xC0) { len = 2; c = u[0] & 0x1F; least = 0x80; }
	else if ((u[0] & 0xF0) == 0xE0) { len = 3; c = u[0] & 0x0F; least = 0x800; }
	else if ((u[0] & 0xF8) == 0xF0) { len = 4; c = u[0] & 0x07; least = 0x10000; }
	else {
		*cp = FALLBACK_BAD;
		return 1;
	}
	if (len > n) {
		*cp = FALLBACK_BAD;
		return 1;
	}
	for (size_t i = 1; i < len; i++) {
		if ((u[i] & 0xC0) != 0x80) {
			*cp = FALLBACK_BAD;
			return 1;
		}
		c = (c << 6) | (u[i] & 0x3F);
	}
	// overlong forms, surrogates and past Unicode's end
	if (c < least || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) {
		*cp = FALLBACK_BAD;
		return 1;
	}
	*cp = c;
	return len;
}

int FallbackModel_joins(uint32_t cp) {
	return (cp >= 0x0300 && cp <= 0x036F)      // combining diacritical marks
		|| (cp >= 0x1AB0 && cp <= 0x1AFF)  // and their extended block
		|| (cp >= 0x1DC0 && cp <= 0x1DFF)  // and supplement
		|| (cp >= 0x20D0 && cp <= 0x20FF)  // for symbols
		|| (cp >= 0xFE20 && cp <= 0xFE2F)  // half marks
		|| (cp >= 0x3099 && cp <= 0x309A)  // kana voiced sound marks
		|| (cp >= 0xFE00 && cp <= 0xFE0F)  // variation selectors
		|| (cp >= 0xE0100 && cp <= 0xE01EF) // and their supplement
		|| cp == 0x200C || cp == 0x200D;   // zero width non-joiner and joiner
}

// The font for the character at text[at], given the one before it (-1
// for none).
static int fontAt(const char* text, size_t len, size_t at, int before, FallbackPick pick, void* data, size_t* size) {
	uint32_t cp;
	*size = FallbackModel_decode(text + at, len - at, &cp);
	if (cp == FALLBACK_BAD) return FALLBACK_UI;
	if (before >= 0 && FallbackModel_joins(cp)) return before;
	return pick(cp, data);
}

size_t FallbackModel_run(const char* text, size_t len, size_t start, FallbackPick pick, void* data, int* font) {
	*font = FALLBACK_UI;
	if (start >= len) return 0;
	size_t size;
	int first = fontAt(text, len, start, -1, pick, data, &size);
	size_t at = start + size;
	while (at < len) {
		if (fontAt(text, len, at, first, pick, data, &size) != first) break;
		at += size;
	}
	*font = first;
	return at - start;
}

int FallbackModel_width(const char* text, size_t len, FallbackPick pick, FallbackRunWidth width, void* data) {
	int total = 0;
	size_t at = 0;
	while (at < len) {
		int font;
		size_t n = FallbackModel_run(text, len, at, pick, data, &font);
		int w = width(font, text + at, n, data);
		if (w < 0) return -1;
		total += w;
		at += n;
	}
	return total;
}

size_t FallbackModel_line(const char* text, size_t len, int max_width, FallbackMeasure measure, void* data, size_t* next) {
	size_t space = 0; // where the line breaks at the last space, or 0 for none
	size_t at = 0;
	while (at < len) {
		if (text[at] == '\n') {
			*next = at + 1;
			return at;
		}
		if (text[at] == ' ') {
			// a run of spaces breaks at its first
			if (at > 0 && text[at - 1] != ' ') space = at;
			at++;
			continue;
		}
		uint32_t cp;
		size_t size = FallbackModel_decode(text + at, len - at, &cp);
		if (max_width > 0) {
			int w = measure(text, at + size, data);
			if (w > max_width) {
				if (space) {
					size_t after = space;
					while (after < len && text[after] == ' ') after++;
					*next = after;
					return space;
				}
				if (at > 0) {
					*next = at;
					return at;
				}
				*next = size;
				return size;
			}
		}
		at += size;
	}
	*next = len;
	return len;
}
