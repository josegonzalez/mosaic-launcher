#ifndef LAUNCHER_SCALEMODEL_H
#define LAUNCHER_SCALEMODEL_H

// The SDL-free half of smooth.c: scaling a piece of stock's sprite sheet to
// a theme's pill or button size with a filter instead of nearest-neighbour.

#include <stdint.h>

// Scales src (sw x sh ARGB8888 pixels, alpha in the top byte, src_pitch
// pixels a row) into dst (dw x dh, dst_pitch pixels a row). Each axis is
// scaled on its own: averaged over the source pixels it covers when it
// shrinks, bilinear between pixel centres when it grows, unchanged when it
// keeps its size. Colours are weighted by their alpha while they're mixed,
// so a transparent pixel's colour never bleeds into an edge. At the same
// size it's an exact copy, and an image that's the same mirrored stays so.
// Writes only dw x dh pixels of dst. Returns 0, or -1 for a size under 1
// or when it can't allocate its working rows (dst is left alone).
int ScaleModel_smooth(const uint32_t* src, int sw, int sh, int src_pitch,
	uint32_t* dst, int dw, int dh, int dst_pitch);

#endif
