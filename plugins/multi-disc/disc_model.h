#ifndef DISC_MODEL_H
#define DISC_MODEL_H

// The multi-disc plugin's SDL-free half: which files in a folder are the
// discs of one game, the .m3u that lists them, the saves and states to
// copy to the .m3u's name, and the files that move with each disc when
// they're combined into a folder of their own.
//
// A disc is a file whose name has exactly one marker, "(Disc N)", "(CD N)"
// or "(Disk N)", or one of them with " of M", such as "(Disc 1 of 3)",
// any case, N from 1. The discs of a game share everything else in their
// names: the rest of the name, the extension, the word and the M. A game
// needs two discs or more, each number once, and at most MD_DISCS_MAX.
// Its .m3u is the name without the marker (and a space beside it) and
// without the extension, plus .m3u, eg. "Game (USA) (Disc 1).chd" is
// "Game (USA).m3u". A file named after a .cue, .ccd or .gdi beside it,
// such as its .bin tracks, belongs to that sheet and isn't a disc, and
// neither is a subchannel or audio file (.sbi, .sub, .wav, .mp3, .ogg,
// .flac, .ape) or an .m3u. Two games that would share an .m3u name, ignoring
// case, aren't grouped, so they stay as they are.

#include <stddef.h>

#define MD_NAME_LEN 256
#define MD_DISCS_MAX 16

typedef struct MDMarker {
	int number;  // N
	int total;   // M, or 0 without " of M"
	int start;   // where the "(" is in the name
	int len;     // up to and with the ")"
	char word[8]; // disc, cd or disk
} MDMarker;

// Finds the name's one marker. Returns 1, or 0 for a name with none or
// more than one.
int MD_marker(const char* name, MDMarker* out);

// The name's last extension, as minarch and stock's art cut it: the part
// from the last dot, when it's 2 to 4 characters after the dot. Returns
// the dot, or NULL.
const char* MD_extension(const char* name);

// The name without its marker, the space beside it and its extension, eg.
// "Game (USA)" for "Game (USA) (Disc 1).chd". Returns 1, or 0 when that
// leaves nothing, a name starting with a dot, or doesn't fit.
int MD_baseName(const char* name, const MDMarker* marker, char* out, size_t size);

typedef struct MDGroup {
	char m3u[MD_NAME_LEN]; // the .m3u's file name
	int discs[MD_DISCS_MAX]; // indexes into the names, by disc number
	int count;
} MDGroup;

typedef struct MDGroups {
	MDGroup* items;
	int count;
} MDGroups;

void MD_groupsInit(MDGroups* groups);
void MD_groupsFree(MDGroups* groups);

// The games among a folder's file names, in order of their .m3u names.
// Hidden names (starting with a dot) are skipped. Returns how many, or -1
// when memory ran out.
int MD_group(const char* const* names, int count, MDGroups* out);

// The group whose .m3u is m3u (a file name), or -1.
int MD_findGroup(const MDGroups* groups, const char* m3u);

// The group a disc (a file name) is in, or -1.
int MD_findDisc(const MDGroups* groups, const char* const* names, const char* disc);

// The .m3u's text: each disc's file name on a line, by disc number.
// Returns its length, or -1 when it doesn't fit.
int MD_m3uText(const MDGroup* group, const char* const* names, char* out, size_t size);

// 1 for a folder in stock's own layout for a game of several files, which
// holds an .m3u or .cue named after the folder: dir_name is the folder's
// own name.
int MD_stockFolder(const char* dir_name, const char* const* names, int count);

// Where a save or state file is: the system's Saves/<tag> folder, each
// <tag>-<core> folder in .userdata/shared, or .userdata/shared/.minui/<tag>.
enum MDPlace { MD_SAVES, MD_STATES, MD_SLOTS };

typedef struct MDCopy {
	int place; // enum MDPlace
	char from[MD_NAME_LEN + 16];
	char to[MD_NAME_LEN + 16];
} MDCopy;

#define MD_COPIES_MAX 64

// The save and state files minarch keeps under a game's file name, disc,
// and the names it keeps them under for m3u: the save (.sav and .rtc, and
// .sav and .srm under the name with its extension cut), the states (.st0
// to .st9, and .state, .state.auto, .state1 to .state8 and .state.0 to
// .state.8 under the cut name) and the slots' last slot, previews and
// discs in .minui (.txt, and .0.bmp to .9.bmp and .0.txt to .9.txt).
// Returns how many, at most MD_COPIES_MAX.
int MD_saveCopies(const char* disc, const char* m3u, MDCopy* out, int max);

// 1 when other (a file name) moves with the disc when the game is
// combined into a folder: a file of the disc's sheet, such as its tracks,
// or a subchannel or audio file named after the disc.
int MD_companion(const char* disc, const char* other);

// A loose .m3u's text again for the folder its discs move into: each line
// naming one of moved (file names) by its name alone, blank lines left
// out. Returns its length, or -1 for a line naming anything else, no
// lines, or text that doesn't fit.
int MD_m3uMoved(const char* text, const char* const* moved, int count, char* out, size_t size);

#endif
