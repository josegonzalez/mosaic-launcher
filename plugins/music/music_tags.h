// The music plugin's tag readers, apart from SDL and the launcher: a
// track's title, artist and album, and its embedded cover art, from ID3v2
// (2.2, 2.3 and 2.4) or ID3v1 in an MP3, and Vorbis comments in a FLAC;
// and the cover art file in a track's folder.
//
// Files are untrusted. Every length is checked against what's left of the
// file, the tag or the block it's in, and a size past it is refused. What
// comes out is always valid UTF-8 without control characters, cut at a
// character's end to fit: text that isn't valid is mended with U+FFFD.

#ifndef MUSIC_TAGS_H
#define MUSIC_TAGS_H

#include <stddef.h>

#define MU_TAG_TEXT_MAX 256          // a title, artist or album, in bytes of UTF-8 with its end
#define MU_TAG_FRAME_MAX 4096        // read of a text frame or comment; the rest is left out
#define MU_PICTURE_MAX (4 << 20)     // an embedded picture; bigger ones are skipped
#define MU_TAG_MAX (16 << 20)        // an unsynchronised ID3v2 tag, read whole
#define MU_TAG_ITEMS_MAX 4096        // frames, blocks or comments looked at
#define MU_COVER_FILE_MAX (16 << 20) // a folder's cover file, as the launcher's image_load reads

enum MuPicture {
	MU_PICTURE_NONE,
	MU_PICTURE_JPEG,
	MU_PICTURE_PNG,
};

typedef struct MuTags {
	char title[MU_TAG_TEXT_MAX];
	char artist[MU_TAG_TEXT_MAX];
	char album[MU_TAG_TEXT_MAX];
	unsigned char* picture; // malloc'd, or NULL
	size_t picture_size;
	int picture_kind;  // an enum MuPicture
	int picture_front; // 1 when it's marked as the front cover
} MuTags;

void MuTags_init(MuTags* tags);
void MuTags_free(MuTags* tags);

// Where tags are read from: read copies len bytes from off into out and
// returns 0, or -1 when it can't (such as past size).
typedef struct MuSource {
	int (*read)(void* ctx, unsigned long long off, void* out, size_t len);
	void* ctx;
	unsigned long long size;
} MuSource;

// A source over memory, for tests.
typedef struct MuMemory {
	const unsigned char* data;
	size_t size;
} MuMemory;
void MU_memorySource(MuSource* src, MuMemory* mem, const void* data, size_t size);

// Reads a track's tags from src, by its kind (an enum MuKind), with its
// picture only when want_picture is 1. Returns 1 when it found any text or
// picture, else 0, with tags empty.
int MU_readTags(const MuSource* src, int kind, int want_picture, MuTags* tags);

// The same for the file at path, by its extension: a regular file only.
int MU_readTagsFile(const char* path, int want_picture, MuTags* tags);

// The picture's kind by its first bytes.
int MU_pictureKind(const unsigned char* data, size_t size);
const char* MU_pictureExt(int kind); // "jpg", "png" or ""

// text as valid UTF-8 into out, cut to fit; each returns its length.
// encoding is ID3v2's: 0 ISO-8859-1, 1 UTF-16 with its byte order mark
// (little-endian without one), 2 UTF-16BE, 3 UTF-8. It stops at the
// text's end (NUL), turns tabs and line breaks into spaces, drops other
// control characters and trims spaces at either end.
size_t MU_tagText(int encoding, const unsigned char* text, size_t len, char* out, size_t size);

// Base64 into out (padding optional, whitespace skipped). Returns the
// decoded length, or -1 for anything else or when it doesn't fit.
long MU_base64Decode(const char* in, size_t len, unsigned char* out, size_t size);

// The cover art file in dir: the first of cover.jpg, cover.png and
// folder.jpg, matched without case, that's a regular file of up to
// MU_COVER_FILE_MAX. Returns 1 with its path in out, or 0.
int MU_folderCover(const char* dir, char* out, size_t size);

// A name for a track's embedded picture, from its path, size and change
// time, so another track's or an older one's never gets its name.
void MU_artName(const char* path, unsigned long long file_size, long long mtime, int kind, char* out, size_t size);

// Makes dir (mode 0700) when it's missing; returns 0 when it's a folder of
// this user's and not a link to one, or -1.
int MU_artDir(const char* dir);

// Writes data to dir/name, through a file beside it opened with O_EXCL
// and O_NOFOLLOW and renamed over it, so a link put there isn't followed.
// Returns 0, or -1 leaving nothing behind.
int MU_writeArt(const char* dir, const char* name, const unsigned char* data, size_t size);

#endif
