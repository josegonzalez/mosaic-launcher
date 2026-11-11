// The romm plugin's logic that needs neither the launcher nor the network:
// URLs, HTTP requests and responses as text, a small JSON reader, RomM's
// platforms, roms, saves and states, the platform to system map, file
// names from the server, minarch's state layouts, the checks on a zip's
// entries and a multi-file game's layout, and the plugin's own small files.

#ifndef ROMM_MODEL_H
#define ROMM_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define RM_HOST_MAX 256
#define RM_PATH_MAX 1024
#define RM_NAME_MAX 256
#define RM_TAG_MAX 16
#define RM_HEAD_MAX 16384 // a response's status line and headers
#define RM_REDIRECTS_MAX 3

///////////////////////////////////////
// URLs

typedef struct RMUrl {
	int https;
	char host[RM_HOST_MAX]; // without brackets for an IPv6 address
	int port;
	char base[RM_PATH_MAX]; // the path RomM is under, "" or "/romm", never ending in '/'
} RMUrl;

// Parses the server setting, eg. "http://192.168.1.20:8080" or
// "https://romm.home/romm/". Returns 0, or -1 with why in error.
int RM_urlParse(const char* text, RMUrl* out, char* error, size_t size);
// 1 when host is an IPv4 or IPv6 address rather than a name
int RM_hostIsAddress(const char* host);
// "host" or "host:port", as the Host header has it (port only when it isn't the scheme's own)
void RM_hostHeader(const RMUrl* url, char* out, size_t size);
// Percent-encodes text for a path segment or query value
void RM_urlEncode(const char* text, char* out, size_t size);
// Works out where a redirect's Location goes from the request's url and
// target (the path and query). Only a path on the same server is followed:
// the same scheme, host and port. Returns 0 with the new target, or -1.
int RM_redirect(const RMUrl* url, const char* location, char* target, size_t size);

///////////////////////////////////////
// Requests and responses

void RM_base64(const unsigned char* data, size_t len, char* out, size_t size);
// "Authorization: ...\r\n" for a user and password (Basic) or, with no
// user, a token (Bearer); "" when there's neither
void RM_authHeader(const char* user, const char* secret, char* out, size_t size);
// The request's status line and headers, up to the blank line. body_length
// is -1 for none. extra is more header lines, each ending in "\r\n", or NULL.
// Returns the length, or -1 when it doesn't fit.
int RM_requestHead(const char* method, const RMUrl* url, const char* target, const char* auth,
	const char* extra, long long body_length, char* out, size_t size);

typedef struct RMHead {
	int status;
	long long length; // Content-Length, or -1
	int chunked;
	char location[RM_PATH_MAX];
} RMHead;

// The length of the head (through the blank line) in buf, 0 when more is
// needed, or -1 when it's past RM_HEAD_MAX.
int RM_headEnd(const char* buf, size_t len);
// Parses a head RM_headEnd found. Returns 0, or -1 when it isn't HTTP/1.x.
int RM_headParse(const char* buf, size_t len, RMHead* out);

// Chunked transfer coding, fed as it arrives.
typedef struct RMChunks {
	int state;
	unsigned long long left; // of the chunk being read
	int digits;
} RMChunks;
// Decodes in place: the body's bytes are moved to the front of buf, and
// *out_len says how many. Returns 1 at the end of the body, 0 for more, or
// -1 for a bad coding.
int RM_chunksFeed(RMChunks* c, char* buf, size_t len, size_t* out_len);

// multipart/form-data for one file field: the text before the file's bytes
// and after them. Returns 0, or -1 when they don't fit.
int RM_multipart(const char* boundary, const char* field, const char* file_name, char* before, size_t before_size,
	char* after, size_t after_size);

///////////////////////////////////////
// JSON

enum { RJ_NULL, RJ_FALSE, RJ_TRUE, RJ_NUMBER, RJ_STRING, RJ_ARRAY, RJ_OBJECT };

