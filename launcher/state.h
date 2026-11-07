#ifndef STATE_H
#define STATE_H

// Launcher-wide state shared between modules. Defined in state.c.

#include "containers.h"
#include "library.h"

extern char* recent_alias; // yiiikes

extern Directory* top;
extern Array* stack; // DirectoryArray
extern Array* recents; // RecentArray
extern Array* quick; // EntryArray
extern Array* quickActions; // EntryArray

extern int quit;
extern int can_resume;
extern int should_resume; // set to 1 on BTN_RESUME but only if can_resume==1
extern int has_preview;
extern int simple_mode;
extern int switcher_selected;
extern char slot_path[256];
extern char preview_path[256];
extern int animationdirection;

extern int restore_depth;
extern int restore_relative;
extern int restore_selected;
extern int restore_start;
extern int restore_end;
extern int startgame;

extern int dirty;
extern int previous_row;
extern int previous_depth;

#endif
