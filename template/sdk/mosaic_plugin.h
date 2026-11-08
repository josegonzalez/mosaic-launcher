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
};

enum MosaicTrigger {
	MOSAIC_TRIGGER_PRESS,
	MOSAIC_TRIGGER_RELEASE,
	// On the press and again while held, at stock's pace for the d-pad and
	// letter jumps. Only override_button takes it, on a launcher with
	// host->override_repeat.
	MOSAIC_TRIGGER_REPEAT,
};

// Images art_path can supply.
enum MosaicArtKind {
	MOSAIC_ART_THUMBNAIL, // a game's art beside the list
	MOSAIC_ART_FOLDER_BG, // the background while a folder is selected
	MOSAIC_ART_LIST_BG,   // the background while a game in a folder is selected
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

typedef struct MosaicLaunch {
	const char* path; // the rom or pak being launched
	int type;         // MOSAIC_ENTRY_ROM or MOSAIC_ENTRY_PAK
} MosaicLaunch;

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
	// or -1 for a row that isn't there. Call them from an action or tick,
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
} MosaicHost;

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
	// launcher falls back as it would for its own path.
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
} MosaicPlugin;

#define MOSAIC_PLUGIN_ENTRY "mosaic_plugin_get"
typedef const MosaicPlugin* (*MosaicPluginGet)(const MosaicHost* host);

#ifdef __cplusplus
}
#endif

#endif
