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
	int owner;               // the loaded plugin's index
	MosaicScreenHooks hooks; // the plugin's, copied; fields past its size are zero
	int off;                 // failed, so it can't open until the launcher restarts
} PluginScreen;

typedef struct ScreenSet {
	PluginScreen screens[SCREENS_MAX];
	int count;
	int stack[SCREEN_STACK_MAX]; // indexes into screens, the bottom first
	int depth;
	int base; // the built-in screen under the stack, the caller's id for it
} ScreenSet;

void ScreenSet_clear(ScreenSet* set);

// Why ScreenSet_add refused, or 0.
enum ScreenAddResult {
	SCREEN_ADDED = 0,
	SCREEN_BAD_ID = -1,    // NULL, empty, too long, or a built-in screen's name
	SCREEN_BAD_HOOKS = -2, // NULL, too small to have render, or no render
	SCREEN_TAKEN = -3,     // another screen has the id
	SCREEN_FULL = -4,      // SCREENS_MAX already
};

// Registers a screen for owner, copying the part of hooks its size says
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

#endif
