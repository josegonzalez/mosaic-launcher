#include "launcher.h"

Entry* Entry_new(char* path, int type) {
	char display_name[256];
	getDisplayName(path, display_name);
	Entry* self = malloc(sizeof(Entry));
	self->path = strdup(path);
	self->name = strdup(display_name);
	self->unique = NULL;
	self->type = type;
	self->alpha = 0;
	self->info = NULL;
	self->info_read = 0;
	return self;
}

Entry* Entry_newNamed(char* path, int type, char* displayName) {
	Entry *self = Entry_new(path, type);
	free(self->name);
	self->name = strdup(displayName);
	return self;
}

void Entry_free(Entry* self) {
	free(self->path);
	free(self->name);
	if (self->unique) free(self->unique);
	free(self->info); // one allocation
	free(self);
}

int EntryArray_indexOf(Array* self, char* path) {
	for (int i=0; i<self->count; i++) {
		Entry* entry = self->items[i];
		if (exactMatch(entry->path, path)) return i;
	}
	return -1;
}
int EntryArray_sortEntry(const void* a, const void* b) {
	Entry* item1 = *(Entry**)a;
	Entry* item2 = *(Entry**)b;
	return strcasecmp(item1->name, item2->name);
}
void EntryArray_sort(Array* self) {
	qsort(self->items, self->count, sizeof(void*), EntryArray_sortEntry);
}

void EntryArray_free(Array* self) {
	for (int i=0; i<self->count; i++) {
		Entry_free(self->items[i]);
	}
	Array_free(self);
}
int getIndexChar(char* str) {
	char i = 0;
	char c = tolower(str[0]);
	if (c>='a' && c<='z') i = (c-'a')+1;
	return i;
}

void getUniqueName(Entry* entry, char* out_name) {
	char* filename = strrchr(entry->path, '/')+1;
	char emu_tag[256];
	getEmuName(entry->path, emu_tag);

	char *tmp;
	strcpy(out_name, entry->name);
	tmp = out_name + strlen(out_name);
	strcpy(tmp, " (");
	tmp = out_name + strlen(out_name);
	strcpy(tmp, emu_tag);
	tmp = out_name + strlen(out_name);
	strcpy(tmp, ")");
}

///////////////////////////////////////
// Listing a directory, in stages: source -> map.txt names -> hide -> sort
// -> plugin filter -> plugin display names -> plugin sort -> plugin
// list_ready -> duplicate names -> letter index. Directory_new runs them in
// order.

// Where a directory's entries come from. Most sources already sort.
Array* Listing_source(char* path) {
	if (exactMatch(path, SDCARD_PATH)) {
		return getRoot();
	}
	else if (exactMatch(path, FAUX_RECENT_PATH)) {
		return getRecents();
	}
	else if (exactMatch(path, ROMS_PATH)) {
		return getRoms();
	}
	else if (!exactMatch(path, COLLECTIONS_PATH) && prefixMatch(COLLECTIONS_PATH, path) && suffixMatch(".txt", path)) {
		return getCollection(path);
	}
	else if (suffixMatch(".m3u", path)) {
		return getDiscs(path);
	}
	return getEntries(path);
}

// The file name -> display name map for a directory, or NULL. Collections
// share one map.txt at the top of the collections folder.
Hash* Listing_loadMap(char* path) {
	int is_collection = prefixMatch(COLLECTIONS_PATH, path);
	char map_path[256];
	sprintf(map_path, "%s/map.txt", is_collection ? COLLECTIONS_PATH : path);
	if (!exists(map_path)) return NULL;

	FILE* file = fopen(map_path, "r");
	if (!file) return NULL;

	Hash* map = Hash_new();
	char line[256];
	while (fgets(line, 256, file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (strlen(line) == 0) continue; // skip empty lines

		char* tmp = strchr(line, '\t');
		if (tmp) {
			tmp[0] = '\0';
			char* key = line;
			char* value = tmp + 1;
			Hash_set(map, key, strdup(value)); // Ensure strdup to store value properly
		}
	}
	fclose(file);
	return map;
}

// Renames entries the map knows about. Sets *renamed when any name changed
// and *hid_any when a new name is hidden (eg. starts with a dot).
void Listing_applyMap(Array* entries, Hash* map, int* renamed, int* hid_any) {
	*renamed = 0;
	*hid_any = 0;
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		char* filename = strrchr(entry->path, '/') + 1;
		char* alias = Hash_get(map, filename);
		if (alias) {
			free(entry->name);  // Free before overwriting
			entry->name = strdup(alias);
			*renamed = 1;
			if (!*hid_any && hide(entry->name)) *hid_any = 1;
		}
	}
}

