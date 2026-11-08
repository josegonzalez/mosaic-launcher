#include <dlfcn.h>
#include <sys/stat.h>
#include "launcher.h"
#include "pluginset.h"
#include "plugins.h"
#include "stocktoggles.h"
#include "leds.h"
#include "bluetooth.h"
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
	if (layout_rendering || decorating || selecting || backgrounding || generating || transition_owner >= 0 || !state.top || index < 0 || index >= state.top->entries->count) return -1;
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

static int hostArtFind(const MosaicEntry* entry, int kind, char* out, size_t size) {
	return ArtLoader_find((const Entry*)entry, kind, out, size);
}
static int hostArtGet(const MosaicEntry* entry, int kind, const MosaicSurface** out) {
	return ArtLoader_get((const Entry*)entry, kind, out);
}
static int hostDecorateCell(MosaicSurface* screen, int index, const MosaicRect* rect, const MosaicRect* name);
static int hostDecorateArt(MosaicSurface* screen, int index, const MosaicRect* art);

static void playLog(const char* message) {
	LOG_info("%s\n", message);
}

// After a plugin's quit, or when its init declines: its surfaces go with it.
static void freeSurfaces(const char* plugin_id) {
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
	.surface_free = PluginDraw_surfaceFree,
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
		freeSurfaces(m->id);
		if (p->cfg) Hash_free(p->cfg);
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
	Plugins_adoptPin(); // before simple mode's cfg is read
	Bluetooth_setLog(bluetoothLog);

	char* choices = readText(PLUGIN_CHOICES_PATH);
	static PluginDecision decisions[PLUGIN_MAX];
	int n = PluginSet_resolve(manifests, manifest_count, choices, decisions);
	free(choices);

	Plugins_ensureStateDir();
	putFile(LOADING_MARKER, "");
	sync();
	// only where to look: nothing is read until a plugin asks
	PlayHistory_configure(SHARED_USERDATA_PATH "/game_logs.sqlite", ROMS_PATH, playLog);
	RAProgress_configure(SDCARD_PATH, ROMS_PATH, SHARED_USERDATA_PATH "/.ra", SHARED_USERDATA_PATH "/launcher/ra-hashes.txt",
		playLog);

	for (int i = 0; i < n; i++) {
		if (decisions[i].load) load(decisions[i].manifest);
		else LOG_info("plugins: not loading %s: %s\n", decisions[i].manifest->id, decisions[i].reason);
	}
	if (loaded_count == 0) unlink(LOADING_MARKER);
	logOverridden();
	claimLayout();
	claimTransitions();
	claimBackground();

	if (exists(LAUNCHED_MARKER)) {
		unlink(LAUNCHED_MARKER);
		// a game or pak ran, so the play history may have moved on
		PlayHistory_invalidate();
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
	LayoutSlot_clear(&layout);
	TransitionSlots_clear(&transitions);
	BackgroundSlot_clear(&background);
	for (int i = loaded_count - 1; i >= 0; i--) {
		LoadedPlugin* p = &loaded[i];
		if (Plugin_has(p->plugin, quit) && p->plugin->quit) p->plugin->quit();
		// turning a plugin off, or a launch, gives the user's colour back
		hostLedClear(p->id);
		freeSurfaces(p->id);
		if (p->cfg) Hash_free(p->cfg);
		dlclose(p->handle);
	}
	loaded_count = 0;
	PlayHistory_quit();
	RAProgress_quit();
	// a refresh or connect still running is stopped and reaped first
	Bluetooth_quit();
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

unsigned char* Plugins_hideNames(Array* entries, const char* dir_path) {
	ListHideName hides[PLUGIN_MAX];
	int count = 0;
	EACH_PLUGIN_WITH(hide_name) hides[count++] = p->hide_name;
	return ListHide_chain(entries, hides, count, dir_path);
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
		MosaicSurface* made = loaded[i].plugin->art_surface((const MosaicEntry*)entry, mosaic_kind);
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
	// RetroAchievements answers came in, for the rows that asked
	if (RAProgress_news()) state.dirty = 1;
	EACH_PLUGIN_WITH(tick) p->tick(now);
}

void Plugins_beforeSleep(void) {
	EACH_PLUGIN_WITH(before_sleep) p->before_sleep();
	Bluetooth_pause(1);
}

void Plugins_afterResume(void) {
	// so plugins' after_resume see a fresh address soon
	Bluetooth_pause(0);
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
	int rc = Plugin_has(p, layout_init) && p->layout_init ? p->layout_init(view) : 0;
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
	int rc = p->layout_input(&input, view, &consumed);
	if (!layoutResult("layout_input", rc)) return;
	// the game list never sees what the layout handled; what's held stays
	// held, so the next frame's presses and releases are as the pad has them
	int taken = LayoutInput_padBits(layoutButtons, LAYOUT_BUTTONS, consumed);
	InputMask_take(&pad.just_pressed, &pad.just_repeated, &pad.just_released, taken);
}

///////////////////////////////////////
// input filters

static PowerHold power_hold;

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
		int rc = loaded[i].plugin->input_filter(&input, &consumed);
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
	int rc = p->layout_render(lent, view);
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
	int rc = p->transition_begin(&transition);
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
	int rc = loaded[transition_owner].plugin->transition_frame(lent, &transition, progress);
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
	int rc = loaded[background.owner].plugin->background_render(lent, (const MosaicEntry*)selected);
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
		int rc = loaded[i].plugin->decorate_row(lent, (const MosaicEntry*)entry, row);
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
		int rc = loaded[i].plugin->decorate_art(lent, (const MosaicEntry*)entry, art, below);
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
		int want = p->header_width(room, row->h);
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
		int rc = p->header_widget(lent, &rect);
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
		int rc = loaded[i].plugin->selection_changed((const MosaicEntry*)entry);
		if (rc == 0) continue;
		loaded[i].selection_off = 1;
		LOG_warn("plugins: %s's selection_changed returned %i, no longer telling it about the selection until the launcher restarts\n",
			loaded[i].id, rc);
	}
	selecting = 0;
}
