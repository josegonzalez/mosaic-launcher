// Game actions: rename, delete and add to collection for the selected game.
//
// Each is an action on Y (the "button" setting picks another), so with
// favorites and hidden on the same button, Y opens a menu of them all for
// the selected game, and with only one available it runs straight away.
//
// Rename sets the name shown for an entry, through the keyboard; an empty
// name puts the usual one back. It never touches files: the names are in
// .userdata/<platform>/launcher/game-actions-names.txt, one card-relative
// path, a tab and the name per line.
//
// Delete removes a game under Roms/ with its sidecar files (saves, states,
// art, its own emulator settings, and for a multi-disc or cue-sheet game
// its whole folder), after a confirm that names what goes. What it deletes
// is listed first by actions_model.c, which keeps to the game's own files.
// The game also leaves recents, collections (favorites among them) and
// the name overrides. The "saves" setting off keeps saves and states.
//
// Add to collection appends the game to a collection in Collections/, the
// way stock collections store it, or to a new one named on the keyboard.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mosaic_plugin.h"
#include "actions_model.h"

#define ID "game-actions"
// an action label the launcher translates where it draws it, for make pot
#define TR_MARK(text) (text)
#define MAX_COLLECTIONS 63 // choose takes 64 choices, one is "New collection"
#define MAX_CLEANUP 256    // collections cleaned of a deleted game

static const MosaicHost* host;
static char state_dir[GA_PATH_LEN];  // <userdata>/launcher
static char names_path[GA_PATH_LEN + 32];
static char collections_dir[GA_PATH_LEN];
static GANames names;
static struct stat names_seen; // the names file as last read or written
static int with_saves = 1;

