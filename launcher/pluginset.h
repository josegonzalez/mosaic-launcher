#ifndef PLUGINSET_H
#define PLUGINSET_H

// Deciding which discovered plugins load, and in what order. Pure: no files,
// no dlopen, so it can be tested on its own.

#define PLUGIN_MAX 64
#define PLUGIN_SETTINGS_MAX 8

enum PluginSettingType {
	SETTING_TOGGLE, // on or off
	SETTING_CHOICE, // one of options
	SETTING_TEXT,
};

// One setting.<key>=<type>|<label>|<default>[|<options>] line.
typedef struct PluginSetting {
	char key[48];
	int type;
	char label[64];
	char fallback[128]; // the default
	char options[256];  // comma-separated, for SETTING_CHOICE
} PluginSetting;

typedef struct PluginManifest {
	char id[64];
	char name[128];
	int api;
	char slots[256];     // comma-separated exclusive slots this plugin claims
	char requires[256];  // comma-separated ids that must also load
	char conflicts[256]; // comma-separated ids that can't load alongside it
	char dir[512];       // the plugin's directory
	int bundled;         // 1: ships in Launcher.pak, off by default; 0: its own pak, on by default
	PluginSetting settings[PLUGIN_SETTINGS_MAX];
	int setting_count;
} PluginManifest;

typedef struct PluginDecision {
	const PluginManifest* manifest;
	int load;
	char reason[160]; // why it isn't loading, when load is 0
} PluginDecision;

// Parses plugin.txt (key=value lines). Returns 0, or -1 when id is missing.
int Manifest_parse(const char* text, PluginManifest* out);

// Decides, given the user's choices from plugins.txt:
//
//   <id>=on|off       turn a plugin on or off; listed ids load first, in order
//   slot.<name>=<id>  which plugin owns an exclusive slot
//
// Fills out with one decision per manifest, in load order, and returns the
// count. choices may be NULL.
int PluginSet_resolve(const PluginManifest* manifests, int count, const char* choices, PluginDecision* out);

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
