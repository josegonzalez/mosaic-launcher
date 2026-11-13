#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include "launcher.h"
#include "pluginset.h"
#include "plugins.h"
#include "theme.h"
#include "fallback.h"
#include "stocktoggles.h"
#include "leds.h"
#include "bluetooth.h"
#include "wifi.h"
#include "plugindraw.h"
#include "drawmodel.h"
#include "layoutmodel.h"
#include "selmodel.h"
#include "infomodel.h"
#include "transmodel.h"
#include "playhistory.h"
#include "raprogress.h"
#include "inputmodel.h"
#include "pin.h"
#include "audiomodel.h"
#include "screenmodel.h"
#include "caller.h"
#include "plugincfg.h"
#include "fnbinds.h"
#include "capture.h"
#include "hintmodel.h"
#include "palettes.h"
#include "entrysets.h"
#include "events.h"
#include "callsite.h"
#include "providers.h"
#include "actionlist.h"

#ifndef PLUGIN_ARCH
#error "build with -DPLUGIN_ARCH=\"...\" (eg. aarch64)"
#endif

#define LAUNCHED_MARKER "/tmp/mosaic-launcher-launched" // a launch went ahead; post_return is due
#define LOADING_MARKER PLUGIN_STATE_DIR "/loading"
#define PAUSED_MARKER PLUGIN_STATE_DIR "/plugins_paused"
#define SETTINGS_MARKER PLUGIN_STATE_DIR "/settings_loading" // the Launcher pak's settings are open with plugins loaded
#define HOME_LEFT_PATH "/tmp/mosaic-launcher-home" // where the last launch left from: home or list

typedef struct LoadedPlugin {
	void* handle;
	const MosaicPlugin* plugin;
	const PluginManifest* manifest; // its plugin.txt, for plugin_info
	char id[64];
	PluginCfg cfg; // its own settings, read on first use
	int owns_root; // claims the root slot, so its root_replace is asked
	int owns_layout; // claims the layout slot, so its layout_* hooks are called
	int owns_transition; // claims the transition slot, so it's asked about pushes, pops and screen changes
	int owns_launch;     // claims the launch slot, so it's asked about launches
	int capped;          // asked for a transition past the cap, which was logged
	int rows_off;    // its decorate_row failed, so it's no longer called
	int header_off;  // its header_width or header_widget failed, so its widget is left out
	int selection_off; // its selection_changed failed, so it's no longer called
	int owns_background; // claims the background slot, so its background_render is called
	int art_off;         // its art_surface returned a surface it didn't make, so it's no longer asked
	int art_deco_off;    // its decorate_art failed, so it's no longer called
	int filter_off;      // its input_filter failed, so it's no longer called
	int overlay_off;     // its overlay_render failed, so its overlay is left out
	int owns_home;       // claims the home slot, so its MOSAIC_SCREEN_HOME screen is in place of the root list
	int owns_switcher;   // claims the switcher slot, so its MOSAIC_SCREEN_GAME_SWITCHER screen is in place of the game switcher
	int owns_strings;    // claims the strings slot, so its translate is asked
	int owns_theme;      // claims the theme slot, so its theme is asked
} LoadedPlugin;

static LoadedPlugin loaded[PLUGIN_MAX];
static int loaded_count = 0;
static int initializing = -1; // the loaded plugin whose init is running
static int launching = -1;    // the loaded plugin whose pre_launch is running
static LayoutSlot layout = { LAYOUT_NONE, -1 };
static int layout_rendering = 0; // while layout_render runs, which only draws
static MosaicSurface* layout_lent = NULL; // the screen layout_render was lent
static MosaicRect layout_area;            // and where it may draw
static ListSelection layout_selected;     // and the last cell decorate_cell got for the selected entry
static int decorating = 0; // while a decorate_row, decorate_art, header_width or header_widget runs, which only draw
static int selecting = 0;  // while a selection_changed runs, which can't move the selection
static TransitionSlots transitions = { -1, -1, 0, 0 };
static int transition_owner = -1; // the plugin whose transition is running
static MosaicTransition transition; // and the transition
static BackgroundSlot background = { -1, 0, 0, 0 };
static int backgrounding = 0; // while background_render runs, which only draws
static int generating = 0;    // while an art_surface runs, which only draws
static ArtSwap swap = { -1, NULL, 0, 0 }; // a plugin's art_replace surface
static SelKey swap_key;                     // and the selection it was shown for
static int swap_drawn = 0;                  // the layout's last layout_render got it with art_replacement
static ScreenSet screens;      // plugins' screens, and the stack of those open
static int screen_hook = 0;    // while a screen's enter or exit runs, which can't open or close screens
static int screen_drawing = 0; // while a screen's render or an overlay runs, which only draw
static MosaicSurface* hints_screen = NULL;     // the screen lent to a screen's render, the only one draw_hints draws on
static const MosaicScreenView* hints_view = NULL; // and its view
static int switcher_declined = 0; // the switcher screen's enter declined, so the built-in switcher shows until it closes

#define SCREEN_COUNT 3

// Runs the statement after it as loaded plugin i, so host calls such as
// cfg_get act on its settings. Every call into a plugin goes through it.
#define AS_PLUGIN(i) AS_CALLER(loaded[i].id)

static void dropOverrides(int owner);
static void saveBindings(int* counts);
static void dropBindings(const int* counts);
static PluginManifest manifests[PLUGIN_MAX];
static int manifest_count = 0;
static int listed = 0; // every plugin has loaded, so plugin_info lists them

// The Launcher pak's settings, with only the strings and theme slots'
// owners loaded (Plugins_initSettings).
static int settings_only = 0;

///////////////////////////////////////
// files

static char* readText(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len < 0 || len > 64 * 1024) {
		fclose(f);
		return NULL;
	}
	char* text = malloc(len + 1);
	size_t got = fread(text, 1, len, f);
	text[got] = '\0';
	fclose(f);
	return text;
}

// mkdir -p, since a fresh card may not have .userdata/<platform> yet.
void Plugins_ensureStateDir(void) {
	char path[512];
	snprintf(path, sizeof(path), "%s", PLUGIN_STATE_DIR);
	for (char* p = path + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(path, 0755);
		*p = '/';
	}
	mkdir(path, 0755);
}

///////////////////////////////////////
// discovery

static void addPluginDir(const char* dir, int bundled) {
	if (manifest_count >= PLUGIN_MAX) return;
	char path[512];
	snprintf(path, sizeof(path), "%s/plugin.txt", dir);
	char* text = readText(path);
	if (!text) return;

	PluginManifest* m = &manifests[manifest_count];
	snprintf(m->dir, sizeof(m->dir), "%s", dir);
	m->bundled = bundled;
	if (Manifest_parse(text, m) == 0) manifest_count++;
	else LOG_warn("plugins: %s has no id, skipping\n", path);
	free(text);
}

// Every directory in parent ending in suffix that holds a plugin.txt.
static void scan(const char* parent, const char* suffix, int bundled) {
	DIR* dh = opendir(parent);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL) {
		// hide() skips dot-files, including the ._* files macOS leaves on cards
		if (hide(dp->d_name) || !suffixMatch((char*)suffix, dp->d_name)) continue;
		char dir[512];
		snprintf(dir, sizeof(dir), "%s/%s", parent, dp->d_name);
		addPluginDir(dir, bundled);
	}
	closedir(dh);
}

int Plugins_pakDir(char* out, size_t size) {
	const char* override = getenv("MOSAIC_LAUNCHER_PAK_DIR");
	if (override && override[0]) {
		snprintf(out, size, "%s", override);
		return 1;
	}
	// <pak>/bin/<platform>/nextui.elf
	char exe[512];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len <= 0) return 0;
	exe[len] = '\0';
	for (int i = 0; i < 3; i++) {
		char* slash = strrchr(exe, '/');
		if (!slash) return 0;
		*slash = '\0';
	}
	snprintf(out, size, "%s", exe);
	return 1;
}

static void discover(void) {
	static int done = 0;
	if (done) return;
	done = 1;
	const char* override = getenv("MOSAIC_LAUNCHER_PLUGIN_PATH");
	if (override && override[0]) {
		char dirs[1024];
		snprintf(dirs, sizeof(dirs), "%s", override);
		for (char* dir = strtok(dirs, ":"); dir; dir = strtok(NULL, ":")) {
			scan(dir, ".plugin", 0);
		}
	}
	else {
		char pak[512];
		if (Plugins_pakDir(pak, sizeof(pak))) {
			char bundled[600];
			snprintf(bundled, sizeof(bundled), "%s/plugins", pak);
			scan(bundled, ".plugin", 1);
		}
	}
	scan(TOOLS_PATH, ".pak", 0);
}

///////////////////////////////////////
// the host table

static LoadedPlugin* loadedById(const char* id) {
	for (int i = 0; i < loaded_count; i++) {
		if (!strcmp(loaded[i].id, id)) return &loaded[i];
	}
	return NULL;
}

static void hostLog(const char* plugin_id, const char* message) {
	LOG_info("plugin %s: %s\n", plugin_id ? plugin_id : "?", message ? message : "");
}

// The loaded plugin whose hook or callback is running on this thread, which
// is the plugin calling. call is logged when there's none, such as from a
// plugin's own thread, rather than guessing.
static LoadedPlugin* callingPlugin(const char* call) {
	const char* id = Caller_current();
	LoadedPlugin* p = id ? loadedById(id) : NULL;
	if (!p) {
		static int logged = 0;
		if (!logged) LOG_warn("plugins: %s refused: it only works from a plugin's init, hooks and callbacks, on the launcher's thread\n", call);
		logged = 1;
	}
	return p;
}

// Its own settings: everything in its cfg.
static const char* hostCfgGet(const char* key, const char* fallback) {
	LoadedPlugin* p = callingPlugin("cfg_get");
	return p ? PluginCfg_get(&p->cfg, key, fallback) : fallback;
}

static void hostCfgSet(const char* key, const char* value) {
	LoadedPlugin* p = callingPlugin("cfg_set");
	if (!p) return;
	Plugins_ensureStateDir();
	if (PluginCfg_set(&p->cfg, key, value) != 0) LOG_warn("plugins: %s couldn't save %s\n", p->id, key ? key : "(null)");
}

// Another plugin's settings: only those its plugin.txt marks public.
static const char* hostCfgRead(const char* plugin_id, const char* key, const char* fallback) {
	if (!callingPlugin("cfg_read") || !plugin_id) return fallback;
	LoadedPlugin* owner = loadedById(plugin_id);
	return owner ? PluginCfg_readPublic(&owner->cfg, owner->manifest, key, fallback) : fallback;
}

static MosaicEntry* hostEntryNew(const char* path, int type, const char* name) {
	if (!path) return NULL;
	Entry* entry = name ? Entry_newNamed((char*)path, type, (char*)name) : Entry_new((char*)path, type);
	return (MosaicEntry*)entry;
}
static const char* hostEntryPath(const MosaicEntry* e) { return e ? ((const Entry*)e)->path : NULL; }
static const char* hostEntryName(const MosaicEntry* e) { return e ? ((const Entry*)e)->name : NULL; }
static int hostEntryType(const MosaicEntry* e) { return e ? ((const Entry*)e)->type : -1; }

// An entry's .info sidecar, or NULL when it has none or it's ignored: only
// a regular file, opened without blocking, of up to INFO_FILE_MAX bytes.
static InfoFile* loadInfo(const char* entry_path) {
	char path[1024];
	Info_path(entry_path, path, sizeof(path));
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		LOG_warn("plugins: ignoring %s: it isn't a regular file\n", path);
		return NULL;
	}
	char data[INFO_FILE_MAX + 1];
	size_t got = 0;
	while (got < sizeof(data)) {
		ssize_t n = read(fd, data + got, sizeof(data) - got);
		if (n <= 0) break;
		got += (size_t)n;
	}
	close(fd);
	const char* why = NULL;
	InfoFile* info = Info_parse(data, got, &why);
	if (!info) LOG_warn("plugins: ignoring %s: %s\n", path, why);
	return info;
}

static int hostEntryInfo(const MosaicEntry* e, const char* key, char* out, size_t size) {
	if (!e || !key || !out || size == 0) return -1;
	// the cache isn't part of what plugins see of the entry
	Entry* entry = (Entry*)e;
	if (!entry->info_read) {
		entry->info_read = 1;
		entry->info = loadInfo(entry->path);
	}
	return Info_copy(Info_get(entry->info, key), out, size);
}

static MosaicList* hostListNew(void) { return (MosaicList*)Array_new(); }
static int hostListCount(const MosaicList* l) { return l ? ((const Array*)l)->count : 0; }
static MosaicEntry* hostListGet(const MosaicList* l, int i) {
	const Array* a = (const Array*)l;
	if (!a || i < 0 || i >= a->count) return NULL;
	return a->items[i];
}
static void hostListPush(MosaicList* l, MosaicEntry* e) {
	if (l && e) Array_push((Array*)l, e);
}

static const MosaicEntry* hostSelectedEntry(void) {
	if (!state.top || state.top->entries->count == 0) return NULL;
	return state.top->entries->items[state.top->selected];
}
static const char* hostCurrentPath(void) {
	return state.top ? state.top->path : NULL;
}
static void hostRequestRedraw(void) {
	state.dirty = 1;
}

static int hostRegisterAction(const char* id, const char* label, int flags,
		int (*available)(const char* path, void* data), void (*run)(const char* path, void* data), void* data) {
	if (!id || !run) return -1;
	Action* action = calloc(1, sizeof(Action));
	action->takes_path = (flags & MOSAIC_ACTION_PATH) != 0;
	action->id = strdup(id);
	action->label = strdup(label ? label : id);
	action->available = available;
	action->run = run;
	action->data = data;
	// its label is the plugin's own text, translated as its id's, and it
	// runs as that plugin
	const char* owner = Caller_current();
	action->plugin_id = owner ? strdup(owner) : NULL;
	Actions_register(action);
	return 0;
}

// A plugin's own strings, under its id, in its own context or none. A NULL
// id would be the launcher's own, so it gets English.
static const char* hostTr(const char* plugin_id, const char* context, const char* text) {
	if (!text || !plugin_id || initializing >= 0) return text;
	return Tr_plugin(plugin_id, context, text);
}
static const char* hostTrPlural(const char* plugin_id, const char* context, const char* singular, const char* plural, unsigned long n) {
	if (!singular || !plural || !plugin_id || initializing >= 0) return n == 1 ? singular : plural;
	return Tr_pluginPlural(plugin_id, context, singular, plural, n);
}

// The plugins loaded this run, or in the settings every plugin the Plugins
// page shows (every one discovered), once they've all loaded.
static int hostPluginsCount(void) {
	if (!listed) return 0;
	return settings_only ? manifest_count : loaded_count;
}
static int hostPluginInfo(int index, MosaicPluginInfo* out) {
	if (!out || out->size < sizeof(out->size) || index < 0 || index >= hostPluginsCount()) return -1;
	const PluginManifest* m = settings_only ? &manifests[index] : loaded[index].manifest;
	MosaicPluginInfo full;
	memset(&full, 0, sizeof(full));
	full.size = out->size;
	snprintf(full.id, sizeof(full.id), "%s", m->id);
	Manifest_res(m, full.res, sizeof(full.res));
	snprintf(full.description, sizeof(full.description), "%s", m->description);
	// only the fields the plugin knows about
	memcpy(out, &full, out->size < sizeof(full) ? out->size : sizeof(full));
	return 0;
}

// A plugin that's on and whose init succeeded: while one's init runs, it
// and the plugins after it don't count yet.
static int hostPluginLoaded(const char* id) {
	if (!id) return 0;
	for (int i = 0; i < loaded_count; i++) {
		if (i != initializing && !strcmp(loaded[i].id, id)) return 1;
	}
	return 0;
}
static int hostHasCapability(const char* name) {
	if (!name || !name[0]) return 0;
	for (int i = 0; i < loaded_count; i++) {
		if (i != initializing && csvHas(loaded[i].manifest->provides, name)) return 1;
	}
	return 0;
}

static int hostBindButton(int screen, int button, int trigger, const char* action_id) {
	return Plugins_bindButton(screen, button, trigger, action_id);
}

static void hostToast(const char* message) {
	Toast_show(message);
}

static void hostInhibitSleep(const char* plugin_id, int on) {
	char who[80];
	snprintf(who, sizeof(who), "plugin:%s", plugin_id ? plugin_id : "?");
	Awake_set(who, on);
}

static int hostOpenPath(const char* path, int type) {
	if (type != MOSAIC_ENTRY_ROM && type != MOSAIC_ENTRY_PAK && type != MOSAIC_ENTRY_DIR) return 0;
	int opened = Entry_openPath(path, type);
	// main's copies for this frame, so the game list's input doesn't put
	// the old folder's index on the new one
	if (state.top) {
		selected = state.top->selected;
		total = state.top->entries->count;
	}
	return opened;
}

static int hostRecentsCount(void) {
	return state.recents ? state.recents->count : 0;
}
static const char* hostRecentPath(int index) {
	static char path[512];
	if (!state.recents || index < 0 || index >= state.recents->count) return NULL;
	Recent* recent = state.recents->items[index];
	snprintf(path, sizeof(path), "%s%s", SDCARD_PATH, recent->path);
	return path;
}
static int hostRecentRemove(int index) {
	int result = Recents_remove(index);
	if (result == 0 && state.switcher_selected >= state.recents->count) {
		state.switcher_selected = state.recents->count > 0 ? state.recents->count - 1 : 0;
	}
	return result;
}
static void hostRecentsClear(void) {
	Recents_clear();
	state.switcher_selected = 0;
}

// The game list's resume state, which readyResumePath works out for one
// entry at a time, kept while the recents' is looked up.
typedef struct ResumeState {
	int can_resume;
	int has_preview;
	char slot_path[256];
	char preview_path[256];
} ResumeState;
static void resumeSave(ResumeState* saved) {
	saved->can_resume = state.can_resume;
	saved->has_preview = state.has_preview;
	memcpy(saved->slot_path, state.slot_path, sizeof(saved->slot_path));
	memcpy(saved->preview_path, state.preview_path, sizeof(saved->preview_path));
}
static void resumeRestore(const ResumeState* saved) {
	state.can_resume = saved->can_resume;
	state.has_preview = saved->has_preview;
	memcpy(state.slot_path, saved->slot_path, sizeof(state.slot_path));
	memcpy(state.preview_path, saved->preview_path, sizeof(state.preview_path));
}

static int hostRecentInfo(int index, MosaicRecent* out) {
	if (!out || out->size < sizeof(out->size) || !state.recents || index < 0 || index >= state.recents->count) return -1;
	Recent* recent = state.recents->items[index];
	char sd_path[256];
	snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, recent->path);
	// as entryFromRecent makes the built-in switcher's entry
	int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM;
	MosaicRecent full;
	memset(&full, 0, sizeof(full));
	full.size = out->size;
	full.type = type == ENTRY_PAK ? MOSAIC_ENTRY_PAK : MOSAIC_ENTRY_ROM;
	full.available = recent->available ? 1 : 0;
	if (recent->alias) snprintf(full.name, sizeof(full.name), "%s", recent->alias);
	else {
		Entry* entry = Entry_new(sd_path, type);
		snprintf(full.name, sizeof(full.name), "%s", entry->name);
		Entry_free(entry);
	}
	ResumeState saved;
	resumeSave(&saved);
	readyResumePath(sd_path, type);
	full.can_resume = state.can_resume;
	if (state.has_preview) snprintf(full.preview, sizeof(full.preview), "%s", state.preview_path);
	resumeRestore(&saved);
	// only the fields the plugin knows about
	memcpy(out, &full, out->size < sizeof(full) ? out->size : sizeof(full));
	return 0;
}

