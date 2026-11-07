#include "launcher.h"

bool isDirectSubdirectory(const Directory* parent, const char* child_path) {
    const char* parent_path = parent->path;

    size_t parent_len = strlen(parent_path);
    size_t child_len = strlen(child_path);

    // Child must be longer than parent to be a subdirectory
    if (child_len <= parent_len || strncmp(child_path, parent_path, parent_len) != 0) {
        return false;
    }

    // Next char after parent path must be '/'
    if (child_path[parent_len] != '/') return false;

    // Walk through the child path after parent, skipping PLATFORM segments
    const char* cursor = child_path + parent_len + 1; // skip the slash

    int levels = 0;
    while (*cursor) {
        const char* next = strchr(cursor, '/');
        size_t segment_len = next ? (size_t)(next - cursor) : strlen(cursor);

        if (segment_len == 0) break;

        // Copy segment into a buffer to compare
        char segment[PATH_MAX];
        if (segment_len >= PATH_MAX) return false;
        strncpy(segment, cursor, segment_len);
        segment[segment_len] = '\0';

        // Count level only if it's not PLATFORM
        if (strcmp(segment, PLATFORM) != 0 && strcmp(segment, "Roms") != 0) {
            levels++;
        }

        if (!next) break;
        cursor = next + 1;
    }

    return (levels == 1);  // exactly one meaningful level deeper
}

Array* pathToStack(const char* path) {
	Array* array = Array_new();

	if (!path || strlen(path) == 0) return array;

	if (!prefixMatch(SDCARD_PATH, path)) return array;

	// Always include root directory
	Directory* root_dir = Directory_new(SDCARD_PATH, 0);
	root_dir->start = 0;
	root_dir->end = (root_dir->entries->count < MAIN_ROW_COUNT) ? root_dir->entries->count : MAIN_ROW_COUNT;
	Array_push(array, root_dir);

	if (exactMatch(path, SDCARD_PATH)) return array;

	char temp_path[PATH_MAX];
	strcpy(temp_path, SDCARD_PATH);
	size_t current_len = strlen(SDCARD_PATH);

	const char* cursor = path + current_len;
	if (*cursor == '/') cursor++;

	while (*cursor) {
		const char* next = strchr(cursor, '/');
		size_t segment_len = next ? (size_t)(next - cursor) : strlen(cursor);
		if (segment_len == 0 || segment_len >= PATH_MAX) break;

		char segment[PATH_MAX];
		strncpy(segment, cursor, segment_len);
		segment[segment_len] = '\0';

		// Append '/' if needed
		if (temp_path[current_len - 1] != '/') {
			if (current_len + 1 >= PATH_MAX) break;
			temp_path[current_len++] = '/';
			temp_path[current_len] = '\0';
		}

		// Append segment
		if (current_len + segment_len >= PATH_MAX) break;
		strcat(temp_path, segment);
		current_len += segment_len;

		if (strcmp(segment, PLATFORM) == 0) {
			// Merge with previous directory
			if (array->count > 0) {
				// Remove the previous directory
				Directory* last = (Directory*)array->items[array->count - 1];
				Array_pop(array);
				Directory_free(last); // assuming you have a Directory_free

				// Replace with updated one using combined path
				Directory* merged = Directory_new(temp_path, 0);
				merged->start = 0;
				merged->end = (merged->entries->count < MAIN_ROW_COUNT) ? merged->entries->count : MAIN_ROW_COUNT;
				Array_push(array, merged);
			}
		} else {
			Directory* dir = Directory_new(temp_path, 0);
			dir->start = 0;
			dir->end = (dir->entries->count < MAIN_ROW_COUNT) ? dir->entries->count : MAIN_ROW_COUNT;
			Array_push(array, dir);
		}

		if (!next) break;
		cursor = next + 1;
	}

	return array;
}

