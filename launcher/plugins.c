#include <dlfcn.h>
#include <sys/stat.h>
#include "launcher.h"
#include "pluginset.h"
#include "plugins.h"

#ifndef PLUGIN_ARCH
#error "build with -DPLUGIN_ARCH=\"...\" (eg. aarch64)"
#endif

#define LAUNCHED_MARKER "/tmp/nextui-launcher-launched" // a launch went ahead; post_return is due
#define LOADING_MARKER PLUGIN_STATE_DIR "/loading"
#define PAUSED_MARKER PLUGIN_STATE_DIR "/plugins_paused"

typedef struct LoadedPlugin {
	void* handle;
	const NUIPlugin* plugin;
	char id[64];
	Hash* cfg; // loaded on first cfg_get or cfg_set
	int owns_root; // claims the root slot, so its root_replace is asked
} LoadedPlugin;

static LoadedPlugin loaded[PLUGIN_MAX];
static int loaded_count = 0;
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
	const char* override = getenv("NEXTUI_LAUNCHER_PAK_DIR");
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
	const char* override = getenv("NEXTUI_LAUNCHER_PLUGIN_PATH");
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

static NUIEntry* hostEntryNew(const char* path, int type, const char* name) {
	if (!path) return NULL;
	Entry* entry = name ? Entry_newNamed((char*)path, type, (char*)name) : Entry_new((char*)path, type);
	return (NUIEntry*)entry;
}
static const char* hostEntryPath(const NUIEntry* e) { return e ? ((const Entry*)e)->path : NULL; }
static const char* hostEntryName(const NUIEntry* e) { return e ? ((const Entry*)e)->name : NULL; }
static int hostEntryType(const NUIEntry* e) { return e ? ((const Entry*)e)->type : -1; }

static NUIList* hostListNew(void) { return (NUIList*)Array_new(); }
static int hostListCount(const NUIList* l) { return l ? ((const Array*)l)->count : 0; }
static NUIEntry* hostListGet(const NUIList* l, int i) {
	const Array* a = (const Array*)l;
	if (!a || i < 0 || i >= a->count) return NULL;
	return a->items[i];
}
static void hostListPush(NUIList* l, NUIEntry* e) {
	if (l && e) Array_push((Array*)l, e);
}

