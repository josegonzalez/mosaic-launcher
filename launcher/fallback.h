#ifndef LAUNCHER_FALLBACK_H
#define LAUNCHER_FALLBACK_H

// A second font for the characters the UI font has no glyphs for (CJK
// game names, or a translated UI). Text is measured and drawn run by run:
// each stretch goes to the UI font when it has the glyphs, otherwise to
// the fallback font at the same size, with their baselines lined up. The
// path comes from the theme slot's owner or the strings slot's (see
// plugins.c); with none, or for text the UI font has every glyph of,
// drawing is exactly stock's. fallbackttf.h sends SDL_ttf's calls here.

#include <stddef.h>
#include "sdl.h"

// SDL_ttf's TTF_SizeUTF8, TTF_RenderUTF8_Blended and
// TTF_RenderUTF8_Blended_Wrapped, run by run for stock's five fonts.
int Fallback_sizeUTF8(TTF_Font* font, const char* text, int* w, int* h);
SDL_Surface* Fallback_renderUTF8(TTF_Font* font, const char* text, SDL_Color fg);
SDL_Surface* Fallback_renderUTF8Wrapped(TTF_Font* font, const char* text, SDL_Color fg, Uint32 wrap);

// Checks a fallback font path an owner gave: a regular file on the card
// that opens as a font. Writes its real path to out and returns 0, or
// logs why it's refused (as owner's field) and returns -1.
int Fallback_check(const char* owner, const char* field, const char* given, char* out, size_t size);

// The fallback font to use, already checked, from owner (for the log), or
// "" for none. Opens it at stock's five sizes when they're open and it
// changed; a font that won't open is logged and left out.
void Fallback_set(const char* path, const char* owner);

// The fallback font in use, or "".
const char* Fallback_path(void);

// Called by the launcher's build of api.c (patches/api.c.patch) once stock's
// five fonts are open, and once they're closed.
void Fallback_fontsLoaded(void);
void Fallback_fontsClosed(void);

#endif
