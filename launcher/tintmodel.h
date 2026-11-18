#ifndef TINTMODEL_H
#define TINTMODEL_H

// The SDL-free half of plugins' screen tint (screen_tint in MosaicPlugin):
// putting every plugin's tint together, and multiplying a captured frame's
// pixels by it as the frame was shown. The launcher multiplies each
// pixel's red, green and blue by the tint's over 255, so white turns into
// the tint and black stays black.

#include <stdint.h>
#include "mosaic_plugin.h"

// The tint that does nothing.
#define TINT_NONE ((MosaicColor){ 255, 255, 255, 255 })

// Multiplies value by by over 255, rounded, as the GPU blends.
uint8_t Tint_scale(uint8_t value, uint8_t by);

// Multiplies *into by c, channel by channel, so two plugins' tints
// together make one. Alpha stays 255.
void Tint_mix(MosaicColor* into, MosaicColor c);

// 1 when c changes nothing (white).
int Tint_isNone(MosaicColor c);

// Each channel's 256 answers for a tint, so a frame takes one look up per
// channel per pixel.
typedef struct TintTable {
	uint8_t r[256], g[256], b[256];
} TintTable;

void Tint_table(MosaicColor c, TintTable* table);

// Makes count 0xAARRGGBB pixels opaque, as a screenshot is, and multiplies
// them by table's tint, or leaves their colour as it is when table is
// NULL.
void Tint_row(uint32_t* row, int count, const TintTable* table);

#endif
