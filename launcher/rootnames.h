#ifndef LAUNCHER_ROOTNAMES_H
#define LAUNCHER_ROOTNAMES_H

#include <stddef.h>

// The SDL-free half of translating the root list's names: the launcher's
// own rows (Recently Played, Collections, Tools) and the system folders,
// as shown on the root and in the Roms folder. Only the shown name
// changes; paths, and so art, sorting and the selection, stay English.

#define ROOTNAME_SYSTEMS "systems" // the context system names are translated in

enum RootNameKind {
	ROOTNAME_ROW,    // one of the launcher's rows, translated whole with no context
	ROOTNAME_SYSTEM, // a system folder, its name translated in the "systems" context
};

// Where the translatable name is in a system folder's name: after stock's
// sorting prefix (such as "001) ", which the list hides) and before its
// tag (the first round or square bracket after the name, such as "(GB)",
// and the space before it). Sets *start and *len.
void RootName_split(const char* name, size_t* start, size_t* len);

// Returns the translation of text in context, or text itself when there's
// none.
typedef const char* (*RootNameTr)(const char* context, const char* text);

// Writes the name to show for name, a row of the given kind, to out (size
// bytes): a row's translation, or a system's prefix, translated name and
// tag. Returns 1 when that differs from name, or 0 when there's no
// translation or it doesn't fit, leaving out unchanged.
int RootName_translate(int kind, const char* name, RootNameTr tr, char* out, size_t size);

#endif