static int screenCallAllowed(const char* call);

// The built-in switcher's A, for a recent by index.
static int hostRecentOpen(int index, int resume) {
	if (!screenCallAllowed("recent_open") || !state.top || !state.recents || index < 0 || index >= state.recents->count) return 0;
	Entry* entry = entryFromRecent(state.recents->items[index]);
	if (!entry) return 0; // its emulator isn't installed
	// back to the switcher screen after the game, as stock's switcher comes back to itself
	int from_switcher = ScreenSet_switcherOpen(&screens);
	if (from_switcher) putFile(GAME_SWITCHER_PERSIST_PATH, "unused");
	ResumeState saved;
	resumeSave(&saved);
	readyResume(entry);
	state.should_resume = resume && state.can_resume;
	Entry_open(entry);
	int opened = state.startgame;
	state.recent_alias = NULL; // the entry's name, freed below
	Entry_free(entry);
	if (!opened) {
		if (from_switcher) unlink(GAME_SWITCHER_PERSIST_PATH);
		state.should_resume = 0;
		resumeRestore(&saved);
	}
	state.dirty = 1;
	return opened;
}
static void hostReloadLists(void) {
	// on the next frame, so a hook mid-listing never has its list freed
	state.reload = 1;
}

// Set while list_ready hooks run, the only time entry_set_name works.
static int listing = 0;
static int list_renamed = 0;

static int hostEntrySetName(MosaicEntry* e, const char* name) {
	if (!listing || !e || !name || !name[0]) return -1;
	Entry* entry = (Entry*)e;
	free(entry->name);
	entry->name = strdup(name);
	list_renamed = 1;
	return 0;
}

static const MosaicList* hostCurrentList(void) {
	return state.top ? (const MosaicList*)state.top->entries : NULL;
}
static int hostSelectedIndex(void) {
	if (!state.top || state.top->entries->count == 0) return -1;
	return state.top->selected;
}
static int hostSelectIndex(int index) {
	if (layout_rendering || decorating || selecting || backgrounding || generating || screen_drawing || transition_owner >= 0 || !state.top || index < 0 || index >= state.top->entries->count) return -1;
	Directory_select(state.top, index);
	// main's copy for this frame, so the game list's input doesn't put it back
	selected = index;
	readyResume(state.top->entries->items[index]);
	state.dirty = 1;
	return 0;
}

static int hostQuickTile(const char* action_id, int row, const char* icon, const char* icon_off, int (*is_on)(void* data)) {
	int quick_row;
	if (row == MOSAIC_QUICK_TOP) quick_row = QUICK_ROW_TOP;
	else if (row == MOSAIC_QUICK_BOTTOM) quick_row = QUICK_ROW_BOTTOM;
	else return -1;
	if (QuickTiles_add(action_id, quick_row, icon, icon_off, is_on) != 0) {
		LOG_warn("plugins: can't add a quick menu tile for %s\n", action_id ? action_id : "(null)");
		return -1;
	}
	state.rebuild_quick = 1;
	return 0;
}
static int hostHideQuickTile(const char* name, int hidden) {
	if (QuickTiles_hide(name, hidden) != 0) return -1;
	state.rebuild_quick = 1;
	return 0;
}
static int hostRecentSet(int index, const char* path) {
	return Recents_set(index, path);
}

static int hostOverrideButton(int screen, int button, int trigger, const char* action_id);
static int hostReplaceLaunch(const char* program, const char* const* args, int count);

static int hostRenameQuickTile(const char* name, const char* label) {
	if (QuickTiles_rename(name, label) != 0) {
		LOG_warn("plugins: can't rename the quick menu tile %s\n", name ? name : "(null)");
		return -1;
	}
	state.rebuild_quick = 1;
	return 0;
}

// Desktop has no battery or LEDs; its platform code reports a full battery
// on charge and LEDs that aren't there.
#define HAS_BATTERY (strcmp(PLATFORM, "desktop") != 0)

// The tg5040's and tg5050's switch, which stock's keymon mirrors into
// msettings' mute state; the h700 and desktop have none.
#define HAS_HOLD_SWITCH (!strcmp(PLATFORM, "tg5040") || !strcmp(PLATFORM, "tg5050"))

static int hostHoldSwitch(void) {
	if (!HAS_HOLD_SWITCH || !InitializedSettings()) return -1;
	return GetMute() ? 1 : 0;
}

static int hostHasPin(void) {
	return Pin_isSet(PIN_PATH);
}

// One PIN keyboard at a time, like the modal it opens.
static void (*pin_done)(int result, void* data);
static void* pin_data;

static void pinTyped(const char* text, void* data) {
	(void)data;
	void (*done)(int, void*) = pin_done;
	pin_done = NULL;
	if (!done) return;
	if (!text) done(MOSAIC_PIN_CANCELLED, pin_data);
	else done(Pin_matches(PIN_PATH, text) ? MOSAIC_PIN_RIGHT : MOSAIC_PIN_WRONG, pin_data);
}

static void hostAskPin(const char* title, void (*on_done)(int result, void* data), void* data) {
	if (!on_done) return;
	if (!Pin_isSet(PIN_PATH)) {
		on_done(MOSAIC_PIN_RIGHT, data);
		return;
	}
	pin_done = on_done;
	pin_data = data;
	Modal_keyboardHidden(title && title[0] ? title : TR("PIN"), pinTyped, NULL);
}

void Plugins_adoptPin(void) {
	// both files sit in PLUGIN_STATE_DIR, so there's nothing to move without it
	if (Pin_adopt(PIN_PATH, PLUGIN_STATE_DIR "/simple-mode.cfg")) LOG_info("plugins: simple mode's PIN is the launcher's PIN now\n");
}

static int hostPlayGameInfo(int index, MosaicPlayGame* out) {
	if (!out || out->size < sizeof(out->size)) return -1;
	const PlayGame* game = PlayHistory_game(index);
	if (!game) return -1;
	MosaicPlayGame full;
	memset(&full, 0, sizeof(full));
	full.size = out->size;
	PlayHistory_gamePath(game, full.path, sizeof(full.path));
	if (game->name) snprintf(full.name, sizeof(full.name), "%s", game->name);
	full.seconds = game->seconds;
	full.first_played = game->first;
	full.last_played = game->last;
	full.count = game->count;
	full.orphan = game->orphan;
	// only the fields the plugin knows about
	memcpy(out, &full, out->size < sizeof(full) ? out->size : sizeof(full));
	return 0;
}

static int hostAchievements(const char* path, MosaicAchievements* out) {
	RASummary summary;
	int rc = RAProgress_get(path, &summary);
	if (rc != 1 || !out) return rc;
	if (out->size < sizeof(out->size)) return MOSAIC_ACHIEVEMENTS_UNKNOWN;
	MosaicAchievements full = {
		.size = out->size,
		.total = summary.total,
		.unlocked = summary.unlocked,
		.unlocked_hardcore = summary.unlocked_hardcore,
		.points = summary.points,
		.points_unlocked = summary.points_unlocked,
		.muted = summary.muted,
		.game_id = summary.game_id,
	};
	// only the fields the plugin knows about
	memcpy(out, &full, out->size < sizeof(full) ? out->size : sizeof(full));
	return 1;
}

static int hostAchievementHash(const char* path, char* out, size_t size) {
	return RAProgress_hash(path, out, size);
}

static int hostBatteryLevel(void) {
	return HAS_BATTERY ? PWR_getBattery() : -1;
}
static int hostIsCharging(void) {
	return HAS_BATTERY ? (PWR_isCharging() ? 1 : 0) : -1;
}
static int hostLedCount(void) {
	return HAS_BATTERY ? LEDS_getCount() : 0;
}

static void hostPowerOff(int reboot) {
	// on the next frame, from the main loop, as the quick menu's tiles do
	state.power_off = reboot == 1 ? 2 : 1;
	LOG_info("plugins: a plugin asked to %s\n", reboot == 1 ? "reboot" : "power off");
}

static int hostShowRecents(void) {
	if (!CFG_getShowRecents() || !state.recents) return 0;
	// as getRoot does: only recents whose game is on the card count
	for (int i = 0; i < state.recents->count; i++) {
		const Recent* recent = state.recents->items[i];
		if (recent->available) return 1;
	}
	return 0;
}

static int hostStockToggleGet(const char* name) {
	return StockToggles_get(name);
}
static int hostStockToggleSet(const char* name, int on) {
	if (StockToggles_set(name, on) != 0) {
		LOG_warn("plugins: refused to set stock toggle %s to %i\n", name ? name : "(null)", on);
		return -1;
	}
	LOG_info("plugins: a plugin set stock toggle %s to %i\n", name, on);
	// the clock and battery percentage are in the status bar
	state.dirty = 1;
	return 0;
}

static int hostLedSetColor(const char* plugin_id, unsigned int rgb) {
	const char* holder = Leds_holder();
	if (hostLedCount() == 0 || Leds_set(plugin_id, rgb) != 0) {
		if (holder && plugin_id && strcmp(holder, plugin_id) != 0) {
			LOG_warn("plugins: refused %s's LED colour, %s's is on\n", plugin_id, holder);
		}
		else {
			LOG_warn("plugins: refused LED colour %06X for %s\n", rgb, plugin_id ? plugin_id : "(null)");
		}
		return -1;
	}
	// once, not on every change of colour
	if (!holder) LOG_info("plugins: %s set the LED colour\n", plugin_id);
	return 0;
}
static int hostLedClear(const char* plugin_id) {
	if (Leds_clear(plugin_id) != 0) return -1;
	LOG_info("plugins: %s cleared the LED colour\n", plugin_id);
	return 0;
}
static int hostThemeColor(int index) {
	return Leds_themeColor(index);
}

// Stock's BT_* calls block for seconds and build shell strings from the
// address, so plugins get bluetooth.c's worker instead. Desktop's stock
// platform code has no Bluetooth.
static int hostBtSupported(void) {
	return BT_supported() ? 1 : 0;
}
static int hostBtEnabled(void) {
	return BT_supported() && CFG_getBluetooth() ? 1 : 0;
}
static int hostBtConnectedAddress(char* out, size_t size) {
	if (out && size) out[0] = '\0';
	if (!BT_supported()) return 0;
	Bluetooth_setEnabled(CFG_getBluetooth());
	return Bluetooth_connectedAddress(out, size);
}
static int hostBtConnect(const char* address) {
	// only a checked address goes in the log
	const char* shown = Bluetooth_validAddress(address) ? address : "(not an address)";
	const char* why = NULL;
	if (!BT_supported()) why = "no Bluetooth";
	else {
		Bluetooth_setEnabled(CFG_getBluetooth());
		switch (Bluetooth_connect(address)) {
			case 0: break;
			case BLUETOOTH_BAD_ADDRESS: why = "not an address"; break;
			case BLUETOOTH_OFF: why = "Bluetooth is off"; break;
			case BLUETOOTH_BUSY: why = "another connect is running"; break;
			default: why = "no worker thread"; break;
		}
	}
	if (why) {
		LOG_warn("plugins: refused Bluetooth connect to %s: %s\n", shown, why);
		return -1;
	}
	LOG_info("plugins: queued Bluetooth connect to %s\n", shown);
	return 0;
}
static void bluetoothLog(const char* message) {
	LOG_info("plugins: %s", message);
}

// Desktop's stock platform code claims a Wi-Fi connection it doesn't have,
// so desktop answers that there's no Wi-Fi. Its tests can stand a file in
// for a device with MOSAIC_LAUNCHER_WIFI_FAKE (see Wifi_parseFake); device
// builds never read it.
#define IS_DESKTOP (strcmp(PLATFORM, "desktop") == 0)

static const char* wifiFake(void) {
	if (!IS_DESKTOP) return NULL;
	const char* path = getenv("MOSAIC_LAUNCHER_WIFI_FAKE");
	return path && path[0] ? path : NULL;
}
static int wifiSupported(void) {
	if (IS_DESKTOP) return wifiFake() ? 1 : 0;
	return WIFI_supported() ? 1 : 0;
}
// The toggle, read on the main thread and handed to the worker.
static int wifiOn(void) {
	if (!wifiSupported()) return 0;
	if (!IS_DESKTOP) return WIFI_enabled() ? 1 : 0;
	char* text = readText(wifiFake());
	int on = 0;
	Wifi_parseFake(text, &on, NULL, NULL);
	free(text);
	return on;
}
// On the worker: stock's call runs wpa_cli, ip and iw.
static int wifiStockSource(WifiInfo* out) {
	struct WIFI_connection c;
	memset(&c, 0, sizeof(c));
	int rc = WIFI_connectionInfo(&c);
	Wifi_fill(out, rc == 0 && c.valid, c.ssid, c.ip, c.freq, c.rssi, c.link_speed);
	return 0;
}
static int wifiFakeSource(WifiInfo* out) {
	const char* path = wifiFake();
	char* text = path ? readText(path) : NULL;
	int delay_ms = 0;
	Wifi_parseFake(text, NULL, out, &delay_ms);
	free(text);
	if (delay_ms) usleep((useconds_t)delay_ms * 1000);
	return 0;
}
static int hostWifiSupported(void) {
	return wifiSupported();
}
static int hostWifiEnabled(void) {
	if (!wifiSupported()) return MOSAIC_WIFI_UNAVAILABLE;
	return wifiOn();
}
static int hostWifiConnection(MosaicWifi* out) {
	if (!out || out->size < sizeof(out->size) || !wifiSupported()) return MOSAIC_WIFI_UNAVAILABLE;
	Wifi_setSource(IS_DESKTOP ? wifiFakeSource : wifiStockSource);
	Wifi_setEnabled(wifiOn());
	WifiInfo info;
	int rc = Wifi_connection(&info);
	MosaicWifi full;
	memset(&full, 0, sizeof(full));
	full.size = out->size;
	if (rc == 1) {
		snprintf(full.ssid, sizeof(full.ssid), "%s", info.ssid);
		snprintf(full.ip, sizeof(full.ip), "%s", info.ip);
		full.rssi = info.rssi;
		full.bars = info.bars;
		full.freq_mhz = info.freq;
		full.link_mbps = info.link;
	}
	// only the fields the plugin knows about
	memcpy(out, &full, out->size < sizeof(full) ? out->size : sizeof(full));
	return rc;
}
static void wifiLog(const char* message) {
	LOG_info("plugins: %s", message);
}

// home is claimed once every plugin has loaded, so it's NULL from init
static const char* hostHomeScreen(void) {
	return ScreenSet_homeId(&screens);
}

///////////////////////////////////////
// FN buttons

static FnBinds fn_binds; // plugins' bindings of FN1 to FN3, never saved

// The device's names for FN1 to FN3, from its platform.h. Desktop has
// none; its tests can stand a comma-separated list in for a device's with
// MOSAIC_LAUNCHER_FN_FAKE, eg. "L3,R3,HOME". Device builds never read it.
static const char* fnName(int index) {
	if (IS_DESKTOP) {
		static char fake[FN_COUNT][FN_NAME_MAX];
		FnBinds_parseNames(getenv("MOSAIC_LAUNCHER_FN_FAKE"), fake);
		return index >= 0 && index < FN_COUNT ? fake[index] : "";
	}
	switch (index) {
		case 0: return BTN_FN1_NAME;
		case 1: return BTN_FN2_NAME;
		case 2: return BTN_FN3_NAME;
		default: return "";
	}
}

const char* Plugins_fnName(int index) {
	const char* name = fnName(index);
	return name ? name : "";
}

static int fnIndex(int button) {
	if (button < MOSAIC_BUTTON_FN1 || button > MOSAIC_BUTTON_FN3) return -1;
	return button - MOSAIC_BUTTON_FN1;
}

static int fnPresent(int index) {
	return index >= 0 && Plugins_fnName(index)[0];
}

// The user's choice in Settings takes over from a plugin's binding as soon
// as the launcher sees it.
static const char* fnUser(int index) {
	const char* user = CFG_getFnAction(index);
	char dropped[FN_OWNER_MAX];
	if (FnBinds_yield(&fn_binds, index, user, dropped, sizeof(dropped))) {
		LOG_info("plugins: %s's binding of FN%i is dropped, as the user assigned it in Settings\n", dropped, index + 1);
	}
	return user;
}

const char* Plugins_fnAction(int index) {
	return FnBinds_effective(&fn_binds, index, fnUser(index), NULL);
}

static const char* hostFnName(int button) {
	return Plugins_fnName(fnIndex(button));
}

static int hostFnBinding(int button, MosaicFnBinding* out) {
	int index = fnIndex(button);
	// it can drop a binding, so only on the launcher's thread
	if (!callingPlugin("fn_binding") || !out || out->size < sizeof(out->size) || !fnPresent(index)) return -1;
	const char* owner = NULL;
	const char* action = FnBinds_effective(&fn_binds, index, fnUser(index), &owner);
	MosaicFnBinding full;
	memset(&full, 0, sizeof(full));
	full.size = out->size;
	full.by = owner ? MOSAIC_FN_PLUGIN : action[0] ? MOSAIC_FN_USER : MOSAIC_FN_NONE;
	snprintf(full.action, sizeof(full.action), "%s", action);
	snprintf(full.plugin_id, sizeof(full.plugin_id), "%s", owner ? owner : "");
	// only the fields the plugin knows about
	memcpy(out, &full, out->size < sizeof(full) ? out->size : sizeof(full));
	return 0;
}

static int hostFnBind(int button, const char* action) {
	LoadedPlugin* p = callingPlugin("fn_bind");
	if (!p) return -1;
	int index = fnIndex(button);
	int rc = FnBinds_bind(&fn_binds, index, p->id, action, fnPresent(index), index >= 0 ? fnUser(index) : NULL);
	switch (rc) {
		case FN_BIND_OK:
			LOG_info("plugins: %s bound FN%i (%s) to %s\n", p->id, index + 1, Plugins_fnName(index), action);
			return 0;
		case FN_BIND_MISSING:
			LOG_info("plugins: %s can't bind FN%i: this device doesn't have it\n", p->id, index + 1);
			break;
		case FN_BIND_USER:
			LOG_info("plugins: %s can't bind FN%i: the user assigned it in Settings\n", p->id, index + 1);
			break;
		case FN_BIND_TAKEN:
			LOG_info("plugins: %s can't bind FN%i: %s bound it first\n", p->id, index + 1, fn_binds.owner[index]);
			break;
		default:
			LOG_warn("plugins: %s can't bind button %i to %s\n", p->id, button, action ? action : "(null)");
			break;
	}
	return -1;
}

static int hostFnRelease(int button) {
	LoadedPlugin* p = callingPlugin("fn_release");
	if (!p) return -1;
	int index = fnIndex(button);
	if (FnBinds_release(&fn_binds, index, p->id) != 0) return -1;
	LOG_info("plugins: %s released FN%i\n", p->id, index + 1);
	return 0;
}

// Unloading a plugin frees its buttons for another.
static void fnReleaseAll(const char* plugin_id) {
	if (FnBinds_releaseAll(&fn_binds, plugin_id) > 0) LOG_info("plugins: %s's FN bindings are released\n", plugin_id);
}

///////////////////////////////////////
// entry sets

static EntrySets sets; // plugins' named sets, by owner; the owners answer for what's in them

