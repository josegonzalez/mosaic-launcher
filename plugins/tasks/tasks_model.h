#ifndef TASKS_MODEL_H
#define TASKS_MODEL_H

// The tasks plugin's SDL-free half: finding the scripts in Tasks/, which
// games a game task is for, and what a finished task's output says.
// tasks_run.h runs them.
//
// Tasks/*.sh run with no game, from the Tasks folder. Tasks/Game/*.sh are
// for every game, and Tasks/Game/<tag>/*.sh only for the games of the
// system with that tag, such as GB for Roms/Game Boy (GB).

#include <stddef.h>

#define TK_PATH_MAX 1024
#define TK_NAME_MAX 256
#define TK_TAG_MAX 64
#define TK_MAX 64 // tasks of each kind

typedef struct TKTask {
	char path[TK_PATH_MAX]; // the script's full path
	char name[TK_NAME_MAX]; // its file name without .sh, as the menus show it
	char tag[TK_TAG_MAX];   // the system tag a game task is limited to, or ""
} TKTask;

typedef struct TKList {
	TKTask items[TK_MAX];
	int count;
} TKList;

// 1 when a file name is a task's: ends in .sh, with a name before it, and
// isn't hidden (no leading dot, so no macOS ._ files either).
int TK_isTask(const char* file_name);

// Lists the tasks in dir (its scripts, not its folders) into list, after
// any already there, each with tag, A-Z by name ignoring case. Returns how
// many it added; a missing dir adds none.
int TK_scanDir(const char* dir, const char* tag, TKList* list);

// Lists tasks_dir's own scripts into global, and Tasks/Game's and each
// Tasks/Game/<tag>/'s into game, A-Z by name and then tag. Either list can
// be NULL to skip it. Empties both first.
void TK_scan(const char* tasks_dir, TKList* global, TKList* game);

// The system tag of a game at path, such as GB for
// <sdcard>/Roms/Game Boy (GB)/Tetris.gb or a game in a folder below it,
// into out, "" when its system folder has none. Returns 1 for a path under
// a system folder in Roms, or 0 (out "") for anything else, the system
// folder itself among them.
int TK_systemTag(const char* sdcard, const char* path, char* out, size_t size);

// 1 when path is a game a game task can be run for: a file under a system
// folder in Roms, or a folder there holding a .m3u or .cue of its own name,
// as a multi-disc game is.
int TK_isGame(const char* sdcard, const char* path);

// 1 when a game task is for games of the system with tag.
int TK_appliesTo(const TKTask* task, const char* tag);

// The last line of text (len bytes) with anything but spaces in it, trimmed
// of spaces at both ends, into out, cut at a character's boundary to fit,
// or "" when it has none.
void TK_lastLine(const char* text, size_t len, char* out, size_t size);

// The name of a task's log file, without its folder or .txt: the script's
// path under tasks_dir without .sh, with each / as a -, such as
// Game-GB-List saves for Tasks/Game/GB/List saves.sh. "" for a script
// outside tasks_dir.
void TK_logName(const char* tasks_dir, const char* script, char* out, size_t size);

// The environment a task runs with: from's (the launcher's), without
// LD_PRELOAD and DYLD_INSERT_LIBRARIES, so libraries preloaded into the
// launcher aren't loaded into it, and without any variable set names, then
// set's "NAME=value" strings. Fills out with up to max - 1 pointers into
// from and set and a NULL, and returns how many it put before the NULL.
int TK_envBuild(char* const* from, const char* const* set, int set_count, char** out, int max);

#endif
