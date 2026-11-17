// Manuals, without the host: where a game's manual is, its pages in
// order, a text manual read and wrapped, where a zoomed page is panned
// to, and the last page read of each game. No SDL; it reads the card with
// libc only, so it's tested on the host.
//
// A game's manual is in the .manuals folder beside it, as stock keeps art
// in .media: <folder>/.manuals/<name>/ holding its pages as images, 01.png,
// 02.png and so on, or <folder>/.manuals/<name>.txt. <name> is the game's
// file name without its extension, as for its art. A game in a folder of
// its own, such as Game/Game.m3u, is named by that folder, beside it.

#ifndef MANUAL_MODEL_H
#define MANUAL_MODEL_H

#include <stddef.h>

#define MN_PATH_MAX 1024
#define MN_NAME_MAX 256
#define MN_MAX_PAGES 500        // pages listed, at most, in name order
#define MN_TEXT_MAX (1 << 20)   // the most of a text manual read, in bytes
#define MN_MEMORY_MAX 500       // games whose last page is kept, the latest first
#define MN_SHELF_MAX 64         // folders whose .manuals listing is kept at once

enum {
	MN_NONE = 0,
	MN_PAGES = 1, // a folder of images
	MN_TEXT = 2,  // a .txt
};

///////////////////////////////////////
// names

// 1 for a page's file name: a .png, .jpg or .jpeg, any case, with
// something before the extension and not hidden (a leading dot, as
// macOS's ._ files have).
int MN_isPage(const char* file);

// Compares names as people count: runs of digits by their value, so 2.png
// comes before 10.png, and otherwise byte by byte, ignoring ASCII case; a
// tie is broken bytewise.
int MN_nameCompare(const char* a, const char* b);

// Writes name without a disc marker, " (Disc 1)", " (CD 2)" or " (Disk 3
// of 4)" in any case, as multi-disc groups loose discs, and returns 1, or
// copies it and returns 0 when it has none.
int MN_stripDisc(const char* name, char* out, size_t size);

// Where a game's manual is looked for: the folders whose .manuals holds
// it (dirs, the game's own folder first, then its system folder in Roms
// when it's deeper than that) and the names it's looked for under (names:
// the game's file name without its extension, then that without a disc
// marker, then shown, the name the game list shows for it, when given and
// different). A game in a folder of its own (an .m3u or .cue in a folder
// of the same name, or the folder itself) is named by the folder, in the
// folder's parent. roms is the card's Roms folder. Returns how many names
// (dirs has *dir_count), or 0 for a path with no name.
typedef struct MNWhere {
	char dirs[2][MN_PATH_MAX];
	int dir_count;
	char names[3][MN_NAME_MAX];
	int name_count;
} MNWhere;
int MN_where(const char* roms, const char* path, int is_dir, const char* shown, MNWhere* out);

///////////////////////////////////////
// finding manuals

// A manual in a .manuals folder: the name it's for, the folder or .txt
// it is, as named on the card, and its kind.
typedef struct MNShelfItem {
	char name[MN_NAME_MAX];
	char file[MN_NAME_MAX];
	int kind;
} MNShelfItem;

// What one folder's .manuals has, sorted by name ignoring ASCII case, as
// the card's FAT matches names.
typedef struct MNShelfDir {
	char dir[MN_PATH_MAX];
	MNShelfItem* items;
	int count;
	unsigned long used;
} MNShelfDir;

// The .manuals listings read so far, kept until MN_shelfClear, so asking
// about every row of a list reads each folder once.
typedef struct MNShelf {
	MNShelfDir dirs[MN_SHELF_MAX];
	int count;
	unsigned long clock;
} MNShelf;

void MN_shelfInit(MNShelf* shelf);
void MN_shelfClear(MNShelf* shelf);

// The manual dir's .manuals has for name, ignoring ASCII case: a folder
// with at least one page (MN_PAGES; a real folder, not a link), or else a
// regular file <name>.txt (MN_TEXT). NULL for none.
const MNShelfItem* MN_shelfFind(MNShelf* shelf, const char* dir, const char* name);

// Finds a game's manual, as MN_where says where, writing its kind and its
// folder or .txt to path. Returns the kind, or MN_NONE.
int MN_find(MNShelf* shelf, const MNWhere* where, char* path, size_t size);

///////////////////////////////////////
// pages

typedef struct MNPages {
	char (*files)[MN_NAME_MAX]; // in MN_nameCompare order
	int count;
} MNPages;

void MN_pagesFree(MNPages* pages);

// Lists the pages in dir: regular files MN_isPage takes, looked at
// without following links, at most MN_MAX_PAGES. Returns how many, or -1
// when dir isn't a real folder.
int MN_pagesScan(const char* dir, MNPages* pages);

