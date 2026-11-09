#ifndef SCREENSAVER_MODEL_H
#define SCREENSAVER_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// What the screensaver decides, apart from the host, the clock and the
// screen: its settings, when it opens, the buttons it takes, which images
// it cycles through and how one changes to the next. No SDL; it reads
// folders and files with libc only, so it's tested on the host.

///////////////////////////////////////
// Settings

#define SS_IDLE_DEFAULT_MS 30000UL     // as in plugin.txt
#define SS_INTERVAL_DEFAULT_MS 10000UL // as in plugin.txt
#define SS_MAX_S 3600
#define SS_CHANGE_MAX_MS 1000UL // the longest a fade or slide takes

// The idle setting in ms: whole seconds from 1 to SS_MAX_S, such as "30"
// or " 60 ", or 0 for "off" or "0". Anything else is the default.
unsigned long SS_parseIdle(const char* setting);

// The interval setting in ms: whole seconds from 1 to SS_MAX_S. Anything
// else, "off" and 0 included, is the default.
unsigned long SS_parseInterval(const char* setting);

// How long the change from one image to the next takes for an interval:
// half of it, up to SS_CHANGE_MAX_MS.
unsigned long SS_changeMs(unsigned long interval_ms);

enum SsSource {
	SS_SOURCE_GAMES,       // recent and favorite games' art
	SS_SOURCE_SCREENSHOTS, // the card's Screenshots folder
	SS_SOURCE_FOLDER,      // the folder setting's folder
};

// "games", "screenshots" or "folder", or games for anything else.
int SS_parseSource(const char* setting);
const char* SS_sourceName(int source);

enum SsArt {
	SS_ART_BOX,        // box art, else the thumbnail
	SS_ART_SCREENSHOT, // screenshot art
};

// "box" or "screenshot", or box for anything else.
int SS_parseArt(const char* setting);

enum SsEffect {
	SS_EFFECT_FADE,
	SS_EFFECT_SLIDE,
};

// "fade" or "slide", or fade for anything else.
int SS_parseEffect(const char* setting);

enum SsClock {
	SS_CLOCK_OFF,
	SS_CLOCK_24H,
	SS_CLOCK_12H,
};

// "off", "24h" or "12h", or off for anything else.
int SS_parseClock(const char* setting);
const char* SS_clockName(int mode);

// "09:05" in 24h, "9:05 AM" in 12h, or "" with the clock off or a bad time.
void SS_formatClock(int hour, int minute, int mode, char* out, size_t size);

// The folder setting's folder on the card: sdcard plus the setting, with
// leading slashes and trailing ones dropped. "Screensaver" for an empty
// setting. Returns 0, or -1 for a setting with a "." or ".." part, a
// backslash or one that doesn't fit.
int SS_folderPath(const char* sdcard, const char* setting, char* out, size_t size);

///////////////////////////////////////
// When it opens and closes

typedef struct SsIdle {
	unsigned long idle_ms;    // how long idle on the list before it opens, 0 for never
	unsigned long last_input; // when a button was last down, or it last couldn't open
	int started;              // last_input has been set
} SsIdle;

void SsIdle_init(SsIdle* idle, unsigned long idle_ms);

// Something happened that isn't idling on the list: a button was down, the
// device woke, another screen showed or the screen just closed.
void SsIdle_reset(SsIdle* idle, unsigned long now);

// 1 when the screen should open now: it's on (idle_ms above 0), the list
// or home has been showing (on_list) and not given up to the charging
// screen (yield 0), and nothing has happened for idle_ms. The first call
// starts the count, and so does any call off them or yielding.
int SsIdle_due(SsIdle* idle, unsigned long now, int on_list, int yield);

// Where the screen opens over: 1 for current, as current_screen returns it,
// being the game list or home, the home slot's screen as home_screen
// returns it (NULL without one). The quick menu, the game switcher and other
// plugins' screens are what the user was looking at.
int SS_idleScreen(const char* current, const char* home);

// 1 when charging-screen's screen would show in its place: charging-screen
// is on (plugin_on), its idle setting isn't off, and the device charges
// (charging 1, as is_charging returns it).
int SS_chargingWins(int plugin_on, const char* charging_idle, int charging);

// 1 when plugins.txt's text turns the plugin id on (eg. "charging-screen=on"),
// the last line for it winning, as the loader reads it.
int SS_pluginOn(const char* plugins_txt, const char* id);

// Parses "<level> <charging>", such as "73 1", as charging-screen's
// test-only battery file holds it. Returns 1, or 0 when it isn't that.
int SS_parseFake(const char* text, int* level, int* charging);

