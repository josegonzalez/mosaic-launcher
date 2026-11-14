/*
 * mosaic_plugin.h - the plugin API for mosaic-launcher.
 *
 * MIT License. Copyright (c) 2026 Jose Diaz-Gonzalez. See sdk/LICENSE.
 *
 * This header is the only thing a plugin includes. It doesn't depend on
 * NextUI's headers, so plugins can use any license.
 *
 * A plugin is a shared object exporting one function:
 *
 *     const MosaicPlugin* mosaic_plugin_get(const MosaicHost* host);
 *
 * It returns a description of the plugin with whichever hooks it
 * implements; every hook is optional. The launcher calls into the plugin
 * only on its main thread, and plugins call the host only from it too,
 * except audio_queue and audio_queued, which are safe from any thread.
 *
 * Both structs begin with their own size, so a launcher and a plugin built
 * against different versions of this header can tell which fields the
 * other has. Fields are only ever added at the end.
 */

#ifndef MOSAIC_PLUGIN_H
#define MOSAIC_PLUGIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOSAIC_API_VERSION 1

///////////////////////////////////////
// Things the launcher owns. Plugins only hold pointers to them.

typedef struct MosaicEntry MosaicEntry; // one row in a list: a folder, game, pak or toggle
typedef struct MosaicList MosaicList;   // an ordered list of entries

enum MosaicEntryType {
	MOSAIC_ENTRY_DIR,
	MOSAIC_ENTRY_PAK,
	MOSAIC_ENTRY_ROM,
	MOSAIC_ENTRY_TOGGLE,
};

// Screens a binding can target.
enum MosaicScreen {
	MOSAIC_SCREEN_GAMELIST,
	MOSAIC_SCREEN_SWITCHER,
	MOSAIC_SCREEN_QUICKMENU,
};

// Buttons a binding can use.
enum MosaicButton {
	MOSAIC_BUTTON_A,
	MOSAIC_BUTTON_B,
	MOSAIC_BUTTON_X,
	MOSAIC_BUTTON_Y,
	MOSAIC_BUTTON_L1,
	MOSAIC_BUTTON_R1,
	MOSAIC_BUTTON_L2,
	MOSAIC_BUTTON_R2,
	MOSAIC_BUTTON_START,
	MOSAIC_BUTTON_SELECT,
	// The d-pad, which only layout_input and input_filter see; bindings
	// can't take it.
	MOSAIC_BUTTON_UP,
	MOSAIC_BUTTON_DOWN,
	MOSAIC_BUTTON_LEFT,
	MOSAIC_BUTTON_RIGHT,
	// Added with input_filter, which is the only hook that sees them;
	// bindings, overrides and layout_input can't take them. MENU and the
	// power button are the launcher's: the quick menu, brightness and sleep.
	// FN1 to FN3 are the buttons stock's Settings lets the user assign (L3
	// and R3 on the Brick, L4, R4 and HOME on the Brick Pro, HOME on the
	// tg5050); a device without one never presses it. A plugin binds them
	// as Settings does with fn_bind.
	MOSAIC_BUTTON_MENU,
	MOSAIC_BUTTON_POWER,
	MOSAIC_BUTTON_FN1,
	MOSAIC_BUTTON_FN2,
	MOSAIC_BUTTON_FN3,
};

// A button's bit in a MosaicInput mask.
#define MOSAIC_BUTTON_BIT(button) (1u << (button))

enum MosaicTrigger {
	MOSAIC_TRIGGER_PRESS,
	MOSAIC_TRIGGER_RELEASE,
	// On the press and again while held, at stock's pace for the d-pad and
	// letter jumps. Only override_button takes it, on a launcher with
	// host->override_repeat.
	MOSAIC_TRIGGER_REPEAT,
};

// Images art_path can supply, and art_find and art_get look up.
enum MosaicArtKind {
	MOSAIC_ART_THUMBNAIL, // a game's art beside the list
	MOSAIC_ART_FOLDER_BG, // the background while a folder is selected
	MOSAIC_ART_LIST_BG,   // the background while a game in a folder is selected
	// Added with art_get. The launcher has no place of its own for these,
	// so only plugins supply them; the built-in list shows the first of a
	// box, 3D box or screenshot when a game has no thumbnail.
	MOSAIC_ART_BOX,        // the front of a game's box
	MOSAIC_ART_BOX_3D,     // its box, drawn in 3D
	MOSAIC_ART_LOGO,       // its wheel or logo, usually on a clear background
	MOSAIC_ART_SCREENSHOT, // a screenshot or title screen
	MOSAIC_ART_BACKDROP,   // wide art to draw behind the list, such as fan art
};

// What art_get and art_load return.
enum MosaicArtState {
	MOSAIC_ART_MISSING = -1, // no image: none found, or it can't be decoded
	MOSAIC_ART_LOADING = 0,  // being decoded off the main thread
	MOSAIC_ART_READY = 1,    // the image is in *out
};

// The quick menu's rows a plugin tile can go in.
enum MosaicQuickRow {
	MOSAIC_QUICK_TOP,    // the large labelled tiles: Recents, Games, Tools
	MOSAIC_QUICK_BOTTOM, // the small icons: Settings, Wifi, Sleep, Power off
};

// What pre_launch decides. MOSAIC_LAUNCH_REPLACE runs the command set with
// host->replace_launch in place of the launch; a launcher older than that
// field launches as usual.
enum MosaicLaunchResult {
	MOSAIC_LAUNCH_CONTINUE,
	MOSAIC_LAUNCH_CANCEL,
	MOSAIC_LAUNCH_REPLACE,
};

// Stock NextUI settings stock_toggle_get and stock_toggle_set take, with
// the row each is in stock's Settings app. Any other name is refused.
#define MOSAIC_TOGGLE_NOTIFY_MANUAL_SAVE "notify_manual_save"     // notifications: Save states
#define MOSAIC_TOGGLE_NOTIFY_LOAD "notify_load"                   // notifications: Load states
#define MOSAIC_TOGGLE_NOTIFY_SCREENSHOT "notify_screenshot"       // notifications: Screenshots
#define MOSAIC_TOGGLE_NOTIFY_ADJUSTMENTS "notify_adjustments"     // notifications: Vol / Display Adjustments
#define MOSAIC_TOGGLE_SHOW_CLOCK "show_clock"                     // Show clock
#define MOSAIC_TOGGLE_SHOW_BATTERY_PERCENT "show_battery_percent" // Show battery percentage
#define MOSAIC_TOGGLE_HAPTICS "haptics"                           // Haptic feedback

// Stock NextUI's theme colours theme_color takes, numbered and named as in
// stock's Settings app.
enum MosaicThemeColor {
	MOSAIC_COLOR_MAIN = 1,               // Main Color
	MOSAIC_COLOR_ACCENT = 2,             // Primary Accent Color
	MOSAIC_COLOR_ACCENT2 = 3,            // Secondary Accent Color
	MOSAIC_COLOR_LIST_TEXT = 4,          // List Text
	MOSAIC_COLOR_LIST_TEXT_SELECTED = 5, // List Text Selected
	MOSAIC_COLOR_HINT = 6,               // Hint info Color
	MOSAIC_COLOR_BACKGROUND = 7,         // Background Color
};

// Drawing. A surface is something to draw on or draw from: one the plugin
// made with surface_new, image_load or surface_scale, or one a render hook
// hands it, such as the screen. Plugins never see its pixels directly.
typedef struct MosaicSurface MosaicSurface;

// In a surface's pixels, from its top left.
typedef struct MosaicRect {
	int x, y, w, h;
} MosaicRect;

// a is 255 for opaque and 0 for invisible.
typedef struct MosaicColor {
	unsigned char r, g, b, a;
} MosaicColor;

// A colour from 0xRRGGBB, such as theme_color returns, at alpha a.
static inline MosaicColor mosaic_color(unsigned int rgb, unsigned char a) {
	MosaicColor color = { (unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8), (unsigned char)rgb, a };
	return color;
}

// The launcher's fonts, at the sizes stock draws them.
enum MosaicFont {
	MOSAIC_FONT_LARGE,  // list rows and menus
	MOSAIC_FONT_MEDIUM, // one-letter button labels
	MOSAIC_FONT_SMALL,  // button hints
	MOSAIC_FONT_TINY,   // longer button labels
	MOSAIC_FONT_MICRO,  // text over icons
};

// How surface_scale fits a surface into its new size.
enum MosaicFit {
	MOSAIC_FIT_STRETCH, // fills it, changing the shape
	MOSAIC_FIT_CONTAIN, // keeps the shape, centred, leaving the rest transparent
	MOSAIC_FIT_COVER,   // keeps the shape and fills it, cropping the edges
};

// The game list as the launcher has it this frame, for the layout slot's
// owner to draw and move around in. The launcher keeps the selection and
// scroll window; a layout reads them here and moves the selection with
// host->select_index. It begins with its own size, and fields are only
// added at the end.
typedef struct MosaicListView {
	size_t size;            // sizeof(MosaicListView) in the launcher
	const MosaicList* list; // the folder's entries, read with list_count and list_get
	int count;              // how many, never 0: an empty folder shows the launcher's own message
	int selected;           // the selected entry's index in list
	int start, end;         // the built-in list's window: entries start to end - 1 are in view
	int rows;               // how many rows the built-in list shows at once
	int depth;              // 1 on the root, one more for each folder open
	const char* path;       // the folder, as current_path returns it
	int letter;             // the selected entry's group in the letter index L1 and R1 jump between, from 0
	int letter_count;       // how many groups the letter index has
	int show_names;         // 0 when the folder's art stands in for names, as stock does with Show folder names at root off
	int show_art;           // 1 while Show game art is on in Settings
	const char* art;        // the selected entry's art as the built-in list shows it, when show_art is 1 and it exists, else ""
	MosaicRect area;        // where the layout draws: the screen above the button hints
	MosaicRect status;      // the status pill (clock, battery, wifi) in area's top right, already drawn; leave it clear
	// Added with header_widget: plugins' header widgets left of status, and
	// the launcher's pill for shell paks' status files, from the leftmost
	// to status, already drawn; leave it clear too. Empty (w 0) when there
	// are none.
	MosaicRect header;
	// Added with hide_name: one byte for each of the count entries, in list
	// order, 1 where a plugin's hide_name hid the entry's name and 0 where it
	// didn't, or NULL when no name in the folder is hidden. Asked once when
	// the folder is listed, so it's cheap to read every frame. A layout
	// decides how a hidden name looks, such as drawing the entry's art in
	// its place, and should still show the name where it has no art to
	// stand in for it. Check for it with MOSAIC_VIEW_HAS(view, names_hidden).
	const unsigned char* names_hidden;
} MosaicListView;

// True when the launcher's MosaicListView has this field.
#define MOSAIC_VIEW_HAS(view, field) \
	((view)->size >= offsetof(MosaicListView, field) + sizeof((view)->field))

// One row of the game list, or one cell of a layout, for decorate_row. It
// begins with its own size, and fields are only added at the end.
typedef struct MosaicRow {
	size_t size;     // sizeof(MosaicRow) in the launcher
	int index;       // the entry's index in the list
	int selected;    // 1 on the selected row
	MosaicRect rect; // the row on the screen; drawing is clipped to it, and under a layout to view->area too
	MosaicRect name; // the part of rect the entry's name takes, from its left; w is 0 when no name shows or the layout doesn't say
	// Added with hide_name: 1 when a plugin's hide_name hid the entry's
	// name, as in MosaicListView's names_hidden, else 0. On the built-in
	// list, name is then empty unless the name shows anyway for want of
	// art. Check for it with MOSAIC_ROW_HAS(row, name_hidden).
	int name_hidden;
} MosaicRow;

// True when the launcher's MosaicRow has this field.
#define MOSAIC_ROW_HAS(row, field) \
	((row)->size >= offsetof(MosaicRow, field) + sizeof((row)->field))

// Buttons this frame, as masks of MOSAIC_BUTTON_BIT, for layout_input and
// input_filter.
typedef struct MosaicInput {
	size_t size;           // sizeof(MosaicInput) in the launcher
	unsigned int pressed;  // went down this frame
	unsigned int repeated; // went down, or repeats while held, at the pace stock moves through the list
	unsigned int released; // came up this frame
	unsigned int held;     // down now
} MosaicInput;

typedef struct MosaicLaunch {
	const char* path; // the rom or pak being launched
	int type;         // MOSAIC_ENTRY_ROM or MOSAIC_ENTRY_PAK
} MosaicLaunch;

// The transitions a plugin can draw, as MosaicTransition's kind.
enum MosaicTransitionKind {
	MOSAIC_TRANSITION_PUSH,   // opening a folder: stock slides the new list in from the right
	MOSAIC_TRANSITION_POP,    // backing out of one: stock slides the old list out to the right
	MOSAIC_TRANSITION_SCREEN, // the game list to the game switcher: stock slides the switcher down over the list
	MOSAIC_TRANSITION_LAUNCH, // a game or pak starting, into black: stock shows none
};

// What transition_begin returns.
enum MosaicTransitionStyle {
	MOSAIC_STYLE_STOCK,  // stock's animation, at the user's Menu transitions setting
	MOSAIC_STYLE_OFF,    // none: the new frame shows at once
	MOSAIC_STYLE_CUSTOM, // the plugin's transition_frame draws each frame
};

// How progress moves through a transition, as MosaicTransition's ease.
enum MosaicEase {
	MOSAIC_EASE_LINEAR,
	MOSAIC_EASE_IN,     // starts slow, cubic
	MOSAIC_EASE_OUT,    // ends slow, cubic, as stock's slides
	MOSAIC_EASE_IN_OUT, // both
};

