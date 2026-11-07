// Genre view, without the host: the names folders and games show as, how
// folder names are matched across systems, the merged folders a listing
// finds, the paths under the Genres folder, and which games get their
// system's tag. No SDL and no file access, so it's tested on the host.

#ifndef GENRE_MODEL_H
#define GENRE_MODEL_H

#include <stddef.h>

#define GV_NAME_LEN 256
#define GV_MAX_DEPTH 8 // folder names a path below the Genres folder can hold

// The name stock shows for a file or folder name: no extensions (a dot
// and 2 to 4 more characters, as many as there are), then nothing from
// the last ( or [ on, again and again until only a leading one is left,
// and no trailing spaces. A name that would end up empty keeps its
// extensionless form.
void GV_cleanName(const char* file_name, char* out, size_t size);

// What folder names are matched by: lowercase ASCII words separated by
// single spaces, so case, spacing and punctuation don't count ("Beat 'em
// up" and "beat-em-up" match). Other bytes, such as UTF-8, stay as they
// are. A name with no letters or digits is matched by its lowercase self.
void GV_key(const char* name, char* out, size_t size);

// True when a comma-separated list names this folder, matched by GV_key.
int GV_listHas(const char* list, const char* name);

// A folder's map.txt, read once and looked up by file name.
typedef struct GVMapItem {
	const char* key;
	const char* value;
	int line;
} GVMapItem;

typedef struct GVMap {
	char* text;
	GVMapItem* items;
	int count;
} GVMap;

// Takes text, the whole file (one "file name<tab>alias" per line), and
// frees it with the map. Lines without a tab or with an empty alias are
// skipped, and for a file name listed twice the later line wins, as in
// stock. Returns 0, or -1 when out of memory (the map is then empty).
int GV_mapParse(GVMap* map, char* text);
// The alias for a file name, or NULL. A NULL map has none.
const char* GV_mapGet(const GVMap* map, const char* file_name);
void GV_mapFree(GVMap* map);

// The name an entry shows as in its folder: its map.txt alias, else its
// cleaned name. Returns 0 when stock would hide it (a dot file, *.disabled,
// map.txt, or an alias starting with a dot).
int GV_shownName(const GVMap* map, const char* file_name, char* out, size_t size);

// Names stock leaves out of its lists: dot files, *.disabled and map.txt.
int GV_hidden(const char* name);

// A system folder's emulator tag: what's in its last parentheses, eg. GB
// for "Game Boy (GB)", or the whole name when it has none.
void GV_emuTag(const char* system, char* out, size_t size);

// A system folder's name as stock's root shows it, collating folders for
// one console: "Game Boy Advance" for "Game Boy Advance (MGBA)".
void GV_systemName(const char* system, char* out, size_t size);

// The system folder a path is in, eg. "Game Boy (GB)" for
// <roms>/Game Boy (GB)/Action/Tetris.gb, written to out. Returns 1 only
// for a path inside a system folder, not the folder itself.
int GV_systemOf(const char* roms, const char* path, char* out, size_t size);

// The folder names a path holds below folder, eg. "Action" and "Arcade"
// for <folder>/Action/Arcade. Returns how many (0 for folder itself), or
// -1 for a path that isn't folder or below it, or holds more than max
// names, an empty one or one too long.
int GV_pathNames(const char* folder, const char* path, char names[][GV_NAME_LEN], int max);

///////////////////////////////////////
// merged folders: same-name folders from different systems

typedef struct GVSource {
	char* path;   // the folder on the card
	char* system; // the system folder it's in, eg. "Game Boy (GB)"
} GVSource;

typedef struct GVGroup {
	char* name; // as the first source added names it
	char* key;  // GV_key of the name
	GVSource* sources;
	int count;
	int cap;
} GVGroup;

typedef struct GVGroups {
	GVGroup* items;
	int count;
	int cap;
} GVGroups;

void GV_groupsInit(GVGroups* groups);
// Frees everything and leaves it empty, ready to fill again.
void GV_groupsFree(GVGroups* groups);
// Adds a folder to the group its name matches, starting one if none does.
// Returns the group's index, or -1 when out of memory or for an empty name
// or one holding a slash.
int GV_groupsAdd(GVGroups* groups, const char* name, const char* path, const char* system);
// The group a name matches, or -1.
int GV_groupsFind(const GVGroups* groups, const char* name);
// By name ignoring case.
void GV_groupsSort(GVGroups* groups);
// How many consoles a group's folders come from, counting folders that
// stock's root collates (eg. GBA and MGBA) as one.
int GV_groupSystems(const GVGroup* group);
// Drops groups from fewer than min consoles and those skip names (a
// comma-separated list, or NULL). Returns how many are left.
int GV_groupsKeep(GVGroups* groups, int min, const char* skip);

///////////////////////////////////////
// a merged folder's list

typedef struct GVItem {
	char* path;
	char* name;
	int type; // the host's entry type
} GVItem;

typedef struct GVItems {
	GVItem* items;
	int count;
	int cap;
} GVItems;

void GV_itemsInit(GVItems* items);
void GV_itemsFree(GVItems* items);
// Returns 0, or -1 when out of memory.
int GV_itemsAdd(GVItems* items, const char* path, const char* name, int type);
// By name ignoring case, as stock sorts a folder, then by path, so the
// same game from two systems keeps one order.
void GV_itemsSort(GVItems* items);

///////////////////////////////////////
// telling games apart

enum {
	GV_TAGS_OFF,    // leave it to stock, which shows file names for neighbours that match
	GV_TAGS_SHARED, // tag games whose name a game from another system has
	GV_TAGS_ALWAYS, // tag every game
};

// The tags setting's value: "off", "shared" or "always". Anything else is
// shared.
int GV_tagsMode(const char* value);

// Marks which of count entries get their system shown. names and systems
// are per entry, systems being NULL for entries that aren't games from a
// system folder (merged folders), which are never marked; tagged gets 1 or
// 0 for each. In GV_TAGS_SHARED names match exactly, as stock's duplicate
// check does. Returns how many were marked, or -1 when out of memory.
int GV_tags(const char* const* names, const char* const* systems, int count, int mode, int* tagged);

#endif
