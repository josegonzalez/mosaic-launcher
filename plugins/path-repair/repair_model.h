// Path repair, without the host: names folded so they survive renames, and
// the one file in a folder a missing name now matches. No SDL and no file
// access, so it's tested on the host.

#ifndef REPAIR_MODEL_H
#define REPAIR_MODEL_H

#include <stddef.h>

// The key two names are matched by. Accents are folded (UTF-8, composed or
// with combining marks: "Pokémon" and "Pokemon" match), letters lowercased,
// and everything but letters and digits dropped, so case, spaces and
// punctuation don't count. The extension is kept apart and only its case
// is folded, so "Tetris.gb" never matches "Tetris.gbc". Letters outside
// Latin are kept as they are. Writes "" when out is too small.
void Repair_key(const char* name, char* out, size_t size);

// The index in names of the one name whose key matches missing's, or -1
// when none does or more than one does (an ambiguous match is left alone).
// A name equal to missing itself never counts.
int Repair_pick(const char* missing, const char* const* names, int count);

// How much of a card-relative path has to exist as it is before the rest
// can be repaired: its first two folders, the system folder, eg.
// "/Roms/Game Boy (GB)" of "/Roms/Game Boy (GB)/Tetris.gb", or for a pak
// "/Tools/tg5040". Returns that length, or 0 when nothing is below them.
size_t Repair_fixedLength(const char* rel);

#endif
