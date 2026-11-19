#ifndef LAUNCHER_TEXT_H
#define LAUNCHER_TEXT_H

#include <stddef.h>
#include "api.h"

// Stock's text helpers, made safe for translated text.

// GFX_truncateText that never cuts a UTF-8 character and keeps to size
// (see Draw_truncateChars). ASCII is truncated exactly as stock's is.
int Text_truncate(TTF_Font* font, const char* text, char* out, size_t size, int max_width, int padding);

// text with a newline wherever it wraps in max_width, into out (size
// bytes), so stock's GFX_blitMessage centres every line in its rect
// rather than drawing a wrapped one from the rect's left edge.
void Text_wrap(TTF_Font* font, const char* text, char* out, size_t size, int max_width);

// GFX_blitHardwareHints with its labels through TR().
void Text_hardwareHints(SDL_Surface* dst, int show_setting);

#endif
