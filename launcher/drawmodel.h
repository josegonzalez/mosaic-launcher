#ifndef DRAWMODEL_H
#define DRAWMODEL_H

// The parts of the drawing calls in mosaic_plugin.h that don't need SDL:
// which surfaces exist and who owns them, text truncation, rounded corners
// and fitting one size into another. plugindraw.c does the drawing.

#include <stddef.h>
#include "mosaic_plugin.h"

#define DRAW_HOST ""                       // the owner of surfaces the launcher lends to hooks
#define DRAW_MAX_SIDE 4096                 // the widest or tallest surface a plugin can make
#define DRAW_PLUGIN_BYTES (32 * 1024 * 1024) // what one plugin's surfaces can take together

// Records a surface for owner, a plugin id or DRAW_HOST, taking bytes of
// owner's budget (DRAW_HOST has none). Returns 0, or -1 for a bad
// argument, a surface already recorded, or a plugin over its budget.
int DrawReg_add(const void* surface, const char* owner, size_t bytes);

// 1 when owner can make a surface of bytes more.
int DrawReg_fits(const char* owner, size_t bytes);

// The owner of a recorded surface, or NULL for one that isn't.
const char* DrawReg_owner(const void* surface);

// Makes a DRAW_HOST surface read-only to plugins, such as an image in the
// art cache: they can draw it and read it, but not draw on it. Returns 0,
// or -1 for a surface that isn't recorded or isn't DRAW_HOST's.
int DrawReg_lock(const void* surface);

// 1 when a recorded surface can be drawn on.
int DrawReg_writable(const void* surface);

// Forgets a surface owner recorded. Returns 0, or -1 when owner didn't.
int DrawReg_remove(const void* surface, const char* owner);

// Forgets up to max of owner's surfaces, writing them to out to be freed.
// Returns how many.
int DrawReg_take(const char* owner, void** out, int max);

// What owner's surfaces take now.
size_t DrawReg_bytes(const char* owner);

// Measures text in pixels, or returns -1.
typedef int (*DrawMeasure)(const char* text, void* data);

// Writes text's first line to out, shortened with "..." when it's wider
// than max_width or doesn't fit in size, cutting only between UTF-8
// characters. Writes "" when not even "..." fits. Returns the width
// written, or -1 for a bad argument or a failed measure.
int Draw_truncate(const char* text, int max_width, DrawMeasure measure, void* data, char* out, size_t size);

// radius cut down to at most half of w and h, or -1 for a negative one.
int Draw_clampRadius(int radius, int w, int h);

// How far in from each side row (from 0) of an h tall rounded rect starts,
// for a radius from Draw_clampRadius.
int Draw_roundInset(int radius, int h, int row);

// The part of a sw by sh source (src) to draw where in a dw by dh one
// (dst) for an enum MosaicFit. Returns 0, or -1 for a bad argument.
int Draw_fit(int sw, int sh, int dw, int dh, int fit, MosaicRect* src, MosaicRect* dst);

#endif
