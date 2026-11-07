#ifndef TITLES_H
#define TITLES_H

#include <stddef.h>

// Telling apart entries whose names collide in a list. Each entry's path
// has parts the shown name leaves out:
//
//     /mnt/SDCARD/Roms/Game Boy (GB)/Puzzle/Tetris (USA) (Rev 1).gb
//                       system ^^    ^ folder ^ tags ^^^^^^^^^^^ ^ ext
//
// For each group of entries with the same name, the fewest of these parts
// that give every entry its own name are added, in this order of
// preference: system, tags, extension, folder. The result reads as
//
//     Tetris (GB) (Puzzle) (USA) (Rev 1).gb
//
// with only the chosen parts present, eg. "Tetris (GB)" and "Tetris (GBC)",
// or "Tetris.gb" and "Tetris.gbc". When none of them do, as for map.txt
// names that collide, the whole file name is the last resort, in place of
// the tags and extension: "Best Game (a.gb)".

enum {
	TITLES_SYSTEM = 1, // the tag in the console folder's name, eg. GB
	TITLES_TAGS = 2,   // the file name's trailing (...) and [...] groups
	TITLES_EXT = 4,    // the extension(s) the shown name drops, eg. .gbc
	TITLES_FOLDER = 8, // the name of the folder holding the entry
	TITLES_FILE = 16,  // the whole file name
};

typedef struct TitleParts {
	char system[64];
	char folder[128];
	char tags[128];
	char ext[32];
	char file[256];
} TitleParts;

// Splits path into its parts. roms is the Roms folder, eg.
// "/mnt/SDCARD/Roms"; entries outside it have no system.
void Titles_parts(const char* roms, const char* path, TitleParts* out);

// Writes name followed by the parts in mask. Longer results are cut short
// at a character boundary to fit size.
void Titles_label(const char* name, const TitleParts* parts, int mask, char* out, size_t size);

// Picks the parts to show for a group of n entries sharing name: the
// smallest set that gives each a different label, or every part that
// differs when none does. Returns 0 when no part differs.
int Titles_pick(const char* name, const TitleParts* parts, int n);

// New names for the entries of a list whose names collide. names and paths
// are the n entries in list order; labels[i] gets a new name from malloc,
// or NULL to keep the entry's name. With same_system 0, only groups that
// mix systems are renamed, leaving the rest to the launcher's own
// handling of neighbours with the same name. Returns how many entries got
// a new name, or -1 when memory ran out (labels are then all NULL).
int Titles_list(const char* roms, int same_system, int n,
	const char* const* names, const char* const* paths, char** labels);

#endif
