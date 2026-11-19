// Sync: keeps the card's saves and states in step with another place,
// through one of two programs bundled beside the plugin:
//
//   rclone     any remote rclone reaches, from the user's rclone.conf
//   Syncthing  another device running Syncthing, which does the syncing
//              itself while the launcher is up
//
// With rclone, sync is two-way and the newest file wins: after a game
// returns it syncs that game's saves and states, and as the launcher
// starts (not back from a game) it syncs everything, pulling what's newer
// on the remote. A file changed on both sides since the last sync keeps
// the newer, and the other is kept beside it as <name>.conflict. A file
// removed from one side since the last sync is removed from the other
// too, moved to <name>.deleted there, unless it changed there since.
// sync_model.c decides; sync_run.c does the work on the plugin's one
// worker thread, which runs one job at a time while the main thread only
// queues jobs and takes their results in tick, with a toast for each.
//
// With Syncthing, the plugin starts it as the launcher starts, with its
// config and data in .userdata/<platform>/launcher/sync/syncthing, its
// API on 127.0.0.1 only, with a generated key, and stops it as the
// launcher quits. It shares Saves and the cores' state folders with the
// device whose ID is set, asks for a rescan after a game, and says from
// Syncthing's status whether the other device has them.
//
// With romm set up and uploading (romm.synced says server=1 and saves=1
// or states=1), sync leaves alone the files romm uploads: a game from
// RomM's saves or states, and files romm last uploaded or restored.
//
// It does nothing with Wi-Fi off or not connected but say so after a
// game, and syncs everything at the next start. The Sync tile on the
// quick menu opens a screen with the state of things, where A syncs now.

#define _GNU_SOURCE // dladdr

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "mosaic_plugin.h"
#include "sync_model.h"
#include "sync_run.h"

#define ID "sync"
#define SCREEN_ID ID ".main"
#define ACTION_ID ID ".open"
// English translated where it's used, for make pot
#define TR_MARK(text) (text)
#define GUI_PORT 8385
#define START_DELAY_MS 1500
#define JOBS_MAX 4

static const MosaicHost* host;
static SrConfig cfg;
static int configured = 0;           // a backend with what it needs
static char why_not[160];            // when not, why
static char pending_path[SY_PATH_MAX]; // marks that a sync is owed at the next start
static char returned[SY_PATH_MAX];   // the game the launcher's back from
static unsigned long started_at = 0, now_ms = 0;
static int start_done = 0;
static volatile pid_t st_pid = -1;
static char st_log[SY_PATH_MAX]; // Syncthing's own log, logs/syncthing.txt
static char device_id[80];
static int st_ready = 0;

// what the screen shows: the last result, as shown
static char last_result[512];
static time_t last_at = 0;
static int conflicts_total = 0;

// snprintf, for paths and messages cut short rather than overflowing;
// through vsnprintf so gcc doesn't warn about every path that could
static int put(char* out, size_t size, const char* format, ...) __attribute__((format(printf, 3, 4)));
static int put(char* out, size_t size, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out, size, format, args);
	va_end(args);
	return n;
}

static void say(const char* message) {
	host->log(ID, message);
}

