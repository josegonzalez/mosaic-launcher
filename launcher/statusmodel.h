#ifndef STATUSMODEL_H
#define STATUSMODEL_H

// The parts of the status pill that don't need SDL or the card: reading a
// shell pak's status file, keeping track of the files in status.d between
// polls, deciding which are stale, and the one the pill shows.
// statuspill.c reads the folder and draws the pill.
//
// A status file is USERDATA_PATH/launcher/status.d/<pak>.status, written by
// a shell pak, with key=value lines:
//
//   state=running   (required: running, done or error)
//   done=3          (optional, 0 to 999999999)
//   total=10        (optional, 0 to 999999999; with done, shows 30%)
//   label=Syncing   (optional, up to STATUS_LABEL_MAX characters; the pak's
//                    name without it)
//
// Blank lines and lines starting with # are skipped, and keys the launcher
// doesn't know are ignored, so newer keys can come later. Anything else
// rejects the whole file.

#include <stddef.h>

#define STATUS_FILE_MAX 1024     // bytes; a bigger file is rejected
#define STATUS_LABEL_MAX 32      // characters kept of a label
#define STATUS_NAME_MAX 64       // bytes of a pak's name, with its NUL
#define STATUS_SLOTS 16          // files tracked at once; more are left out
#define STATUS_POLL_MS 1000      // how often status.d is looked at
#define STATUS_STALE_MS 120000   // running or error, unchanged this long: stale
#define STATUS_DONE_MS 5000      // done is shown this long after it's seen
#define STATUS_NUMBER_MAX 999999999

enum StatusState {
	STATUS_RUNNING,
	STATUS_DONE,
	STATUS_ERROR,
};

typedef struct StatusFile {
	int state;
	long done, total; // -1 when the file doesn't say
	char label[STATUS_LABEL_MAX + 1]; // sanitised, "" when the file doesn't say
} StatusFile;

// Parses len bytes of a status file. Returns 0, or -1 for a file that's
// rejected, with why (a static string) saying what was wrong.
int Status_parse(const char* data, size_t len, StatusFile* out, const char** why);

// Copies text to out keeping only what the launcher's font draws: printable
// ASCII, with each other character (or byte that isn't UTF-8) as a "?" and
// tabs as spaces, trimmed, and cut to max_chars characters. out holds size
// bytes.
void Status_sanitise(const char* text, size_t len, int max_chars, char* out, size_t size);

// The pak's name from a file name in status.d, written to out: 1 for
// "<pak>.status" with a name that doesn't start with a dot, else 0.
int Status_fileName(const char* file, char* out, size_t size);

// What the pill says for a file from pak: its label (or the pak's name),
// then "30%" while running with a total, "done" or "failed".
void Status_text(const StatusFile* file, const char* pak, char* out, size_t size);

// The printf formats Status_text says those with: the name and "done",
// "failed", or the percentage (a long long), such as translations of
// STATUS_DONE_FORMAT.
#define STATUS_DONE_FORMAT "%s done"
#define STATUS_FAILED_FORMAT "%s failed"
#define STATUS_PERCENT_FORMAT "%s %lld%%"
typedef struct StatusWords {
	const char* done;
	const char* failed;
	const char* percent;
} StatusWords;

// Status_text in words (NULL for English's), cutting out only between
// UTF-8 characters.
void Status_textIn(const StatusFile* file, const char* pak, const StatusWords* words, char* out, size_t size);

// What a file looked like when it was last read: a change to any of these
// means it's read again.
typedef struct StatusSig {
	long long mtime; // seconds since the epoch
	long mtime_ns;
	long long size;
	unsigned long long ino;
} StatusSig;

typedef struct StatusSlot {
	char name[STATUS_NAME_MAX]; // "" for a free slot
	StatusSig sig;
	unsigned long changed_ms; // launcher time when sig last changed
	unsigned long done_ms;    // launcher time when it was first seen done
	int done_seen;            // done_ms is set
	int valid;                // parsed cleanly
	StatusFile file;
	int seen;                 // found in this poll
} StatusSlot;

typedef struct StatusBoard {
	StatusSlot slots[STATUS_SLOTS];
	unsigned long next_poll; // launcher time of the next poll
} StatusBoard;

// 1 when a poll is due at now (launcher milliseconds), scheduling the next.
int StatusBoard_due(StatusBoard* board, unsigned long now);

// Starts a poll: every slot is unseen until StatusBoard_track finds it.
void StatusBoard_begin(StatusBoard* board);

// The slot for pak, found this poll, with what its file looks like now.
// Returns 1 when the file changed since it was last read (or is new), so
// the caller reads it and calls StatusSlot_set, 0 when it's unchanged, and
// -1 when every slot is taken.
int StatusBoard_track(StatusBoard* board, const char* pak, const StatusSig* sig, unsigned long now,
	StatusSlot** out);

// What the caller read of a slot's file, len bytes of data, or NULL when it
// couldn't. Returns 0, or -1 when it's rejected, with why.
int StatusSlot_set(StatusSlot* slot, const char* data, size_t len, unsigned long now, const char** why);

// Ends a poll, freeing the slots of files that are gone.
void StatusBoard_end(StatusBoard* board);

// 1 while a slot's file is shown: parsed cleanly, and not stale. A running
// or error file is stale once it's gone STATUS_STALE_MS without changing,
// counted from its mtime (wall_now, seconds since the epoch) and from when
// the launcher saw it change, whichever is sooner, so neither a clock that
// jumps nor a file left from before the launcher started keeps it up. A
// done file shows for STATUS_DONE_MS from when the launcher first saw it
// done, and not at all when it was written over STATUS_STALE_MS ago.
int StatusSlot_visible(const StatusSlot* slot, unsigned long now, long long wall_now);

// The slot the pill shows: the visible one changed most recently, by
// mtime, then by name. NULL when none is visible.
const StatusSlot* StatusBoard_pick(const StatusBoard* board, unsigned long now, long long wall_now);

#endif
