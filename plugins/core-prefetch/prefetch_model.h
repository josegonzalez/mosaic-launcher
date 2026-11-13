#ifndef PREFETCH_MODEL_H
#define PREFETCH_MODEL_H

#include <stddef.h>
#include "mosaic_plugin.h"

// The core prefetch's logic, with no SDL and no host: which emulator pak
// a game launches with, which core its launch.sh runs, which files the
// game's launch reads first, and warming them within a byte and time
// budget. Nothing here writes a file or starts a process.

#define CP_PATH_LEN 1024
#define CP_NAME_LEN 64     // a pak's name, without .pak
#define CP_MAX_FILES 3     // the core, the rom, and a .cue's first track
#define CP_SCRIPT_MAX 16384 // the most of a launch.sh, .m3u or .cue read

///////////////////////////////////////
// emulator paks

// The system tag of a game under <sdcard>/Roms/, as stock's getEmuName reads
// it: what's in the last parentheses of its system folder's name, or the
// whole name. Returns 1, or 0 for a path outside a system folder.
int CP_emuTag(const char* sdcard, const char* path, char* tag, size_t size);

// A name a pak can have: not empty, under CP_NAME_LEN, not starting with a
// dot, and no slash, backslash or control character.
int CP_validName(const char* name);

// Where stock would find the pak called name: <sdcard>/Emus/<platform>/
// <name>.pak/launch.sh, then <sdcard>/.system/<platform>/paks/Emus/
// <name>.pak/launch.sh, the first that exists. It must be a regular file
// that, with links and ".." resolved, is still <folder>/<name>.pak/
// launch.sh in a folder on the card. Writes it, resolved, and returns 1,
// or returns 0.
int CP_pakLaunch(const char* sdcard, const char* platform, const char* name, char* out, size_t size);

// The core a pak's launch.sh runs, from its text, for scripts that set
// EMU_EXE=<name> and run "$CORES_PATH/${EMU_EXE}_libretro.so", as stock's
// paks all do. CORES_PATH is <sdcard>/.system/<platform>/cores, as stock
// sets it, or the pak's own folder when the script sets
// CORES_PATH=$(dirname "$0"). Writes the core's path and returns 1, or
// returns 0 when the script runs its core some other way.
int CP_scriptCore(const char* script, const char* pak_dir, const char* sdcard, const char* platform, char* out, size_t size);

///////////////////////////////////////
// emulator-override

// 1 when emulator-override is loaded, as host->plugin_loaded says, or 0 on
// a launcher without it.
int CP_overrideOn(const MosaicHost* host);

// The card-relative key emulator-override keeps a game's choice under: the
// .m3u named after the game's folder when there's one, or the game's own
// path. Returns 1, or 0 for a path that isn't on the card.
int CP_overrideKey(const char* sdcard, const char* path, char* key, size_t size);

// The pak emulator-override.txt chooses for key, from at most max_bytes of
// the file: the last line with key, a tab, and a name CP_validName takes.
// Returns 1 with the name, or 0.
int CP_overridePak(const char* file, const char* key, long max_bytes, char* out, size_t size);

///////////////////////////////////////
// files

// Reads at most size - 1 bytes of a file as text. Returns the length, or
// -1 when it can't be opened.
long CP_readText(const char* path, char* out, size_t size);

// The files a game's launch reads first, in order: the disc a .m3u starts
// with (its first line, beside it), or the game itself; then, for a .cue,
// the first file it names. Writes at most max and returns how many.
int CP_romFiles(const char* path, char files[][CP_PATH_LEN], int max);

// path, with links and ".." resolved, when it's a regular file inside
// card_real, the card's own resolved path. Returns 1, or 0.
int CP_cardFile(const char* card_real, const char* path, char* real, size_t size);

///////////////////////////////////////
// warming

typedef struct CPBudget {
	long long max_bytes; // across every file
	long long max_ms;    // since start; no chunk starts after it
	long long chunk;     // bytes per advise call
	long long start_ms;  // now_ms() when the launch began
	// asks the kernel to read a range into the page cache, eg. with
	// posix_fadvise(POSIX_FADV_WILLNEED). Returns 0 when it did.
	int (*advise)(int fd, long long offset, long long len);
	long long (*now_ms)(void);
} CPBudget;

typedef struct CPResult {
	long long bytes; // advised
	int files;       // opened and advised at least in part
	int timed_out;   // stopped at max_ms
	int capped;      // stopped at max_bytes
	int failed;      // a file that couldn't be opened or advised
} CPResult;

// Opens each file read-only and advises it from the start, a chunk at a
// time, until the file ends, the budget runs out or the clock passes
// max_ms. Files come in order, so the first gets the budget first.
void CP_warm(char files[][CP_PATH_LEN], int count, const CPBudget* budget, CPResult* result);

#endif