static void sayf(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void sayf(const char* format, ...) {
	char message[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	say(message);
}

static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static const char* trPlural(const char* one, const char* many, unsigned long n) {
	if (MOSAIC_HOST_HAS(host, tr_plural)) return host->tr_plural(ID, NULL, one, many, n);
	return n == 1 ? one : many;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
	sayf("toast: %s", message);
}

static void toastf(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void toastf(const char* format, ...) {
	char message[512];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	toast(message);
}

///////////////////////////////////////
// the worker

static pthread_t worker;
static int worker_up = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static SrJob* queue[JOBS_MAX];
static int queued = 0;
static SrJob* running = NULL;
static SrJob* done[JOBS_MAX];
static int done_count = 0;
static int stopping = 0;

static void* work(void* arg) {
	(void)arg;
	pthread_mutex_lock(&lock);
	for (;;) {
		while (!stopping && !queued) pthread_cond_wait(&wake, &lock);
		if (stopping) break;
		SrJob* job = queue[0];
		memmove(&queue[0], &queue[1], (size_t)(--queued) * sizeof(queue[0]));
		running = job;
		pthread_mutex_unlock(&lock);
		if (job->kind == SR_JOB_ST_SETUP && st_pid <= 0) {
			// started here so it goes with this thread, which lives until quit
			st_pid = Sr_startSyncthing(&job->cfg, st_log);
		}
		Sr_run(job);
		pthread_mutex_lock(&lock);
		running = NULL;
		if (done_count < JOBS_MAX) done[done_count++] = job;
		else free(job);
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

static int busy(void) {
	pthread_mutex_lock(&lock);
	int n = queued + (running != NULL);
	pthread_mutex_unlock(&lock);
	return n;
}

static int submit(SrJob* job) {
	pthread_mutex_lock(&lock);
	int ok = worker_up && queued < JOBS_MAX;
	if (ok) {
		queue[queued++] = job;
		pthread_cond_signal(&wake);
	}
	pthread_mutex_unlock(&lock);
	if (!ok) {
		free(job->skip);
		free(job);
	}
	return ok ? 0 : -1;
}

static void stopWorker(void) {
	if (!worker_up) return;
	pthread_mutex_lock(&lock);
	stopping = 1;
	for (int i = 0; i < queued; i++) {
		free(queue[i]->skip);
		free(queue[i]);
	}
	queued = 0;
	if (running) {
		running->cancel = 1;
		if (running->child > 0) kill(running->child, SIGTERM);
	}
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
	pthread_join(worker, NULL);
	worker_up = 0;
	for (int i = 0; i < done_count; i++) {
		free(done[i]->skip);
		free(done[i]);
	}
	done_count = 0;
}

///////////////////////////////////////
// the network gate, as romm's

static unsigned long last_gate_toast = 0;

static int networkReady(int quiet) {
	const char* why = NULL;
	if (!MOSAIC_HOST_HAS(host, wifi_connection) || host->wifi_supported() != 1) return 1; // such as desktop
	MosaicWifi wifi = { .size = sizeof(wifi) };
	int rc = host->wifi_connection(&wifi);
	if (rc == 1) return 1;
	if (host->wifi_enabled() != 1) why = TR_MARK("Wi-Fi is off");
	else if (rc == 0) why = TR_MARK("Wi-Fi isn't connected");
	else why = TR_MARK("Wi-Fi isn't ready yet");
	sayf("network: %s", why);
	if (!quiet && (!last_gate_toast || now_ms - last_gate_toast > 4000)) {
		last_gate_toast = now_ms ? now_ms : 1;
		toastf(tr("Sync: %s"), tr(why));
	}
	return 0;
}

///////////////////////////////////////
// romm

static int rommSkipsGame = 0; // the returned game is from RomM

// The rels romm uploads, sorted, into job->skip: game's, when it's a game
// from RomM, and the files romm last uploaded or restored
static void rommSkips(SrJob* job, const char* game) {
	rommSkipsGame = 0;
	if (!MOSAIC_HOST_HAS(host, call)) return;
	static char reply[MOSAIC_CALL_MAX], game_reply[MOSAIC_CALL_MAX], file_reply[MOSAIC_CALL_MAX];
	if (host->call("romm.synced", NULL, reply, sizeof(reply)) < 0) return;
	if (Sy_replyInt(reply, "server", 0) != 1) return;
	if (Sy_replyInt(reply, "saves", 0) != 1 && Sy_replyInt(reply, "states", 0) != 1) return;
	char args[SY_PATH_MAX + 8];
	if (game && game[0] && put(args, sizeof(args), "path=%s", game) < (int)sizeof(args)
			&& host->call("romm.synced", args, game_reply, sizeof(game_reply)) >= 0) {
		rommSkipsGame = Sy_replyInt(game_reply, "game", 0) == 1;
	}
	char tag[SY_NAME_MAX], base[SY_NAME_MAX];
	int have_game = rommSkipsGame && Sy_gameOf(cfg.sdcard, game, tag, sizeof(tag), base, sizeof(base)) == 0;
	SyFile* files = NULL;
	int count = Sr_scanLocal(&cfg, &files);
	job->skip = calloc(count > 0 ? (size_t)count : 1, sizeof(job->skip[0]));
	for (int i = 0; job->skip && i < count; i++) {
		int kind = Sy_kind(files[i].rel);
		int skip;
		if (have_game && Sy_ofGame(files[i].rel, tag, base)) {
			skip = Sy_rommSkips(game_reply, NULL, kind);
		} else {
			char path[SY_PATH_MAX];
			skip = Sy_localPath(cfg.sdcard, files[i].rel, path, sizeof(path)) == 0
				&& put(args, sizeof(args), "path=%s", path) < (int)sizeof(args)
				&& host->call("romm.synced", args, file_reply, sizeof(file_reply)) >= 0
				&& Sy_rommSkips(NULL, file_reply, kind);
		}
		if (skip) put(job->skip[job->skip_count++], SY_PATH_MAX, "%s", files[i].rel);
	}
	free(files);
	if (job->skip_count) sayf("leaving %d files to romm", job->skip_count);
}

///////////////////////////////////////
// settings

static void ownDir(char* out, size_t size) {
	Dl_info info;
	out[0] = '\0';
	if (!dladdr((const void*)&ownDir, &info) || !info.dli_fname) return;
	const char* slash = strrchr(info.dli_fname, '/');
	if (slash) put(out, size, "%.*s", (int)(slash - info.dli_fname), info.dli_fname);
}

static void notConfigured(const char* why) {
	configured = 0;
	put(why_not, sizeof(why_not), "%s", why);
	sayf("not syncing: %s", why);
}

static void loadSettings(void) {
	memset(&cfg, 0, sizeof(cfg));
	const char* backend = host->cfg_get("backend", "rclone");
	cfg.backend = !strcmp(backend, "syncthing") ? SR_SYNCTHING : SR_RCLONE;
	cfg.saves = strcmp(host->cfg_get("saves", "on"), "off") != 0;
	cfg.states = strcmp(host->cfg_get("states", "on"), "off") != 0;
	put(cfg.sdcard, sizeof(cfg.sdcard), "%s", host->sdcard_path);
	put(cfg.data_dir, sizeof(cfg.data_dir), "%s/launcher/sync", host->userdata_path);
	char logs[SY_PATH_MAX];
	put(logs, sizeof(logs), "%s/logs", host->userdata_path);
	mkdir(logs, 0755);
	put(cfg.log_path, sizeof(cfg.log_path), "%s/" ID ".txt", logs);
	put(st_log, sizeof(st_log), "%s/syncthing.txt", logs);
	put(pending_path, sizeof(pending_path), "%s/pending.txt", cfg.data_dir);
	Sr_findCa(host->sdcard_path, host->platform, cfg.ca_file, sizeof(cfg.ca_file));
	char dir[SY_PATH_MAX];
	ownDir(dir, sizeof(dir));
	Sr_findTools(dir, &cfg);
	mkdir(cfg.data_dir, 0755);

	put(cfg.remote, sizeof(cfg.remote), "%s", host->cfg_get("remote", ""));
	const char* conf = host->cfg_get("rclone_config", "");
	if (conf[0]) put(cfg.rclone_config, sizeof(cfg.rclone_config), "%s", conf);
	else put(cfg.rclone_config, sizeof(cfg.rclone_config), "%s/rclone.conf", cfg.data_dir);
	put(cfg.rclone_pass, sizeof(cfg.rclone_pass), "%s", host->cfg_get("rclone_pass", ""));
	put(cfg.peer, sizeof(cfg.peer), "%s", host->cfg_get("peer", ""));
	for (char* p = cfg.peer; *p; p++) {
		if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
	}
	cfg.gui_port = GUI_PORT;

	configured = 1;
	why_not[0] = '\0';
	if (!cfg.saves && !cfg.states) notConfigured(TR_MARK("nothing to sync: saves and states are both off"));
	else if (cfg.backend == SR_RCLONE) {
		struct stat st;
		if (!cfg.rclone[0]) notConfigured(TR_MARK("rclone isn't installed beside the plugin"));
		else if (!cfg.remote[0]) notConfigured(TR_MARK("no rclone remote set"));
		else if (stat(cfg.rclone_config, &st) != 0 && strchr(cfg.remote, ':') && cfg.remote[0] != '/') {
			notConfigured(TR_MARK("no rclone.conf"));
		}
	} else {
		if (!cfg.syncthing[0]) notConfigured(TR_MARK("Syncthing isn't installed beside the plugin"));
		else if (cfg.peer[0] && !Sy_deviceIdValid(cfg.peer)) notConfigured(TR_MARK("the other device's ID isn't a Syncthing device ID"));
		const char* api = host->cfg_get("api_key", "");
		if (strlen(api) < 16) {
			char made[64];
			if (Sr_newKey(made, sizeof(made)) == 0) {
				host->cfg_set("api_key", made);
				api = host->cfg_get("api_key", "");
			}
		}
		put(cfg.api_key, sizeof(cfg.api_key), "%s", api);
		if (configured && strlen(cfg.api_key) < 16) notConfigured(TR_MARK("couldn't make Syncthing's API key"));
	}
	if (configured) {
		const char* what = cfg.saves && cfg.states ? "saves and states" : cfg.saves ? "saves" : "states";
		if (cfg.backend == SR_RCLONE) sayf("rclone to %s, %s", cfg.remote, what);
		else sayf("Syncthing with %s, %s", cfg.peer[0] ? cfg.peer : "no other device yet", what);
	}
}

///////////////////////////////////////
// jobs

static SrJob* newJob(int kind) {
	SrJob* job = calloc(1, sizeof(SrJob));
	if (!job) return NULL;
	job->kind = kind;
	job->cfg = cfg;
	return job;
}

static int owed(void) {
	struct stat st;
	return stat(pending_path, &st) == 0;
}

static void owe(int yes) {
	if (!yes) {
		unlink(pending_path);
		return;
	}
	FILE* f = fopen(pending_path, "w");
	if (f) fclose(f);
}

// a sync of game's files, or with game NULL, of everything
static void startSync(const char* game, int quiet) {
	if (!configured || cfg.backend == SR_SYNCTHING) return;
	if (!networkReady(quiet)) {
		owe(1);
		return;
	}
	SrJob* job = newJob(SR_JOB_SYNC);
	if (!job) return;
	job->whole = !game;
	if (game && Sy_gameOf(cfg.sdcard, game, job->tag, sizeof(job->tag), job->base, sizeof(job->base)) != 0) {
		sayf("%s isn't a game in Roms, nothing to sync", game);
		free(job);
		return;
	}
	rommSkips(job, game);
	if (game && rommSkipsGame) sayf("%s is from RomM, which uploads its files", job->base);
	if (game) sayf("syncing %s", job->base);
	else say("syncing everything");
	if (submit(job) != 0) say("couldn't queue the sync");
}

static void startSyncthing(const char* game) {
	SrJob* job = newJob(SR_JOB_ST_SETUP);
	if (!job) return;
	rommSkips(job, game);
	if (submit(job) != 0) say("couldn't queue Syncthing's start");
	if (!game) return;
	SrJob* scan = newJob(SR_JOB_ST_SCAN);
	if (scan && submit(scan) != 0) say("couldn't queue Syncthing's scan");
}

static void finished(SrJob* job) {
	time(&last_at);
	if (job->kind == SR_JOB_ST_SETUP) {
		if (job->device_id[0]) put(device_id, sizeof(device_id), "%s", job->device_id);
		st_ready = job->ok;
		if (!job->ok) {
			sayf("Syncthing: %s", job->error);
			toastf(tr("Sync: %s"), job->error);
			put(last_result, sizeof(last_result), "%s", job->error);
		} else {
			sayf("Syncthing up as %s, sharing with %s", device_id, cfg.peer[0] ? cfg.peer : "nobody yet");
			put(last_result, sizeof(last_result), "%s", tr("Syncthing is running"));
		}
		if (MOSAIC_HOST_HAS(host, request_redraw)) host->request_redraw();
		return;
	}
	if (job->kind == SR_JOB_ST_SCAN) {
		if (!job->ok) {
			sayf("Syncthing scan: %s", job->error);
			toastf(tr("Sync: %s"), job->error);
			put(last_result, sizeof(last_result), "%s", job->error);
		} else if (!job->connected) {
			sayf("Syncthing: the other device isn't connected");
			toast(tr("Sync: the other device isn't connected; Syncthing sends your saves when it is"));
			put(last_result, sizeof(last_result), "%s", tr("waiting for the other device"));
		} else if (job->completion >= 100) {
			sayf("Syncthing: the other device has everything");
			toast(tr("Sync: the other device has your saves"));
			put(last_result, sizeof(last_result), "%s", tr("up to date"));
		} else {
			sayf("Syncthing: the other device has %d%%", job->completion);
			toast(tr("Sync: sending your saves to the other device"));
			put(last_result, sizeof(last_result), "%s", tr("sending"));
		}
		host->request_redraw();
		return;
	}
	sayf("synced%s: %d up, %d down, %d conflicts, %d removed, %d failed%s%s", job->whole ? " everything" : "", job->up, job->down,
		job->conflicts, job->deleted, job->failed, job->ok ? "" : ": ", job->ok ? "" : job->error);
	conflicts_total += job->conflicts;
	if (!job->ok) {
		owe(1);
		toastf(tr("Sync: %s; it tries again next time"), job->error);
		put(last_result, sizeof(last_result), "%s", job->error);
		host->request_redraw();
		return;
	}
	if (job->whole) owe(0);
	char message[512];
	int moved = job->up + job->down;
	if (!moved && job->deleted) {
		put(message, sizeof(message), trPlural("Sync: %d file removed, as on the other side (kept as .deleted)",
			"Sync: %d files removed, as on the other side (kept as .deleted)", (unsigned long)job->deleted), job->deleted);
	} else if (!moved) {
		put(message, sizeof(message), "%s", tr("Sync: up to date"));
	} else if (job->up && job->down) {
		put(message, sizeof(message), tr("Sync: %d sent, %d fetched"), job->up, job->down);
	} else if (job->up) {
		put(message, sizeof(message), trPlural("Sync: %d file sent", "Sync: %d files sent", (unsigned long)job->up), job->up);
	} else {
		put(message, sizeof(message), trPlural("Sync: %d newer file fetched", "Sync: %d newer files fetched",
			(unsigned long)job->down), job->down);
	}
	if (job->conflicts) {
		size_t len = strlen(message);
		put(message + len, sizeof(message) - len, trPlural(" (%d changed on both sides, the older kept as .conflict)",
			" (%d changed on both sides, the older kept as .conflict)", (unsigned long)job->conflicts), job->conflicts);
	}
	if (moved && job->deleted) {
		size_t len = strlen(message);
		put(message + len, sizeof(message) - len, trPlural(", %d removed (kept as .deleted)", ", %d removed (kept as .deleted)",
			(unsigned long)job->deleted), job->deleted);
	}
	put(last_result, sizeof(last_result), "%s", message);
	// quiet at the start when there was nothing to do
	if (moved || job->deleted || !job->whole || job->conflicts) toast(message);
	host->request_redraw();
}

///////////////////////////////////////
// hooks

static void onReturn(const char* name, const char* payload, void* data) {
	(void)name;
	(void)data;
	char type[16];
	if (host->event_value(payload, "type", type, sizeof(type)) <= 0 || strcmp(type, "rom") != 0) return;
	if (host->event_value(payload, "path", returned, sizeof(returned)) >= (int)sizeof(returned)) returned[0] = '\0';
	if (returned[0]) sayf("back from %s", returned);
}

static void tick(unsigned long now) {
	now_ms = now;
	if (!started_at) started_at = now ? now : 1;
	if (!start_done && now - started_at > START_DELAY_MS) {
		start_done = 1;
		if (configured && cfg.backend == SR_SYNCTHING) {
			startSyncthing(returned[0] ? returned : NULL);
		} else if (configured) {
			// back from a game: that game's files, else everything, as at
			// boot or when a sync is owed from before
			int all = !returned[0] || owed();
			if (returned[0]) startSync(returned, 0);
			if (all) startSync(NULL, returned[0] != 0);
		}
	}
	for (;;) {
		pthread_mutex_lock(&lock);
		SrJob* job = done_count ? done[0] : NULL;
		if (job) memmove(&done[0], &done[1], (size_t)(--done_count) * sizeof(done[0]));
		pthread_mutex_unlock(&lock);
		if (!job) break;
		finished(job);
		free(job->skip);
		free(job);
	}
	if (MOSAIC_HOST_HAS(host, inhibit_sleep)) {
		static int held = 0;
		int now_busy = busy() > 0 && cfg.backend != SR_SYNCTHING;
		if (now_busy != held) {
			held = now_busy;
			host->inhibit_sleep(ID, held);
		}
	}
}

static void stopAll(void) {
	stopWorker();
	if (st_pid > 0) {
		Sr_stopSyncthing(st_pid);
		say("stopped Syncthing");
		st_pid = -1;
	}
}

///////////////////////////////////////
// the screen

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = MOSAIC_HOST_HAS(host, theme_color) ? host->theme_color(index) : -1;
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

static void text(MosaicSurface* screen, int font, const char* s, int x, int y, int max_w, MosaicColor color) {
	char cut[512];
	if (max_w <= 0 || host->text_truncate(font, s, max_w, cut, sizeof(cut)) < 0) return;
	host->draw_text(screen, font, cut, x, y, color, max_w);
}

#define ROWS_MAX 8

// The screen's rows, label and value; a row with an empty label carries
// on the value above it
static int rows(const char* labels[ROWS_MAX], char values[ROWS_MAX][200]) {
	int n = 0;
	static const char* names[] = { "rclone", "Syncthing" };
	labels[n] = TR_MARK("Sync with");
	put(values[n++], 200, "%s: %s", names[cfg.backend],
		tr(cfg.saves && cfg.states ? TR_MARK("saves and states") : cfg.saves ? TR_MARK("saves") : TR_MARK("states")));
	labels[n] = TR_MARK("To");
	if (cfg.backend == SR_RCLONE) put(values[n++], 200, "%s", cfg.remote[0] ? cfg.remote : "-");
	else put(values[n++], 200, "%s", cfg.peer[0] ? cfg.peer : tr("no other device set"));
	if (cfg.backend == SR_SYNCTHING) {
		// this device's ID in full, to type on the other: three groups a line
		labels[n] = TR_MARK("This device");
		if (strlen(device_id) == 63) {
			put(values[n++], 200, "%.23s", device_id);
			labels[n] = "";
			put(values[n++], 200, "%.23s", device_id + 24);
			labels[n] = "";
			put(values[n++], 200, "%s", device_id + 48);
		} else {
			put(values[n++], 200, "%s", device_id[0] ? device_id : tr("starting..."));
		}
	}
	labels[n] = TR_MARK("Status");
	if (!configured) put(values[n++], 200, "%s", tr(why_not));
	else if (busy()) put(values[n++], 200, "%s", tr("syncing..."));
	else put(values[n++], 200, "%s", last_result[0] ? last_result : tr("not synced yet"));
	if (cfg.backend == SR_SYNCTHING) return n;
	labels[n] = TR_MARK("Last sync");
	if (last_at) {
		struct tm tm;
		strftime(values[n++], 200, "%Y-%m-%d %H:%M", localtime_r(&last_at, &tm));
	} else {
		put(values[n++], 200, "-");
	}
	labels[n] = TR_MARK("Conflicts");
	put(values[n++], 200, "%d", conflicts_total);
	return n;
}

static int render(MosaicSurface* screen, const MosaicScreenView* view, void* data) {
	(void)data;
	host->fill_rect(screen, &view->area, themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000));
	int scale = host->screen_scale();
	if (scale < 1) scale = 1;
	int pad = 10 * scale;
	int x = view->area.x + pad, y = view->area.y + pad;
	int right = view->status.w > 0 ? view->status.x - pad : view->area.x + view->area.w - pad;
	if (view->header.w > 0 && view->header.x - pad < right) right = view->header.x - pad;
	// the title pill
	int title_h = host->text_height(MOSAIC_FONT_LARGE) + 8 * scale;
	const char* title = tr("Sync");
	int title_w = host->text_width(MOSAIC_FONT_LARGE, title) + 24 * scale;
	if (title_w > right - x) title_w = right - x;
	MosaicRect pill = { x, y, title_w, title_h };
	if (pill.w > 0) {
		host->fill_round_rect(screen, &pill, pill.h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
		text(screen, MOSAIC_FONT_LARGE, title, x + 12 * scale, y + 4 * scale, title_w - 24 * scale, themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF));
	}
	y += title_h + pad;

	const char* labels[ROWS_MAX];
	char values[ROWS_MAX][200];
	int n = rows(labels, values);
	int font = MOSAIC_FONT_SMALL;
	int label_w = 0;
	for (int i = 0; i < n; i++) {
		int w = labels[i][0] ? host->text_width(font, tr(labels[i])) : 0;
		if (w > label_w) label_w = w;
	}
	label_w += pad;
	int full = view->area.x + view->area.w - pad - x;
	int row_h = host->text_height(font) + 6 * scale;
	// above the hints
	int bottom = view->area.y + view->area.h - title_h - 2 * pad;
	for (int i = 0; i < n && y + row_h <= bottom; i++) {
		if (labels[i][0]) text(screen, font, tr(labels[i]), x, y, label_w, themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF));
		text(screen, font, values[i], x + label_w, y, full - label_w, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF));
		y += row_h;
	}
	if (MOSAIC_HOST_HAS(host, draw_hints)) {
		MosaicHint left[] = { { MOSAIC_BUTTON_B, "BACK" } };
		MosaicHint now_hint[] = { { MOSAIC_BUTTON_A, "SYNC NOW" } };
		host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, 1, NULL);
		if (configured && cfg.backend != SR_SYNCTHING) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, now_hint, 1, NULL);
		else if (configured && st_ready && Sy_deviceIdValid(cfg.peer)) {
			MosaicHint scan_hint[] = { { MOSAIC_BUTTON_A, "SCAN NOW" } };
			host->draw_hints(screen, MOSAIC_HINTS_RIGHT, scan_hint, 1, NULL);
		}
	}
	return 0;
}

