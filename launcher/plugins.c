#include <dlfcn.h>
#include <sys/stat.h>
#include "launcher.h"
#include "pluginset.h"
#include "plugins.h"
#include "stocktoggles.h"
#include "leds.h"

#ifndef PLUGIN_ARCH
#error "build with -DPLUGIN_ARCH=\"...\" (eg. aarch64)"
#endif

#define LAUNCHED_MARKER "/tmp/mosaic-launcher-launched" // a launch went ahead; post_return is due
#define LOADING_MARKER PLUGIN_STATE_DIR "/loading"
#define PAUSED_MARKER PLUGIN_STATE_DIR "/plugins_paused"

typedef struct LoadedPlugin {
	void* handle;
	const MosaicPlugin* plugin;
	char id[64];
	Hash* cfg; // loaded on first cfg_get or cfg_set
	int owns_root; // claims the root slot, so its root_replace is asked
} LoadedPlugin;

static LoadedPlugin loaded[PLUGIN_MAX];
static int loaded_count = 0;
static int initializing = -1; // the loaded plugin whose init is running
static int launching = -1;    // the loaded plugin whose pre_launch is running

#define SCREEN_COUNT 3

static void dropOverrides(int owner);
static void saveBindings(int* counts);
static void dropBindings(const int* counts);
static PluginManifest manifests[PLUGIN_MAX];
static int manifest_count = 0;

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

static void cfgPath(const char* plugin_id, char* out, size_t size) {
	snprintf(out, size, PLUGIN_STATE_DIR "/%s.cfg", plugin_id);
}

static Hash* cfgFor(const char* plugin_id) {
	LoadedPlugin* p = loadedById(plugin_id);
	if (!p) return NULL;
	if (p->cfg) return p->cfg;
	p->cfg = Hash_new();
	char path[512];
	cfgPath(plugin_id, path, sizeof(path));
	FILE* f = fopen(path, "r");
	if (!f) return p->cfg;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		Hash_set(p->cfg, line, eq + 1);
	}
	fclose(f);
	return p->cfg;
}

static const char* hostCfgGet(const char* plugin_id, const char* key, const char* fallback) {
	Hash* cfg = cfgFor(plugin_id);
	if (!cfg || !key) return fallback;
	// the last value set wins
	for (int i = cfg->keys->count - 1; i >= 0; i--) {
		if (!strcmp(cfg->keys->items[i], key)) return cfg->values->items[i];
	}
	return fallback;
}

static void hostCfgSet(const char* plugin_id, const char* key, const char* value) {
	Hash* cfg = cfgFor(plugin_id);
	if (!cfg || !key || !value || strchr(key, '=') || strchr(key, '\n') || strchr(value, '\n')) return;
	for (int i = 0; i < cfg->keys->count; i++) {
		if (!strcmp(cfg->keys->items[i], key)) {
			free(cfg->values->items[i]);
			cfg->values->items[i] = strdup(value);
			goto save;
		}
	}
	Hash_set(cfg, (char*)key, (char*)value);
save:
	Plugins_ensureStateDir();
	char path[512];
	cfgPath(plugin_id, path, sizeof(path));
	FILE* f = fopen(path, "w");
	if (!f) return;
	for (int i = 0; i < cfg->keys->count; i++) {
		fprintf(f, "%s=%s\n", (char*)cfg->keys->items[i], (char*)cfg->values->items[i]);
	}
	fclose(f);
}

static MosaicEntry* hostEntryNew(const char* path, int type, const char* name) {
	if (!path) return NULL;
	Entry* entry = name ? Entry_newNamed((char*)path, type, (char*)name) : Entry_new((char*)path, type);
	return (MosaicEntry*)entry;
}
static const char* hostEntryPath(const MosaicEntry* e) { return e ? ((const Entry*)e)->path : NULL; }
static const char* hostEntryName(const MosaicEntry* e) { return e ? ((const Entry*)e)->name : NULL; }
static int hostEntryType(const MosaicEntry* e) { return e ? ((const Entry*)e)->type : -1; }

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

static int hostRegisterAction(const char* id, const char* label,
		int (*available)(void* data), void (*run)(void* data), void* data) {
	if (!id || !run) return -1;
	Action* action = calloc(1, sizeof(Action));
	action->id = strdup(id);
	action->label = strdup(label ? label : id);
	action->available = available;
	action->run = run;
	action->data = data;
	Actions_register(action);
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
	return Entry_openPath(path, type);
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
	if (!state.top || index < 0 || index >= state.top->entries->count) return -1;
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
	p->handle = handle;
	p->plugin = plugin;
	snprintf(p->id, sizeof(p->id), "%s", m->id);
	p->owns_root = csvHas(m->slots, ROOT_SLOT);
	if (!p->owns_root && Plugin_has(plugin, root_replace) && plugin->root_replace) {
		LOG_warn("plugins: %s has root_replace but no slots=" ROOT_SLOT ", so it won't be asked\n", m->id);
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
	int declined = Plugin_has(plugin, init) && plugin->init && plugin->init(&host) != 0;
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
			free(action);
		}
		Actions_restore(actions, action_count);
		state.rebuild_quick = rebuild_quick;
		state.power_off = power_off;
		hostInhibitSleep(m->id, 0);
		hostLedClear(m->id);
		if (p->cfg) Hash_free(p->cfg);
		dlclose(handle);
		loaded_count--;
		return;
	}
	QuickTiles_discard(tiles);
	LOG_info("plugins: loaded %s\n", m->id);
}

