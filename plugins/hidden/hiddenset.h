#ifndef HIDDENSET_H
#define HIDDENSET_H

// The set of hidden games, by full path, kept sorted so lookups are quick.
//
// On disk it's a plain text file with one full path per line, eg.
// /mnt/SDCARD/Roms/Game Boy (GB)/Tetris.gb. Blank lines and lines starting
// with # are skipped, and a trailing slash on a folder is dropped, so the
// file is easy to edit by hand. Saving writes the paths back in sorted order.

typedef struct HiddenSet {
	char** paths;
	int count;
	int capacity;
} HiddenSet;

void HiddenSet_init(HiddenSet* set);
void HiddenSet_clear(HiddenSet* set);

// Replaces the set with the file's paths. A missing file is an empty set.
// Returns 0, or -1 if the file couldn't be read or memory ran out.
int HiddenSet_load(HiddenSet* set, const char* file);

// Writes the set to file through a temporary file, so a power cut never
// leaves half a list. Returns 0 or -1.
int HiddenSet_save(const HiddenSet* set, const char* file);

// Returns 1 when added, 0 when it was already there, -1 on a bad path or
// when memory ran out.
int HiddenSet_add(HiddenSet* set, const char* path);

// Returns 1 when removed, 0 when it wasn't there.
int HiddenSet_remove(HiddenSet* set, const char* path);

int HiddenSet_contains(const HiddenSet* set, const char* path);

#endif