static void say(const char* message) {
	host->log(ID, message);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

static void reload(void) {
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
	else host->request_redraw();
}

// The card-relative form of path, or NULL if it isn't on the card.
static const char* relative(const char* path) {
	size_t len = strlen(host->sdcard_path);
	if (!path || strncmp(path, host->sdcard_path, len) != 0 || path[len] != '/') return NULL;
	return path + len;
}

static const char* selectedPath(void) {
	const MosaicEntry* entry = host->selected_entry();
	if (!entry || host->entry_type(entry) == MOSAIC_ENTRY_TOGGLE) return NULL;
	return host->entry_path(entry);
}

///////////////////////////////////////
// name overrides

static void rememberNames(void) {
	if (stat(names_path, &names_seen) != 0) memset(&names_seen, 0, sizeof(names_seen));
}

// Reads the names again if the file changed since we last saw it.
static void refreshNames(void) {
	struct stat st;
	if (stat(names_path, &st) != 0) memset(&st, 0, sizeof(st));
	if (st.st_mtime == names_seen.st_mtime && st.st_size == names_seen.st_size && st.st_ino == names_seen.st_ino) return;
	if (GA_namesLoad(&names, names_path) != 0) say("couldn't read the names file");
	names_seen = st;
}

static int saveNames(void) {
	mkdir(state_dir, 0755);
	if (GA_namesSave(&names, names_path) != 0) {
		say("couldn't save the names file");
		return -1;
	}
	rememberNames();
	return 0;
}

static int displayName(const MosaicEntry* entry, char* out, size_t size) {
	if (!names.count) return 0;
	const char* name = GA_namesGet(&names, relative(host->entry_path(entry)));
	if (!name) return 0;
	snprintf(out, size, "%s", name);
	return 1;
}

///////////////////////////////////////
// rename

static char renaming[GA_PATH_LEN]; // card-relative, waiting on the keyboard

static int canRename(void* data) {
	(void)data;
	return relative(selectedPath()) != NULL;
}

static void renamed(const char* text, void* data) {
	(void)data;
	if (!text || !renaming[0]) return; // cancelled
	refreshNames();
	int changed = GA_namesSet(&names, renaming, text);
	if (changed < 0) {
		toast(tr("Couldn't rename that"));
		return;
	}
	if (changed && saveNames() != 0) {
		GA_namesLoad(&names, names_path);
		toast(tr("Couldn't save the name"));
		return;
	}
	char message[GA_PATH_LEN + GA_NAME_LEN + 16];
	const char* name = GA_namesGet(&names, renaming);
	snprintf(message, sizeof(message), "named %s %s", renaming, name ? name : "(its own name)");
	say(message);
	toast(name ? tr("Renamed") : tr("Name restored"));
	renaming[0] = '\0';
	reload();
}

static void rename_(void* data) {
	(void)data;
	const char* rel = relative(selectedPath());
	if (!rel) return;
	refreshNames();
	snprintf(renaming, sizeof(renaming), "%s", rel);
	const char* current = GA_namesGet(&names, rel);
	if (!current) current = host->entry_name(host->selected_entry());
	host->keyboard(tr("Rename"), current ? current : "", renamed, NULL);
}

///////////////////////////////////////
// delete

static GAPlan plan;                  // what the confirm dialog is asking about
static char checked[GA_PATH_LEN];    // the last path canDelete looked at
static int checked_ok;

static GACard card(void) {
	GACard c = { host->sdcard_path, host->userdata_path, with_saves };
	return c;
}

static int canDelete(void* data) {
	(void)data;
	const char* path = selectedPath();
	if (!path) return 0;
	// asked every time the hints draw, so only look on the card when the
	// selection moves
	if (strcmp(path, checked) != 0) {
		char game[GA_PATH_LEN];
		int is_dir;
		snprintf(checked, sizeof(checked), "%s", path);
		checked_ok = GA_gameFor(host->sdcard_path, path, game, sizeof(game), &is_dir);
	}
	return checked_ok;
}

// The game leaves recents, collections and the name overrides.
static void forget(const char* target, int is_dir) {
	const char* rel = relative(target);
	if (!rel) return;
	char rel_copy[GA_PATH_LEN];
	snprintf(rel_copy, sizeof(rel_copy), "%s", rel);

	if (MOSAIC_HOST_HAS(host, recent_remove)) {
		size_t len = strlen(target);
		for (int i = host->recents_count() - 1; i >= 0; i--) {
			const char* recent = host->recent_path(i);
			if (!recent) continue;
			int match = !strcmp(recent, target) || (is_dir && !strncmp(recent, target, len) && recent[len] == '/');
			if (match && host->recent_remove(i) != 0) say("couldn't remove a recent");
		}
	}

	char* collections[MAX_CLEANUP];
	int count = GA_collections(host->sdcard_path, collections, MAX_CLEANUP);
	for (int i = 0; i < count; i++) {
		char file[GA_PATH_LEN * 2];
		snprintf(file, sizeof(file), "%s/%s.txt", collections_dir, collections[i]);
		int removed = GA_collectionRemove(file, rel_copy, is_dir);
		if (removed > 0) {
			char message[GA_PATH_LEN + 64];
			snprintf(message, sizeof(message), "removed it from the %s collection", collections[i]);
			say(message);
		}
		else if (removed < 0) say("couldn't update a collection");
		free(collections[i]);
	}

	refreshNames();
	if (GA_namesRemove(&names, rel_copy, is_dir) > 0) saveNames();
}

static void deleteConfirmed(void* data) {
	(void)data;
	if (!plan.count) return;
	GACard c = card();
	for (int i = 0; i < plan.count; i++) {
		char message[GA_PATH_LEN + 16];
		snprintf(message, sizeof(message), "deleting %s", plan.items[i].path);
		say(message);
	}
	int failed = 0;
	int removed = GA_runDelete(&c, &plan, &failed);
	char message[64];
	snprintf(message, sizeof(message), "deleted %i of %i paths", removed, plan.count);
	say(message);
	forget(plan.items[0].path, plan.items[0].is_dir);
	toast(failed ? tr("Couldn't delete everything") : tr("Deleted"));
	GA_planFree(&plan);
	checked[0] = '\0';
	reload();
}

static void delete_(void* data) {
	(void)data;
	const char* path = selectedPath();
	if (!path) return;
	GACard c = card();
	if (GA_planDelete(&c, path, &plan) != 0) {
		toast(tr("Can't delete that"));
		return;
	}
	// name the game as the list shows it, cut to fit the dialog
	const char* shown = host->entry_name(host->selected_entry());
	if (!shown || !shown[0]) shown = plan.name;
	char summary[160];
	GA_planSummary(&plan, summary, sizeof(summary));
	char also[512];
	if (plan.items[0].is_dir) snprintf(also, sizeof(also), tr("Also deletes its folder%s%s.\n"), summary[0] ? ", " : "", summary);
	else if (summary[0]) snprintf(also, sizeof(also), tr("Also deletes %s.\n"), summary);
	else also[0] = '\0';
	char question[1024]; // the dialog keeps 255 characters per line
	snprintf(question, sizeof(question), tr("Delete %.40s%s?\n%sThis can't be undone."), shown, strlen(shown) > 40 ? "..." : "", also);
	host->confirm(question, tr("DELETE"), deleteConfirmed, NULL);
}

///////////////////////////////////////
// add to collection

static char collecting[GA_PATH_LEN]; // card-relative, waiting on a choice
static char* choices[MAX_COLLECTIONS + 1];
static int choice_count;

static void freeChoices(void) {
	for (int i = 0; i < choice_count; i++) free(choices[i]);
	choice_count = 0;
}

static int canCollect(void* data) {
	(void)data;
	const char* path = selectedPath();
	char rel[GA_PATH_LEN];
	return path && GA_collectionPath(host->sdcard_path, path, rel, sizeof(rel));
}

static void addTo(const char* name) {
	char file[GA_PATH_LEN * 2];
	snprintf(file, sizeof(file), "%s/%s.txt", collections_dir, name);
	mkdir(collections_dir, 0755);
	int added = GA_collectionAdd(file, collecting);
	char shown[GA_NAME_LEN + 256];
	if (added < 0) snprintf(shown, sizeof(shown), tr("Couldn't add to %s"), name);
	else if (added == 0) snprintf(shown, sizeof(shown), tr("Already in %s"), name);
	else {
		snprintf(shown, sizeof(shown), tr("Added to %s"), name);
		char message[GA_PATH_LEN * 2 + 16];
		snprintf(message, sizeof(message), "added %.1000s to %.1000s", collecting, file);
		say(message);
	}
	toast(shown);
	collecting[0] = '\0';
	if (added > 0) reload();
}

static void named(const char* text, void* data) {
	(void)data;
	if (!text || !collecting[0]) return;
	if (!GA_validCollectionName(text)) {
		toast(tr("That name can't be a collection"));
		return;
	}
	addTo(text);
}

static void picked(int index, void* data) {
	(void)data;
	if (index < 0 || index > choice_count || !collecting[0]) {
		freeChoices();
		return;
	}
	if (index == choice_count) {
		freeChoices();
		host->keyboard(tr("New collection"), "", named, NULL);
		return;
	}
	char name[GA_NAME_LEN];
	snprintf(name, sizeof(name), "%s", choices[index]);
	freeChoices();
	addTo(name);
}

static void collect(void* data) {
	(void)data;
	const char* path = selectedPath();
	if (!path || !GA_collectionPath(host->sdcard_path, path, collecting, sizeof(collecting))) return;
	freeChoices();
	choice_count = GA_collections(host->sdcard_path, choices, MAX_COLLECTIONS);
	const char* list[MAX_COLLECTIONS + 1];
	for (int i = 0; i < choice_count; i++) list[i] = choices[i];
	list[choice_count] = tr("New collection");
	if (host->choose(tr("Add to collection"), list, choice_count + 1, 0, picked, NULL) != 0) {
		freeChoices();
		toast(tr("Couldn't list collections"));
	}
}

///////////////////////////////////////
// hooks

static void postReturn(void) {
	refreshNames();
	checked[0] = '\0';
}

static void quit(void) {
	GA_namesFree(&names);
	GA_planFree(&plan);
	freeChoices();
}

static int settingOn(const char* key) {
	return strcmp(host->cfg_get(ID, key, "on"), "off") != 0;
}

static const struct {
	const char* name;
	int button;
} buttons[] = {
	{ "Y", MOSAIC_BUTTON_Y },
	{ "L2", MOSAIC_BUTTON_L2 },
	{ "R2", MOSAIC_BUTTON_R2 },
};

static void add(const char* id, const char* label, int (*available)(void*), void (*run)(void*), int button) {
	if (host->register_action(id, label, available, run, NULL) != 0
			|| host->bind_button(MOSAIC_SCREEN_GAMELIST, button, MOSAIC_TRIGGER_PRESS, id) != 0) {
		char message[128];
		snprintf(message, sizeof(message), "couldn't bind %s", id);
		say(message);
	}
}

static int init(const MosaicHost* h) {
	host = h;
	snprintf(state_dir, sizeof(state_dir), "%s/launcher", host->userdata_path);
	snprintf(names_path, sizeof(names_path), "%s/game-actions-names.txt", state_dir);
	snprintf(collections_dir, sizeof(collections_dir), "%s/Collections", host->sdcard_path);
	GA_namesInit(&names);
	GA_planInit(&plan);
	memset(&names_seen, 0, sizeof(names_seen));
	names_seen.st_size = -1; // so the first refresh reads the file
	refreshNames();
	checked[0] = '\0';
	with_saves = settingOn("saves");

	const char* name = host->cfg_get(ID, "button", "Y");
	int button = MOSAIC_BUTTON_Y;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (!strcmp(name, buttons[i].name)) button = buttons[i].button;
	}

	if (settingOn("rename")) {
		if (MOSAIC_HOST_HAS(host, keyboard)) add("game-actions.rename", TR_MARK("RENAME"), canRename, rename_, button);
		else say("this launcher has no keyboard; rename is off");
	}
	if (settingOn("collections")) {
		if (MOSAIC_HOST_HAS(host, choose) && MOSAIC_HOST_HAS(host, keyboard)) add("game-actions.collect", TR_MARK("ADD TO COLLECTION"), canCollect, collect, button);
		else say("this launcher can't show a choice list; add to collection is off");
	}
	if (settingOn("delete")) {
		if (MOSAIC_HOST_HAS(host, confirm) && MOSAIC_HOST_HAS(host, reload_lists)) add("game-actions.delete", TR_MARK("DELETE"), canDelete, delete_, button);
		else say("this launcher has no confirm dialog; delete is off");
	}
	return 0;
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.display_name = displayName,
	.post_return = postReturn,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
