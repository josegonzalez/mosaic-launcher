#ifndef PLUGINCFG_H
#define PLUGINCFG_H

// A plugin's own settings, its .userdata/<platform>/launcher/<id>.cfg,
// read on first use and kept in memory, and which of them other plugins
// may read. No SDL, so it can be tested on its own.

#include "pluginset.h"

typedef struct PluginCfg {
	char path[512];
	int read; // its file was read
	int count;
	char** keys;
	char** values;
} PluginCfg;

// Starts cfg on the file at path, which is read on first use.
void PluginCfg_init(PluginCfg* cfg, const char* path);
void PluginCfg_free(PluginCfg* cfg);

// The value for key, or fallback when it isn't set. It stays valid until
// the next PluginCfg_set on cfg.
const char* PluginCfg_get(PluginCfg* cfg, const char* key, const char* fallback);

// Sets key and saves the file. Returns 0, or -1 for a key with '=' or a
// newline, a value with a newline, or a file that can't be written.
int PluginCfg_set(PluginCfg* cfg, const char* key, const char* value);

// What another plugin may read: the value for key only when the owner's
// schema m marks it public, else fallback.
const char* PluginCfg_readPublic(PluginCfg* cfg, const PluginManifest* m, const char* key, const char* fallback);

#endif