static int hostSetRegister(const char* name, int (*has)(const char* name, const char* path),
		void (*list)(const char* name, void (*add)(const char* path, void* ctx), void* ctx)) {
	LoadedPlugin* p = callingPlugin("set_register");
	if (!p) return -1;
	int rc = EntrySets_register(&sets, name, p->id, has, list);
	if (rc == SET_OK) return 0;
	const char* shown = name ? name : "(null)";
	switch (rc) {
		case SET_RESERVED:
			LOG_info("plugins: %s can't register set %s: it may only register favorites, hidden, tags:<tag>, smart:<collection> or %s.<name>\n",
				p->id, shown, p->id);
			break;
		case SET_TAKEN:
			LOG_info("plugins: %s can't register set %s: %s registered it first\n", p->id, shown, EntrySets_find(&sets, name)->owner);
			break;
		case SET_FULL:
			LOG_warn("plugins: %s can't register set %s: there are too many sets\n", p->id, shown);
			break;
		default:
			LOG_warn("plugins: %s can't register set %s without both callbacks\n", p->id, shown);
			break;
	}
	return -1;
}

static int hostSetUnregister(const char* name) {
	LoadedPlugin* p = callingPlugin("set_unregister");
	return p ? EntrySets_unregister(&sets, name, p->id) : -1;
}

// The set to ask, marked busy until setDone, with its name and owner
// copied out, as a callback can register or unregister sets meanwhile.
// NULL for none, or one already being asked.
static EntrySet* setBegin(const char* name, const char* call, char* copy, LoadedPlugin** owner) {
	EntrySet* set = EntrySets_find(&sets, name);
	if (!set) return NULL;
	if (set->busy) {
		static int logged = 0;
		if (!logged) LOG_warn("plugins: %s on set %s refused: it was asked from inside its own callback\n", call, set->name);
		logged = 1;
		return NULL;
	}
	*owner = loadedById(set->owner);
	if (!*owner) return NULL;
	snprintf(copy, SET_NAME_MAX, "%s", set->name);
	set->busy = 1;
	return set;
}

static void setDone(const char* name) {
	EntrySet* set = EntrySets_find(&sets, name);
	if (set) set->busy = 0;
}

static int hostSetHas(const char* name, const char* path) {
	if (!callingPlugin("set_has") || !path) return 0;
	char copy[SET_NAME_MAX];
	LoadedPlugin* owner = NULL;
	EntrySet* set = setBegin(name, "set_has", copy, &owner);
	if (!set) return 0;
	SetHasFn has = set->has;
	int in = 0;
	AS_CALLER(owner->id) in = has(copy, path) != 0;
	setDone(copy);
	return in;
}

// A set_list in progress: the reader's callback, run as the reader for
// each path the owner adds. They nest, so add can tell a walk that's
// still going from one that's over.
typedef struct SetWalk {
	const char* reader;
	void (*each)(const char* path, void* data);
	void* data;
	int count;
	struct SetWalk* outer;
} SetWalk;
static SetWalk* walks = NULL;

static void setAdd(const char* path, void* ctx) {
	SetWalk* walk = walks;
	while (walk && walk != ctx) walk = walk->outer;
	if (!walk) {
		static int logged = 0;
		if (!logged) LOG_warn("plugins: a set's add was called after its list returned, and ignored\n");
		logged = 1;
		return;
	}
	if (!path || !path[0]) return;
	walk->count++;
	if (walk->each) AS_CALLER(walk->reader) walk->each(path, walk->data);
}

static int hostSetList(const char* name, void (*each)(const char* path, void* data), void* data) {
	LoadedPlugin* reader = callingPlugin("set_list");
	if (!reader) return -1;
	char copy[SET_NAME_MAX];
	LoadedPlugin* owner = NULL;
	EntrySet* set = setBegin(name, "set_list", copy, &owner);
	if (!set) return -1;
	SetListFn list = set->list;
	SetWalk walk = { reader->id, each, data, 0, walks };
	walks = &walk;
	AS_CALLER(owner->id) list(copy, setAdd, &walk);
	walks = walk.outer;
	setDone(copy);
	return walk.count;
}

// Unloading a plugin removes its sets.
static void setsReleaseAll(const char* plugin_id) {
	int released = EntrySets_releaseAll(&sets, plugin_id);
	if (released > 0) LOG_info("plugins: %s's sets are released (%i)\n", plugin_id, released);
}

static int hostIsFiltered(const char* path);

///////////////////////////////////////
// events

static EventQueue event_queue; // published, waiting for the launcher's thread; under event_lock
static EventSubs event_subs;   // only touched on the launcher's thread
static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;

// Each loaded plugin's library, so publish knows who called from a
// plugin's own thread; under event_lock.
typedef struct Publisher {
	void* base;
	char id[EVENT_OWNER_MAX];
} Publisher;
static Publisher publishers[PLUGIN_MAX];
static int publisher_count = 0;

static void publisherAdd(void* base, const char* id) {
	if (!base) return;
	pthread_mutex_lock(&event_lock);
	if (publisher_count < PLUGIN_MAX) {
		publishers[publisher_count].base = base;
		snprintf(publishers[publisher_count].id, EVENT_OWNER_MAX, "%s", id);
		publisher_count++;
	}
	pthread_mutex_unlock(&event_lock);
}

static void publisherRemove(const char* id) {
	pthread_mutex_lock(&event_lock);
	for (int i = publisher_count - 1; i >= 0; i--) {
		if (strcmp(publishers[i].id, id) != 0) continue;
		memmove(&publishers[i], &publishers[i + 1], (size_t)(publisher_count - i - 1) * sizeof(Publisher));
		publisher_count--;
	}
	pthread_mutex_unlock(&event_lock);
}

// The plugin calling, into id: the one running on this thread, else the
// one whose library the call came from. 0 for none.
static int publisherOf(const void* from, char* id) {
	const char* current = Caller_current();
	if (current) {
		snprintf(id, EVENT_OWNER_MAX, "%s", current);
		return 1;
	}
	void* base = Callsite_base(from);
	int found = 0;
	pthread_mutex_lock(&event_lock);
	for (int i = 0; i < publisher_count && !found && base; i++) {
		if (publishers[i].base != base) continue;
		snprintf(id, EVENT_OWNER_MAX, "%s", publishers[i].id);
		found = 1;
	}
	pthread_mutex_unlock(&event_lock);
	return found;
}

// Logs a refused event, once for each reason, as a plugin's thread could
// publish one every frame.
static void eventRefused(int rc, const char* name, const char* publisher) {
	static int logged[5];
	int reason = -rc;
	if (reason < 1 || reason > 4 || logged[reason]) return;
	logged[reason] = 1;
	const char* shown = name ? name : "(null)";
	const char* who = publisher ? publisher : "the launcher";
	switch (rc) {
		case EVENT_NOT_YOURS:
			LOG_info("plugins: %s can't publish %s: it may only publish %s.<name>\n", who, shown, who);
			break;
		case EVENT_BAD_PAYLOAD:
			LOG_info("plugins: %s's event %s was refused: its payload isn't key=value lines under %i bytes\n", who, shown, EVENT_PAYLOAD_MAX);
			break;
		case EVENT_FULL:
			LOG_warn("plugins: %s's event %s was dropped: %i events are already waiting\n", who, shown, EVENT_QUEUE_MAX);
			break;
		default:
			LOG_info("plugins: %s's event %s was refused: its name is empty, too long or has spaces\n", who, shown);
			break;
	}
}

// Queues an event, publisher's or (NULL) the launcher's. 0, or -1 when it
// was refused.
static int eventPost(const char* name, const char* payload, const char* publisher, int reserve) {
	int rc;
	pthread_mutex_lock(&event_lock);
	if (publisher && !Events_mayPublish(name, publisher)) rc = Events_nameValid(name) ? EVENT_NOT_YOURS : EVENT_BAD_NAME;
	else rc = EventQueue_add(&event_queue, name, payload, reserve);
	pthread_mutex_unlock(&event_lock);
	if (rc == EVENT_OK) return 0;
	eventRefused(rc, name, publisher);
	return -1;
}

// The launcher's own events, launcher.<name>; none without plugins.
static void hostEvent(const char* name, const char* payload) {
	if (loaded_count > 0) eventPost(name, payload, NULL, 1);
}

__attribute__((noinline))
static int hostPublish(const char* name, const char* payload) {
	char id[EVENT_OWNER_MAX];
	if (!publisherOf(__builtin_return_address(0), id)) {
		static int logged = 0;
		if (!logged) LOG_warn("plugins: publish refused: it didn't come from a loaded plugin\n");
		logged = 1;
		return -1;
	}
	return eventPost(name, payload, id, 0);
}

static int hostSubscribe(const char* name, void (*on_event)(const char* name, const char* payload, void* data), void* data) {
	LoadedPlugin* p = callingPlugin("subscribe");
	if (!p) return -1;
	int rc = EventSubs_add(&event_subs, name, p->id, on_event, data);
	if (rc == EVENT_OK) return 0;
	if (rc == EVENT_FULL) LOG_warn("plugins: %s can't subscribe to %s: there are %i subscriptions\n", p->id, name, EVENT_SUBS_MAX);
	else LOG_info("plugins: %s can't subscribe to %s: no callback, or a name that's empty, too long or has spaces\n",
		p->id, name ? name : "(null)");
	return -1;
}

static int hostUnsubscribe(const char* name) {
	LoadedPlugin* p = callingPlugin("unsubscribe");
	return p ? EventSubs_remove(&event_subs, name, p->id) : -1;
}

static int hostEventValue(const char* payload, const char* key, char* out, size_t size) {
	return Events_value(payload, key, out, size);
}

// Delivers the events waiting now, oldest first, each to its subscribers
// in the order they subscribed, as the subscriber. Events published
// meanwhile wait for the next time, so two plugins answering each other
// can't keep it going.
static int delivering = 0;

static void eventsDeliver(void) {
	if (delivering || !loaded_count) return;
	pthread_mutex_lock(&event_lock);
	int waiting = event_queue.count;
	pthread_mutex_unlock(&event_lock);
	if (!waiting) return;
	delivering = 1;
	static Event event; // only on this thread, and too big for every stack
	for (int n = 0; n < waiting; n++) {
		pthread_mutex_lock(&event_lock);
		int took = EventQueue_take(&event_queue, &event);
		pthread_mutex_unlock(&event_lock);
		if (!took) break;
		EventSub matched[PLUGIN_MAX];
		int count = EventSubs_match(&event_subs, event.name, matched, PLUGIN_MAX);
		for (int k = 0; k < count; k++) {
			// one that unsubscribed, or changed its callback, in an earlier callback
			const EventSub* still = EventSubs_find(&event_subs, event.name, matched[k].owner);
			if (!still || still->fn != matched[k].fn || still->data != matched[k].data) continue;
			LoadedPlugin* p = loadedById(matched[k].owner);
			if (!p) continue;
			AS_CALLER(p->id) matched[k].fn(event.name, event.payload, matched[k].data);
		}
	}
	delivering = 0;
}

// Unloading a plugin removes its subscriptions; what it published stays
// queued.
static void eventsReleaseAll(const char* plugin_id) {
	int released = EventSubs_releaseAll(&event_subs, plugin_id);
	if (released > 0) LOG_info("plugins: %s's subscriptions are released (%i)\n", plugin_id, released);
	publisherRemove(plugin_id);
}

// An owner's set changed: launcher.set_changed, with name=<set>. A set
// the caller owns, or one it may register that nobody has, so a set it
// just unregistered can be announced too.
static int hostSetChanged(const char* name) {
	LoadedPlugin* p = callingPlugin("set_changed");
	if (!p || !name) return -1;
	EntrySet* set = EntrySets_find(&sets, name);
	if (set ? strcmp(set->owner, p->id) != 0 : !EntrySets_nameAllowed(name, p->id)) {
		LOG_info("plugins: %s can't announce set %s: it isn't its set\n", p->id, name);
		return -1;
	}
	char payload[SET_NAME_MAX + 8];
	snprintf(payload, sizeof(payload), "name=%s", name);
	return eventPost(EVENT_HOST_PREFIX "set_changed", payload, NULL, 0);
}

static int hostSetNames(void (*each)(const char* name, void* data), void* data) {
	if (!callingPlugin("set_names")) return -1;
	int count = sets.count;
	if (!each || !count) return count;
	// a copy, as each can register or unregister sets
	char (*names)[SET_NAME_MAX] = malloc((size_t)count * SET_NAME_MAX);
	if (!names) return -1;
	for (int i = 0; i < count; i++) memcpy(names[i], sets.items[i].name, SET_NAME_MAX);
	for (int i = 0; i < count; i++) each(names[i], data);
	free(names);
	return count;
}

///////////////////////////////////////
// providers

static Providers providers; // plugins' services, by owner; only touched on the launcher's thread
_Static_assert(CALL_TEXT_MAX == MOSAIC_CALL_MAX && CALL_DEPTH_MAX == MOSAIC_CALL_DEPTH, "calls' sizes differ from the SDK's");
_Static_assert(CALL_UNKNOWN == MOSAIC_CALL_UNKNOWN && CALL_TOO_SMALL == MOSAIC_CALL_TOO_SMALL
	&& CALL_REFUSED == MOSAIC_CALL_REFUSED && CALL_FAILED == MOSAIC_CALL_FAILED, "calls' results differ from the SDK's");

static int hostProvide(const char* name, int (*fn)(const char* name, const char* args, char* out, size_t size)) {
	LoadedPlugin* p = callingPlugin("provide");
	if (!p) return -1;
	int rc = Providers_add(&providers, name, p->id, p->manifest->provides, fn);
	if (rc == PROVIDE_OK) return 0;
	const char* shown = name ? name : "(null)";
	switch (rc) {
		case PROVIDE_RESERVED:
			LOG_info("plugins: %s can't provide %s: sets. and events. names are entry sets' and events'\n", p->id, shown);
			break;
		case PROVIDE_UNDECLARED:
			LOG_info("plugins: %s can't provide %s: its plugin.txt's provides= doesn't list it\n", p->id, shown);
			break;
		case PROVIDE_TAKEN:
			LOG_info("plugins: %s can't provide %s: %s provided it first\n", p->id, shown, Providers_find(&providers, name)->owner);
			break;
		case PROVIDE_FULL:
			LOG_warn("plugins: %s can't provide %s: there are %i services\n", p->id, shown, PROVIDERS_MAX);
			break;
		default:
			LOG_info("plugins: %s can't provide %s: no callback, or a name that isn't <kind>.<name>\n", p->id, shown);
			break;
	}
	return -1;
}

// Runs a provider's callback as its owner.
static int providerRun(const Provider* provider, const char* args, char* reply, size_t size, void* ctx) {
	(void)ctx;
	LoadedPlugin* owner = loadedById(provider->owner);
	if (!owner) return -1;
	int rc = -1;
	AS_CALLER(owner->id) rc = provider->fn(provider->name, args, reply, size);
	return rc;
}

static int hostCall(const char* name, const char* args, char* out, size_t size) {
	LoadedPlugin* p = callingPlugin("call");
	if (!p) {
		if (out && size) out[0] = '\0';
		return CALL_REFUSED;
	}
	int rc = Providers_call(&providers, name, args, out, size, providerRun, NULL);
	// once each, as a plugin could call every frame
	static int logged[5];
	int reason = -rc;
	if (rc >= CALL_UNKNOWN || reason > 4 || logged[reason]) return rc;
	logged[reason] = 1;
	const char* shown = name ? name : "(null)";
	const Provider* provider = Providers_find(&providers, name);
	const char* owner = provider ? provider->owner : "?";
	switch (rc) {
		case CALL_TOO_SMALL:
			LOG_info("plugins: %s's call to %s got a reply bigger than its %zu bytes\n", p->id, shown, size);
			break;
		case CALL_FAILED:
			LOG_info("plugins: %s's call to %s failed: %s didn't answer, or its reply isn't key=value lines\n", p->id, shown, owner);
			break;
		default:
			LOG_info("plugins: %s's call to %s was refused: bad arguments, %s's callback for it is still running, or calls nested %i deep\n",
				p->id, shown, owner, CALL_DEPTH_MAX);
			break;
	}
	return rc;
}

// Unloading a plugin releases its services.
static void providersReleaseAll(const char* plugin_id) {
	int released = Providers_releaseAll(&providers, plugin_id);
	if (released > 0) LOG_info("plugins: %s's services are released (%i)\n", plugin_id, released);
}


///////////////////////////////////////
// screen captures

// Only with plugins loaded, so with none a frame costs what stock's does.
static int capturing(void) {
	return loaded_count > 0 && !settings_only;
}

void Plugins_frameBegin(void) {
	if (capturing()) Capture_frameBegin(currentScreen == SCREEN_MODAL || currentScreen == SCREEN_QUICKMENU);
}

void Plugins_frameShown(void) {
	if (capturing()) Capture_frameShown();
}

// The calling plugin, on the launcher's thread, and not from its init,
// where no frame has been shown, or a hook that draws, whose frame isn't
// done.
static LoadedPlugin* captureCaller(const char* call) {
	LoadedPlugin* p = callingPlugin(call);
	if (!p) return NULL;
	const char* why = NULL;
	if (initializing >= 0) why = "not from init";
	else if (layout_rendering || decorating || backgrounding || generating || screen_drawing || transition_owner >= 0)
		why = "not while drawing";
	if (!why) return p;
	LOG_info("plugins: %s's %s refused: %s\n", p->id, call, why);
	return NULL;
}

static SDL_Surface* captureFrame(LoadedPlugin* p, const char* call, int* owned) {
	const char* why = "";
	SDL_Surface* frame = Capture_frame(owned, &why);
	if (!frame) LOG_info("plugins: %s's %s refused: %s\n", p->id, call, why);
	return frame;
}

static MosaicSurface* hostCaptureScreen(void) {
	LoadedPlugin* p = captureCaller("capture_screen");
	if (!p) return NULL;
	int owned = 0;
	SDL_Surface* frame = captureFrame(p, "capture_screen", &owned);
	if (!frame) return NULL;
	// the plugin's own copy, against its budget
	MosaicSurface* copy = PluginDraw_surfaceNew(p->id, frame->w, frame->h);
	if (copy) SDL_BlitSurface(frame, NULL, (SDL_Surface*)copy, NULL);
	if (owned) SDL_FreeSurface(frame);
	return copy;
}

static int hostSaveScreenshot(char* path, size_t size) {
	LoadedPlugin* p = captureCaller("save_screenshot");
	if (!p) return -1;
	int owned = 0;
	SDL_Surface* frame = captureFrame(p, "save_screenshot", &owned);
	if (!frame) return -1;
	int rc = Capture_save(frame, path, size);
	if (owned) SDL_FreeSurface(frame);
	if (rc == 0) LOG_info("plugins: %s saved a screenshot\n", p->id);
	return rc;
}

///////////////////////////////////////
// button hints

static int hostDrawButton(MosaicSurface* surface, int button, int x, int y, MosaicRect* drawn) {
	// FN1 to FN3 by the device's names, such as HOME, as draw_hints takes them
	const char* fn_names[] = { Plugins_fnName(0), Plugins_fnName(1), Plugins_fnName(2) };
	return PluginDraw_button(surface, Hint_glyph(button, fn_names), x, y, drawn);
}