typedef struct RJNode {
	int type;
	const char* key; // in an object, else NULL
	const char* text; // a string's value or a number's text
	int first;       // child index, or -1
	int next;        // sibling index, or -1
	int count;       // children
} RJNode;

typedef struct RJDoc {
	RJNode* nodes;
	int count, cap;
	char* text; // a copy the strings point into
} RJDoc;

// Parses len bytes of text. Returns 0, or -1 for bad JSON, too deep
// (64 levels) or too many values (max_nodes). Free with RJ_free.
int RJ_parse(RJDoc* doc, const char* text, size_t len, int max_nodes);
void RJ_free(RJDoc* doc);
const RJNode* RJ_root(const RJDoc* doc);
const RJNode* RJ_get(const RJDoc* doc, const RJNode* object, const char* key);
const RJNode* RJ_at(const RJDoc* doc, const RJNode* array, int index);
const RJNode* RJ_next(const RJDoc* doc, const RJNode* node);
const RJNode* RJ_first(const RJDoc* doc, const RJNode* node);
// a string member, or fallback when it's missing or not a string
const char* RJ_string(const RJDoc* doc, const RJNode* object, const char* key, const char* fallback);
long long RJ_int(const RJDoc* doc, const RJNode* object, const char* key, long long fallback);
int RJ_bool(const RJDoc* doc, const RJNode* object, const char* key, int fallback);

///////////////////////////////////////
// RomM

typedef struct RMPlatform {
	int id;
	char slug[64];
	char name[RM_NAME_MAX];
	int rom_count;
	char tag[RM_TAG_MAX]; // NextUI's, "" when it isn't mapped
} RMPlatform;

typedef struct RMRom {
	int id;
	int platform_id;
	char file[RM_NAME_MAX]; // fs_name, checked with RM_safeName
	char name[RM_NAME_MAX];
	long long size;
	int multi; // more than one file: served as a zip, unpacked into a folder named file
	char launch[RM_NAME_MAX]; // a multi rom's file to launch in that folder, from RM_launchFile
} RMRom;

enum { RM_ASSET_SAVE, RM_ASSET_STATE };

typedef struct RMAsset {
	int kind; // RM_ASSET_SAVE or RM_ASSET_STATE
	int id;
	int rom_id;
	char file[RM_NAME_MAX];
	long long size;
	time_t updated; // 0 when it can't be read
	char emulator[64];
	char slot[64];
	char hash[64];
} RMAsset;

// The system a RomM platform slug maps to: its tag and the Roms folder
// stock names for it. Returns 0, or -1 for a platform with no system.
int RM_tagFor(const char* slug, char* tag, size_t tag_size, char* folder, size_t folder_size);
// How many slugs RM_tagFor knows, and the i-th, for the README and tests
int RM_tagCount(void);
const char* RM_tagSlug(int index);

// Parses GET /api/platforms. Returns how many went in out (up to max), or -1.
int RM_parsePlatforms(const char* json, size_t len, RMPlatform* out, int max);
// Parses a page of GET /api/roms (asked with_files=true), adding to out
// from *count up to max, and sets *total to the server's total. A rom of
// more than one file is multi, with the file to launch worked out from its
// files; a rom of one file in a folder takes that file's name. Roms with
// unsafe names, and multi roms without their files or with nothing to
// launch, are left out. Returns how many this page had, or -1.
int RM_parseRoms(const char* json, size_t len, RMRom* out, int* count, int max, int* total);
// Parses GET /api/saves or /api/states as kind, adding to out from *count
// up to max. Returns how many this response had, or -1.
int RM_parseAssets(const char* json, size_t len, int kind, RMAsset* out, int* count, int max);
// Newest first, then saves before states, then by id
void RM_sortAssets(RMAsset* assets, int count);

// ISO 8601 as RomM writes it, eg. "2026-11-09T07:54:23.813303+00:00".
// Returns 0 when it can't be read.
time_t RM_parseTime(const char* text);
// "2026-11-09 07:54", in local time
void RM_formatTime(time_t when, char* out, size_t size);
// "27 B", "3.2 KB", "1.5 MB"
void RM_formatSize(long long bytes, char* out, size_t size);