// Drops entries whose display name is hidden. Takes ownership of entries.
Array* Listing_filterHidden(Array* entries) {
	Array* kept = Array_new();
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		if (hide(entry->name)) {
			Entry_free(entry); // Ensure Entry_free handles all memory cleanup
		} else {
			Array_push(kept, entry);
		}
	}
	Array_free(entries);
	return kept;
}

// Neighbours with the same display name get a unique name: their file
// names, or the name plus emulator tag when the file names match too
// (the same game in two collated console folders).
void Listing_markDuplicates(Array* entries) {
	Entry* prior = NULL;
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		if (prior != NULL && exactMatch(prior->name, entry->name)) {
			free(prior->unique);
			free(entry->unique);
			prior->unique = NULL;
			entry->unique = NULL;

			char* prior_filename = strrchr(prior->path, '/') + 1;
			char* entry_filename = strrchr(entry->path, '/') + 1;
			if (exactMatch(prior_filename, entry_filename)) {
				char prior_unique[256];
				char entry_unique[256];
				getUniqueName(prior, prior_unique);
				getUniqueName(entry, entry_unique);

				prior->unique = strdup(prior_unique);
				entry->unique = strdup(entry_unique);
			} else {
				prior->unique = strdup(prior_filename);
				entry->unique = strdup(entry_filename);
			}
		}
		prior = entry;
	}
}

// One letter group per run of entries sharing a first letter, for L1/R1.
void Listing_buildIndex(Directory* self) {
	int alpha = -1;
	int index = 0;
	for (int i = 0; i < self->entries->count; i++) {
		Entry* entry = self->entries->items[i];
		int a = getIndexChar(entry->name);
		if (a != alpha) {
			index = self->alphas->count;
			IntArray_push(self->alphas, i);
			alpha = a;
		}
		entry->alpha = index;
	}
}

void Directory_index(Directory* self) {
	int is_collection = prefixMatch(COLLECTIONS_PATH, self->path);
	int skip_index = exactMatch(FAUX_RECENT_PATH, self->path) || is_collection; // not alphabetized

	Hash* map = Listing_loadMap(self->path);
	if (map) {
		int renamed, hid_any;
		Listing_applyMap(self->entries, map, &renamed, &hid_any);
		if (hid_any) self->entries = Listing_filterHidden(self->entries);
		if (renamed) EntryArray_sort(self->entries);
		Hash_free(map);
	}

	self->entries = Plugins_filter(self->entries, self->path);
	// recents and collections keep their own order even when renamed
	if (Plugins_displayNames(self->entries) && !skip_index) EntryArray_sort(self->entries);
	// plugins' order goes on top of the launcher's, so it comes after any
	// sort by name
	Plugins_sort(self->entries, self->path);
	if (Plugins_listReady(self->entries, self->path) && !skip_index) {
		EntryArray_sort(self->entries);
		Plugins_sort(self->entries, self->path);
	}

	Listing_markDuplicates(self->entries);
	if (!skip_index) Listing_buildIndex(self);
	// once the names and order are final
	free(self->names_hidden);
	self->names_hidden = Plugins_hideNames(self->entries, self->path);
}