// One transition, for the transition and launch slots' owners. It begins
// with its own size, and fields are only added at the end.
typedef struct MosaicTransition {
	size_t size;                    // sizeof(MosaicTransition) in the launcher
	int kind;                       // an enum MosaicTransitionKind
	const MosaicSurface* old_frame; // what was on screen, read-only
	const MosaicSurface* new_frame; // what replaces it, read-only
	int stock_ms;                   // how long stock's runs at the user's setting, 0 where it shows none
	// transition_begin can change these two for MOSAIC_STYLE_CUSTOM
	int duration_ms; // stock_ms on entry; cut to 0 to host->transition_max_ms
	int ease;        // an enum MosaicEase, MOSAIC_EASE_OUT on entry; one the launcher doesn't know runs linear
	// Added with the selection's rects: where the game list's selection is
	// in each frame, in its pixels. Each is empty (w 0) where it isn't
	// known, such as a frame that isn't the game list, an empty folder, or
	// a layout that didn't pass the selected entry to decorate_cell, so
	// check w first. Check for them with
	// MOSAIC_TRANSITION_HAS(transition, new_pill).
	MosaicRect old_row;  // the selected row in old_frame; under a layout, the last cell decorate_cell got for it
	MosaicRect old_pill; // the selection pill the built-in list draws on old_row; empty under a layout or with names hidden
	MosaicRect new_row;  // the same in new_frame, for pushes and pops
	MosaicRect new_pill;
} MosaicTransition;

// True when the launcher's MosaicTransition has this field.
#define MOSAIC_TRANSITION_HAS(transition, field) \
	((transition)->size >= offsetof(MosaicTransition, field) + sizeof((transition)->field))

// A recent, as the game switcher shows it, for host->recent_info. The
// plugin sets size to sizeof(MosaicRecent) before asking, and the
// launcher fills in only the fields that fit in it, so fields are only
// added at the end.
typedef struct MosaicRecent {
	size_t size;       // sizeof(MosaicRecent) in the plugin
	char name[256];    // the name it was launched under, as the switcher's title shows it
	int type;          // MOSAIC_ENTRY_ROM or MOSAIC_ENTRY_PAK
	int available;     // 1 when its emulator is installed, so it can open
	int can_resume;    // 1 when it has a save state to resume: the last one minarch saved
	char preview[512]; // that state's preview image, for art_load, or "" when there's none
} MosaicRecent;

// A loaded plugin, for host->plugin_info. The plugin sets size to
// sizeof(MosaicPluginInfo) before asking, and the launcher fills in only
// the fields that fit in it, so fields are only added at the end.
typedef struct MosaicPluginInfo {
	size_t size;   // sizeof(MosaicPluginInfo) in the plugin
	char id[64];   // its id, as in its plugin.txt
	char res[600]; // its resource folder, <its folder>/res, which may not be there
	char description[256]; // its plugin.txt's description, or "" without one; an older launcher leaves it
} MosaicPluginInfo;

// A game in the play history, for host->play_game_info. The plugin sets
// size to sizeof(MosaicPlayGame) before asking, and the launcher fills in
// only the fields that fit in it, so fields are only added at the end.
typedef struct MosaicPlayGame {
	size_t size;            // sizeof(MosaicPlayGame) in the plugin
	char path[512];         // its full path on the card, as entry_path returns it (it may no longer be there), or "" for an orphan
	char name[256];         // the name the tracker keeps for it: the file's name without its extension, or "" when it has none
	long long seconds;      // played in all, as play_time returns it
	long long first_played; // when its first session started, as a Unix time
	long long last_played;  // when its latest session started, as last_played returns it
	int count;              // sessions, as play_count returns it
	int orphan;             // 1 for a game carried over from Onion, known only by its name
} MosaicPlayGame;

// Whose binding an FN button runs, for host->fn_binding.
enum MosaicFnBy {
	MOSAIC_FN_NONE,   // unassigned, so it does nothing
	MOSAIC_FN_USER,   // the user's, from stock's Settings
	MOSAIC_FN_PLUGIN, // a plugin's, from fn_bind
};

// An FN button's binding, for host->fn_binding. The plugin sets size to
// sizeof(MosaicFnBinding) before asking, and the launcher fills in only
// the fields that fit in it, so fields are only added at the end.
typedef struct MosaicFnBinding {
	size_t size;        // sizeof(MosaicFnBinding) in the plugin
	int by;             // enum MosaicFnBy
	char action[256];   // what it runs: "action:<id>" or "pak:<folder>", or "" with MOSAIC_FN_NONE
	char plugin_id[64]; // the plugin that bound it, with MOSAIC_FN_PLUGIN, else ""
} MosaicFnBinding;

// What register_action's flags say about an action.
enum MosaicActionFlag {
	// It acts on an entry, such as a game: its available and run get the
	// entry's full path, and it's refused without one.
	MOSAIC_ACTION_PATH = 1 << 0,
};

// Where an action's binding comes from, for MosaicActionBinding.
enum MosaicBindBy {
	MOSAIC_BIND_SCREEN,    // the screen's own, such as A for gamelist.open
	MOSAIC_BIND_BUTTON,    // a plugin's bind_button
	MOSAIC_BIND_OVERRIDE,  // a plugin's override_button
	MOSAIC_BIND_FN_USER,   // an FN button the user assigned in stock's Settings
	MOSAIC_BIND_FN_PLUGIN, // an FN button a plugin bound with fn_bind
};

// One button an action is on, for MosaicActionInfo.
typedef struct MosaicActionBinding {
	int by;             // enum MosaicBindBy
	int screen;         // the enum MosaicScreen it's on, or -1 for an FN button, which works wherever FN buttons do
	int button;         // enum MosaicButton
	int trigger;        // enum MosaicTrigger
	char plugin_id[64]; // the plugin that bound it, or "" for the launcher's own and the user's
	char setting[48];   // the key of that plugin's setting it follows, one its plugin.txt marks button:<action id>, or ""
} MosaicActionBinding;

#define MOSAIC_ACTION_BINDINGS 8

// A registered action, for host->action_info. The plugin sets size to
// sizeof(MosaicActionInfo) before asking, and the launcher fills in only
// the fields that fit in it, so fields are only added at the end.
typedef struct MosaicActionInfo {
	size_t size;           // sizeof(MosaicActionInfo) in the plugin
	char id[64];           // its id, such as gamelist.back or romm.flashback
	char label[128];       // its label, translated, as the game list's hints show it
	char plugin_id[64];    // the plugin that registered it, or "" for the launcher's own
	int takes_path;        // 1 when it was registered with MOSAIC_ACTION_PATH
	int binding_count;     // the buttons it's on, which can be more than bindings holds
	MosaicActionBinding bindings[MOSAIC_ACTION_BINDINGS]; // the first of them
} MosaicActionInfo;

// How button hints are drawn, as stock's Settings app offers them (its
// "Input prompt style"): the button's name as text, or an icon from one of
// three sets. host->hint_style returns the style in effect, and a theme
// can pick one with MosaicTheme's hint_style.
enum MosaicHintStyle {
	MOSAIC_HINT_STYLE_USER = 0,      // in MosaicTheme only: the user's, from Settings
	MOSAIC_HINT_STYLE_TEXT = 1,      // Text: the button's name, such as A or START
	MOSAIC_HINT_STYLE_ABXY = 2,      // ABXY: A, B, X and Y icons
	MOSAIC_HINT_STYLE_CARDINALS = 3, // Cardinals: a diamond of four dots, the button's filled
	MOSAIC_HINT_STYLE_SHAPES = 4,    // Shapes: a circle, cross, triangle and square
};

// D-pad glyphs host->draw_button and host->draw_hints take besides the
// d-pad's own buttons (MOSAIC_BUTTON_UP to MOSAIC_BUTTON_RIGHT). In the
// icon styles each is a d-pad icon of that style; in the text style they
// are words: UP, DOWN, LEFT, RIGHT, and these as U/D, L/R and DPAD.
enum MosaicGlyph {
	MOSAIC_GLYPH_UP_DOWN = 32,    // up and down
	MOSAIC_GLYPH_LEFT_RIGHT = 33, // left and right
	MOSAIC_GLYPH_DPAD = 34,       // the whole d-pad
};

// What the theme slot's owner supplies, for MosaicPlugin's theme. The
// launcher sets size to sizeof(MosaicTheme) in the launcher and every
// field to its "stock's" value before asking, so a theme sets only what it
// changes; fields are only added at the end, so check for a newer one with
// MOSAIC_THEME_HAS(theme, field) before setting it. Sizes are in stock's
// layout units, before scaling, as screen_scale says. Paths are full paths
// to regular files anywhere on the card; a symlink may point anywhere on
// the card but not off it.
//
// extra holds keys the theme passes on to other plugins, each named after
// the plugin that reads it, as <its id>.<name> (such as led-status.colour),
// which says what it means; they read them with host->theme_value. A key
// is 3 to 63 bytes of letters, digits, '_', '-' and '.', with a '.' that
// isn't its first or last byte, and a value at most
// MOSAIC_THEME_EXTRA_VALUE_MAX bytes with no control characters. The
// launcher keeps up to MOSAIC_THEME_EXTRA_KEYS keys; a line that isn't such
// a key=value, or one past them, is refused and logged, and of a key given
// twice the last line wins.
#define MOSAIC_THEME_COLORS 7
#define MOSAIC_THEME_UNSET -1
#define MOSAIC_THEME_EXTRA_MAX 4096 // extra's size, its terminator included
#define MOSAIC_THEME_EXTRA_KEYS 64
#define MOSAIC_THEME_EXTRA_VALUE_MAX 255
typedef struct MosaicTheme {
	size_t size;                       // sizeof(MosaicTheme) in the launcher
	int colors[MOSAIC_THEME_COLORS];   // color1 to color7 as stock's palettes number them (enum MosaicThemeColor less 1; colors[6] is the background), each 0xRRGGBB or MOSAIC_THEME_UNSET for the user's own
	char font[512];                    // a TrueType or OpenType font, or "" for the user's own
	int text_step;                     // points added to each of stock's text sizes, -2 to 8, or 0
	int contrast;                      // the least contrast ratio text keeps against what it's drawn on, in tenths (45 is 4.5:1), 10 to 210, or 0 for none
	int pill_size;                     // the height of a row and of a pill, 18 to 60, or 0 for stock's (30)
	int button_size;                   // a button's circle in the hints, 12 to 4 less than the pill, or 0 for stock's (20)
	int padding;                       // the screen's margins, 1 to 30, or 0 for stock's (or, with a pill_size, one worked out from it)
	char wallpaper[512];               // the background where the card has no bg.png (and no folder's .media/bg.png), or ""
	char wallpaper_list[512];          // a game folder's background where it has no .media/bglist.png and the card no bg.png, or "" for wallpaper
	char wallpaper_quick[512];         // the quick menu's background where the card has no .media/quick_<name>.png or quick.png, or ""
	char fallback_font[512];           // a TrueType or OpenType font for the characters the UI font has no glyphs for, or "" for the strings slot's owner's (see fallback_font in MosaicPlugin), else stock's font while font is set, or none
	int hint_style;                    // an enum MosaicHintStyle the launcher draws button hints in over the user's, or MOSAIC_HINT_STYLE_USER for the user's
	char extra[MOSAIC_THEME_EXTRA_MAX]; // keys for other plugins: key=value lines separated by \n, or "" for none
} MosaicTheme;

#define MOSAIC_THEME_HAS(theme, field) \
	((theme)->size >= offsetof(MosaicTheme, field) + sizeof((theme)->field))

// One of stock's colour palettes, for host->palette_info. The plugin sets
// size to sizeof(MosaicPalette) before asking, and the launcher fills in
// only the fields that fit in it, so fields are only added at the end.
typedef struct MosaicPalette {
	size_t size;                     // sizeof(MosaicPalette) in the plugin
	char name[64];                   // the name stock's Settings shows: the file's name=, else its file name without .txt and with underscores as spaces
	int colors[MOSAIC_THEME_COLORS]; // color1 to color7, each 0xRRGGBB as MosaicTheme takes them (stock's alpha dropped); a colour the file leaves out is stock's default
	int builtin;                     // 1 for one of stock's own, in .system/res/palettes, or 0 for one in the card's Palettes
	char path[512];                  // its file's full path
} MosaicPalette;

// The UI sizes ui_size returns, before scaling.
enum MosaicUISize {
	MOSAIC_UI_PILL = 1,      // a row's and a pill's height (stock's PILL_SIZE)
	MOSAIC_UI_BUTTON = 2,    // a button's circle in the hints (BUTTON_SIZE)
	MOSAIC_UI_PADDING = 3,   // the screen's margins (PADDING)
	MOSAIC_UI_ROWS = 4,      // the rows the built-in list shows (MAIN_ROW_COUNT)
	MOSAIC_UI_TEXT_STEP = 5, // points added to stock's text sizes
};

// The Wi-Fi connection, for host->wifi_connection. The plugin sets size to
// sizeof(MosaicWifi) before asking, and the launcher fills in only the
// fields that fit in it, so fields are only added at the end.
typedef struct MosaicWifi {
	size_t size;   // sizeof(MosaicWifi) in the plugin
	char ssid[64]; // the network's name as wpa_supplicant reports it
	char ip[48];   // the device's address on it, or "" before it has one
	int rssi;      // the signal in dBm (below 0), or 0 when not known
	int bars;      // 1 to 3, as stock's status pill shows the signal, or 0 when not known
	int freq_mhz;  // the channel's frequency in MHz, or 0 when not known
	int link_mbps; // the link speed in Mbit/s, or 0 when not known
} MosaicWifi;

// A game's local RetroAchievements progress, for host->achievements. The
// plugin sets size to sizeof(MosaicAchievements) before asking, and the
// launcher fills in only the fields that fit in it, so fields are only
// added at the end.
typedef struct MosaicAchievements {
	size_t size;           // sizeof(MosaicAchievements) in the plugin
	int total;             // the game's achievements (its core set and the sets minarch loads with it)
	int unlocked;          // of them, unlocked in either mode
	int unlocked_hardcore; // of them, unlocked in hardcore
	int points;            // points for all of them
	int points_unlocked;   // points for the unlocked ones
	int muted;             // of them, muted in minarch's menu
	unsigned int game_id;  // the game's RetroAchievements id, or 0 when the cache doesn't say
} MosaicAchievements;

