#ifndef SCREENSAVER_MODEL_H
#define SCREENSAVER_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// Where the text the model builds gets its words: tr(text) for fixed
// English and tr_plural(singular, plural, n) for a count's form, each
// keeping the English's printf conversions (the plugin's helpers over
// host->tr and host->tr_plural). Until SS_setWords is called, or after
// it's called with NULL, the text is English.
typedef struct SSWords {
	const char* (*tr)(const char* text);
	const char* (*tr_plural)(const char* singular, const char* plural, unsigned long n);
} SSWords;
void SS_setWords(const SSWords* words);

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
	SS_SOURCE_VIDEOS,      // as games, playing the games' clips through video-previews
	SS_SOURCE_RANDOM,      // a random game from the card index each time
};

// "games", "screenshots", "folder", "videos" or "random", or games for
// anything else.
int SS_parseSource(const char* setting);
const char* SS_sourceName(int source);

enum SsArt {
	SS_ART_BOX,        // box art, else the thumbnail
	SS_ART_SCREENSHOT, // screenshot art
};

// "box" or "screenshot", or box for anything else.
int SS_parseArt(const char* setting);

enum SsStyle {
	SS_STYLE_IMAGES, // the source's images, full screen
	SS_STYLE_LOGO,   // the game's wheel logo, or its name, bouncing on black
	SS_STYLE_CLOCK,  // an analog clock face
};

// "images", "logo" or "clock", or images for anything else.
int SS_parseStyle(const char* setting);
const char* SS_styleName(int style);

enum SsFace {
	SS_FACE_FULL, // the face alone on black, drifting
	SS_FACE_OVER, // the face over the source's dimmed images
};

// "full" or "over", or full for anything else.
int SS_parseFace(const char* setting);
const char* SS_faceName(int face);

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

// The ambient screen's wants: 1 when the launcher should open it, with the
// idle setting on (setting_ms above 0) and idle_ms, the launcher's idle
// time, at least the setting. The launcher asks only over the game list,
// home or a later ambient screen, so the user's order of ambient screens
// decides whether the charging screen comes first.
int SS_wants(unsigned long idle_ms, unsigned long setting_ms);

// 1 when the plugin id is loaded, as host->plugin_loaded says, or 0 on a
// launcher without it.
int SS_pluginLoaded(const MosaicHost* host, const char* id);

// Which buttons the screen takes while it shows and until they're let go
// after it closes, so the press that closes it doesn't also act on the
// screen under it.
typedef struct SsFilter {
	int swallowing; // a press took the screen's buttons and they aren't all up yet
} SsFilter;

// One frame's buttons, as MOSAIC_BUTTON_BIT masks. keep is what's never
// taken (the power button, so sleep still works), and toggle the button
// that plays or pauses music in place of closing the screen (X while music
// has a track), or 0. Sets *close to 1 when a press should close the open
// screen, or *toggled to 1 when it was toggle alone, and returns the
// buttons to take.
unsigned int SsFilter_frame(SsFilter* f, int open, unsigned int pressed, unsigned int held, unsigned int keep,
	unsigned int toggle, int* close, int* toggled);

// 1 when a frame's press, leaving out keep, is button alone, as A alone
// launches the random source's game as it closes the screen.
int SS_pressedAlone(unsigned int pressed, unsigned int keep, unsigned int button);

///////////////////////////////////////
// The images

#define SS_POOL_MAX 256  // images cycled through, at most
#define SS_PATH_MAX 512
#define SS_SCAN_MAX 4096 // names looked at in a folder, at most

#define SS_NAME_MAX 128

typedef struct SsPool {
	char paths[SS_POOL_MAX][SS_PATH_MAX];
	char games[SS_POOL_MAX][SS_PATH_MAX]; // the game an image is art of, or ""
	char names[SS_POOL_MAX][SS_NAME_MAX]; // that game's name, or ""
	unsigned char text[SS_POOL_MAX]; // 1 when it shows the name in place of an image
	unsigned char bad[SS_POOL_MAX]; // 1 when it couldn't be loaded, so it's skipped
	int count;
} SsPool;

void SS_poolClear(SsPool* pool);

