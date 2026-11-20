// Home dashboard, without the host: the home modes and their tabs, the
// home tab's rows and which games each picks, moving through them, the
// text the dashboard shows (play time, time left, RetroAchievements
// progress, the clock), which games are most played, which folders hold
// Pico-8 carts, the consoles and their game counts, and where things go on
// the screen. No SDL and no file access, so it's tested on the host.

#ifndef HOME_MODEL_H
#define HOME_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until HD_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct HDWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} HDWords;
void HD_setWords(const HDWords* words);

// The home layouts the "mode" setting picks.
enum HDMode {
	HD_MODE_DASHBOARD,        // tabs: home (continue card and top shelf), pinned, stats
	HD_MODE_MINUI,            // a plain list of recent games, then Games
	HD_MODE_RETROACHIEVEMENTS, // recent and pinned games with their achievements
	HD_MODE_PICO8,            // the card's Pico-8 carts as a grid
	HD_MODE_MINIMAL,          // the continue card and the clock
	HD_MODE_CONSOLES,         // every system as a tile with its game count
};

// The dashboard's tabs.
enum HDTab {
	HD_TAB_HOME,
	HD_TAB_PINNED,
	HD_TAB_STATS,
};

#define HD_TABS_MAX 3

// The mode a setting names: "dashboard", "minui", "retroachievements",
// "pico8", "minimal" or "consoles", or HD_MODE_DASHBOARD for anything
// else.
int HD_modeOf(const char* name);
const char* HD_modeName(int mode);

// home-dashboard.setup's reply to a call (see README.md): mode=<current>,
// mode.count and each mode as mode.<i> with its label, in the caller's
// words, as mode.label.<i>, restart=1, as a new mode shows the next time
// the launcher starts, and error=mode when refused. Returns its length, or
// -1 when it doesn't fit in size.
int HD_setupReply(const char* current, int refused, char* out, size_t size);

// The dashboard's tabs in order, written to out: home always, pinned when
// it's on and favorites is loaded, stats when it's on. Returns how many.
int HD_tabs(int pinned_on, int favorites_loaded, int stats_on, int* out);
// A tab's label, as drawn: "Home", "Pinned" or "Stats".
const char* HD_tabLabel(int tab);
// Where tab is in tabs, or 0 when it isn't there.
int HD_tabIndex(const int* tabs, int count, int tab);
// The tab index after moving by delta (-1 or 1), wrapping around.
int HD_tabStep(int index, int count, int delta);

// The rows under the home tab's continue card the "rows" setting names.
enum HDRow {
	HD_ROW_CONTINUE,  // continue: the other recent games with a save state to resume
	HD_ROW_RECENT,    // recent: the games played before the last one (the top shelf)
	HD_ROW_ADDED,     // added: the games put on the card last
	HD_ROW_PLAYED,    // played: the most played games
	HD_ROW_RANDOM,    // random: games picked at random
	HD_ROW_SHORTCUTS, // shortcuts: tiles that run the actions the "shortcuts" setting names
	HD_ROW_MISSIONS,  // missions: gamification's quests running now, as cards
	HD_ROW_KINDS,
};

#define HD_ROWS_DEFAULT "continue, recent"
#define HD_SHORTCUTS_DEFAULT "search, random-game, activity-stats, system-info"

// The next name in a comma-separated list from *p, without spaces round
// it, written to out, moving *p past it. Returns 1, or 0 at the end. Empty
// names are skipped.
int HD_listNext(const char** p, char* out, size_t size);

// The rows a "rows" setting names, in its order, each once, ignoring case,
// written to out; up to max. The names it doesn't know go to unknown,
// comma-separated, for the log. Returns how many.
int HD_parseRows(const char* text, int* out, int max, char* unknown, size_t size);
// A row's name in the setting ("recent"), and its heading as drawn
// ("RECENTLY PLAYED").
const char* HD_rowName(int row);
const char* HD_rowLabel(int row);

// The recent games a continue or recent row shows, by their index in the
// recents: every one after the first (which is on the continue card), and
// for continue only those available with a save state to resume. Returns
// how many, up to max.
int HD_recentPick(int row, const int* can_resume, const int* available, int count, int* out, int max);

// Keeps the max largest keys, largest first, ties in the order added, with
// each one's id beside it in ids: adds key and id to the n kept so far and
// returns how many are kept now. For the games added last, by time.
int HD_topAdd(long long* keys, int* ids, int n, int max, long long key, int id);

// A random number from *state, which it moves on (a xorshift); a state of
// 0 is taken as 1.
unsigned int HD_random(unsigned int* state);
// Picks want of a stream of ids at random, each as likely (reservoir
// sampling): seen is how many came before id. Returns how many of out are
// filled now.
int HD_reservoir(int* out, int want, int seen, int id, unsigned int* state);