Directory* Directory_new(char* path, int selected) {
	char display_name[256];
	getDisplayName(path, display_name);

	Directory* self = malloc(sizeof(Directory));
	self->path = strdup(path);
	self->name = strdup(display_name);
	self->entries = Plugins_virtualDir(path);
	if (!self->entries) self->entries = Listing_source(path);
	self->alphas = IntArray_new();
	self->names_hidden = NULL;
	self->selected = selected;
	Directory_index(self);
	return self;
}

// Lists the directory again, after something on the card changed. The
// selection follows the selected entry's path, or stays on the same row if
// that entry is gone, and the visible rows move to keep it in view.
void Directory_reload(Directory* self) {
	char* was = NULL;
	if (self->selected >= 0 && self->selected < self->entries->count) {
		was = strdup(((Entry*)self->entries->items[self->selected])->path);
	}
	EntryArray_free(self->entries);
	IntArray_free(self->alphas);
	self->entries = Plugins_virtualDir(self->path);
	if (!self->entries) self->entries = Listing_source(self->path);
	self->alphas = IntArray_new();
	Directory_index(self);

	int count = self->entries->count;
	int selected = was ? EntryArray_indexOf(self->entries, was) : -1;
	free(was);
	if (selected < 0) selected = self->selected < count ? self->selected : count - 1;
	if (selected < 0) selected = 0;
	Directory_select(self, selected);
}

// Selects a row, moving the visible rows only as far as it takes to show it.
void Directory_select(Directory* self, int selected) {
	int count = self->entries->count;
	self->selected = selected;

	int rows = MAIN_ROW_COUNT;
	if (self->start > selected) self->start = selected;
	if (selected >= self->start + rows) self->start = selected - rows + 1;
	self->end = self->start + rows;
	if (self->end > count) {
		self->end = count;
		self->start = count > rows ? count - rows : 0;
	}
}

void Directory_free(Directory* self) {
	free(self->path);
	free(self->name);
	EntryArray_free(self->entries);
	IntArray_free(self->alphas);
	free(self->names_hidden);
	free(self);
}

void DirectoryArray_pop(Array* self) {
	Directory_free(Array_pop(self));
}
void DirectoryArray_free(Array* self) {
	for (int i=0; i<self->count; i++) {
		Directory_free(self->items[i]);
	}
	Array_free(self);
}
Recent* Recent_new(char* path, char* alias) {
	Recent* self = malloc(sizeof(Recent));

	char sd_path[256]; // only need to get emu name
	sprintf(sd_path, "%s%s", SDCARD_PATH, path);

	char emu_name[256];
	getEmuName(sd_path, emu_name);

	self->path = strdup(path);
	self->alias = alias ? strdup(alias) : NULL;
	self->available = hasEmu(emu_name);
	return self;
}
void Recent_free(Recent* self) {
	free(self->path);
	if (self->alias) free(self->alias);
	free(self);
}

int RecentArray_indexOf(Array* self, char* str) {
	for (int i=0; i<self->count; i++) {
		Recent* item = self->items[i];
		if (exactMatch(item->path, str)) return i;
	}
	return -1;
}
void RecentArray_free(Array* self) {
	for (int i=0; i<self->count; i++) {
		Recent_free(self->items[i]);
	}
	Array_free(self);
}
void saveRecents(void) {
	FILE* file = fopen(RECENT_PATH, "w");
	if (file) {
		for (int i=0; i<state.recents->count; i++) {
			Recent* recent = state.recents->items[i];
			fputs(recent->path, file);
			if (recent->alias) {
				fputs("\t", file);
				fputs(recent->alias, file);
			}
			putc('\n', file);
		}
		fclose(file);
	}
}
void addRecent(char* path, char* alias) {
	path += strlen(SDCARD_PATH); // makes paths platform agnostic
	int id = RecentArray_indexOf(state.recents, path);
	if (id==-1) { // add
		while (state.recents->count>=MAX_RECENTS) {
			Recent_free(Array_pop(state.recents));
		}
		Array_unshift(state.recents, Recent_new(path, alias));
	}
	else if (id>0) { // bump to top
		for (int i=id; i>0; i--) {
			void* tmp = state.recents->items[i-1];
			state.recents->items[i-1] = state.recents->items[i];
			state.recents->items[i] = tmp;
		}
	}
	saveRecents();
}

