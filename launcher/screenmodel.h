#ifndef SCREENMODEL_H
#define SCREENMODEL_H

// Plugins' screens and overlays, without SDL or the loader: the screens
// plugins registered, the stack of those open above a built-in screen,
// and which screens slide and carry overlays. plugins.c calls the hooks;
// screen_plugin.c draws.

#include "mosaic_plugin.h"

#define SCREENS_MAX 32      // plugin screens registered, across every plugin
#define SCREEN_STACK_MAX 8  // plugin screens open at once
#define SCREEN_ID_MAX 64    // an id's length, its terminator included

// What a screen is, for the rules below.
enum ScreenKind {
	SCREENKIND_LIST,      // the game list
	SCREENKIND_QUICKMENU,
	SCREENKIND_SWITCHER,
	SCREENKIND_DIALOG,    // a confirm, message, keyboard or choice dialog
	SCREENKIND_PLUGIN,
};

typedef struct PluginScreen {
	char id[SCREEN_ID_MAX];
	int owner;               // the loaded plugin's index, or -1 for the launcher's lock screen
	MosaicScreenHooks hooks; // the plugin's, copied; fields past its size are zero
	int off;                 // failed, so it can't open until the launcher restarts
} PluginScreen;

typedef struct ScreenSet {
	PluginScreen screens[SCREENS_MAX];
	int count;
	int stack[SCREEN_STACK_MAX]; // indexes into screens, the bottom first
	int depth;
	int base; // the built-in screen under the stack, the caller's id for it
	int home;  // the home slot's screen, or -1
	int under; // the quick menu or game switcher in front opened over the home screen, so goes back to it
	int switcher; // the switcher slot's screen, or -1
} ScreenSet;

void ScreenSet_clear(ScreenSet* set);

// Why ScreenSet_add refused, or 0.
enum ScreenAddResult {
	SCREEN_ADDED = 0,
	SCREEN_BAD_ID = -1,    // NULL, empty, too long, or a built-in screen's name for a plugin
	SCREEN_BAD_HOOKS = -2, // NULL, too small to have render, or no render
	SCREEN_TAKEN = -3,     // another screen has the id
	SCREEN_FULL = -4,      // SCREENS_MAX already
};

// Registers a screen for owner, -1 for the launcher's own, which alone may
// have a built-in screen's name, copying the part of hooks its size says
// it has. Returns an enum ScreenAddResult.
int ScreenSet_add(ScreenSet* set, const char* id, int owner, const MosaicScreenHooks* hooks);

// The registered screen with id, or -1.
int ScreenSet_find(const ScreenSet* set, const char* id);

// Forgets owner's screens, which none of may be open, eg. after its init
// declined. Returns how many.
int ScreenSet_dropOwner(ScreenSet* set, int owner);

// 1 for an id a built-in screen goes by in current_screen.
int Screen_builtinName(const char* id);

// Why a screen can't open, or 0.
enum ScreenOpenResult {
	SCREEN_OPENS = 0,
	SCREEN_UNKNOWN = -1,  // not a registered screen
	SCREEN_IS_OFF = -2,   // failed earlier
	SCREEN_IS_OPEN = -3,  // already on the stack
	SCREEN_STACK_FULL = -4,
};

int ScreenSet_canOpen(const ScreenSet* set, int index);

// Puts a screen on top. The first on an empty stack records base, the
// built-in screen it opened over. Returns an enum ScreenOpenResult.
int ScreenSet_push(ScreenSet* set, int index, int base);

// The screen on top, or -1 with none open.
int ScreenSet_top(const ScreenSet* set);

// Where index is on the stack, from 0 at the bottom, or -1.
int ScreenSet_position(const ScreenSet* set, int index);

// Takes the top screen off and returns it, or -1 with none open.
int ScreenSet_pop(ScreenSet* set);

// 1 when going from one kind of screen to another slides, as opening and
// closing a folder does: never to or from the game switcher, which brings
// itself in, or a dialog, which stock shows at once.
int ScreenKind_slides(int from, int to);

// 1 when plugins' overlays draw over a kind of screen: not over dialogs,
// whose questions they could hide, or a plugin screen with
// MOSAIC_SCREEN_FULL.
int ScreenKind_overlays(int kind, int flags);

// The home slot: one plugin's screen in place of the root list.

// Makes owner's first screen with MOSAIC_SCREEN_HOME the home screen, or
// leaves none. Returns it, or -1.
int ScreenSet_claimHome(ScreenSet* set, int owner);

// 1 while there's a home screen that can open: it hasn't failed.
int ScreenSet_homeReady(const ScreenSet* set);

// 1 while the home screen is open, anywhere on the stack.
int ScreenSet_homeOpen(const ScreenSet* set);

// The home screen's id while it's ready, or NULL: with no home screen, or
// after it failed.
const char* ScreenSet_homeId(const ScreenSet* set);

// What the launcher starts on, given whether the home screen is ready,
// what the last launch left in the home file (NULL without one), and the
// kind of screen it would start on.
enum HomeStart {
	HOME_START_NONE,  // the built-in screen, as stock
	HOME_START_OPEN,  // the home screen, over the game list
	HOME_START_UNDER, // the quick menu or game switcher, going back to the home screen
};
int Home_start(int ready, const char* left, int kind);

// What a launch leaves in the home file: "home" with the home screen open,
// or under the quick menu or game switcher in front, and "list" otherwise.
const char* ScreenSet_leftFrom(const ScreenSet* set);

// After a built-in screen came in front of the stack some other way than
// open_screen and close_screen, such as an FN button: 1 when the open
// screens should close. A quick menu or game switcher over the home screen
// goes back to it once it closes; the game list in front forgets that.
int ScreenSet_coveredBy(ScreenSet* set, int kind);

// The quick menu or game switcher closing back to the game list: 1 when
// the home screen opens in the list's place, as it was under them.
int ScreenSet_backToHome(ScreenSet* set);

// The switcher slot: one plugin's screen in place of the built-in game
// switcher. It opens over the game list as the only screen open, the way
// the built-in one shows, and counts as the game switcher for the rules
// above.

// Makes owner's first screen with MOSAIC_SCREEN_GAME_SWITCHER, other than the
// home screen, the switcher screen, or leaves none. Returns it, or -1.
int ScreenSet_claimSwitcher(ScreenSet* set, int owner);

// 1 while there's a switcher screen that can open: it hasn't failed.
int ScreenSet_switcherReady(const ScreenSet* set);

// 1 while the switcher screen is open.
int ScreenSet_switcherOpen(const ScreenSet* set);

// What kind of screen a registered one is for ScreenKind_slides: the
// switcher screen is SCREENKIND_SWITCHER, any other SCREENKIND_PLUGIN.
int ScreenSet_kind(const ScreenSet* set, int index);

// What shows once closed, the last open screen, has closed.
enum AfterClose {
	AFTER_BASE,     // the built-in screen the stack opened over
	AFTER_LIST,     // the game list, as the built-in switcher goes back to
	AFTER_HOME,     // the home screen, which the switcher opened over
	AFTER_SWITCHER, // the built-in switcher, in place of a switcher screen that failed
};
int ScreenSet_afterClose(ScreenSet* set, int closed);

#endif
