// The second screen's model: where things go on the companion frame, how
// the name wraps, which .info lines show, the window size setting and the
// frame file the plugin hands the display program. No SDL, so the plugin,
// the display program and the unit test all build it.

#ifndef SS_MODEL_H
#define SS_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

#define SS_LINE_SIZE 256
#define SS_TITLE_LINES 3
#define SS_SIZE_DEFAULT "640x480"
#define SS_SIDE_MIN 160
#define SS_SIDE_MAX 1920

// 1 on a platform with a display beside the launcher's own: only the
// desktop build, which opens a second window. No NextUI device has one.
int SS_hasSecondDisplay(const char* platform);

// The size setting, "<w>x<h>", into *w and *h: 1, or 0 (leaving them) for
// anything else or a side outside SS_SIDE_MIN to SS_SIDE_MAX.
int SS_parseSize(const char* text, int* w, int* h);

// Where the art and the text go on a w by h frame. On a frame at least
// 4:3 wide the art is a square on the left and the text on its right;
// on a narrower one the art is on top, at most half of what the margins
// leave, and the text under it.
typedef struct SSLayout {
	MosaicRect art;  // the box the art is fitted into
	MosaicRect text; // the text's column
	int margin;
	int beside; // 1 when the text is right of the art, 0 when under it
} SSLayout;
void SS_layout(int w, int h, SSLayout* out);

// src_w by src_h fitted into box keeping its shape, centred: the rect to
// draw it in. A zero rect for a bad size.
MosaicRect SS_fit(int src_w, int src_h, MosaicRect box);

// Wraps text at spaces into up to max_lines lines no wider than max_width,
// as measure says, with whatever's left on the last line (the caller cuts
// it with "..."). A word wider than max_width gets a line of its own.
// Returns the lines written, 0 for empty text.
typedef int (*SSMeasure)(const char* text, void* data);
int SS_wrap(const char* text, int max_width, int max_lines, SSMeasure measure, void* data, char lines[][SS_LINE_SIZE]);

// The .info keys the frame shows, in order, with their labels.
enum { SS_YEAR, SS_GENRE, SS_PUBLISHER, SS_DEVELOPER, SS_PLAYERS, SS_INFO_COUNT };
const char* SS_infoKey(int field);
const char* SS_infoLabel(int field);

// A raw .info value tidied: spaces trimmed, and a year written as a date
// (1994-11-21) cut to its four digits. Returns 1 when something's left.
int SS_clean(int field, const char* raw, char* out, size_t size);

// "<label>: <value>" for each field get finds (get writes the raw value
// and returns 1, or 0 for none), up to max lines. Returns the lines written.
typedef int (*SSInfoGet)(const char* key, char* out, size_t size, void* data);
int SS_infoLines(SSInfoGet get, void* data, char lines[][SS_LINE_SIZE], int max);

// What an entry is (an enum MosaicEntryType), for the frame: "Game",
// "Folder", "Pak", or "" for anything else.
const char* SS_kindLabel(int type);

// The frame file: a 16 byte header (the magic, then the frame's number,
// width and height as little-endian 32-bit words) and w * h RGBA pixels,
// four bytes each, a row after another from the top.
#define SS_FRAME_MAGIC "MSS1"
#define SS_FRAME_HEADER 16
void SS_frameHeader(unsigned char out[SS_FRAME_HEADER], unsigned int number, int w, int h);
// Reads a header from len bytes: 1 with *number, *w and *h when the magic
// matches, the size is in range and len holds every pixel, or 0.
int SS_frameParse(const unsigned char* data, size_t len, unsigned int* number, int* w, int* h);

// Whether the display program gets entry ("NAME=value") from the
// launcher's environment: only what SDL needs to open a window on the
// desktop, and the test's MOSAIC_SECOND_SCREEN_SHOT. Never LD_PRELOAD,
// DYLD_INSERT_LIBRARIES or any other LD_ or DYLD_ variable but the
// library paths SDL may be found on.
int SS_keepEnv(const char* entry);

#endif
