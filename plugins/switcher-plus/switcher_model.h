// Game switcher plus, without the host: its style setting, where the
// d-pad and a removal leave the selection, the button hints, the recents
// it keeps until they change, where each style puts the title, the
// previews and the rows, and the play time captions and where they go. No
// SDL and no file access, so it's tested on the host.

#ifndef SWITCHER_MODEL_H
#define SWITCHER_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until SP_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct SPWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} SPWords;
void SP_setWords(const SPWords* words);

// recents kept, at most; stock keeps 24
#define SP_MAX_RECENTS 64
#define SP_PATH_MAX 512
// previews either side of the selected one in the strip, at most
#define SP_MAX_SIDES 3

// The style setting.
enum SPStyle {
	SP_STYLE_FULL,  // stock's: the selected recent's preview across the screen, its name on a pill
	SP_STYLE_STRIP, // the selected preview large in the middle, its neighbours smaller either side
	SP_STYLE_LIST,  // the recents' names in rows, the selected one's preview in a panel beside them
};

// full, strip or list; full for anything else.
int SP_style(const char* value);

// Where a step of by entries moves the selection in count entries. A step
// past an end stops at the first or last entry; from the first or last
// entry, a press (fresh is 1) that way wraps to the other end and a held
// repeat stays. Returns the new index, or selected when it doesn't move.
int SP_move(int selected, int count, int by, int fresh);

// The selection after the recent at selected was removed, leaving count:
// the one before it, or the last one from the first, as stock's switcher
// does. 0 when none are left.
int SP_afterRemove(int selected, int count);

// The first row in view, given the one in view before, so the list moves
// only as far as it takes to show the selected row. 0 for an empty list.
int SP_window(int first, int selected, int count, int rows);

// Stock's switcher's hints: with no recents only B BACK, on the right;
// else B BACK on the left, and Y REMOVE and A RESUME on the right when the
// selected recent has a state to resume, or A OPEN alone when it doesn't.
// Writes each group and its count (0 for none).
void SP_hints(int count, int can_resume, MosaicHint left[2], int* left_count, MosaicHint right[2], int* right_count);

///////////////////////////////////////
// the recents, kept until they change

typedef struct SPRecents {
	int count;
	char path[SP_MAX_RECENTS][SP_PATH_MAX];
	int known[SP_MAX_RECENTS]; // 1 once info is filled in, -1 when asking failed
	MosaicRecent info[SP_MAX_RECENTS];
} SPRecents;

// how SP_sync and SP_info ask the host
typedef const char* (*SPPathFn)(int index, void* data);
typedef int (*SPInfoFn)(int index, MosaicRecent* out, void* data);

// Compares the recents with count recents whose paths path gives (count
// past SP_MAX_RECENTS is cut to it). Returns 1 when they changed, forgetting
// every recent's info, or 0 when they're the same.
int SP_sync(SPRecents* recents, int count, SPPathFn path, void* data);

// The recent at index's info, asked of info only the first time since it
// changed. NULL past the end, or when info failed.
const MosaicRecent* SP_info(SPRecents* recents, int index, SPInfoFn info, void* data);

///////////////////////////////////////
// layout

// The parts of r outside hole: above it, either side of it and below it,
// written to out and counted, or r itself when they don't meet. 0 for an
// empty r.
int SP_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]);

// The status pill and header widgets together (either can be NULL or
// empty), as one rect to leave clear; w 0 for none.
MosaicRect SP_hole(const MosaicRect* status, const MosaicRect* header);

// The part of a and b both; w 0 when they don't meet.
MosaicRect SP_meet(const MosaicRect* a, const MosaicRect* b);

// part of dst, where a src_w by src_h image is stretched across dst, as
// the part of the image that lands there.
MosaicRect SP_source(const MosaicRect* dst, const MosaicRect* part, int src_w, int src_h);

// A src_w by src_h image fitted inside box keeping its shape, centred. w 0
// for an empty size.
MosaicRect SP_fit(int src_w, int src_h, const MosaicRect* box);

// Stock's title pill in area's top left, PADDING in, PILL_SIZE tall, for a
// name text_w wide with BUTTON_PADDING either side, at most as wide as the
// room left of hole (or of the area's right) less PADDING each side. w 0
// when there's no room.
MosaicRect SP_title(const MosaicRect* area, const MosaicRect* hole, int scale, int text_w);

// Where previews and rows go: area less PADDING all round, below hole and
// a PADDING under it, and above the hint pills (PILL_SIZE and a PADDING
// over the bottom PADDING). w 0 when nothing's left.
MosaicRect SP_room(const MosaicRect* area, const MosaicRect* hole, int scale);

typedef struct SPStrip {
	MosaicRect room;
	int sides;                       // previews either side of the selected one
	int w[SP_MAX_SIDES + 1];         // each preview's width: w[0] the selected one's, then outwards
	int h[SP_MAX_SIDES + 1];
	int centre[SP_MAX_SIDES + 1];    // how far each preview's middle is from the selected one's
	int cx, cy;                      // the middle of the selected preview
	MosaicRect name;                 // the name pill's strip under the selected preview
} SPStrip;

