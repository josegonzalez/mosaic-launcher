#ifndef RESETMODEL_H
#define RESETMODEL_H

// Resetting the launcher's plugin settings to their defaults
// (host->reset_settings): plugins.txt goes, so each plugin is back on or
// off as it ships, and each plugin's <id>.cfg loses the keys its
// plugin.txt lists as settings, apart from secret ones, so they read as
// their defaults. Every other file in the folder stays, the PIN and the
// Launcher page's settings.txt among them, and so do a cfg's other keys,
// which plugins keep as their own state. No SDL here, and every call takes
// its folder, so the host tests can check it on its own.

#include "pluginset.h"

// 1 when key is one of m's settings that a reset takes back to its
// default: listed in its plugin.txt and not secret.
int Reset_isSetting(const PluginManifest* m, const char* key);

// text, a cfg's key=value lines, without the lines whose key
// Reset_isSetting says to reset, or blank, in a new string the caller
// frees. Counts the lines it left out in *removed, when it isn't NULL.
// NULL when out of memory.
char* Reset_cfgText(const char* text, const PluginManifest* m, int* removed);

// Resets the settings in dir, a .userdata/<platform>/launcher folder:
// removes dir/plugins.txt and rewrites dir/<id>.cfg for each of the count
// manifests that has one, removing a cfg left with nothing in it. log,
// when not NULL, gets a line for each file changed and each that couldn't
// be. Returns how many files it changed, or -1 when one couldn't be.
int Reset_apply(const char* dir, const PluginManifest* manifests, int count, void (*log)(const char* message));

#endif