// Where a plugin screen or an overlay draws, for MosaicScreenHooks' render
// and overlay_render. It begins with its own size, and fields are only
// added at the end.
typedef struct MosaicScreenView {
	size_t size;       // sizeof(MosaicScreenView) in the launcher
	MosaicRect area;   // the whole screen
	MosaicRect status; // the status pill (clock, battery, wifi) in the top right, already drawn; leave it clear. Empty (w 0) when none shows
	MosaicRect header; // shell paks' status pill and plugins' header widgets left of status, already drawn; leave it clear too. Empty when there are none
	const char* screen; // what's showing, as host->current_screen returns it
} MosaicScreenView;

// MosaicScreenHooks' flags.
enum MosaicScreenFlag {
	// The whole screen is the plugin's: no status pill, header widgets or
	// overlays, such as for a screensaver or a charging screen.
	MOSAIC_SCREEN_FULL = 1,
	// The home screen, in place of the root list: what the launcher opens
	// first and goes back to. Only for the home slot's owner (slots=home),
	// on one of its screens; the first it registers with the flag wins.
	MOSAIC_SCREEN_HOME = 2,
	// The game switcher, in place of the built-in one: what SELECT, an FN
	// button, gamelist.switcher, Settings' start screen and coming back
	// from a game launched from the switcher open. Only for the switcher
	// slot's owner (slots=switcher), on one of its screens other than its
	// home screen; the first it registers with the flag wins.
	MOSAIC_SCREEN_GAME_SWITCHER = 4,
	// Opens and closes at once, with no slide, such as a screen that takes
	// over from the one under it in place: video-previews' clip over the
	// screensaver.
	MOSAIC_SCREEN_INSTANT = 8,
};

// A button hint for host->draw_hints: a button and what it does there,
// such as { MOSAIC_BUTTON_B, "BACK" }. The button is any enum MosaicButton
// but the d-pad, named as stock names it (A, B, X, Y, L1, R1, L2, R2,
// START, SELECT, MENU, POWER), and FN1 to FN3 by the device's name for
// them (see fn_name), such as HOME, on a launcher with fn_name and a
// device that has them. On a launcher with draw_button it can also be the
// d-pad or an enum MosaicGlyph. The label is one line, in upper case like
// stock's.
typedef struct MosaicHint {
	int button;
	const char* label;
} MosaicHint;

// Where host->draw_hints puts a group, as stock's screens do: the left
// one has the secondary hints, such as the game switcher's "B BACK", and
// the right one the primary, such as its "Y REMOVE  A RESUME".
enum MosaicHintGroup {
	MOSAIC_HINTS_LEFT,
	MOSAIC_HINTS_RIGHT,
};

// What MosaicScreenHooks' input returns.
enum MosaicScreenResult {
	MOSAIC_SCREEN_STAY,  // keep the screen open
	MOSAIC_SCREEN_CLOSE, // close it, back to the screen it opened over
};

// A screen of a plugin's own, for host->register_screen. The plugin sets
// size to sizeof(MosaicScreenHooks); the launcher copies the fields that
// fit in it when the screen is registered, so fields are only added at
// the end. Every hook gets data. Only render is required.
typedef struct MosaicScreenHooks {
	size_t size; // sizeof(MosaicScreenHooks) in the plugin
	void* data;
	int flags;   // enum MosaicScreenFlag bits
	// When the screen opens, from inside host->open_screen: return 0 to
	// open, or anything else to stay closed, and open_screen returns -1.
	int (*enter)(void* data);
	// When it closes, however it closes: input, close_screen, an error, a
	// launch, or the launcher quitting.
	void (*exit)(void* data);
	// Each frame while it's on top, with the buttons, as layout_input gets
	// them, and MENU too. Returns an enum MosaicScreenResult, or below 0
	// for an error. Without input, B closes the screen.
	int (*input)(const MosaicInput* input, void* data);
	// Draws the screen on screen, lent for the call, on frames the launcher
	// redraws. Returns 0, or anything else for an error.
	int (*render)(MosaicSurface* screen, const MosaicScreenView* view, void* data);
	// Asked each frame while it's on top: 1 to redraw it every frame, such
	// as while something moves, or 0 to redraw only when asked.
	int (*animating)(void* data);
} MosaicScreenHooks;

///////////////////////////////////////
// What the launcher offers plugins.