// Lays the strip out in room (see SP_room), for previews aspect_w by
// aspect_h (the screen's shape) and a name strip name_h tall under the
// selected one, with a PADDING gap around them. The selected preview is
// as tall as the room less the name strip allows, and at most two fifths
// of the room's width; each one out from it is 3/5 the size of the one before,
// and as many as fit whole in the room, at least min_w wide, show either
// side, up to SP_MAX_SIDES. Previews and name are centred in the room
// together. Returns 0, or -1 when not even the selected one fits at min_w.
int SP_strip(const MosaicRect* room, int aspect_w, int aspect_h, int scale, int name_h, int min_w, SPStrip* out);

// The preview offset steps from the selected one (negative to its left);
// w 0 past the sides.
MosaicRect SP_stripCard(const SPStrip* strip, int offset);

typedef struct SPList {
	MosaicRect rows;  // the column of rows, on the left
	int row_h;
	int count;        // rows that fit
	MosaicRect panel; // the selected recent's preview, on the right
} SPList;

// Lays the list out in area: rows PILL_SIZE tall in a column from
// PADDING in at the top left, two fifths of the width less PADDING (below
// hole too when it reaches over the column), down to the hint pills; and
// the panel in the rest of room (see SP_room) right of it, a PADDING
// between them. Returns 0, or -1 when not even a row fits.
int SP_list(const MosaicRect* area, const MosaicRect* hole, int scale, SPList* out);


///////////////////////////////////////
// play time captions, written as playtime-captions writes them: a copy of
// its formatter, as plugins can't share code

// How the parts read, as playtime-captions' format setting picks.
enum SPFormat {
	SP_FORMAT_RELATIVE, // "2h 15m · 3 days ago · 4 sessions"
	SP_FORMAT_COMPACT,  // "2h · 3d · 4x"
	SP_FORMAT_DATE,     // "2h 15m · Nov 5 · 4 sessions"
};

// The parts a caption can have, as bits, shown in this order.
enum SPPart {
	SP_PART_TIME = 1,
	SP_PART_LAST = 2,
	SP_PART_SESSIONS = 4,
	SP_PART_ACHIEVEMENTS = 8,
};

// What the play history says about a recent, as the host's play_time,
// last_played and play_count return it, and its RetroAchievements
// progress (cheevos_total 0 for none).
typedef struct SPPlay {
	long long time;
	long long last;
	int count;
	int cheevos;
	int cheevos_total;
} SPPlay;

// The parts (SP_PART_* bits), the format (an enum SPFormat) and whether
// games the history hasn't seen say "Never played".
typedef struct SPCaptions {
	int parts;
	int format;
	int never;
} SPCaptions;

// playtime-captions' settings, as its cfg has them: "on" or anything else
// for each part and never, and the format's name ("relative", "compact"
// or "date", relative for anything else).
SPCaptions SP_captions(const char* time, const char* last, const char* sessions, const char* format,
	const char* never, const char* achievements);

// The first max_parts of parts, in format, joined with " · ", as
// playtime-captions' PC_build: a recent with unknown history gets no play
// parts, and one the history hasn't seen "Never played" in their place
// when never is set, or none; the achievements part follows either way
// when it's picked and the game has a total. Returns how many parts it
// wrote, 0 (writing "") for nothing.
int SP_captionBuild(const SPPlay* play, long long now, int parts, int format, int never, int max_parts, char* out,
	size_t size);

// The caption that fits max_width, as playtime-captions' PC_fit: every
// part in the picked format, then compact, then fewer parts, dropping them
// from the end. Returns its width, or 0 (writing "") when nothing fits.
int SP_captionFit(const SPPlay* play, long long now, const SPCaptions* captions, int max_width,
	int (*measure)(const char* text, void* ctx), void* ctx, char* out, size_t size);

// fg moved toward bg by percent, from 0 (fg) to 100 (bg), as 0xRRGGBB.
int SP_mute(int fg, int bg, int percent);

// The most room for the full style's caption pill under the title pill
// (see SP_title): from its left, BUTTON_MARGIN under it, as tall as
// BUTTON_SIZE or text_h when that's taller, and as wide as the title's
// widest, PADDING short of the area's right or of hole where they're
// level; above the hint pills. w 0 for an empty title or no room.
MosaicRect SP_titleCaption(const MosaicRect* area, const MosaicRect* hole, const MosaicRect* title, int scale,
	int text_h);

// The strip style's caption line, text_h tall, BUTTON_MARGIN under the
// name pill, pill_h tall at the top of strip->name, across it. w 0 when
// strip->name has no room for it.
MosaicRect SP_stripCaption(const SPStrip* strip, int pill_h, int scale, int text_h);

// The list style's caption line, text_h tall along the bottom of panel
// (see SP_list), with what's left above it, less BUTTON_MARGIN, written
// to preview for the selected recent's preview. w 0, and preview the whole
// panel, when that would leave the preview less than half of it.
MosaicRect SP_panelCaption(const MosaicRect* panel, int scale, int text_h, MosaicRect* preview);

#endif