// A system as the consoles mode shows it.
typedef struct HDConsole {
	char system[128]; // its folder's name, as the card index gives it
	char name[128];   // the name to show, without its tag
	char path[512];   // its full path
	int games;        // its games, leaving out what a filter hides
} HDConsole;

// A system folder's name without its last "(...)" tag and the spaces
// before it: "Game Boy" for "Game Boy (GB)".
void HD_systemName(const char* folder, char* out, size_t size);
// Adds one card index entry to the consoles, as the index lists them, each
// system folder before what's in it: a system folder with an emulator
// starts one, and a game (kind MOSAIC_INDEX_GAME) that counts (not hidden)
// adds to its system's. Returns how many consoles there are now, up to max.
int HD_consoleAdd(HDConsole* list, int count, int max, int kind, int emulator, const char* system, const char* path,
	const char* name, int counts);
// Drops the consoles with no games, keeping the order. Returns how many
// are left.
int HD_consolesKeep(HDConsole* list, int count);

// The text a tile without art shows, its name broken into at most
// max_lines lines no wider than width, as measure(text, data) gives a
// text's width: at spaces, a word too wide on a line of its own (cut when
// it's drawn), and the rest on the last line. Returns how many lines.
#define HD_LINE_MAX 128
int HD_wrap(const char* text, int width, int (*measure)(const char* text, void* data), void* data,
	char lines[][HD_LINE_MAX], int max_lines);
// 1 when each of count lines is at most width wide, as measure gives it.
int HD_linesFit(char lines[][HD_LINE_MAX], int count, int width, int (*measure)(const char* text, void* data), void* data);
// Where a tile's first line goes so lines of line_h are centred in box.
int HD_linesTop(const MosaicRect* box, int lines, int line_h);

// The home tab's focus: the continue card (row HD_FOCUS_CONTINUE), or a
// card in one of the rows under it, by their index from 0.
#define HD_FOCUS_CONTINUE -1
typedef struct HDFocus {
	int row; // HD_FOCUS_CONTINUE or a row
	int col; // the card in it
} HDFocus;

// Moves the home tab's focus for a d-pad button (MOSAIC_BUTTON_UP, DOWN,
// LEFT or RIGHT), with counts[i] cards in each of rows rows: down from the
// card to the first row's first card and on to the next row, up back the
// same way, and left and right along a row, stopping at its ends, left
// from the first row's first card going to the continue card. A row
// change keeps the column as far as the row reaches. With no rows, the
// focus stays on the card.
void HD_rowsMove(HDFocus* focus, const int* counts, int rows, int button);

// A selection in a list of count after moving by delta, stopping at the
// ends, or wrapping around when wrap is set and the move was one step.
// -1 for an empty list.
int HD_listMove(int selected, int count, int delta, int wrap);
// The first row in view so selected shows in rows rows, scrolling only as
// far as it takes from first.
int HD_window(int selected, int count, int rows, int first);

// A list that scrolls without a selection, such as the Stats tab: the
// first of count rows in view after scrolling by rows from first, with
// rows in view, never past the last page. 0 when they all fit.
int HD_scroll(int first, int by, int count, int rows);
// How many rows to draw from first, with rows in view: as many as fit,
// less any section headings (header[i] set) at the bottom, so a heading
// never shows with nothing under it.
int HD_shown(const int* header, int count, int first, int rows);

// Play time as text: "2h 15m", "45m", "<1m" under a minute, "" for 0 or
// less.
void HD_duration(long long seconds, char* out, size_t size);
// The continue card's detail line without playtime-captions: "2h 15m
// played", or "" for 0 or less.
void HD_playedLine(long long seconds, char* out, size_t size);
// The Stats tab's time left row from playtime-limit's limit.left: its
// state, limit and granted minutes and seconds left. Writes "35m of 1h
// 0m", the time left in whole minutes rounded up (as playtime-limit
// shows it) of today's limit with the time granted, and returns 1 while a
// limit is set and the history is known (state ok, warn or up), or
// returns 0 (writing "").
int HD_timeLeft(const char* state, int limit, int granted, long long left, char* out, size_t size);
// When something happened, days days ago by the local calendar: "Today",
// "Yesterday" or "3 days ago"; "Today" for less than 0, as from a clock
// that went back.
void HD_daysAgo(int days, char* out, size_t size);
// RetroAchievements progress: "12/40 · 150/400 points", or "12/40" when
// there are no points. "" with no total.
void HD_achievementLine(int unlocked, int total, int points_unlocked, int points, char* out, size_t size);

// The indices of the max games with the most play time, most first, ties
// in index order, leaving out times of 0 or less. Returns how many.
int HD_mostPlayed(const long long* times, int count, int* out, int max);

// The clock as text: "14:05" in 24h, or "2:05 PM" in 12h.
void HD_clock(int hour, int minute, int h24, char* out, size_t size);