static void logOverridden(void);

void Plugins_init(void) {
	if (exists(PAUSED_MARKER)) {
		LOG_warn("plugins: paused after a crash; turn them back on from the Launcher pak\n");
		return;
	}
	if (exists(LOADING_MARKER)) {
		// The last start died before its first frame with plugins loaded.
		LOG_warn("plugins: the launcher crashed while starting with plugins, pausing them\n");
		unlink(LOADING_MARKER);
		Plugins_ensureStateDir();
		putFile(PAUSED_MARKER, "");
		return;
	}

	discover();
	if (manifest_count == 0) return;

	char* choices = readText(PLUGIN_CHOICES_PATH);
	static PluginDecision decisions[PLUGIN_MAX];
	int n = PluginSet_resolve(manifests, manifest_count, choices, decisions);
	free(choices);

	Plugins_ensureStateDir();
	putFile(LOADING_MARKER, "");
	sync();

	for (int i = 0; i < n; i++) {
		if (decisions[i].load) load(decisions[i].manifest);
		else LOG_info("plugins: not loading %s: %s\n", decisions[i].manifest->id, decisions[i].reason);
	}
	if (loaded_count == 0) unlink(LOADING_MARKER);
	logOverridden();

	if (exists(LAUNCHED_MARKER)) {
		unlink(LAUNCHED_MARKER);
		for (int i = 0; i < loaded_count; i++) {
			const MosaicPlugin* p = loaded[i].plugin;
			if (Plugin_has(p, post_return) && p->post_return) p->post_return();
		}
	}
}

void Plugins_started(void) {
	if (exists(LOADING_MARKER)) unlink(LOADING_MARKER);
}

void Plugins_quit(void) {
	for (int i = loaded_count - 1; i >= 0; i--) {
		LoadedPlugin* p = &loaded[i];
		if (Plugin_has(p->plugin, quit) && p->plugin->quit) p->plugin->quit();
		// turning a plugin off, or a launch, gives the user's colour back
		hostLedClear(p->id);
		if (p->cfg) Hash_free(p->cfg);
		dlclose(p->handle);
	}
	loaded_count = 0;
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
// hooks

#define EACH_PLUGIN_WITH(field) \
	for (int i_ = 0; i_ < loaded_count; i_++) \
		for (const MosaicPlugin* p = loaded[i_].plugin; p && Plugin_has(p, field) && p->field; p = NULL)

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
		Array* root = (Array*)p->root_replace();
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

void Plugins_sort(Array* entries, const char* dir_path) {
	ListSortCompare compares[PLUGIN_MAX];
	int count = 0;
	EACH_PLUGIN_WITH(sort_compare) compares[count++] = p->sort_compare;
	if (count > 0) ListSort_chain(entries, compares, count, dir_path);
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
		int result = p->pre_launch(&launch);
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
	if (loaded_count > 0) putFile(LAUNCHED_MARKER, "");
	return 1;
}

const LaunchCmd* Plugins_launchReplacement(void) {
	return replacer >= 0 ? &replacement : NULL;
}

int Plugins_artPath(const Entry* entry, int kind, char* out, size_t size) {
	int mosaic_kind;
	switch (kind) {
		case ART_THUMBNAIL: mosaic_kind = MOSAIC_ART_THUMBNAIL; break;
		case ART_FOLDER_BG: mosaic_kind = MOSAIC_ART_FOLDER_BG; break;
		case ART_LIST_BG: mosaic_kind = MOSAIC_ART_LIST_BG; break;
		default: return 0;
	}
	EACH_PLUGIN_WITH(art_path) {
		out[0] = '\0';
		if (p->art_path((const MosaicEntry*)entry, mosaic_kind, out, size) && out[0]) return 1;
	}
	return 0;
}

int Plugins_repairPath(const char* missing, char* out, size_t size) {
	EACH_PLUGIN_WITH(repair_path) {
		out[0] = '\0';
		if (p->repair_path(missing, out, size) == 1 && out[0]) return 1;
	}
	return 0;
}

void Plugins_tick(unsigned long now) {
	EACH_PLUGIN_WITH(tick) p->tick(now);
}

void Plugins_beforeSleep(void) {
	EACH_PLUGIN_WITH(before_sleep) p->before_sleep();
}

void Plugins_afterResume(void) {
	EACH_PLUGIN_WITH(after_resume) p->after_resume();
}

void Plugins_beforePoweroff(void) {
	EACH_PLUGIN_WITH(before_poweroff) p->before_poweroff();
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