void openDirectory(char* path, int auto_launch) {
	char auto_path[256];
	if (hasCue(path, auto_path) && auto_launch) {
		openRom(auto_path, path);
		return;
	}

	char m3u_path[256];
	strcpy(m3u_path, auto_path);
	char* tmp = strrchr(m3u_path, '.') + 1; // extension
	strcpy(tmp, "m3u"); // replace with m3u
	if (exists(m3u_path) && auto_launch) {
		auto_path[0] = '\0';
		if (getFirstDisc(m3u_path, auto_path)) {
			openRom(auto_path, path);
			return;
		}
		// TODO: doesn't handle empty m3u files
	}

	// If this is the exact same directory for some reason, just return.
	if(state.top && strcmp(state.top->path, path) == 0)
		return;

	// If this path is a direct subdirectory of top, push it on top of the stack
	// If it isnt, we need to recreate the stack to keep navigation consistent
	if(!state.top || isDirectSubdirectory(state.top, path)) {
		int selected = 0;
		int start = 0;
		int end = 0;
		if (state.top && state.top->entries->count>0) {
			if (state.restore_depth==state.stack->count && state.top->selected==state.restore_relative) {
				selected = state.restore_selected;
				start = state.restore_start;
				end = state.restore_end;
			}
		}

		state.top = Directory_new(path, selected);
		state.top->start = start;
		state.top->end = end ? end : ((state.top->entries->count<MAIN_ROW_COUNT) ? state.top->entries->count : MAIN_ROW_COUNT);

		Array_push(state.stack, state.top);
	}
	else {
		// keep a copy of path, which might be a reference into stack which is about to be freed
		char temp_path[256];
		strcpy(temp_path, path);

		// construct a fresh stack by walking upwards until SDCARD_ROOT
		DirectoryArray_free(state.stack);

		state.stack = pathToStack(temp_path);
		state.top = state.stack->items[state.stack->count - 1];
	}
}

void closeDirectory(void) {
	state.restore_selected = state.top->selected;
	state.restore_start = state.top->start;
	state.restore_end = state.top->end;
	DirectoryArray_pop(state.stack);
	state.restore_depth = state.stack->count;
	state.top = state.stack->items[state.stack->count-1];
	state.restore_relative = state.top->selected;
}
void toggleQuick(Entry* self)
{
	if(!self)
		return;

	QuickItem_activate(self);
}

void Entry_open(Entry* self) {
	state.recent_alias = self->name;  // yiiikes
	if (self->type==ENTRY_ROM) {
		state.startgame = 1;
		char *last = NULL;
		if (prefixMatch(COLLECTIONS_PATH, state.top->path)) {
			char* tmp;
			char filename[256];

			tmp = strrchr(self->path, '/');
			if (tmp) strcpy(filename, tmp+1);

			char last_path[256];
			sprintf(last_path, "%s/%s", state.top->path, filename);
			last = last_path;
		}
		// a plugin cancelling the launch leaves us in the menu, not waiting for a game
		if (!openRom(self->path, last)) state.startgame = 0;
	}
	else if (self->type==ENTRY_PAK) {
		state.startgame = 1;
		if (!openPak(self->path)) state.startgame = 0;
	}
	else if (self->type==ENTRY_DIR) {
		openDirectory(self->path, 1);
	}
	else if (self->type==ENTRY_DIP) {
		toggleQuick(self);
	}
}
void saveLast(char* path) {
	// special case for recently played
	if (exactMatch(state.top->path, FAUX_RECENT_PATH)) {
		// NOTE: that we don't have to save the file because
		// your most recently played game will always be at
		// the top which is also the default selection
		path = FAUX_RECENT_PATH;
	}
	putFile(LAST_PATH, path);
}
void loadLast(void) { // call after loading root directory
	if (!exists(LAST_PATH)) return;

	char last_path[256];
	getFile(LAST_PATH, last_path, 256);

	char full_path[256];
	strcpy(full_path, last_path);

	char* tmp;
	char filename[256];
	tmp = strrchr(last_path, '/');
	if (tmp) strcpy(filename, tmp);

	Array* last = Array_new();
	while (!exactMatch(last_path, SDCARD_PATH)) {
		Array_push(last, strdup(last_path));

		char* slash = strrchr(last_path, '/');
		last_path[(slash-last_path)] = '\0';
	}

	while (last->count>0) {
		char* path = Array_pop(last);
		if (!exactMatch(path, ROMS_PATH)) { // romsDir is effectively root as far as restoring state after a game
			char collated_path[256];
			collated_path[0] = '\0';
			if (suffixMatch(")", path) && isConsoleDir(path)) {
				strcpy(collated_path, path);
				tmp = strrchr(collated_path, '(');
				if (tmp) tmp[1] = '\0'; // 1 because we want to keep the opening parenthesis to avoid collating "Game Boy Color" and "Game Boy Advance" into "Game Boy"
			}

			for (int i=0; i<state.top->entries->count; i++) {
				Entry* entry = state.top->entries->items[i];

				// NOTE: strlen() is required for collated_path, '\0' wasn't reading as NULL for some reason
				if (exactMatch(entry->path, path) || (strlen(collated_path) && prefixMatch(collated_path, entry->path)) || (prefixMatch(COLLECTIONS_PATH, full_path) && suffixMatch(filename, entry->path))) {
					state.top->selected = i;
					if (i>=state.top->end) {
						state.top->start = i;
						state.top->end = state.top->start + MAIN_ROW_COUNT;
						if (state.top->end>state.top->entries->count) {
							state.top->end = state.top->entries->count;
							state.top->start = state.top->end - MAIN_ROW_COUNT;
						}
					}
					if (last->count==0 && !exactMatch(entry->path, FAUX_RECENT_PATH) && !(!exactMatch(entry->path, COLLECTIONS_PATH) && prefixMatch(COLLECTIONS_PATH, entry->path))) break; // don't show contents of auto-launch dirs

					if (entry->type==ENTRY_DIR) {
						openDirectory(entry->path, 0);
						break;
					}
				}
			}
		}
		free(path); // we took ownership when we popped it
	}

	StringArray_free(last);

	if (state.top->selected >= 0 && state.top->selected < state.top->entries->count) {
		Entry *selected_entry = state.top->entries->items[state.top->selected];
		readyResume(selected_entry);
	}
}
static void logRow(const char* which, Array* entries) {
	char names[512] = "";
	for (int i = 0; i < entries->count; i++) {
		size_t len = strlen(names);
		Entry* entry = entries->items[i];
		const char* label = QuickTiles_label(entry);
		if (label == entry->name) snprintf(names + len, sizeof(names) - len, "%s%s", i ? "," : "", entry->name);
		else snprintf(names + len, sizeof(names) - len, "%s%s (%s)", i ? "," : "", entry->name, label);
	}
	LOG_info("quick menu: %s row %s\n", which, names);
}
void QuickMenu_init(void) {
	state.rebuild_quick = 0;
	state.quick = getQuickEntries();
	state.quickActions = getQuickToggles();
	// only once plugins have changed the tiles, so stock's log is unchanged
	if (QuickTiles_any()) {
		logRow("top", state.quick);
		logRow("bottom", state.quickActions);
	}
}
void QuickMenu_quit(void) {
	EntryArray_free(state.quick);
	EntryArray_free(state.quickActions);
}
void QuickMenu_rebuild(void) {
	state.rebuild_quick = 0;
	if (!state.quick) return; // not built yet; Menu_init will
	QuickMenu_quit();
	QuickMenu_init();
	QuickMenu_refit();
	state.dirty = 1;
}

