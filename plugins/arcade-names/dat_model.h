#ifndef DAT_MODEL_H
#define DAT_MODEL_H

// The arcade names plugin's model, without the launcher: the sets read
// from the user's DATs (Logiqx XML, as MAME's -listxml, FBNeo and
// ClrMamePro write it, or ClrMamePro's own text format), marked from a
// catver.ini, kept on the card as a compact cache, and the names and
// hiding for a folder of them.
//
// DATs come from anywhere, so they're read as untrusted: an XML file's
// DOCTYPE, entity declarations included, is skipped and never resolved,
// only the five predefined entities and character references are decoded,
// and every name, description, tag, line, file and the table itself has a
// size limit, past which a value is cut or skipped.

#include <stddef.h>
#include <stdint.h>

#define AN_NAME_LEN 64            // a set's name, its terminator included; longer ones are skipped
#define AN_DESC_LEN 256           // a description, cut to fit
#define AN_TAG_MAX 16384          // an XML tag; a longer one is skipped
#define AN_LINE_MAX 1024          // a catver.ini line; a longer one is skipped
#define AN_SETS_MAX 400000        // sets kept, from every DAT together
#define AN_STRINGS_MAX (64u << 20) // bytes of names and descriptions kept
#define AN_FILE_MAX (2LL << 30)   // bytes read from one file
#define AN_CACHE_VERSION 1

// What the DATs and catver.ini say about a set.
enum {
	AN_BIOS = 1,        // a BIOS set (isbios, or a ClrMamePro resource)
	AN_DEVICE = 2,      // a device set (isdevice, or runnable="no")
	AN_NOT_WORKING = 4, // its driver's status is preliminary
	AN_CATEGORY = 8,    // catver.ini puts it under mahjong, casino or quiz
};

typedef struct ANSet {
	uint32_t name;   // offsets into the table's strings; 0 is ""
	uint32_t desc;
	int32_t parent;  // the set it's a clone of, by index, or -1; before AN_finish, its name's offset
	uint32_t flags;  // AN_*
} ANSet;

typedef struct ANTable {
	char* strings;
	size_t strings_len, strings_cap;
	ANSet* sets; // by name, bytewise, once finished
	int count, cap;
	int finished;
	int full;    // 1 once a limit was reached and sets were left out
} ANTable;

void AN_init(ANTable* t);
void AN_free(ANTable* t);

// Adds the sets of a DAT, telling the formats apart by the first thing in
// it ('<' for XML). Returns how many sets it read, or -1 for a file that
// can't be opened. AN_readDatText reads one already in memory.
int AN_readDat(ANTable* t, const char* path);
int AN_readDatText(ANTable* t, const char* text, size_t len);

// Sorts the sets by name, keeping the first DAT's set where two have the
// same name, points clones at their parents, and drops what's no longer
// needed. Once, after the DATs and before catver.ini. Returns 0, or -1
// when out of memory, leaving the table empty.
int AN_finish(ANTable* t);

// Marks the sets catver.ini's [Category] puts under mahjong, casino or
// quiz. Returns how many it marked, or -1 for a file that can't be opened.
int AN_readCatver(ANTable* t, const char* path);
int AN_readCatverText(ANTable* t, const char* text, size_t len);

// A set by name, without case, or -1.
int AN_find(const ANTable* t, const char* name);
const char* AN_str(const ANTable* t, uint32_t offset);

// Bytes the table holds in memory, for measuring.
size_t AN_bytes(const ANTable* t);

// The cache: the table with a signature of the files it was read from
// (their names, sizes and times), written through a file beside it
// renamed into place. AN_load reads one back only when its signature is
// the same, checking every offset and index; it returns 0, or -1 for a
// missing, stale or malformed cache, leaving the table empty.
int AN_save(const ANTable* t, const char* path, const char* signature);
int AN_load(ANTable* t, const char* path, const char* signature);

// The name stock's list shows for a file name: no extensions (a dot and 2
// to 4 more characters, as many as there are), then nothing from the last
// ( or [ on, again and again until only a leading one is left, and no
// trailing spaces. A name that would end up empty keeps its
// extensionless form.
void AN_stockName(const char* file_name, char* out, size_t size);
// The set a file is: its name without its last extension, in lower case.
// Returns 0 when it can't be one (empty, or too long).
int AN_setOf(const char* file_name, char* out, size_t size);
// A description without its parentheses and brackets: everything before
// the first ( or [ after its start, without trailing spaces, or the whole
// description when that leaves nothing.
void AN_cleanTitle(const char* desc, char* out, size_t size);
// The region in a description's first parentheses: what's there before
// any comma, without dates and other numbers of 4 digits or more, such as
// "World" for "(World 910522)" and "US" for "(US, rev A)". "" when there
// are no parentheses or nothing's left.
void AN_region(const char* desc, char* out, size_t size);
// 1 when tag is in a comma-separated list, without case or spaces around
// the commas.
int AN_hasTag(const char* list, const char* tag);

// What to hide and how to name, from the plugin's settings.
typedef struct ANOptions {
	int full;        // the DAT's full description, rather than a clean title
	int clones;      // hide a clone when its parent is in the folder
	int not_working; // hide sets marked not working
	int categories;  // hide sets catver.ini marks
} ANOptions;

// A folder's files that are sets, by file name bytewise: each one's name,
// or NULL to leave it as it is, and whether it's hidden.
typedef struct ANFolder {
	char** files;
	char** names;
	unsigned char* hidden;
	int count;
} ANFolder;

// Works out a folder's names and hiding from its files. BIOS and device
// sets are always hidden, and the rest as options says, a clone only when
// its parent is in the folder and isn't hidden itself. A clean title that
// two shown sets would share gets the region back, then the whole of the
// first parentheses, then the full description. Returns 0, or -1 when out
// of memory, leaving out empty.
int AN_folder(const ANTable* t, const ANOptions* options, const char* const* files, int count, ANFolder* out);
// A file's index in the folder, or -1.
int AN_folderFind(const ANFolder* f, const char* file_name);
void AN_folderFree(ANFolder* f);

#endif
