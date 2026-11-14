#include <stdint.h>
#include <sys/wait.h>
#include "launcher.h"
#include "cfgfile.h"
#include "pin.h"
#include "pluginorder.h"
#include "settings.h"
#include "drawmodel.h"
#include "lockmodel.h"
#include "ambientmodel.h"

// Anything that mounts, unmounts or edits the boot hook goes through
// Launcher.pak/bin/manage.sh, so the rules live in one place and stay
// testable from shell. Plugin choices and settings are plain files, written
// here.
//
// Settings a plugin marks protected ask for the launcher's PIN, when one is
// set, before they're shown or changed, and so does turning off a plugin
// with any (or the whole launcher, while one is on). The right PIN unlocks
// them all until the user leaves the pak. A wrong one only says so: there's
// no lockout. Secret text settings are never shown once set, only replaced.
//
// Only the strings and theme slots' owners load here (Plugins_initSettings),
// so the pages' text goes through TR() and a plugin's name, labels and
// options through Tr_plugin() as its own text, the same as host->tr. Values
// the user typed show as typed.

enum MenuKind { MENU_MAIN, MENU_PLUGINS, MENU_PLUGIN, MENU_CHOICE, MENU_AMBIENT };

enum RowKind {
	ROW_INFO,      // can't be selected
	ROW_HEADER,    // a group's name on the Plugins page; can't be selected
	ROW_TRY_AGAIN,
	ROW_PLUGINS,
	ROW_TURN_ON,
	ROW_TURN_OFF,
	ROW_PLUGIN,    // arg: manifest index
	ROW_STATUS,    // the plugin's on/off
	ROW_SETTING,   // arg: setting index
	ROW_OPTION,    // a choice setting's option
	ROW_PIN,       // the launcher's PIN
	ROW_LAUNCHER,  // one of the launcher's own choices; arg: its index in launcher_settings
	ROW_AMBIENT,   // opens the ambient screens' order
	ROW_AMBIENT_SCREEN, // arg: its place in the order
};

typedef struct Row {
	int kind;
	int arg;
	char label[128];
	char value[128];
	char option[128]; // a choice's option as saved, for ROW_OPTION
} Row;

typedef struct Menu {
	int kind;
	int plugin;  // manifest index, for MENU_PLUGIN and MENU_CHOICE
	int setting; // setting index, for MENU_CHOICE
	int selected;
	int start;   // first visible row
} Menu;

#define MENU_DEPTH 4

static Menu stack[MENU_DEPTH];
static int depth = 0;
static Row* rows = NULL; // grown to fit the longest page, such as a plugin with many settings
static int row_count = 0;
static int row_room = 0;
static char title[128];
static const PluginManifest* manifests;
static int manifest_count = 0;
static char pak[512];
static int done = 0;
static int unlocked = 0; // the PIN was typed since the pak opened

// The launcher's own settings on its main page, in LAUNCHER_SETTINGS_PATH:
// the built-in lock's.
typedef struct LauncherSetting {
	const char* key;
	const char* label;
	const char* fallback;
	const char* options; // comma-separated; a toggle's are on,off
	int toggle;
} LauncherSetting;

static const LauncherSetting launcher_settings[] = {
	{ LOCK_CHORD_KEY, TR_MARK("Lock chord"), LOCK_CHORD_DEFAULT, LOCK_CHORD_OPTIONS, 0 },
	{ LOCK_HOLD_KEY, TR_MARK("Hold the chord for (seconds)"), LOCK_HOLD_DEFAULT, LOCK_HOLD_OPTIONS, 0 },
	{ LOCK_SWITCH_KEY, TR_MARK("Lock with the hold switch"), LOCK_SWITCH_DEFAULT, "on,off", 1 },
};
#define LAUNCHER_SETTING_COUNT (int)(sizeof(launcher_settings) / sizeof(launcher_settings[0]))

// The ambient screens page: the lock and the ambient= screens of the
// plugins that are on, in the user's order.
static char ambient_ids[AMBIENT_MAX][AMBIENT_ID_MAX];
static char ambient_labels[AMBIENT_MAX][128];
static int ambient_count = 0;
static int ambient_moving = 0; // A picked the selected one up, so up and down move it

