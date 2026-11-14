#ifndef INDEXMODEL_H
#define INDEXMODEL_H

// The card index without SDL or threads: every system folder in Roms,
// every folder in them and every game, as stock lists them, with each
// entry's name (its map.txt alias, else its tidied file name) and the
// year, publisher and genre of its .info sidecar. It's built by reading
// the card, reusing what an earlier index read of a system whose folders
// haven't changed, and kept on the card as a text file, so the next start
// only checks the folders' times. cardindex.c runs it on a thread of its
// own and hands it to plugins (index_count and the rest).

#include <stddef.h>
#include <stdint.h>

#define INDEX_DEPTH_MAX 7 // folders inside a system folder that are read
#define INDEX_FILE_VERSION 1
#define INDEX_UNSURE -2                 // a stamp that's always read again
#define INDEX_RACY_NS 3000000000LL      // changes this recent may hide another (FAT keeps 2 s)

enum {
	INDEX_KIND_SYSTEM = 0, // as enum MosaicIndexKind
	INDEX_KIND_FOLDER = 1,
	INDEX_KIND_GAME = 2,
};

// Offsets into the system's strings; 0 is "".
typedef struct IndexEntry {
	uint32_t path, launch, name, year, publisher, genre;
	uint8_t kind;
	uint8_t type;  // MOSAIC_ENTRY_*
	int8_t depth;  // -1 for the system folder
} IndexEntry;

// A folder, .media folder or map.txt the system's entries were read
// from, and its modification time then, in nanoseconds, or INDEX_UNSURE.
typedef struct IndexStamp {
	uint32_t path;
	long long mtime;
} IndexStamp;

typedef struct IndexSystem {
	char* folder;   // its name in Roms, such as "Game Boy (GB)"
	char* tag;      // its emulator tag, such as "GB"
	int emulator;   // 1 when the tag has an emulator pak
	int first;      // its first entry's index among every system's
	char* strings;
	size_t strings_len, strings_cap;
	IndexEntry* entries; // by path, bytewise; the system folder first
	int count, cap;
	IndexStamp* stamps;
	int stamp_count, stamp_cap;
} IndexSystem;

typedef struct CardIndex {
	char* roms;            // <card>/Roms
	long long roms_mtime;  // Roms, -1 when it wasn't there
	long long map_mtime;   // Roms/map.txt, -1 when it wasn't there
	IndexSystem* systems;  // by folder name, bytewise
	int system_count;
	int total;             // entries across every system
} CardIndex;

// Where emulators are, as stock looks for a system's: the card and the
// platform, for <card>/.system/<platform>/paks/Emus/<tag>.pak/launch.sh
// and <card>/Emus/<platform>/<tag>.pak/launch.sh.
typedef struct IndexCard {
	const char* sdcard;
	const char* platform;
} IndexCard;

// What a build did.
typedef struct IndexBuildStats {
	int systems_read;   // read from the card
	int systems_kept;   // taken from the earlier index unchanged
	int changed;        // 1 when its entries differ from the earlier index's
	int restamped;      // 1 when it read something again, so it's worth keeping even when unchanged
} IndexBuildStats;

void Index_init(CardIndex* index);
// Frees everything and leaves it empty.
void Index_free(CardIndex* index);

// Builds index from the card. A system of prev (NULL for none) whose
// folders, .media folders and map.txt files all have the times prev read
// them at is copied rather than read again. One that changed in the last
// few seconds is kept INDEX_UNSURE, so the next build reads it again. Stops early, returning -1,
// once *cancel (when it isn't NULL) is set. Returns 0, or -1 when out of
// memory or cancelled, leaving index empty.
int Index_build(CardIndex* index, const IndexCard* card, const CardIndex* prev, volatile int* cancel, IndexBuildStats* stats);

// Writes index to path as text, through a file beside it renamed into
// place. Returns 0, or -1 when it couldn't be written.
int Index_save(const CardIndex* index, const char* path);
// Reads what Index_save wrote, for an index of roms (<card>/Roms). Returns
// 0, or -1 for a file that's missing, malformed, for another version or
// another card, leaving index empty.
int Index_load(CardIndex* index, const char* path, const char* roms);

// The entries in a system folder (NULL for every system), 0 for one that
// isn't there.
int Index_count(const CardIndex* index, const char* system);
// The entry at i in a system folder (NULL for every system), with its
// system in *sys, or NULL past the end.
const IndexEntry* Index_at(const CardIndex* index, const char* system, int i, const IndexSystem** sys);
// The index among every system's of the entry at path, with it and its
// system in *entry and *sys, or -1.
int Index_find(const CardIndex* index, const char* path, const IndexEntry** entry, const IndexSystem** sys);
// One of an entry's strings.
const char* Index_str(const IndexSystem* sys, uint32_t offset);

// The name stock shows for a file or folder name: no extensions (a dot
// and 2 to 4 more characters, as many as there are), then nothing from
// the last ( or [ on, again and again until only a leading one is left,
// and no trailing spaces. A name that would end up empty keeps its
// extensionless form.
void Index_cleanName(const char* file_name, char* out, size_t size);
// A system folder's emulator tag: what's in its last parentheses, or the
// whole name when it has none.
void Index_emuTag(const char* system, char* out, size_t size);

// Bytes the index holds in memory, for measuring.
size_t Index_bytes(const CardIndex* index);

#endif
