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

///////////////////////////////////////
// Hooks, called from the launcher. Each runs every loaded plugin's hook in
// load order; with no plugins loaded they change nothing.

// Appends plugins' entries to the root list.
void Plugins_rootEntries(Array* root);

// The first plugin list for a path it owns, or NULL.
Array* Plugins_virtualDir(const char* path);

// Drops entries any plugin hides. Takes ownership of entries.
Array* Plugins_filter(Array* entries, const char* dir_path);

// Lets plugins rename entries. Returns 1 if any name changed.
int Plugins_displayNames(Array* entries);

// Asks plugins before a launch. Returns 1 to go ahead, 0 if one cancelled.
// Going ahead also arranges for post_return on the next start.
int Plugins_preLaunch(const char* path, int type);

// The first plugin's image for an entry, for ART_THUMBNAIL, ART_FOLDER_BG or
// ART_LIST_BG (see art.h). Returns 1 if a plugin answered.
int Plugins_artPath(const Entry* entry, int kind, char* out, size_t size);

void Plugins_tick(unsigned long now);
void Plugins_beforeSleep(void);
void Plugins_afterResume(void);
void Plugins_beforePoweroff(void);

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