// Which buttons the screen takes while it shows and until they're let go
// after it closes, so the press that closes it doesn't also act on the
// screen under it.
typedef struct SsFilter {
	int swallowing; // a press closed the screen and its buttons aren't all up yet
} SsFilter;

// One frame's buttons, as MOSAIC_BUTTON_BIT masks. keep is what's never
// taken (the power button, so sleep still works). Sets *close to 1 when a
// press should close the open screen, and returns the buttons to take.
unsigned int SsFilter_frame(SsFilter* f, int open, unsigned int pressed, unsigned int held, unsigned int keep,
	int* close);

///////////////////////////////////////
// The images

#define SS_POOL_MAX 256  // images cycled through, at most
#define SS_PATH_MAX 512
#define SS_SCAN_MAX 4096 // names looked at in a folder, at most

typedef struct SsPool {
	char paths[SS_POOL_MAX][SS_PATH_MAX];
	unsigned char bad[SS_POOL_MAX]; // 1 when it couldn't be loaded, so it's skipped
	int count;
} SsPool;

void SS_poolClear(SsPool* pool);

// Adds path, unless it's empty, too long, already there or the pool is
// full. Returns 1 when added.
int SS_poolAdd(SsPool* pool, const char* path);

// The index of path, or -1.
int SS_poolFind(const SsPool* pool, const char* path);

// The file name part of a path.
const char* SS_baseName(const char* path);

// 1 for a name that could be an image: a .png, .jpg, .jpeg or .bmp (any
// case) with something before the extension, not hidden (a leading dot,
// as macOS's ._ files have) and with no slash.
int SS_isImage(const char* name);

// Adds dir's images to pool in name order, regular files only (a link to
// one counts), up to the pool's room. Returns how many were added, or -1
// when dir can't be opened.
int SS_scanFolder(const char* dir, SsPool* pool);

// The next line of text at *cursor into out, without its line ending,
// skipping empty lines; moves *cursor past it. Returns 0 at the end.
int SS_nextLine(const char** cursor, char* out, size_t size);

// The collection favorites keeps its list in, from its "collection"
// setting: the value, or "Favorites" when it's empty or has a "/".
const char* SS_collectionName(const char* setting);

///////////////////////////////////////
// Cycling

typedef struct SsCycle {
	int current;              // the index shown, -1 for none
	int next;                 // the index coming next, -1 for none
	unsigned long interval_ms; // how long each image shows
	unsigned long change_ms;   // how long a change takes
	unsigned long shown_at;    // when current was first shown whole
	unsigned long change_at;   // when the change to next began
	int changing;              // 1 while current changes to next
} SsCycle;

void SsCycle_init(SsCycle* c, unsigned long interval_ms, unsigned long change_ms);

// The index after from that isn't bad, going round, or -1 when there's
// none but from (or none at all with from -1 and every one bad).
int SS_following(const SsPool* pool, int from);

// Where to start: the image after the one last shown (last, a path, or
// NULL) when it's still in the pool, else the first good one; -1 for none.
int SS_startIndex(const SsPool* pool, const char* last);

enum SsStep {
	SS_STEP_NONE,   // nothing to redraw
	SS_STEP_START,  // a change began
	SS_STEP_MOVING, // a change goes on
	SS_STEP_DONE,   // the change ended: current is what was next
};

// Moves the cycle on to now. next_ready is 1 when the next image is ready
// to draw. A change starts once current has shown for interval_ms and next
// is ready, and ends change_ms later. Returns an enum SsStep.
int SsCycle_step(SsCycle* c, unsigned long now, int next_ready);

// How far a change has gone, from 0 to 1000, eased in and out.
int SsCycle_progress(const SsCycle* c, unsigned long now);

///////////////////////////////////////
// Drawing

// The alphas of the outgoing image (*out) and the incoming one (*in) at
// progress p (0 to 1000).
void SS_fadeAlphas(int progress, int* out, int* in);

// How far a slide has moved, in pixels of a width wide screen, at
// progress p (0 to 1000).
int SS_slideOffset(int width, int progress);

// A src_w by src_h image fitted inside box keeping its shape, centred; w 0
// for an empty size.
MosaicRect SS_fit(int src_w, int src_h, const MosaicRect* box);

// The part of a surface drawn whole at dst that lands inside area: *src
// is that part of the surface and *to where it goes. Returns 0 when none
// of it does.
int SS_clip(const MosaicRect* dst, const MosaicRect* area, MosaicRect* src, MosaicRect* to);

#endif
