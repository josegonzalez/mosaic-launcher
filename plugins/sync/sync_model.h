// The sync plugin's model, without SDL or the host: which files sync and
// where they go on the remote, reading rclone's and rsync's listings,
// the record of what was last in sync, deciding each file's step (newest
// wins, the other side kept as a .conflict copy), what romm.synced's
// replies mean for a file, and the bits of Syncthing's REST API it reads
// and writes.
//
// A file's place on the remote, its "rel", is Saves/<tag>/<file> for
// Saves/<tag>/<file> on the card and States/<tag>-<core>/<file> for
// .userdata/shared/<tag>-<core>/<file>, where minarch keeps a core's
// states. Only files directly in those folders sync; hidden files,
// partial downloads and .conflict copies don't.

#ifndef SYNC_MODEL_H
#define SYNC_MODEL_H

#include <stddef.h>

#define SY_PATH_MAX 1024
#define SY_NAME_MAX 256
#define SY_FILES_MAX 8192
#define SY_CONFLICT ".conflict"
// mtimes this close are the same time: FAT keeps them to 2 seconds
#define SY_SLACK 2

enum { SY_SAVE, SY_STATE };

// What a file needs. UPLOAD_KEEP and DOWNLOAD_KEEP are a file changed on
// both sides: the newer goes across, and the one it replaces is kept
// beside it as <name>.conflict. SAME is a file found the same on both
// sides with no record yet, so only the record changes.
enum { SY_NONE, SY_SAME, SY_UPLOAD, SY_DOWNLOAD, SY_UPLOAD_KEEP, SY_DOWNLOAD_KEEP };

typedef struct SyFile {
	char rel[SY_PATH_MAX];
	long long size;
	long long mtime; // seconds since 1970, UTC
} SyFile;

// What a file was the last time it was in sync, on each side
typedef struct SyBase {
	char rel[SY_PATH_MAX];
	long long lsize, lmtime;
	long long rsize, rmtime;
} SyBase;

typedef struct SyStep {
	char rel[SY_PATH_MAX];
	int action;
} SyStep;

// 1 when name is a file that syncs: not hidden, not a .conflict copy or a
// partial download, no slash or control characters
int Sy_syncable(const char* name);

// 1 when name is a folder in .userdata/shared that holds a core's states,
// <tag>-<core>
int Sy_stateDir(const char* name);

// 1 when rel is Saves/<dir>/<file> or States/<state dir>/<file> with a
// file that syncs
int Sy_relValid(const char* rel);

// SY_SAVE or SY_STATE for a valid rel, else -1
int Sy_kind(const char* rel);

// The card's path for rel. Returns 0, or -1 for a rel that isn't valid or
// doesn't fit.
int Sy_localPath(const char* sdcard, const char* rel, char* out, size_t size);

// rel for a file on the card, the reverse of Sy_localPath. Returns 0, or
// -1 for a path that doesn't sync.
int Sy_relOf(const char* sdcard, const char* path, char* out, size_t size);

// The game's system tag and its file's name without the extension, from
// its path under <sdcard>/Roms/<System (TAG)>/. Returns 0, or -1 for a
// path outside Roms or a system folder without a tag.
int Sy_gameOf(const char* sdcard, const char* rom_path, char* tag, size_t tag_size, char* base, size_t base_size);

// 1 when rel is one of that game's saves or states: Saves/<tag>/<base>.*
// or States/<tag>-*/<base>.*
int Sy_ofGame(const char* rel, const char* tag, const char* base);

// The remote's path for rel: remote/rel, without doubling a slash or
// adding one after a bare "name:". Returns 0, or -1 when it doesn't fit.
int Sy_remoteJoin(const char* remote, const char* rel, char* out, size_t size);

// Seconds since 1970 for a UTC date and time. -1 for one out of range.
long long Sy_utc(int year, int month, int day, int hour, int minute, int second);

