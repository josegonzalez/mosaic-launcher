#ifndef PLUGINSET_H
#define PLUGINSET_H

// Deciding which discovered plugins load, and in what order. Pure: no files,
// no dlopen, so it can be tested on its own.

#define PLUGIN_MAX 64

typedef struct PluginManifest {
	char id[64];
	char name[128];
	int api;
	char slots[256];     // comma-separated exclusive slots this plugin claims
	char requires[256];  // comma-separated ids that must also load
	char conflicts[256]; // comma-separated ids that can't load alongside it
	char dir[512];       // the plugin's directory
	int bundled;         // 1: ships in Launcher.pak, off by default; 0: its own pak, on by default
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

#endif
