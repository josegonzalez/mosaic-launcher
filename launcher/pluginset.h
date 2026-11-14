#ifndef PLUGINSET_H
#define PLUGINSET_H

// Deciding which discovered plugins load, and in what order. Pure: no files,
// no dlopen, so it can be tested on its own.

#include <stddef.h>

#define PLUGIN_MAX 64

enum PluginSettingType {
	SETTING_TOGGLE, // on or off
	SETTING_CHOICE, // one of options
	SETTING_TEXT,
};

// Markers from a setting line's flags field.
enum PluginSettingFlag {
	SETTING_PROTECTED = 1 << 0, // the Plugins page asks for the launcher's PIN first, when one is set
	SETTING_SECRET = 1 << 1,    // text only: never shown once set, only replaced
	SETTING_PUBLIC = 1 << 2,    // other plugins may read it with cfg_read; never on a protected or secret one
};

// One setting.<key>=<type>|<label>|<default>[|<options>[|<flags>]] line.
typedef struct PluginSetting {
	char key[48];
	int type;
	char label[64];
	char fallback[128]; // the default
	char options[256];  // comma-separated, for SETTING_CHOICE
	int flags;          // enum PluginSettingFlag bits
	char buttons[256];  // comma-separated ids of the actions it puts on a button, from button:<id> markers
} PluginSetting;

typedef struct PluginManifest {
	char id[64];
	char name[128];
	int api;
	char slots[256];     // comma-separated exclusive slots this plugin claims
	char requires[256];  // comma-separated ids that must also load
	char conflicts[256]; // comma-separated ids that can't load alongside it
	char optional[256];  // comma-separated ids it loads after when they load, and does without
	char provides[256];  // comma-separated capabilities it offers, for has_capability
	char ambient[256];   // comma-separated screens it makes ambient screens, for register_ambient
	char description[256]; // one line about it, for plugin_info
	char dir[512];       // the plugin's directory
	int bundled;         // 1: ships in Launcher.pak, off by default; 0: its own pak, on by default
	PluginSetting* settings; // as many as plugin.txt lists, allocated as they're parsed; Manifest_free frees them
	int setting_count;
} PluginManifest;

typedef struct PluginDecision {
	const PluginManifest* manifest;
	int load;
	char reason[160]; // why it isn't loading, when load is 0
} PluginDecision;

// Parses plugin.txt (key=value lines). Returns 0, or -1 when id is missing.
// out is zeroed or holds an earlier parse, whose settings are freed first.
// After a 0, Manifest_free frees its settings; after a -1 there's nothing
// to free.
int Manifest_parse(const char* text, PluginManifest* out);

// Frees the manifest's settings, leaving it with none.
void Manifest_free(PluginManifest* m);

// True when any of the plugin's settings is protected.
int Manifest_isProtected(const PluginManifest* m);

// True when the plugin's setting key is public, so other plugins may read
// it with cfg_read.
int Manifest_isPublic(const PluginManifest* m, const char* key);

// The key of the plugin's first setting marked button:<action_id>, the
// setting that says which button the plugin puts that action on, or NULL
// for none.
const char* Manifest_buttonSetting(const PluginManifest* m, const char* action_id);

// The index'th of a choice setting's comma-separated options, as the
// Plugins page lists them: leading spaces dropped, empty ones skipped.
// Returns 1, or 0 when there's no such option.
int Manifest_option(const char* options, int index, char* out, size_t size);

// Calls fn with each piece of a plugin's text the Plugins page shows, for
// translating under the plugin's id: its name, then each setting's label
// and, for a choice, each of its options, in order. Repeats are passed
// again. Values, such as defaults, are left out.
void Manifest_strings(const PluginManifest* m, void (*fn)(const char* text, void* data), void* data);

// Writes the plugin's resource folder, <its directory>/res, to out (size
// bytes), as host->plugin_info gives it. Returns 0, or -1 when it doesn't
// fit.
int Manifest_res(const PluginManifest* m, char* out, size_t size);

// Decides, given the user's choices from plugins.txt:
//
//   <id>=on|off       turn a plugin on or off; listed ids load first, in order
//   slot.<name>=<id>  which plugin owns an exclusive slot
//
// Fills out with one decision per manifest, in load order, and returns the
// count. choices may be NULL. Once it's decided which plugins load, each
// that does is moved after the loading plugins its optional= names, the
// rest keeping their order; optional= never changes which plugins load.
// Plugins that name each other load in their usual order, and
// PluginSet_orderBroken says which.
int PluginSet_resolve(const PluginManifest* manifests, int count, const char* choices, PluginDecision* out);

// After PluginSet_resolve: keeps loading only the plugins that claim one
// of slots (comma-separated), refusing the rest with why. The plugins they
// require aren't kept for them. Returns how many still load.
int PluginSet_onlySlots(PluginDecision* out, int count, const char* slots, const char* why);

// After PluginSet_resolve: the first loading plugin that out[index] names
// in optional= but loads after it, as they name each other, or NULL.
const char* PluginSet_orderBroken(const PluginDecision* out, int count, int index);

// True when id appears in a comma-separated list.
int csvHas(const char* csv, const char* id);

// Whether the user has a plugin on: the last <id>=on|off line in choices,
// or, without one, on unless it's bundled. The same rule PluginSet_resolve
// starts from.
int PluginSet_isOn(const char* choices, const PluginManifest* m);

// choices with the plugin's line set to on or off, written where the line
// already is so load order doesn't change, or added at the end. Returns a
// new string to free.
char* PluginSet_setChoice(const char* choices, const char* id, int on);

#endif