// One line of `rclone lsf -R --files-only --format pst --separator "\t"`,
// run with TZ=UTC: path, size and "YYYY-MM-DD HH:MM:SS". Returns 0, or
// -1 for a line that isn't a file that syncs.
int Sy_parseRclone(const char* line, SyFile* out);

// One line of `rsync -r --list-only`, run with TZ=UTC: "-rw-r--r--
// 1,234 2026/11/15 10:00:00 Saves/GB/x.sav". Returns 0, or -1 for a
// folder, a link or a line that isn't a file that syncs.
int Sy_parseRsync(const char* line, SyFile* out);

// The record of what was last in sync, one file a line:
// lsize, lmtime, rsize, rmtime and rel, separated by tabs.
int Sy_baseParse(const char* line, SyBase* out);
int Sy_baseFormat(const SyBase* b, char* out, size_t size);

// What a file needs, from how it is locally (l), on the remote (r) and
// when it was last in sync (b), each NULL when there's none. A file
// missing from one side is copied there: removing a file isn't synced.
// With no record, two different files are a file changed on both sides.
int Sy_decide(const SyFile* l, const SyFile* r, const SyBase* b);

// Sorts files or records by rel, for the lookups below.
void Sy_sortFiles(SyFile* files, int count);
void Sy_sortBase(SyBase* base, int count);
const SyFile* Sy_findFile(const SyFile* files, int count, const char* rel);
const SyBase* Sy_findBase(const SyBase* base, int count, const char* rel);

// Every file's step, in rel order, for the files in either sorted list
// that want(rel, data) keeps (want NULL for all). Steps that are
// SY_NONE are left out. Returns the number of steps, at most max.
int Sy_plan(const SyFile* local, int local_count, const SyFile* remote, int remote_count, const SyBase* base, int base_count,
	int (*want)(const char* rel, void* data), void* data, SyStep* steps, int max);

// The record after a run, into out (at most max): for a file want keeps,
// whose step didn't fail (failed(rel, data) is 0) and that's now on both
// sides, how it is on each from the lists made after the run; for every
// other file, its old record, if it had one. All lists sorted. Returns
// the count, sorted.
int Sy_baseNext(const SyBase* old, int old_count, const SyFile* local, int local_count, const SyFile* remote, int remote_count,
	int (*want)(const char* rel, void* data), int (*failed)(const char* rel, void* data), void* data, SyBase* out, int max);

// The value of key in a reply of key=value lines, such as romm.synced's,
// as a number, or fallback when it has no such key
int Sy_replyInt(const char* reply, const char* key, int fallback);

// 1 when romm uploads this file itself, so sync leaves it alone: its
// romm.synced reply says a server is set and that romm uploads that kind
// (saves=1 for a save, states=1 for a state), and either game_reply, the
// reply for the game the file is from, says it's a game from RomM
// (game=1), or the file's own reply says romm last uploaded or restored
// it, unchanged since (synced=1). Either reply can be NULL.
int Sy_rommSkips(const char* game_reply, const char* file_reply, int kind);

// 1 for a Syncthing device ID: 8 groups of 7 of A-Z and 2-7 with dashes
// between (or the same 56 letters without them), case ignored
int Sy_deviceIdValid(const char* id);

// The scalar value of the first "key" in a JSON text, a string without its
// quotes (escapes as they are) or a number, true or false as written.
// Returns its length, or -1 without one.
int Sy_jsonValue(const char* json, const char* key, char* out, size_t size);

// text as a JSON string's contents. Returns 0, or -1 when it doesn't fit
// or has control characters.
int Sy_jsonEscape(const char* text, char* out, size_t size);

// name as a Syncthing ignore pattern matching just that path: its *, ?,
// [, ], {, }, \ and a leading ! or # escaped with a backslash. Returns 0,
// or -1 when it doesn't fit.
int Sy_ignoreEscape(const char* name, char* out, size_t size);

#endif