// 1 when a name from the server is safe as a file name in one folder: not
// empty, ".", "..", or starting with a dot, no '/', '\\' or control
// characters, and under RM_NAME_MAX.
int RM_safeName(const char* name);
// The save names minarch may use for a rom file, by the Save format
// setting: "<file>.sav" (MinUI), "<file without ext>.srm" and
// "<file without ext>.sav". Fills names[3]; returns 3.
int RM_saveNames(const char* rom_file, char names[3][RM_NAME_MAX]);
// Where a save from the server goes for a rom file, given which of
// RM_saveNames exists locally (bit i for names[i]): the existing one,
// newest kept by the caller, else by the server file's extension.
int RM_saveTarget(const char* rom_file, const char* server_file, int existing, char* out, size_t size);
// The slot of a state file from the server, in any of the layouts minarch
// writes by its State format setting: "<x>.st<N>" (its own), "<x>.state"
// (slot 0), "<x>.state<N>" and "<x>.state.<N>" for slots 0 to 9, and
// "<x>.state.auto" (the auto-resume slot, 9). -1 for any other.
int RM_stateSlot(const char* server_file);
// The name minarch gives rom_file's state in slot (0 to 9) for its State
// format setting (stateFormat in .userdata/shared/minuisettings.txt, 0 to
// 4). Returns 0, or -1.
int RM_stateName(const char* rom_file, int format, int slot, char* out, size_t size);
// The stateFormat in minuisettings.txt's text, or 0 (minarch's own) when
// it isn't there or isn't one minarch knows.
int RM_stateFormat(const char* settings);
// The core an emulator pak's launch.sh runs (its EMU_EXE=), which names
// its state folder .userdata/shared/<tag>-<core>. Returns 0, or -1.
int RM_emuCore(const char* launch_sh, char* out, size_t size);
// Which state folder of a tag's to restore into, from the names of the
// <tag>-<core> folders there are: the pak's core's (even when it isn't
// there yet), else the only one, else the one for the server state's
// emulator. Returns 0 with the folder's name, or -1 when it can't tell.
int RM_stateFolder(const char* tag, const char* const* folders, int count, const char* core, const char* emulator,
	char* out, size_t size);

///////////////////////////////////////
// Games of more than one file, which RomM serves as a zip. The zip is
// untrusted: these check every entry before anything is written.

#define RM_ZIP_ENTRIES_MAX 2048
#define RM_ZIP_NAME_MAX 512 // bytes in an entry's path
#define RM_ZIP_DEPTH_MAX 8 // parts in an entry's path
#define RM_ZIP_FILE_MAX (4LL * 1024 * 1024 * 1024 - 1) // the card's (FAT32's) largest file
#define RM_ZIP_TOTAL_MAX (16LL * 1024 * 1024 * 1024) // all the files together
#define RM_ZIP_RATIO_MAX 1032 // deflate can't shrink anything further than this
#define RM_ZIP_SLACK (64 * 1024) // what a zip may hold past the size RomM gave: its playlist
#define RM_STOCK_PATH_MAX 256 // NextUI's own path buffers, which a launched path must fit

typedef struct RMZipEntry {
	const char* name; // as the central directory has it: not NUL-terminated, and may hold NULs
	size_t name_len;
	unsigned int method; // 0 stored, 8 deflate
	unsigned int flags; // the general purpose bits
	unsigned long external; // the external attributes, a Unix mode in the top 16 bits
	unsigned long long comp, size;
} RMZipEntry;

