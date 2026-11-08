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
 * only on its main thread.
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
	// tg5050); a device without one never presses it.
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

///////////////////////////////////////
// What the launcher offers plugins.

typedef struct MosaicHost {
	size_t size;     // sizeof(MosaicHost) in the launcher
	int api_version; // MOSAIC_API_VERSION the launcher was built with

	const char* platform;      // eg. "tg5040"
	const char* sdcard_path;   // eg. "/mnt/SDCARD"
	const char* userdata_path; // eg. "/mnt/SDCARD/.userdata/tg5040"

	void (*log)(const char* plugin_id, const char* message);

	// Per-plugin settings in .userdata/<platform>/launcher/<id>.cfg.
	// cfg_get returns fallback when the key isn't set. The returned string
	// stays valid until the next cfg_set for that plugin.
	const char* (*cfg_get)(const char* plugin_id, const char* key, const char* fallback);
	void (*cfg_set)(const char* plugin_id, const char* key, const char* value);

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
	// For now only MOSAIC_SCREEN_GAMELIST accepts bindings.
	int (*register_action)(const char* id, const char* label,
		int (*available)(void* data), void (*run)(void* data), void* data);
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
	// corners, and with art decorators under it. The launcher doesn't take
	// it: the plugin can keep drawing into it or writing into it with
	// surface_write, and calls art_replace again with the same surface to
	// show what it has now, which redraws only the thumbnail, not the list.
	// Never call request_redraw for each frame: it redraws the whole list.
	// NULL takes the plugin's surface away again. It goes away by itself
	// when the selection moves, the game list is left, the surface is
	// freed or no longer the plugin's, or the plugin quits; after that the
	// next call shows it afresh, for whatever is selected then. Only one
	// plugin's surface shows at a time: the first keeps the place until it
	// goes. Returns 0, also for NULL, or -1 for a NULL id, a surface the
	// plugin didn't make or no longer owns, another plugin's surface
	// showing, from init or a hook that only draws, or when the built-in
	// list shows no thumbnail there: off the game list, in an empty folder,
	// with Show game art off or under a plugin layout. Main thread only.
	// Check for it with MOSAIC_HOST_HAS(host, art_replace).
	int (*art_replace)(const char* plugin_id, MosaicSurface* surface);
} MosaicHost;

// What the play history calls return when the history can't be read.
#define MOSAIC_PLAY_UNKNOWN -1

// What ask_pin's on_done gets.
#define MOSAIC_PIN_RIGHT 1
#define MOSAIC_PIN_WRONG 0
#define MOSAIC_PIN_CANCELLED -1

// What the RetroAchievements calls return until a game's hash is worked out.
#define MOSAIC_ACHIEVEMENTS_UNKNOWN -1

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
	// game list, game switcher, quick menu and dialogs. Each frame the
	// launcher redraws, it asks each plugin with both hooks, in load order,
	// how wide its widget is, given the room left (max_width) and the
	// pill's height, and lays them out right to left from the pill: the
	// first in load order next to it. A widget is cut to max_width, and
	// together they take at most half the header left of the pill.
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
	// game switcher, the quick menu and dialogs), right after the launcher
	// reads the pad and before tick, power, the FN buttons and the screen's
	// own input, a layout's layout_input included. input has every button
	// in enum MosaicButton, MENU, the power button and the FN buttons too,
	// less what filters before it took. It sets *consumed (0 on entry) to
	// the buttons to take, as MOSAIC_BUTTON_BIT masks, which are taken off
	// pressed, repeated and released for the rest of the frame; held stays
	// as the pad has it. A taken power press doesn't sleep, but held for a
	// second it still powers off, as with stock, whatever filters take. Take
	// the power button on its press: once a press has got through, its
	// release does too. The volume keys, and MENU with them for brightness,
	// are stock's keymon's, which changes them outside the launcher, so no
	// filter can stop those. The Launcher pak's settings screen loads no
	// plugins. Return 0, or anything else for an error, after which the
	// launcher logs it, takes nothing for that call and stops calling this
	// plugin's input_filter until it restarts. Check for it with
	// MOSAIC_HOST_HAS(host, hold_switch); an older launcher never calls it.
	int (*input_filter)(const MosaicInput* input, unsigned int* consumed);
} MosaicPlugin;

#define MOSAIC_PLUGIN_ENTRY "mosaic_plugin_get"
typedef const MosaicPlugin* (*MosaicPluginGet)(const MosaicHost* host);

#ifdef __cplusplus
}
#endif

#endif
