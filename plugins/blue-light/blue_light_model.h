#ifndef BLUE_LIGHT_MODEL_H
#define BLUE_LIGHT_MODEL_H

// The blue light tint's logic, without SDL or the host: the colours, the
// schedule and the toggle.

#include "mosaic_plugin.h"

#define BL_STRENGTH_DEFAULT 50 // percent
#define BL_STRENGTH_STEP 10    // the strengths are 10 to 100 in these steps
#define BL_DAY (24 * 60)       // minutes

// The colours, by the colour setting's names, or -1 for anything else.
enum { BL_AMBER, BL_ORANGE, BL_RED, BL_COLOURS };
int BL_colour(const char* name);

// What white turns into at full strength.
MosaicColor BL_colourAt100(int colour);

// The strength setting's percent, 10 to 100 in steps of 10, or -1 for
// anything else.
int BL_strength(const char* text);

// The tint the launcher multiplies the screen by: each channel goes from
// 255 (nothing) at 0% to the colour's at 100%.
MosaicColor BL_tint(int colour, int strength);

// "HH:MM" as minutes after midnight, or -1.
int BL_parseTime(const char* text);

// minutes as "HH:MM" into out (at least 6 bytes).
void BL_formatTime(int minutes, char* out, int size);

// 1 when minute (after midnight) is in the range from from up to to,
// which crosses midnight when to is earlier than from, such as 21:00 to
// 07:00. An empty range (from equal to to) has no minutes.
int BL_inRange(int minute, int from, int to);

// The next edge of the range after minute: the minutes to wait, 1 to
// BL_DAY, and the edge itself in *edge when edge isn't NULL. -1 for an
// empty range, which has none.
int BL_toNextEdge(int minute, int from, int to, int* edge);

// The game list button the button setting names (MOSAIC_BUTTON_Y, L2, R2
// or SELECT), or -1 for none or anything else.
int BL_button(const char* name);

// Whether the tint is on. Without a schedule, or with an empty range, it's
// the toggle's (manual). With one, it's on inside the range, unless the toggle forced it the
// other way (forced 0 or 1, -1 for not), which lasts until the next edge
// of the range, the time until (seconds since 1970).
typedef struct BLState {
	int manual;
	int scheduled;
	int from, to; // minutes after midnight
	int forced;
	long long until;
} BLState;

// The time now, as seconds since 1970 and its local minutes and seconds
// after midnight.
typedef struct BLClock {
	long long now;
	int minute, second;
} BLClock;

// 1 when the tint is on at clock.
int BL_active(const BLState* s, const BLClock* clock);

// 1 when a forcing has ended at clock and can be forgotten.
int BL_forcedEnded(const BLState* s, const BLClock* clock);

// Flips the tint at clock, as the toggle does. Without a schedule it
// flips manual. With one, it forces the other way until the range's next
// edge, or stops forcing when that's what the schedule wants anyway.
void BL_toggle(BLState* s, const BLClock* clock);

#endif
