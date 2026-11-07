#include "launcher.h"

char* recent_alias = NULL;
Directory* top;
Array* stack; // DirectoryArray
Array* recents; // RecentArray
Array *quick; // EntryArray
Array *quickActions; // EntryArray

int quit = 0;
int can_resume = 0;
int should_resume = 0; // set to 1 on BTN_RESUME but only if can_resume==1
int has_preview = 0;
int simple_mode = 0;
int switcher_selected = 0;
char slot_path[256];
char preview_path[256];
int animationdirection = 0;

int restore_depth = -1;
int restore_relative = -1;
int restore_selected = 0;
int restore_start = 0;
int restore_end = 0;
int startgame = 0;
int dirty = 1;
int previous_row = 0;
int previous_depth = 0;