typedef struct MosaicHost {
	size_t size;     // sizeof(MosaicHost) in the launcher
	int api_version; // MOSAIC_API_VERSION the launcher was built with

	const char* platform;      // eg. "tg5040"
	const char* sdcard_path;   // eg. "/mnt/SDCARD"
	const char* userdata_path; // eg. "/mnt/SDCARD/.userdata/tg5040"

	void (*log)(const char* plugin_id, const char* message);

	// The calling plugin's own settings, in
	// .userdata/<platform>/launcher/<id>.cfg: the launcher knows which
	// plugin is calling from the hook or callback it's running, so there's
	// no id to pass. cfg_get returns fallback when the key isn't set. The
	// returned string stays valid until the plugin's next cfg_set. They
	// work from init, hooks and callbacks (actions, dialogs, screens) on
	// the launcher's thread; from a thread of the plugin's own, or anywhere
	// the calling plugin can't be told, they're refused (cfg_get returns
	// fallback, cfg_set does nothing) and the launcher logs it once. Read
	// what a thread needs first, and hand it over. Another plugin's
	// settings are read with cfg_read.
	const char* (*cfg_get)(const char* key, const char* fallback);
	void (*cfg_set)(const char* key, const char* value);

	// Entries. A new entry belongs to whoever it's pushed into.
	MosaicEntry* (*entry_new)(const char* path, int type, const char* name);
	const char* (*entry_path)(const MosaicEntry* entry);
	const char* (*entry_name)(const MosaicEntry* entry);
	int (*entry_type)(const MosaicEntry* entry);

	MosaicList* (*list_new)(void);
	int (*list_count)(const MosaicList* list);
	MosaicEntry* (*list_get)(const MosaicList* list, int index);
	void (*list_push)(MosaicList* list, MosaicEntry* entry);

	// What the user is looking at. Either can be NULL.
	const MosaicEntry* (*selected_entry)(void);
	const char* (*current_path)(void);

	void (*request_redraw)(void);

	// Actions are named things the user can do; bindings put them on
	// buttons. Plugin bindings rank after the screen's own, so they can
	// only use buttons the screen leaves free. Both return 0 on success.
	// For now only MOSAIC_SCREEN_GAMELIST accepts bindings. flags is 0 or
	// enum MosaicActionFlag bits. available and run get the entry's full
	// path for an action with MOSAIC_ACTION_PATH, and NULL for any other.
	// A button gives such an action the game list's selected entry while
	// the game list shows; anywhere else, or with nothing selected, it
	// isn't available and is refused. available can be NULL for always.
	int (*register_action)(const char* id, const char* label, int flags,
		int (*available)(const char* path, void* data), void (*run)(const char* path, void* data), void* data);
	int (*bind_button)(int screen, int button, int trigger, const char* action_id);

	// Fields below were added after the first release; check for them with
	// MOSAIC_HOST_HAS before calling.

	// A short message at the bottom of the screen for a moment, eg. "Added
	// to favorites". A new one replaces the last.
	void (*toast)(const char* message);

	// Holds off auto-sleep while on is 1, eg. during a download, until the
	// same plugin calls it with 0. The power button still sleeps the device.
	// Shell paks can do the same by creating /tmp/stay_awake.
	void (*inhibit_sleep)(const char* plugin_id, int on);

	// Full-screen dialogs. Each returns at once and calls back after the
	// dialog closes, so call them from an action, not from a list hook.
	// confirm shows message with B to cancel and A labelled yes_label;
	// on_yes runs only if the user chose A. keyboard lets the user type,
	// starting from initial; on_done gets the text, or NULL if cancelled.
	void (*confirm)(const char* message, const char* yes_label, void (*on_yes)(void* data), void* data);
	void (*keyboard)(const char* title, const char* initial, void (*on_done)(const char* text, void* data), void* data);

	// Opens a game, pak or folder (an enum MosaicEntryType) the way selecting
	// it in the game list would: a game or pak launches, after pre_launch,
	// and a folder opens, launching its .cue or .m3u if it has one. Returns
	// 1 when it opened or the launch is queued, 0 when it didn't (a missing
	// file, a cancelled launch, or called from init, before the lists exist).
	int (*open_path)(const char* path, int type);

	// Recently played games and paks, newest first, as the game switcher
	// lists them. recent_path returns the full path, or NULL past the end;
	// the string stays valid until the next recent_path call. recent_remove
	// returns 0 on success. Both changes are saved at once.
	int (*recents_count)(void);
	const char* (*recent_path)(int index);
	int (*recent_remove)(int index);
	void (*recents_clear)(void);

	// Rebuilds the open folders from the card on the next frame, keeping
	// each selection on the same entry where it can. Call it after changing
	// something on disk, such as deleting a game or clearing recents.
	void (*reload_lists)(void);

	// Renames an entry from inside list_ready. The new name is what the
	// list draws and what the letter index uses. Returns 0, or -1 when
	// called anywhere but list_ready.
	int (*entry_set_name)(MosaicEntry* entry, const char* name);

	// The list the user is looking at, read with list_count and list_get,
	// or NULL before the lists exist. Don't keep it: it's freed when the
	// user leaves the folder or the lists reload. selected_index returns
	// the selected row in it, or -1 when it's empty. select_index moves
	// the selection to a row, scrolling to keep it in view, and returns 0,
	// or -1 for a row that isn't there, during layout_render or during
	// selection_changed. Call them from an action, tick or layout_input,
	// not from a list hook, which may be listing another folder.
	const MosaicList* (*current_list)(void);
	int (*selected_index)(void);
	int (*select_index)(int index);

	// Quick menu tiles. quick_tile puts a registered action on the quick
	// menu as a tile in row (an enum MosaicQuickRow), after the built-in ones:
	// its label is the action's, A runs it, and it shows only while the
	// action is available. icon is a PNG to draw on it, or NULL. With
	// is_on, the tile has an on/off state (called with the action's data),
	// and icon_off, when given, is drawn while it's off. hide_quick_tile
	// hides a built-in tile by name (Recents, Collections, Games, Tools,
	// Settings, Pak Store, Wifi, Bluetooth, Sleep, Reboot, Poweroff) or a
	// plugin tile by its action id while hidden is 1, and shows it again
	// with 0. The tiles are built at start and again when the lists
	// reload, which is also when available is checked. Both return 0, or
	// -1 for an unknown action or a bad argument.
	int (*quick_tile)(const char* action_id, int row, const char* icon, const char* icon_off, int (*is_on)(void* data));
	int (*hide_quick_tile)(const char* name, int hidden);

	// A full-screen list of choices, drawn like the Launcher pak's menus.
	// Returns at once, then calls on_pick with the index the user chose
	// with A, or -1 if they backed out with B. selected is the row the
	// cursor starts on. The strings are copied. Returns 0, or -1 for no
	// choices or more than 64. Call it from an action, like confirm.
	int (*choose)(const char* title, const char* const* choices, int count, int selected,
		void (*on_pick)(int index, void* data), void* data);

	// Points a recent at a new full path on the card, eg. after a game was
	// renamed, keeping its place and its name. Saved at once. Returns 0, or
	// -1 for a bad index, a path off the card, or a path already in recents.
	int (*recent_set)(int index, const char* path);

	// Puts an action on a game list button in place of what the game list
	// does with it. The action is one from register_action or one of the
	// game list's own:
	//
	//   gamelist.open         A, press: open the selected folder, game or pak
	//   gamelist.back         B, press: back out of the folder
	//   gamelist.resume       X, release: resume the selected game
	//   gamelist.prev_letter  L1, held repeats: jump to the previous letter
	//   gamelist.next_letter  R1, held repeats: jump to the next letter
	//   gamelist.switcher     SELECT, tap: open the game switcher
	//   gamelist.quick_menu   MENU, tap: open the quick menu
	//
	// The override takes the whole button: the game list's own bindings on
	// it and plugins' bind_button ones stop running, whatever their trigger,
	// and a NULL action_id leaves it doing nothing. The game list's hints
	// follow its actions to their new buttons, so swapping A and X swaps
	// their hints too. A plugin can override a button more than once, for
	// press and release or for several actions on one trigger, where the
	// first available one runs. A button overridden by one plugin can't be
	// by another: the first in load order keeps it, and the loader logs
	// the refusal. Call it from init, where it returns 0, or -1 for an
	// unknown action, a taken button or a bad argument. For now only
	// MOSAIC_SCREEN_GAMELIST takes overrides. With override_repeat, it
	// also takes MOSAIC_TRIGGER_REPEAT, and a letter jump put back on its
	// own shoulder keeps stock's check that the other shoulder is up.
	int (*override_button)(int screen, int button, int trigger, const char* action_id);

	// Called from pre_launch, sets the command to run instead of the
	// launch, then pre_launch returns MOSAIC_LAUNCH_REPLACE. program is a file
	// on the card, such as another emulator pak's launch.sh or minarch.elf;
	// args (up to 8, each under 256 bytes, no newlines) follow it. For a
	// game, the path the launcher would have launched comes last, after it
	// picks the disc to resume; for a pak nothing is added. Everything is single-quoted, the way the
	// launcher quotes its own commands. Recents, resume and play time are
	// kept as for any launch. When two plugins replace a launch the first in
	// load order wins, and the second's call returns -1. Returns 0, or -1
	// outside pre_launch, for a program that isn't a file on the card, or
	// for arguments that don't fit.
	int (*replace_launch)(const char* program, const char* const* args, int count);

	// Relabels a built-in quick menu tile in the top row (Recents,
	// Collections, Games or Tools), or restores its own label with NULL or
	// "". Hiding and art still go by the built-in name. Returns 0, or -1
	// for any other name.
	int (*rename_quick_tile)(const char* name, const char* label);

	// The battery, as the status bar shows it. battery_level returns the
	// charge from 0 to 100, and is_charging returns 1 while it charges and
	// 0 otherwise. Both return -1 on a platform without a battery, such as
	// desktop, so a plugin can report itself unsupported there.
	int (*battery_level)(void);
	int (*is_charging)(void);

	// How many LEDs the device's LED settings control: 0 on devices without
	// controllable LEDs, and on desktop. Set them with led_set_color.
	int (*led_count)(void);

	// Powers the device off, or reboots it when reboot is 1, the way the
	// quick menu's Poweroff and Reboot do: on the next frame, after every
	// plugin's before_poweroff. On desktop both quit the launcher.
	void (*power_off)(int reboot);

	// 1 when stock's root would list Recently Played: Show Recents is on
	// in Settings and there are recents. A root_replace can add the folder
	// itself, as a MOSAIC_ENTRY_DIR at sdcard_path + "/Recently Played".
	int (*show_recents)(void);

	// Stock NextUI's own on/off settings, by a MOSAIC_TOGGLE_* name. They go
	// through the launcher's settings, so the change is saved to
	// minuisettings.txt at once, the status bar redraws with it, and games
	// launched afterwards see it. stock_toggle_get returns 1 for on, 0 for
	// off, or -1 for a name it doesn't know. stock_toggle_set takes 1 or 0
	// and returns 0, or -1 for a name it doesn't know or another value.
	int (*stock_toggle_get)(const char* name);
	int (*stock_toggle_set)(const char* name, int on);

	// 1 when override_button takes MOSAIC_TRIGGER_REPEAT, which runs the
	// action on the press and again while the button is held, as stock's
	// L1 and R1 do. bind_button doesn't take it. Check it with
	// MOSAIC_HOST_HAS(host, override_repeat), and use
	// MOSAIC_TRIGGER_PRESS without it.
	int override_repeat;

	// Lights every LED in one static colour, rgb as 0xRRGGBB, at the
	// brightness the user's LED settings give each one, or changes the
	// colour the same plugin set. Stock's charging, critical and low
	// battery, mute and sleep lights still come first, and the colour
	// comes back after them. led_clear puts the user's own LED settings
	// back; the launcher also does that after the plugin's quit (such as
	// for a launch, or when it's turned off) and before a power-off, and
	// games set the LEDs their own way. Nothing is saved: the user's LED
	// settings files are never written. One plugin's colour is on at a
	// time: until it clears it, another plugin's led_set_color returns -1.
	// led_set_color returns 0, or -1 with no LEDs (led_count is 0, as on
	// desktop), while another plugin's colour is on, or for rgb past
	// 0xFFFFFF. led_clear returns 0, or -1 when the plugin's colour isn't
	// on.
	int (*led_set_color)(const char* plugin_id, unsigned int rgb);
	int (*led_clear)(const char* plugin_id);

	// A stock theme colour, by an enum MosaicThemeColor, as 0xRRGGBB, or
	// -1 for any other index.
	int (*theme_color)(int index);

	// Bluetooth. bt_supported returns 1 on a device with Bluetooth and 0
	// elsewhere, including desktop, where the others refuse or report
	// nothing. bt_enabled returns the user's Bluetooth toggle, 1 or 0;
	// plugins can't change it. bt_connected_address writes the device
	// connected now as "XX:XX:XX:XX:XX:XX" (upper case, size at least 18)
	// and returns 1, or writes "" and returns 0 when none is, while
	// Bluetooth is off, or before the host's first look. The host looks
	// every few seconds off the main thread, so it's cheap enough for tick.
	// bt_connect asks for a paired device to connect and returns at once:
	// 0 when the connect is queued, or -1 for an address not in that form,
	// while Bluetooth is off or unsupported, or while another connect is
	// queued or running. The connect runs on a host thread for up to 15
	// seconds; watch bt_connected_address to see whether it took.
	int (*bt_supported)(void);
	int (*bt_enabled)(void);
	int (*bt_connected_address)(char* out, size_t size);
	int (*bt_connect)(const char* address);

	// Drawing; check for it with MOSAIC_HOST_HAS(host, draw_text). Every
	// call checks its arguments: a NULL, a surface the launcher doesn't
	// know (such as one already freed) or a size out of range is refused,
	// with -1 or NULL, rather than drawn.
	//
	// The screen's size in pixels, and stock's FIXED_SCALE: how many pixels
	// one unit of stock's layout is (2 or 3 on today's devices).
	int (*screen_width)(void);
	int (*screen_height)(void);
	int (*screen_scale)(void);

	// Surfaces a plugin makes are its own: free them with surface_free, and
	// the launcher frees any left after the plugin's quit, or when its init
	// declines. surface_new makes a transparent w by h surface. image_load
	// reads a PNG or JPEG from a regular file. surface_scale makes a w by h
	// copy of src, fitted by an enum MosaicFit. Each is up to 4096 pixels a
	// side, and a plugin's surfaces together up to 32 MiB; past that, or
	// for a bad argument or file, they return NULL. surface_free returns 0,
	// or -1 for a surface this plugin didn't make.
	MosaicSurface* (*surface_new)(const char* plugin_id, int w, int h);
	MosaicSurface* (*image_load)(const char* plugin_id, const char* path);
	MosaicSurface* (*surface_scale)(const char* plugin_id, const MosaicSurface* src, int w, int h, int fit);
	int (*surface_free)(const char* plugin_id, MosaicSurface* surface);

	// surface_size writes a surface's size. read_pixel writes the colour
	// at a point in it. Both return 0, or -1 for a bad argument or a point
	// outside it.
	int (*surface_size)(const MosaicSurface* surface, int* w, int* h);
	int (*read_pixel)(const MosaicSurface* surface, int x, int y, MosaicColor* out);

	// Drawing on a surface, blending by alpha, and only inside the area a
	// render hook allows on a surface it hands over. A NULL rect is the
	// whole surface. fill_round_rect rounds the corners by radius, at most
	// half the shorter side. blit draws src_rect of src (NULL for all of
	// it) into dst_rect (NULL for the same size at 0,0), stretching it to
	// fit. Each returns 0, or -1 for a bad argument.
	int (*fill_rect)(MosaicSurface* target, const MosaicRect* rect, MosaicColor color);
	int (*fill_round_rect)(MosaicSurface* target, const MosaicRect* rect, int radius, MosaicColor color);
	int (*blit)(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect, const MosaicRect* dst_rect);

	// Text, in the launcher's font (an enum MosaicFont), one line of UTF-8:
	// anything after a newline is left out. text_width returns how wide
	// text is and text_height how tall a line is, in pixels. text_truncate
	// writes text to out, shortened with "..." when it's wider than
	// max_width or longer than size, cutting only between characters, and
	// returns its width. draw_text draws text with its top left at x, y,
	// truncated the same way when max_width is above 0, and returns the
	// width drawn. Each returns -1 for a bad argument.
	int (*text_width)(int font, const char* text);
	int (*text_height)(int font);
	int (*text_truncate)(int font, const char* text, int max_width, char* out, size_t size);
	int (*draw_text)(MosaicSurface* target, int font, const char* text, int x, int y, MosaicColor color, int max_width);

	// Art; check for it with MOSAIC_HOST_HAS(host, art_get). art_find
	// writes where an entry's image of a kind (an enum MosaicArtKind) is,
	// as plugins' art_path hooks in load order and then the launcher's own
	// places answer, and returns 1 when that file exists, or 0 when it
	// doesn't or nothing answers (writing "").
	//
	// art_get gives that image from the launcher's art cache: it returns
	// MOSAIC_ART_READY with the image in *out, MOSAIC_ART_LOADING while it's
	// decoded off the main thread (the launcher redraws once it's in), or
	// MOSAIC_ART_MISSING when art_find finds no file or it can't be decoded.
	// art_load does the same for an image by its full path, such as a save
	// state's preview. Ask each frame you draw: when the selection moves,
	// loads asked for before it that haven't finished are dropped. Asking
	// for the selected entry's art also loads that kind for the entries
	// either side of it.
	//
	// The images are the launcher's, read-only: draw them with blit, read
	// them, or copy them with surface_scale, but don't keep them. One stays
	// valid until the next frame starts, after which it may have been
	// dropped and calls refuse it. surface_free and drawing onto one
	// return -1. The cache holds up to 32 MiB of decoded images for the
	// launcher and every plugin, apart from each plugin's own 32 MiB,
	// dropping the least recently used first. Each image is a PNG, JPEG or
	// BMP regular file up to 16 MiB and 4096 pixels a side.
	int (*art_find)(const MosaicEntry* entry, int kind, char* out, size_t size);
	int (*art_get)(const MosaicEntry* entry, int kind, const MosaicSurface** out);
	int (*art_load)(const char* path, const MosaicSurface** out);

	// Row decorators under a plugin layout. Called from layout_render with
	// the screen it was lent, runs every plugin's decorate_row, in load
	// order, on the entry at index in the list, drawn in rect (a cell of
	// the layout, clipped to view->area) with its name in name, or NULL
	// when the layout doesn't say. A layout calls it for each cell it draws,
	// after drawing the cell. Returns 0, or -1 outside layout_render, from
	// inside a decorate_row, for another surface, an index not in the list
	// or a rect outside view->area. Check for it, and for row decorators
	// and header widgets, with MOSAIC_HOST_HAS(host, decorate_cell).
	int (*decorate_cell)(MosaicSurface* screen, int index, const MosaicRect* rect, const MosaicRect* name);

	// Selection events: how long, in milliseconds, the game list's
	// selection stays put, with no d-pad or shoulder button held, before
	// plugins' selection_changed runs. Check for them with
	// MOSAIC_HOST_HAS(host, selection_settle_ms).
	int selection_settle_ms;

	// .info metadata sidecars, for display hooks such as display_name,
	// decorate_row and layout_render. Writes the value of key (matched
	// without case) in the entry's <dir>/.media/<file name without its last
	// extension>.info, such as "year" or "publisher", to out, cut between
	// characters to fit size, and returns 1, or writes "" and returns 0
	// when the file or the key isn't there, and returns -1 for a bad
	// argument. The file is read the first time an entry is asked about,
	// and kept with the entry until its list is freed. Check for it with
	// MOSAIC_HOST_HAS(host, entry_info).
	int (*entry_info)(const MosaicEntry* entry, const char* key, char* out, size_t size);

	// blit with src's alpha scaled by alpha, from 255 (as blit) to 0
	// (nothing), for fades. Returns 0, or -1 for a bad argument or an alpha
	// outside 0 to 255. Check for it with MOSAIC_HOST_HAS(host, blit_alpha).
	int (*blit_alpha)(MosaicSurface* target, const MosaicSurface* src, const MosaicRect* src_rect,
		const MosaicRect* dst_rect, int alpha);

	// Transition styles: the longest, in milliseconds, the launcher lets a
	// plugin's transition run. Check for them with
	// MOSAIC_HOST_HAS(host, transition_max_ms).
	int transition_max_ms;

	// 1 on a launcher that asks plugins' art_surface for generated art and
	// has the background slot (background_render). Check for both with
	// MOSAIC_HOST_HAS(host, plugin_art).
	int plugin_art;

	// 1 on a launcher that asks plugins' hide_name, and passes the answers
	// to layouts and row decorators. Check for it with
	// MOSAIC_HOST_HAS(host, hide_names).
	int hide_names;

	// Play history, read-only from stock's play tracker: gametimectl's
	// .userdata/shared/game_logs.sqlite, which the Game Tracker tool shows.
	// path is a game's full path on the card, as entry_path and recent_path
	// return it. play_time returns the seconds it has been played in all,
	// last_played when its latest session started, as a Unix time, and
	// play_count how many sessions it has, as the Game Tracker counts them
	// (waking from sleep in a game starts another). A game the tracker
	// hasn't seen gets 0 from each, and so does a path outside Roms.
	// play_time_today returns the seconds played across every game since
	// local midnight, counting the part of a session from before it that
	// ran past it. Each returns MOSAIC_PLAY_UNKNOWN (-1) when the history
	// can't be read: there's no database yet (it's made at the first launch
	// of a game), no sqlite library, or the database can't be opened or is
	// busy. Nothing is read until a plugin first asks; then the database is
	// read once, and the answers are kept until a game or pak returns (just
	// before post_return), so they're cheap enough to ask every frame. A
	// session gametimectl hasn't closed yet counts up to that read. Check
	// for them with MOSAIC_HOST_HAS(host, play_time).
	long long (*play_time)(const char* path);
	long long (*last_played)(const char* path);
	int (*play_count)(const char* path);
	long long (*play_time_today)(void);

	// Art decorators under a plugin layout. Called from layout_render with
	// the screen it was lent, runs every plugin's decorate_art, in load
	// order, on the entry at index in the list, with art the rect the
	// layout drew its art in and below the part of view->area under it, as
	// wide as art, clipped to below. A layout calls it after drawing the
	// art it wants details under, such as the selected game's. Returns 0,
	// also when there's no room under art, or -1 outside layout_render, from
	// inside a decorator, for another surface, an index not in the list or
	// art outside view->area. Check for it, and for the decorate_art hook,
	// with MOSAIC_HOST_HAS(host, decorate_art).
	int (*decorate_art)(MosaicSurface* screen, int index, const MosaicRect* art);

	// The hold switch: the switch on the side of the tg5040 and tg5050,
	// which stock's keymon reads and keeps as msettings' mute state, the one
	// stock's Mute settings act on. Returns 1 or 0 for its position, as that
	// mute state has it (1 is muted), or -1 on a device without one, such as
	// the h700 and desktop. keymon looks at the switch five times a second,
	// so a change can take a fifth of a second to show. It's cheap enough to
	// ask every frame. Check for it, and for input_filter and the MENU,
	// POWER and FN buttons, with MOSAIC_HOST_HAS(host, hold_switch).
	int (*hold_switch)(void);

	// The launcher's PIN, the one the user sets on the Launcher pak's
	// Plugins page, shared by simple mode and the Plugins page's protected
	// settings. Plugins never see it: has_pin returns 1 when one is set,
	// and ask_pin opens the keyboard under title, hiding what's typed, and
	// checks it. on_done runs after the keyboard closes with
	// MOSAIC_PIN_RIGHT, MOSAIC_PIN_WRONG or MOSAIC_PIN_CANCELLED; with no
	// PIN set it runs right away, from inside ask_pin, with
	// MOSAIC_PIN_RIGHT. Neither logs what's typed. Check for them with
	// MOSAIC_HOST_HAS(host, ask_pin).
	int (*has_pin)(void);
	void (*ask_pin)(const char* title, void (*on_done)(int result, void* data), void* data);

	// RetroAchievements progress, read-only from what minarch keeps under
	// .userdata/shared/.ra/: the cached achievement set and unlocks it
	// fetched for the game (offline/cache), its unlock ledger for unlocks it
	// made offline (offline/ledger.bin) and the game's muted list. Nothing
	// is fetched over the network, so a game minarch hasn't loaded with
	// RetroAchievements on has nothing. minarch names those files by the
	// game's rcheevos hash, which the launcher works out the way minarch
	// does, with rcheevos' own code, on a worker thread one game at a time,
	// and keeps on the card in .userdata/shared/launcher/ra-hashes.txt by
	// path, size and modification time; a changed file is hashed again.
	// path is a game's full path on the card, as entry_path and recent_path
	// return it. achievements fills out (see MosaicAchievements) and returns
	// 1, returns 0 when there's nothing on the card for the game (no cached
	// set, a system rcheevos can't hash, a path that isn't a file), or
	// MOSAIC_ACHIEVEMENTS_UNKNOWN (-1) until its hash is worked out. The
	// first ask about a path queues it, and the list is drawn again when
	// answers come in, so asking every frame is fine; an answer then stays
	// the same until the launcher restarts (it does after every game).
	// achievement_hash writes the hash (32 hex digits) with the same
	// answers. Nothing runs until a plugin first asks. Check for them with
	// MOSAIC_HOST_HAS(host, achievements).
	int (*achievements)(const char* path, MosaicAchievements* out);
	int (*achievement_hash)(const char* path, char* out, size_t size);

	// Copies pixels into rect of a surface the plugin made (NULL for all of
	// it), replacing what's there, alpha included, rather than blending.
	// pixels are RGBA8888: four bytes a pixel, r, g, b then a, with a 255
	// for opaque and not premultiplied (ffmpeg's AV_PIX_FMT_RGBA), from
	// rect's top left, with rows starting pitch bytes apart. The part of
	// rect past the surface's edges is left out. Returns 0, also when none
	// of rect is on the surface, or -1 for a NULL, a rect with a negative
	// size, a pitch under rect's width times 4, or a surface the plugin
	// didn't make or no longer owns: one a hook lent it, the art cache's,
	// one it handed the launcher from art_surface, or another plugin's.
	// Like every drawing call it's for the main thread only: convert
	// decoded frames on a thread of your own and only copy them here.
	// Check for it with MOSAIC_HOST_HAS(host, surface_write).
	int (*surface_write)(const char* plugin_id, MosaicSurface* surface, const MosaicRect* rect, const void* pixels,
		int pitch);

	// Shows surface, one the plugin made and still owns, in place of the
	// selected entry's thumbnail on the built-in list: sized and placed as
	// a thumbnail of its size would be, with the thumbnail's rounded
	// corners, and with art decorators under it. Under a plugin layout the
	// layout draws it in its own place for the selected entry's art, when
	// it asks for it with art_replacement. The launcher doesn't take it:
	// the plugin can keep drawing into it or writing into it with
	// surface_write, and calls art_replace again with the same surface to
	// show what it has now, which redraws only the thumbnail, not the list;
	// under a layout it redraws the layout's whole area, and only while the
	// layout drew the surface the last time it drew. Never call
	// request_redraw for each frame: it redraws the whole list.
	// NULL takes the plugin's surface away again. It goes away by itself
	// when the selection moves, the game list is left, the surface is
	// freed or no longer the plugin's, or the plugin quits; after that the
	// next call shows it afresh, for whatever is selected then. Only one
	// plugin's surface shows at a time: the first keeps the place until it
	// goes. Returns 0, also for NULL, or -1 for a NULL id, a surface the
	// plugin didn't make or no longer owns, another plugin's surface
	// showing, from init or a hook that only draws, or where no art for the
	// selected entry shows: off the game list, in an empty folder or with
	// Show game art off. Main thread only. Check for it with
	// MOSAIC_HOST_HAS(host, art_replace).
	int (*art_replace)(const char* plugin_id, MosaicSurface* surface);

	// Audio, such as a video preview's sound, through SDL's audio queue.
	// One plugin at a time has the device. audio_open opens it for frames
	// of signed 16-bit samples in the machine's byte order, channels (1 or
	// 2) of them interleaved, at rate (8000 to 96000 Hz); SDL converts them
	// to what the device plays. It starts playing at once, silence while
	// nothing is queued. Opening again in the same format keeps the device
	// and what's queued; in another format closes and reopens it. It
	// returns 0, or -1 for a bad rate or channel count, while another
	// plugin has the device, or when SDL can't open it. audio_queue adds
	// count frames after what's queued, and returns 0, or -1 when the
	// plugin doesn't have the device, for NULL frames or a negative count,
	// or when the queue would hold more than audio_queue_max_ms of frames. audio_queued returns how many frames
	// are still queued, not yet handed to the device (whose own buffer
	// adds about 1024 frames), which gives a video its clock, or -1 when
	// the plugin doesn't have the device. audio_pause(1) stops playing,
	// keeping the queue, and audio_pause(0) plays on; audio_close closes
	// the device, dropping what's queued. Both return 0, or -1 when the
	// plugin doesn't have the device. Closing the device clicks on some
	// devices, so keep it open on silence between clips and close it after
	// a short linger. The launcher closes it itself before a sleep, once
	// what's queued plays out, before a game or pak launches, before
	// powering off, and when it quits, after which audio_queue and
	// audio_queued return -1 until the plugin opens it again. Volume and
	// mute are the system's. audio_queue and audio_queued are the only
	// host calls that are safe from a plugin's own thread, such as a
	// decoder's; everything else, audio_open, audio_pause and audio_close
	// included, is for the main thread only. Check for them with
	// MOSAIC_HOST_HAS(host, audio_open).
	int (*audio_open)(const char* plugin_id, int rate, int channels);
	int (*audio_queue)(const char* plugin_id, const short* frames, int count);
	int (*audio_queued)(const char* plugin_id);
	int (*audio_pause)(const char* plugin_id, int paused);
	int (*audio_close)(const char* plugin_id);
	int audio_queue_max_ms; // the most audio_queue lets a plugin have queued, in ms

	// Art replacement under a plugin layout. Called from layout_render, it
	// writes the surface a plugin showed with art_replace for the entry at
	// index to *out and returns 1, or returns 0 when there's none for it:
	// only the selected entry has one, and only while a plugin shows one.
	// The layout draws it where it draws the selected entry's art, in place
	// of that art, with blit or blit_alpha, fitting it as it likes. It's
	// the other plugin's, read-only: draw it, size it or read it, but don't
	// draw on it, free it or keep it, since it's only good until
	// layout_render returns. Asking for it is what lets that plugin's new
	// frames redraw the layout: while the layout's last layout_render got
	// it, each frame art_replace shows draws the layout again. Returns -1
	// outside layout_render, from inside a decorator, for a NULL out or an
	// index not in the list. Check for it with
	// MOSAIC_HOST_HAS(host, art_replacement).
	int (*art_replacement)(int index, const MosaicSurface** out);

	// Screens of a plugin's own, over the game list, the quick menu, the
	// game switcher or another plugin screen. register_screen, from init
	// only, adds a screen by id ("<plugin id>.<name>" by convention): it
	// returns 0, or -1 outside init, for an id that's NULL, empty, 64
	// characters or more, taken, or a built-in screen's name, for hooks
	// without render, or past 32 screens in all.
	//
	// open_screen puts a screen on top of what shows, running its enter,
	// such as from an action's run (a button, an FN button or a quick menu
	// tile), tick, a dialog's callback, or another screen's input. It
	// returns 0, or -1 for an unknown screen, one already open, one off
	// after an error, past 8 open, an enter that declined, the switcher
	// slot's screen (run_action("gamelist.switcher") opens it), while a dialog
	// shows or a launch starts, and from init, enter, exit or any hook that
	// only draws. close_screen closes a screen and any opened above it,
	// running their exit, and returns 0, or -1 for one that isn't open,
	// while a dialog shows, and from the hooks open_screen refuses. Opening
	// slides in as a folder does, and closing out, at the user's Menu
	// transitions setting, except to or from the game switcher or a dialog;
	// the transition slot's owner gets them as pushes and pops.
	//
	// current_screen returns what's showing: the open plugin screen's id,
	// or "gamelist", "switcher", "quickmenu" or "dialog". Check for these
	// and overlay_render with MOSAIC_HOST_HAS(host, open_screen).
	int (*register_screen)(const char* id, const MosaicScreenHooks* hooks);
	int (*open_screen)(const char* id);
	int (*close_screen)(const char* id);
	const char* (*current_screen)(void);

	// Runs an action as a button bound to it would: one from
	// register_action, or one of the game list's own (see override_button),
	// such as gamelist.quick_menu or gamelist.switcher to open the quick
	// menu or game switcher from a plugin screen. path is the full path of
	// the entry an action with MOSAIC_ACTION_PATH acts on, such as a game
	// for romm.flashback; with NULL or "" it gets what a button would give
	// it, the game list's selected entry while the game list shows. Other
	// actions get NULL whatever path is. Returns 0, or -1 for an unknown
	// action, one that isn't available, one with MOSAIC_ACTION_PATH and no
	// path, and wherever open_screen is refused. A built-in screen it opens
	// from a plugin screen closes the plugin screens, as an FN button does.
	// Check for it with MOSAIC_HOST_HAS(host, run_action).
	int (*run_action)(const char* action_id, const char* path);

	// The game switcher's recents, for a switcher of a plugin's own (see
	// MOSAIC_SCREEN_GAME_SWITCHER), alongside recents_count, recent_path and
	// recent_remove. recent_info fills out (see MosaicRecent) for the
	// recent at index, newest first, and returns 0, or -1 for an index past
	// the end or a NULL out. recent_open opens it as the built-in
	// switcher's A does: with resume 1 a game resumes its save state when
	// it has one, and with 0 it starts fresh; a pak just launches. Launched
	// while the switcher slot's screen is open, the launcher comes back to
	// that screen after the game, as stock's switcher does. recent_open
	// returns 1 when the launch is queued, or 0 for a bad index, a recent
	// whose emulator isn't installed, a launch pre_launch cancelled, and
	// wherever open_screen is refused. Check for them with
	// MOSAIC_HOST_HAS(host, recent_open).
	int (*recent_info)(int index, MosaicRecent* out);
	int (*recent_open)(int index, int resume);

	// Draws stock's button hints on a plugin screen, as the built-in
	// screens draw theirs: one group of one or two hints (see MosaicHint)
	// in a pill at the bottom left (MOSAIC_HINTS_LEFT) or bottom right
	// (MOSAIC_HINTS_RIGHT) of screen, in stock's fonts and theme colours,
	// with each label put through the launcher's translation. Only on the
	// screen lent to a MosaicScreenHooks render, from inside it. When
	// drawn isn't NULL it gets the pill's rect. Returns 0, or -1 for any
	// other surface or hook, a bad group, a count other than 1 or 2, a
	// button stock has no hint for, a NULL or empty label, or a pill that
	// wouldn't fit on the screen clear of the status pill, the header and
	// the pills already drawn this frame. Check for it with
	// MOSAIC_HOST_HAS(host, draw_hints).
	int (*draw_hints)(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn);

	// The whole play history, from the same read as play_time.
	// play_games_count returns how many games it lists: every game with a
	// session, each path once, and orphans by name, up to the 10000 most
	// played (the rest aren't read, and get 0 from play_time too).
	// play_game_info fills out (see MosaicPlayGame) for the game at index,
	// most played first, then the latest played, and returns 0, or -1 for an
	// index past the end or a NULL out. play_time_on returns the seconds
	// played across every game on the local day days_ago days before today
	// (0 is today, as play_time_today), counting the part of each session
	// that falls in it; every session is read the first time a plugin asks
	// for a day, up to the newest 100000, and a day from before those gets
	// MOSAIC_PLAY_UNKNOWN. Each returns MOSAIC_PLAY_UNKNOWN when the history
	// can't be read, as play_time does, and play_time_on for a negative
	// days_ago too. The answers are kept until a game or pak returns, and
	// days move on by themselves at midnight. Check for them with
	// MOSAIC_HOST_HAS(host, play_time_on).
	int (*play_games_count)(void);
	int (*play_game_info)(int index, MosaicPlayGame* out);
	long long (*play_time_on)(int days_ago);

	// Wi-Fi, read only: plugins can't turn it on or off or join a network.
	// wifi_supported returns 1 on a device with Wi-Fi and 0 elsewhere,
	// including desktop. wifi_enabled returns the user's Wi-Fi toggle, 1 or
	// 0, or MOSAIC_WIFI_UNAVAILABLE where there's no Wi-Fi.
	// wifi_connection fills out (see MosaicWifi) and returns 1 while
	// connected, or clears it and returns 0 when not connected or Wi-Fi is
	// off, or MOSAIC_WIFI_UNAVAILABLE where there's no Wi-Fi, for a NULL
	// out, or before the host's first look since Wi-Fi came on. The host
	// looks off the main thread every 5 seconds while plugins keep asking,
	// so it's cheap enough for tick. Check for them with
	// MOSAIC_HOST_HAS(host, wifi_connection).
	int (*wifi_supported)(void);
	int (*wifi_enabled)(void);
	int (*wifi_connection)(MosaicWifi* out);

	// The home slot's screen (see MOSAIC_SCREEN_HOME): home_screen returns
	// its id, which current_screen returns too while it's on top, or NULL
	// with no home slot owner, when the owner registered no screen marked
	// home, after the home screen failed and the root list took its place
	// for the run, and from init, before the slot is settled. Check for it
	// with MOSAIC_HOST_HAS(host, home_screen).
	const char* (*home_screen)(void);

	// The strings slot's translations, for a plugin's own text (see
	// translate in MosaicPlugin). tr returns text translated by the slot's
	// owner, and tr_plural the form of singular and plural for the count n
	// (English rules without an owner: n == 1 picks singular). The owner
	// gets plugin_id, so each plugin's strings are kept apart from the
	// launcher's and other plugins', and context, the plugin's own, to tell
	// apart the same English text with different meanings, or NULL for
	// none. With no owner, an unknown text, a
	// translation the host refuses, from init, and from inside the owner's
	// own translate, they return text (or singular or plural) itself.
	// Otherwise the result is the host's copy, kept until the launcher
	// quits. Ask with fixed English text, not names or numbers, and put
	// numbers in with printf afterwards: a translation of a string with
	// printf conversions is only used when it has the same ones. Check for
	// them with MOSAIC_HOST_HAS(host, tr).
	const char* (*tr)(const char* plugin_id, const char* context, const char* text);
	const char* (*tr_plural)(const char* plugin_id, const char* context, const char* singular, const char* plural, unsigned long n);

	// The UI size an enum MosaicUISize names, before scaling: stock's, or
	// the theme slot's owner's (see theme in MosaicPlugin), or -1 for any
	// other value. They change only when the lists reload. Check for it
	// with MOSAIC_HOST_HAS(host, ui_size).
	int (*ui_size)(int which);

	// The plugins loaded this run, for the strings slot's owner to find
	// each one's own files: plugins_count returns how many, and
	// plugin_info fills out (see MosaicPluginInfo) for the one at index,
	// in load order, and returns 0, or -1 for an index past the end or a
	// NULL out. They list every plugin that's on and loaded, bundled ones
	// and ones installed as their own pak, the asking plugin too, and none
	// that's off, skipped or declined to start. In the Launcher pak's
	// settings, which load only the strings and theme slots' owners, they
	// list every plugin its Plugins page shows instead, loaded or not.
	// From init, before every plugin has loaded, plugins_count returns 0
	// and plugin_info -1. Check for them with
	// MOSAIC_HOST_HAS(host, plugin_info).
	int (*plugins_count)(void);
	int (*plugin_info)(int index, MosaicPluginInfo* out);

	// Another plugin's setting, read only: the value of plugin_id's key
	// when that plugin is loaded and its plugin.txt marks the setting
	// public, else fallback, as for an unset key. Everything else is
	// private to its plugin, protected and secret settings always. The
	// returned string stays valid until that plugin's next cfg_set. It
	// works where cfg_get does. Check for it with
	// MOSAIC_HOST_HAS(host, cfg_read).
	const char* (*cfg_read)(const char* plugin_id, const char* key, const char* fallback);

	// FN1 to FN3 (MOSAIC_BUTTON_FN1 to MOSAIC_BUTTON_FN3), the buttons
	// stock's Settings lets the user assign to a Tools pak or an action.
	// fn_name returns the device's name for one, such as "L3", "R4" or
	// "HOME", or "" when the device lacks it (the h700 and desktop lack all
	// three) and for any other button. fn_binding fills out (see
	// MosaicFnBinding) with what the button runs when pressed and whose
	// binding it is, and returns 0, or -1 for a button the device lacks, a
	// NULL out, or where cfg_get is refused. fn_bind binds a button for the
	// calling plugin, which the launcher knows as for cfg_get, to action:
	// "action:<id>" for an action from register_action or one of the game
	// list's own, or "pak:<folder>" for a Tools pak, the folder relative to
	// Tools/<platform>, such as "pak:Clock.pak". A press runs it as the
	// user's own binding would: from the game list, the game switcher and
	// the quick menu, doing nothing while the action is unknown or
	// unavailable or the pak isn't there. Bindings are held in memory and
	// never written to stock's settings. The user's choice always wins: a
	// plugin can only bind a button the user left unassigned in Settings,
	// and once the user assigns it, the plugin's binding is dropped, which
	// fn_binding then shows as MOSAIC_FN_USER. The first plugin to bind a
	// button keeps it, and can bind it again to another action, until it
	// releases it with fn_release or is unloaded. fn_bind returns 0, or -1
	// for a button the device lacks, a bad action, a button the user
	// assigned or another plugin holds, or where cfg_get is refused; the
	// launcher logs each refusal. fn_release returns 0, or -1 when the
	// calling plugin doesn't hold the button. Plugins show what they bound
	// only with their own hints (see draw_hints). Check for them with
	// MOSAIC_HOST_HAS(host, fn_release).
	const char* (*fn_name)(int button);
	int (*fn_binding)(int button, MosaicFnBinding* out);
	int (*fn_bind)(int button, const char* action);
	int (*fn_release)(int button);

	// The last frame shown, the whole screen as it was composited,
	// overlays, pills and the status pill included. While the launcher's
	// dialogs (confirm, keyboard, choose) or the quick menu show, and from
	// the callback a dialog runs as it closes, it's the last frame shown
	// before the first of them opened, so an action run from a menu
	// captures what the menu opened over. capture_screen returns it as a
	// screen_width by screen_height surface of the calling plugin's own,
	// which the launcher knows as for cfg_get, counted against its 32 MiB
	// and freed with surface_free. save_screenshot saves it as a PNG in
	// Screenshots on the card, named as stock names a game's,
	// Launcher.<YYYY-MM-DD-HH-MM-SS>.png (a second save in the same second
	// replaces the first, as stock's does), copies its full path to path
	// (size bytes) when path isn't NULL, and shows "Screenshot saved" when
	// stock's screenshot notification setting is on. Both work only on the
	// launcher's thread, from a plugin's hooks and callbacks but not its
	// init, outside rendering, and the launcher logs each refusal:
	// capture_screen returns NULL and save_screenshot -1 then, as for a
	// surface over the budget or a file that can't be written. Check for
	// them with MOSAIC_HOST_HAS(host, save_screenshot).
	MosaicSurface* (*capture_screen)(void);
	int (*save_screenshot)(char* path, size_t size);

	// Button hints in the style in effect. hint_style returns an enum
	// MosaicHintStyle other than MOSAIC_HINT_STYLE_USER: the theme slot's
	// owner's when its theme sets one (see hint_style in MosaicTheme),
	// otherwise the user's from stock's Settings. It changes only when the
	// lists reload. draw_button draws one button as stock's hints draw it,
	// without a label: in that style, at the button size (ui_size's
	// MOSAIC_UI_BUTTON, scaled) high, in the theme's colours, its top left
	// at x and y on surface, one the plugin can draw on (its own, or one
	// lent to a hook). button is any a MosaicHint takes, the d-pad and enum
	// MosaicGlyph included: a letter or an icon is a circle as wide as it's
	// high, and a name such as START, or the d-pad's in the text style, a
	// wider pill. With surface NULL it draws nothing, to measure. When drawn
	// isn't NULL it gets the button's rect. draw_button returns 0, or -1
	// for a surface the plugin can't draw on, a button with no hint (such
	// as an FN button the device lacks) or x or y out of range. Check for
	// them with MOSAIC_HOST_HAS(host, draw_button).
	int (*hint_style)(void);
	int (*draw_button)(MosaicSurface* surface, int button, int x, int y, MosaicRect* drawn);
	// Stock's colour palettes, read only, listed as stock's Settings lists
	// them under Appearance's Color Palette: stock's own (.system/res/
	// palettes) first, then the card's Palettes, each folder in the order
	// it reads them from the card, up to the first 64, leaving out a file
	// for a newer palette format, as Settings does. palettes_count reads
	// them again and returns how many there are. palette_info fills out
	// (see MosaicPalette) for the one at index in that list, read the
	// first time it's asked when palettes_count hasn't been, and returns
	// 0, or -1 for an index past the end or a NULL out. palette_selected
	// returns the index in that list of the palette the user picked in
	// Settings, the first with its name, or -1 when it's Custom: none
	// picked, the colours edited since, or a palette that's no longer
	// there, which Settings shows as Custom too. The launcher never applies
	// a palette or writes stock's settings; the theme slot's owner can use
	// one's colours in its theme (see MosaicTheme), which then shows only
	// in the launcher. Check for them with
	// MOSAIC_HOST_HAS(host, palette_selected).
	int (*palettes_count)(void);
	int (*palette_info)(int index, MosaicPalette* out);
	int (*palette_selected)(void);

	// Entry sets: named sets of entries, by full path as entry_path
	// returns it, that one plugin owns and others read, in place of
	// reading each other's files. The first plugin to register a name owns
	// it, and others can only read it: favorites, hidden, tags:<tag>,
	// smart:<collection>, or <its own id>.<anything>; any other name is
	// refused. Names are matched exactly. set_register registers the
	// calling plugin's set, which the launcher knows as for cfg_get, with
	// two callbacks the launcher calls on its thread, as the owner, each
	// time a plugin asks, so the set is always current and the launcher
	// keeps no copy: has returns non-zero when path is in the set, and list
	// calls add(path, ctx) for each path in it, only before it returns.
	// Both get the set's name, so one pair can answer for several sets.
	// Registering its own set again replaces the callbacks. set_register
	// returns 0, or -1 for a name it may not have, one another plugin
	// registered first, or a missing callback; the launcher logs each
	// refusal. set_unregister removes the calling plugin's set, returning
	// 0, or -1 when it has no set by that name; unloading a plugin removes
	// all of its sets. set_has returns 1 when path is in the set, else 0,
	// a set nobody registered too. set_list calls each(path, data), as the
	// calling plugin, for each path in the set, before it returns, and
	// returns how many there were, or -1 for a set nobody registered; each
	// can be NULL, to count. A set whose callback is running isn't asked
	// again until it returns: set_has returns 0 and set_list -1 then.
	// is_filtered returns 1 when a loaded plugin's filter hides the entry
	// at path from its folder right now, without saying which, else 0. Its
	// type is worked out from the card (a folder, a .pak folder, or a
	// file), and the folder is path's parent. Every filter is asked, the
	// calling plugin's too; a filter that calls is_filtered while
	// is_filtered is asking it gets 0. These work where cfg_get does, and
	// are refused elsewhere (set_has and is_filtered return 0, the others
	// -1), and the launcher logs it once. From init, plugins that load
	// later haven't registered their sets yet. Check for them with
	// MOSAIC_HOST_HAS(host, is_filtered).
	int (*set_register)(const char* name, int (*has)(const char* name, const char* path),
		void (*list)(const char* name, void (*add)(const char* path, void* ctx), void* ctx));
	int (*set_unregister)(const char* name);
	int (*set_has)(const char* name, const char* path);
	int (*set_list)(const char* name, void (*each)(const char* path, void* data), void* data);
	int (*is_filtered)(const char* path);

	// Events: a plugin subscribes to a name and the launcher calls it back
	// each time that event is published, in place of reading each other's
	// files. Published events wait in a queue and are delivered on the
	// launcher's thread before its next frame, oldest first, to each
	// subscriber in the order it subscribed, as that subscriber (as for
	// cfg_get), so publishing from a plugin's own thread is allowed and no
	// callback runs inside another; an event published from a callback
	// waits for the next frame. Everything still waiting is delivered as
	// the launcher quits, before plugins' quit hooks, which is how
	// launcher.launch reaches subscribers before a game or pak starts.
	// A plugin publishes only <its own id>.<name>, such as music.track; the
	// launcher's own events are launcher.launch (path=<path>, type=rom or
	// pak, once a launch goes ahead past every pre_launch), launcher.return
	// (the same payload, as the launcher starts again after it, after
	// post_return), launcher.set_changed (name=<set>, from set_changed) and
	// launcher.theme_changed (no payload, when the theme slot's owner
	// puts a different theme in place, or its theme fails and stock's is
	// back). A name is 1 to 63 bytes with no spaces or control
	// characters, matched exactly. A payload is NULL, or key=value lines
	// separated by \n, under 1024 bytes: keys of 1 to 31 letters, digits,
	// '_', '-' or '.', values with no control characters. publish returns
	// 0, or -1 for a name that isn't the plugin's, a bad payload, or a full
	// queue (64 events waiting, 8 of them kept for the launcher's own), and
	// the event is dropped; the launcher logs the first of each. subscribe
	// returns 0, or -1 for a bad name, no callback or 256 subscriptions in
	// all; subscribing to the same name again replaces the callback and
	// data. unsubscribe removes the calling plugin's subscription to name,
	// returning 0, or -1 for none; unloading a plugin, or its init
	// declining, removes all of them. event_value copies the value of key
	// in a payload into out, cut to fit, and returns its whole length, or
	// -1 when the payload has no such key; the first line with it wins.
	// set_changed(name) is for a set's owner: it publishes
	// launcher.set_changed with name=<name> for a set the plugin owns, or
	// one it may register that no other plugin has (so one it just
	// unregistered too), returning 0, or -1. set_names calls each(name,
	// data) for each registered set's name, in the order they were
	// registered, and returns how many there were; each can be NULL, to
	// count. subscribe, unsubscribe, set_changed and set_names work where
	// cfg_get does and return -1 elsewhere; publish and event_value work
	// from any thread. Check for them with MOSAIC_HOST_HAS(host, set_names).
	int (*subscribe)(const char* name, void (*on_event)(const char* name, const char* payload, void* data), void* data);
	int (*unsubscribe)(const char* name);
	int (*publish)(const char* name, const char* payload);
	int (*event_value)(const char* payload, const char* key, char* out, size_t size);
	int (*set_changed)(const char* name);
	int (*set_names)(void (*each)(const char* name, void* data), void* data);

	// Other plugins: plugin_loaded returns 1 when the plugin with that id
	// is on and its init succeeded, and 0 for one that's off, missing,
	// declined, skipped or paused after a crash. has_capability returns 1
	// when such a plugin lists name in its plugin.txt's provides=, matched
	// exactly, else 0; the launcher's own features are checked with
	// MOSAIC_HOST_HAS instead. From init they answer for the plugins that
	// loaded before it, and 0 for the plugin itself and later ones, so list
	// a plugin in optional= to load after it. In the Launcher pak's
	// settings they answer for the strings and theme slots' owners only.
	// Check for them with MOSAIC_HOST_HAS(host, has_capability).
	int (*plugin_loaded)(const char* id);
	int (*has_capability)(const char* name);

	// Providers: services one plugin answers for and others call, in place
	// of copying each other's logic, such as captions.text for a game's
	// play time caption. A service's name is <kind>.<name>, the kind
	// letters, digits, '_' or '-', 1 to 63 bytes with no spaces or control
	// characters, matched exactly; it's the capability the provider lists
	// in its plugin.txt's provides=, so has_capability answers for it, and
	// sets. and events. names aren't services. provide makes the calling
	// plugin, which the launcher knows as for cfg_get, the provider of
	// name, with fn the launcher calls on its thread, as the provider (as
	// for cfg_get), each time a plugin calls the name. The first plugin to
	// provide a name keeps it, and can provide it again to replace fn;
	// unloading it, or its init declining, releases all of its names.
	// provide returns 0, or -1 for a name its provides= doesn't list, a
	// sets. or events. name, a name another plugin provided first, a bad
	// name or no fn; the launcher logs each refusal. call calls name with
	// args, NULL or key=value lines as for publish's payload, but under
	// MOSAIC_CALL_MAX bytes, and copies the reply, key=value lines too, into
	// out, which a value is read from with event_value. Each service
	// documents its keys. fn gets the name and args ("" for none) and an
	// empty out MOSAIC_CALL_MAX bytes long, writes its reply there, and
	// returns 0, or non-zero when it can't answer. call returns the reply's
	// length, or MOSAIC_CALL_UNKNOWN when nobody provides name,
	// MOSAIC_CALL_TOO_SMALL when the reply doesn't fit size,
	// MOSAIC_CALL_REFUSED for bad args, a NULL out or a size of 0, a name
	// whose fn is running (called from inside it, directly or through
	// other services), calls nested MOSAIC_CALL_DEPTH deep, or where cfg_get
	// is refused, and MOSAIC_CALL_FAILED when fn returned non-zero or its
	// reply isn't key=value lines; out is "" for all of those. A call from
	// inside another service's fn is allowed. From init, plugins that load
	// later haven't provided their names yet, so list a provider in
	// optional= to load after it. Check for them with
	// MOSAIC_HOST_HAS(host, call).
	int (*provide)(const char* name, int (*fn)(const char* name, const char* args, char* out, size_t size));
	int (*call)(const char* name, const char* args, char* out, size_t size);

	// Every registered action, the launcher's own (such as gamelist.back)
	// and plugins', in the order they were registered, with the buttons
	// each is on. actions_count returns how many there are. action_info
	// fills out (see MosaicActionInfo) for the one at index and returns 0,
	// or -1 for an index past the end or a NULL out. Its bindings are the
	// ones in effect, in this order: the game list's own, then
	// override_button's, bind_button's and the FN buttons'. A screen's own
	// and bind_button ones whose button an override took are left out, and
	// an FN button shows what a press runs, the user's binding or a
	// plugin's. A plugin's binding has the key of the setting it follows
	// when that plugin's plugin.txt marks a setting button:<the action's
	// id>. They work where cfg_get does; elsewhere
	// actions_count returns 0 and action_info -1. Check for them with
	// MOSAIC_HOST_HAS(host, action_info).
	int (*actions_count)(void);
	int (*action_info)(int index, MosaicActionInfo* out);

	// A key the theme slot's owner passes on with its theme (see extra in
	// MosaicTheme), such as led-status.colour: its value, or fallback when
	// the theme doesn't set it, there's no theme slot owner, or its theme
	// failed. There's no plugin id to pass: a plugin reads the keys named
	// after its own id, and may read any other. The theme is asked for
	// once every plugin has loaded, so from init it returns fallback: read
	// keys where they're used, and again on launcher.theme_changed, which
	// is published when they change. The returned string stays valid until
	// the lists next reload. It works where cfg_get does, on the
	// launcher's thread, and returns fallback elsewhere. Check for it with
	// MOSAIC_HOST_HAS(host, theme_value).
	const char* (*theme_value)(const char* key, const char* fallback);

	// The launcher's built-in lock. The user sets its chord (SELECT and
	// START, L1 and R1, L2 and R2, or off), how long it's held and whether
	// the hold switch locks, on the Launcher page. While it's locked, the
	// launcher takes every button before input_filter sees it, except a
	// long power press, and shows a padlock in the header; over the game
	// list or home its lock screen shows, an ambient screen (see
	// register_ambient) with the time, plugins' lock_text lines and their
	// lock_button buttons. lock locks it as the chord does, such as for a
	// lock action, and returns 0, or -1 when nothing could unlock it (the
	// chord is off) or where cfg_get is refused; the launcher logs each
	// refusal. Plugins can't unlock it. is_locked returns 1 while it's
	// locked, else 0. lock_button, from init only, puts an action from
	// register_action on a button of the lock screen, A, B, X, Y, L1, R1,
	// L2, R2 or the d-pad: a press there runs it, and its hint shows,
	// labelled with the action's label, while it's available. A button in
	// the user's chord does nothing there. It returns 0, or -1 for an
	// unknown action, another button, one another plugin put an action on
	// first, or a call from anywhere but init. Check for them with
	// MOSAIC_HOST_HAS(host, register_ambient).
	int (*lock)(void);
	int (*is_locked)(void);
	int (*lock_button)(int button, const char* action_id);

	// Ambient screens: screens that open by themselves, such as the lock, a
	// charging screen or a screensaver. The user puts them in order on the
	// Launcher page (by default the lock, charging-screen's, then
	// screensaver's, then the rest as they load), and the launcher opens
	// the first that wants to show. register_ambient, from init only, makes
	// screen_id, a screen the plugin registered with register_screen and
	// lists in its plugin.txt's ambient=, an ambient screen. Each frame
	// while the game list, the home screen or an ambient screen is on top,
	// with no dialog showing, the launcher calls wants(idle_ms, data), as
	// the plugin (as for cfg_get), for each ambient screen before the one
	// showing, in order, with how long it's been since a button was down,
	// the device woke, an ambient screen closed or another screen showed;
	// it returns 1 to show. The first that does opens, as open_screen
	// would, over the ambient screen showing, which closes first. One whose
	// enter declines isn't asked again until a button is pressed. The
	// launcher doesn't close an ambient screen: it closes itself, such as
	// on a button, as any screen does. Keep wants cheap: it runs every
	// frame. It returns 0, or -1 for a screen the plugin didn't register,
	// one its ambient= doesn't list, one that's already ambient, past 16,
	// no wants, or a call from anywhere but init; the launcher logs each
	// refusal. Check for it with MOSAIC_HOST_HAS(host, register_ambient).
	int (*register_ambient)(const char* screen_id, int (*wants)(unsigned long idle_ms, void* data), void* data);

	// How many different characters of text, one or more lines of UTF-8,
	// the UI font in use (the theme's, or stock's) has no glyph for, such
	// as a language pack's translations in a theme's font. Control
	// characters, malformed bytes and characters drawn with the one before
	// them (combining marks, variation selectors and zero width joiners)
	// aren't counted, and a fallback font's glyphs don't count either.
	// When out isn't NULL it gets as many of those characters as fit in
	// size bytes, whole, in the order they come. It returns the count, or
	// -1 for a NULL text. It asks the font about every character, so call
	// it when the text or the font changes (launcher.theme_changed), not
	// each frame. Check for it with MOSAIC_HOST_HAS(host, font_missing).
	int (*font_missing)(const char* text, char* out, size_t size);
} MosaicHost;

