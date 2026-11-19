// Activity stats, without the host: the games ranked by play time and by
// sessions, the all-time totals, time per system and play by hour of day,
// the 25 most played, the chart's scale and bars, the durations and
// captions shown, the tabs, where the d-pad moves in a list, the hints and
// the layout. No SDL, so it's tested on the host.

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
// the hours of the day the All time tab's chart shows
#define AS_HOURS 24
// the games the Most Played folder and set hold
#define AS_TOP 25

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until AS_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct ASWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} ASWords;
void AS_setWords(const ASWords* words);

// text in those words.
const char* AS_word(const char* text);

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
// all time

// The totals the All time tab shows: the time, sessions and games from the
// games, and the longest session and play by hour of day from the
// sessions.
typedef struct ASAllTime {
	long long seconds;
	long long sessions;
	int games; // with any session
	long long longest; // the longest session's seconds, 0 for none
	char longest_name[AS_NAME_MAX];
	long long hours[AS_HOURS]; // seconds played in each local hour of the day
} ASAllTime;

// Starts all from the games: their time and sessions (as AS_totals), how
// many have a session, and no sessions yet.
void AS_allTimeStart(ASAllTime* all, const ASGame* games, int count);

// Adds a session that started at start (a Unix time) and ran seconds, of
// the game named name: the longest, the first of equals, and its time
// spread over the local hours it ran in. A negative or 0 seconds adds
// nothing.
void AS_allTimeAdd(ASAllTime* all, long long start, long long seconds, const char* name);

// Spreads seconds from start over the local hours of the day it runs in,
// into hours; whole days add a day's worth to every hour at once.
void AS_addHours(long long hours[AS_HOURS], long long start, long long seconds);

// The hour of the day with the most play, the first of equals, or -1 when
// none has any.
int AS_busiestHour(const long long hours[AS_HOURS]);

// The All time tab's hour caption: "Most active hour: 21:00", or "No
// play by hour yet".
void AS_hourCaption(const long long hours[AS_HOURS], char* out, size_t size);

// The All time tab's captions: "12h 5m · 40 sessions · 9 games" and
// "Longest session: 2h (Zelda)", or "" when there's no session.
void AS_allTimeCaption(const ASAllTime* all, char* out, size_t size);
void AS_longestCaption(const ASAllTime* all, char* out, size_t size);

// The system folder a game's full path is in, under roms (the card's Roms
// folder): "GBA (MGBA)" for <roms>/GBA (MGBA)/Zelda.gba, or "" for an
// empty path or one outside roms.
void AS_systemFolder(const char* roms, const char* path, char* out, size_t size);

// What a list shows for a system folder: its name without the " (TAG)"
// stock hides, "GBA" for "GBA (MGBA)", or "Other" for "".
void AS_systemLabel(const char* folder, char* out, size_t size);

// One system's time, for the All time tab and the export.
typedef struct ASSystem {
	char label[AS_NAME_MAX]; // as AS_systemLabel shows it
	long long seconds;
	int games;
} ASSystem;

// Sums the games' time by system, as AS_systemLabel names them (so two
// folders of one system add up, and orphans and games outside roms are
// Other), into out, most played first, then by label. out has room for
// count + 1. Returns how many systems.
int AS_systems(const ASGame* games, int count, const char* roms, ASSystem* out);

// The most played of the games keep says to keep (keep[i] non-zero for
// games[i]), up to max of them, as indexes into games in top, ranked as
// AS_rankPlayed. Returns how many. order has room for count.
int AS_pickTop(const ASGame* games, int count, const unsigned char* keep, int max, int* order, int* top);

// Sorts paths bytewise, and finds path in them, for the paths a filter
// hides. AS_pathIn returns 1 when it's there.
void AS_sortPaths(const char** paths, int count);
int AS_pathIn(const char* const* sorted, int count, const char* path);

// The path a reset of game forgets with play_forget: its full path, or
// NULL for an orphan, which only a reset of everything forgets.
const char* AS_resetPath(const ASGame* game);

// The confirms before a reset: of one game ("Reset Zelda's play time?
// ...") and of everything, each saying it can't be undone and that stock's
// Game Tracker loses it too.
void AS_resetQuestion(const char* name, char* out, size_t size);
void AS_resetAllQuestion(char* out, size_t size);

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
	AS_TAB_ALL,
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

// What the hints offer, as AS_HINT_ bits.
enum {
	AS_HINT_OPEN = 1 << 0,      // A PLAY, for a game on the card
	AS_HINT_EXPORT = 1 << 1,    // Y EXPORT
	AS_HINT_RESET = 1 << 2,     // X RESET, for the selected game
	AS_HINT_RESET_ALL = 1 << 3, // X RESET ALL
};

// The hints, as offer says: B BACK and Y EXPORT on the left, and X RESET
// or RESET ALL and A PLAY on the right, in that order.
void AS_hints(int offer, MosaicHint left[2], int* left_count, MosaicHint right[2], int* right_count);

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

// The All time tab's two halves of room, under its captions (two lines
// caption_h tall and a PADDING): systems on the left and the hour chart on
// the right, PADDING apart. w 0 for both when there's no room.
void AS_allTimeRooms(const MosaicRect* room, int scale, int caption_h, MosaicRect* systems, MosaicRect* hours);

// The hour chart's plot in room: under a caption caption_h tall and half
// a PADDING, over hour labels label_h tall and half a PADDING. w 0 when it
// doesn't fit.
MosaicRect AS_hourPlot(const MosaicRect* room, int scale, int caption_h, int label_h);

// The chart's plot in room: under a caption caption_h tall and a
// PADDING, right of the scale's labels axis_w wide and half a PADDING,
// over the day labels label_h tall and half a PADDING. w 0 when it
// doesn't fit.
MosaicRect AS_plot(const MosaicRect* room, int scale, int caption_h, int axis_w, int label_h);

#endif
