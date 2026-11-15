// The sync plugin's work off the launcher's thread: running rclone and
// Syncthing's REST API, reading the card's saves and states, and keeping
// the record of what's in sync. Nothing here calls the host;
// it logs to its own file, .userdata/<platform>/logs/sync.txt, which the
// programs' own output goes to too.

#ifndef SYNC_RUN_H
#define SYNC_RUN_H

#include <sys/types.h>

#include "sync_model.h"

enum { SR_RCLONE, SR_SYNCTHING };

#define SR_FOLDER_SAVES "mosaic-saves"
#define SR_FOLDER_STATES "mosaic-states"

typedef struct SrConfig {
	int backend;
	int saves, states; // which kinds sync
	char sdcard[SY_PATH_MAX];
	char data_dir[SY_PATH_MAX]; // .userdata/<platform>/launcher/sync
	char log_path[SY_PATH_MAX];
	char ca_file[SY_PATH_MAX];
	// the programs, beside plugin.so; empty when one isn't there
	char rclone[SY_PATH_MAX], syncthing[SY_PATH_MAX];
	// rclone
	char rclone_config[SY_PATH_MAX];
	char rclone_pass[256];
	char remote[SY_PATH_MAX];
	// Syncthing
	int gui_port;
	char api_key[64];
	char peer[80];
} SrConfig;

enum { SR_JOB_SYNC, SR_JOB_ST_SETUP, SR_JOB_ST_SCAN };

typedef struct SrJob {
	int kind;
	SrConfig cfg;
	// a sync of one game's files (tag and base, from Sy_gameOf), or all
	int whole;
	char tag[SY_NAME_MAX], base[SY_NAME_MAX];
	// rels left alone, sorted, as romm uploads them
	char (*skip)[SY_PATH_MAX];
	int skip_count;
	volatile int cancel;
	volatile pid_t child;
	volatile int finished;
	// what came of it
	int ok;
	int up, down, conflicts, deleted, failed;
	char error[256];
	// Syncthing: this device's id, whether the peer is connected and how
	// far it has the folders, and how many files this device still needs
	char device_id[80];
	int connected;
	int completion;
	int need;
} SrJob;

// The programs beside the plugin, in dir: rclone and syncthing.
void Sr_findTools(const char* dir, SrConfig* cfg);

// A CA bundle for rclone and Syncthing's HTTPS: the card's own, then the
// system's. Empty when there's none.
void Sr_findCa(const char* sdcard, const char* platform, char* out, size_t size);

// The card's saves and states that sync (cfg's kinds), sorted, into a
// list to free. Returns the count, or -1.
int Sr_scanLocal(const SrConfig* cfg, SyFile** out);

// Runs the job; called on the worker's thread.
void Sr_run(SrJob* job);

// Syncthing on this device, with its config and data in
// <data_dir>/syncthing, its REST API on 127.0.0.1:<gui_port> with cfg's
// API key, logging to <logs>/syncthing.txt; quits if the launcher dies
// (on Linux). Returns its pid, or -1.
pid_t Sr_startSyncthing(const SrConfig* cfg, const char* log_path);

// Asks the Syncthing at pid to quit, and returns at once.
void Sr_stopSyncthing(pid_t pid);

// A new random API key, 32 letters and digits. Returns 0, or -1.
int Sr_newKey(char* out, size_t size);

// Appends a line to cfg's log, with the time.
void Sr_log(const SrConfig* cfg, const char* format, ...) __attribute__((format(printf, 2, 3)));

#endif
