/*
 * nextui_plugin.h - the plugin API for nextui-launcher.
 *
 * MIT License. Copyright (c) 2026 Jose Diaz-Gonzalez. See sdk/LICENSE.
 *
 * This header is the only thing a plugin includes. It doesn't depend on
 * NextUI's headers, so plugins can use any license.
 *
 * A plugin is a shared object exporting one function:
 *
 *     const NUIPlugin* nextui_plugin_get(const NUIHost* host);
 *
 * It returns a description of the plugin with whichever hooks it
 * implements; every hook is optional. The launcher calls into the plugin
 * only on its main thread.
 *
 * Both structs begin with their own size, so a launcher and a plugin built
 * against different versions of this header can tell which fields the
 * other has. Fields are only ever added at the end.
 */

#ifndef NEXTUI_PLUGIN_H
#define NEXTUI_PLUGIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUI_API_VERSION 1

///////////////////////////////////////
// Things the launcher owns. Plugins only hold pointers to them.

typedef struct NUIEntry NUIEntry; // one row in a list: a folder, game, pak or toggle
typedef struct NUIList NUIList;   // an ordered list of entries

enum NUIEntryType {
	NUI_ENTRY_DIR,
	NUI_ENTRY_PAK,
	NUI_ENTRY_ROM,
	NUI_ENTRY_TOGGLE,
};

// Screens a binding can target.
enum NUIScreen {
	NUI_SCREEN_GAMELIST,
	NUI_SCREEN_SWITCHER,
	NUI_SCREEN_QUICKMENU,
};

// Buttons a binding can use.
enum NUIButton {
	NUI_BUTTON_A,
	NUI_BUTTON_B,
	NUI_BUTTON_X,
	NUI_BUTTON_Y,
	NUI_BUTTON_L1,
	NUI_BUTTON_R1,
	NUI_BUTTON_L2,
	NUI_BUTTON_R2,
	NUI_BUTTON_START,
	NUI_BUTTON_SELECT,
};

enum NUITrigger {
	NUI_TRIGGER_PRESS,
	NUI_TRIGGER_RELEASE,
};

// Images art_path can supply.
enum NUIArtKind {
	NUI_ART_THUMBNAIL, // a game's art beside the list
	NUI_ART_FOLDER_BG, // the background while a folder is selected
	NUI_ART_LIST_BG,   // the background while a game in a folder is selected
};

// The quick menu's rows a plugin tile can go in.
enum NUIQuickRow {
	NUI_QUICK_TOP,    // the large labelled tiles: Recents, Games, Tools
	NUI_QUICK_BOTTOM, // the small icons: Settings, Wifi, Sleep, Power off
};

// What pre_launch decides. NUI_LAUNCH_REPLACE runs the command set with
// host->replace_launch in place of the launch; a launcher older than that
// field launches as usual.
enum NUILaunchResult {
	NUI_LAUNCH_CONTINUE,
	NUI_LAUNCH_CANCEL,
	NUI_LAUNCH_REPLACE,
};

typedef struct NUILaunch {
	const char* path; // the rom or pak being launched
	int type;         // NUI_ENTRY_ROM or NUI_ENTRY_PAK
} NUILaunch;

///////////////////////////////////////
// What the launcher offers plugins.

typedef struct NUIHost {
	size_t size;     // sizeof(NUIHost) in the launcher
	int api_version; // NUI_API_VERSION the launcher was built with

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
	NUIEntry* (*entry_new)(const char* path, int type, const char* name);
	const char* (*entry_path)(const NUIEntry* entry);
	const char* (*entry_name)(const NUIEntry* entry);
	int (*entry_type)(const NUIEntry* entry);

	NUIList* (*list_new)(void);
	int (*list_count)(const NUIList* list);
	NUIEntry* (*list_get)(const NUIList* list, int index);
	void (*list_push)(NUIList* list, NUIEntry* entry);

	// What the user is looking at. Either can be NULL.
	const NUIEntry* (*selected_entry)(void);
	const char* (*current_path)(void);

	void (*request_redraw)(void);

	// Actions are named things the user can do; bindings put them on
	// buttons. Plugin bindings rank after the screen's own, so they can
	// only use buttons the screen leaves free. Both return 0 on success.
	// For now only NUI_SCREEN_GAMELIST accepts bindings.
	int (*register_action)(const char* id, const char* label,
		int (*available)(void* data), void (*run)(void* data), void* data);
	int (*bind_button)(int screen, int button, int trigger, const char* action_id);

	// Fields below were added after the first release; check for them with
	// NUI_HOST_HAS before calling.

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

	// Opens a game, pak or folder (an enum NUIEntryType) the way selecting
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
	int (*entry_set_name)(NUIEntry* entry, const char* name);

	// The list the user is looking at, read with list_count and list_get,
	// or NULL before the lists exist. Don't keep it: it's freed when the
	// user leaves the folder or the lists reload. selected_index returns
	// the selected row in it, or -1 when it's empty. select_index moves
	// the selection to a row, scrolling to keep it in view, and returns 0,
	// or -1 for a row that isn't there. Call them from an action or tick,
	// not from a list hook, which may be listing another folder.
	const NUIList* (*current_list)(void);
	int (*selected_index)(void);
	int (*select_index)(int index);

	// Quick menu tiles. quick_tile puts a registered action on the quick
	// menu as a tile in row (an enum NUIQuickRow), after the built-in ones:
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
	// NUI_SCREEN_GAMELIST takes overrides.
	int (*override_button)(int screen, int button, int trigger, const char* action_id);

	// Called from pre_launch, sets the command to run instead of the
	// launch, then pre_launch returns NUI_LAUNCH_REPLACE. program is a file
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
} NUIHost;