// Surfaces belong to a loaded plugin, the one whose init is running included.
static int isLoaded(const char* plugin_id) {
	for (int i = 0; plugin_id && i < loaded_count; i++) {
		if (!strcmp(loaded[i].id, plugin_id)) return 1;
	}
	return 0;
}
static MosaicSurface* hostSurfaceNew(const char* plugin_id, int w, int h) {
	return isLoaded(plugin_id) ? PluginDraw_surfaceNew(plugin_id, w, h) : NULL;
}
static MosaicSurface* hostImageLoad(const char* plugin_id, const char* path) {
	return isLoaded(plugin_id) ? PluginDraw_imageLoad(plugin_id, path) : NULL;
}
static MosaicSurface* hostSurfaceScale(const char* plugin_id, const MosaicSurface* src, int w, int h, int fit) {
	return isLoaded(plugin_id) ? PluginDraw_surfaceScale(plugin_id, src, w, h, fit) : NULL;
}

static int hostSurfaceWrite(const char* plugin_id, MosaicSurface* surface, const MosaicRect* rect, const void* pixels, int pitch) {
	return isLoaded(plugin_id) ? PluginDraw_surfaceWrite(plugin_id, surface, rect, pixels, pitch) : -1;
}

// The thumbnail layer is out of date, and with full the list too: a
// thumbnail beside rows that left no room for one, art decorators placed
// under the last art, or a plugin layout that draws it.
static void swapChanged(int full) {
	SDL_LockMutex(thumbMutex);
	thumbchanged = 1;
	SDL_UnlockMutex(thumbMutex);
	if (full) state.dirty = 1;
}

// A freed art_replace surface no longer shows.
static int hostSurfaceFree(const char* plugin_id, MosaicSurface* surface) {
	if (PluginDraw_surfaceFree(plugin_id, surface) != 0) return -1;
	if (ArtSwap_dropSurface(&swap, surface)) swapChanged(1);
	return 0;
}

static int loadedIndex(const char* plugin_id) {
	for (int i = 0; plugin_id && i < loaded_count; i++) {
		if (!strcmp(loaded[i].id, plugin_id)) return i;
	}
	return -1;
}

// 1 while the game list shows the selected entry's art, or would with art
// for it: the built-in list's thumbnail, or a plugin layout's own place.
static int listShowsArt(void) {
	return currentScreen == SCREEN_GAMELIST && state.top && state.top->entries->count > 0 && CFG_getShowGameArt();
}

static int hostArtReplace(const char* plugin_id, MosaicSurface* surface) {
	int owner = loadedIndex(plugin_id);
	if (owner < 0 || initializing >= 0 || layout_rendering || decorating || backgrounding || generating || screen_drawing) return -1;
	if (!surface) {
		// a full redraw: the list may have made room for it, and art decorators placed under it
		if (ArtSwap_set(&swap, owner, NULL, 0, 0) == ARTSWAP_CLEARED) swapChanged(1);
		return 0;
	}
	SDL_Surface* s = (SDL_Surface*)surface;
	if (!DrawReg_mine(surface, plugin_id) || !listShowsArt()) return -1;
	// a move since it was shown took it away, even if Plugins_artReplaceCheck hasn't run yet
	if (ArtSwap_active(&swap) && SelKey_changed(&swap_key, state.top, state.top->entries, state.top->selected))
		ArtSwap_clear(&swap);
	int had = ArtSwap_active(&swap);
	int result = ArtSwap_set(&swap, owner, surface, s->w, s->h);
	if (result == ARTSWAP_REFUSED) return -1;
	if (result == ARTSWAP_SHOWN && !had) {
		SelKey_reset(&swap_key);
		SelKey_changed(&swap_key, state.top, state.top->entries, state.top->selected);
	}
	// rows beside no art make room, art decorators go under a new size, and
	// a layout draws it again
	int redraw = ArtSwap_redraw(result, Plugins_hasLayout(), swap_drawn, had_thumb, Plugins_hasArtDecorators());
	if (redraw != ARTSWAP_REDRAW_NONE) swapChanged(redraw == ARTSWAP_REDRAW_LIST);
	return 0;
}

SDL_Surface* Plugins_artReplaced(void) {
	if (!ArtSwap_active(&swap)) return NULL;
	return DrawReg_mine(swap.surface, loaded[swap.owner].id) ? (SDL_Surface*)swap.surface : NULL;
}

// Only the selected entry has one; Plugins_artReplaceCheck has dropped it
// before the frame if the selection moved, and it can't move while a
// layout draws.
static int hostArtReplacement(int index, const MosaicSurface** out) {
	if (!layout_rendering || decorating || !out) return -1;
	*out = NULL;
	if (!state.top || index < 0 || index >= state.top->entries->count) return -1;
	SDL_Surface* s = index == state.top->selected ? Plugins_artReplaced() : NULL;
	if (!s) return 0;
	*out = (const MosaicSurface*)s;
	swap_drawn = 1;
	return 1;
}

void Plugins_artReplaceCheck(void) {
	if (!ArtSwap_active(&swap)) return;
	int moved = !state.top || SelKey_changed(&swap_key, state.top, state.top->entries, state.top->selected);
	if (!moved && listShowsArt() && DrawReg_mine(swap.surface, loaded[swap.owner].id)) return;
	ArtSwap_clear(&swap);
	swapChanged(1);
}

// Audio_queue and Audio_queued run on plugins' threads too, so they never
// read the loaded plugins; the others are refused off the main thread
// before they do.
static int hostAudioOpen(const char* plugin_id, int rate, int channels) {
	if (!Audio_onMain(plugin_id, "audio_open")) return -1;
	return isLoaded(plugin_id) ? Audio_open(plugin_id, rate, channels) : -1;
}

static int hostArtFind(const MosaicEntry* entry, int kind, char* out, size_t size) {
	return ArtLoader_find((const Entry*)entry, kind, out, size);
}
static int hostArtGet(const MosaicEntry* entry, int kind, const MosaicSurface** out) {
	return ArtLoader_get((const Entry*)entry, kind, out);
}
static int hostDecorateCell(MosaicSurface* screen, int index, const MosaicRect* rect, const MosaicRect* name);
static int hostDecorateArt(MosaicSurface* screen, int index, const MosaicRect* art);

static int hostRegisterScreen(const char* id, const MosaicScreenHooks* hooks);
static int hostOpenScreen(const char* id);
static int hostCloseScreen(const char* id);
static int hostRunAction(const char* action_id, const char* path);
static int hostActionsCount(void);
static int hostActionInfo(int index, MosaicActionInfo* out);
static int hostDrawHints(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn);

static void playLog(const char* message) {
	LOG_info("%s\n", message);
}

// After a plugin's quit, or when its init declines: its surfaces go with it.
static void freeSurfaces(const char* plugin_id) {
	int owner = loadedIndex(plugin_id);
	if (owner >= 0 && ArtSwap_dropOwner(&swap, owner)) swapChanged(1);
	int freed = PluginDraw_freeAll(plugin_id);
	if (freed) LOG_info("plugins: freed %i surface%s %s left\n", freed, freed == 1 ? "" : "s", plugin_id);
}

static const MosaicHost host = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.platform = PLATFORM,
	.sdcard_path = SDCARD_PATH,
	.userdata_path = USERDATA_PATH,
	.log = hostLog,
	.cfg_get = hostCfgGet,
	.cfg_set = hostCfgSet,
	.entry_new = hostEntryNew,
	.entry_path = hostEntryPath,
	.entry_name = hostEntryName,
	.entry_type = hostEntryType,
	.list_new = hostListNew,
	.list_count = hostListCount,
	.list_get = hostListGet,
	.list_push = hostListPush,
	.selected_entry = hostSelectedEntry,
	.current_path = hostCurrentPath,
	.request_redraw = hostRequestRedraw,
	.register_action = hostRegisterAction,
	.bind_button = hostBindButton,
	.toast = hostToast,
	.inhibit_sleep = hostInhibitSleep,
	.confirm = Modal_confirm,
	.keyboard = Modal_keyboard,
	.open_path = hostOpenPath,
	.recents_count = hostRecentsCount,
	.recent_path = hostRecentPath,
	.recent_remove = hostRecentRemove,
	.recents_clear = hostRecentsClear,
	.reload_lists = hostReloadLists,
	.entry_set_name = hostEntrySetName,
	.current_list = hostCurrentList,
	.selected_index = hostSelectedIndex,
	.select_index = hostSelectIndex,
	.quick_tile = hostQuickTile,
	.hide_quick_tile = hostHideQuickTile,
	.choose = Modal_choose,
	.recent_set = hostRecentSet,
	.override_button = hostOverrideButton,
	.replace_launch = hostReplaceLaunch,
	.rename_quick_tile = hostRenameQuickTile,
	.battery_level = hostBatteryLevel,
	.is_charging = hostIsCharging,
	.led_count = hostLedCount,
	.power_off = hostPowerOff,
	.show_recents = hostShowRecents,
	.stock_toggle_get = hostStockToggleGet,
	.stock_toggle_set = hostStockToggleSet,
	.override_repeat = 1,
	.led_set_color = hostLedSetColor,
	.led_clear = hostLedClear,
	.theme_color = hostThemeColor,
	.bt_supported = hostBtSupported,
	.bt_enabled = hostBtEnabled,
	.bt_connected_address = hostBtConnectedAddress,
	.bt_connect = hostBtConnect,
	.screen_width = PluginDraw_screenWidth,
	.screen_height = PluginDraw_screenHeight,
	.screen_scale = PluginDraw_screenScale,
	.surface_new = hostSurfaceNew,
	.image_load = hostImageLoad,
	.surface_scale = hostSurfaceScale,
	.surface_free = hostSurfaceFree,
	.surface_size = PluginDraw_surfaceSize,
	.read_pixel = PluginDraw_readPixel,
	.fill_rect = PluginDraw_fillRect,
	.fill_round_rect = PluginDraw_fillRoundRect,
	.blit = PluginDraw_blit,
	.text_width = PluginDraw_textWidth,
	.text_height = PluginDraw_textHeight,
	.text_truncate = PluginDraw_textTruncate,
	.draw_text = PluginDraw_drawText,
	.art_find = hostArtFind,
	.art_get = hostArtGet,
	.art_load = ArtLoader_load,
	.decorate_cell = hostDecorateCell,
	.selection_settle_ms = SEL_SETTLE_MS,
	.entry_info = hostEntryInfo,
	.blit_alpha = PluginDraw_blitAlpha,
	.transition_max_ms = TRANSITION_MAX_MS,
	.plugin_art = 1,
	.hide_names = 1,
	.play_time = PlayHistory_seconds,
	.last_played = PlayHistory_lastPlayed,
	.play_count = PlayHistory_count,
	.play_time_today = PlayHistory_today,
	.decorate_art = hostDecorateArt,
	.hold_switch = hostHoldSwitch,
	.has_pin = hostHasPin,
	.ask_pin = hostAskPin,
	.achievements = hostAchievements,
	.achievement_hash = hostAchievementHash,
	.surface_write = hostSurfaceWrite,
	.art_replace = hostArtReplace,
	.audio_open = hostAudioOpen,
	.audio_queue = Audio_queue,
	.audio_queued = Audio_queued,
	.audio_pause = Audio_pause,
	.audio_close = Audio_close,
	.audio_queue_max_ms = AUDIO_QUEUE_MAX_MS,
	.art_replacement = hostArtReplacement,
	.register_screen = hostRegisterScreen,
	.open_screen = hostOpenScreen,
	.close_screen = hostCloseScreen,
	.current_screen = Plugins_currentScreen,
	.run_action = hostRunAction,
	.recent_info = hostRecentInfo,
	.recent_open = hostRecentOpen,
	.draw_hints = hostDrawHints,
	.play_games_count = PlayHistory_gameCount,
	.play_game_info = hostPlayGameInfo,
	.play_time_on = PlayHistory_day,
	.wifi_supported = hostWifiSupported,
	.wifi_enabled = hostWifiEnabled,
	.wifi_connection = hostWifiConnection,
	.home_screen = hostHomeScreen,
	.tr = hostTr,
	.tr_plural = hostTrPlural,
	.ui_size = Theme_uiSize,
	.plugins_count = hostPluginsCount,
	.plugin_info = hostPluginInfo,
	.cfg_read = hostCfgRead,
	.fn_name = hostFnName,
	.fn_binding = hostFnBinding,
	.fn_bind = hostFnBind,
	.fn_release = hostFnRelease,
	.capture_screen = hostCaptureScreen,
	.save_screenshot = hostSaveScreenshot,
	.hint_style = Theme_hintStyle,
	.draw_button = hostDrawButton,
	.palettes_count = Palettes_count,
	.palette_info = Palettes_info,
	.palette_selected = Palettes_selected,
	.set_register = hostSetRegister,
	.set_unregister = hostSetUnregister,
	.set_has = hostSetHas,
	.set_list = hostSetList,
	.is_filtered = hostIsFiltered,
	.subscribe = hostSubscribe,
	.unsubscribe = hostUnsubscribe,
	.publish = hostPublish,
	.event_value = hostEventValue,
	.set_changed = hostSetChanged,
	.set_names = hostSetNames,
	.plugin_loaded = hostPluginLoaded,
	.has_capability = hostHasCapability,
	.provide = hostProvide,
	.call = hostCall,
	.actions_count = hostActionsCount,
	.action_info = hostActionInfo,
};

///////////////////////////////////////
// loading

static void load(const PluginManifest* m) {
	if (m->api != MOSAIC_API_VERSION) {
		LOG_warn("plugins: %s targets API %i, this launcher has %i, skipping\n", m->id, m->api, MOSAIC_API_VERSION);
		return;
	}

	char so[600];
	snprintf(so, sizeof(so), "%s/lib/%s/plugin.so", m->dir, PLUGIN_ARCH);
	if (!exists(so)) {
		LOG_warn("plugins: %s has no build for %s (%s), skipping\n", m->id, PLUGIN_ARCH, so);
		return;
	}

	void* handle = dlopen(so, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		LOG_warn("plugins: %s failed to load: %s\n", m->id, dlerror());
		return;
	}
	MosaicPluginGet get = (MosaicPluginGet)dlsym(handle, MOSAIC_PLUGIN_ENTRY);
	const MosaicPlugin* plugin = get ? get(&host) : NULL;
	if (!plugin || plugin->size < offsetof(MosaicPlugin, init) || plugin->api_version != MOSAIC_API_VERSION
			|| !plugin->id || strcmp(plugin->id, m->id) != 0) {
		LOG_warn("plugins: %s doesn't describe itself as plugin %s for API %i, skipping\n", so, m->id, MOSAIC_API_VERSION);
		dlclose(handle);
		return;
	}

	LoadedPlugin* p = &loaded[loaded_count++];
	memset(p, 0, sizeof(*p));
	// so publish from a thread its init starts knows it
	publisherAdd(Callsite_base((const void*)get), m->id);
	p->handle = handle;
	p->plugin = plugin;
	p->manifest = m;
	snprintf(p->id, sizeof(p->id), "%s", m->id);
	char cfg[512];
	snprintf(cfg, sizeof(cfg), PLUGIN_STATE_DIR "/%s.cfg", m->id);
	PluginCfg_init(&p->cfg, cfg);
	p->owns_root = csvHas(m->slots, ROOT_SLOT);
	if (!p->owns_root && Plugin_has(plugin, root_replace) && plugin->root_replace) {
		LOG_warn("plugins: %s has root_replace but no slots=" ROOT_SLOT ", so it won't be asked\n", m->id);
	}
	p->owns_layout = csvHas(m->slots, LAYOUT_SLOT);
	int has_layout = (Plugin_has(plugin, layout_init) && plugin->layout_init)
		|| (Plugin_has(plugin, layout_input) && plugin->layout_input)
		|| (Plugin_has(plugin, layout_render) && plugin->layout_render);
	if (!p->owns_layout && has_layout) {
		LOG_warn("plugins: %s has a layout but no slots=" LAYOUT_SLOT ", so it won't be called\n", m->id);
	}
	p->owns_transition = csvHas(m->slots, TRANSITION_SLOT);
	p->owns_launch = csvHas(m->slots, LAUNCH_SLOT);
	int has_transitions = (Plugin_has(plugin, transition_begin) && plugin->transition_begin)
		|| (Plugin_has(plugin, transition_frame) && plugin->transition_frame);
	if (!p->owns_transition && !p->owns_launch && has_transitions) {
		LOG_warn("plugins: %s has transitions but no slots=" TRANSITION_SLOT " or slots=" LAUNCH_SLOT ", so they won't be called\n", m->id);
	}
	p->owns_background = csvHas(m->slots, BACKGROUND_SLOT);
	if (!p->owns_background && Plugin_has(plugin, background_render) && plugin->background_render) {
		LOG_warn("plugins: %s has background_render but no slots=" BACKGROUND_SLOT ", so it won't be called\n", m->id);
	}
	p->owns_home = csvHas(m->slots, HOME_SLOT);
	p->owns_switcher = csvHas(m->slots, SWITCHER_SLOT);
	p->owns_strings = csvHas(m->slots, STRINGS_SLOT);
	p->owns_theme = csvHas(m->slots, THEME_SLOT);
	if (!p->owns_theme && Plugin_has(plugin, theme) && plugin->theme) {
		LOG_warn("plugins: %s has a theme but no slots=" THEME_SLOT ", so it won't be asked\n", m->id);
	}
	int has_translate = (Plugin_has(plugin, translate) && plugin->translate)
		|| (Plugin_has(plugin, translate_plural) && plugin->translate_plural);
	if (!p->owns_strings && has_translate) {
		LOG_warn("plugins: %s translates but has no slots=" STRINGS_SLOT ", so it won't be asked\n", m->id);
	}
	if (!p->owns_strings && Plugin_has(plugin, fallback_font) && plugin->fallback_font) {
		LOG_warn("plugins: %s has a fallback_font but no slots=" STRINGS_SLOT ", so it won't be asked\n", m->id);
	}
	int has_width = Plugin_has(plugin, header_width) && plugin->header_width;
	int has_widget = Plugin_has(plugin, header_widget) && plugin->header_widget;
	if (has_width != has_widget) {
		LOG_warn("plugins: %s has %s but no %s, so its header widget won't show\n", m->id,
			has_width ? "header_width" : "header_widget", has_width ? "header_widget" : "header_width");
	}

	// what init registers, to take back if it declines: its actions, tiles
	// and bindings point into the code that's about to be unloaded
	const Action* actions[ACTIONS_MAX];
	int action_count = Actions_list(actions, ACTIONS_MAX);
	QuickTilesSaved* tiles = QuickTiles_save();
	int bindings[SCREEN_COUNT];
	saveBindings(bindings);
	int rebuild_quick = state.rebuild_quick;
	int power_off = state.power_off;

	initializing = loaded_count - 1;
	int declined = 0;
	if (Plugin_has(plugin, init) && plugin->init) AS_PLUGIN(initializing) declined = plugin->init(&host) != 0;
	initializing = -1;
	if (declined) {
		LOG_warn("plugins: %s declined to start\n", m->id);
		dropOverrides(loaded_count - 1);
		dropBindings(bindings);
		QuickTiles_restore(tiles);
		const Action* now[ACTIONS_MAX];
		int now_count = Actions_list(now, ACTIONS_MAX);
		for (int i = 0; i < now_count && i < ACTIONS_MAX; i++) {
			int kept = 0;
			for (int j = 0; j < action_count && !kept; j++) kept = now[i] == actions[j];
			if (kept) continue;
			// registered by this init, so made by hostRegisterAction
			Action* action = (Action*)now[i];
			free((char*)action->id);
			free((char*)action->label);
			free((char*)action->plugin_id);
			free(action);
		}
		Actions_restore(actions, action_count);
		state.rebuild_quick = rebuild_quick;
		state.power_off = power_off;
		hostInhibitSleep(m->id, 0);
		hostLedClear(m->id);
		// none can be open: open_screen refuses from init
		ScreenSet_dropOwner(&screens, loaded_count - 1);
		fnReleaseAll(m->id);
		setsReleaseAll(m->id);
		eventsReleaseAll(m->id);
		providersReleaseAll(m->id);
		freeSurfaces(m->id);
		Audio_closeFor(m->id, "as it declined to start");
		PluginCfg_free(&p->cfg);
		dlclose(handle);
		loaded_count--;
		return;
	}
	QuickTiles_discard(tiles);
	LOG_info("plugins: loaded %s\n", m->id);
}