///////////////////////////////////////
// the launcher's state, through manage.sh

typedef struct Status {
	char status[32];
	int enabled;
	int try_again;
	char release[128];
} Status;

static void readStatus(Status* s) {
	memset(s, 0, sizeof(*s));
	snprintf(s->status, sizeof(s->status), "off");
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "\"%s/bin/manage.sh\" status", pak);
	FILE* p = popen(cmd, "r");
	if (!p) return;
	char line[256];
	while (fgets(line, sizeof(line), p)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!strncmp(line, "status=", 7)) snprintf(s->status, sizeof(s->status), "%s", line + 7);
		else if (!strncmp(line, "enabled=", 8)) s->enabled = atoi(line + 8);
		else if (!strncmp(line, "try_again=", 10)) s->try_again = atoi(line + 10);
		else if (!strncmp(line, "release=", 8)) snprintf(s->release, sizeof(s->release), "%s", line + 8);
	}
	pclose(p);
}

static const char* statusText(const Status* s) {
	if (!strcmp(s->status, "unsupported")) return TR("Waiting for a supported NextUI");
	if (!strcmp(s->status, "fallback")) return TR("Paused after crashes - using stock");
	if (!strcmp(s->status, "paused")) return TR("On, plugins paused after a crash");
	if (!strcmp(s->status, "on")) return TR("On");
	if (!strcmp(s->status, "pending")) return TR("On after the next reboot");
	return TR("Off - using the stock launcher");
}

// Returns manage.sh's exit code, or -1 if it didn't run.
static int manage(const char* command) {
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "\"%s/bin/manage.sh\" %s", pak, command);
	int rc = system(cmd);
	if (rc == -1 || !WIFEXITED(rc)) return -1;
	return WEXITSTATUS(rc);
}

static void turnOn(void) {
	Status s;
	int rc = manage("turn-on");
	if (rc == 0) Modal_message(TR("Launcher is on.\nIt starts when you leave this screen."));
	else if (rc == 3) {
		readStatus(&s);
		char text[256];
		snprintf(text, sizeof(text), "%s\n%s: %s\n%s", TR("This build supports a different NextUI release."),
			TR("Installed"), s.release[0] ? s.release : TR("unknown"), TR("Install a Launcher build for this release."));
		Modal_message(text);
	}
	else Modal_message(TR("Launcher could not start.\nThe stock launcher is still in use."));
}

static void turnOff(void) {
	manage("turn-off");
	Modal_message(TR("Launcher is off.\nThe stock launcher is back."));
}

static void tryAgain(void) {
	manage("try-again");
	Modal_message(TR("Crash guard reset.\nThe launcher and its plugins start when you leave this screen."));
}

///////////////////////////////////////
// plugin choices and settings

static int pluginOn(const PluginManifest* m) {
	char* choices = allocFile(PLUGIN_CHOICES_PATH);
	int on = PluginSet_isOn(choices, m);
	free(choices);
	return on;
}

// The launcher reads this the next time it starts, which is when the user
// leaves the pak.
static void setPluginOn(const PluginManifest* m, int on) {
	char* choices = allocFile(PLUGIN_CHOICES_PATH);
	char* next = PluginSet_setChoice(choices, m->id, on);
	free(choices);
	Plugins_ensureStateDir();
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", PLUGIN_CHOICES_PATH);
	FILE* f = fopen(tmp, "w");
	if (f) {
		fputs(next, f);
		fclose(f);
		if (rename(tmp, PLUGIN_CHOICES_PATH) != 0) LOG_warn("settings: couldn't save %s\n", PLUGIN_CHOICES_PATH);
	}
	free(next);
}

///////////////////////////////////////
// the PIN

// Whether protected things need the PIN first.
static int locked(void) {
	return !unlocked && Pin_isSet(PIN_PATH);
}

static int settingLocked(const PluginSetting* s) {
	return (s->flags & SETTING_PROTECTED) && locked();
}