// What the play history calls return when the history can't be read.
#define MOSAIC_PLAY_UNKNOWN -1

// What the Wi-Fi calls return where there's no Wi-Fi to report on.
#define MOSAIC_WIFI_UNAVAILABLE -1

// What ask_pin's on_done gets.
#define MOSAIC_PIN_RIGHT 1
#define MOSAIC_PIN_WRONG 0
#define MOSAIC_PIN_CANCELLED -1

// What the RetroAchievements calls return until a game's hash is worked out.
#define MOSAIC_ACHIEVEMENTS_UNKNOWN -1

// What call returns when it has no reply, the most a call's args or reply
// holds, its terminator included, so an out this big always fits, and how
// deep calls from inside providers' fn nest at most.
#define MOSAIC_CALL_UNKNOWN -1
#define MOSAIC_CALL_TOO_SMALL -2
#define MOSAIC_CALL_REFUSED -3
#define MOSAIC_CALL_FAILED -4
#define MOSAIC_CALL_MAX 4096
#define MOSAIC_CALL_DEPTH 8

// True when the launcher's host table has this field, so a plugin can use
// newer features when they're there and do without on older launchers.
#define MOSAIC_HOST_HAS(host, field) \
	((host)->size >= offsetof(MosaicHost, field) + sizeof((host)->field) && (host)->field)

