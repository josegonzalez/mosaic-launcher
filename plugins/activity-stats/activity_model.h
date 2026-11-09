// Activity stats, without the host: the games ranked by play time and by
// sessions, the chart's scale and bars, the durations and captions shown,
// the tabs, where the d-pad moves in a list, the hints and the layout. No
// SDL, so it's tested on the host.

#ifndef ACTIVITY_MODEL_H
#define ACTIVITY_MODEL_H

#include <stddef.h>
#include <time.h>
#include "mosaic_plugin.h"

#define AS_NAME_MAX 256
#define AS_PATH_MAX 512
// days the month chart shows; the week chart shows the last AS_WEEK
#define AS_DAYS 30
#define AS_WEEK 7

// One game from the play history.
typedef struct ASGame {
	char path[AS_PATH_MAX]; // its full path on the card, or "" for an orphan
	char name[AS_NAME_MAX]; // what the lists show
	long long seconds;
	long long last_played;
	int count; // sessions
} ASGame;

// What a list shows for a game: the tracker's name, else the path's file
// name without its extension, else "Unknown game".
void AS_name(const char* name, const char* path, char* out, size_t size);

// Fills order with 0..count-1, most played first: the most seconds, then
// the most sessions, then the latest played, then the first listed.
void AS_rankPlayed(const ASGame* games, int count, int* order);

// Fills order with 0..count-1, most sessions first: then the most
// seconds, then the latest played, then the first listed.
void AS_rankLaunched(const ASGame* games, int count, int* order);

// The totals over the games: seconds and sessions, leaving out negatives.
void AS_totals(const ASGame* games, int count, long long* seconds, long long* sessions);

///////////////////////////////////////
// formatting

// "0m", "<1m", "45m", "2h", "2h 5m"; "" for a negative.
void AS_duration(long long seconds, char* out, size_t size);

// "1 session", "4 sessions".
void AS_sessions(long long count, char* out, size_t size);

// A list row's right side: on the games tab (launches 0) "2h 5m · 4
// sessions", on the launches tab "4 sessions · 2h 5m".
void AS_rowRight(const ASGame* game, int launches, char* out, size_t size);

// The list caption: "3 games · 2h 30m · 5 sessions", "1 game · ...".
void AS_listCaption(int games, long long seconds, long long sessions, char* out, size_t size);

// The chart caption for the last days days, from day[0] (today) on:
// "7 days: 2h 30m · 21m a day", the average over the days it knows
// (negatives are unknown), or "7 days: no play history" when it knows
// none of them.
void AS_chartCaption(const long long* day, int days, char* out, size_t size);

// The local date days_ago days before now, at noon, into out.
void AS_dayOf(time_t now, int days_ago, struct tm* out);

// The label under a day's bar: the week chart (days AS_WEEK) labels
// every day with its weekday ("Mon"), the month chart every 7th from
// today with its day of the month ("9"); "" for the rest.
void AS_dayLabel(time_t now, int days, int days_ago, char* out, size_t size);

///////////////////////////////////////
// the chart

// The top of the chart's scale for the days' seconds (negatives left
// out): the smallest of 15m, 30m, 1h, 2h, 3h, 4h, 6h, 8h, 12h and 24h at
// or over the most, or else the most rounded up to a whole day. 1h when
// none were played.
long long AS_axisMax(const long long* day, int days);

// A bar's height in a plot plot_h tall scaled to axis: 0 for none (0 or
// unknown), at least 1 for any play, at most plot_h.
int AS_barHeight(long long seconds, long long axis, int plot_h);

// Day days_ago's bar in plot, for a chart of days: the days run from the
// oldest on the left to today on the right, each in an equal slot with the
// bar two thirds of it (at least 1 wide), centred, standing on the plot's
// bottom. w 0 for an empty plot, a bad day or no bar.
MosaicRect AS_bar(const MosaicRect* plot, int days, int days_ago, long long seconds, long long axis);

// The slot day days_ago's bar sits in, for its label.
MosaicRect AS_slot(const MosaicRect* plot, int days, int days_ago);

///////////////////////////////////////
// tabs and moving

enum ASTab {
	AS_TAB_WEEK,
	AS_TAB_MONTH,
	AS_TAB_GAMES,
	AS_TAB_LAUNCHES,
	AS_TABS,
};

const char* AS_tabLabel(int tab);

// 1 for the tabs that are lists.
int AS_isList(int tab);

// The tab by steps (-1 or 1) from tab, wrapping.
int AS_tabStep(int tab, int by);

// Where a step of by moves the selection in count rows: past an end it
// stops at the first or last, and from the first or last a press (fresh
// is 1) that way wraps to the other end while a held repeat stays.
int AS_step(int selected, int count, int by, int fresh);

// The first row in view, given the one before, so the list scrolls only
// as far as it takes to show selected, with fit rows in view.
int AS_window(int selected, int count, int fit, int first);

// The hints: B BACK on the left, and A PLAY on the right when can_open.
void AS_hints(int can_open, MosaicHint left[1], int* left_count, MosaicHint right[1], int* right_count);

///////////////////////////////////////
// layout

// The parts of r outside hole: up to four, or r itself when they don't
// meet. 0 for an empty r.
int AS_avoid(const MosaicRect* r, const MosaicRect* hole, MosaicRect out[4]);

// status and header together (either can be NULL or empty); w 0 for none.
MosaicRect AS_hole(const MosaicRect* status, const MosaicRect* header);

// The room for the tab bar in area's top left, PADDING in, PILL_SIZE
// tall, as wide as the room left of hole allows. w 0 when there's none.
MosaicRect AS_tabRoom(const MosaicRect* area, const MosaicRect* hole, int scale);

// How the tab bar fits in room_w: tab_w[i] is each tab's pill width and
// arrows_w the L1 and R1 labels' with their gaps. 1 when every tab fits,
// 0 when only the selected one shows between the labels.
int AS_tabsFit(const int* tab_w, int count, int arrows_w, int room_w);

// The room for a page in area: under the tab bar (PADDING, PILL_SIZE
// and a PADDING) and hole, above the hint pills, PADDING in at the sides.
// w 0 when there's none.
MosaicRect AS_room(const MosaicRect* area, const MosaicRect* hole, int scale);

// The chart's plot in room: under a caption caption_h tall and a
// PADDING, right of the scale's labels axis_w wide and half a PADDING,
// over the day labels label_h tall and half a PADDING. w 0 when it
// doesn't fit.
MosaicRect AS_plot(const MosaicRect* room, int scale, int caption_h, int axis_w, int label_h);

#endif
