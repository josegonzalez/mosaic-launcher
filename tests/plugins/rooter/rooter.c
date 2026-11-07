// A plugin that replaces the root and drives the open, recents and reload
// host calls once the launcher is up, logging what each returned.
//
// The root holds one virtual folder, <sd>/Rooter, listing the game named by
// the "rom" setting. pre_launch cancels every launch, so opening the game
// goes through the launcher's launch path without leaving it.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "nextui_plugin.h"

#define ID "rooter"

static const NUIHost* host;
static char folder[512];
static int replaced = 0; // times root_replace ran
static int listed = 0;   // times the folder was listed
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

static const char* shown(const char* s) {
	return s ? s : "(null)";
}

static NUIList* rootReplace(void) {
	replaced++;
	host->log(ID, "root_replace called");
	NUIList* root = host->list_new();
	host->list_push(root, host->entry_new(folder, NUI_ENTRY_DIR, "Rooter"));
	return root;
}

static NUIList* virtualDir(const char* path) {
	if (strcmp(path, folder) != 0) return NULL;
	listed++;
	NUIList* list = host->list_new();
	host->list_push(list, host->entry_new(host->cfg_get(ID, "rom", ""), NUI_ENTRY_ROM, "Game"));
	return list;
}

static int preLaunch(const NUILaunch* launch) {
	say("pre_launch %s", launch->path);
	return NUI_LAUNCH_CANCEL;
}

static const char* selectedPath(void) {
	const NUIEntry* entry = host->selected_entry();
	return entry ? host->entry_path(entry) : NULL;
}

static void tick(unsigned long now) {
	(void)now;
	if (!replaced) return;
	ticks++;
	if (ticks == 1) {
		say("selected %s", shown(selectedPath()));
		say("recents %i, first %s", host->recents_count(), shown(host->recent_path(0)));
		say("past the end %s", shown(host->recent_path(99)));

		const char* rom = host->cfg_get(ID, "rom", "");
		say("open rom %i", host->open_path(rom, NUI_ENTRY_ROM));
		say("open missing %i", host->open_path("/nowhere/Missing.gb", NUI_ENTRY_ROM));
		int opened = host->open_path(folder, NUI_ENTRY_DIR);
		say("open folder %i, now in %s", opened, shown(host->current_path()));

		int removed = host->recent_remove(0);
		int bad = host->recent_remove(5);
		say("removed %i, recents %i, bad index %i", removed, host->recents_count(), bad);
		host->recents_clear();
		say("cleared, recents %i", host->recents_count());

		host->reload_lists();
		say("reload asked, listed %i", listed);
	}
	else if (ticks == 2) {
		say("reloaded, in %s, listed %i", shown(host->current_path()), listed);
		say("selected after reload %s", shown(selectedPath()));
	}
}

static int init(const NUIHost* h) {
	host = h;
	if (!NUI_HOST_HAS(host, open_path) || !NUI_HOST_HAS(host, recents_count) || !NUI_HOST_HAS(host, recent_path)
			|| !NUI_HOST_HAS(host, recent_remove) || !NUI_HOST_HAS(host, recents_clear) || !NUI_HOST_HAS(host, reload_lists)) {
		host->log(ID, "the launcher is missing a host call");
		return 1;
	}
	snprintf(folder, sizeof(folder), "%s/Rooter", host->sdcard_path);
	say("open from init %i", host->open_path(folder, NUI_ENTRY_DIR));
	return 0;
}

static const NUIPlugin plugin = {
	.size = sizeof(NUIPlugin),
	.api_version = NUI_API_VERSION,
	.id = ID,
	.init = init,
	.virtual_dir = virtualDir,
	.pre_launch = preLaunch,
	.tick = tick,
	.root_replace = rootReplace,
};

const NUIPlugin* nextui_plugin_get(const NUIHost* h) {
	(void)h;
	return &plugin;
}
