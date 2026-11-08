// Carousel layout, without the host: how big the covers are and where
// they go, where a cover sits partway through a slide, where the d-pad
// moves the selection, how far a slide has got, and the letter a cover
// without art shows. No SDL and no file access, so it's tested on the
// host.

#ifndef CAROUSEL_MODEL_H
#define CAROUSEL_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// covers either side of the selected one, at most
#define LC_MAX_SIDES 4
// one cover's step, in the units LC_slot positions and slides use
#define LC_UNIT 1000

typedef struct LCStrip {
	MosaicRect room;               // what's left of the area for covers and the name
	int sides;                     // covers either side of the selected one, from 0
	int size[LC_MAX_SIDES + 1];    // each cover's side, square: size[0] the selected one's, then outwards
	int centre[LC_MAX_SIDES + 1];  // how far each cover's middle is from the selected one's
	int cx, cy;                    // the middle of the selected cover
	int gap;                       // between covers
	MosaicRect name;               // the selected entry's name, under its cover; h 0 with no names
} LCStrip;

// Lays the carousel out in area, below status and header (the status pill
// and header widgets, which stay clear; w 0 when there are none), with a
// gap of 4 * scale around it and between covers. The selected cover is
// square, as tall as the room less the name strip (name_h tall, 0 for
// none) and the gap above it, and at most 3/10 of the room's width; each
// cover out from it is 3/5 the size of the one before, and as many as fit
// whole in the room, at least min_size, show either side. The covers'
// middles are on one line, and the covers and name together are centred
// in the room. Returns 0, or -1 when not even the selected cover fits at
// min_size.
int LC_strip(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int name_h,
	int min_size, LCStrip* out);

// The cover at pos, in LC_UNIT steps from the selected cover (negative to
// its left), with the size and place between two covers' worked out in
// proportion, for a slide. Past the last side it stays the outermost
// cover's size, moving on out by the same step. An empty rect for a strip
// with no covers.
MosaicRect LC_slot(const LCStrip* strip, int pos);

// Where a d-pad direction (MOSAIC_BUTTON_UP, DOWN, LEFT or RIGHT) moves
// the selection in count entries: left and right by one, up and down by
// page (back and on). A step past an end stops at the first or last entry;
// from the first or last entry, a press (fresh is 1) wraps to the other
// end and a held repeat stays. Returns the new index, or selected when it
// doesn't move.
int LC_move(int selected, int count, int dir, int fresh, int page);

// How far a slide of duration ms has got after elapsed ms, from 0 to
// LC_UNIT, easing out: quick at first and slowing to a stop.
int LC_ease(unsigned long elapsed, unsigned long duration);

// Where a slide starts when the selection moves by delta while the covers
// are drawn shift from their places (in LC_UNIT steps, 0 at rest): the
// selected cover keeps its place on screen and slides from there. 0, no
// slide, when the result is more than sides steps away, as for a page or
// a wrap.
int LC_slideFrom(int shift, int delta, int sides);

// The letter a cover shows for a name: its first letter or digit, upper
// case, or first character that isn't ASCII (a whole UTF-8 sequence), or
// else its first character that isn't a space; "" for a name with none.
void LC_letter(const char* name, char* out, size_t size);

#endif