static void logOverridden(void);

// The layout slot's owner, once every plugin has loaded: slot resolution
// lets only one plugin claim it.
static void claimLayout(void) {
	for (int i = 0; i < loaded_count; i++) {
		if (!loaded[i].owns_layout) continue;
		const MosaicPlugin* p = loaded[i].plugin;
		int has_render = Plugin_has(p, layout_render) && p->layout_render;
		LayoutSlot_claim(&layout, i, has_render);
		if (has_render) LOG_info("plugins: %s draws the game list\n", loaded[i].id);
		else LOG_warn("plugins: %s owns the " LAYOUT_SLOT " slot but has no layout_render, using the built-in list\n", loaded[i].id);
		return;
	}
}

// The transition and launch slots' owners, once every plugin has loaded.
static void claimTransitions(void) {
	TransitionSlots_clear(&transitions);
	for (int i = 0; i < loaded_count; i++) {
		const MosaicPlugin* p = loaded[i].plugin;
		int has_begin = Plugin_has(p, transition_begin) && p->transition_begin;
		if (loaded[i].owns_transition && transitions.transition < 0) {
			if (has_begin) {
				transitions.transition = i;
				LOG_info("plugins: %s picks the " TRANSITION_SLOT " styles\n", loaded[i].id);
			}
			else LOG_warn("plugins: %s owns the " TRANSITION_SLOT " slot but has no transition_begin, using stock's\n", loaded[i].id);
		}
		if (loaded[i].owns_launch && transitions.launch < 0) {
			if (has_begin) {
				transitions.launch = i;
				LOG_info("plugins: %s picks the " LAUNCH_SLOT " style\n", loaded[i].id);
			}
			else LOG_warn("plugins: %s owns the " LAUNCH_SLOT " slot but has no transition_begin, using stock's\n", loaded[i].id);
		}
	}
}

// The background slot's owner, once every plugin has loaded.
static void claimBackground(void) {
	BackgroundSlot_clear(&background);
	for (int i = 0; i < loaded_count; i++) {
		if (!loaded[i].owns_background) continue;
		const MosaicPlugin* p = loaded[i].plugin;
		if (Plugin_has(p, background_render) && p->background_render) {
			BackgroundSlot_claim(&background, i);
			LOG_info("plugins: %s draws the background\n", loaded[i].id);
		}
		else LOG_warn("plugins: %s owns the " BACKGROUND_SLOT " slot but has no background_render, using stock's\n", loaded[i].id);
		return;
	}
}

static void trLog(const char* message) {
	LOG_warn("%s\n", message);
}

// The strings slot's owner, or -1, for its fallback font.
static int strings_owner = -1;

// The strings slot's owner, once every plugin has loaded, so every
// plugin's init sees English.
static void claimStrings(void) {
	Tr_setOwner(NULL);
	strings_owner = -1;
	for (int i = 0; i < loaded_count; i++) {
		if (!loaded[i].owns_strings) continue;
		const MosaicPlugin* p = loaded[i].plugin;
		strings_owner = i;
		if (Plugin_has(p, translate) && p->translate) {
			TrOwner owner = {
				.translate = p->translate,
				.translate_plural = Plugin_has(p, translate_plural) ? p->translate_plural : NULL,
				.id = loaded[i].id,
				.log = trLog,
			};
			Tr_setOwner(&owner);
			LOG_info("plugins: %s translates the launcher's strings\n", loaded[i].id);
		}
		else LOG_warn("plugins: %s owns the " STRINGS_SLOT " slot but has no translate, using English\n", loaded[i].id);
		return;
	}
}

// The theme slot's owner, or -1, and whether it has failed for the run.
static int theme_owner = -1;
static int theme_failed = 0;
static MosaicTheme theme_last; // the owner's theme as last put in place, for launcher.theme_changed
static int theme_known = 0;

// The theme slot's owner, once every plugin has loaded and before the
// first list is built.
static void claimTheme(void) {
	theme_owner = -1;
	theme_failed = 0;
	theme_known = 0;
	for (int i = 0; i < loaded_count; i++) {
		if (!loaded[i].owns_theme) continue;
		const MosaicPlugin* p = loaded[i].plugin;
		if (Plugin_has(p, theme) && p->theme) theme_owner = i;
		else LOG_warn("plugins: %s owns the " THEME_SLOT " slot but has no theme, using stock's\n", loaded[i].id);
		break;
	}
	Plugins_themeRefresh();
}

// The fallback font: the theme's, else the strings slot's owner's, else
// none.
static void fallbackRefresh(void) {
	char path[512] = "";
	char from[300] = "";
	if (theme_owner >= 0 && !theme_failed && Theme_fallbackFont()[0]) {
		snprintf(path, sizeof(path), "%s", Theme_fallbackFont());
		snprintf(from, sizeof(from), "%s's theme", loaded[theme_owner].id);
	}
	else if (strings_owner >= 0) {
		LoadedPlugin* owner = &loaded[strings_owner];
		if (Plugin_has(owner->plugin, fallback_font) && owner->plugin->fallback_font) {
			const char* given = NULL;
			AS_CALLER(owner->id) given = owner->plugin->fallback_font();
			if (given && given[0] && Fallback_check(owner->id, "fallback font", given, path, sizeof(path)) == 0)
				snprintf(from, sizeof(from), "%s", owner->id);
		}
	}
	if (strcmp(path, Fallback_path()) != 0) state.dirty = 1;
	Fallback_set(path, from);
}

int Plugins_themeRefresh(void) {
	if (theme_owner < 0 || theme_failed) {
		fallbackRefresh();
		return 0;
	}
	LoadedPlugin* owner = &loaded[theme_owner];
	MosaicTheme theme;
	memset(&theme, 0, sizeof(theme)); // so two asks compare alike
	Theme_blank(&theme);
	int result = 0;
	AS_CALLER(owner->id) result = owner->plugin->theme(&theme);
	if (result != 0) {
		LOG_warn("plugins: %s's theme failed (%d), using stock's until the launcher restarts\n", owner->id, result);
		theme_failed = 1;
		Theme_clear();
		if (theme_known) hostEvent(EVENT_HOST_PREFIX "theme_changed", "");
	}
	else {
		Theme_set(owner->id, &theme);
		// the first, as the launcher starts, isn't a change
		if (theme_known && memcmp(&theme, &theme_last, sizeof(theme)) != 0) hostEvent(EVENT_HOST_PREFIX "theme_changed", "");
		theme_last = theme;
		theme_known = 1;
	}
	fallbackRefresh();
	state.dirty = 1;
	return 1;
}

// The home slot's screen, once every plugin has loaded. The flag on any
// other plugin's screen means nothing.
static void claimHome(void) {
	int owner = -1;
	for (int i = 0; i < loaded_count && owner < 0; i++) {
		if (loaded[i].owns_home) owner = i;
	}
	for (int i = 0; i < screens.count; i++) {
		const PluginScreen* s = &screens.screens[i];
		if (s->owner != owner && (s->hooks.flags & MOSAIC_SCREEN_HOME)) {
			LOG_warn("plugins: %s's screen %s is marked home but %s has no slots=" HOME_SLOT ", so it's an ordinary screen\n",
				loaded[s->owner].id, s->id, loaded[s->owner].id);
		}
	}
	if (owner < 0) return;
	int home = ScreenSet_claimHome(&screens, owner);
	if (home >= 0) LOG_info("plugins: %s's screen %s is the home screen\n", loaded[owner].id, screens.screens[home].id);
	else LOG_warn("plugins: %s owns the " HOME_SLOT " slot but registered no MOSAIC_SCREEN_HOME screen, using the root list\n", loaded[owner].id);
}

// The switcher slot's screen, once every plugin has loaded and home is
// claimed. The flag on any other plugin's screen means nothing.
static void claimSwitcher(void) {
	int owner = -1;
	for (int i = 0; i < loaded_count && owner < 0; i++) {
		if (loaded[i].owns_switcher) owner = i;
	}
	for (int i = 0; i < screens.count; i++) {
		const PluginScreen* s = &screens.screens[i];
		if (s->owner != owner && (s->hooks.flags & MOSAIC_SCREEN_GAME_SWITCHER)) {
			LOG_warn("plugins: %s's screen %s is marked switcher but %s has no slots=" SWITCHER_SLOT ", so it's an ordinary screen\n",
				loaded[s->owner].id, s->id, loaded[s->owner].id);
		}
	}
	if (owner < 0) return;
	int switcher = ScreenSet_claimSwitcher(&screens, owner);
	if (switcher >= 0) LOG_info("plugins: %s's screen %s is the game switcher\n", loaded[owner].id, screens.screens[switcher].id);
	else LOG_warn("plugins: %s owns the " SWITCHER_SLOT " slot but registered no MOSAIC_SCREEN_GAME_SWITCHER screen other than its home screen, using the built-in switcher\n", loaded[owner].id);
}

// Any exit but a crash: the settings closed, or powered off from there.
static void settingsExit(void) {
	if (settings_only) unlink(SETTINGS_MARKER);
}

// The crash guard for the settings: 1 to load, or 0 to open them with
// English and stock's theme. Their owners are asked as the pages draw, so
// the marker stays for as long as they're open, and a crash with it there
// leaves them out of the next open, so a crashing owner can't keep the
// user from the page that turns it off. The launcher's own guard is left
// to the launcher: plugins it paused, or is about to pause, aren't loaded
// here either.
static int settingsGuard(void) {
	if (exists(PAUSED_MARKER)) {
		LOG_warn("plugins: paused after a crash, so the settings load none\n");
		return 0;
	}
	if (exists(LOADING_MARKER)) {
		LOG_warn("plugins: the launcher crashed while starting with plugins, so the settings load none\n");
		return 0;
	}
	if (exists(SETTINGS_MARKER)) {
		LOG_warn("plugins: the settings crashed with plugins loaded, so they load none this time\n");
		unlink(SETTINGS_MARKER);
		return 0;
	}
	return 1;
}

static void start(void) {
	Audio_setMainThread();
	ScreenSet_clear(&screens);
	if (settings_only) {
		if (!settingsGuard()) return;
	}
	else if (exists(PAUSED_MARKER)) {
		LOG_warn("plugins: paused after a crash; turn them back on from the Launcher pak\n");
		return;
	}
	else if (exists(LOADING_MARKER)) {
		// The last start died before its first frame with plugins loaded.
		LOG_warn("plugins: the launcher crashed while starting with plugins, pausing them\n");
		unlink(LOADING_MARKER);
		Plugins_ensureStateDir();
		putFile(PAUSED_MARKER, "");
		return;
	}

	discover();
	if (manifest_count == 0) return;
	if (!settings_only) Plugins_adoptPin(); // before simple mode's cfg is read; the settings adopt it themselves
	Bluetooth_setLog(bluetoothLog);
	Wifi_setLog(wifiLog);

	char* choices = readText(PLUGIN_CHOICES_PATH);
	static PluginDecision decisions[PLUGIN_MAX];
	int n = PluginSet_resolve(manifests, manifest_count, choices, decisions);
	free(choices);
	const char* marker = LOADING_MARKER;
	if (settings_only) {
		// what they require isn't loaded: plugins can't call each other's code,
		// and the settings run none of the hooks that would need it
		if (PluginSet_onlySlots(decisions, n, STRINGS_SLOT "," THEME_SLOT,
				"the settings load only the " STRINGS_SLOT " and " THEME_SLOT " slots' owners") == 0) {
			return;
		}
		marker = SETTINGS_MARKER;
		atexit(settingsExit);
	}

	Plugins_ensureStateDir();
	putFile((char*)marker, "");
	sync();
	// only where to look: nothing is read until a plugin asks
	PlayHistory_configure(SHARED_USERDATA_PATH "/game_logs.sqlite", ROMS_PATH, playLog);
	RAProgress_configure(SDCARD_PATH, ROMS_PATH, SHARED_USERDATA_PATH "/.ra", SHARED_USERDATA_PATH "/launcher/ra-hashes.txt",
		playLog);

	for (int i = 0; i < n; i++) {
		const char* before = PluginSet_orderBroken(decisions, n, i);
		if (before) {
			LOG_warn("plugins: %s loads before %s, which its optional= names, as they name each other\n",
				decisions[i].manifest->id, before);
		}
		if (decisions[i].load) load(decisions[i].manifest);
		else LOG_info("plugins: not loading %s: %s\n", decisions[i].manifest->id, decisions[i].reason);
	}
	if (loaded_count == 0) unlink(marker);
	listed = 1;
	logOverridden();
	claimLayout();
	claimTransitions();
	claimBackground();
	claimHome();
	claimSwitcher();
	claimStrings();
	claimTheme();

	// post_return is the launcher's, when it starts after the pak
	if (!settings_only && exists(LAUNCHED_MARKER)) {
		char* launched = readText(LAUNCHED_MARKER);
		unlink(LAUNCHED_MARKER);
		// a game or pak ran, so the play history may have moved on
		PlayHistory_invalidate();
		for (int i = 0; i < loaded_count; i++) {
			const MosaicPlugin* p = loaded[i].plugin;
			if (Plugin_has(p, post_return) && p->post_return) AS_PLUGIN(i) p->post_return();
		}
		// the launch's own payload, or none from an older or hand-made marker
		hostEvent(EVENT_HOST_PREFIX "return", launched && Events_payloadValid(launched) ? launched : "");
		free(launched);
	}
}

void Plugins_init(void) {
	start();
}

void Plugins_initSettings(void) {
	settings_only = 1;
	start();
}

void Plugins_started(void) {
	if (exists(LOADING_MARKER)) unlink(LOADING_MARKER);
}

void Plugins_quit(void) {
	// open screens close first, as a launch leaves them
	Plugins_screensClose();
	// what's still waiting, launcher.launch too, before plugins' quit hooks
	eventsDeliver();
	ScreenSet_clear(&screens);
	LayoutSlot_clear(&layout);
	TransitionSlots_clear(&transitions);
	BackgroundSlot_clear(&background);
	// English from here on, as the owner's strings go with it
	Tr_setOwner(NULL);
	// before plugins' quit, which stop their threads: one still queueing meanwhile is refused
	Audio_hostClose("at quit", 0);
	for (int i = loaded_count - 1; i >= 0; i--) {
		LoadedPlugin* p = &loaded[i];
		if (Plugin_has(p->plugin, quit) && p->plugin->quit) AS_PLUGIN(i) p->plugin->quit();
		// turning a plugin off, or a launch, gives the user's colour back
		hostLedClear(p->id);
		fnReleaseAll(p->id);
		setsReleaseAll(p->id);
		eventsReleaseAll(p->id);
		providersReleaseAll(p->id);
		freeSurfaces(p->id);
		PluginCfg_free(&p->cfg);
		dlclose(p->handle);
	}
	loaded_count = 0;
	listed = 0;
	EntrySets_free(&sets);
	Providers_init(&providers);
	// what quit hooks published goes with them
	EventQueue_init(&event_queue);
	Capture_quit();
	PlayHistory_quit();
	RAProgress_quit();
	// a refresh or connect still running is stopped and reaped first
	Bluetooth_quit();
	// a Wi-Fi look still running is left to finish, its answer dropped
	Wifi_quit();
	settingsExit();
}

int Plugins_discover(const PluginManifest** out) {
	discover();
	*out = manifests;
	return manifest_count;
}

int Plugins_count(void) {
	return loaded_count;
}

///////////////////////////////////////
// button bindings

static Binding pluginBindings[SCREEN_COUNT][PLUGIN_BINDING_MAX];
static int pluginBindingOwner[SCREEN_COUNT][PLUGIN_BINDING_MAX]; // index in loaded, or -1
static int pluginBindingCount[SCREEN_COUNT];
static Binding overrideBindings[SCREEN_COUNT][PLUGIN_OVERRIDE_MAX];
static int overrideOwner[SCREEN_COUNT][PLUGIN_OVERRIDE_MAX]; // index in loaded
static int overrideCount[SCREEN_COUNT];

typedef struct ButtonInfo {
	int nui;
	int btn;
	const char* glyph;
} ButtonInfo;

static const ButtonInfo buttons[] = {
	{ MOSAIC_BUTTON_A, BTN_A, "A" },
	{ MOSAIC_BUTTON_B, BTN_B, "B" },
	{ MOSAIC_BUTTON_X, BTN_X, "X" },
	{ MOSAIC_BUTTON_Y, BTN_Y, "Y" },
	{ MOSAIC_BUTTON_L1, BTN_L1, "L1" },
	{ MOSAIC_BUTTON_R1, BTN_R1, "R1" },
	{ MOSAIC_BUTTON_L2, BTN_L2, "L2" },
	{ MOSAIC_BUTTON_R2, BTN_R2, "R2" },
	{ MOSAIC_BUTTON_START, BTN_START, "START" },
	{ MOSAIC_BUTTON_SELECT, BTN_SELECT, "SELECT" },
};

// Every button a layout sees, the d-pad too, which bindings can't take.
static const LayoutButton layoutButtons[] = {
	{ MOSAIC_BUTTON_A, BTN_A },
	{ MOSAIC_BUTTON_B, BTN_B },
	{ MOSAIC_BUTTON_X, BTN_X },
	{ MOSAIC_BUTTON_Y, BTN_Y },
	{ MOSAIC_BUTTON_L1, BTN_L1 },
	{ MOSAIC_BUTTON_R1, BTN_R1 },
	{ MOSAIC_BUTTON_L2, BTN_L2 },
	{ MOSAIC_BUTTON_R2, BTN_R2 },
	{ MOSAIC_BUTTON_START, BTN_START },
	{ MOSAIC_BUTTON_SELECT, BTN_SELECT },
	{ MOSAIC_BUTTON_UP, BTN_UP },
	{ MOSAIC_BUTTON_DOWN, BTN_DOWN },
	{ MOSAIC_BUTTON_LEFT, BTN_LEFT },
	{ MOSAIC_BUTTON_RIGHT, BTN_RIGHT },
};
#define LAYOUT_BUTTONS (int)(sizeof(layoutButtons) / sizeof(layoutButtons[0]))

static const ButtonInfo* buttonInfo(int button) {
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (buttons[i].nui == button) return &buttons[i];
	}
	return NULL;
}

int Plugins_bindButton(int screen, int button, int trigger, const char* action_id) {
	// only the game list dispatches through bindings so far
	if (screen != MOSAIC_SCREEN_GAMELIST) return -1;
	if (trigger != MOSAIC_TRIGGER_PRESS && trigger != MOSAIC_TRIGGER_RELEASE) return -1;
	const Action* action = Actions_find(action_id);
	if (!action) {
		LOG_warn("plugins: can't bind unknown action %s\n", action_id ? action_id : "(null)");
		return -1;
	}
	const ButtonInfo* info = buttonInfo(button);
	if (!info || pluginBindingCount[screen] >= PLUGIN_BINDING_MAX) return -1;

	const char* caller = Caller_current();
	LoadedPlugin* owner = caller ? loadedById(caller) : NULL;
	pluginBindingOwner[screen][pluginBindingCount[screen]] = owner ? (int)(owner - loaded) : -1;
	Binding* b = &pluginBindings[screen][pluginBindingCount[screen]++];
	b->button = info->btn;
	b->trigger = trigger == MOSAIC_TRIGGER_RELEASE ? TRIGGER_RELEASE : TRIGGER_PRESS;
	b->glyph = info->glyph;
	b->action = action;

	int sharing = 0;
	for (int i = 0; i < pluginBindingCount[screen]; i++) {
		const Binding* other = &pluginBindings[screen][i];
		if (other->button == b->button && other->trigger == b->trigger && other->action != action) sharing++;
	}
	if (sharing) LOG_info("plugins: %s on the game list is shared by %i actions; with more than one available it opens a menu\n", info->glyph, sharing + 1);
	return 0;
}

