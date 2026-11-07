#ifndef PLUGINS_H
#define PLUGINS_H

// Finding, loading and calling plugins. See sdk/nextui_plugin.h for the API
// they're written against.
//
// Plugins live in a directory holding plugin.txt and lib/<arch>/plugin.so:
//
//   Launcher.pak/plugins/<name>.plugin/   bundled, off unless turned on
//   Tools/<platform>/<name>.pak/          installed separately, on by default
//
// NEXTUI_LAUNCHER_PLUGIN_PATH, a colon-separated list of directories holding
// such plugin directories, replaces the bundled location (for desktop runs
// and tests).

#include <stddef.h>
#include "actions.h"
#include "nextui_plugin.h"

#define PLUGIN_STATE_DIR USERDATA_PATH "/launcher"

// True when the plugin's struct is new enough to have this field. A plugin
// built against an older header has a smaller struct, and reading past its
// end would be reading garbage.
#define Plugin_has(p, field) ((p)->size >= offsetof(NUIPlugin, field) + sizeof((p)->field))

// The loaded plugins, in load order.
const NUIPlugin* Plugins_at(int i);

// Bindings plugins added to a screen (an enum NUIScreen), after the
// screen's own. Returns how many, and points *bindings at them.
int Plugins_bindings(int screen, const Binding** bindings);
int Plugins_bindButton(int screen, int button, int trigger, const char* action_id);

// Loads plugins unless they're paused after a crash. Call once, before
// Menu_init, since building the root list already runs plugin hooks.
void Plugins_init(void);

// Call after the first frame is on screen. Until then, a crash leaves a
// marker that pauses plugins on the next start.
void Plugins_started(void);

void Plugins_quit(void);

// How many plugins loaded.
int Plugins_count(void);

#endif
