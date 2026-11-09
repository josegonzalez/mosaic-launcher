// The music plugin's model, apart from SDL and the launcher, shared by the
// plugin (plugin.so) and its daemon (musicd): which files are tracks and
// how they sort, listing a folder, the play queue with shuffle and repeat,
// the file it's saved in to resume after a power-off, the socket
// protocol's commands and status line, and the plugin's own
// small state machines (the now playing pill's timing, the lock screen's
// unlock press, browsing up and down folders).

#ifndef MUSIC_MODEL_H
#define MUSIC_MODEL_H

#include <stddef.h>

// Where the daemon listens and writes its pid.
#define MU_SOCKET_PATH "/tmp/mosaic-music.sock"
#define MU_PID_PATH "/tmp/mosaic-music.pid"

#define MU_PATH_MAX 1024
#define MU_NAME_MAX 256
#define MU_LINE_MAX 2048
#define MU_LIST_MAX 2000 // entries listed in one folder

///////////////////////////////////////
// files

enum MuKind {
	MU_KIND_NONE,
	MU_KIND_MP3,
	MU_KIND_FLAC,
	MU_KIND_WAV,
};

// The decoder a file name's extension picks, matched without case, or
// MU_KIND_NONE for anything else and for dot files (such as macOS's ._).
int MU_trackKind(const char* name);

// Folder and file names in the order they're listed: without case, and
// runs of digits by their value, so "2 Two" comes before "10 Ten".
int MU_compareNames(const char* a, const char* b);

// A track's title: its file name without the folder or extension.
void MU_trackTitle(const char* path, char* out, size_t size);

typedef struct MuItem {
	char name[MU_NAME_MAX];
	int is_dir;
} MuItem;

typedef struct MuList {
	MuItem* items;
	int count;
	int dirs; // the first dirs items are folders
} MuList;

void MU_listInit(MuList* list);
void MU_listFree(MuList* list);

// Lists dir into list: its folders first (with_dirs 1; none with 0), then
// its tracks, each sorted by MU_compareNames, without dot files and up to
// MU_LIST_MAX. Returns how many, or -1 when dir can't be read.
int MU_scan(const char* dir, MuList* list, int with_dirs);

// dir + "/" + name into out; 0 when it doesn't fit.
int MU_childPath(const char* dir, const char* name, char* out, size_t size);

// The folder above dir into out, and 1, or 0 when dir is root or outside it.
int MU_parentPath(const char* root, const char* dir, char* out, size_t size);

///////////////////////////////////////
// the queue

enum MuRepeat {
	MU_REPEAT_OFF,
	MU_REPEAT_ALL,
	MU_REPEAT_ONE,
};

int MU_parseRepeat(const char* text); // off, all or one; off for anything else
const char* MU_repeatName(int repeat);

// The tracks playing, in order, and which is playing. With shuffle on,
// order is a shuffle of them that starts with the track that was playing
// when it was turned on.
typedef struct MuQueue {
	char (*paths)[MU_PATH_MAX];
	int count;
	int* order; // count indexes into paths
	int pos;    // where in order the current track is
	int shuffle;
	int repeat;
	unsigned int seed; // the shuffle's, for a repeatable order in tests
} MuQueue;

void MuQueue_init(MuQueue* queue, unsigned int seed);
void MuQueue_free(MuQueue* queue);

// Fills the queue with the tracks of a listing of dir (its folders are
// skipped), playing start (an index among list's items) first. Returns 0,
// or -1 for no tracks, a start that isn't a track or no memory.
int MuQueue_set(MuQueue* queue, const char* dir, const MuList* list, int start);

// The current track's index into paths, or -1 when the queue is empty.
int MuQueue_current(const MuQueue* queue);

// Moves on to the next track: at the end of a track (manual 0), repeat one
// plays it again, repeat all goes round to the start and repeat off stops;
// a press of next (manual 1) always moves on, going round with repeat all
// or one. Returns the new current track, or -1 to stop.
int MuQueue_next(MuQueue* queue, int manual);

// Moves back a track, going round from the first with repeat all or one,
// or staying on the first. Returns the new current track.
int MuQueue_prev(MuQueue* queue);

// Turns shuffle on (a new order, the current track first) or off (the
// tracks' own order, staying on the current one).
void MuQueue_setShuffle(MuQueue* queue, int on);

///////////////////////////////////////
// resuming after a power-off

// The daemon keeps the queue and its track's place in a file of its own,
// MU_RESUME_NAME in the plugin's folder in the launcher's userdata folder,
// which the plugin reads at start. It's a MU_RESUME_HEADER line, then the
// fields pos and len (in ms), index (the current track's place in order),
// shuffle, repeat, count, order (count indexes, by commas) and track, as
// key=value lines in any order, then a path= line for each queued track
// and an end line, so a file cut off part way isn't taken.
#define MU_RESUME_HEADER "music-resume 1"
#define MU_RESUME_NAME "resume.txt"

// Writes the queue and its track's place to path, through a temporary
// file beside it that's synced and renamed over it. Returns 0, or -1 for
// an empty queue or a write that failed, leaving path as it was.
int MU_writeResume(const char* path, const MuQueue* queue, long long pos_ms, long long len_ms);