int Recents_remove(int index) {
	if (!state.recents || index < 0 || index >= state.recents->count) return -1;
	Recent* recent = state.recents->items[index];
	Array_remove(state.recents, recent);
	Recent_free(recent);
	saveRecents();
	return 0;
}
int Recents_set(int index, const char* path) {
	if (!state.recents || index < 0 || index >= state.recents->count || !path) return -1;
	size_t len = strlen(SDCARD_PATH);
	if (strncmp(path, SDCARD_PATH, len) != 0 || path[len] != '/' || strlen(path) >= 256) return -1;
	const char* rel = path + len; // card-relative, as recent.txt holds it
	int at = RecentArray_indexOf(state.recents, (char*)rel);
	if (at == index) return 0;
	if (at >= 0) return -1;
	Recent* old = state.recents->items[index];
	state.recents->items[index] = Recent_new((char*)rel, old->alias);
	Recent_free(old);
	saveRecents();
	return 0;
}
void Recents_clear(void) {
	if (!state.recents) return;
	while (state.recents->count > 0) Recent_free(Array_pop(state.recents));
	saveRecents();
}

Entry* entryFromPakName(char* pak_name)
{
	char pak_path[256];
	// Check in Tools
	sprintf(pak_path, "%s/Tools/%s/%s.pak", SDCARD_PATH, PLATFORM, pak_name);
	if(exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	// Check in Emus
	sprintf(pak_path, "%s/Emus/%s.pak", PAKS_PATH, pak_name);
	if(exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	// Check in platform Emus
	sprintf(pak_path, "%s/Emus/%s/%s.pak", SDCARD_PATH, PLATFORM, pak_name);
	if(exists(pak_path))
		return Entry_newNamed(pak_path, ENTRY_PAK, pak_name);

	return NULL;
}

int hasEmu(char* emu_name) {
	char pak_path[256];
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", PAKS_PATH, emu_name);
	if (exists(pak_path)) return 1;

	sprintf(pak_path, "%s/Emus/%s/%s.pak/launch.sh", SDCARD_PATH, PLATFORM, emu_name);
	return exists(pak_path);
}
int hasCue(char* dir_path, char* cue_path) { // NOTE: dir_path not rom_path
	char* tmp = strrchr(dir_path, '/') + 1; // folder name
	sprintf(cue_path, "%s/%s.cue", dir_path, tmp);
	return exists(cue_path);
}
int hasM3u(char* rom_path, char* m3u_path) { // NOTE: rom_path not dir_path
	char* tmp;

	strcpy(m3u_path, rom_path);
	tmp = strrchr(m3u_path, '/') + 1;
	tmp[0] = '\0';

	// path to parent directory
	char base_path[256];
	strcpy(base_path, m3u_path);

	tmp = strrchr(m3u_path, '/');
	tmp[0] = '\0';

	// get parent directory name
	char dir_name[256];
	tmp = strrchr(m3u_path, '/');
	strcpy(dir_name, tmp);

	// dir_name is also our m3u file name
	tmp = m3u_path + strlen(m3u_path);
	strcpy(tmp, dir_name);

	// add extension
	tmp = m3u_path + strlen(m3u_path);
	strcpy(tmp, ".m3u");

	return exists(m3u_path);
}

// True when a card-relative path is already in recents or on a line of
// recent.txt, where it may come later.
static int recentListed(const char* path) {
	if (RecentArray_indexOf(state.recents, (char*)path) >= 0) return 1;
	FILE* file = fopen(RECENT_PATH, "r");
	if (!file) return 0;
	int listed = 0;
	char line[256];
	while (!listed && fgets(line, sizeof(line), file)) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		char* tab = strchr(line, '\t');
		if (tab) *tab = '\0';
		listed = !strcmp(line, path);
	}
	fclose(file);
	return listed;
}

// A plugin's new path for a recent whose file is missing, written to out
// as a full path: only one on the card that isn't a recent already.
static int repairRecent(const char* missing, char* out, size_t size) {
	if (!Plugins_repairPath(missing, out, size)) return 0;
	size_t len = strlen(SDCARD_PATH);
	if (strncmp(out, SDCARD_PATH, len) != 0 || out[len] != '/' || strlen(out) >= 256
			|| strstr(out, "/../") || suffixMatch("/..", out) || !exists(out) || recentListed(out + len)) {
		LOG_warn("recents: can't repair %s with %s\n", missing, out);
		return 0;
	}
	LOG_info("recents: repaired %s as %s\n", missing, out);
	return 1;
}

int hasRecents(void) {
	LOG_info("hasRecents %s\n", RECENT_PATH);
	int has = 0;
	RecentArray_free(state.recents);
	state.recents = Array_new();

	Array* parent_paths = Array_new();
	if (exists(CHANGE_DISC_PATH)) {
		char sd_path[256];
		getFile(CHANGE_DISC_PATH, sd_path, 256);
		if (exists(sd_path)) {
			char* disc_path = sd_path + strlen(SDCARD_PATH); // makes path platform agnostic
			Recent* recent = Recent_new(disc_path, NULL);
			if (recent->available) has += 1;
			Array_push(state.recents, recent);

			char parent_path[256];
			strcpy(parent_path, disc_path);
			char* tmp = strrchr(parent_path, '/') + 1;
			tmp[0] = '\0';
			Array_push(parent_paths, strdup(parent_path));
		}
		unlink(CHANGE_DISC_PATH);
	}

	FILE *file = fopen(RECENT_PATH, "r"); // newest at top
	if (file) {
		char line[256];
		while (fgets(line,256,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines

			// LOG_info("line: %s\n", line);

			char* path = line;
			char* alias = NULL;
			char* tmp = strchr(line,'\t');
			if (tmp) {
				tmp[0] = '\0';
				alias = tmp+1;
			}

			char sd_path[256];
			sprintf(sd_path, "%s%s", SDCARD_PATH, path);
			// plugins only see lines stock would drop
			char repaired[256];
			if (!exists(sd_path) && repairRecent(sd_path, repaired, sizeof(repaired))) {
				snprintf(sd_path, sizeof(sd_path), "%s", repaired);
				path = sd_path + strlen(SDCARD_PATH);
			}
			if (exists(sd_path)) {
				if (state.recents->count<MAX_RECENTS) {
					// this logic replaces an existing disc from a multi-disc game with the last used
					char m3u_path[256];
					if (hasM3u(sd_path, m3u_path)) { // TODO: this might tank launch speed
						char parent_path[256];
						strcpy(parent_path, path);
						char* tmp = strrchr(parent_path, '/') + 1;
						tmp[0] = '\0';

						int found = 0;
						for (int i=0; i<parent_paths->count; i++) {
							char* path = parent_paths->items[i];
							if (prefixMatch(path, parent_path)) {
								found = 1;
								break;
							}
						}
						if (found) continue;

						Array_push(parent_paths, strdup(parent_path));
					}

					// LOG_info("path:%s alias:%s\n", path, alias);

					Recent* recent = Recent_new(path, alias);
					if (recent->available) has += 1;
					Array_push(state.recents, recent);
				}
			}
		}
		fclose(file);
	}

	saveRecents();

	StringArray_free(parent_paths);
	return has>0;
}
int hasCollections(void) {
	int has = 0;
	if (!exists(COLLECTIONS_PATH)) return has;

	DIR *dh = opendir(COLLECTIONS_PATH);
	struct dirent *dp;
	while((dp = readdir(dh)) != NULL) {
		if (hide(dp->d_name)) continue;
		has = 1;
		break;
	}
	closedir(dh);
	return has;
}
int hasRoms(char* dir_name) {
	int has = 0;
	char emu_name[256];
	char rom_path[256];

	getEmuName(dir_name, emu_name);

	// check for emu pak
	if (!hasEmu(emu_name)) return has;

	// check for at least one non-hidden file (we're going to assume it's a rom)
	sprintf(rom_path, "%s/%s/", ROMS_PATH, dir_name);
	DIR *dh = opendir(rom_path);
	if (dh!=NULL) {
		struct dirent *dp;
		while((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name)) continue;
			has = 1;
			break;
		}
		closedir(dh);
	}
	// if (!has) printf("No roms for %s!\n", dir_name);
	return has;
}

int hasTools(void) {
	char tools_path[256];
    snprintf(tools_path, sizeof(tools_path), "%s/Tools/%s", SDCARD_PATH, PLATFORM);
	return exists(tools_path);
}

Array* getRoms()
{
	Array* entries = Array_new();
    DIR* dh = opendir(ROMS_PATH);
    if (dh) {
        struct dirent* dp;
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/", ROMS_PATH);
        char* tmp = full_path + strlen(full_path);

        Array* emus = Array_new();
        while ((dp = readdir(dh)) != NULL) {
            if (hide(dp->d_name)) continue;
            if (hasRoms(dp->d_name)) {
                strcpy(tmp, dp->d_name);
                Array_push(emus, Entry_new(full_path, ENTRY_DIR));
            }
        }
        closedir(dh); // Ensure directory is closed immediately after use

        EntryArray_sort(emus);
        Entry* prev_entry = NULL;
        for (int i = 0; i < emus->count; i++) {
            Entry* entry = emus->items[i];
            if (prev_entry && exactMatch(prev_entry->name, entry->name)) {
                Entry_free(entry);
                continue;
            }
            Array_push(entries, entry);
            prev_entry = entry;
        }
        Array_free(emus); // Only frees container, entries now owns the items
    }

	// Handle mapping logic
    char map_path[256];
    snprintf(map_path, sizeof(map_path), "%s/map.txt", ROMS_PATH);
    if (entries->count > 0 && exists(map_path)) {
        FILE* file = fopen(map_path, "r");
        if (file) {
            Hash* map = Hash_new();
            char line[256];

            while (fgets(line, sizeof(line), file)) {
                normalizeNewline(line);
                trimTrailingNewlines(line);
                if (strlen(line) == 0) continue; // Skip empty lines

                char* tmp = strchr(line, '\t');
                if (tmp) {
                    *tmp = '\0';
                    char* key = line;
                    char* value = tmp + 1;
                    Hash_set(map, key, strdup(value)); // Ensure strdup
                }
            }
            fclose(file);

            int resort = 0;
            for (int i = 0; i < entries->count; i++) {
                Entry* entry = entries->items[i];
                char* filename = strrchr(entry->path, '/') + 1;
                char* alias = Hash_get(map, filename);
                if (alias) {
                    free(entry->name);  // Free before overwriting
                    entry->name = strdup(alias);
                    resort = 1;
                }
            }
            if (resort) EntryArray_sort(entries);
            Hash_free(map);
        }
    }

	return entries;
}

Array* getCollections(void)
{
	DIR* dh = opendir(COLLECTIONS_PATH);
	if (dh) {
		struct dirent* dp;
		char full_path[256];
		snprintf(full_path, sizeof(full_path), "%s/", COLLECTIONS_PATH);
		char* tmp = full_path + strlen(full_path);

		Array* collections = Array_new();
		while ((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name)) continue;
			strcpy(tmp, dp->d_name);
			Array_push(collections, Entry_new(full_path, ENTRY_DIR)); // Collections are fake directories
		}
		closedir(dh); // Close immediately after use
		EntryArray_sort(collections);
		return collections;
	}
	return NULL;
}

