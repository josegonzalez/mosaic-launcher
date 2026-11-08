// A plugin that hears about the selection and moves it from tick, logging
// each selection_changed. It adds a <sd>/Selector folder of seven games to
// the root. Once the starting selection is in, it opens Selector, then
// scrolls through it a row a frame, reloads the lists, and goes back to the
// root, logging how many events each step brought. Each event tries
// select_index, which is refused.
//
// With fail=1 in its config, selection_changed returns an error on its
// second event, and the plugin then only scrolls and counts.
//
// With jump=1, it goes from Selector's last game to <sd>/Selector/Tiny, a
// subfolder of one game, then back to Selector's last game and over to
// <sd>/Tiny, another folder of one game, logging where each open left the
// selection.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic_plugin.h"

#define ID "selector"
#define GAMES 7

static const MosaicHost* host;
static char folder[512];
static char sub[600];  // <sd>/Selector/Tiny
static char tiny[512]; // <sd>/Tiny
static int fail = 0;
static int jump = 0;
static int events = 0;
static int step = 0;
static int moves = 0;
static unsigned long frame = 0; // this frame's tick
static unsigned long since = 0; // when the step began, or the last move

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
	host->list_push(root, host->entry_new(folder, MOSAIC_ENTRY_DIR, "Selector"));
}

static MosaicList* virtualDir(const char* path) {
	if (!strcmp(path, sub) || !strcmp(path, tiny)) {
		MosaicList* list = host->list_new();
		char file[700];
		snprintf(file, sizeof(file), "%s/Only.gb", path);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, "Only"));
		return list;
	}
	if (strcmp(path, folder) != 0) return NULL;
	static const char* names[GAMES] = { "Apple", "Banana", "Cherry", "Date", "Elder", "Fig", "Grape" };
	MosaicList* list = host->list_new();
	for (int i = 0; i < GAMES; i++) {
		char file[600];
		snprintf(file, sizeof(file), "%s/%s.gb", folder, names[i]);
		host->list_push(list, host->entry_new(file, MOSAIC_ENTRY_ROM, names[i]));
	}
	return list;
}

static const char* where(void) {
	const char* path = host->current_path();
	if (path && !strcmp(path, folder)) return "Selector";
	if (path && !strcmp(path, sub)) return "Selector/Tiny";
	if (path && !strcmp(path, tiny)) return "Tiny";
	if (path && !strcmp(path, host->sdcard_path)) return "root";
	return path ? path : "(null)";
}

static int selectionChanged(const MosaicEntry* entry) {
	events++;
	// it comes once the selection has been still for the settle time
	int settled = frame - since >= (unsigned long)host->selection_settle_ms;
	say("event %i %s in %s, index %i, settled %i, select %i", events, entry ? host->entry_name(entry) : "NULL",
		where(), host->selected_index(), settled, host->select_index(0));
	if (fail && events == 2) {
		say("event failing");
		return -1;
	}
	return 0;
}

static void tick(unsigned long now) {
	frame = now;
	if (!host->current_list()) return;
	switch (step) {
	case 0: // the starting selection
		if (events == 1) {
			say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
			since = now;
			step = 1;
		}
		break;
	case 1: // the folder's first game
		if (events == 2) step = jump ? 10 : 2;
		break;
	case 2: // a row a frame, faster than it settles
		moves++;
		host->select_index(moves);
		since = now;
		if (moves == GAMES - 1) step = 3;
		break;
	case 3:
		if (events == 3 || now - since > 1500) {
			say("after scrolling %i events", events);
			if (fail) {
				say("done");
				step = 99;
				break;
			}
			host->reload_lists();
			since = now;
			step = 4;
		}
		break;
	case 4: // a reload keeping the same game is nothing
		if (now - since > 1000) {
			say("after reload %i events", events);
			say("open root %i", host->open_path(host->sdcard_path, MOSAIC_ENTRY_DIR));
			since = now;
			step = 5;
		}
		break;
	case 5:
		if (events == 4 || now - since > 1500) {
			say("done");
			step = 99;
		}
		break;
	// jump=1: each open from the last game, then where it left the selection
	// on the next frame, after the game list's input has run
	case 10:
		host->select_index(GAMES - 1);
		say("open sub %i", host->open_path(sub, MOSAIC_ENTRY_DIR));
		step = 11;
		break;
	case 11:
		say("in %s, index %i of %i", where(), host->selected_index(), host->list_count(host->current_list()));
		say("open folder %i", host->open_path(folder, MOSAIC_ENTRY_DIR));
		step = 12;
		break;
	case 12:
		host->select_index(GAMES - 1);
		say("open tiny %i", host->open_path(tiny, MOSAIC_ENTRY_DIR));
		step = 13;
		break;
	case 13:
		say("in %s, index %i of %i", where(), host->selected_index(), host->list_count(host->current_list()));
		say("done");
		step = 99;
		break;
	}
}

static int init(const MosaicHost* h) {
	host = h;
	if (!MOSAIC_HOST_HAS(host, selection_settle_ms)) return 1;
	snprintf(folder, sizeof(folder), "%s/Selector", host->sdcard_path);
	snprintf(sub, sizeof(sub), "%s/Tiny", folder);
	snprintf(tiny, sizeof(tiny), "%s/Tiny", host->sdcard_path);
	fail = atoi(host->cfg_get(ID, "fail", "0"));
	jump = atoi(host->cfg_get(ID, "jump", "0"));
	say("settle %i", host->selection_settle_ms);
	return 0;
}

static MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.tick = tick,
	.selection_changed = selectionChanged,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
