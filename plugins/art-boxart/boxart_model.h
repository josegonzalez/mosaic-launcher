// Box art provider, without the host: the art folder conventions an
// entry's art is looked for in, in order, and the art folders hidden from
// the list. No SDL, and files are only checked through a callback, so it's
// tested on the host.
//
// For a game, named by its file name without its last extension as stock
// names its .media thumbnail (eg. "Super Mario Bros. 3 (USA)"), and for a
// folder, by its whole name, the first file that exists wins:
//
//   box and thumbnail
//     1. rom as art: a Pico-8 cart (*.p8.png) is its own art
//     2. folder as cover: <folder>/cover.png, for folders
//     3. nx-redux:   <dir>/.media/boxart2d/<name>.*, the flat box
//     4. nx-redux:   <dir>/.media/boxart/<name>.*, the 3D box, or the flat
//                    one when there's no 3D
//     5. MyMinUI:    <dir>/Imgs/<name>.png
//     6. UnuOS:      <dir>/.res/<name>.png
//     7. visible art folders: <dir>/Images/<name>.png
//   3D box:     <dir>/.media/boxart/<name>.* (nx-redux)
//   logo:       <dir>/.media/wheel/<name>.* (nx-redux)
//   screenshot: <dir>/.media/screenshot/<name>.* (nx-redux)
//
// where <dir> is the folder the entry is in, and <name>.* is <name>.png,
// then <name>.jpg and <name>.jpeg, as nx-redux reads them (its .webp is
// left out: the art cache can't decode it, and so is its .media/mix
// composite, which isn't any one art kind). Each convention has its own
// setting, and the thumbnail is answered only with the thumbnail setting
// on, so stock's .media thumbnail stays the default.

#ifndef BOXART_MODEL_H
#define BOXART_MODEL_H

#include <stddef.h>

#define BA_PATH_LEN 1024

typedef struct BAConfig {
	int thumbnail; // answer the thumbnail kind too, before stock's .media one
	int media;     // nx-redux's .media/boxart2d, boxart, wheel and screenshot
	int imgs;      // MyMinUI's Imgs
	int res;       // UnuOS's .res
	int images;    // visible art folders: Images
	int hide;      // hide the visible art folders (Imgs, Images) whose convention is on
	int cover;     // folder as cover: cover.png in a folder
	int cover_name; // with cover, hide the names of folders that have one
	int rom_art;   // rom as art: *.p8.png carts
} BAConfig;

// Everything on but the thumbnail and hiding covered folders' names.
void BA_defaults(BAConfig* config);

// A toggle setting's value: 1 for "on", 0 for "off", fallback otherwise.
int BA_toggle(const char* value, int fallback);

// The name an entry's art is filed under: its file name, without its last
// extension for a game (is_dir 0). Writes "" and returns 0 for a path with
// no folder, an empty name or one that doesn't fit.
int BA_artName(const char* path, int is_dir, char* out, size_t size);

// True for a Pico-8 cart: a name ending in .p8.png, ignoring case.
int BA_isCart(const char* path);

// Checks that a file exists; data is passed through.
typedef int (*BAExists)(const char* path, void* data);

// Where an entry's art of kind (an enum MosaicArtKind) is: writes the first
// file that exists, as the conventions above order them, and returns 1, or
// writes "" and returns 0 for none, a kind it doesn't answer, or a path too
// long to build. type is the entry's enum MosaicEntryType; only games and
// folders have art.
int BA_find(const BAConfig* config, const char* path, int type, int kind, BAExists exists, void* data,
	char* out, size_t size);

// True when an entry is a visible art folder the list hides: a folder
// named Imgs or Images (ignoring case) with its convention and the hide
// setting on.
int BA_hides(const BAConfig* config, const char* path, int type);

// True when an entry's name is hidden, so its cover stands in for it: a
// folder with a cover.png, with the cover and cover_name settings on.
int BA_hidesName(const BAConfig* config, const char* path, int type, BAExists exists, void* data);

#endif