// Turning this plugin off needs the PIN first.
static int offLocked(const PluginManifest* m) {
	return Manifest_isProtected(m) && pluginOn(m) && locked();
}

// Turning the launcher off needs the PIN first: it turns every plugin off.
static int launcherOffLocked(void) {
	if (!locked()) return 0;
	for (int i = 0; i < manifest_count; i++) {
		if (Manifest_isProtected(&manifests[i]) && pluginOn(&manifests[i])) return 1;
	}
	return 0;
}

static void buildRows(void);
static void activate(int step);

// After the PIN, the press that asked for it goes ahead, on the same row.
static void pinTyped(const char* text, void* data) {
	if (!text) return; // cancelled
	if (!Pin_matches(PIN_PATH, text)) {
		Modal_message(TR("Wrong PIN"));
		return;
	}
	unlocked = 1;
	buildRows();
	activate((int)(intptr_t)data);
}

static void askPin(int step) {
	Modal_keyboardHidden(TR("PIN"), pinTyped, (void*)(intptr_t)step);
}

static void newPin(const char* text, void* data) {
	(void)data;
	if (!text) return; // cancelled
	char pin[PIN_LEN];
	Pin_clean(text, pin);
	Plugins_ensureStateDir();
	if (Pin_write(PIN_PATH, pin) != 0) {
		LOG_warn("settings: couldn't save the PIN in %s\n", PIN_PATH);
		Modal_message(TR("The PIN could not be saved."));
	}
	else {
		unlocked = 1; // whoever set it knows it
		Modal_message(pin[0] ? TR("PIN set.\nSimple mode and protected settings ask for it.") : TR("PIN removed."));
	}
	memset(pin, 0, sizeof(pin));
	buildRows();
}

static void cfgPath(const PluginManifest* m, char* out, size_t size) {
	snprintf(out, size, PLUGIN_STATE_DIR "/%s.cfg", m->id);
}

static void settingValue(const PluginManifest* m, const PluginSetting* s, char* out, size_t size) {
	char path[600];
	cfgPath(m, path, sizeof(path));
	Cfg_get(path, s->key, s->fallback, out, size);
}

static void setSetting(const PluginManifest* m, const PluginSetting* s, const char* value) {
	Plugins_ensureStateDir();
	char path[600];
	cfgPath(m, path, sizeof(path));
	if (Cfg_set(path, s->key, value) != 0) LOG_warn("settings: couldn't save %s in %s\n", s->key, path);
}

// The option after (or before) current in a comma-separated list, wrapping.
static void nextOption(const char* options, const char* current, int step, char* out, size_t size) {
	char list[256];
	snprintf(list, sizeof(list), "%s", options);
	char* items[32];
	int n = 0;
	for (char* o = strtok(list, ","); o && n < 32; o = strtok(NULL, ",")) {
		while (*o == ' ') o++;
		if (*o) items[n++] = o;
	}
	if (!n) return;
	int at = -1;
	for (int i = 0; i < n; i++) {
		if (!strcmp(items[i], current)) at = i;
	}
	at = at == -1 ? 0 : (at + step + n) % n;
	snprintf(out, size, "%s", items[at]);
}

static void launcherValue(const LauncherSetting* l, char* out, size_t size) {
	Cfg_get(LAUNCHER_SETTINGS_PATH, l->key, l->fallback, out, size);
}

static void setLauncherValue(const LauncherSetting* l, const char* value) {
	Plugins_ensureStateDir();
	if (Cfg_set(LAUNCHER_SETTINGS_PATH, l->key, value) != 0) LOG_warn("settings: couldn't save %s in %s\n", l->key, LAUNCHER_SETTINGS_PATH);
}

