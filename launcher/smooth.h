#ifndef LAUNCHER_SMOOTH_H
#define LAUNCHER_SMOOTH_H

// Pieces of stock's sprite sheets (pill ends and middles, button circles
// and prompts) smooth-scaled to a theme's pill or button size, for the
// launcher's api.c (see patches/api.c.patch). Each piece is scaled once
// with scalemodel.c and kept, so drawing it again is a lookup; the theme
// changing or the sheets closing drops them all. Stock's sizes never get
// here: they're drawn from the sheet as stock does.

#include "sdl.h"

// rect of sheet scaled to w x h, as a surface that blends, or NULL when it
// can't be made (the caller draws it as stock does). The cache owns it.
SDL_Surface* Smooth_piece(SDL_Surface* sheet, const SDL_Rect* rect, int w, int h);

// Frees every piece. GFX_themeChanged and GFX_quit call it.
void Smooth_flush(void);

#endif
