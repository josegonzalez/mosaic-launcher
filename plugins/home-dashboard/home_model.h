// Home dashboard, without the host: the home modes and their tabs, moving
// through a tab, the text the dashboard shows (play time, last played,
// RetroAchievements progress), which games are most played, which
// folders hold Pico-8 carts, reading favorites' collection, and where
// things go on the screen. No SDL and no file access, so it's tested on
// the host.

#ifndef HOME_MODEL_H
#define HOME_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// The home layouts the "mode" setting picks.
enum HDMode {
	HD_MODE_DASHBOARD,        // tabs: home (continue card and top shelf), pinned, stats
	HD_MODE_MINUI,            // a plain list of recent games, then Games
	HD_MODE_RETROACHIEVEMENTS, // recent and pinned games with their achievements
	HD_MODE_PICO8,            // the card's Pico-8 carts as a grid
};

// The dashboard's tabs.
enum HDTab {
	HD_TAB_HOME,
	HD_TAB_PINNED,
	HD_TAB_STATS,
};

#define HD_TABS_MAX 3

// The mode a setting names: "dashboard", "minui", "retroachievements" or
// "pico8", or HD_MODE_DASHBOARD for anything else.
int HD_modeOf(const char* name);
const char* HD_modeName(int mode);

// The dashboard's tabs in order, written to out: home always, pinned when
// it's on and favorites is loaded, stats when it's on. Returns how many.
int HD_tabs(int pinned_on, int favorites_loaded, int stats_on, int* out);
// A tab's label, as drawn: "Home", "Pinned" or "Stats".
const char* HD_tabLabel(int tab);
// Where tab is in tabs, or 0 when it isn't there.
int HD_tabIndex(const int* tabs, int count, int tab);
// The tab index after moving by delta (-1 or 1), wrapping around.
int HD_tabStep(int index, int count, int delta);

// The home tab's focus: HD_FOCUS_CONTINUE for the continue card, or a top
// shelf card's index from 0.
#define HD_FOCUS_CONTINUE -1

// Where the home tab's focus goes for a d-pad button (MOSAIC_BUTTON_UP,
// DOWN, LEFT or RIGHT): down from the card to the shelf's first card, up
// back to the card, and left and right along the shelf, stopping at its
// ends (left from the first card goes to the continue card). With no
// shelf, the focus stays on the card.
int HD_homeMove(int focus, int shelf_count, int button);

// A selection in a list of count after moving by delta, stopping at the
// ends, or wrapping around when wrap is set and the move was one step.
// -1 for an empty list.
int HD_listMove(int selected, int count, int delta, int wrap);
// The first row in view so selected shows in rows rows, scrolling only as
// far as it takes from first.
int HD_window(int selected, int count, int rows, int first);

// Play time as text: "2h 15m", "45m", "<1m" under a minute, "" for 0 or
// less.
void HD_duration(long long seconds, char* out, size_t size);
// When something was last played, by local days from now: "today",
// "yesterday", "3 days ago", "" for 0 or less (never or unknown).
void HD_lastPlayed(long long when, long long now, char* out, size_t size);
// Local calendar days from when to now: 0 the same day, 0 when when is
// after now.
int HD_daysBetween(long long when, long long now);
// The continue card's detail line from what the play history says: "2h
// 15m played · yesterday", either half alone, or "" when it says nothing.
void HD_playLine(long long time, long long last, long long now, char* out, size_t size);
// RetroAchievements progress: "12/40 · 150/400 points", or "12/40" when
// there are no points. "" with no total.
void HD_achievementLine(int unlocked, int total, int points_unlocked, int points, char* out, size_t size);

// The indices of the max games with the most play time, most first, ties
// in index order, leaving out times of 0 or less. Returns how many.
int HD_mostPlayed(const long long* times, int count, int* out, int max);

// A system folder's tag, the text inside its name's last "(...)", eg. "P8"
// for "Pico-8 (P8)", written to out; 0 (writing "") when it has none.
int HD_tagOf(const char* folder, char* out, size_t size);
// 1 when tag is one of the comma-separated tags, ignoring case and spaces.
int HD_tagIn(const char* tag, const char* tags);
// 1 for a Pico-8 cart's file name: .p8 or .p8.png, ignoring case, and not
// a dot file.
int HD_isPicoCart(const char* name);

// 1 when plugins.txt's text turns the plugin id on (eg. "favorites=on"),
// by its last line for id.
int HD_pluginOn(const char* plugins_txt, const char* id);
// The collection favorites keeps its list in, from its "collection"
// setting: the value, or "Favorites" when it's empty or has a "/".
const char* HD_collectionName(const char* setting);
// Reads the next line of a collection file's text from *cursor into out,
// without its line ending, skipping blank lines. Returns 1, or 0 at the
// end.
int HD_nextLine(const char** cursor, char* out, size_t size);

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

// src_w by src_h fitted inside box, keeping its shape and centred. Returns
// 0, or -1 for an empty size.
int HD_fit(int src_w, int src_h, const MosaicRect* box, MosaicRect* out);

// How many cells of size cell with gap between them fit across width,
// at least 0.
int HD_cellsFit(int width, int cell, int gap);

#endif
