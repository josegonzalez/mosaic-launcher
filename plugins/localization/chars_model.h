#ifndef LOCALIZATION_CHARS_MODEL_H
#define LOCALIZATION_CHARS_MODEL_H

#include <stddef.h>
#include "po_model.h"

// The SDL-free half of localization's font check: the characters a
// language's translations use, each once, which the launcher's
// font_missing checks the UI font for, and when to warn about the ones it
// lacks.

// The characters, as UTF-8, each once, in the order they first come.
// Control characters and malformed bytes are left out. Start from {0}.
typedef struct CmChars {
	char* text;          // NUL-terminated, or NULL while empty
	size_t len, cap;
	unsigned char* seen; // a bit for each code point
} CmChars;

// Adds the characters of text that aren't there yet. Returns 0, or -1
// when there's no memory.
int Cm_add(CmChars* chars, const char* text);

// Adds every translation in catalog (each msgstr and plural form); a NULL
// catalog adds nothing.
int Cm_addCatalog(CmChars* chars, const PoCatalog* catalog);

void Cm_free(CmChars* chars);

// What a warning is about: the language's code, how many characters the
// font lacks and which (the first of them, from font_missing), in out.
// A warning with the same key was already shown.
void Cm_key(const char* code, int count, const char* missing, char* out, size_t size);

// 1 when a warning is due: the font lacks count (above 0) characters and
// key isn't the one saved for the last warning.
int Cm_warnDue(int count, const char* key, const char* saved);

#endif