// Adds path, unless it's empty, too long, already there or the pool is
// full. Returns 1 when added.
int SS_poolAdd(SsPool* pool, const char* path);
// The same, for a game's art: game is the game's full path (NULL or "" for
// none), kept beside the image for its clip. Too long a game isn't added.
int SS_poolAddGame(SsPool* pool, const char* path, const char* game);
// The same, with the game's name (NULL or "" for none, cut to fit), and
// text 1 when the name shows in place of an image: path is then the
// game's own, so it's still one entry per game.
int SS_poolAddNamed(SsPool* pool, const char* path, const char* game, const char* name, int text);
// Puts an entry at index, below SS_POOL_MAX, whatever was there, and
// grows count to cover it: the random source's two places, one showing
// and one next. Returns 1, or 0 for a bad index or too long a path.
int SS_poolPut(SsPool* pool, int index, const char* path, const char* game, const char* name, int text);

// The index of path, or -1.
int SS_poolFind(const SsPool* pool, const char* path);

// The file name part of a path.
const char* SS_baseName(const char* path);

// 1 for a name that could be an image: a .png, .jpg, .jpeg or .bmp (any
// case) with something before the extension, not hidden (a leading dot,
// as macOS's ._ files have) and with no slash.
int SS_isImage(const char* name);

// The name of a game for when nothing better is known: its file name
// without its last extension, or a folder's whole name (is_dir 1).
void SS_gameName(const char* path, int is_dir, char* out, size_t size);

#define SS_MEDIA_EXTS 3 // .png, .jpg and .jpeg

// <dir>/.media/<kind>/<name><ext> for a game in dir, its name as
// SS_gameName has it and ext 0 to SS_MEDIA_EXTS - 1, such as kind "wheel"
// for its logo, or <dir>/.media/<name><ext> for kind "", stock's art. Returns 0, or -1 for no name, a bad ext or a path that
// doesn't fit (writing "").
int SS_mediaPath(const char* game, int is_dir, const char* kind, int ext, char* out, size_t size);

// Adds dir's images to pool in name order, regular files only (a link to
// one counts), up to the pool's room. Returns how many were added, or -1
// when dir can't be opened.
int SS_scanFolder(const char* dir, SsPool* pool);

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
// The random source

// A random source: a number from 0 to below - 1.
typedef unsigned long (*SsRandom)(unsigned long below, void* data);

// How well index suits: 0 not at all, 1 it can show, 2 it has all the
// style shows, such as art.
typedef int (*SsAccept)(int index, void* data);

// Picks an index below count: up to tries random ones, the first that
// accept gives 2, else the first it gave 1; when none did, the first it
// takes going round from a random index. avoid (the game showing, or -1)
// is left out while there's another. -1 when accept takes none.
int SS_randomPick(int count, int avoid, int tries, SsRandom random, void* random_data, SsAccept accept,
	void* accept_data);

// A small generator (xorshift64): state is the seed, then kept between
// calls. A zero state is replaced with a fixed one.
unsigned long long SS_xorshift(unsigned long long* state);

///////////////////////////////////////
// Bouncing and drifting

// Something moving inside a box and turning at its edges, as a DVD logo
// does: its top left, in thousandths of a pixel from the box's, and which
// way it goes on each axis (1 or -1).
typedef struct SsBounce {
	long x, y;
	int dx, dy;
} SsBounce;

void SsBounce_init(SsBounce* b, int x, int y, int dx, int dy);

// Moves it speed pixels a second for ms, with its top left kept from 0 to
// range_w and 0 to range_h (the box less its size; 0 or less holds it at
// 0), turning at each edge it reaches. A range smaller than before moves
// it inside first. Returns how many edges it turned at.
int SsBounce_step(SsBounce* b, unsigned long ms, int speed, int range_w, int range_h);

// Its top left in whole pixels.
int SsBounce_x(const SsBounce* b);
int SsBounce_y(const SsBounce* b);

///////////////////////////////////////
// The analog clock

// The hands' angles in degrees, clockwise from 12: the hour hand moves
// on through the hour and the minute hand through the minute, each by
// whole degrees, and the second hand by the second.
void SS_handAngles(int hour, int minute, int second, int* hour_deg, int* minute_deg, int* second_deg);

// Where a hand of length ends at degrees from 12, from the centre, with y
// going down.
void SS_handPoint(int degrees, int length, int* dx, int* dy);

// The face's radius in a w by h area: two fifths of the shorter side, so
// a full face has room to drift, or nine twentieths over the images.
int SS_faceRadius(int w, int h, int face);

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
