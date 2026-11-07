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
	if(top && strcmp(top->path, path) == 0)
		return;

	// If this path is a direct subdirectory of top, push it on top of the stack
	// If it isnt, we need to recreate the stack to keep navigation consistent
	if(!top || isDirectSubdirectory(top, path)) {
		int selected = 0;
		int start = 0;
		int end = 0;
		if (top && top->entries->count>0) {
			if (restore_depth==stack->count && top->selected==restore_relative) {
				selected = restore_selected;
				start = restore_start;
				end = restore_end;
			}
		}

		top = Directory_new(path, selected);
		top->start = start;
		top->end = end ? end : ((top->entries->count<MAIN_ROW_COUNT) ? top->entries->count : MAIN_ROW_COUNT);

		Array_push(stack, top);
	}
	else {
		// keep a copy of path, which might be a reference into stack which is about to be freed
		char temp_path[256];
		strcpy(temp_path, path);

		// construct a fresh stack by walking upwards until SDCARD_ROOT
		DirectoryArray_free(stack);

		stack = pathToStack(temp_path);
		top = stack->items[stack->count - 1];
	}
}

void closeDirectory(void) {
	restore_selected = top->selected;
	restore_start = top->start;
	restore_end = top->end;
	DirectoryArray_pop(stack);
	restore_depth = stack->count;
	top = stack->items[stack->count-1];
	restore_relative = top->selected;
}
void toggleQuick(Entry* self)
{
	if(!self)
		return;

	if(!strcmp(self->name, "Wifi")) {
		WIFI_enable(!WIFI_enabled());
	}
	else if(!strcmp(self->name, "Bluetooth")) {
		BT_enable(!BT_enabled());
	}
	else if(!strcmp(self->name, "Sleep")) {
		PWR_sleep();
	}
	else if(!strcmp(self->name, "Reboot")) {
		cleanupImageLoaderPool();  // Stop worker threads before SDL cleanup
		PWR_powerOff(1);
	}
	else if(!strcmp(self->name, "Poweroff")) {
		cleanupImageLoaderPool();  // Stop worker threads before SDL cleanup
		PWR_powerOff(0);
	}
}

void Entry_open(Entry* self) {
	recent_alias = self->name;  // yiiikes
	if (self->type==ENTRY_ROM) {
		startgame = 1;
		char *last = NULL;
		if (prefixMatch(COLLECTIONS_PATH, top->path)) {
			char* tmp;
			char filename[256];

			tmp = strrchr(self->path, '/');
			if (tmp) strcpy(filename, tmp+1);

			char last_path[256];
			sprintf(last_path, "%s/%s", top->path, filename);
			last = last_path;
		}
		openRom(self->path, last);
	}
	else if (self->type==ENTRY_PAK) {
		startgame = 1;
		openPak(self->path);
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
	if (exactMatch(top->path, FAUX_RECENT_PATH)) {
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

			for (int i=0; i<top->entries->count; i++) {
				Entry* entry = top->entries->items[i];

				// NOTE: strlen() is required for collated_path, '\0' wasn't reading as NULL for some reason
				if (exactMatch(entry->path, path) || (strlen(collated_path) && prefixMatch(collated_path, entry->path)) || (prefixMatch(COLLECTIONS_PATH, full_path) && suffixMatch(filename, entry->path))) {
					top->selected = i;
					if (i>=top->end) {
						top->start = i;
						top->end = top->start + MAIN_ROW_COUNT;
						if (top->end>top->entries->count) {
							top->end = top->entries->count;
							top->start = top->end - MAIN_ROW_COUNT;
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

	if (top->selected >= 0 && top->selected < top->entries->count) {
		Entry *selected_entry = top->entries->items[top->selected];
		readyResume(selected_entry);
	}
}
void QuickMenu_init(void) {
	quick = getQuickEntries();
	quickActions = getQuickToggles();
}
void QuickMenu_quit(void) {
	EntryArray_free(quick);
	EntryArray_free(quickActions);
}

void Menu_init(void) {
	stack = Array_new(); // array of open Directories
	recents = Array_new();

	openDirectory(SDCARD_PATH, 0);
	loadLast(); // restore state when available

	QuickMenu_init(); // needs Menu_init
}
void Menu_quit(void) {
	RecentArray_free(recents);
	DirectoryArray_free(stack);

	QuickMenu_quit();
}