static int input(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	if ((in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A)) && configured && !busy()) {
		if (cfg.backend != SR_SYNCTHING) {
			startSync(NULL, 0);
		} else if (st_ready && Sy_deviceIdValid(cfg.peer)) {
			SrJob* scan = newJob(SR_JOB_ST_SCAN);
			if (scan && submit(scan) != 0) say("couldn't queue Syncthing's scan");
		}
		host->request_redraw();
	}
	return MOSAIC_SCREEN_STAY;
}

static int animating(void* data) {
	(void)data;
	return busy() > 0;
}

static int enter(void* data) {
	(void)data;
	say("screen opened");
	const char* labels[ROWS_MAX];
	char values[ROWS_MAX][200];
	int n = rows(labels, values);
	for (int i = 0; i < n; i++) sayf("screen: %s: %s", labels[i][0] ? labels[i] : "...", values[i]);
	return 0;
}

static void openScreen(const char* path, void* data) {
	(void)path;
	(void)data;
	if (host->open_screen(SCREEN_ID) != 0) say("couldn't open the sync screen");
}

///////////////////////////////////////
// starting and stopping

static int init(const MosaicHost* h) {
	host = h;
	loadSettings();
	if (MOSAIC_HOST_HAS(host, subscribe)) host->subscribe("launcher.return", onReturn, NULL);
	stopping = 0;
	if (pthread_create(&worker, NULL, work, NULL) == 0) worker_up = 1;
	else say("couldn't start its worker thread");
	if (MOSAIC_HOST_HAS(host, open_screen) && MOSAIC_HOST_HAS(host, draw_text)) {
		static const MosaicScreenHooks hooks = {
			.size = sizeof(MosaicScreenHooks),
			.enter = enter,
			.input = input,
			.render = render,
			.animating = animating,
		};
		if (host->register_screen(SCREEN_ID, &hooks) != 0) {
			say("couldn't register its screen");
		} else if (host->register_action(ACTION_ID, "Sync", MOSAIC_ACTION_EDITS, NULL, openScreen, NULL) != 0) {
			say("couldn't register " ACTION_ID);
		} else if (strcmp(host->cfg_get("tile", "on"), "off") != 0
				&& (!MOSAIC_HOST_HAS(host, quick_tile) || host->quick_tile(ACTION_ID, MOSAIC_QUICK_TOP, NULL, NULL, NULL) != 0)) {
			say("couldn't add the Sync tile");
		}
	}
	return 0;
}

static void quit(void) {
	stopAll();
}

static void beforePoweroff(void) {
	stopAll();
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.tick = tick,
	.before_poweroff = beforePoweroff,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
