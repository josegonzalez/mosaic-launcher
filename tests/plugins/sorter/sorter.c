// A plugin that orders and renames a folder's entries, then reads the
// list and moves the selection once the launcher is up, logging what each
// host call returned.
//
// The root gets one virtual folder, <sd>/Sorter. Its sort_compare puts
// the pinned game first, and its list_ready gives the two games titled
// Tetris their extensions.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "sorter"

static const MosaicHost* host;
static char folder[512];
static int ticks = 0;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	char message[700];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	host->log(ID, message);
}

static void rootEntries(MosaicList* root) {
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Sorter"));
}

static void addGame(MosaicList* list, const char* file, const char* name) {
	char path[600];
	snprintf(path, sizeof(path), "%s/%s", folder, file);
	host->list_push(list, host->entry_new(path, MOSAIC_ENTRY_ROM, name));
}

static MosaicList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	MosaicList* list = host->list_new();
	addGame(list, "Tetris.gbc", "Tetris");
	addGame(list, "Beta.gb", "Beta");
	addGame(list, "Pinned.gb", "Pinned");
	addGame(list, "Tetris.gb", "Tetris");
	addGame(list, "Alpha.gb", "Alpha");
	return list;
}

static int isPinned(const MosaicEntry* entry) {
	return strstr(host->entry_path(entry), "/Pinned.gb") != NULL;
}

static int sortCompare(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	if (strcmp(dir_path, folder) != 0) return 0;
	return isPinned(b) - isPinned(a);
}

static void listReady(const MosaicList* list, const char* dir_path) {
	if (strcmp(dir_path, folder) != 0) return;
	int count = host->list_count(list);
	say("list_ready %i entries", count);
	int collides[16] = { 0 };
	for (int i = 0; i < count && i < 16; i++) {
		for (int j = 0; j < count; j++) {
			if (i != j && !strcmp(host->entry_name(host->list_get(list, i)), host->entry_name(host->list_get(list, j)))) collides[i] = 1;
		}
	}
	for (int i = 0; i < count && i < 16; i++) {
		if (!collides[i]) continue;
		MosaicEntry* entry = host->list_get(list, i);
		char name[300];
		snprintf(name, sizeof(name), "%s (%s)", host->entry_name(entry), strrchr(host->entry_path(entry), '.') + 1);
		say("renamed %s %i", name, host->entry_set_name(entry, name));
	}
}

// The current list's names, comma separated.
static const char* order(void) {
	static char text[512];
	text[0] = '\0';
	const MosaicList* list = host->current_list();
	for (int i = 0; list && i < host->list_count(list); i++) {
		if (i) strncat(text, ",", sizeof(text) - strlen(text) - 1);
		strncat(text, host->entry_name(host->list_get(list, i)), sizeof(text) - strlen(text) - 1);
	}
	return text;
}

static const char* selectedName(void) {
	const MosaicEntry* entry = host->selected_entry();
	return entry ? host->entry_name(entry) : "(null)";
}

static void tick(unsigned long now) {
	(void)now;
	if (!host->current_list()) return;
	ticks++;
	if (ticks == 1) {
		MosaicEntry* first = host->list_get(host->current_list(), 0);
		say("set_name outside list_ready %i", host->entry_set_name(first, "Nope"));
		say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
		say("order %s", order());
		say("selected index %i", host->selected_index());
		say("select 3 %i, bad %i %i", host->select_index(3), host->select_index(99), host->select_index(-1));
		say("selected %i %s", host->selected_index(), selectedName());
	}
	else if (ticks == 2) {
		say("next frame selected %i %s", host->selected_index(), selectedName());
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, entry_set_name) || !MOSAIC_HOST_HAS(host, current_list)
			|| !MOSAIC_HOST_HAS(host, selected_index) || !MOSAIC_HOST_HAS(host, select_index)) {
		host->log(ID, "the launcher is missing a host call");
		return 1;
	}
	snprintf(folder, sizeof(folder), "%s/Sorter", host->sdcard_path);
	say("current list from init %s", host->current_list() ? "set" : "(null)");
	say("select from init %i", host->select_index(0));
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.sort_compare = sortCompare,
	.list_ready = listReady,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