// 1 when the entry is a folder (its name ends in '/')
int RM_zipIsDir(const RMZipEntry* e);
// Checks an entry's path: relative, made of '/'-separated parts that are
// none of "", "." or "..", with no NUL, backslash, control character or
// character the card can't hold (":*?\"<>|"), no part ending in a dot or a
// space or starting ".romm-" (the plugin's own temporary names), at most
// RM_ZIP_DEPTH_MAX parts and RM_ZIP_NAME_MAX bytes, each part under
// RM_NAME_MAX. A folder's trailing '/' is allowed. Returns 0, or -1 with why.
int RM_zipNameCheck(const char* name, size_t len, char* why, size_t size);
// Checks one entry: its name, that it's a plain file or folder (never a
// link or other kind), not encrypted, stored or deflated, and sizes that
// add up: a folder is empty, a stored file's sizes match, nothing is over
// RM_ZIP_FILE_MAX, and nothing claims to inflate past RM_ZIP_RATIO_MAX.
// Returns 0, or -1 with why.
int RM_zipEntryCheck(const RMZipEntry* e, char* why, size_t size);
// Checks the entries together: each one, at most RM_ZIP_ENTRIES_MAX, a
// total (in *total) under RM_ZIP_TOTAL_MAX and, when expect isn't -1 (the
// size RomM gave), under expect + RM_ZIP_SLACK, and no two paths the same
// when case is ignored, as the card ignores it (a file and a folder of the
// same name, a file twice, or two spellings of a folder). Returns 0, or -1
// with why.
int RM_zipCheck(const RMZipEntry* entries, int count, long long expect, long long* total, char* why, size_t size);

// 1 when name is "<game>.m3u" at the top of the game's folder, case
// ignored: the playlist the plugin writes itself, in place of any in the zip
int RM_isGameM3u(const char* game, const char* name);
// The disc number a file's name gives, as RomM reads it: "(Disc 2)",
// "(CD 2)", "(Disk 1 of 2)", "(Disc 2A)" or "(Disc B)" (B is 2). 0 for none.
int RM_discNumber(const char* name);
// The files of a game's folder that are discs to play, by RomM's rule:
// any but an .m3u, and when there's a .cue, .gdi, .ccd or .mds, none of
// the tracks those point at (.bin, .raw, .img, .sub, .mdf, .wav, .ogg,
// .flac, .mp3). Numbered discs first in order, then the rest by name. Fills
// order with indexes into names; returns how many.
int RM_discs(const char* const* names, int count, int* order);
// The file to launch in a game's folder of these files (paths in it, the
// plugin's own <game>.m3u left out): "<game>.m3u" with two discs or more,
// else the one disc. Returns how many discs there are (0 with nothing to
// launch), with out set when there's one or more.
int RM_launchFile(const char* game, const char* const* names, int count, char* out, size_t size);
// The .m3u listing the discs, one path per line. Returns its length, or -1
// when it doesn't fit.
int RM_m3uText(const char* const* names, const int* order, int discs, char* out, size_t size);
// "<folder>/<folder's name>.m3u" for a path in folder: where NextUI looks
// for the playlist that makes a folder one game, and that minarch then
// names the game's saves and states after. Returns 0, or -1.
int RM_m3uFor(const char* path, char* out, size_t size);

// A SHA-256 fingerprint as hex, with or without ':' or spaces. Returns 0
// with the 32 bytes, or -1.
int RM_parsePin(const char* text, unsigned char out[32]);
// Lowercase hex of len bytes
void RM_hex(const unsigned char* data, size_t len, char* out, size_t size);

// The status.d file's text for a download. Returns its length.
int RM_statusText(const char* state, long long done, long long total, const char* label, char* out, size_t size);

///////////////////////////////////////
// The plugin's records, one per line: "<rom id>\t<platform tag>\t<value>\t<path>"
// where value is the md5 of the save last synced, or "-" for none.

typedef struct RMRecord {
	int rom_id;
	char tag[RM_TAG_MAX];
	char value[64];
	char path[RM_PATH_MAX];
} RMRecord;

// Parses one line. Returns 0, or -1 for a line that isn't one.
int RM_recordParse(const char* line, RMRecord* out);
int RM_recordFormat(const RMRecord* record, char* out, size_t size);

#endif
