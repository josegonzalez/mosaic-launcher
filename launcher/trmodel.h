#ifndef LAUNCHER_TRMODEL_H
#define LAUNCHER_TRMODEL_H

#include <stddef.h>

// The SDL-free half of the strings slot: whether a translation is safe to
// draw and to pass to printf in place of its English, and the cache of
// answers, so the owner is asked once for each string.

#define TR_MAX_BYTES 1023 // a translation's longest, without its NUL
#define TR_MAX_LINE 255   // a line's longest, as stock's GFX_blitMessage copies each into 256 bytes
#define TR_MAX_LINES 8
#define TR_CACHE_MAX 4096 // strings kept; past it, the rest show in English

enum TrCheck {
	TR_OK,
	TR_NONE,   // NULL: no translation
	TR_EMPTY,  // "", which stock's text drawing can't render
	TR_LONG,   // over TR_MAX_BYTES
	TR_LINE,   // a line over TR_MAX_LINE
	TR_LINES,  // more than TR_MAX_LINES lines
	TR_FORMAT, // printf conversions that differ from the English
	TR_UTF8,   // not valid UTF-8, an error that turns the slot off
};

// Writes the printf conversions in text to out (size bytes), one
// character for each argument it takes ('i', 'l', 'L', 'z', 'j', 't',
// 'h', 'H' for the integer sizes, 'f' and 'F' for double and long double,
// 's' and 'p', '*' for a width or precision argument), and returns how
// many, or -1 when a '%' doesn't start a conversion the host allows (none
// with a position such as %1$s, no %n), or out is too small. "%%" takes
// nothing.
int Tr_conversions(const char* text, char* out, size_t size);

// 1 when text is valid UTF-8 (no overlong forms, surrogates or code
// points past U+10FFFF).
int Tr_utf8(const char* text);

// Whether translation can stand in for english, or for either of english
// and english2 (a plural's two forms; NULL for none). An English text
// with no conversions is plain text, so a '%' in its translation is fine
// unless it starts a conversion. Returns an enum TrCheck.
int Tr_check(const char* english, const char* english2, const char* translation);

// What a check result means, for logs.
const char* Tr_reason(int check);

// The English form for n: singular when n is 1, as English's plural rule
// picks.
const char* Tr_english(const char* singular, const char* plural, unsigned long n);

// The cache key for a string (plural NULL) or a plural's forms and n, in
// context (NULL for the launcher's own), into out. Returns 0, or -1 when it
// doesn't fit.
int Tr_key(const char* context, const char* text, const char* plural, unsigned long n, char* out, size_t size);

typedef struct TrCacheEntry {
	char* key;
	char* value; // the translation, or NULL to show the English
} TrCacheEntry;

#define TR_CACHE_SLOTS (TR_CACHE_MAX * 2)

typedef struct TrCache {
	TrCacheEntry entries[TR_CACHE_SLOTS];
	int count;
} TrCache;

// The entry for key, or NULL when it hasn't been asked.
const TrCacheEntry* TrCache_find(const TrCache* cache, const char* key);

// Keeps a copy of value (NULL for English) for key. Returns the entry, or
// NULL when the cache is full or out of memory.
const TrCacheEntry* TrCache_put(TrCache* cache, const char* key, const char* value);

// Forgets everything, freeing the copies.
void TrCache_clear(TrCache* cache);

#endif
