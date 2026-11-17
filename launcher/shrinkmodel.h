#ifndef LAUNCHER_SHRINKMODEL_H
#define LAUNCHER_SHRINKMODEL_H

// The SDL-free half of plugins' image_load_fit: an image's size from its
// PNG or JPEG header, read before anything is decoded so a small file
// can't decode to more than the launcher allows, the size it's shrunk to
// to fit a box, and shrinking it a row at a time by averaging the pixels
// each one covers, so only the result and a few rows are held beside the
// decoded image.

#include <stddef.h>
#include <stdint.h>

#define SHRINK_MAX_SIDE 8192                  // the widest or tallest image it decodes
#define SHRINK_MAX_PIXELS (40 * 1000 * 1000)  // the most pixels it decodes, 160 MB as ARGB

// Writes the width and height a PNG's IHDR or a baseline or progressive
// JPEG's first SOF gives, from the first size bytes of the file. Returns 0,
// or -1 for anything else, a header cut short or a side of 0.
int Shrink_headerSize(const unsigned char* data, size_t size, int* w, int* h);

// 1 when a w by h image is one it decodes: both sides 1 to SHRINK_MAX_SIDE
// and at most SHRINK_MAX_PIXELS in all.
int Shrink_allowed(int w, int h);

// The size a sw by sh image is shrunk to so it fits inside max_w by max_h,
// keeping its shape, rounded to the nearest pixel and at least 1 a side;
// an image that fits already keeps its size, as it's never enlarged.
// Returns 0, or -1 for a size under 1.
int Shrink_fit(int sw, int sh, int max_w, int max_h, int* dw, int* dh);

typedef struct Shrink Shrink;

// Shrinks sw by sh pixels to dw by dh, each no larger than its source
// side. Returns NULL for a size out of range or when it can't allocate.
Shrink* Shrink_new(int sw, int sh, int dw, int dh);

// Takes the next of the source's rows, sw ARGB8888 pixels (alpha in the
// top byte), and writes each row of the result it completes into dst, the
// whole dw by dh result, dst_pitch pixels a row. Each result pixel is the
// average of the source pixels it covers, weighted by how much of each,
// with colours weighted by their alpha so a transparent pixel's colour
// never bleeds in. Returns 0, or -1 past the last row.
int Shrink_row(Shrink* s, const uint32_t* row, uint32_t* dst, int dst_pitch);

// 1 once every row of the result is written.
int Shrink_done(const Shrink* s);

void Shrink_free(Shrink* s);

#endif
