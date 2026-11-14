// The second screen's model: where things go on the companion frame, how
// the name wraps, what the panel beside or under the art shows, the window
// size setting and the frame file the plugin hands the display program.
// No SDL, so the plugin, the display program and the unit test all build
// it.

#ifndef SS_MODEL_H
#define SS_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until SS_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct SSWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} SSWords;
void SS_setWords(const SSWords* words);

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

// What the panel beside or under the art shows, each piece on unless its
// setting (details, caption, marks, music) is "off". A piece whose owner
// is off doesn't show either, as nothing answers for it.
typedef struct SSShow {
	int details; // metadata-band's fields, from its info.format
	int caption; // playtime-captions' caption, from its captions.text
	int marks;   // favorite and tag marks, from favorites' and tags' sets
	int music;   // what music plays, from music's music.now
} SSShow;
typedef const char* (*SSSetting)(const char* key, const char* fallback);
void SS_showFrom(SSSetting get, SSShow* out);

// The label for an .info key info.format replies with, such as "Year" for
// year, or NULL for a key the panel doesn't show.
const char* SS_infoLabel(const char* key);

// "<label>: <value>" for each field in an info.format reply (count=, then
// <key>=<value> for each field with a value, in metadata-band's order,
// then line=), up to max lines. Returns the lines written.
#define SS_INFO_MAX 5
int SS_infoLines(const char* reply, char lines[][SS_LINE_SIZE], int max);

// Adds a set's tag to list, the tags joined with ", ", when the set is a
// tags:<name> one. Returns 1 when it was added.
int SS_tagAdd(char* list, size_t size, const char* set_name);

// The marks line: "Favorite", "Tags: <tags>", both joined with " · ", or
// "" for neither. Returns 1 when there's a mark.
int SS_marks(int favorite, const char* tags, char* out, size_t size);

// What the panel has for an entry, as the plugin gathered it on the
// launcher's thread: NULL or "" for a piece nobody answered for.
typedef struct SSPieces {
	int empty;           // an empty folder
	int type;            // an enum MosaicEntryType
	const char* info;    // info.format's reply
	const char* caption; // captions.text's text=
	int favorite;        // in the favorites set
	const char* tags;    // the tags it has, joined with ", "
	const char* music;   // music.now's line=
} SSPieces;

// The panel's rows under the name, in order, and the now playing line
// kept for the bottom of the frame ("" for none).
enum { SS_ROW_KIND, SS_ROW_MARKS, SS_ROW_INFO, SS_ROW_CAPTION };
#define SS_ROWS_MAX (3 + SS_INFO_MAX)
typedef struct SSPanel {
	struct {
		int style; // an SS_ROW_*
		char text[SS_LINE_SIZE];
	} rows[SS_ROWS_MAX];
	int count;
	char music[SS_LINE_SIZE];
} SSPanel;
void SS_panel(const SSShow* show, const SSPieces* pieces, SSPanel* out);

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
