#ifndef INFOMODEL_H
#define INFOMODEL_H

// Reading .info metadata sidecars, without SDL or the card: what a game's
// <rom dir>/.media/<rom name without its last extension>.info says, such as
// its year, publisher or genre, for plugins' display hooks. Each line is
// key=value. Blank lines and lines starting with # are skipped, and so are
// lines without an =, an empty key or a key with anything but letters,
// digits, _, - and . in it. Keys are matched without case, and a key given
// twice keeps its last value. Values are trimmed, have control characters
// (tabs too) turned to spaces, and are cut to INFO_VALUE_MAX bytes,
// between characters.

#include <stddef.h>

#define INFO_FILE_MAX 4096 // bytes; a bigger file is ignored
#define INFO_KEYS_MAX 32   // keys kept; later new keys are left out
#define INFO_KEY_MAX 31    // bytes of a key; a longer one skips its line
#define INFO_VALUE_MAX 255 // bytes kept of a value

typedef struct InfoFile {
	int count;
	const char* keys[INFO_KEYS_MAX];   // lower case
	const char* values[INFO_KEYS_MAX];
	// the keys and values follow, in the same allocation
} InfoFile;

// Parses len bytes of a .info file into one allocation, so free() frees it.
// Returns NULL for a file that's too big, has a NUL in it, or has no keys,
// with why (a static string) saying which.
InfoFile* Info_parse(const char* data, size_t len, const char** why);

// The value of key, matched without case, or NULL.
const char* Info_get(const InfoFile* info, const char* key);

// Where a ROM's .info sidecar is, written to out.
void Info_path(const char* rom_path, char* out, size_t size);

// Copies value to out, cut between UTF-8 characters to fit size. Returns 1,
// or 0 for a NULL value, writing "".
int Info_copy(const char* value, char* out, size_t size);

#endif
