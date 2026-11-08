// Art backdrop, without the host: its settings, which art it looks for,
// and where the art and its gradient go on the screen. No SDL, so it's
// tested on the host.
//
// The art covers the top of the screen, as tall as the height setting
// says, scaled to fill it and cropped at the edges. Two styles:
//
//   full      the art at full strength over the whole area
//   gradient  the same over the top half of the area, then fading out to
//             stock's background over its bottom half, in bands each
//             drawn at one alpha
//
// Either way it's dimmed by the dim setting, towards black.

#ifndef BACKDROP_MODEL_H
#define BACKDROP_MODEL_H

#include "mosaic_plugin.h"

enum BDStyle {
	BD_STYLE_GRADIENT,
	BD_STYLE_FULL,
};

// The style a setting's value names, or gradient for anything else.
int BD_style(const char* value);

// The dim setting, a percentage from 0 to 100, as the alpha of black to
// draw over the art, from 0 to 255. Anything else is BD_DIM_DEFAULT.
#define BD_DIM_DEFAULT 50
int BD_dim(const char* value);

// How much of the screen's height the height setting gives the art: full,
// half or third, as a divisor (1, 2 or 3), or 1 for anything else.
int BD_height(const char* value);

// The area the art covers on a w by h screen with that divisor: the top of
// the screen, w wide.
MosaicRect BD_area(int screen_w, int screen_h, int divisor);

// The kinds of art to look for (enum MosaicArtKind), in order: the
// backdrop, then the screenshot, the thumbnail and the box, each only when
// its fallback is on. Writes them to kinds and returns how many.
#define BD_MAX_KINDS 4
int BD_kinds(int screenshot, int thumbnail, int box, int kinds[BD_MAX_KINDS]);

// The part of a src_w by src_h image that covers a dst_w by dst_h area
// keeping its shape: all of it one way, centred and cropped the other.
// Returns 0, or -1 for an empty size.
int BD_cover(int src_w, int src_h, int dst_w, int dst_h, MosaicRect* crop);

// The gradient's bands on an area h tall: the top half is one band at
// full alpha, and the bottom half is split into up to BD_MAX_BANDS bands,
// each at least BD_MIN_BAND tall, fading from nearly full to nearly
// nothing. BD_bands returns how many bands there are in all, and BD_band
// writes band i's top, height and alpha (0 to 255) and returns 0, or -1
// past the end.
#define BD_MAX_BANDS 64
#define BD_MIN_BAND 2
int BD_bands(int h);
int BD_band(int h, int i, int* y, int* band_h, int* alpha);

// The part of crop that's drawn into rows y to y + band_h of an area h
// tall, when crop is stretched over the whole area.
MosaicRect BD_srcBand(const MosaicRect* crop, int h, int y, int band_h);

// alpha scaled by another, both 0 to 255, rounded.
int BD_mulAlpha(int a, int b);

#endif
