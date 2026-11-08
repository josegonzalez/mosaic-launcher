#ifndef ART_H
#define ART_H

// Where the launcher looks for each kind of image. Resolving a path doesn't
// check that the file exists; callers fall back as they need to.

#include <stddef.h>

enum ArtKind {
	ART_THUMBNAIL,      // subject: rom path         -> <rom dir>/.media/<rom name>.png
	ART_FOLDER_BG,      // subject: folder path      -> <folder>/.media/bg.png
	ART_LIST_BG,        // subject: rom's folder     -> <folder>/.media/bglist.png
	ART_DEFAULT_BG,     // subject: unused           -> <sd>/bg.png
	ART_QUICK_BG,       // subject: tile name+state  -> <sd>/.media/quick_<subject>.png
	ART_QUICK_FALLBACK, // subject: unused           -> <sd>/.media/quick.png
	ART_QUICK_ICON,     // subject: tile name        -> <sd>/.system/res/<subject>@<scale>x.png
	// only plugins' art_path supplies these, so they resolve to ""
	ART_BOX,
	ART_BOX_3D,
	ART_LOGO,
	ART_SCREENSHOT,
	ART_BACKDROP,
};

void resolveArt(int kind, const char* subject, char* out, size_t size);

// The kinds the built-in list looks through for a game's art, in order,
// showing the first that exists: its thumbnail, then a box, 3D box or
// screenshot from a plugin. Returns -1 past the last.
int Art_listKind(int i);

#endif
