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
};

void resolveArt(int kind, const char* subject, char* out, size_t size);

#endif
