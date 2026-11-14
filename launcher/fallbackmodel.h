#ifndef LAUNCHER_FALLBACKMODEL_H
#define LAUNCHER_FALLBACKMODEL_H

// The SDL-free half of the fallback font: splitting UTF-8 text into runs,
// each drawn by the UI font or the fallback font, and where wrapped text
// breaks. fallback.c measures and draws the runs.

#include <stddef.h>
#include <stdint.h>

enum {
	FALLBACK_UI,     // the UI font
	FALLBACK_SECOND, // the fallback font
};

// The UTF-8 character at s, which has n (at least 1) bytes left: writes
// its code point and returns its length. A malformed or cut off sequence
// is one byte, with FALLBACK_BAD as its code point.
#define FALLBACK_BAD 0xFFFFFFFFu
size_t FallbackModel_decode(const char* s, size_t n, uint32_t* cp);

// 1 for a code point that's drawn with the character before it, so it
// stays in that character's run: combining marks, variation selectors and
// zero width joiners.
int FallbackModel_joins(uint32_t cp);

// Which font draws cp: FALLBACK_UI or FALLBACK_SECOND. Never asked for
// FALLBACK_BAD, which the UI font draws.
typedef int (*FallbackPick)(uint32_t cp, void* data);

// The run of text (len bytes) that starts at start: returns its length in
// bytes, 0 at the end, and writes the font that draws it.
size_t FallbackModel_run(const char* text, size_t len, size_t start, FallbackPick pick, void* data, int* font);

// The width of len bytes of text in one font (an enum above), or -1 when
// it can't be measured.
typedef int (*FallbackRunWidth)(int font, const char* text, size_t len, void* data);

// The width of text (len bytes) run by run: the sum of each run's width
// in its font, or -1 when one can't be measured. pick and width share data.
int FallbackModel_width(const char* text, size_t len, FallbackPick pick, FallbackRunWidth width, void* data);

// The width of len bytes of text, or -1 when it can't be measured.
typedef int (*FallbackMeasure)(const char* text, size_t len, void* data);

// The first line of text (len bytes) in max_width: up to its first
// newline, broken after the last space that fits when it's wider, or
// between characters when no space fits, and always at least one
// character. A max_width of 0 or less breaks only at newlines. Returns
// the line's length, without the newline or the space broken at, and
// writes where the next line starts (len after the last line).
size_t FallbackModel_line(const char* text, size_t len, int max_width, FallbackMeasure measure, void* data, size_t* next);

// 1 when a font has a glyph for cp.
typedef int (*FallbackHas)(uint32_t cp, void* data);

// How many different characters of text (len bytes) has says the font
// has no glyph for, or -1 when there's no memory. Control characters,
// malformed bytes and what's drawn with the character before it (see
// FallbackModel_joins) aren't asked about. When out isn't NULL it gets
// the first of them that fit in size bytes, in the order they come, as
// UTF-8.
int FallbackModel_missing(const char* text, size_t len, FallbackHas has, void* data, char* out, size_t size);

#endif