// bind_button only appends, so a declined init's bindings are the ones past
// the counts from before it ran.
static void saveBindings(int* counts) {
	for (int screen = 0; screen < SCREEN_COUNT; screen++) counts[screen] = pluginBindingCount[screen];
}
static void dropBindings(const int* counts) {
	for (int screen = 0; screen < SCREEN_COUNT; screen++) pluginBindingCount[screen] = counts[screen];
}

int Plugins_bindings(int screen, const Binding** bindings) {
	if (screen < 0 || screen >= SCREEN_COUNT) return 0;
	*bindings = pluginBindings[screen];
	return pluginBindingCount[screen];
}

// Overrides are made from init, so the loader knows which plugin made each
// and can settle two plugins wanting the same button: the first in load
// order keeps it.
static int hostOverrideButton(int screen, int button, int trigger, const char* action_id) {
	if (initializing < 0) {
		LOG_warn("plugins: override_button only works from init\n");
		return -1;
	}
	const char* id = loaded[initializing].id;
	if (screen != MOSAIC_SCREEN_GAMELIST || (trigger != MOSAIC_TRIGGER_PRESS && trigger != MOSAIC_TRIGGER_RELEASE
			&& trigger != MOSAIC_TRIGGER_REPEAT)) return -1;
	const ButtonInfo* info = buttonInfo(button);
	if (!info) return -1;
	const Action* action = NULL;
	if (action_id) {
		action = Actions_find(action_id);
		if (!action) {
			LOG_warn("plugins: %s can't override %s with unknown action %s\n", id, info->glyph, action_id);
			return -1;
		}
	}
	int btn_trigger = trigger == MOSAIC_TRIGGER_RELEASE ? TRIGGER_RELEASE
		: trigger == MOSAIC_TRIGGER_REPEAT ? TRIGGER_REPEAT : TRIGGER_PRESS;
	for (int i = 0; i < overrideCount[screen]; i++) {
		const Binding* other = &overrideBindings[screen][i];
		if (other->button != info->btn) continue;
		int owner = overrideOwner[screen][i];
		if (owner != initializing) {
			LOG_warn("plugins: %s can't override %s on the game list, %s already has it (the first in load order wins)\n",
				id, info->glyph, loaded[owner].id);
			return -1;
		}
		if (other->trigger == btn_trigger && other->action == action) return 0; // already there
	}
	if (overrideCount[screen] >= PLUGIN_OVERRIDE_MAX) return -1;

	int at = overrideCount[screen]++;
	Binding* b = &overrideBindings[screen][at];
	memset(b, 0, sizeof(*b));
	b->button = info->btn;
	b->trigger = btn_trigger;
	b->glyph = info->glyph;
	b->action = action;
	overrideOwner[screen][at] = initializing;
	LOG_info("plugins: %s overrides %s %s on the game list with %s\n", id, info->glyph,
		trigger == MOSAIC_TRIGGER_RELEASE ? "release" : trigger == MOSAIC_TRIGGER_REPEAT ? "repeat" : "press",
		action ? action->id : "nothing");
	return 0;
}

// A plugin that declined to start takes its overrides with it.
static void dropOverrides(int owner) {
	for (int screen = 0; screen < SCREEN_COUNT; screen++) {
		int kept = 0;
		for (int i = 0; i < overrideCount[screen]; i++) {
			if (overrideOwner[screen][i] == owner) continue;
			overrideBindings[screen][kept] = overrideBindings[screen][i];
			overrideOwner[screen][kept] = overrideOwner[screen][i];
			kept++;
		}
		overrideCount[screen] = kept;
	}
}

// Plugin bindings an override took the button from never run; say so once.
static void logOverridden(void) {
	for (int screen = 0; screen < SCREEN_COUNT; screen++) {
		for (int i = 0; i < pluginBindingCount[screen]; i++) {
			const Binding* b = &pluginBindings[screen][i];
			for (int j = 0; j < overrideCount[screen]; j++) {
				if (overrideBindings[screen][j].button != b->button) continue;
				LOG_info("plugins: %s overrides %s, so %s bound to it doesn't run\n",
					loaded[overrideOwner[screen][j]].id, b->glyph, b->action->id);
				break;
			}
		}
	}
}

int Plugins_overrides(int screen, const Binding** bindings) {
	if (screen < 0 || screen >= SCREEN_COUNT) return 0;
	*bindings = overrideBindings[screen];
	return overrideCount[screen];
}

///////////////////////////////////////
// listing actions

#define OWN_BINDINGS_MAX 16
#define ACTION_BINDS_MAX (OWN_BINDINGS_MAX + SCREEN_COUNT * (PLUGIN_OVERRIDE_MAX + PLUGIN_BINDING_MAX) + FN_COUNT)

// A BTN_* as an enum MosaicButton, MENU too, or -1.
static int mosaicButton(int btn) {
	if (btn == BTN_MENU) return MOSAIC_BUTTON_MENU;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (buttons[i].btn == btn) return buttons[i].nui;
	}
	return -1;
}

// A plugin's bind, with the setting it follows when its plugin.txt marks one.
static ActionBind pluginBind(const Binding* b, int by, int screen, int owner) {
	const char* plugin_id = owner >= 0 && owner < loaded_count ? loaded[owner].id : NULL;
	const char* action_id = b->action ? b->action->id : NULL;
	return (ActionBind){
		action_id, by, screen, mosaicButton(b->button), b->trigger, plugin_id,
		plugin_id ? Manifest_buttonSetting(loaded[owner].manifest, action_id) : NULL,
	};
}

// Every button an action is on, in effect: the game list's own, then
// overrides, bind_button's and the FN buttons'.
static int actionBinds(ActionBind* out) {
	int n = 0;
	const Binding* own[OWN_BINDINGS_MAX];
	int own_count = GameList_ownBindings(own, OWN_BINDINGS_MAX);
	if (own_count > OWN_BINDINGS_MAX) own_count = OWN_BINDINGS_MAX;
	for (int i = 0; i < own_count; i++) {
		out[n++] = (ActionBind){ own[i]->action->id, MOSAIC_BIND_SCREEN, MOSAIC_SCREEN_GAMELIST,
			mosaicButton(own[i]->button), own[i]->trigger, NULL, NULL };
	}
	for (int screen = 0; screen < SCREEN_COUNT; screen++) {
		for (int i = 0; i < overrideCount[screen]; i++) {
			// a NULL action leaves the button doing nothing, which still takes it
			out[n++] = pluginBind(&overrideBindings[screen][i], MOSAIC_BIND_OVERRIDE, screen, overrideOwner[screen][i]);
		}
	}
	for (int screen = 0; screen < SCREEN_COUNT; screen++) {
		for (int i = 0; i < pluginBindingCount[screen]; i++) {
			out[n++] = pluginBind(&pluginBindings[screen][i], MOSAIC_BIND_BUTTON, screen, pluginBindingOwner[screen][i]);
		}
	}
	n = ActionList_dropOverridden(out, n);
	size_t prefix = strlen(FN_ACTION_ACTION_PREFIX);
	for (int index = 0; index < FN_COUNT; index++) {
		if (!fnPresent(index)) continue;
		const char* owner = NULL;
		const char* action = FnBinds_effective(&fn_binds, index, fnUser(index), &owner);
		if (strncmp(action, FN_ACTION_ACTION_PREFIX, prefix) != 0) continue;
		LoadedPlugin* p = owner ? loadedById(owner) : NULL;
		out[n++] = (ActionBind){ action + prefix, owner ? MOSAIC_BIND_FN_PLUGIN : MOSAIC_BIND_FN_USER, -1,
			MOSAIC_BUTTON_FN1 + index, MOSAIC_TRIGGER_PRESS, p ? p->id : NULL,
			p ? Manifest_buttonSetting(p->manifest, action + prefix) : NULL };
	}
	return n;
}

static int hostActionsCount(void) {
	if (!callingPlugin("actions_count")) return 0;
	const Action* all[ACTIONS_MAX];
	int count = Actions_list(all, ACTIONS_MAX);
	return count < ACTIONS_MAX ? count : ACTIONS_MAX;
}

static int hostActionInfo(int index, MosaicActionInfo* out) {
	if (!callingPlugin("action_info") || !out || out->size < sizeof(out->size)) return -1;
	const Action* all[ACTIONS_MAX];
	int count = Actions_list(all, ACTIONS_MAX);
	if (index < 0 || index >= count || index >= ACTIONS_MAX) return -1;
	const Action* action = all[index];
	static ActionBind binds[ACTION_BINDS_MAX];
	int bind_count = actionBinds(binds);
	MosaicActionInfo full;
	ActionList_fill(&full, action->id, Action_label(action), action->plugin_id, action->takes_path, binds, bind_count);
	return ActionList_copy(&full, out);
}

///////////////////////////////////////
// hooks

// Each loaded plugin with the hook, run as that plugin (see caller.h).
#define EACH_PLUGIN_WITH(field) \
	for (int i_ = 0; i_ < loaded_count; i_++) \
		for (const MosaicPlugin* p = loaded[i_].plugin; p && Plugin_has(p, field) && p->field; p = NULL) \
			AS_PLUGIN(i_)

void Plugins_rootEntries(Array* root) {
	EACH_PLUGIN_WITH(root_entries) p->root_entries((MosaicList*)root);
}

Array* Plugins_rootReplace(int* with_entries) {
	*with_entries = 0;
	for (int i = 0; i < loaded_count; i++) {
		const MosaicPlugin* p = loaded[i].plugin;
		if (!loaded[i].owns_root) continue;
		// slot resolution lets only one plugin claim it
		if (!Plugin_has(p, root_replace) || !p->root_replace) return NULL;
		Array* root = NULL;
		AS_PLUGIN(i) root = (Array*)p->root_replace();
		if (root && Plugin_has(p, root_with_entries)) *with_entries = p->root_with_entries;
		return root;
	}
	return NULL;
}

Array* Plugins_virtualDir(const char* path) {
	EACH_PLUGIN_WITH(virtual_dir) {
		MosaicList* list = p->virtual_dir(path);
		if (list) return (Array*)list;
	}
	return NULL;
}

Array* Plugins_filter(Array* entries, const char* dir_path) {
	int any = 0;
	EACH_PLUGIN_WITH(filter) any = 1;
	if (!any) return entries;

	Array* kept = Array_new();
	for (int i = 0; i < entries->count; i++) {
		Entry* entry = entries->items[i];
		int keep = 1;
		EACH_PLUGIN_WITH(filter) {
			if (keep && !p->filter((const MosaicEntry*)entry, dir_path)) keep = 0;
		}
		if (keep) Array_push(kept, entry);
		else Entry_free(entry);
	}
	Array_free(entries);
	return kept;
}

// While is_filtered asks the filters, which can't ask it again.
static int filtering_path = 0;

static int hostIsFiltered(const char* path) {
	if (!callingPlugin("is_filtered") || !path || !path[0]) return 0;
	if (filtering_path) {
		static int logged = 0;
		if (!logged) LOG_warn("plugins: is_filtered refused: it was called from a filter\n");
		logged = 1;
		return 0;
	}
	struct stat st;
	int type = ENTRY_ROM;
	if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) type = suffixMatch(".pak", path) ? ENTRY_PAK : ENTRY_DIR;
	char dir[MAX_PATH * 2];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	if (slash && slash != dir) *slash = '\0';
	else if (slash) slash[1] = '\0';
	Entry* entry = Entry_new((char*)path, type);
	int hidden = 0;
	filtering_path = 1;
	EACH_PLUGIN_WITH(filter) {
		if (!hidden && !p->filter((const MosaicEntry*)entry, dir)) hidden = 1;
	}
	filtering_path = 0;
	Entry_free(entry);
	return hidden;
}

int Plugins_displayNames(Array* entries) {
	int renamed = 0;
	EACH_PLUGIN_WITH(display_name) {
		for (int i = 0; i < entries->count; i++) {
			Entry* entry = entries->items[i];
			char name[256];
			name[0] = '\0';
			if (p->display_name((const MosaicEntry*)entry, name, sizeof(name)) && name[0]) {
				free(entry->name);
				entry->name = strdup(name);
				renamed = 1;
			}
		}
	}
	return renamed;
}

// The plugins with a sort_compare, in load order, while a sort runs. Each
// is asked as itself, so the chain is one compare over all of them.
static int sorters[PLUGIN_MAX];
static int sorter_count = 0;

static int pluginsCompare(const MosaicEntry* a, const MosaicEntry* b, const char* dir_path) {
	for (int k = 0; k < sorter_count; k++) {
		int order = 0;
		AS_PLUGIN(sorters[k]) order = loaded[sorters[k]].plugin->sort_compare(a, b, dir_path);
		if (order) return order;
	}
	return 0;
}

void Plugins_sort(Array* entries, const char* dir_path) {
	sorter_count = 0;
	for (int i = 0; i < loaded_count; i++) {
		const MosaicPlugin* p = loaded[i].plugin;
		if (Plugin_has(p, sort_compare) && p->sort_compare) sorters[sorter_count++] = i;
	}
	ListSortCompare compare = pluginsCompare;
	if (sorter_count > 0) ListSort_chain(entries, &compare, 1, dir_path);
}

int Plugins_listReady(Array* entries, const char* dir_path) {
	// a hook that lists another folder (it shouldn't) gets its own flags
	int was_listing = listing;
	int was_renamed = list_renamed;
	listing = 1;
	list_renamed = 0;
	EACH_PLUGIN_WITH(list_ready) p->list_ready((const MosaicList*)entries, dir_path);
	int renamed = list_renamed;
	listing = was_listing;
	list_renamed = was_renamed;
	return renamed;
}

// The plugins with a hide_name, like sorters.
static int hiders[PLUGIN_MAX];
static int hider_count = 0;

static int pluginsHide(const MosaicEntry* entry, const char* dir_path) {
	for (int k = 0; k < hider_count; k++) {
		int hide = 0;
		AS_PLUGIN(hiders[k]) hide = loaded[hiders[k]].plugin->hide_name(entry, dir_path);
		if (hide) return 1;
	}
	return 0;
}

unsigned char* Plugins_hideNames(Array* entries, const char* dir_path) {
	hider_count = 0;
	for (int i = 0; i < loaded_count; i++) {
		const MosaicPlugin* p = loaded[i].plugin;
		if (Plugin_has(p, hide_name) && p->hide_name) hiders[hider_count++] = i;
	}
	ListHideName hide = pluginsHide;
	return ListHide_chain(entries, &hide, hider_count > 0 ? 1 : 0, dir_path);
}

// The replacement for the launch in progress: the one pre_launch kept, and
// the one the running pre_launch asked for, kept if it returns REPLACE.
static LaunchCmd replacement;
static int replacer = -1;
static LaunchCmd pending;
static int pending_set = 0;

static int hostReplaceLaunch(const char* program, const char* const* args, int count) {
	if (launching < 0) {
		LOG_warn("plugins: replace_launch only works from pre_launch\n");
		return -1;
	}
	const char* id = loaded[launching].id;
	if (replacer >= 0) {
		LOG_warn("plugins: %s can't replace the launch, %s already did (the first in load order wins)\n", id, loaded[replacer].id);
		return -1;
	}
	if (LaunchCmd_set(&pending, SDCARD_PATH, program, args, count) != 0) {
		LOG_warn("plugins: %s can't replace the launch with %s: it isn't a file on the card, or its arguments don't fit\n",
			id, program ? program : "(null)");
		return -1;
	}
	pending_set = 1;
	return 0;
}

int Plugins_preLaunch(const char* path, int type) {
	replacer = -1;
	// paks' own pre-launch.sh scripts first; they don't need plugins on
	if (!PakHooks_preLaunch(path, type)) return 0;

	MosaicLaunch launch = { .path = path, .type = type };
	for (int i = 0; i < loaded_count; i++) {
		const MosaicPlugin* p = loaded[i].plugin;
		if (!Plugin_has(p, pre_launch) || !p->pre_launch) continue;
		launching = i;
		pending_set = 0;
		int result = MOSAIC_LAUNCH_CONTINUE;
		AS_PLUGIN(i) result = p->pre_launch(&launch);
		launching = -1;
		if (result == MOSAIC_LAUNCH_CANCEL) {
			LOG_info("plugins: launch of %s cancelled by a plugin\n", path);
			replacer = -1;
			return 0;
		}
		if (result != MOSAIC_LAUNCH_REPLACE) continue;
		if (!pending_set) {
			LOG_warn("plugins: %s returned MOSAIC_LAUNCH_REPLACE without a replace_launch command, ignoring it\n", loaded[i].id);
			continue;
		}
		replacement = pending;
		replacer = i;
		LOG_info("plugins: %s replaced the launch of %s with %s\n", loaded[i].id, path, replacement.program);
	}
	pending_set = 0;
	if (loaded_count > 0) {
		// launcher.launch's payload, kept for launcher.return when the launcher starts again
		char payload[EVENT_PAYLOAD_MAX];
		snprintf(payload, sizeof(payload), "path=%s\ntype=%s", path, type == ENTRY_PAK ? "pak" : "rom");
		putFile(LAUNCHED_MARKER, payload);
		// queued like any other: the launcher delivers it before its next frame,
		// or as it quits for the launch, before plugins' quit hooks
		hostEvent(EVENT_HOST_PREFIX "launch", payload);
	}
	Plugins_homeLeaving();
	return 1;
}

const LaunchCmd* Plugins_launchReplacement(void) {
	return replacer >= 0 ? &replacement : NULL;
}

// The MOSAIC_ART_* kind for an art.h kind, or -1 for one plugins don't see.
static int mosaicKind(int kind) {
	switch (kind) {
		case ART_THUMBNAIL: return MOSAIC_ART_THUMBNAIL;
		case ART_FOLDER_BG: return MOSAIC_ART_FOLDER_BG;
		case ART_LIST_BG: return MOSAIC_ART_LIST_BG;
		case ART_BOX: return MOSAIC_ART_BOX;
		case ART_BOX_3D: return MOSAIC_ART_BOX_3D;
		case ART_LOGO: return MOSAIC_ART_LOGO;
		case ART_SCREENSHOT: return MOSAIC_ART_SCREENSHOT;
		case ART_BACKDROP: return MOSAIC_ART_BACKDROP;
		default: return -1;
	}
}

int Plugins_artPath(const Entry* entry, int kind, char* out, size_t size) {
	int mosaic_kind = mosaicKind(kind);
	if (mosaic_kind < 0) return 0;
	EACH_PLUGIN_WITH(art_path) {
		out[0] = '\0';
		if (p->art_path((const MosaicEntry*)entry, mosaic_kind, out, size) && out[0]) return 1;
	}
	return 0;
}

static int generates(int i) {
	const MosaicPlugin* p = loaded[i].plugin;
	return !loaded[i].art_off && Plugin_has(p, art_surface) && p->art_surface;
}