void Menu_init(void) {
	state.stack = Array_new(); // array of open Directories
	state.recents = Array_new();

	openDirectory(SDCARD_PATH, 0);
	loadLast(); // restore state when available

	QuickMenu_init(); // needs Menu_init
}
void Menu_quit(void) {
	RecentArray_free(state.recents);
	DirectoryArray_free(state.stack);

	QuickMenu_quit();
}

// Lists every open folder again. A folder other than the root that this
// empties is closed, as if the user had backed out of it.
void Menu_reload(void) {
	state.reload = 0;
	if (!state.stack || state.stack->count == 0) return;

	int count = state.stack->count;
	int emptied[count];
	for (int i = 0; i < count; i++) {
		Directory* dir = state.stack->items[i];
		int had = dir->entries->count;
		Directory_reload(dir);
		emptied[i] = had > 0 && dir->entries->count == 0;
	}
	while (state.stack->count > 1 && emptied[state.stack->count - 1]) DirectoryArray_pop(state.stack);
	state.top = state.stack->items[state.stack->count - 1];

	// recents are read again with the root
	if (state.switcher_selected >= state.recents->count) {
		state.switcher_selected = state.recents->count > 0 ? state.recents->count - 1 : 0;
	}
	if (state.top->entries->count > 0) readyResume(state.top->entries->items[state.top->selected]);
	// eg. so clearing recents drops the Recents tile
	QuickMenu_rebuild();
	state.dirty = 1;
}

// Opens a path as if its entry had been selected. Returns 1 when it opened
// or a launch is queued.
int Entry_openPath(const char* path, int type) {
	if (!path || !path[0] || !state.top) return 0;
	if ((type == ENTRY_ROM || type == ENTRY_PAK) && !exists((char*)path)) return 0;

	// openRom and openPak quote the path in place, so leave them room
	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "%s", path);

	int opened = 0;
	if (type == ENTRY_ROM) {
		Entry* entry = Entry_new(buffer, ENTRY_ROM);
		state.recent_alias = entry->name; // as Entry_open records it
		state.startgame = 1;
		opened = openRom(buffer, NULL);
		state.recent_alias = NULL;
		Entry_free(entry);
	}
	else if (type == ENTRY_PAK) {
		state.startgame = 1;
		opened = openPak(buffer);
	}
	else if (type == ENTRY_DIR) {
		openDirectory(buffer, 1);
		opened = 1;
		if (state.top->entries->count > 0) readyResume(state.top->entries->items[state.top->selected]);
	}
	if (!opened) state.startgame = 0;
	state.dirty = 1;
	return opened;
}
