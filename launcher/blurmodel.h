#ifndef BLURMODEL_H
#define BLURMODEL_H

// host->surface_blur without SDL: a box blur run three times over a small
// copy of the image, then enlarged again, so its memory is the small
// copy's, at most BLUR_SMALL_MAX pixels a side, whatever the image's size
// and the blur's radius. Pixels are 32-bit ARGB (alpha in the top byte).
// The small copy is premultiplied by alpha, so clear parts don't darken
// the colours next to them. plugindraw.c reads and writes the surfaces.

#include <stdint.h>

#define BLUR_SMALL_MAX 256  // the small copy's longest side
#define BLUR_SMALL_RADIUS 3 // the box's radius in the small copy, at most
#define BLUR_PASSES 3       // box blurs, which come out close to a gaussian
#define BLUR_RADIUS_MAX 1024
#define BLUR_SIDE_MAX 4096  // the widest a blur can be enlarged to

// The small copy for blurring a src_w by src_h image into w by h by
// radius pixels: sw by sh, small enough for its box to be at most
// BLUR_SMALL_RADIUS and its sides at most BLUR_SMALL_MAX, and never larger
// than the image, so each of its pixels covers at least one of the
// image's; and the box's radius there, in *r, at least 1. radius is from
// 1 to BLUR_RADIUS_MAX. Returns 0, or -1 for a bad argument.
int Blur_plan(int src_w, int src_h, int w, int h, int radius, int* sw, int* sh, int* r);

// Shrinks a w by h image into the sw by sh small copy, no larger, one row
// at a time, each small pixel the average of the image's pixels it
// covers. sums is sw * 4 uint64_t of the caller's, zeroed by
// Blur_shrinkStart.
typedef struct BlurShrink {
	int w, h, sw, sh;
	int row;          // the next of the image's rows
	int small_row;    // the small row it adds to
	uint64_t* sums;   // premultiplied a, r, g, b of the small row being made
	uint32_t* small;  // sw * sh, premultiplied
} BlurShrink;

void Blur_shrinkStart(BlurShrink* s, int w, int h, int sw, int sh, uint64_t* sums, uint32_t* small);

// Adds the image's next row, w pixels. Returns 0, or -1 past its last row.
int Blur_shrinkRow(BlurShrink* s, const uint32_t* row);

// One box blur of radius r across and then down a w by h premultiplied
// image, the edge pixels repeated past the edges. tmp is max(w, h) pixels.
void Blur_box(uint32_t* px, int w, int h, int r, uint32_t* tmp);

// Enlarges the sw by sh premultiplied small copy into dst, w by h with rows
// pitch pixels apart, smoothly (bilinear), and takes the premultiplication
// back out. Does nothing for a w over BLUR_SIDE_MAX.
void Blur_grow(const uint32_t* small, int sw, int sh, uint32_t* dst, int pitch, int w, int h);

#endif