// The ambient screens there are, in the saved order.
static void listAmbient(void) {
	static char kept[AMBIENT_MAX][AMBIENT_ID_MAX];
	const char* ids[AMBIENT_MAX];
	char labels[AMBIENT_MAX][128];
	int n = 0;
	ids[n] = AMBIENT_LOCK;
	snprintf(labels[n++], sizeof(labels[0]), "%s", TR("Lock"));
	for (int i = 0; i < manifest_count && n < AMBIENT_MAX; i++) {
		const PluginManifest* m = &manifests[i];
		if (!m->ambient[0] || !pluginOn(m)) continue;
		char list[256];
		snprintf(list, sizeof(list), "%s", m->ambient);
		int several = strchr(list, ',') != NULL;
		for (char* id = strtok(list, ","); id && n < AMBIENT_MAX; id = strtok(NULL, ",")) {
			while (*id == ' ') id++;
			if (!*id || strlen(id) >= AMBIENT_ID_MAX) continue;
			snprintf(kept[n], AMBIENT_ID_MAX, "%s", id);
			ids[n] = kept[n];
			const char* name = Tr_plugin(m->id, NULL, m->name);
			const char* dot = strchr(id, '.');
			if (several) snprintf(labels[n], sizeof(labels[0]), "%s (%s)", name, dot ? dot + 1 : id);
			else snprintf(labels[n], sizeof(labels[0]), "%s", name);
			n++;
		}
	}
	char setting[512];
	Cfg_get(LAUNCHER_SETTINGS_PATH, AMBIENT_ORDER_KEY, "", setting, sizeof(setting));
	int order[AMBIENT_MAX];
	Ambient_order(setting, ids, n, order);
	for (int i = 0; i < n; i++) {
		snprintf(ambient_ids[i], AMBIENT_ID_MAX, "%s", ids[order[i]]);
		snprintf(ambient_labels[i], sizeof(ambient_labels[0]), "%s", labels[order[i]]);
	}
	ambient_count = n;
}

// Moves the selected ambient screen one step, saving the order.
static void moveAmbient(int step) {
	Menu* m = &stack[depth - 1];
	const char* shown[AMBIENT_MAX];
	for (int i = 0; i < ambient_count; i++) shown[i] = ambient_ids[i];
	char setting[512], next[512];
	Cfg_get(LAUNCHER_SETTINGS_PATH, AMBIENT_ORDER_KEY, "", setting, sizeof(setting));
	int to = Ambient_move(setting, shown, ambient_count, m->selected, step, next, sizeof(next));
	if (to < 0) return;
	Plugins_ensureStateDir();
	if (Cfg_set(LAUNCHER_SETTINGS_PATH, AMBIENT_ORDER_KEY, next) != 0) {
		LOG_warn("settings: couldn't save %s in %s\n", AMBIENT_ORDER_KEY, LAUNCHER_SETTINGS_PATH);
		return;
	}
	m->selected = to;
}

///////////////////////////////////////
// menus

static Menu* top(void) {
	return &stack[depth - 1];
}

static Row* addRow(int kind, int arg, const char* label, const char* value) {
	if (row_count >= row_room) {
		int room = row_room ? row_room * 2 : 32;
		Row* grown = realloc(rows, room * sizeof(*grown));
		if (!grown) return NULL;
		rows = grown;
		row_room = room;
	}
	Row* r = &rows[row_count++];
	r->kind = kind;
	r->arg = arg;
	Draw_copy(r->label, sizeof(r->label), label);
	Draw_copy(r->value, sizeof(r->value), value ? value : "");
	r->option[0] = '\0';
	return r;
}

// A plugin's own text from its plugin.txt (its name, a setting's label or a
// choice's option), under its id with no context, as host->tr gives
// plugins theirs.
static const char* pluginText(const PluginManifest* m, const char* text) {
	return Tr_plugin(m->id, NULL, text);
}

// A choice's value as shown: one of its options is the plugin's text, and
// anything else stays as it is.
static const char* choiceShown(const PluginManifest* m, const PluginSetting* s, const char* value) {
	char option[128];
	for (int i = 0; Manifest_option(s->options, i, option, sizeof(option)); i++) {
		if (!strcmp(option, value)) return pluginText(m, value);
	}
	return value;
}

static int selectable(const Row* r) {
	return r->kind != ROW_INFO && r->kind != ROW_HEADER;
}

static const char* onOff(int on) {
	return on ? TR("On") : TR("Off");
}