// True when the launcher's host table has this field, so a plugin can use
// newer features when they're there and do without on older launchers.
#define NUI_HOST_HAS(host, field) \
	((host)->size >= offsetof(NUIHost, field) + sizeof((host)->field) && (host)->field)

///////////////////////////////////////
// What a plugin offers the launcher.

typedef struct NUIPlugin {
	size_t size;     // sizeof(NUIPlugin) in the plugin
	int api_version; // NUI_API_VERSION the plugin was built with
	const char* id;  // must match id= in plugin.txt

	// Return 0 to load, anything else to be skipped. quit runs at exit.
	int (*init)(const NUIHost* host);
	void (*quit)(void);

	// Lists. root_entries adds to the top-level list, after the consoles
	// and before Tools. virtual_dir
	// returns a list (from host->list_new) for a path the plugin owns, or
	// NULL to let the launcher list it. filter returns 0 to hide an entry.
	// display_name writes a new name and returns 1, or returns 0.
	void (*root_entries)(NUIList* root);
	NUIList* (*virtual_dir)(const char* path);
	int (*filter)(const NUIEntry* entry, const char* dir_path);
	int (*display_name)(const NUIEntry* entry, char* out, size_t size);

	// Launching. pre_launch runs before a game or pak starts and can cancel
	// it; post_return runs when the launcher starts again afterwards.
	int (*pre_launch)(const NUILaunch* launch);
	void (*post_return)(void);

	// Called every frame with SDL_GetTicks().
	void (*tick)(unsigned long now);

	// Power.
	void (*before_sleep)(void);
	void (*after_resume)(void);
	void (*before_poweroff)(void);

	// Art. Writes the image to use for an entry (an enum NUIArtKind) and
	// returns 1, or returns 0 to let the launcher look in its usual place.
	// The first plugin to answer wins. The image doesn't have to exist; the
	// launcher falls back as it would for its own path.
	int (*art_path)(const NUIEntry* entry, int kind, char* out, size_t size);

	// The root slot. A plugin with slots=root in its plugin.txt can return
	// the whole top-level list (from host->list_new) in place of the stock
	// one, or NULL to keep stock's. Only the slot's owner is asked. Other
	// plugins' root_entries are added to a replaced root only when the
	// owner sets root_with_entries.
	NUIList* (*root_replace)(void);

	// Ordering a list. After the launcher sorts a folder its own way, it
	// sorts it again with every plugin's sort_compare, in load order: for
	// each pair, the first plugin to return non-zero (negative for a
	// first, positive for b first) decides, and when none has an opinion
	// the pair keeps the launcher's order. Return 0 for lists you don't
	// care about. Recents and collections are passed too, in their own
	// order.
	int (*sort_compare)(const NUIEntry* a, const NUIEntry* b, const char* dir_path);

	// Sees a whole list once it's filtered, named and sorted, before it's
	// shown, so it can rename entries with host->entry_set_name based on
	// their siblings, such as titles that collide. Plugins run in load
	// order and each sees the names the last one set. If any name changed,
	// the list is sorted again by name and then by sort_compare, as for
	// display_name, except recents and collections, which keep their
	// order. It can't add, remove or reorder entries.
	void (*list_ready)(const NUIList* list, const char* dir_path);

	// With root_replace: 1 runs plugins' root_entries on the replaced root,
	// in load order and after the entries root_replace returned, as for
	// the stock root (the owner's own root_entries too, if it has one); 0,
	// the default and what older plugins get, leaves them out.
	int root_with_entries;
} NUIPlugin;

#define NUI_PLUGIN_ENTRY "nextui_plugin_get"
typedef const NUIPlugin* (*NUIPluginGet)(const NUIHost* host);

#ifdef __cplusplus
}
#endif

#endif