Array* getQuickEntries(void) {
	Array* entries = Array_new();

	// We assume Menu_init was already called and populated this
	if (state.recents && state.recents->count)
		Array_push(entries, Entry_newNamed(FAUX_RECENT_PATH, ENTRY_DIR, "Recents"));

	if (hasCollections())
		Array_push(entries, Entry_new(COLLECTIONS_PATH, ENTRY_DIR));

	// Not sure we need this, its just a button press away (B)
	Array_push(entries, Entry_newNamed(ROMS_PATH, ENTRY_DIR, "Games"));

	// Add tools if applicable
    if (hasTools() && !state.simple_mode) {
		char tools_path[256];
		snprintf(tools_path, sizeof(tools_path), "%s/Tools/%s", SDCARD_PATH, PLATFORM);
        Array_push(entries, Entry_new(tools_path, ENTRY_DIR));
    }

	QuickTiles_apply(entries, QUICK_ROW_TOP);
	return entries;
}

Array* getQuickToggles(void) {
	Array* entries = QuickItem_entries();
	QuickTiles_apply(entries, QUICK_ROW_BOTTOM);
	return entries;
}

Array* getRoot(void) {
	// loads state.recents, which the game switcher and quick menu use even
	// when a plugin replaces the root
	int has_recents = hasRecents();

	int with_entries = 0;
	Array* replaced = Plugins_rootReplace(&with_entries);
	if (replaced) {
		// only when the slot's owner asks for them
		if (with_entries) Plugins_rootEntries(replaced);
		return replaced;
	}

    Array* root = Array_new();

    if (has_recents && CFG_getShowRecents())
		Array_push(root, Entry_new(FAUX_RECENT_PATH, ENTRY_DIR));

	Array *entries = getRoms();

	// Handle collections
	if (hasCollections() && CFG_getShowCollections()) {
        if (entries->count) {
            Array_push(root, Entry_new(COLLECTIONS_PATH, ENTRY_DIR));
        } else { // No visible systems, promote collections to root
			Array *collections = getCollections();
			Array_yoink(entries, collections);
        }
    }

    // Move entries to root
	Array_yoink(root, entries);

	// plugins' entries go after the consoles, before Tools
	Plugins_rootEntries(root);

	// Add tools if applicable
    if (hasTools() && CFG_getShowTools() && !state.simple_mode) {
		char tools_path[256];
		snprintf(tools_path, sizeof(tools_path), "%s/Tools/%s", SDCARD_PATH, PLATFORM);
        Array_push(root, Entry_new(tools_path, ENTRY_DIR));
    }

    return root;
}