// A system folder's tag, the text inside its name's last "(...)", eg. "P8"
// for "Pico-8 (P8)", written to out; 0 (writing "") when it has none.
int HD_tagOf(const char* folder, char* out, size_t size);
// 1 when tag is one of the comma-separated tags, ignoring case and spaces.
int HD_tagIn(const char* tag, const char* tags);
// 1 for a Pico-8 cart's file name: .p8 or .p8.png, ignoring case, and not
// a dot file.
int HD_isPicoCart(const char* name);

// 1 when the plugin id is loaded, as host->plugin_loaded says, or 0 on a
// launcher without it.
int HD_pluginLoaded(const MosaicHost* host, const char* id);

// What A does on a game: "RESUME" with a save state to resume, "PLAY" for
// a game without one, "OPEN" for a pak.
const char* HD_openLabel(int type, int can_resume);

// Where the dashboard draws, from the screen view and the hints' top.
typedef struct HDLayout {
	MosaicRect tabs;    // the tab bar, along the top left of the header
	MosaicRect content; // under it, down to the hints
	MosaicRect card;    // the home tab's continue card
	MosaicRect art;     // its art, at the card's left
	MosaicRect text;    // its text, right of the art
	MosaicRect shelf;   // the top shelf, under the card
	int cell;           // a shelf card's width and its art's height
	int cells;          // how many shelf cards fit
	int gap;            // between shelf cards
} HDLayout;

// Works out the layout. top_h is the height of the header row (the status
// pill's, or 0 with none), hints_y where the hints start, scale the
// screen's scale and name_h a shelf card's name line. Returns 0, or -1
// when there's no room.
int HD_layout(const MosaicRect* area, const MosaicRect* status, const MosaicRect* header, int top_h, int hints_y,
	int scale, int name_h, int shelf_on, HDLayout* out);

// Makes room for a heading of title_h over the shelf, as when it shows one
// of several rows: from the continue card as far as it stays card_min_h
// tall, and the rest from the shelf, whose cards shrink. Leaves layout as
// it was when the shelf's cards wouldn't fit then.
void HD_layoutTitle(HDLayout* layout, int title_h, int card_min_h, int name_h, int scale);

// The minimal mode's layout: the clock's row of clock_h at the top of the
// content and the continue card centred under it, in layout's card, art
// and text. Returns 0, or -1 when there's no room.
int HD_minimalLayout(const MosaicRect* content, int clock_h, int scale, HDLayout* out);

// A grid of square tiles in content, each with a line of name_h under it:
// two rows of them when they fit, else one. Writes the tile size, columns
// and rows in view. Returns 0, or -1 when there's no room.
int HD_grid(const MosaicRect* content, int scale, int name_h, int* cell, int* cols, int* rows);

// src_w by src_h fitted inside box, keeping its shape and centred. Returns
// 0, or -1 for an empty size.
int HD_fit(int src_w, int src_h, const MosaicRect* box, MosaicRect* out);

// How many cells of size cell with gap between them fit across width,
// at least 0.
int HD_cellsFit(int width, int cell, int gap);

// A card in the missions row: one of gamification's quests running now,
// from its quests.cards service.
#define HD_MISSIONS_MAX 8
typedef struct HDMission {
	char id[32];        // the quest's id, such as "play"
	char title[160];    // "Play for 30 minutes today"
	char progress[64];  // "12m / 30m", "Done"
	char reward[64];    // "+1 quest done"
	long long value, target;
	int done;
} HDMission;

// The cards in quests.cards's reply, read with value (as the host's
// event_value reads a key): count=<n>, then for each from 0 id.<i>,
// title.<i>, progress.<i>, value.<i>, target.<i>, done.<i> and
// reward.<i>. A card without a title is left out. Returns how many, up to
// max, 0 for a NULL or empty reply.
int HD_parseMissions(const char* reply, int (*value)(const char* payload, const char* key, char* out, size_t size),
	HDMission* out, int max);

// How many missions cards fit across width, gap apart, each at least
// HD_MISSION_MIN_W (times scale) wide, and at least 1 while width is;
// writes their width to card_w. 0 for no width.
#define HD_MISSION_MIN_W 240
int HD_missionCards(int width, int scale, int gap, int* card_w);

// Where a missions card's parts go: its title of title_h, a bar under it,
// and under that its progress at the left and its reward, reward_w wide
// (at most half), at the right, in lines of small_h; padded in from the
// card's sides and centred down it.
typedef struct HDMissionLayout {
	MosaicRect title;
	MosaicRect bar;
	MosaicRect progress;
	MosaicRect reward;
} HDMissionLayout;
// Returns 0, or -1 when they don't fit in card.
int HD_missionLayout(const MosaicRect* card, int scale, int title_h, int small_h, int reward_w, HDMissionLayout* out);

// The part of a bar w wide that value of target fills: 0 for none, w when
// done or over, and at least 1 for any progress.
int HD_barWidth(long long value, long long target, int w);

#endif