int Plugins_hasArtSurface(void) {
	for (int i = 0; i < loaded_count; i++) {
		if (generates(i)) return 1;
	}
	return 0;
}

SDL_Surface* Plugins_artSurface(const Entry* entry, int kind) {
	int mosaic_kind = mosaicKind(kind);
	if (!entry || mosaic_kind < 0 || generating) return NULL;
	for (int i = 0; i < loaded_count; i++) {
		if (!generates(i)) continue;
		generating = 1;
		MosaicSurface* made = NULL;
		AS_PLUGIN(i) made = loaded[i].plugin->art_surface((const MosaicEntry*)entry, mosaic_kind);
		generating = 0;
		if (!made) continue;
		// the launcher's from now on, so the plugin can't draw on it or free it
		if (DrawReg_give(made, loaded[i].id) == 0) return (SDL_Surface*)made;
		loaded[i].art_off = 1;
		LOG_warn("plugins: %s's art_surface returned a surface it didn't make, no longer asking it for art until the launcher restarts\n",
			loaded[i].id);
	}
	return NULL;
}

int Plugins_repairPath(const char* missing, char* out, size_t size) {
	EACH_PLUGIN_WITH(repair_path) {
		out[0] = '\0';
		if (p->repair_path(missing, out, size) == 1 && out[0]) return 1;
	}
	return 0;
}

void Plugins_tick(unsigned long now) {
	// the quick menu's Bluetooth tile may have changed it; the worker,
	// when there is one, never reads stock's settings itself
	if (loaded_count && BT_supported()) Bluetooth_setEnabled(CFG_getBluetooth());
	// and its Wi-Fi tile; the toggle alone never starts the worker
	if (loaded_count && wifiSupported()) Wifi_setEnabled(wifiOn());
	// RetroAchievements answers came in, for the rows that asked
	if (RAProgress_news()) state.dirty = 1;
	// what was published since the last frame, before plugins' ticks
	eventsDeliver();
	EACH_PLUGIN_WITH(tick) p->tick(now);
}

void Plugins_beforeSleep(void) {
	EACH_PLUGIN_WITH(before_sleep) p->before_sleep();
	// what plugins left open, once its last sound plays out
	Audio_hostClose("before sleep", 1);
	Bluetooth_pause(1);
	Wifi_pause(1);
}

// input filters' power button (see Plugins_inputFilter)
static PowerHold power_hold;

void Plugins_afterResume(void) {
	// stock's resume_tick, which PWR_sleep has just set
	PowerHold_woke(&power_hold, SDL_GetTicks());
	// so plugins' after_resume see a fresh address soon
	Bluetooth_pause(0);
	Wifi_pause(0);
	EACH_PLUGIN_WITH(after_resume) p->after_resume();
}

void Plugins_beforePoweroff(void) {
	EACH_PLUGIN_WITH(before_poweroff) p->before_poweroff();
	Audio_hostClose("before powering off", 0);
	// so the device doesn't power off in a plugin's colour
	const char* holder = Leds_holder();
	if (holder) {
		char id[64];
		snprintf(id, sizeof(id), "%s", holder);
		hostLedClear(id);
	}
}

const MosaicPlugin* Plugins_at(int i) {
	return (i >= 0 && i < loaded_count) ? loaded[i].plugin : NULL;
}

///////////////////////////////////////
// the layout slot

int Plugins_hasLayout(void) {
	return LayoutSlot_active(&layout);
}

// After a layout call: an error hands the game list back to the built-in
// list for the rest of the run.
static int layoutResult(const char* hook, int rc) {
	if (LayoutSlot_result(&layout, rc)) return 1;
	LOG_warn("plugins: %s's %s returned %i, using the built-in list until the launcher restarts\n",
		loaded[layout.owner].id, hook, rc);
	return 0;
}

// Runs layout_init first, the first time the layout is needed.
static int layoutReady(const MosaicListView* view) {
	if (!LayoutSlot_active(&layout)) return 0;
	if (!LayoutSlot_needsInit(&layout)) return 1;
	const MosaicPlugin* p = loaded[layout.owner].plugin;
	int rc = 0;
	if (Plugin_has(p, layout_init) && p->layout_init) AS_PLUGIN(layout.owner) rc = p->layout_init(view);
	if (LayoutSlot_result(&layout, rc)) return 1;
	LOG_warn("plugins: %s declined the " LAYOUT_SLOT " slot (layout_init returned %i), using the built-in list\n",
		loaded[layout.owner].id, rc);
	return 0;
}

void Plugins_layoutInput(const MosaicListView* view) {
	if (!layoutReady(view)) return;
	const MosaicPlugin* p = loaded[layout.owner].plugin;
	if (!Plugin_has(p, layout_input) || !p->layout_input) return;
	MosaicInput input;
	LayoutInput_fill(&input, layoutButtons, LAYOUT_BUTTONS, pad.just_pressed, pad.just_repeated, pad.just_released, pad.is_pressed);
	unsigned int consumed = 0;
	int rc = 0;
	AS_PLUGIN(layout.owner) rc = p->layout_input(&input, view, &consumed);
	if (!layoutResult("layout_input", rc)) return;
	// the game list never sees what the layout handled; what's held stays
	// held, so the next frame's presses and releases are as the pad has them
	int taken = LayoutInput_padBits(layoutButtons, LAYOUT_BUTTONS, consumed);
	InputMask_take(&pad.just_pressed, &pad.just_repeated, &pad.just_released, taken);
}

///////////////////////////////////////
// input filters

static int filters(int i) {
	const MosaicPlugin* p = loaded[i].plugin;
	return !loaded[i].filter_off && Plugin_has(p, input_filter) && p->input_filter;
}

// Every button a filter sees: a layout's, and MENU, power and the FN
// buttons, which on some devices are only known at run time.
static int filterButtons(LayoutButton* out) {
	int n = 0;
	for (int i = 0; i < LAYOUT_BUTTONS; i++) out[n++] = layoutButtons[i];
	out[n++] = (LayoutButton){ MOSAIC_BUTTON_MENU, BTN_MENU };
	out[n++] = (LayoutButton){ MOSAIC_BUTTON_POWER, BTN_POWER };
	out[n++] = (LayoutButton){ MOSAIC_BUTTON_FN1, BTN_FN1 };
	out[n++] = (LayoutButton){ MOSAIC_BUTTON_FN2, BTN_FN2 };
	out[n++] = (LayoutButton){ MOSAIC_BUTTON_FN3, BTN_FN3 };
	return n;
}

void Plugins_inputFilter(unsigned long now) {
	int any = 0;
	for (int i = 0; i < loaded_count && !any; i++) any = filters(i);
	// a press taken before a filter failed is still timed
	if (!any && !power_hold.taken_at) return;
	LayoutButton map[LAYOUT_BUTTONS + 5];
	int count = filterButtons(map);
	int pressed = pad.just_pressed, repeated = pad.just_repeated, released = pad.just_released;
	int taken = 0;
	for (int i = 0; i < loaded_count; i++) {
		if (!filters(i)) continue;
		MosaicInput input;
		LayoutInput_fill(&input, map, count, pressed & ~taken, repeated & ~taken, released & ~taken, pad.is_pressed);
		unsigned int consumed = 0;
		int rc = 0;
		AS_PLUGIN(i) rc = loaded[i].plugin->input_filter(&input, &consumed);
		if (rc != 0) {
			loaded[i].filter_off = 1;
			LOG_warn("plugins: %s's input_filter returned %i, no longer filtering input with it until the launcher restarts\n",
				loaded[i].id, rc);
			continue;
		}
		taken |= LayoutInput_padBits(map, count, consumed);
	}
	if (PowerHold_step(&power_hold, now, BTN_POWER, pressed, released, pad.is_pressed, &taken)) {
		// stock's own power-off button, which PWR_update powers off for
		LOG_info("plugins: power held through an input filter, powering off\n");
		pad.just_released |= BTN_POWEROFF;
	}
	InputMask_take(&pad.just_pressed, &pad.just_repeated, &pad.just_released, taken);
}

int Plugins_layoutRender(SDL_Surface* surface, const MosaicListView* view, ListSelection* selected) {
	if (selected) ListSelection_clear(selected);
	if (!layoutReady(view)) return 0;
	const MosaicPlugin* p = loaded[layout.owner].plugin;
	SDL_Rect clip = { view->area.x, view->area.y, view->area.w, view->area.h };
	MosaicSurface* lent = PluginDraw_lend(surface, &clip);
	if (!lent) {
		LOG_warn("plugins: the screen is already lent, so %s's layout can't draw this frame\n", loaded[layout.owner].id);
		return 0;
	}
	layout_rendering = 1;
	layout_lent = lent;
	layout_area = view->area;
	ListSelection_clear(&layout_selected);
	swap_drawn = 0;
	int rc = 0;
	AS_PLUGIN(layout.owner) rc = p->layout_render(lent, view);
	layout_rendering = 0;
	layout_lent = NULL;
	PluginDraw_unlend(lent);
	int drawn = layoutResult("layout_render", rc);
	if (drawn && selected) *selected = layout_selected;
	return drawn;
}

///////////////////////////////////////
// transition styles

int Plugins_hasTransition(int kind) {
	return TransitionSlots_asked(&transitions, kind, CFG_getMenuTransitions()) >= 0;
}

// After an error: stock's transitions for the rest of the run.
static void transitionFailed(const char* hook, int rc) {
	LOG_warn("plugins: %s's %s returned %i, using stock's transitions until the launcher restarts\n",
		loaded[transition_owner].id, hook, rc);
	TransitionSlots_fail(&transitions, transition_owner);
}

int Plugins_transitionBegin(int kind, SDL_Surface* old_frame, SDL_Surface* new_frame, int stock_ms,
		const ListSelection* old_sel, const ListSelection* new_sel, MosaicTransition* out) {
	int owner = TransitionSlots_asked(&transitions, kind, CFG_getMenuTransitions());
	if (owner < 0 || transition_owner >= 0 || !old_frame || !new_frame || old_frame == new_frame) return MOSAIC_STYLE_STOCK;
	const MosaicSurface* old_lent = PluginDraw_lendReadOnly(old_frame);
	const MosaicSurface* new_lent = PluginDraw_lendReadOnly(new_frame);
	if (!old_lent || !new_lent) {
		LOG_warn("plugins: a transition's frames are already lent, so %s can't draw it\n", loaded[owner].id);
		if (old_lent) PluginDraw_unlend((MosaicSurface*)old_lent);
		if (new_lent) PluginDraw_unlend((MosaicSurface*)new_lent);
		return MOSAIC_STYLE_STOCK;
	}
	memset(&transition, 0, sizeof(transition));
	transition.size = sizeof(transition);
	transition.kind = kind;
	transition.old_frame = old_lent;
	transition.new_frame = new_lent;
	transition.stock_ms = stock_ms;
	transition.duration_ms = stock_ms;
	transition.ease = MOSAIC_EASE_OUT;
	if (old_sel) {
		transition.old_row = old_sel->row;
		transition.old_pill = old_sel->pill;
	}
	if (new_sel) {
		transition.new_row = new_sel->row;
		transition.new_pill = new_sel->pill;
	}
	transition_owner = owner;

	const MosaicPlugin* p = loaded[owner].plugin;
	int has_frame = Plugin_has(p, transition_frame) && p->transition_frame;
	int rc = 0;
	AS_PLUGIN(owner) rc = p->transition_begin(&transition);
	int style = Transition_checkStyle(rc, has_frame);
	if (style < 0) {
		if (rc == MOSAIC_STYLE_CUSTOM) {
			LOG_warn("plugins: %s chose a custom transition but has no transition_frame, using stock's transitions until the launcher restarts\n",
				loaded[owner].id);
			TransitionSlots_fail(&transitions, owner);
		}
		else transitionFailed("transition_begin", rc);
		Plugins_transitionEnd();
		return MOSAIC_STYLE_STOCK;
	}
	if (style != MOSAIC_STYLE_CUSTOM) {
		Plugins_transitionEnd();
		return style;
	}
	int capped = Transition_capDuration(transition.duration_ms, TRANSITION_MAX_MS);
	if (capped != transition.duration_ms && !loaded[owner].capped) {
		loaded[owner].capped = 1;
		LOG_warn("plugins: %s asked for a %i ms transition, running it for %i ms\n", loaded[owner].id, transition.duration_ms, capped);
	}
	transition.duration_ms = capped;
	*out = transition;
	return style;
}

int Plugins_transitionFrame(SDL_Surface* surface, float progress) {
	if (transition_owner < 0) return -1;
	MosaicSurface* lent = PluginDraw_lend(surface, NULL);
	if (!lent) {
		LOG_warn("plugins: a transition's screen is already lent, so %s can't draw it\n", loaded[transition_owner].id);
		return -1;
	}
	int rc = 0;
	AS_PLUGIN(transition_owner) rc = loaded[transition_owner].plugin->transition_frame(lent, &transition, progress);
	PluginDraw_unlend(lent);
	if (rc != 0) transitionFailed("transition_frame", rc);
	return rc == 0 ? 0 : -1;
}

void Plugins_transitionEnd(void) {
	if (transition_owner < 0) return;
	PluginDraw_unlend((MosaicSurface*)transition.old_frame);
	PluginDraw_unlend((MosaicSurface*)transition.new_frame);
	memset(&transition, 0, sizeof(transition));
	transition_owner = -1;
}

///////////////////////////////////////
// the background slot

int Plugins_hasBackground(void) {
	return BackgroundSlot_active(&background);
}

int Plugins_backgroundRender(SDL_Surface* surface, const Entry* selected, unsigned serial) {
	if (!BackgroundSlot_active(&background)) return 0;
	MosaicSurface* lent = PluginDraw_lend(surface, NULL);
	if (!lent) {
		LOG_warn("plugins: the background is already lent, so %s can't draw it this frame\n", loaded[background.owner].id);
		BackgroundSlot_skip(&background);
		return 0;
	}
	backgrounding = 1;
	int rc = 0;
	AS_PLUGIN(background.owner) rc = loaded[background.owner].plugin->background_render(lent, (const MosaicEntry*)selected);
	backgrounding = 0;
	PluginDraw_unlend(lent);
	if (BackgroundSlot_result(&background, rc, serial)) return 1;
	LOG_warn("plugins: %s's background_render returned %i, using stock's background until the launcher restarts\n",
		loaded[background.owner].id, rc);
	return 0;
}

void Plugins_backgroundSkip(void) {
	BackgroundSlot_skip(&background);
}

int Plugins_backgroundShow(int on_gamelist, unsigned serial) {
	return BackgroundSlot_show(&background, on_gamelist, serial);
}

///////////////////////////////////////
// row decorators, art decorators and header widgets

static int decorates(int i) {
	const MosaicPlugin* p = loaded[i].plugin;
	return !loaded[i].rows_off && Plugin_has(p, decorate_row) && p->decorate_row;
}

int Plugins_hasDecorators(void) {
	for (int i = 0; i < loaded_count; i++) {
		if (decorates(i)) return 1;
	}
	return 0;
}

// Every decorator in load order on one row, on a surface lent and clipped
// to it. One that fails is turned off for the rest of the run.
static void decorate(MosaicSurface* lent, const Entry* entry, const MosaicRow* row) {
	decorating = 1;
	for (int i = 0; i < loaded_count; i++) {
		if (!decorates(i)) continue;
		int rc = 0;
		AS_PLUGIN(i) rc = loaded[i].plugin->decorate_row(lent, (const MosaicEntry*)entry, row);
		if (rc == 0) continue;
		loaded[i].rows_off = 1;
		LOG_warn("plugins: %s's decorate_row returned %i, leaving its decorations out until the launcher restarts\n",
			loaded[i].id, rc);
	}
	decorating = 0;
}

void Plugins_decorateRow(SDL_Surface* surface, const Entry* entry, MosaicRow* row) {
	if (!Plugins_hasDecorators() || row->rect.w <= 0 || row->rect.h <= 0) return;
	row->size = sizeof(*row);
	SDL_Rect clip = { row->rect.x, row->rect.y, row->rect.w, row->rect.h };
	MosaicSurface* lent = PluginDraw_lend(surface, &clip);
	if (!lent) {
		LOG_warn("plugins: the screen is already lent, so rows can't be decorated this frame\n");
		return;
	}
	decorate(lent, entry, row);
	PluginDraw_unlend(lent);
}

static int hostDecorateCell(MosaicSurface* screen, int index, const MosaicRect* rect, const MosaicRect* name) {
	if (!layout_rendering || decorating || !screen || screen != layout_lent || !rect) return -1;
	if (!state.top || index < 0 || index >= state.top->entries->count) return -1;
	MosaicRect clip;
	if (!LayoutRect_clip(rect, &layout_area, &clip)) return -1;
	ListSelection_cell(&layout_selected, index, state.top->selected, rect, &layout_area);
	if (!Plugins_hasDecorators()) return 0;
	MosaicRow row = { sizeof(row), index, index == state.top->selected, *rect, { rect->x, rect->y, 0, rect->h },
		state.top->names_hidden ? state.top->names_hidden[index] : 0 };
	// a name is kept inside its cell
	if (name && !LayoutRect_clip(name, rect, &row.name)) row.name = (MosaicRect){ rect->x, rect->y, 0, rect->h };
	PluginDraw_clip(screen, &(SDL_Rect){ clip.x, clip.y, clip.w, clip.h });
	decorate(screen, state.top->entries->items[index], &row);
	PluginDraw_clip(screen, &(SDL_Rect){ layout_area.x, layout_area.y, layout_area.w, layout_area.h });
	return 0;
}

static int decoratesArt(int i) {
	const MosaicPlugin* p = loaded[i].plugin;
	return !loaded[i].art_deco_off && Plugin_has(p, decorate_art) && p->decorate_art;
}

int Plugins_hasArtDecorators(void) {
	for (int i = 0; i < loaded_count; i++) {
		if (decoratesArt(i)) return 1;
	}
	return 0;
}

// Every art decorator in load order, on a surface lent and clipped to
// below. One that fails is turned off for the rest of the run.
static void decorateArt(MosaicSurface* lent, const Entry* entry, const MosaicRect* art, const MosaicRect* below) {
	decorating = 1;
	for (int i = 0; i < loaded_count; i++) {
		if (!decoratesArt(i)) continue;
		int rc = 0;
		AS_PLUGIN(i) rc = loaded[i].plugin->decorate_art(lent, (const MosaicEntry*)entry, art, below);
		if (rc == 0) continue;
		loaded[i].art_deco_off = 1;
		LOG_warn("plugins: %s's decorate_art returned %i, leaving its art decorations out until the launcher restarts\n",
			loaded[i].id, rc);
	}
	decorating = 0;
}

void Plugins_decorateArt(SDL_Surface* surface, const Entry* entry, const MosaicRect* art, const MosaicRect* below) {
	if (!Plugins_hasArtDecorators() || below->w <= 0 || below->h <= 0) return;
	MosaicSurface* lent = PluginDraw_lend(surface, &(SDL_Rect){ below->x, below->y, below->w, below->h });
	if (!lent) {
		LOG_warn("plugins: the screen is already lent, so the art can't be decorated this frame\n");
		return;
	}
	decorateArt(lent, entry, art, below);
	PluginDraw_unlend(lent);
}

