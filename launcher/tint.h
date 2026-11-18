#ifndef TINT_H
#define TINT_H

// Plugins' screen tint (screen_tint in MosaicPlugin), multiplied into
// every frame as it's shown (see tintpresent.h). Main thread only.

#include "api.h"
#include "mosaic_plugin.h"

// The tint for the frames shown from now on: c when on is 1, none
// otherwise. The main loop sets it from plugins' screen_tint each frame it
// draws.
void Tint_set(int on, MosaicColor c);

// The tint on the frame on screen, into out when it isn't NULL (white for
// none). Returns 1 when it has one.
int Tint_shown(MosaicColor* out);

// The frame on screen without its tint, read back from the renderer as
// GFX_captureRendererToSurface does, for a transition to start from: the
// tint goes on again as each of its frames is shown.
SDL_Surface* Tint_captureShown(void);

// A captured frame (ARGB8888) made opaque, as a screenshot is, and with
// the tint on screen, so it's as seen.
void Tint_capture(SDL_Surface* frame);

#endif
