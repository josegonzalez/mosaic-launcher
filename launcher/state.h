#ifndef STATE_H
#define STATE_H

// Launcher-wide state shared between modules, in one place. Defined in
// state.c. Screen-local state lives in screens.h.

#include "containers.h"
#include "library.h"

typedef struct LauncherState {
	char* recent_alias; // yiiikes

	// what's open
	Directory* top;
	Array* stack; // DirectoryArray
	Array* recents; // RecentArray
	Array* quick; // EntryArray
	Array* quickActions; // EntryArray

	int quit;
	int simple_mode;
	int switcher_selected;

	// resume state for the selected entry, from readyResume()
	int can_resume;
	int should_resume; // set to 1 on BTN_RESUME but only if can_resume==1
	int has_preview;
	char slot_path[256];
	char preview_path[256];

	// where to put the selection back after closing a folder
	int restore_depth;
	int restore_relative;
	int restore_selected;
	int restore_start;
	int restore_end;

	int startgame;
	int animationdirection;
	int dirty;
	int reload; // list the open folders again before the next frame's input
	int rebuild_quick; // build the quick menu's tiles again before the next frame's input
	int previous_row;
	int previous_depth;
} LauncherState;

extern LauncherState state;

#endif
