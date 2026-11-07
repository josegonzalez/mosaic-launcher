#ifndef LIBRARY_H
#define LIBRARY_H

// What's on the card: entries, directories, recents, and the lists the
// launcher builds from them.

#include "containers.h"

enum EntryType {
	ENTRY_DIR,
	ENTRY_PAK,
	ENTRY_ROM,
	ENTRY_DIP,
};
typedef struct Entry {
	char* path;
	char* name;
	char* unique;
	int type;
	int alpha; // index in parent Directory's alphas Array, which points to the index of an Entry in its entries Array :sweat_smile:
} Entry;

Entry* Entry_new(char* path, int type);
Entry* Entry_newNamed(char* path, int type, char* displayName);
void Entry_free(Entry* self);
int EntryArray_indexOf(Array* self, char* path);
int EntryArray_sortEntry(const void* a, const void* b);
void EntryArray_sort(Array* self);
void EntryArray_free(Array* self);

typedef struct Directory {
	char* path;
	char* name;
	Array* entries;
	IntArray* alphas;
	// rendering
	int selected;
	int start;
	int end;
} Directory;

int getIndexChar(char* str);
void getUniqueName(Entry* entry, char* out_name);

// Listing a directory, in stages. Directory_new runs them in this order.
Array* Listing_source(char* path);
Hash* Listing_loadMap(char* path);
void Listing_applyMap(Array* entries, Hash* map, int* renamed, int* hid_any);
Array* Listing_filterHidden(Array* entries);
void Listing_markDuplicates(Array* entries);
void Listing_buildIndex(Directory* self);

void Directory_index(Directory* self);
Directory* Directory_new(char* path, int selected);
// Lists it again from the card, keeping the selection on the same entry
// where it can.
void Directory_reload(Directory* self);
// Selects a row, scrolling the visible rows to keep it in view.
void Directory_select(Directory* self, int selected);
void Directory_free(Directory* self);
void DirectoryArray_pop(Array* self);
void DirectoryArray_free(Array* self);

typedef struct Recent {
	char* path; // NOTE: this is without the SDCARD_PATH prefix!
	char* alias;
	int available;
} Recent;

#define MAX_RECENTS 24 // a multiple of all menu rows

Recent* Recent_new(char* path, char* alias);
void Recent_free(Recent* self);
int RecentArray_indexOf(Array* self, char* str);
void RecentArray_free(Array* self);
void saveRecents(void);
void addRecent(char* path, char* alias);
// Both save at once. Recents_remove returns 0, or -1 for a bad index.
int Recents_remove(int index);
void Recents_clear(void);

Entry* entryFromPakName(char* pak_name);
int hasEmu(char* emu_name);
int hasCue(char* dir_path, char* cue_path); // NOTE: dir_path not rom_path
int hasM3u(char* rom_path, char* m3u_path); // NOTE: rom_path not dir_path
int hasRecents(void);
int hasCollections(void);
int hasRoms(char* dir_name);
int hasTools(void);

Array* getRoms(void);
Array* getCollections(void);
Array* getQuickEntries(void);
Array* getQuickToggles(void);
Array* getRoot(void);
Entry* entryFromRecent(Recent* recent);
Array* getRecents(void);
Array* getCollection(char* path);
Array* getDiscs(char* path);
int getFirstDisc(char* m3u_path, char* disc_path);
void addEntries(Array* entries, char* path);
int isConsoleDir(char* path);
Array* getEntries(char* path);

#endif
