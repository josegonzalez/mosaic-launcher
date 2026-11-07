#ifndef NAV_H
#define NAV_H

// The directory stack: opening and closing folders, restoring the last
// position, and acting on a selected entry.

#include <stdbool.h>
#include "library.h"

bool isDirectSubdirectory(const Directory* parent, const char* child_path);
Array* pathToStack(const char* path);
void openDirectory(char* path, int auto_launch);
void closeDirectory(void);
void toggleQuick(Entry* self);
void Entry_open(Entry* self);
void saveLast(char* path);
void loadLast(void); // call after loading root directory
void QuickMenu_init(void);
void QuickMenu_quit(void);
void Menu_init(void);
void Menu_quit(void);
// Lists the open folders again; main runs it when state.reload is set.
void Menu_reload(void);
// Opens a game, pak or folder by path as if it were selected (host->open_path).
int Entry_openPath(const char* path, int type);

#endif