///////////////////////////////////////
// What a plugin offers the launcher.

typedef struct MosaicPlugin {
	size_t size;     // sizeof(MosaicPlugin) in the plugin
	int api_version; // MOSAIC_API_VERSION the plugin was built with
	const char* id;  // must match id= in plugin.txt

	// Return 0 to load, anything else to be skipped. quit runs at exit.
	int (*init)(const MosaicHost* host);
	void (*quit)(void);

	// Lists. root_entries adds to the top-level list, after the consoles
	// and before Tools. virtual_dir
	// returns a list (from host->list_new) for a path the plugin owns, or
	// NULL to let the launcher list it. filter returns 0 to hide an entry.
	// display_name writes a new name and returns 1, or returns 0.
	void (*root_entries)(MosaicList* root);
	MosaicList* (*virtual_dir)(const char* path);
	int (*filter)(const MosaicEntry* entry, const char* dir_path);
	int (*display_name)(const MosaicEntry* entry, char* out, size_t size);

	// Launching. pre_launch runs before a game or pak starts and can cancel
	// it; post_return runs when the launcher starts again afterwards.
	int (*pre_launch)(const MosaicLaunch* launch);
	void (*post_return)(void);

	// Called every frame with SDL_GetTicks().
	void (*tick)(unsigned long now);

	// Power.
	void (*before_sleep)(void);
	void (*after_resume)(void);
	void (*before_poweroff)(void);

	// Art. Writes the image to use for an entry (an enum MosaicArtKind) and
	// returns 1, or returns 0 to let the launcher look in its usual place.
	// The first plugin to answer wins. The image doesn't have to exist; the
	// launcher falls back as it would for its own path. Return 0 for kinds
	// you don't know: newer launchers ask for more.
	int (*art_path)(const MosaicEntry* entry, int kind, char* out, size_t size);

	// The root slot. A plugin with slots=root in its plugin.txt can return
	// the whole top-level list (from host->list_new) in place of the stock
	// one, or NULL to keep stock's. Only the slot's owner is asked. Other
	// plugins' root_entries are added to a replaced root only when the
	// owner sets root_with_entries.
	MosaicList* (*root_replace)(void);

	// Ordering a list. After the launcher sorts a folder its own way, it
	// sorts it again with every plugin's sort_compare, in load order: for
	// each pair, the first plugin to return non-zero (negative for a
	// first, positive for b first) decides, and when none has an opinion
	// the pair keeps the launcher's order. Return 0 for lists you don't
	// care about. Recents and collections are passed too, in their own
	// order.
	int (*sort_compare)(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path);

	// Sees a whole list once it's filtered, named and sorted, before it's
	// shown, so it can rename entries with host->entry_set_name based on
	// their siblings, such as titles that collide. Plugins run in load
	// order and each sees the names the last one set. If any name changed,
	// the list is sorted again by name and then by sort_compare, as for
	// display_name, except recents and collections, which keep their
	// order. It can't add, remove or reorder entries.
	void (*list_ready)(const MosaicList* list, const char* dir_path);

	// With root_replace: 1 runs plugins' root_entries on the replaced root,
	// in load order and after the entries root_replace returned, as for
	// the stock root (the owner's own root_entries too, if it has one); 0,
	// the default and what older plugins get, leaves them out.
	int root_with_entries;

	// Repairing recents. When a line in recent.txt names a file that's no
	// longer on the card, which stock drops, this can write the full path
	// it moved to and return 1, or return 0. The first plugin to answer
	// wins. The new path is used only when it's on the card (a game or pak) and
	// isn't a recent already; recents are saved with it, keeping the line's
	// place and name. It runs whenever recents load: at start, and when the
	// lists reload.
	int (*repair_path)(const char* missing, char* out, size_t size);

	// The layout slot. A plugin with slots=layout in its plugin.txt draws
	// the game list in place of the built-in list. Only the slot's owner is
	// called, and only for a folder with entries. The launcher still owns
	// the selection, scroll window and letter index, its button hints and
	// status pill, and every button the layout leaves alone, so navigation
	// and actions work as with the built-in list.
	//
	// layout_init runs once, before the layout's first input or render,
	// with the first view; return 0 to take the game list, or anything
	// else to leave it to the built-in list until the launcher restarts.
	//
	// layout_input runs each frame before the game list's own input, with
	// the buttons and the view. It moves the selection with
	// host->select_index rather than keeping a selection of its own, and
	// sets *consumed (0 on entry) to the buttons it handled, as
	// MOSAIC_BUTTON_BIT masks, which the game list then ignores for that
	// frame, eg. a grid takes the d-pad and leaves A, B and the shoulders.
	//
	// layout_render draws the list on screen, a surface lent for the call,
	// over the background and the status pill and clipped to view->area,
	// which leaves out the button hints. It only draws: select_index
	// returns -1 during it. It runs on frames the launcher redraws; call
	// host->request_redraw from tick to animate.
	//
	// Each returns 0, or anything else for an error, after which the
	// launcher logs it and draws the built-in list from that frame on,
	// until it restarts.
	int (*layout_init)(const MosaicListView* view);
	int (*layout_input)(const MosaicInput* input, const MosaicListView* view, unsigned int* consumed);
	int (*layout_render)(MosaicSurface* screen, const MosaicListView* view);

	// Row decorators, for badges and captions. Every plugin's decorate_row
	// runs, in load order, for each row the built-in list draws, after its
	// name, and for each cell a plugin layout passes to host->decorate_cell.
	// It draws on screen, a surface lent for the call and clipped to
	// row->rect, which on the built-in list's top row stops short of the
	// status pill and header widgets. On the built-in list the selected
	// row's pill is drawn under the screen and its scrolling name over it,
	// so a badge on it stays in view outside row->name. It only draws:
	// select_index returns -1 during it. Return 0, or anything else for an
	// error, after which the launcher logs it and stops calling this
	// plugin's decorate_row until it restarts.
	int (*decorate_row)(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRow* row);

	// Header widgets, beside the status pill (clock, battery, wifi) on the
	// game list, game switcher, quick menu, dialogs and plugin screens
	// without MOSAIC_SCREEN_FULL. Each frame the launcher redraws, it asks
	// each plugin with both hooks, in load order, how wide its widget is,
	// given the room left (max_width) and the pill's height, and lays them
	// out right to left from the pill: the first in load order next to it.
	// A widget is cut to max_width, and together they take at most half the
	// header left of the pill.
	// header_width returns the width, 0 for no widget this frame, or below
	// 0 for an error. header_widget then draws on screen, lent for the call
	// and clipped to rect, which is that wide and as tall and high as the
	// pill, and returns 0. It only draws, as decorate_row does. After an
	// error from either, the launcher logs it and leaves this plugin's
	// widget out until it restarts. Call host->request_redraw from tick to
	// update one, such as a clock.
	int (*header_width)(int max_width, int height);
	int (*header_widget)(MosaicSurface* screen, const MosaicRect* rect);

	// Selection events, for companion screens and previews. Runs, in load
	// order, once each time the game list's selection settles on another
	// entry: after it has gone host->selection_settle_ms without moving and
	// no d-pad or shoulder button is held, so scrolling through a list gives
	// one call at the end rather than one a row. The selection the launcher
	// starts on counts once it settles, and so does a folder's when it's
	// opened, or backed out to; a reload that keeps the same entry selected
	// doesn't. entry is the selected entry, or NULL in an empty folder; don't
	// keep it, as it goes with its list. It runs on the main thread after
	// the frame's input and before it's drawn, where what tick can call
	// works too, except select_index, which returns -1 during it so a plugin
	// can't keep moving the selection from its own events; move it from tick
	// or an action instead. Return 0, or anything else for an error, after
	// which the launcher logs it and stops calling this plugin's
	// selection_changed until it restarts.
	int (*selection_changed)(const MosaicEntry* entry);

	// Transition styles. A plugin with slots=transition in its plugin.txt
	// picks how folders push and pop and how the game switcher comes in,
	// and one with slots=launch how a game or pak starts; one plugin can
	// own both. Only the slot's owner is asked, each time one of its kinds
	// runs. Input waits until a transition ends, as with stock's.
	//
	// transition_begin gets the old and new frames, lent read-only until
	// the transition ends, and returns an enum MosaicTransitionStyle. For
	// MOSAIC_STYLE_CUSTOM it can set duration_ms and ease first, and then
	// transition_frame draws each frame on screen, a screen-sized surface
	// lent for the call and filled with opaque black before it, at
	// progress from 0 to 1 along the ease. The launcher keeps the time:
	// frames come about 60 a second, until duration_ms has passed by the
	// clock, however long each takes to draw, and the last has progress
	// exactly 1. A duration_ms of 0 draws that one frame. They only draw:
	// select_index returns -1 during them.
	//
	// Any other value from transition_begin, MOSAIC_STYLE_CUSTOM without a
	// transition_frame, or a transition_frame that returns anything but 0
	// is an error: the launcher logs it, draws stock's transition in its
	// place, and stops asking this plugin until it restarts.
	int (*transition_begin)(MosaicTransition* transition);
	int (*transition_frame)(MosaicSurface* screen, const MosaicTransition* transition, float progress);

	// Generated art, such as a title card for a game with none. When no
	// file answers for an entry's kind (an enum MosaicArtKind): no plugin's
	// art_path names one that exists and the launcher's own places have
	// none, so real art always comes first. The launcher asks each plugin
	// with art_surface, in load order, and the first to return a surface
	// wins. It's one the plugin made with surface_new, image_load or
	// surface_scale, and the launcher takes it: it moves into the art
	// cache, off the plugin's 32 MiB and into the cache's, read-only from
	// then on, and the plugin must not use or free it again. art_get hands
	// it out like loaded art, and the built-in list shows the first of a
	// thumbnail, box, 3D box or screenshot one when a game has no art file.
	// art_find still reports files only. Return NULL for no art; the
	// launcher asks again only once the cache has dropped the answer, such
	// as when the lists reload. It runs on the main thread while a frame is
	// drawn, so keep it quick. It only draws: select_index returns -1, and
	// art_get gives no generated art, during it. Returning a surface the
	// plugin didn't make, or one already handed over, is an error: the
	// launcher logs it and stops asking this plugin until it restarts.
	MosaicSurface* (*art_surface)(const MosaicEntry* entry, int kind);

	// The background slot. A plugin with slots=background in its plugin.txt
	// draws what's behind the game list, such as the selected game's art.
	// Each frame the game list redraws, before the list draws, the owner's
	// background_render gets screen, a screen-sized surface lent for the
	// call and filled with what stock shows there (the folder's background,
	// or the theme's background colour), and the selected entry, or NULL in
	// an empty folder. What it draws goes on the screen's background layer
	// in place of stock's, under the list, the selection pill, the
	// thumbnail and a plugin layout, and transitions start from it. It only
	// draws: select_index returns -1 during it. It returns 0, or anything
	// else for an error, after which the launcher logs it and shows stock's
	// background until it restarts. Other screens keep stock's background.
	int (*background_render)(MosaicSurface* screen, const MosaicEntry* selected);

	// Hiding names, for entries whose art stands in for them, such as a
	// folder shown as its cover. Asked once for each entry each time a
	// folder is listed, after list_ready, when its names and order are
	// final: return 1 to hide the entry's name, or 0. Plugins are asked in
	// load order, and the first to return 1 hides it. dir_path is the
	// folder being listed. The built-in list leaves a hidden name out while
	// Show game art is on: the row is blank, and when it's selected there's
	// no pill or scrolling name, only its art beside the list. A selected
	// entry with no art to show keeps its name, so there's always a way to
	// tell where the selection is. Layouts get the answers in MosaicListView's
	// names_hidden and decorators in MosaicRow's name_hidden. It's a list
	// hook: it can't rename entries or move the selection.
	int (*hide_name)(const MosaicEntry* entry, const char* dir_path);

	// Art decorators, for details under the game's art, such as its year
	// and publisher. Every plugin's decorate_art runs, in load order, each
	// frame the built-in list draws its thumbnail beside the list: once the
	// selected entry's image is in, so a frame still loading it has none.
	// art is where the thumbnail is, and below the free space under it: the
	// art's column, as wide as the widest thumbnail, from the art's bottom
	// to the button hints. It isn't called when no thumbnail shows (Show
	// game art off, an entry with no art, an empty folder) or there's no
	// room under it. Under a plugin layout it runs for the art the layout
	// passes to host->decorate_art. It draws on screen, a surface lent for
	// the call and clipped to below, over the background and beside the
	// thumbnail, which never covers it. It only draws: select_index returns
	// -1 during it. Return 0, or anything else for an error, after which the
	// launcher logs it and stops calling this plugin's decorate_art until it
	// restarts.
	int (*decorate_art)(MosaicSurface* screen, const MosaicEntry* entry, const MosaicRect* art, const MosaicRect* below);

	// Input filters, such as an input lock. Every plugin's input_filter
	// runs, in load order, each frame on every screen (the game list, the
	// game switcher, the quick menu, dialogs and plugin screens), right
	// after the launcher reads the pad and before tick, power, the FN
	// buttons and the screen's own input, a layout's layout_input
	// included. input has every button in enum MosaicButton, MENU, the
	// power button and the FN buttons too, less what filters before it
	// took. It sets *consumed (0 on entry) to the buttons to take, as
	// MOSAIC_BUTTON_BIT masks, which are taken off pressed, repeated and
	// released for the rest of the frame; held stays
	// as the pad has it. A taken power press doesn't sleep, but held for a
	// second it still powers off, as with stock, whatever filters take
	// (and, as stock, not for a press in the first second after the
	// launcher starts or wakes). Take the power button on its press: once
	// a press has got through, its release does too. The volume keys, and MENU with them for brightness,
	// are stock's keymon's, which changes them outside the launcher, so no
	// filter can stop those. The Launcher pak's settings screen loads no
	// plugins. Return 0, or anything else for an error, after which the
	// launcher logs it, takes nothing for that call and stops calling this
	// plugin's input_filter until it restarts. Check for it with
	// MOSAIC_HOST_HAS(host, hold_switch); an older launcher never calls it.
	int (*input_filter)(const MosaicInput* input, unsigned int* consumed);

	// Overlays, such as a now playing pill. Every plugin's overlay_render
	// runs, in load order, each frame the launcher redraws the game list,
	// the quick menu, the game switcher or a plugin screen, on overlay, a
	// clear screen-sized surface lent for the call, which then goes over
	// everything on screen, later plugins' overlays over earlier ones'. It
	// isn't called over dialogs, plugin screens with MOSAIC_SCREEN_FULL,
	// transitions or a launch. view->screen says what's under it. Overlays
	// get no input: take buttons with input_filter. Call
	// host->request_redraw from tick to change one. It only draws, as
	// decorate_row does. Return 0, or anything else for an error, after
	// which the launcher logs it and leaves this plugin's overlay out until
	// it restarts. Check for it with MOSAIC_HOST_HAS(host, open_screen).
	int (*overlay_render)(MosaicSurface* overlay, const MosaicScreenView* view);

	// The strings slot. A plugin with slots=strings in its plugin.txt
	// translates the launcher's text, and plugins' through host->tr.
	// translate gets the id of the plugin whose text it is, or NULL for
	// the launcher's own strings, the context, the launcher's or the one
	// the plugin passed to host->tr (NULL for none), and the English text.
	// host->plugin_info says where each plugin's files are.
	// translate_plural gets both English forms and
	// the count, and returns the form the language's plural rules pick for
	// n. Return NULL for text you have no translation of, which then shows
	// in English. The host copies what's returned, so it only has to last
	// until the call returns, and asks once for each text (and each n),
	// on the main thread, as the text is first drawn. A translation is
	// refused, and the English shown, when it's empty, over 1023 bytes,
	// has a line over 255 bytes or more than 8 lines, or has different
	// printf conversions from its English (for translate_plural, from
	// either form); one that isn't valid UTF-8 is an error, after which
	// the launcher logs it and shows English for everything until it
	// restarts. Without translate_plural, translate is asked for the form
	// English rules pick. Check for them with MOSAIC_HOST_HAS(host, tr).
	const char* (*translate)(const char* plugin_id, const char* context, const char* text);
	const char* (*translate_plural)(const char* plugin_id, const char* context, const char* singular, const char* plural, unsigned long n);

	// The theme slot. A plugin with slots=theme in its plugin.txt supplies
	// the launcher's colours, font and fallback font, text size, UI sizes,
	// wallpapers and hint style, and keys other plugins read with
	// host->theme_value: theme gets a MosaicTheme with every field at stock's value, sets the
	// ones it changes and returns 0. Nothing it sets is written to stock's
	// settings. The launcher asks once every plugin has loaded, before the
	// first list is built, and again each time the lists reload. A value
	// out of range, a path that isn't a regular file on the card, a font
	// that won't open and sizes that leave the list fewer than 3 rows are
	// refused one by one, logged, and stock's used in their place. A
	// wallpaper only shows where the card has no stock background of its
	// own. A non-zero return is an error: the launcher logs it, puts
	// stock's theme back and doesn't ask again until it restarts. Check
	// for it with MOSAIC_HOST_HAS(host, ui_size).
	int (*theme)(MosaicTheme* theme);

	// The strings slot's fallback font. The launcher asks the slot's owner
	// for the full path of a TrueType or OpenType font for the characters
	// the UI font has no glyphs for (such as a CJK font for its language),
	// or NULL for none, when it asks for the theme and again each time the
	// lists reload. Text is then measured and drawn stretch by stretch:
	// each goes to the UI font when it has the glyphs, otherwise to the
	// fallback font at the same size, with their baselines lined up. Text
	// the UI font has every glyph of is drawn exactly as without one. A
	// theme's fallback_font wins over this one. A path that isn't a
	// regular file on the card (symlinks may point anywhere on it), or a
	// font that won't open, is refused and logged, and none is used. An
	// older launcher never asks.
	const char* (*fallback_font)(void);

	// The lock screen's lines (see lock in MosaicHost), such as what's
	// playing. Every plugin's lock_text runs, in load order, each time the
	// lock screen draws, and writes up to 3 lines, separated by '\n', into
	// out (size bytes, empty on entry), which show in the small font under
	// the time, each cut to fit. Call host->request_redraw from tick to
	// change them. It returns 0, or anything else for an error, after
	// which the launcher logs it and leaves this plugin's lines out until
	// it restarts. Check for it with MOSAIC_HOST_HAS(host, register_ambient).
	int (*lock_text)(char* out, size_t size);

	// A note on the Launcher pak's page of the plugin plugin_id, such as a
	// warning about its settings. The pages load only the strings and
	// theme slots' owners, and ask each of them that has settings_note,
	// in load order, each time a plugin's page is drawn again: it writes
	// one line into out (size bytes, empty on entry), or leaves it empty
	// for none, and returns 0. Each note shows under the page's Status
	// row in the small font, as it's given (translate it with host->tr
	// first), cut to fit.
	// Anything but 0 is an error: the launcher logs it and leaves this
	// plugin's notes out until the pages close. The launcher itself never
	// asks. Check for it with MOSAIC_HOST_HAS(host, font_missing).
	int (*settings_note)(const char* plugin_id, char* out, size_t size);
} MosaicPlugin;

#define MOSAIC_PLUGIN_ENTRY "mosaic_plugin_get"
typedef const MosaicPlugin* (*MosaicPluginGet)(const MosaicHost* host);

#ifdef __cplusplus
}
#endif

#endif
