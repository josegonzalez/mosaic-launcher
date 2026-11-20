// Simple mode: a launcher for kids or a shared device. While it's on, the
// Tools folder is gone from every list (the root, Recently Played and
// collections), Tools paks don't launch from the game switcher or an FN
// button, and the quick menu loses its Tools and Pak Store tiles, plus
// Settings, Wifi, Bluetooth and Reboot with the "settings" setting (on by
// default).
//
// A Simple mode tile on the quick menu turns it on, and off again: with
// the launcher's PIN set (on the Launcher pak's Plugins page), only after
// typing it, and otherwise after a confirm. The host keeps the PIN and
// checks it; this plugin never sees it. On a launcher without ask_pin, the
// old "pin" key in this plugin's cfg is used instead. Turning the plugin
// or its "on" setting off in the Launcher pak, or editing simple-mode.cfg
// on the card, leaves it too.
//
// Two more parts only apply while it's on. With the "kid" setting naming a
// tag, every list shows only the games with that tag (tags' tags:<name>
// set): systems and folders without one are gone, and so are Recently
// Played and collections without one, and other games don't launch from
// the game switcher either. With the "kiosk" setting on, actions
// registered with MOSAIC_ACTION_EDITS, such as tags, favorites, hide,
// rename and delete, ask for the launcher's PIN first (action_gate), and
// with no PIN set they're refused.

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "simple_model.h"
#include "mosaic_plugin.h"

#define ID "simple-mode"
#define TOGGLE_ID "simple-mode.toggle"

static const MosaicHost* host;

// the kid tag's games, read again when its set changes
static KidList kid;
static char kid_tag[80] = "";
static int kid_dirty = 1;

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static int isOn(void* data) {
	(void)data;
	return Simple_isOn(host->cfg_get("on", "on"));
}