static void buildRows(void) {
	Menu* m = top();
	row_count = 0;
	if (m->kind == MENU_MAIN) {
		Status s;
		readStatus(&s);
		snprintf(title, sizeof(title), "%s", TR("Launcher"));
		addRow(ROW_INFO, 0, statusText(&s), NULL);
		if (s.try_again) addRow(ROW_TRY_AGAIN, 0, TR("Try again"), NULL);
		if (manifest_count) addRow(ROW_PLUGINS, 0, TR("Plugins"), NULL);
		for (int i = 0; i < LAUNCHER_SETTING_COUNT; i++) {
			const LauncherSetting* l = &launcher_settings[i];
			char value[64];
			launcherValue(l, value, sizeof(value));
			// on and off as the toggles show them; the other choices as saved
			int on_off = l->toggle || !strcmp(value, "off");
			addRow(ROW_LAUNCHER, i, TR(l->label), on_off ? onOff(!strcmp(value, "on")) : value);
		}
		addRow(ROW_AMBIENT, 0, TR("Ambient screens"), NULL);
		if (s.enabled) addRow(ROW_TURN_OFF, 0, TR("Turn off"), NULL);
		else addRow(ROW_TURN_ON, 0, TR("Turn on"), NULL);
	}
	else if (m->kind == MENU_PLUGINS) {
		snprintf(title, sizeof(title), "%s", TR("Plugins"));
		PluginPageRow order[PLUGIN_MAX + PLUGIN_GROUP_COUNT + 1];
		int n = PluginOrder_rows(manifests, manifest_count, order, (int)(sizeof(order) / sizeof(order[0])));
		for (int i = 0; i < n; i++) {
			int at = order[i].plugin;
			if (at < 0) addRow(ROW_HEADER, order[i].group, TR(PluginOrder_title(order[i].group)), NULL);
			else addRow(ROW_PLUGIN, at, pluginText(&manifests[at], manifests[at].name), onOff(pluginOn(&manifests[at])));
		}
		addRow(ROW_PIN, 0, TR("PIN"), Pin_isSet(PIN_PATH) ? TR("Set") : TR("Not set"));
	}
	else if (m->kind == MENU_PLUGIN) {
		const PluginManifest* p = &manifests[m->plugin];
		snprintf(title, sizeof(title), "%s", pluginText(p, p->name));
		addRow(ROW_STATUS, 0, TR("Status"), onOff(pluginOn(p)));
		for (int i = 0; i < p->setting_count; i++) {
			const PluginSetting* s = &p->settings[i];
			const char* label = pluginText(p, s->label);
			char value[128];
			settingValue(p, s, value, sizeof(value));
			if (settingLocked(s)) addRow(ROW_SETTING, i, label, TR("Locked"));
			else if (s->flags & SETTING_SECRET) addRow(ROW_SETTING, i, label, value[0] ? TR("Set") : TR("Not set"));
			else if (s->type == SETTING_TOGGLE) addRow(ROW_SETTING, i, label, onOff(!strcmp(value, "on")));
			else if (s->type == SETTING_CHOICE) addRow(ROW_SETTING, i, label, choiceShown(p, s, value));
			else addRow(ROW_SETTING, i, label, value); // typed, so shown as typed
			memset(value, 0, sizeof(value));
		}
	}
	else if (m->kind == MENU_AMBIENT) {
		snprintf(title, sizeof(title), "%s", TR("Ambient screens"));
		listAmbient();
		for (int i = 0; i < ambient_count; i++) {
			addRow(ROW_AMBIENT_SCREEN, i, ambient_labels[i], ambient_moving && i == m->selected ? TR("Moving") : NULL);
		}
	}
	else if (m->kind == MENU_CHOICE) {
		const PluginManifest* p = &manifests[m->plugin];
		const PluginSetting* s = &p->settings[m->setting];
		snprintf(title, sizeof(title), "%s", pluginText(p, s->label));
		char option[128];
		for (int i = 0; Manifest_option(s->options, i, option, sizeof(option)); i++) {
			Row* r = addRow(ROW_OPTION, 0, pluginText(p, option), NULL);
			if (r) Draw_copy(r->option, sizeof(r->option), option);
		}
	}

	// keep the selection on a row that can be selected
	if (m->selected >= row_count) m->selected = row_count - 1;
	if (m->selected < 0) m->selected = 0;
	while (m->selected < row_count - 1 && !selectable(&rows[m->selected])) m->selected++;
	state.dirty = 1;
}

