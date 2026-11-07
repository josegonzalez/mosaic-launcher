#ifndef LAUNCH_H
#define LAUNCH_H

// Handing off to a game or pak: the /tmp/next command, resume state, and
// the user-assignable FN buttons.

#include "library.h"

void queueNext(char* cmd);
int replaceString(char* line, const char* search, const char* replace);
char* escapeSingleQuotes(char* str);
void readyResumePath(char* rom_path, int type);
void readyResume(Entry* entry);
int autoResume(void);
// Both return 1 when the launch was queued, 0 when a plugin cancelled it.
int openPak(char* path);
int runFnAction(int index); // 0 == FN1, 1 == FN2. Returns 1 if something was launched.
int openRom(char* path, char* last);

#endif
