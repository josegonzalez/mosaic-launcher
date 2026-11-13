// Save state manager, without the host: where stock's minarch keeps a
// game's save states and their previews, which slots a game has, the games
// on the card with states, deleting a slot, the grids and where the d-pad
// moves in them, the button hints, the scaled copies kept and how they
// fit. No SDL; it reads and deletes files with libc only, so it's tested on
// the host.
//
// Stock's layout, for a game minarch names <name> (the rom's file name, or
// the .m3u's for a game with discs) in the system tagged <tag>:
//
//   .userdata/shared/<tag>-<core>/<name>.st<slot>   the state, one folder per core
//   .userdata/shared/.minui/<tag>/<name>.<slot>.bmp its preview
//   .userdata/shared/.minui/<tag>/<name>.<slot>.txt the disc it was saved on
//   .userdata/shared/.minui/<tag>/<name>.txt        the last used slot, which
//                                                   the launcher resumes from
//
// with the .srm state formats naming the state <cut>.state, <cut>.state<slot>
// or <cut>.state.<slot> instead (<cut> is <name> less its extension), and
// <cut>.state.auto for slot 9. Slots 0 to 7 are the menu's 1 to 8, slot 8
// is MinUI's default state (loaded whenever the game starts without
// resuming) and slot 9 is the one saved when the device sleeps.

#ifndef STATE_MODEL_H
#define STATE_MODEL_H

#include <stddef.h>
#include <time.h>
#include "mosaic_plugin.h"

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until SM_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct SMWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} SMWords;
void SM_setWords(const SMWords* words);

#define SM_NAME_MAX 256
#define SM_DIR_MAX 512 // the card's .userdata/shared, so it and the names under it fit in SM_PATH_MAX
#define SM_PATH_MAX 1024
#define SM_SLOTS 10
#define SM_AUTO_SLOT 9
#define SM_DEFAULT_SLOT 8
// games listed, at most; past it the least recently saved are left out
#define SM_MAX_GAMES 2048
// columns a grid has, at most
#define SM_MAX_COLS 8
// state files a slot can have, one per core folder
#define SM_MAX_FILES 16

///////////////////////////////////////
// names

// The system tag stock files a game's states under, as its getEmuName
// works it out: the system folder's name (the first folder below roms) or
// what's in its last parentheses, eg. GB for "Game Boy (GB)". Returns 1,
// or 0 for a path that isn't inside a system folder (writing "").
int SM_tagOf(const char* roms, const char* path, char* out, size_t size);

// The file stock resumes a game by, given whether a file exists: for a
// folder (is_dir 1), its "<folder>/<folder>.cue", or else
// "<folder>/<folder>.m3u"; then, for anything but an .m3u, the .m3u named
// for the folder it's in when there is one, so a disc goes by its game's
// .m3u; or else path itself. Writes it and returns 1, or returns 0 for a
// folder with neither (or a path too long).
int SM_gameFile(const char* path, int is_dir, int (*exists)(const char* path, void* ctx), void* ctx, char* out,
	size_t size);

// name less its extension, as minarch cuts it for the .srm formats: a last
// dot with one to four characters after it.
void SM_cut(const char* name, char* out, size_t size);

// The name stock's list shows for a file name: its extensions (one to four
// characters each, eg. .p8.png) and trailing (...) and [...] parts gone,
// and trailing spaces, or the name itself when that leaves nothing.
void SM_displayName(const char* name, char* out, size_t size);

// "Slot 1" to "Slot 8" for slots 0 to 7, "Default" for 8 and "Auto" for 9.
void SM_slotLabel(int slot, char* out, size_t size);

// The slot a last-slot file's text names, read as stock reads it with
// atoi: spaces, a sign and digits, with "" or anything else being 0.
int SM_slotOf(const char* text);

// "2026-11-08 14:03" in local time, or "" for 0.
void SM_date(time_t when, char* out, size_t size);

///////////////////////////////////////
// a game's states

typedef struct SMSlot {
	int slot;          // 0 to 9
	time_t saved;      // the newest of its state files' times
	int files;         // how many state files it has (one per core folder)
	int has_preview;   // 1 when its preview is a regular file
	char preview[SM_PATH_MAX];
} SMSlot;

typedef struct SMGame {
	char tag[SM_NAME_MAX];
	char name[SM_NAME_MAX];    // the file name minarch keeps its states under
	char display[SM_NAME_MAX]; // what the screen shows for it
	int use_cut;               // 1 when the .srm formats' cut names count as its own
	SMSlot slots[SM_SLOTS];    // the slots with a state, in slot order
	int count;
	time_t newest;             // the newest slot's time, 0 with none
} SMGame;

// The card's .userdata/shared, where everything lives.
typedef struct SMCard {
	char shared[SM_DIR_MAX];
} SMCard;

void SM_cardInit(SMCard* card, const char* sdcard);

// Sets up game for tag and name: display is SM_displayName's unless given.
// The .srm formats' names count (use_cut 1) unless another game the card
// knows in the tag cuts to the same name: one in rom_dir (the folder the
// game is in, or NULL), or one with states or slot files under the tag.
// Returns 0, or -1 for a tag or name that can't be a folder or file name.
int SM_gameInit(const SMCard* card, const char* tag, const char* name, const char* display, const char* rom_dir,
	SMGame* game);

// Fills in game's slots from the card, in slot order: a slot is listed when
// it has a state file, a regular file found without following links, in a
// <tag>-<core> folder that's a real folder. Returns how many.
int SM_gameScan(const SMCard* card, SMGame* game);

