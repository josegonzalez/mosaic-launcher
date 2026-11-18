#ifndef REPORT_H
#define REPORT_H

// Bug report bundles: one zip in Reports/ on the card with the launcher's
// and plugins' logs, settings, device and build details and a game list
// summary, private details taken out (reportmodel.c). plugins.c makes one
// for host->bug_report and when the crash guard pauses plugins.

#include <time.h>
#include "pluginset.h"
#include "indexmodel.h"

typedef struct ReportSource {
	const char* sdcard;   // the card, eg. SDCARD_PATH
	const char* userdata; // .userdata/<platform>, eg. USERDATA_PATH
	const char* state;    // the launcher's state, eg. PLUGIN_STATE_DIR
	const char* platform;
	const char* model;      // the device, as stock names it
	const char* os_version; // the device's own, or ""
	const char* pak;        // Launcher.pak, for its VERSION, or NULL
	const char* profile;    // the live profile, "" for the owner's
	const char* theme;      // the theme slot's owner and its theme, or "stock"
	const char* made_by;    // what asked, eg. "the bug-report plugin" or "the crash guard, which paused plugins"
	const PluginManifest* manifests;
	const char* const* states; // what each manifest's plugin is doing, eg. "on, loaded" or "off: ..."
	int manifest_count;
	const CardIndex* index; // or NULL when it hasn't been read
	const char* wifi_name;  // the network joined, or NULL
	int keep_network;       // 1 keeps IP and MAC addresses and Wi-Fi names
	time_t now;
	void (*log)(const char* message); // what it does, one line each, without newlines
} ReportSource;

// Writes Reports/mosaic-<date>.zip on the card, then removes the oldest
// past REPORT_KEEP, saying which. Returns 0 with the zip's path from the
// card, eg. "Reports/mosaic-2026-11-17-153012.zip", in out, or -1, which
// it logs, leaving nothing half written.
int Report_make(const ReportSource* src, char* out, size_t size);

#endif