static void push(int kind, int plugin, int setting) {
	if (depth >= MENU_DEPTH) return;
	stack[depth++] = (Menu){ .kind = kind, .plugin = plugin, .setting = setting };
	if (kind == MENU_CHOICE) {
		// start on the current value
		const PluginManifest* p = &manifests[plugin];
		char value[128];
		settingValue(p, &p->settings[setting], value, sizeof(value));
		buildRows();
		for (int i = 0; i < row_count; i++) {
			if (!strcmp(rows[i].option, value)) top()->selected = i;
		}
	}
	buildRows();
}

static void pop(void) {
	ambient_moving = 0;
	if (depth <= 1) {
		done = 1;
		return;
	}
	depth--;
	buildRows();
}

// The text setting the keyboard is open for.
static int typing_plugin = 0;
static int typing_setting = 0;

static void typed(const char* text, void* data) {
	(void)data;
	const PluginManifest* p = &manifests[typing_plugin];
	if (text) setSetting(p, &p->settings[typing_setting], text);
	buildRows();
}

// A, or left and right (step -1 or 1) on rows with a value to cycle.
static void activate(int step) {
	Menu* m = top();
	if (!row_count) return;
	Row* r = &rows[m->selected];
	int cycling = step != 0;
	switch (r->kind) {
		case ROW_TRY_AGAIN: if (!cycling) tryAgain(); break;
		case ROW_PLUGINS: if (!cycling) push(MENU_PLUGINS, 0, 0); break;
		case ROW_LAUNCHER: {
			const LauncherSetting* l = &launcher_settings[r->arg];
			char value[64], next[64];
			launcherValue(l, value, sizeof(value));
			snprintf(next, sizeof(next), "%s", value);
			nextOption(l->options, value, cycling ? step : 1, next, sizeof(next));
			setLauncherValue(l, next);
			break;
		}
		case ROW_AMBIENT: if (!cycling) push(MENU_AMBIENT, 0, 0); break;
		case ROW_AMBIENT_SCREEN: if (!cycling) ambient_moving = !ambient_moving; break;
		case ROW_TURN_ON: if (!cycling) turnOn(); break;
		case ROW_TURN_OFF:
			if (cycling) break;
			if (launcherOffLocked()) {
				askPin(step);
				return;
			}
			turnOff();
			break;
		case ROW_PLUGIN: {
			const PluginManifest* p = &manifests[r->arg];
			if (p->setting_count && !cycling) push(MENU_PLUGIN, r->arg, 0);
			else if (offLocked(p)) {
				askPin(step);
				return;
			}
			else setPluginOn(p, !pluginOn(p));
			break;
		}
		case ROW_STATUS: {
			const PluginManifest* p = &manifests[m->plugin];
			if (offLocked(p)) {
				askPin(step);
				return;
			}
			setPluginOn(p, !pluginOn(p));
			break;
		}
		case ROW_PIN:
			if (cycling) break;
			if (locked()) {
				askPin(step);
				return;
			}
			Modal_keyboard(TR("New PIN, empty for none"), "", newPin, NULL);
			return;
		case ROW_SETTING: {
			const PluginManifest* p = &manifests[m->plugin];
			const PluginSetting* s = &p->settings[r->arg];
			if (settingLocked(s)) {
				askPin(step);
				return;
			}
			char value[128];
			settingValue(p, s, value, sizeof(value));
			if (s->type == SETTING_TOGGLE) setSetting(p, s, strcmp(value, "on") ? "on" : "off");
			else if (s->type == SETTING_CHOICE) {
				if (!cycling) {
					push(MENU_CHOICE, m->plugin, r->arg);
					return;
				}
				char next[128];
				snprintf(next, sizeof(next), "%s", value);
				nextOption(s->options, value, step, next, sizeof(next));
				setSetting(p, s, next);
			}
			else if (!cycling) {
				// a secret is replaced, never shown, so its keyboard starts empty
				typing_plugin = m->plugin;
				typing_setting = r->arg;
				Modal_keyboard(pluginText(p, s->label), s->flags & SETTING_SECRET ? "" : value, typed, NULL);
				memset(value, 0, sizeof(value));
				return;
			}
			break;
		}
		case ROW_OPTION: {
			if (cycling) break;
			const PluginManifest* p = &manifests[m->plugin];
			setSetting(p, &p->settings[m->setting], r->option);
			pop();
			return;
		}
	}
	buildRows();
}