// Reads path into queue, which it frees first (its seed is kept), and the
// track's place into pos_ms and len_ms. Returns 1, or 0 when there's no
// file or it isn't a whole, valid one, leaving the queue empty.
int MU_readResume(const char* path, MuQueue* queue, long long* pos_ms, long long* len_ms);

// Drops the queued tracks exists says are gone, keeping the others' order
// and the current track. Returns how many are left, or -1 (with the queue
// freed) when the current track is gone.
int MuQueue_dropMissing(MuQueue* queue, int (*exists)(const char* path, void* data), void* data);

// An exists for MuQueue_dropMissing: path is a regular file.
int MU_isFile(const char* path, void* data);

///////////////////////////////////////
// the protocol: one line to the daemon, one line back, then it closes

enum MuCommand {
	MU_CMD_NONE = -1, // not a command
	MU_CMD_STATUS,    // the status line
	MU_CMD_PLAY,      // play <path>: the track, then the rest of its folder
	MU_CMD_PAUSE,
	MU_CMD_RESUME,
	MU_CMD_TOGGLE,  // pause or resume
	MU_CMD_NEXT,
	MU_CMD_PREV,    // back to the track's start after 3 seconds in, else the track before
	MU_CMD_STOP,
	MU_CMD_SEEK,    // seek <ms>, or seek +<ms> or -<ms> from where it is
	MU_CMD_SHUFFLE, // shuffle on|off
	MU_CMD_REPEAT,  // repeat off|all|one
	MU_CMD_HOLD,    // pause for a game, closing the audio device, to play on at release
	MU_CMD_RELEASE, // play on after hold, when it was playing
	MU_CMD_SLEEP,   // pause and close the audio device now, before a sleep
	MU_CMD_QUIT,
	MU_CMD_RESTORE, // play on from the resume file, at its track's place
};

// The command in line, with what follows its name (after one space,
// without the line's end) in arg, or MU_CMD_NONE.
int MU_parseCommand(const char* line, char* arg, size_t size);

// The line for a command, with its newline: returns its length, or -1 when
// it doesn't fit or arg has a line break.
int MU_formatCommand(int command, const char* arg, char* out, size_t size);

enum MuState {
	MU_STOPPED,
	MU_PLAYING,
	MU_PAUSED,
};

typedef struct MuStatus {
	int state;
	int index, count; // the track's place in the queue's order, and the queue's length
	long long pos_ms, len_ms;
	int shuffle, repeat, held;
	char track[MU_PATH_MAX]; // "" when stopped with nothing queued
} MuStatus;

const char* MU_stateName(int state);

// "ok" then tab-separated key=value fields, the track last, as it can hold
// anything but a line break, with a newline. Returns its length, or -1.
int MU_formatStatus(const MuStatus* status, char* out, size_t size);

// Reads a status line back. Returns 1, or 0 for anything else.
int MU_parseStatus(const char* line, MuStatus* status);

// Where a seek goes, from its argument and where the track is, kept inside
// the track. Returns 1, or 0 for a bad argument.
int MU_parseSeek(const char* arg, long long pos_ms, long long len_ms, long long* out);

// m:ss, or h:mm:ss from an hour.
void MU_formatTime(long long ms, char* out, size_t size);

///////////////////////////////////////
// the plugin's state

// The overlay setting: seconds to show the pill, 0 for off or
// MU_OVERLAY_ALWAYS.
#define MU_OVERLAY_ALWAYS -1
int MU_parseOverlay(const char* text);

// When the now playing pill shows: for its seconds after a track starts or
// playing resumes, or always while something plays or is paused.
typedef struct MuOverlay {
	int seconds;
	int state;
	char track[MU_PATH_MAX];
	unsigned long until;
} MuOverlay;

void MuOverlay_init(MuOverlay* overlay, int seconds);

// Takes the daemon's latest status. Returns 1 when that starts the pill's
// time again (a new track, or playing again).
int MuOverlay_update(MuOverlay* overlay, const MuStatus* status, unsigned long now);
int MuOverlay_visible(const MuOverlay* overlay, unsigned long now);

// Starts the pill's time for status whatever its state, as for a paused
// track restored at start.
void MuOverlay_show(MuOverlay* overlay, const MuStatus* status, unsigned long now);

// The list on the plugin's screen: the selection moved by delta, going
// round at either end only for a single step (wrap 1, a fresh press), and
// the first row in view that keeps it in view, rows at a time.
int MU_move(int selected, int count, int delta, int wrap);
int MU_window(int selected, int count, int rows, int first);

// The lock screen's unlock: START pressed within MU_UNLOCK_MS of SELECT,
// or while SELECT is held.
#define MU_UNLOCK_MS 1000
typedef struct MuUnlock {
	int armed;
	unsigned long select_at;
} MuUnlock;

// Returns 1 on the frame that unlocks.
int MuUnlock_frame(MuUnlock* unlock, unsigned int pressed, unsigned int held, unsigned int select_bit,
	unsigned int start_bit, unsigned long now);

#endif
