// Button remap: puts the game list's own actions, or plugins' actions, on
// the buttons the settings name. Each button's setting is a list of action
// ids tried in order, the first available one running, optionally after
// "press:", "release:" or "repeat:" (again while held), or "none" for
// nothing. Two presets build on it: Resume with A swaps A and X, and
// Resume on press makes X act on press.
//
// A, B, X, L1, R1 and SELECT are the game list's own, so a remap overrides
// them. Y, L2, R2 and START are free, so a remap binds them alongside other
// plugins' bindings there, and with two or more actions available the
// button opens a menu of them; with "repeat:" it overrides them instead.
// Buttons without a setting are left alone.
//
// FN1, FN2 and HOME each run an action id or a Tools pak their setting
// names, where the device has them and the user left them unassigned in
// stock's Settings.

#include <stdio.h>
#include <string.h>
#include "remap_model.h"
#include "mosaic_plugin.h"

#define ID "button-remap"

static const MosaicHost* host;
static RemapTable table;
static RemapCall calls[REMAP_CALLS_MAX];
static int call_count = 0;
static int bound = 0;

static int isOn(const char* key) {
	return !strcmp(host->cfg_get(key, "off"), "on");
}

static char message[320];
#define SAY(...) do { snprintf(message, sizeof(message), __VA_ARGS__); host->log(ID, message); } while (0)

static void readTable(void) {
	Remap_presets(&table, isOn("resume_with_a"), isOn("resume_on_press"));
	for (int button = 0; button < REMAP_BUTTONS; button++) {
		const char* key = Remap_key(button);
		const char* value = host->cfg_get(key, "");
		RemapButton b;
		int parsed = Remap_parse(value, &b);
		if (parsed == REMAP_SET) table.buttons[button] = b;
		else if (parsed == REMAP_BAD) SAY("can't read %s=%s, leaving the button as it was", key, value);
	}
	call_count = Remap_calls(&table, calls);
}

// FN1, FN2 and HOME, through the launcher's FN bindings: the user's own
// choice in stock's Settings wins, and the first plugin to bind a button
// keeps it, so the launcher may refuse one (and logs why). A press runs
// the action as any FN button does, through plugins' action gates.
static void bindFns(void) {
	for (int index = 0; index < REMAP_FNS; index++) {
		const char* key = Remap_fnKey(index);
		const char* value = host->cfg_get(key, "");
		char action[REMAP_FN_LEN];
		int parsed = Remap_parseFn(value, host->platform, action);
		if (parsed == REMAP_UNSET) continue;
		if (parsed == REMAP_BAD) {
			SAY("can't read %s=%s, leaving the button as it was", key, value);
			continue;
		}
		if (!MOSAIC_HOST_HAS(host, fn_release)) {
			SAY("this launcher can't bind FN buttons, so %s is left as it was", key);
			continue;
		}
		int button = MOSAIC_BUTTON_FN1 + index;
		if (!host->fn_name(button)[0]) {
			SAY("this device has no %s button, so %s does nothing", key, key);
			continue;
		}
		host->fn_bind(button, action);
	}
}

static int init(const MosaicHost* h) {
	host = h;
	bound = 0;
	readTable();
	bindFns();
	for (int i = 0; i < call_count; i++) {
		const RemapCall* c = &calls[i];
		if (c->bind) continue; // on the first tick, once every plugin has registered its actions
		const char* key = Remap_key(c->button);
		if (!MOSAIC_HOST_HAS(host, override_button)) {
			SAY("this launcher can't override %s; only Y, L2, R2 and START can be remapped", key);
			continue;
		}
		int trigger = c->trigger;
		if (trigger == MOSAIC_TRIGGER_REPEAT && !MOSAIC_HOST_HAS(host, override_repeat)) {
			SAY("this launcher can't repeat %s while held; it acts once per press", key);
			trigger = MOSAIC_TRIGGER_PRESS;
		}
		if (host->override_button(MOSAIC_SCREEN_GAMELIST, c->button, trigger, c->action) != 0) {
			SAY("couldn't put %s on %s: another plugin has the button, or the action isn't registered yet "
				"(a plugin's action needs button-remap after that plugin in plugins.txt)",
				c->action ? c->action : "none", key);
		}
	}
	return 0;
}

static void tick(unsigned long now) {
	(void)now;
	if (bound) return;
	bound = 1;
	for (int i = 0; i < call_count; i++) {
		const RemapCall* c = &calls[i];
		if (!c->bind) continue;
		if (host->bind_button(MOSAIC_SCREEN_GAMELIST, c->button, c->trigger, c->action) != 0)
			SAY("couldn't put %s on %s: no plugin that's on has that action", c->action, Remap_key(c->button));
	}
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