// The state files a slot of game has on the card, as SM_gameScan finds
// them. Returns how many were written, at most max.
int SM_stateFiles(const SMCard* card, const SMGame* game, int slot, char out[][SM_PATH_MAX], int max);

// Deletes the slot at index from the card and from game: its state files,
// its preview and its disc file, and the last-slot file only when it names
// that slot, so the launcher stops offering to resume it. Only regular
// files in real folders (no links on the way) under shared are removed,
// each looked at again first. Returns how many files went, or -1 when
// none could (game is left as it was).
int SM_remove(const SMCard* card, SMGame* game, int index);

// The states.last service's reply for a scanned game: count=<slots with
// a state>, and with one or more, the most recently saved slot as
// slot=<0 to 9>, label=<SM_slotLabel's>, saved=<Unix time> and
// preview=<its preview's path, or empty>. A NULL game is one with none.
// Returns 0, or -1 when it doesn't fit size.
int SM_reply(const SMGame* game, char* out, size_t size);

///////////////////////////////////////
// every game with states

typedef struct SMList {
	SMGame* games; // the most recently saved first
	int count;
	int cap;
} SMList;

void SM_listInit(SMList* list);
void SM_listFree(SMList* list);

// Lists every game with a state on the card, the most recently saved
// first: names from the <tag>-<core> folders' .st<slot> files and the
// .minui/<tag> folders' previews and slot files, each then scanned with
// SM_gameScan and kept when it has a slot. Returns how many.
int SM_scanAll(const SMCard* card, SMList* list);

// 1 when the card has at least one game SM_scanAll would list; stops at
// the first.
int SM_any(const SMCard* card);

// Drops the game at index from list.
void SM_listDrop(SMList* list, int index);

///////////////////////////////////////
// moving

// Where a step of by moves the selection in count cells: past an end it
// stops at the first or last, and from the first or last a press (fresh
// is 1) that way wraps to the other end while a held repeat stays.
int SM_step(int selected, int count, int by, int fresh);

// Up (dy -1) or down (dy 1) a row in a grid of cols: up from the top row
// and down from the bottom row stay; down onto a short last row lands on
// its last cell.
int SM_row(int selected, int count, int cols, int dy);

// The selection after the cell at selected was removed, leaving count:
// the same place, which is now the next one, or the last. 0 when none
// are left.
int SM_afterRemove(int selected, int count);

// The first row in view, given the one before, so the grid scrolls only
// as far as it takes to show the selected cell's row.
int SM_firstRow(int first, int selected, int count, int cols, int rows);

// What the screen shows.
enum SMView {
	SM_VIEW_GAMES, // the games with states
	SM_VIEW_SLOTS, // one game's slots
	SM_VIEW_FULL,  // one slot's preview across the screen
};

// The hints for a view with count cells: B BACK on the left, and on the
// right A OPEN on the games, Y DELETE (when can_delete) and A VIEW on the
// slots, and Y DELETE in the full view; with nothing to show, B BACK alone
// on the right.
void SM_hints(int view, int count, int can_delete, MosaicHint left[2], int* left_count, MosaicHint right[2],
	int* right_count);

///////////////////////////////////////
// layout

// The parts of r outside hole: up to four, or r itself when they don't
// meet. 0 for an empty r.
int SM_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]);

// status and header together (either can be NULL or empty); w 0 for none.
MosaicRect SM_hole(const MosaicRect* status, const MosaicRect* header);

// A src_w by src_h image fitted inside box keeping its shape, centred; w 0
// for an empty size.
MosaicRect SM_fit(int src_w, int src_h, const MosaicRect* box);

// part of dst, where a src_w by src_h image is stretched across dst, as
// the part of the image that lands there.
MosaicRect SM_source(const MosaicRect* dst, const MosaicRect* part, int src_w, int src_h);

// Stock's title pill in area's top left, PADDING in, PILL_SIZE tall, for
// text text_w wide with BUTTON_PADDING either side, as wide as the room
// left of hole allows. w 0 when there's no room.
MosaicRect SM_titlePill(const MosaicRect* area, const MosaicRect* hole, int scale, int text_w);

typedef struct SMGrid {
	MosaicRect room; // where the cells go
	int cols, rows;
	int cell_w, cell_h; // a cell: the preview, then lines_h of text under it
	int thumb_h;
	int lines_h;
	int gap;
} SMGrid;

// Lays a grid out in area: below the title pill (PADDING, PILL_SIZE and a
// PADDING) and hole, above the hint pills, PADDING in at the sides,
// centred. Cells have previews in the screen's shape (aspect_w by
// aspect_h) and lines_h of text under each, a PADDING apart, in the
// fewest columns from 3 that fit two rows, up to SM_MAX_COLS. With no room
// for a whole row the previews get shorter to fit one. Returns 0, or -1
// when nothing fits.
int SM_grid(const MosaicRect* area, const MosaicRect* hole, int scale, int aspect_w, int aspect_h, int lines_h,
	SMGrid* out);

// The cell for the one slot places past the first in view.
MosaicRect SM_cell(const SMGrid* grid, int slot);

///////////////////////////////////////
// scaled copies, the least recently used going first

typedef struct SMCopy {
	char path[SM_PATH_MAX];
	int w, h;
	MosaicSurface* surface; // NULL for a free slot
	unsigned long used;
} SMCopy;

// The copy of path at w by h, or NULL.
SMCopy* SM_copyFind(SMCopy* copies, int count, const char* path, int w, int h);

// A slot for a new copy: a free one, or else the least recently used (its
// surface is the caller's to free).
SMCopy* SM_copyPick(SMCopy* copies, int count);

#endif