static void move(int step) {
	Menu* m = top();
	if (row_count < 2) return;
	int at = m->selected;
	for (int tries = 0; tries < row_count; tries++) {
		at = (at + step + row_count) % row_count;
		if (selectable(&rows[at])) break;
	}
	m->selected = at;
	state.dirty = 1;
}

static void handleInput(void) {
	if (ambient_moving && top()->kind == MENU_AMBIENT) {
		// up and down move the one picked up; A or B puts it down
		if (PAD_justRepeated(BTN_UP)) moveAmbient(-1);
		else if (PAD_justRepeated(BTN_DOWN)) moveAmbient(1);
		else if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) ambient_moving = 0;
		else return;
		buildRows();
		return;
	}
	if (PAD_justRepeated(BTN_UP)) move(-1);
	else if (PAD_justRepeated(BTN_DOWN)) move(1);
	else if (PAD_justRepeated(BTN_LEFT)) activate(-1);
	else if (PAD_justRepeated(BTN_RIGHT)) activate(1);
	else if (PAD_justPressed(BTN_A)) activate(0);
	else if (PAD_justPressed(BTN_B)) pop();
}

///////////////////////////////////////
// drawing, in the game list's style

static void blitRowText(TTF_Font* f, const char* text, SDL_Color color, int x, int y, int max_w) {
	if (!text[0] || max_w <= 0) return;
	SDL_Surface* s = TTF_RenderUTF8_Blended(f, text, color);
	if (!s) return;
	SDL_Rect src = { 0, 0, MIN(s->w, max_w), s->h };
	SDL_Rect dst = { x, y + ((SCALE1(PILL_SIZE) - s->h + 1) >> 1) };
	SDL_BlitSurface(s, &src, screen, &dst);
	SDL_FreeSurface(s);
}

static const char* confirmLabel(void) {
	if (!row_count) return NULL;
	const Row* r = &rows[top()->selected];
	switch (r->kind) {
		case ROW_INFO: return NULL;
		case ROW_HEADER: return NULL;
		case ROW_PLUGIN: return manifests[r->arg].setting_count ? TR("OPEN") : TR("CHANGE");
		case ROW_STATUS: return TR("CHANGE");
		case ROW_SETTING: return TR("CHANGE");
		case ROW_PIN: return TR("CHANGE");
		case ROW_LAUNCHER: return TR("CHANGE");
		case ROW_AMBIENT: return TR("OPEN");
		case ROW_AMBIENT_SCREEN: return ambient_moving ? TR("DONE") : TR("MOVE");
		default: return TR("SELECT");
	}
}