static const NUIEntry* hostSelectedEntry(void) {
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
	if (type != NUI_ENTRY_ROM && type != NUI_ENTRY_PAK && type != NUI_ENTRY_DIR) return 0;
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

static int hostEntrySetName(NUIEntry* e, const char* name) {
	if (!listing || !e || !name || !name[0]) return -1;
	Entry* entry = (Entry*)e;
	free(entry->name);
	entry->name = strdup(name);
	list_renamed = 1;
	return 0;
}

static const NUIList* hostCurrentList(void) {
	return state.top ? (const NUIList*)state.top->entries : NULL;
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

static const NUIHost host = {
	.size = sizeof(NUIHost),
	.api_version = NUI_API_VERSION,
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
};

///////////////////////////////////////
// loading

static void load(const PluginManifest* m) {
	if (m->api != NUI_API_VERSION) {
		LOG_warn("plugins: %s targets API %i, this launcher has %i, skipping\n", m->id, m->api, NUI_API_VERSION);
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
	NUIPluginGet get = (NUIPluginGet)dlsym(handle, NUI_PLUGIN_ENTRY);
	const NUIPlugin* plugin = get ? get(&host) : NULL;
	if (!plugin || plugin->size < offsetof(NUIPlugin, init) || plugin->api_version != NUI_API_VERSION
			|| !plugin->id || strcmp(plugin->id, m->id) != 0) {
		LOG_warn("plugins: %s doesn't describe itself as plugin %s for API %i, skipping\n", so, m->id, NUI_API_VERSION);
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

	if (Plugin_has(plugin, init) && plugin->init && plugin->init(&host) != 0) {
		LOG_warn("plugins: %s declined to start\n", m->id);
		if (p->cfg) Hash_free(p->cfg);
		dlclose(handle);
		loaded_count--;
		return;
	}
	LOG_info("plugins: loaded %s\n", m->id);
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

	if (exists(LAUNCHED_MARKER)) {
		unlink(LAUNCHED_MARKER);
		for (int i = 0; i < loaded_count; i++) {
			const NUIPlugin* p = loaded[i].plugin;
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

#define SCREEN_COUNT 3
#define MAX_PLUGIN_BINDINGS 16
static Binding pluginBindings[SCREEN_COUNT][MAX_PLUGIN_BINDINGS];
static int pluginBindingCount[SCREEN_COUNT];

typedef struct ButtonInfo {
	int nui;
	int btn;
	const char* glyph;
} ButtonInfo;

static const ButtonInfo buttons[] = {
	{ NUI_BUTTON_A, BTN_A, "A" },
	{ NUI_BUTTON_B, BTN_B, "B" },
	{ NUI_BUTTON_X, BTN_X, "X" },
	{ NUI_BUTTON_Y, BTN_Y, "Y" },
	{ NUI_BUTTON_L1, BTN_L1, "L1" },
	{ NUI_BUTTON_R1, BTN_R1, "R1" },
	{ NUI_BUTTON_L2, BTN_L2, "L2" },
	{ NUI_BUTTON_R2, BTN_R2, "R2" },
	{ NUI_BUTTON_START, BTN_START, "START" },
	{ NUI_BUTTON_SELECT, BTN_SELECT, "SELECT" },
};

int Plugins_bindButton(int screen, int button, int trigger, const char* action_id) {
	// only the game list dispatches through bindings so far
	if (screen != NUI_SCREEN_GAMELIST) return -1;
	if (trigger != NUI_TRIGGER_PRESS && trigger != NUI_TRIGGER_RELEASE) return -1;
	const Action* action = Actions_find(action_id);
	if (!action) {
		LOG_warn("plugins: can't bind unknown action %s\n", action_id ? action_id : "(null)");
		return -1;
	}
	const ButtonInfo* info = NULL;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		if (buttons[i].nui == button) info = &buttons[i];
	}
	if (!info || pluginBindingCount[screen] >= MAX_PLUGIN_BINDINGS) return -1;

	Binding* b = &pluginBindings[screen][pluginBindingCount[screen]++];
	b->button = info->btn;
	b->trigger = trigger == NUI_TRIGGER_RELEASE ? TRIGGER_RELEASE : TRIGGER_PRESS;
	b->glyph = info->glyph;
	b->action = action;
	return 0;
}

int Plugins_bindings(int screen, const Binding** bindings) {
	if (screen < 0 || screen >= SCREEN_COUNT) return 0;
	*bindings = pluginBindings[screen];
	return pluginBindingCount[screen];
}

///////////////////////////////////////
// hooks

#define EACH_PLUGIN_WITH(field) \
	for (int i_ = 0; i_ < loaded_count; i_++) \
		for (const NUIPlugin* p = loaded[i_].plugin; p && Plugin_has(p, field) && p->field; p = NULL)

void Plugins_rootEntries(Array* root) {
	EACH_PLUGIN_WITH(root_entries) p->root_entries((NUIList*)root);
}

Array* Plugins_rootReplace(void) {
	for (int i = 0; i < loaded_count; i++) {
		const NUIPlugin* p = loaded[i].plugin;
		if (!loaded[i].owns_root) continue;
		// slot resolution lets only one plugin claim it
		if (!Plugin_has(p, root_replace) || !p->root_replace) return NULL;
		return (Array*)p->root_replace();
	}
	return NULL;
}

Array* Plugins_virtualDir(const char* path) {
	EACH_PLUGIN_WITH(virtual_dir) {
		NUIList* list = p->virtual_dir(path);
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
			if (keep && !p->filter((const NUIEntry*)entry, dir_path)) keep = 0;
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
			if (p->display_name((const NUIEntry*)entry, name, sizeof(name)) && name[0]) {
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
	EACH_PLUGIN_WITH(list_ready) p->list_ready((const NUIList*)entries, dir_path);
	int renamed = list_renamed;
	listing = was_listing;
	list_renamed = was_renamed;
	return renamed;
}

int Plugins_preLaunch(const char* path, int type) {
	// paks' own pre-launch.sh scripts first; they don't need plugins on
	if (!PakHooks_preLaunch(path, type)) return 0;

	NUILaunch launch = { .path = path, .type = type };
	EACH_PLUGIN_WITH(pre_launch) {
		if (p->pre_launch(&launch) == NUI_LAUNCH_CANCEL) {
			LOG_info("plugins: launch of %s cancelled by a plugin\n", path);
			return 0;
		}
	}
	if (loaded_count > 0) putFile(LAUNCHED_MARKER, "");
	return 1;
}

int Plugins_artPath(const Entry* entry, int kind, char* out, size_t size) {
	int nui_kind;
	switch (kind) {
		case ART_THUMBNAIL: nui_kind = NUI_ART_THUMBNAIL; break;
		case ART_FOLDER_BG: nui_kind = NUI_ART_FOLDER_BG; break;
		case ART_LIST_BG: nui_kind = NUI_ART_LIST_BG; break;
		default: return 0;
	}
	EACH_PLUGIN_WITH(art_path) {
		out[0] = '\0';
		if (p->art_path((const NUIEntry*)entry, nui_kind, out, size) && out[0]) return 1;
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
}

const NUIPlugin* Plugins_at(int i) {
	return (i >= 0 && i < loaded_count) ? loaded[i].plugin : NULL;
}
