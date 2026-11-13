// Hero and card rail layout, without the host: its settings, where the
// hero art, the logo and the cards go, which cards are in view, where the
// d-pad moves the selection, how far the logo has faded in, and the parts
// of the hero drawn around the status pill. No SDL and no file access, so
// it's tested on the host.
//
// The hero is the selected entry's art across the area from its top to
// the middle of the cards, cover-fitted, with its bottom fading into the
// theme's background colour, and the rest of the area below it filled with
// that colour. The cards sit in a row along the bottom, each with its name
// below it, and the selected one is raised. The logo, or else the name,
// sits over the hero at the bottom left, above the cards.

#ifndef HERO_MODEL_H
#define HERO_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

#define HR_MAX_CARDS 16

// The card size setting, small, medium or large, as the cards' height in
// percent of the area's: 22, 28 or 34, or medium's for anything else.
int HR_cardPercent(const char* value);

// The value a setting that can follow the theme gives: the setting itself,
// or for theme (or NULL or "", unset) the theme's layout-hero-rail.cards, theme_value, which
// is NULL when the theme doesn't set it. NULL for neither, which HR_cardPercent
// takes as its default.
const char* HR_themed(const char* setting, const char* theme_value);

// The tint setting, a percentage from 0 to 100, as the alpha of the theme
// colour drawn over the hero art, from 0 to 255. Anything else is
// HR_TINT_DEFAULT.
#define HR_TINT_DEFAULT 20
int HR_tint(const char* value);

// The fade setting, how long the logo takes to fade in, in milliseconds
// from 0 to 2000. Anything else is HR_FADE_DEFAULT.
#define HR_FADE_DEFAULT 300
int HR_fadeMs(const char* value);

typedef struct HRLayout {
	MosaicRect hero;  // the hero art, from the area's top to the middle of the cards
	int fade_y;       // where in the hero, from its top, it starts fading into the background colour
	MosaicRect below; // the rest of the area under the hero
	MosaicRect logo;  // where the logo or name fits, above the raised card
	MosaicRect hole;  // the status pill and header widgets together, to leave clear (w 0 for none)
	int cards;        // how many cards are in view
	int card_x;       // the first card's left
	int card_y;       // the top of a card that isn't raised
	int card_w, card_h;
	int gap;    // between cards, and around the edges
	int raise;  // how far the selected card is raised
	int frame;  // the selected card's highlight around it
	int name_h; // the name strip under each card, 0 for none
} HRLayout;

// Lays the rail and hero out in area, leaving status and header (either
// can be NULL or empty) clear. percent is the cards' height (see
// HR_cardPercent), and name_h the name strip under each card, 0 when names
// don't show. Returns 0, or -1 when the area is too small for a card.
int HR_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int scale, int percent,
	int name_h, HRLayout* out);

// The card at slot, counted from the left of the rail, raised when it's
// the selected one; and the name strip under a card (h 0 for none).
MosaicRect HR_card(const HRLayout* layout, int slot, int raised);
MosaicRect HR_nameStrip(const HRLayout* layout, const MosaicRect* card);

// The first entry in view, given the one in view before, so the rail moves
// only as far as it takes to show the selected entry with one card either
// side (when there are 3 or more in view), and never past the last card.
// 0 for an empty list.
int HR_window(int first, int selected, int count, int cards);

// Where a d-pad direction (MOSAIC_BUTTON_UP, DOWN, LEFT or RIGHT) moves
// the selection in count entries with cards in view. Left and right step
// one entry; up goes back a rail's worth of cards and down on one,
// stopping at the first and last entries. From the first or last entry a
// press (fresh is 1) the way off the end wraps to the other end, and a
// held repeat stays. Returns the new index, or selected when it doesn't
// move.
int HR_move(int selected, int count, int cards, int dir, int fresh);

// The logo's alpha, 0 to 255, elapsed milliseconds into a fade of duration,
// easing out. A duration of 0 or less is 255 at once.
int HR_fade(long elapsed, int duration);

// The parts of r outside hole: above it, either side of it and below it,
// written to out and counted, or r itself when they don't meet.
int HR_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]);

// The part of a src_w by src_h image that covers a dst_w by dst_h area
// keeping its shape: all of it one way, centred and cropped the other.
// Returns 0, or -1 for an empty size.
int HR_cover(int src_w, int src_h, int dst_w, int dst_h, MosaicRect* crop);

// A src_w by src_h image fitted inside box keeping its shape, at its
// bottom left. w 0 for an empty size.
MosaicRect HR_fit(int src_w, int src_h, const MosaicRect* box);

// The hero's fade over its rows from fade_y to h, in bands each drawn at
// one alpha of the background colour, from nearly nothing to nearly full.
// HR_bands returns how many (up to HR_MAX_BANDS, each at least 2 rows), and
// HR_band writes band i's top, height and alpha and returns 0, or -1 past
// the end.
#define HR_MAX_BANDS 64
int HR_bands(int fade_y, int h);
int HR_band(int fade_y, int h, int i, int* y, int* band_h, int* alpha);

// The letter a card with no art shows for a name: its first letter or
// digit, upper case, or first character that isn't ASCII (a whole UTF-8
// sequence), or else its first character that isn't a space; "" for a
// name with none.
void HR_letter(const char* name, char* out, size_t size);

#endif