static int hostDecorateArt(MosaicSurface* screen, int index, const MosaicRect* art) {
	if (!layout_rendering || decorating || !screen || screen != layout_lent || !art) return -1;
	if (!state.top || index < 0 || index >= state.top->entries->count) return -1;
	MosaicRect inside, below;
	if (!LayoutRect_clip(art, &layout_area, &inside)) return -1;
	if (!Plugins_hasArtDecorators() || !LayoutArt_below(art, &layout_area, &below)) return 0;
	PluginDraw_clip(screen, &(SDL_Rect){ below.x, below.y, below.w, below.h });
	decorateArt(screen, state.top->entries->items[index], art, &below);
	PluginDraw_clip(screen, &(SDL_Rect){ layout_area.x, layout_area.y, layout_area.w, layout_area.h });
	return 0;
}

static int hasHeader(int i) {
	const MosaicPlugin* p = loaded[i].plugin;
	return !loaded[i].header_off && Plugin_has(p, header_width) && p->header_width
		&& Plugin_has(p, header_widget) && p->header_widget;
}

static void headerOff(int i, const char* hook, int rc) {
	loaded[i].header_off = 1;
	LOG_warn("plugins: %s's %s returned %i, leaving its header widget out until the launcher restarts\n",
		loaded[i].id, hook, rc);
}

void Plugins_header(SDL_Surface* surface, HeaderRow* row) {
	for (int i = 0; i < loaded_count; i++) {
		if (!hasHeader(i)) continue;
		int room = HeaderRow_room(row);
		if (room <= 0) return;
		const MosaicPlugin* p = loaded[i].plugin;
		decorating = 1;
		int want = 0;
		AS_PLUGIN(i) want = p->header_width(room, row->h);
		decorating = 0;
		if (want < 0) {
			headerOff(i, "header_width", want);
			continue;
		}
		MosaicRect rect;
		if (!HeaderRow_place(row, want, &rect)) continue;
		MosaicSurface* lent = PluginDraw_lend(surface, &(SDL_Rect){ rect.x, rect.y, rect.w, rect.h });
		if (!lent) {
			LOG_warn("plugins: the screen is already lent, so header widgets can't draw this frame\n");
			return;
		}
		decorating = 1;
		int rc = 0;
		AS_PLUGIN(i) rc = p->header_widget(lent, &rect);
		decorating = 0;
		PluginDraw_unlend(lent);
		if (rc != 0) headerOff(i, "header_widget", rc);
	}
}

///////////////////////////////////////
// selection events

static SelKey sel_key = { NULL, NULL, -1, 0 };
static SelSettle sel_settle; // all zero: nothing sent yet

static int notifies(int i) {
	const MosaicPlugin* p = loaded[i].plugin;
	return !loaded[i].selection_off && Plugin_has(p, selection_changed) && p->selection_changed;
}

// the buttons that scroll the game list, under the built-in list or a layout
#define SCROLL_BUTTONS (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_L1 | BTN_R1 | BTN_L2 | BTN_R2)

void Plugins_selection(unsigned long now) {
	int any = 0;
	for (int i = 0; i < loaded_count && !any; i++) any = notifies(i);
	if (!any || !state.top) return;
	Array* entries = state.top->entries;
	const Entry* entry = entries->count > 0 ? entries->items[state.top->selected] : NULL;
	int moved = SelKey_changed(&sel_key, state.top, entries, state.top->selected);
	int held = (pad.is_pressed & SCROLL_BUTTONS) != 0;
	if (!SelSettle_step(&sel_settle, now, moved, held, state.top->path, entry ? entry->path : NULL)) return;
	selecting = 1;
	for (int i = 0; i < loaded_count; i++) {
		if (!notifies(i)) continue;
		int rc = 0;
		AS_PLUGIN(i) rc = loaded[i].plugin->selection_changed((const MosaicEntry*)entry);
		if (rc == 0) continue;
		loaded[i].selection_off = 1;
		LOG_warn("plugins: %s's selection_changed returned %i, no longer telling it about the selection until the launcher restarts\n",
			loaded[i].id, rc);
	}
	selecting = 0;
}

///////////////////////////////////////
// screens and overlays

static int screenKind(int screen) {
	switch (screen) {
		case SCREEN_MODAL: return SCREENKIND_DIALOG;
		case SCREEN_QUICKMENU: return SCREENKIND_QUICKMENU;
		case SCREEN_GAMESWITCHER: return SCREENKIND_SWITCHER;
		case SCREEN_PLUGIN: return SCREENKIND_PLUGIN;
		default: return SCREENKIND_LIST;
	}
}

static int hostRegisterScreen(const char* id, const MosaicScreenHooks* hooks) {
	if (initializing < 0) {
		LOG_warn("plugins: register_screen only works from init\n");
		return -1;
	}
	const char* owner = loaded[initializing].id;
	int rc = ScreenSet_add(&screens, id, initializing, hooks);
	if (rc == SCREEN_ADDED) {
		LOG_info("plugins: %s registered screen %s\n", owner, id);
		return 0;
	}
	const char* why = rc == SCREEN_BAD_ID ? "its id is empty, too long or a built-in screen's"
		: rc == SCREEN_BAD_HOOKS ? "it has no render"
		: rc == SCREEN_TAKEN ? "another screen has its id" : "there are too many screens";
	LOG_warn("plugins: %s can't register screen %s: %s\n", owner, id ? id : "(null)", why);
	return -1;
}

// 1 where open_screen and close_screen work: not from init, a screen's
// enter or exit, or a hook that only draws, nor while a dialog shows or a
// launch starts.
static int screenCallAllowed(const char* call) {
	if (initializing >= 0 || screen_hook || layout_rendering || decorating || backgrounding || generating
			|| screen_drawing || transition_owner >= 0) {
		LOG_warn("plugins: %s doesn't work from init, a screen's enter or exit, or a hook that only draws\n", call);
		return 0;
	}
	return currentScreen != SCREEN_MODAL && !state.startgame;
}

int Plugins_screenFlags(void) {
	int top = ScreenSet_top(&screens);
	return top >= 0 ? screens.screens[top].hooks.flags : 0;
}

const char* Plugins_currentScreen(void) {
	int top = ScreenSet_top(&screens);
	if (currentScreen == SCREEN_PLUGIN && top >= 0) return screens.screens[top].id;
	switch (currentScreen) {
		case SCREEN_MODAL: return "dialog";
		case SCREEN_QUICKMENU: return "quickmenu";
		case SCREEN_GAMESWITCHER: return "switcher";
		default: return "gamelist";
	}
}

// Opens a screen that can open, running its enter, and slides it in with
// slide (ANIM_NONE for none) where screens slide. Returns 0, or -1 when
// enter declined.
static int openScreen(int index, int slide) {
	PluginScreen* s = &screens.screens[index];
	if (s->hooks.enter) {
		screen_hook = 1;
		int rc = 0;
		AS_PLUGIN(s->owner) rc = s->hooks.enter(s->hooks.data);
		screen_hook = 0;
		if (rc != 0) {
			LOG_info("plugins: %s's screen %s didn't open (enter returned %i)\n", loaded[s->owner].id, s->id, rc);
			return -1;
		}
	}
	// the switcher screen is the game switcher, which nothing slides to or from
	int from = screens.depth > 0 ? ScreenSet_kind(&screens, ScreenSet_top(&screens)) : screenKind(currentScreen);
	// the base is only kept from the first, which opens over a built-in screen
	ScreenSet_push(&screens, index, currentScreen);
	currentScreen = SCREEN_PLUGIN;
	if (ScreenKind_slides(from, ScreenSet_kind(&screens, index))) state.animationdirection = slide;
	state.dirty = 1;
	LOG_info("plugins: opened screen %s\n", s->id);
	return 0;
}

static int hostOpenScreen(const char* id) {
	if (!screenCallAllowed("open_screen")) return -1;
	int index = ScreenSet_find(&screens, id);
	if (index >= 0 && index == screens.switcher) {
		LOG_warn("plugins: can't open screen %s: it's the game switcher, which run_action(\"gamelist.switcher\") opens\n", id);
		return -1;
	}
	int why = ScreenSet_canOpen(&screens, index);
	if (why != SCREEN_OPENS) {
		LOG_warn("plugins: can't open screen %s: %s\n", id ? id : "(null)",
			why == SCREEN_UNKNOWN ? "there's no such screen" : why == SCREEN_IS_OFF ? "it failed earlier"
			: why == SCREEN_IS_OPEN ? "it's open already" : "too many screens are open");
		return -1;
	}
	return openScreen(index, SLIDE_LEFT);
}

static void runExit(PluginScreen* s) {
	if (!s->hooks.exit) return;
	screen_hook = 1;
	AS_PLUGIN(s->owner) s->hooks.exit(s->hooks.data);
	screen_hook = 0;
}

// Closes the open screens from the top down to index, running their exit,
// and goes back to what's under it, sliding out as a folder does. The home
// screen, the root's parent, goes on to the list as a folder opens. The
// switcher screen goes back as the built-in switcher does, at once: to the
// game list or the home screen it opened over, or after an error to the
// built-in switcher in its place.
static void closeTo(int index) {
	int top = -1;
	while (screens.depth > 0) {
		top = ScreenSet_pop(&screens);
		runExit(&screens.screens[top]);
		LOG_info("plugins: closed screen %s\n", screens.screens[top].id);
		if (top == index) break;
	}
	int after = AFTER_BASE;
	int to;
	if (screens.depth > 0) {
		currentScreen = SCREEN_PLUGIN;
		to = ScreenSet_kind(&screens, ScreenSet_top(&screens));
	}
	else {
		after = ScreenSet_afterClose(&screens, top);
		currentScreen = after == AFTER_BASE ? screens.base : after == AFTER_SWITCHER ? SCREEN_GAMESWITCHER : SCREEN_GAMELIST;
		to = screenKind(currentScreen);
	}
	int forward = screens.depth == 0 && top == screens.home;
	if (ScreenKind_slides(ScreenSet_kind(&screens, top), to)) state.animationdirection = forward ? SLIDE_LEFT : SLIDE_RIGHT;
	folderbgchanged = 1; // the screen cleared every layer; repaint the background
	state.dirty = 1;
	if (after == AFTER_SWITCHER) {
		state.switcher_selected = 0;
		LOG_warn("plugins: using the built-in game switcher in place of %s until the launcher restarts\n", screens.screens[top].id);
	}
	// the built-in switcher shows the home screen at once, so this does too
	else if (after == AFTER_HOME && !state.startgame) openScreen(screens.home, ANIM_NONE);
}

static int hostCloseScreen(const char* id) {
	if (!screenCallAllowed("close_screen")) return -1;
	int index = ScreenSet_find(&screens, id);
	if (index < 0 || ScreenSet_position(&screens, index) < 0) {
		LOG_warn("plugins: can't close screen %s: it isn't open\n", id ? id : "(null)");
		return -1;
	}
	closeTo(index);
	return 0;
}

static int hostRunAction(const char* action_id, const char* path) {
	if (!screenCallAllowed("run_action")) return -1;
	const Action* action = Actions_find(action_id);
	if (!action) {
		LOG_warn("plugins: can't run action %s: there's no such action\n", action_id ? action_id : "(null)");
		return -1;
	}
	// with no path given, what a button would give it
	if (!path || !path[0]) path = Action_buttonPath(action);
	if (action->takes_path && !path) {
		LOG_warn("plugins: can't run action %s: it acts on an entry, and none was given or is selected\n", action->id);
		return -1;
	}
	if (!Action_available(action, path)) return -1;
	return Action_run(action, path);
}

// After an error the screen stays off until the launcher restarts; it
// closes on the next input, where nothing is mid-draw.
static void screenFailed(PluginScreen* s, const char* hook, int rc) {
	s->off = 1;
	state.dirty = 1;
	LOG_warn("plugins: %s's screen %s's %s returned %i, closing it until the launcher restarts\n",
		loaded[s->owner].id, s->id, hook, rc);
	if (s - screens.screens == screens.home) LOG_warn("plugins: using the root list in place of the home screen until the launcher restarts\n");
}

// Every button a screen sees: a layout's, and MENU.
static int screenButtons(LayoutButton* out) {
	int n = 0;
	for (int i = 0; i < LAYOUT_BUTTONS; i++) out[n++] = layoutButtons[i];
	out[n++] = (LayoutButton){ MOSAIC_BUTTON_MENU, BTN_MENU };
	return n;
}

void Plugins_screenInput(void) {
	int top = ScreenSet_top(&screens);
	if (top < 0) {
		// nothing open; shouldn't happen
		currentScreen = screens.base;
		state.dirty = 1;
		return;
	}
	PluginScreen* s = &screens.screens[top];
	if (s->off) {
		closeTo(top);
		return;
	}
	int rc;
	if (s->hooks.input) {
		LayoutButton map[LAYOUT_BUTTONS + 1];
		int count = screenButtons(map);
		MosaicInput input;
		LayoutInput_fill(&input, map, count, pad.just_pressed, pad.just_repeated, pad.just_released, pad.is_pressed);
		AS_PLUGIN(s->owner) rc = s->hooks.input(&input, s->hooks.data);
	}
	else rc = PAD_justPressed(BTN_B) ? MOSAIC_SCREEN_CLOSE : MOSAIC_SCREEN_STAY;
	if (rc != MOSAIC_SCREEN_STAY && rc != MOSAIC_SCREEN_CLOSE) screenFailed(s, "input", rc);
	// a dialog it opened shows over it, and it closes after; a built-in
	// screen it opened with run_action closes the stack in screensCheck
	if (currentScreen != SCREEN_PLUGIN) return;
	if ((rc == MOSAIC_SCREEN_CLOSE || s->off) && ScreenSet_position(&screens, top) >= 0) {
		closeTo(top);
		return;
	}
	// it, or one it opened, redraws every frame while it says so
	int now_top = ScreenSet_top(&screens);
	if (currentScreen != SCREEN_PLUGIN || now_top < 0) return;
	PluginScreen* shown = &screens.screens[now_top];
	int animating = 0;
	if (!shown->off && shown->hooks.animating) AS_PLUGIN(shown->owner) animating = shown->hooks.animating(shown->hooks.data);
	if (animating) state.dirty = 1;
}

void Plugins_screenRender(SDL_Surface* surface, const MosaicScreenView* view) {
	int top = ScreenSet_top(&screens);
	if (top < 0 || screens.screens[top].off) return;
	PluginScreen* s = &screens.screens[top];
	MosaicSurface* lent = PluginDraw_lend(surface, NULL);
	if (!lent) {
		LOG_warn("plugins: the screen is already lent, so screen %s can't draw this frame\n", s->id);
		return;
	}
	screen_drawing = 1;
	hints_screen = lent;
	hints_view = view;
	int rc = 0;
	AS_PLUGIN(s->owner) rc = s->hooks.render(lent, view, s->hooks.data);
	hints_screen = NULL;
	hints_view = NULL;
	screen_drawing = 0;
	PluginDraw_unlend(lent);
	if (rc != 0) screenFailed(s, "render", rc);
}

static int hostDrawHints(MosaicSurface* screen, int group, const MosaicHint* hints, int count, MosaicRect* drawn) {
	if (!hints_screen || screen != hints_screen) return -1;
	return PluginScreen_drawHints((SDL_Surface*)hints_screen, hints_view, group, hints, count, drawn);
}

// Closes the open screens once a built-in screen of kind came in front of
// them some other way than open_screen and close_screen; a quick menu or
// game switcher over the home screen goes back to it once it closes.
static void closeCovered(int kind, const char* by) {
	if (!ScreenSet_coveredBy(&screens, kind)) return;
	while (screens.depth > 0) {
		int top = ScreenSet_pop(&screens);
		runExit(&screens.screens[top]);
		LOG_info("plugins: closed screen %s, as the %s opened over it\n", screens.screens[top].id, by);
	}
}

void Plugins_screensCheck(void) {
	// the switcher slot's screen in place of the built-in switcher
	Plugins_switcherCheck();
	// left for a built-in screen some other way, such as an FN button's action
	closeCovered(screenKind(currentScreen), Plugins_currentScreen());
}

void Plugins_switcherCheck(void) {
	if (currentScreen != SCREEN_GAMESWITCHER) {
		switcher_declined = 0;
		return;
	}
	if (switcher_declined || !ScreenSet_switcherReady(&screens) || state.startgame) return;
	// as the built-in one would: over the game list, alone
	closeCovered(SCREENKIND_SWITCHER, "switcher");
	currentScreen = SCREEN_GAMELIST;
	// it brings itself in, so nothing slides
	if (openScreen(screens.switcher, ANIM_NONE) == 0) return;
	// the built-in one this time
	currentScreen = SCREEN_GAMESWITCHER;
	switcher_declined = 1;
	state.dirty = 1;
}

void Plugins_homeStart(void) {
	char left[16] = "";
	int has_left = exists(HOME_LEFT_PATH);
	if (has_left) {
		getFile(HOME_LEFT_PATH, left, sizeof(left));
		unlink(HOME_LEFT_PATH);
	}
	switch (Home_start(ScreenSet_homeReady(&screens), has_left ? left : NULL, screenKind(currentScreen))) {
		case HOME_START_OPEN:
			// as the first screen, so nothing slides
			openScreen(screens.home, ANIM_NONE);
			break;
		case HOME_START_UNDER:
			screens.under = 1;
			LOG_info("plugins: the %s goes back to the home screen\n", Plugins_currentScreen());
			break;
		default:
			break;
	}
}

int Plugins_homeReady(void) {
	return ScreenSet_homeReady(&screens) && !ScreenSet_homeOpen(&screens);
}

void Plugins_openHome(void) {
	if (!Plugins_homeReady() || currentScreen == SCREEN_MODAL || state.startgame) return;
	openScreen(screens.home, SLIDE_RIGHT);
}

void Plugins_homeBack(void) {
	if (!ScreenSet_backToHome(&screens) || state.startgame) return;
	// the quick menu and switcher show the list at once, so home does too
	openScreen(screens.home, ANIM_NONE);
}

void Plugins_homeLeaving(void) {
	if (screens.home < 0) return;
	putFile(HOME_LEFT_PATH, (char*)ScreenSet_leftFrom(&screens));
}

void Plugins_screensClose(void) {
	while (screens.depth > 0) {
		int top = ScreenSet_pop(&screens);
		runExit(&screens.screens[top]);
		LOG_info("plugins: closed screen %s\n", screens.screens[top].id);
	}
}

int Plugins_overlaysOver(int screen) {
	if (!ScreenKind_overlays(screenKind(screen), screen == SCREEN_PLUGIN ? Plugins_screenFlags() : 0)) return 0;
	for (int i = 0; i < loaded_count; i++) {
		const MosaicPlugin* p = loaded[i].plugin;
		if (!loaded[i].overlay_off && Plugin_has(p, overlay_render) && p->overlay_render) return 1;
	}
	return 0;
}

void Plugins_overlays(SDL_Surface* surface, const MosaicScreenView* view) {
	MosaicSurface* lent = PluginDraw_lend(surface, NULL);
	if (!lent) {
		LOG_warn("plugins: the overlay is already lent, so overlays can't draw this frame\n");
		return;
	}
	screen_drawing = 1;
	for (int i = 0; i < loaded_count; i++) {
		const MosaicPlugin* p = loaded[i].plugin;
		if (loaded[i].overlay_off || !Plugin_has(p, overlay_render) || !p->overlay_render) continue;
		int rc = 0;
		AS_PLUGIN(i) rc = p->overlay_render(lent, view);
		if (rc == 0) continue;
		loaded[i].overlay_off = 1;
		LOG_warn("plugins: %s's overlay_render returned %i, leaving its overlay out until the launcher restarts\n",
			loaded[i].id, rc);
	}
	screen_drawing = 0;
	PluginDraw_unlend(lent);
}