Entry* entryFromRecent(Recent* recent)
{
	if(!recent || !recent->available)
		return NULL;

	char sd_path[256];
	sprintf(sd_path, "%s%s", SDCARD_PATH, recent->path);
	int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM; // ???
	Entry* entry = Entry_new(sd_path, type);
	if (recent->alias) {
		free(entry->name);
		entry->name = strdup(recent->alias);
	}
	return entry;
}

Array* getRecents(void) {
	Array* entries = Array_new();
	for (int i=0; i<state.recents->count; i++) {
		Recent *recent = state.recents->items[i];
		Entry *entry = entryFromRecent(recent);
		if(entry)
			Array_push(entries, entry);
	}
	return entries;
}

Array* getCollection(char* path) {
	Array* entries = Array_new();
	FILE* file = fopen(path, "r");
	if (file) {
		char line[256];
		while (fgets(line,256,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines

			char sd_path[256];
			sprintf(sd_path, "%s%s", SDCARD_PATH, line);
			if (exists(sd_path)) {
				int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM; // ???
				Array_push(entries, Entry_new(sd_path, type));

				// char emu_name[256];
				// getEmuName(sd_path, emu_name);
				// if (hasEmu(emu_name)) {
					// Array_push(entries, Entry_new(sd_path, ENTRY_ROM));
				// }
			}
		}
		fclose(file);
	}
	return entries;
}
Array* getDiscs(char* path){

	// TODO: does path have SDCARD_PATH prefix?

	Array* entries = Array_new();

	char base_path[256];
	strcpy(base_path, path);
	char* tmp = strrchr(base_path, '/') + 1;
	tmp[0] = '\0';

	// TODO: limit number of discs supported (to 9?)
	FILE* file = fopen(path, "r");
	if (file) {
		char line[256];
		int disc = 0;
		while (fgets(line,256,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines

			char disc_path[256];
			sprintf(disc_path, "%s%s", base_path, line);

			if (exists(disc_path)) {
				disc += 1;
				Entry* entry = Entry_new(disc_path, ENTRY_ROM);
				free(entry->name);
				char name[16];
				sprintf(name, "Disc %i", disc);
				entry->name = strdup(name);
				Array_push(entries, entry);
			}
		}
		fclose(file);
	}
	return entries;
}
int getFirstDisc(char* m3u_path, char* disc_path) { // based on getDiscs() natch
	int found = 0;

	char base_path[256];
	strcpy(base_path, m3u_path);
	char* tmp = strrchr(base_path, '/') + 1;
	tmp[0] = '\0';

	FILE* file = fopen(m3u_path, "r");
	if (file) {
		char line[256];
		while (fgets(line,256,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines

			sprintf(disc_path, "%s%s", base_path, line);

			if (exists(disc_path)) found = 1;
			break;
		}
		fclose(file);
	}
	return found;
}

void addEntries(Array* entries, char* path) {
	DIR *dh = opendir(path);
	if (dh!=NULL) {
		struct dirent *dp;
		char* tmp;
		char full_path[256];
		sprintf(full_path, "%s/", path);
		tmp = full_path + strlen(full_path);
		while((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name)) continue;
			strcpy(tmp, dp->d_name);
			int is_dir = dp->d_type==DT_DIR;
			int type;
			if (is_dir) {
				// TODO: this should make sure launch.sh exists
				if (suffixMatch(".pak", dp->d_name)) {
					type = ENTRY_PAK;
				}
				else {
					type = ENTRY_DIR;
				}
			}
			else {
				if (prefixMatch(COLLECTIONS_PATH, full_path)) {
					type = ENTRY_DIR; // :shrug:
				}
				else {
					type = ENTRY_ROM;
				}
			}
			Array_push(entries, Entry_new(full_path, type));
		}
		closedir(dh);
	}
}

int isConsoleDir(char* path) {
	char* tmp;
	char parent_dir[256];
	strcpy(parent_dir, path);
	tmp = strrchr(parent_dir, '/');
	tmp[0] = '\0';

	return exactMatch(parent_dir, ROMS_PATH);
}

Array* getEntries(char* path){
	Array* entries = Array_new();

	if (isConsoleDir(path)) { // top-level console folder, might collate
		char collated_path[256];
		strcpy(collated_path, path);
		char* tmp = strrchr(collated_path, '(');
		// 1 because we want to keep the opening parenthesis to avoid collating "Game Boy Color" and "Game Boy Advance" into "Game Boy"
		// but conditional so we can continue to support a bare tag name as a folder name
		if (tmp) tmp[1] = '\0';

		DIR *dh = opendir(ROMS_PATH);
		if (dh!=NULL) {
			struct dirent *dp;
			char full_path[256];
			sprintf(full_path, "%s/", ROMS_PATH);
			tmp = full_path + strlen(full_path);
			// while loop so we can collate paths, see above
			while((dp = readdir(dh)) != NULL) {
				if (hide(dp->d_name)) continue;
				if (dp->d_type!=DT_DIR) continue;
				strcpy(tmp, dp->d_name);

				if (!prefixMatch(collated_path, full_path)) continue;
				addEntries(entries, full_path);
			}
			closedir(dh);
		}
	}
	else addEntries(entries, path); // just a subfolder

	EntryArray_sort(entries);
	return entries;
}