///////////////////////////////////////
// text

typedef struct MNLine {
	int start; // its first byte in the text
	int len;   // its bytes, without the newline or the space it broke at
} MNLine;

typedef struct MNText {
	char* text; // tidied: valid UTF-8, \n line ends, tabs as spaces, no other control characters
	int size;
	MNLine* lines;
	int line_count;
	int truncated; // 1 when the file was longer than MN_TEXT_MAX
} MNText;

void MN_textFree(MNText* text);

// Tidies size bytes of raw text into text->text: a byte order mark and
// \r dropped (\r alone ends a line), a tab as 4 spaces, other control
// characters dropped and any byte that isn't part of valid UTF-8 as "?".
// Returns 0, or -1 when it can't allocate.
int MN_textSet(MNText* text, const char* raw, size_t size);

// Reads path, a regular file (not a link), into text with MN_textSet,
// its first MN_TEXT_MAX bytes when it's longer. Returns 0, or -1.
int MN_textLoad(const char* path, MNText* text);

// The width of one character, len bytes of UTF-8 at s, in pixels.
typedef int (*MNCharWidth)(const char* s, int len, void* ctx);

// Wraps text->text into lines no wider than width, breaking at the last
// space that fits, or between characters in a word wider than the line;
// each newline ends a line, and an empty line stays one. Returns how many
// lines, or -1.
int MN_wrap(MNText* text, int width, MNCharWidth char_width, void* ctx);

// The first line to show so it ends on the last, rows to a screen.
int MN_lastTop(int line_count, int rows);

// top moved by delta, kept from 0 to MN_lastTop.
int MN_scroll(int top, int delta, int line_count, int rows);

///////////////////////////////////////
// a page on screen

typedef struct MNRect {
	int x, y, w, h;
} MNRect;

// The size w by h takes to fit inside box_w by box_h, keeping its shape,
// smaller or larger, at least 1 a side. Returns 0, or -1 for a size under
// 1.
int MN_fit(int w, int h, int box_w, int box_h, int* fw, int* fh);

// Where an image w by h is drawn in view, and which part of it, with its
// top left at pan_x, pan_y in the image: centred on an axis it's smaller
// than view along, and showing view's size from the pan along one it's
// bigger. Writes the image's part to src and where it goes to dst.
void MN_place(const MNRect* view, int w, int h, int pan_x, int pan_y, MNRect* src, MNRect* dst);

// The rect around both of a and b, either of which can be empty (w or h
// 0), for the status pill and header widgets the screen leaves clear.
MNRect MN_hole(const MNRect* a, const MNRect* b);

// The parts of r outside hole, as up to 4 rects: above it, left and right
// of it, and below it. Returns how many.
int MN_avoid(const MNRect* r, const MNRect* hole, MNRect out[4]);

// A pan kept inside the image: from 0 to its size less the view's along
// each axis, 0 where the image is the smaller.
void MN_panClamp(const MNRect* view, int w, int h, int* pan_x, int* pan_y);

// The pan that keeps the same point of a page in view's centre when its
// image goes from w0 by h0 to w1 by h1, such as zooming in or out.
void MN_panZoom(const MNRect* view, int w0, int h0, int pan_x0, int pan_y0, int w1, int h1, int* pan_x, int* pan_y);

///////////////////////////////////////
// the last page read

typedef struct MNMemoryItem {
	char key[MN_PATH_MAX]; // the game's path, from the card's root
	int page;              // the page from 0, or a text manual's first line
} MNMemoryItem;

typedef struct MNMemory {
	MNMemoryItem* items; // the latest first
	int count;
} MNMemory;

void MN_memoryInit(MNMemory* memory);
void MN_memoryFree(MNMemory* memory);

// Reads lines of "<page>\t<key>", skipping any it can't read, at most
// MN_MEMORY_MAX. A missing file is an empty memory. Returns how many.
int MN_memoryLoad(MNMemory* memory, const char* file);

// Writes it to file, through a temporary file beside it. Returns 0, or -1.
int MN_memorySave(const MNMemory* memory, const char* file);

// The page kept for key, or 0.
int MN_memoryGet(const MNMemory* memory, const char* key);

// Keeps page for key, first, dropping the oldest past MN_MEMORY_MAX.
// Returns 1 when it changed anything, 0 when it was already so, or -1.
int MN_memorySet(MNMemory* memory, const char* key, int page);

// key for path: the part after the card's root, or path when it's
// elsewhere.
void MN_memoryKey(const char* sdcard, const char* path, char* out, size_t size);

#endif
