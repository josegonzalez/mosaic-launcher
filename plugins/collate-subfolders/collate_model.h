// Collate subfolders, without the host: which system folders stock lists
// together, and which of their same-name subfolders is the one shown.
// Folders are matched and merged with genre-view's model (genre_model.h).
// No SDL and no file access, so it's tested on the host.

#ifndef COLLATE_MODEL_H
#define COLLATE_MODEL_H

#include <stddef.h>
#include "../genre-view/genre_model.h"

// The part of a system folder's name stock collates by: up to and
// including its last "(", eg. "Game Boy (" for "Game Boy (GB)". Returns
// 1, or 0 for a name without a "(", which this plugin leaves to stock.
int CS_prefix(const char* system, char* out, size_t size);

// True when two system folders collate together: both have a "(" and
// the same name up to their last one, as "Game Boy (GB)" and "Game Boy
// (MGB)" do, but not "Game Boy Color (GBC)".
int CS_collates(const char* a, const char* b);

// The system folder a console list shows, for a dir_path directly inside
// roms, eg. "Game Boy (GB)" for <roms>/Game Boy (GB). Returns 1, or 0 for
// any other path.
int CS_consoleDir(const char* roms, const char* dir_path, char* out, size_t size);

// The merged folder a folder on the card is part of: the index of the
// group holding it as a source, or -1.
int CS_find(const GVGroups* groups, const char* path);

// True when a folder shows in its list: it's the first source of its
// group, or in no group (a pak, a game folder, or not a folder at all).
int CS_shown(const GVGroups* groups, const char* path);

#endif