static void render(int ow) {
	Menu* m = top();
	SDL_Color normal = uintToColour(THEME_COLOR4_255);
	SDL_Color picked = uintToColour(THEME_COLOR5_255);
	int left = SCALE1(BUTTON_MARGIN);
	int width = screen->w - SCALE1(BUTTON_MARGIN * 2);
	int pad = SCALE1(BUTTON_PADDING);

	// the title takes the top row, beside the battery and clock
	char shown[256];
	Text_truncate(font.large, title, shown, sizeof(shown), screen->w - SCALE1(PADDING * 2) - ow, pad * 2);
	blitRowText(font.large, shown, normal, left + pad, SCALE1(PADDING), screen->w);

	// not MAX(): NextUI's has no outer parentheses, so a comparison with it
	// is always true on a list longer than the screen
	int visible = MAIN_ROW_COUNT - 1;
	int last_start = row_count > visible ? row_count - visible : 0;
	if (m->selected < m->start) m->start = m->selected;
	if (m->selected >= m->start + visible) m->start = m->selected - visible + 1;
	// a group's first plugin brings its header along
	if (m->start == m->selected && m->start > 0 && rows[m->start - 1].kind == ROW_HEADER) m->start--;
	if (m->start > last_start) m->start = last_start;

	for (int i = m->start, j = 1; i < row_count && j <= visible; i++, j++) {
		const Row* r = &rows[i];
		int y = SCALE1(PADDING + j * PILL_SIZE);
		if (r->kind == ROW_HEADER) {
			char label[256];
			int label_w = Text_truncate(font.small, r->label, label, sizeof(label), width, pad * 2);
			blitRowText(font.small, label, normal, left + pad, y, label_w - pad * 2);
			continue;
		}
		int selected = i == m->selected && selectable(r);

		int value_w = 0;
		char value[128] = "";
		if (r->value[0]) value_w = Text_truncate(font.small, r->value, value, sizeof(value), width / 2, pad * 2);
		char label[256];
		int label_w = Text_truncate(font.large, r->label, label, sizeof(label), width - value_w, pad * 2);

		// rows with a value get a full-width pill, like the stock settings
		if (selected) {
			SDL_Rect pill = { left, y, value_w ? width : label_w, SCALE1(PILL_SIZE) };
			GFX_blitPillDark(ASSET_WHITE_PILL, screen, &pill);
		}
		SDL_Color color = selected ? picked : normal;
		blitRowText(font.large, label, color, left + pad, y, label_w - pad * 2);
		if (value_w) blitRowText(font.small, value, color, left + width - value_w + pad, y, value_w - pad * 2);
	}

	if (show_setting) Text_hardwareHints(screen, show_setting);
	const char* confirm = confirmLabel();
	if (confirm) GFX_blitButtonGroup((char*[]){ "B", TR("BACK"), "A", (char*)confirm, NULL }, 1, screen, 1);
	else GFX_blitButtonGroup((char*[]){ "B", TR("BACK"), NULL }, 0, screen, 1);
}

///////////////////////////////////////

int Settings_main(int argc, char* argv[]) {
	if (!Plugins_pakDir(pak, sizeof(pak))) {
		LOG_error("settings: can't tell where Launcher.pak is\n");
		return 1;
	}
	manifest_count = Plugins_discover(&manifests);
	if (manifest_count) Plugins_adoptPin();

	InitSettings();
	screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	if (!HAS_POWER_BUTTON) PWR_disableSleep();
	GFX_setVsync(VSYNC_STRICT);
	// the strings and theme slots' owners, so the pages are in their
	// language and look, with the fallback font
	Plugins_initSettings();

	push(argc > 0 && !strcmp(argv[0], "plugins") ? MENU_PLUGINS : MENU_MAIN, 0, 0);
	currentScreen = SCREEN_SETTINGS;
	show_setting = 0;
	PAD_reset();
	GFX_clearLayers(LAYER_ALL);
	GFX_clear(screen);
	state.dirty = 1;

	while (!done && !state.quit) {
		GFX_startFrame();
		now = SDL_GetTicks();
		PAD_poll();
		PWR_update(&state.dirty, &show_setting, NULL, NULL);

		if (currentScreen == SCREEN_MODAL) Modal_handleInput();
		else handleInput();

		if (state.dirty && !done) {
			GFX_clearLayers(LAYER_ALL);
			GFX_clear(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			if (currentScreen == SCREEN_MODAL) Modal_render();
			else render(ow);
			GFX_flip(screen);
			state.dirty = 0;
		}
		else GFX_sync();
	}

	Plugins_quit(); // frees the manifests
	manifests = NULL;
	manifest_count = 0;
	free(rows);
	rows = NULL;
	row_count = row_room = 0;
	PWR_quit();
	PAD_quit();
	GFX_quit();
	QuitSettings();
	return 0;
}
