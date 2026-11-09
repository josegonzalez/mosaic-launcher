#ifndef LOCALIZATION_NAMES_MODEL_H
#define LOCALIZATION_NAMES_MODEL_H

#include <stddef.h>

// The SDL-free half of localization's map.txt writer: names lists, which
// give a game's name in the user's language by its English title, and
// matching them to a folder's files loosely, so "Super Mario Land (World)
// (Rev 1).gb" finds "Super Mario Land".
//
// A names list has one game a line, its English title, a tab and the name
// to show; blank lines and lines starting with # are skipped. Stock's
// map.txt has the same shape, keyed on the file's whole name.

#define NM_KEY_MAX 256

// The loose key for a name, into out: with strip_extension, its last
// extension (up to 8 characters after a dot, with no spaces or brackets)
// goes; then everything in (), [] and {}, a leading "The " and a
// trailing ", The", and every ASCII character other than letters and digits,
// with letters lowercased and "&" read as "and". Other UTF-8 is kept as it
// is. Returns 0, or -1 when nothing is left or it doesn't fit.
int Nm_key(const char* name, int strip_extension, char* out, size_t size);

// The tag in a system folder's name, the text in its last (), such as "GB"
// for "Game Boy (GB)". Returns 0, or -1 when it has none.
int Nm_tag(const char* folder, char* out, size_t size);

typedef struct NmName {
	char* key;
	char* name;
	int seq; // the order it was read in
} NmName;

typedef struct NmNames {
	NmName* items; // sorted by key once read
	int count;
	int cap;
	int seq;
} NmNames;

// Adds a names list's lines to names. The first name for a key stays; a
// line with no tab, no name, a key that's empty or a name that isn't UTF-8
// is skipped. Returns how many were added, or -1 when out of memory.
int Nm_namesRead(NmNames* names, const char* text);

// The name for key, or NULL.
const char* Nm_namesFind(const NmNames* names, const char* key);

void Nm_namesFree(NmNames* names);

// What a folder's map.txt becomes with names for its files added:
// existing's text as it is (NULL or "" for no map.txt), then for each file
// in files (dirs[i] set for a folder, whose name keeps its last dot) that
// existing doesn't list and the names list has, a line of its file name, a
// tab and the name. Returns the new text to free, with how many lines were
// added in added, or NULL when out of memory.
char* Nm_merge(const char* existing, const char* const* files, const int* dirs, int count, const NmNames* names, int* added);

#endif