static int always(const char* path, void* data) {
	(void)path;
	(void)data;
	return 1;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

// Hides the tiles for the "settings" setting, or shows every tile simple
// mode might have hidden.
static void hideTiles(int hide) {
	if (!MOSAIC_HOST_HAS(host, hide_quick_tile)) return;
	const char* names[SIMPLE_TILES_MAX];
	int hide_settings = strcmp(host->cfg_get("settings", "on"), "off") != 0;
	int count = Simple_tiles(hide ? hide_settings : 1, names);
	for (int i = 0; i < count; i++) host->hide_quick_tile(names[i], hide);
}

static void setOn(int on) {
	host->cfg_set("on", on ? "on" : "off");
	hideTiles(on);
	toast(on ? tr("Simple mode on") : tr("Simple mode off"));
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	host->request_redraw();
}

static void leave(void* data) {
	(void)data;
	setOn(0);
}

static void checkedPin(int result, void* data) {
	(void)data;
	if (result == MOSAIC_PIN_RIGHT) setOn(0);
	else if (result == MOSAIC_PIN_WRONG) toast(tr("Wrong PIN"));
}

static void typedPin(const char* text, void* data) {
	(void)data;
	if (!text) return; // cancelled
	if (Simple_pinMatches(host->cfg_get("pin", ""), text)) setOn(0);
	else toast(tr("Wrong PIN"));
}

static void toggle(const char* path, void* data) {
	(void)path;
	if (!isOn(data)) {
		setOn(1);
		return;
	}
	if (MOSAIC_HOST_HAS(host, ask_pin)) {
		if (host->has_pin()) host->ask_pin(tr("PIN to leave simple mode"), checkedPin, NULL);
		else if (MOSAIC_HOST_HAS(host, confirm)) host->confirm(tr("Leave simple mode?"), tr("LEAVE"), leave, NULL);
		else setOn(0);
	}
	else if (Simple_leaveStep(host->cfg_get("pin", "")) == SIMPLE_LEAVE_PIN) {
		if (MOSAIC_HOST_HAS(host, keyboard)) host->keyboard(tr("PIN to leave simple mode"), "", typedPin, NULL);
		else host->log(ID, "this launcher has no keyboard to type the PIN on; turn simple mode off from the Launcher pak");
	}
	else if (MOSAIC_HOST_HAS(host, confirm)) host->confirm(tr("Leave simple mode?"), tr("LEAVE"), leave, NULL);
	else setOn(0);
}

///////////////////////////////////////
// the kid tag

static void addKid(const char* path, void* data) {
	(void)data;
	if (Kid_add(&kid, path) != 0) host->log(ID, "out of memory reading the kid tag's games");
}

// A tag named the same but for case, as tags matches names.
static char kid_found[sizeof(kid_tag) + 8];
static void findSet(const char* name, void* data) {
	(void)data;
	if (!kid_found[0] && !strncmp(name, "tags:", 5) && !strcasecmp(name + 5, kid_tag)) snprintf(kid_found, sizeof(kid_found), "%s", name);
}

// The kid tag's name, and its games read again when they may have changed.
// 0 while no kid tag is set.
static int kidOn(void) {
	char tag[sizeof(kid_tag)];
	if (!Kid_tag(host->cfg_get("kid", ""), tag, sizeof(tag))) return 0;
	if (strcmp(tag, kid_tag) != 0) {
		snprintf(kid_tag, sizeof(kid_tag), "%s", tag);
		kid_dirty = 1;
	}
	if (!kid_dirty) return 1;
	kid_dirty = 0;
	Kid_clear(&kid);
	if (!MOSAIC_HOST_HAS(host, is_filtered)) return 1;
	char set[sizeof(kid_found)];
	snprintf(set, sizeof(set), "tags:%s", kid_tag);
	if (host->set_list(set, addKid, NULL) >= 0) return 1;
	kid_found[0] = '\0';
	if (MOSAIC_HOST_HAS(host, set_names)) host->set_names(findSet, NULL);
	if (!kid_found[0] || host->set_list(kid_found, addKid, NULL) < 0) {
		char message[sizeof(set) + 64];
		snprintf(message, sizeof(message), "no %s set, so the kid lists have no games; is tags on?", set);
		host->log(ID, message);
	}
	return 1;
}

static void setChanged(const char* name, const char* payload, void* data) {
	(void)name;
	(void)data;
	char set[sizeof(kid_found)];
	if (host->event_value(payload, "name", set, sizeof(set)) >= 0 && !strncmp(set, "tags:", 5)) kid_dirty = 1;
}

static int recentAllowed(void) {
	if (!MOSAIC_HOST_HAS(host, recents_count)) return 1;
	int count = host->recents_count();
	for (int i = 0; i < count; i++) {
		if (Kid_allows(&kid, host->recent_path(i))) return 1;
	}
	return 0;
}

#define COLLECTION_MAX (256 * 1024)

static int collectionAllowed(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	char* text = malloc(COLLECTION_MAX + 1);
	size_t len = text ? fread(text, 1, COLLECTION_MAX, f) : 0;
	fclose(f);
	if (!text) return 0;
	text[len] = '\0';
	int allowed = Kid_collectionAllows(&kid, text, host->sdcard_path);
	free(text);
	return allowed;
}

static int collectionsAllowed(const char* path) {
	DIR* d = opendir(path);
	if (!d) return 0;
	int allowed = 0;
	struct dirent* de;
	while (!allowed && (de = readdir(d))) {
		size_t len = strlen(de->d_name);
		if (de->d_name[0] == '.' || len < 5 || strcmp(de->d_name + len - 4, ".txt") != 0) continue;
		char full[1024];
		if (snprintf(full, sizeof(full), "%s/%s", path, de->d_name) < (int)sizeof(full)) allowed = collectionAllowed(full);
	}
	closedir(d);
	return allowed;
}

static int kidShows(const char* path, int type) {
	switch (Kid_place(path, type, host->sdcard_path)) {
		case KID_GAME: return Kid_allows(&kid, path);
		case KID_RECENTS: return recentAllowed();
		case KID_COLLECTIONS: return collectionsAllowed(path);
		case KID_COLLECTION: return collectionAllowed(path);
		default: return 1;
	}
}

static int filter(const MosaicEntry* entry, const char* dir_path) {
	(void)dir_path;
	if (!isOn(NULL)) return 1;
	const char* path = host->entry_path(entry);
	if (Simple_isTools(path, host->sdcard_path, host->platform)) return 0;
	return !kidOn() || kidShows(path, host->entry_type(entry));
}

// 1 for a path in <sdcard>/Roms.
static int inRoms(const char* path) {
	char roms[512];
	int len = snprintf(roms, sizeof(roms), "%s/Roms/", host->sdcard_path);
	return len > 0 && len < (int)sizeof(roms) && !strncmp(path, roms, (size_t)len);
}

static int preLaunch(const MosaicLaunch* launch) {
	if (!isOn(NULL)) return MOSAIC_LAUNCH_CONTINUE;
	if (Simple_isTools(launch->path, host->sdcard_path, host->platform)) {
		toast(tr("Tools are off in simple mode"));
		return MOSAIC_LAUNCH_CANCEL;
	}
	if (kidOn() && inRoms(launch->path) && !Kid_allows(&kid, launch->path)) {
		toast(tr("That game is off in simple mode"));
		return MOSAIC_LAUNCH_CANCEL;
	}
	return MOSAIC_LAUNCH_CONTINUE;
}

static void postReturn(void) {
	kid_dirty = 1;
}

///////////////////////////////////////
// kiosk

static int actionGate(const char* action_id, int flags) {
	(void)action_id;
	int kiosk = strcmp(host->cfg_get("kiosk", "off"), "on") == 0;
	int has_pin = MOSAIC_HOST_HAS(host, ask_pin) && host->has_pin();
	int answer = Kiosk_gate(isOn(NULL), kiosk, flags, has_pin);
	if (answer == MOSAIC_GATE_REFUSE) toast(tr("Set a PIN to make changes in kiosk mode"));
	return answer;
}

static void quit(void) {
	Kid_free(&kid);
	kid_tag[0] = '\0';
	kid_dirty = 1;
}

static int init(const MosaicHost* h) {
	host = h;
	if (host->register_action(TOGGLE_ID, "Simple mode", 0, always, toggle, NULL) != 0
			|| !MOSAIC_HOST_HAS(host, quick_tile)
			|| host->quick_tile(TOGGLE_ID, MOSAIC_QUICK_BOTTOM, "quick/simple-mode.png", "quick/simple-mode-off.png", isOn) != 0) {
		host->log(ID, "couldn't add the Simple mode tile; turn simple mode off from the Launcher pak");
	}
	if (isOn(NULL)) hideTiles(1);
	kid_dirty = 1;
	if (MOSAIC_HOST_HAS(host, set_names) && host->subscribe("launcher.set_changed", setChanged, NULL) != 0) {
		host->log(ID, "couldn't follow tag changes; the kid lists change on the next start");
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.filter = filter,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.action_gate = actionGate,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
