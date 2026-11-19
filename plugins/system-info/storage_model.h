#ifndef STORAGE_MODEL_H
#define STORAGE_MODEL_H

// What the Storage page works out apart from the host and SDL: each
// system's tag from its Roms folder, counting the card's folders without
// following links and each file once, the sizes grouped by system and
// sorted largest first, the progress line, and when a count is due, so
// the host tests can check it on its own. It uses only libc, so the
// plugin runs the count on a thread of its own.

#include <stddef.h>

#define ST_ROWS_MAX 128 // systems shown
#define ST_TAG_MAX 32
#define ST_NAME_MAX 96

// A system's sizes.
enum StPart {
	ST_GAMES, // its Roms folder, less its art
	ST_SAVES, // Saves/<tag> and the states in .userdata/shared/<tag>-<core>
	ST_ART,   // the .media folders in its Roms folder
	ST_PARTS,
};

typedef struct StRow {
	char tag[ST_TAG_MAX];   // eg. "GB"
	char name[ST_NAME_MAX]; // eg. "Game Boy", or the tag when it has no Roms folder
	unsigned long long bytes[ST_PARTS];
} StRow;

// What a count found.
typedef struct StTotals {
	int count;
	StRow rows[ST_ROWS_MAX];
	unsigned long long bios;        // Bios
	unsigned long long screenshots; // Screenshots
	unsigned long long files;       // files counted
} StTotals;

void St_init(StTotals* t);

// A Roms folder's tag, the text in the brackets at the end of its name,
// as "GB" for "Game Boy (GB)", or the whole name when it has none.
void St_tag(const char* folder, char* out, size_t size);

// A Roms folder's name as the list shows it: without a sort prefix such as
// "01) " or its tag, as "Game Boy" for "01) Game Boy (GB)".
void St_name(const char* folder, char* out, size_t size);

// The tag of a folder of states in .userdata/shared, the part before the
// first "-" of "<tag>-<core>", as "GB" for "GB-gambatte". Returns 0, or -1
// for a name without one.
int St_stateTag(const char* folder, char* out, size_t size);

// The row for tag, added with name when there's none yet (a name given
// later fills a row added without one). Returns its index, or -1 when
// the rows are full or the tag is empty. find only looks.
int St_row(StTotals* t, const char* tag, const char* name);
int St_find(const StTotals* t, const char* tag);

unsigned long long St_rowTotal(const StRow* row);

// Every row, BIOS and Screenshots added up.
unsigned long long St_total(const StTotals* t);

// Largest first, then by name, then by tag.
void St_sort(StTotals* t);

// Files already counted, by device and inode, for files with more than
// one link, so a hard link is counted once.
typedef struct StSeen {
	size_t count, cap;
	unsigned long long* keys; // pairs of device and inode
} StSeen;

void St_seenInit(StSeen* s);
void St_seenFree(StSeen* s);

// 1 the first time dev and ino are added, 0 after, or -1 out of memory.
int St_seenAdd(StSeen* s, unsigned long long dev, unsigned long long ino);

// How a count goes: cancel is checked before each file and stops the
// count when it's set, and progress hears of each system as it starts,
// with done from 1 to total, then with name NULL and done total + 1 for
// Bios and total + 2 for Screenshots. Both run on the counting thread.
typedef struct StWalk {
	volatile int* cancel;
	void (*progress)(void* ctx, const char* name, int done, int total, unsigned long long files);
	void* ctx;
	StSeen seen;
	unsigned long long files;
} StWalk;

void St_walkInit(StWalk* w);
void St_walkFree(StWalk* w);

// The size of the files under path, links left as they are (a link counts
// as itself, its target isn't followed), each file once, adding to *bytes
// or, inside any folder named .media, to *art when art isn't NULL. A file
// that is path itself counts too. Returns 0, or -1 when it was cancelled.
int St_dirSize(StWalk* w, const char* path, unsigned long long* bytes, unsigned long long* art);

// Counts the card at sdcard into t: each Roms folder (not those starting
// with "."), Saves/<tag> and .userdata/shared/<tag>-<core> for tags a Roms
// or Saves folder has, Bios and Screenshots, then sorts the rows. Folders
// with the same tag share a row, named after the first by name. Returns
// 0, or -1 when it was cancelled.
int St_count(const char* sdcard, StTotals* t, StWalk* w);

// The progress line, as "Counting Game Boy, 3 of 12 systems, 1,234
// files", from what progress heard: past the systems, "Counting BIOS" or
// "Counting Screenshots", with every system done.
void St_formatProgress(const char* name, int done, int total, unsigned long long files, char* out, size_t size);

// A count with thousands separators, as "1,234".
void St_formatCount(unsigned long long n, char* out, size_t size);

// When a count is due: the card's generation moves on with each change
// (a game run, a new card index), and a count remembers the generation
// it started at.
typedef struct StCache {
	unsigned int card;    // the card's generation
	unsigned int counted; // the generation the last finished count started at
	unsigned int started; // the generation the running count started at
	int have;             // 1 once a count has finished
	int counting;         // 1 while one runs
} StCache;

void St_cacheInit(StCache* c);

// Something changed the card.
void St_cacheChanged(StCache* c);

// 1 when a count should start now, as the page opens or a count finishes:
// none has finished, or the card changed since the last started, and
// none is running. It then counts as started.
int St_cacheStart(StCache* c);

// The running count finished (ok 1) or was cancelled (ok 0).
void St_cacheDone(StCache* c, int ok);

// 1 while what shows is older than the card.
int St_cacheStale(const StCache* c);

#endif
