#ifndef LAUNCHER_PALETTES_H
#define LAUNCHER_PALETTES_H

// Stock's colour palettes for plugins, read only: listed by stock's own
// PALETTE_enumerate (palette.c), as its Settings app lists them, and the
// one the user picked there. Nothing here applies a palette or writes
// stock's settings. SDL-free, so tests/c/test_palettes.c runs it against
// stock's palette.c and config.c on a fixture card.

#include <stdint.h>
#include "mosaic_plugin.h"

// As many as Settings' Color Palette menu lists.
#define PALETTES_MAX 64

// host->palettes_count: reads the palettes again and returns how many.
int Palettes_count(void);

// host->palette_info.
int Palettes_info(int index, MosaicPalette* out);

// host->palette_selected: the index of the user's palette, or -1 for Custom.
int Palettes_selected(void);

// The first of count names equal to current, as Settings finds the
// palette it shows, or -1 for "" or none.
int Palettes_find(const char* const* names, int count, const char* current);

// 0xRRGGBB from stock's packed 0xRRGGBBAA.
int Palettes_rgb(uint32_t rgba);

#endif
