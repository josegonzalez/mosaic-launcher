// RomM: a RomM server's library as a RomM folder on the root, with a
// folder for each platform the launcher has a system for, listing its
// roms. A rom that isn't on the card yet is a stub: launching it
// downloads it into Roms/<system>/ (a progress screen and the status
// pill show how far it's got, and B cancels), then launches it. A game of
// several files comes as a zip, unpacked into Roms/<system>/<game>/ with a
// <game>.m3u when it has more than one disc (romm_zip.c). After a
// game from RomM returns, its save is uploaded when it changed, and the
// Flashback screen (the "button" setting, Y by default) lists the game's
// saves and states on the server, newest first, and restores one after a
// confirm, keeping a copy of the local file first.
//
// Other plugins hear from it without the network: the romm.synced service
// says whether a server is set and whether a file is the one it last
// uploaded or restored, unchanged since (by its size and modification
// time, kept in synced.txt), and whether a path is a game from RomM;
// romm.downloads lists the games last downloaded (downloads.txt), and
// romm.download_done is published as each download finishes. None of
// them gives out the server's address, user or password.
//
// Network work runs one job at a time on a thread of its own; the main
// thread only starts jobs and takes their results in tick. Lists are kept
// in .userdata/<platform>/launcher/romm/ so folders open at once, and
// refreshed in the background when they're older than a minute.
//
// It does nothing until the server is set, and nothing with Wi-Fi off or
// not connected but say so. The password or token is never logged.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "mosaic_plugin.h"
#include "romm_model.h"
#include "romm_net.h"
#include "romm_zip.h"

#define ID "romm"
// English translated where it's used, for make pot
#define TR_MARK(text) (text)
#define DOWNLOAD_SCREEN ID ".download"
#define FLASHBACK_SCREEN ID ".flashback"
#define FLASHBACK_ACTION ID ".flashback"
#define PLATFORMS_MAX 256
#define ROMS_MAX 20000
#define ASSETS_MAX 200
#define JOBS_MAX 8
#define REFRESH_MS 60000
#define JSON_MAX (32LL * 1024 * 1024)
#define SAVE_MAX (64LL * 1024 * 1024)
#define ROM_MAX (4LL * 1024 * 1024 * 1024 - 1)
#define ZIP_OVERHEAD (16LL * 1024 * 1024) // a zip's headers past the files' size, with room to spare
#define SAVE_SLOT "mosaic-launcher"

// snprintf, for paths and messages that are cut short rather than
// overflow; through vsnprintf so gcc doesn't warn about every path that
// could in theory be cut
static int put(char* out, size_t size, const char* format, ...) __attribute__((format(printf, 3, 4)));
static int put(char* out, size_t size, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out, size, format, args);
	va_end(args);
	return n;
}

static const MosaicHost* host;
static int configured = 0;
static RNConfig net;
static char sd[RM_PATH_MAX];
static char data_dir[RM_PATH_MAX]; // .userdata/<platform>/launcher/romm
static char root_path[RM_PATH_MAX]; // <sd>/RomM, the virtual folder
static char status_path[RM_PATH_MAX];
static int sync_saves = 1;
static int keep_saves = 10;

static void say(const char* message) {
	host->log(ID, message);
}

static void sayf(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void sayf(const char* format, ...) {
	char line[1600];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	say(line);
}

// Fixed text, translated in this plugin's id's context.
static const char* tr(const char* text) {
	return MOSAIC_HOST_HAS(host, tr) ? host->tr(ID, NULL, text) : text;
}

static void toast(const char* message) {
	if (MOSAIC_HOST_HAS(host, toast)) host->toast(message);
}

///////////////////////////////////////
// files

static int isDir(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int isFile(const char* path) {
	struct stat st;
	return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void makeDirs(const char* path) {
	char buf[RM_PATH_MAX];
	put(buf, sizeof(buf), "%s", path);
	for (char* p = buf + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(buf, 0755);
		*p = '/';
	}
	mkdir(buf, 0755);
}

// Writes text to path through a temporary file. Returns 0, or -1.
static int writeFile(const char* path, const char* text, size_t len) {
	char tmp[RM_PATH_MAX + 8];
	put(tmp, sizeof(tmp), "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) return -1;
	int ok = write(fd, text, len) == (ssize_t)len && fsync(fd) == 0;
	close(fd);
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

// The whole of a small file, NUL-terminated, or NULL. Free it.
static char* readFile(const char* path, size_t max, size_t* len) {
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size > max) {
		close(fd);
		return NULL;
	}
	char* buf = malloc((size_t)st.st_size + 1);
	ssize_t got = buf ? read(fd, buf, (size_t)st.st_size) : -1;
	close(fd);
	if (got != st.st_size) {
		free(buf);
		return NULL;
	}
	buf[got] = '\0';
	if (len) *len = (size_t)got;
	return buf;
}

// 1 when path's folder is a real folder inside <sd>/Roms or <sd>/Saves or
// .userdata/shared, not reached through a link out of it
static int insideCard(const char* dir, const char* top) {
	char real_dir[PATH_MAX], real_top[PATH_MAX], want[RM_PATH_MAX];
	put(want, sizeof(want), "%s/%s", sd, top);
	if (!realpath(dir, real_dir) || !realpath(want, real_top)) return 0;
	size_t n = strlen(real_top);
	return strncmp(real_dir, real_top, n) == 0 && real_dir[n] == '/';
}

///////////////////////////////////////
// settings

static void loadSettings(void) {
	configured = 0;
	memset(&net, 0, sizeof(net));
	const char* url = host->cfg_get("url", "");
	const char* user = host->cfg_get("user", "");
	char user_copy[256];
	put(user_copy, sizeof(user_copy), "%s", user);
	sync_saves = strcmp(host->cfg_get("sync", "on"), "off") != 0;
	keep_saves = atoi(host->cfg_get("keep", "10"));
	if (keep_saves < 1 || keep_saves > 1000) keep_saves = 10;
	if (!url[0]) {
		say("no server set, so it does nothing");
		return;
	}
	char error[200];
	if (RM_urlParse(url, &net.url, error, sizeof(error)) != 0) {
		sayf("the server setting is unusable: %s", error);
		return;
	}
	const char* secret = host->cfg_get("password", "");
	RM_authHeader(user_copy, secret, net.auth, sizeof(net.auth));
	int have_secret = secret[0] != '\0';
	const char* pin = host->cfg_get("pin", "");
	if (pin[0]) {
		if (RM_parsePin(pin, net.pin) != 0) {
			say("the certificate fingerprint setting isn't a SHA-256 fingerprint");
			if (net.url.https) return; // never fall back to trusting something else
		} else {
			net.have_pin = 1;
		}
	}
	const char* ca = host->cfg_get("ca_file", "");
	if (ca[0]) put(net.ca_file, sizeof(net.ca_file), "%s", ca);
	else RN_systemCa(sd, host->platform, net.ca_file, sizeof(net.ca_file));
	net.connect_ms = 8000;
	net.io_ms = 20000;
	configured = 1;
	char shown[RM_HOST_MAX + 64];
	RM_hostHeader(&net.url, shown, sizeof(shown));
	const char* trust = !net.url.https ? "" : net.have_pin ? ", certificate pinned" : net.ca_file[0] ? ", CA file " : ", no CA file";
	sayf("server %s://%s%s, %s%s, %s%s%s", net.url.https ? "https" : "http", shown, net.url.base,
		user_copy[0] ? "user " : "", user_copy[0] ? user_copy : "token login", have_secret ? "secret set" : "no secret",
		trust, net.url.https && !net.have_pin ? net.ca_file : "");
	if (!net.url.https) say("http:// sends the login in clear text; use https:// on networks you don't trust");
}

///////////////////////////////////////
// the network gate

static unsigned long last_gate_toast = 0;
static unsigned long now_ms = 0;

// 1 when the network can be tried; else says why (once every few seconds) and returns 0
static int networkReady(int quiet) {
	const char* why = NULL;
	if (MOSAIC_HOST_HAS(host, wifi_connection) && host->wifi_supported() == 1) {
		MosaicWifi wifi = { .size = sizeof(wifi) };
		int rc = host->wifi_connection(&wifi);
		if (rc == 1) return 1;
		if (host->wifi_enabled() != 1) why = TR_MARK("Wi-Fi is off");
		else if (rc == 0) why = TR_MARK("Wi-Fi isn't connected");
		else why = TR_MARK("Wi-Fi isn't ready yet");
	} else {
		return 1; // no Wi-Fi to ask about, such as desktop: try the network
	}
	if (!quiet && (!last_gate_toast || now_ms - last_gate_toast > 4000)) {
		last_gate_toast = now_ms ? now_ms : 1;
		char message[512];
		put(message, sizeof(message), tr("RomM: %s"), tr(why));
		toast(message);
		sayf("network: %s", why);
	}
	return 0;
}

///////////////////////////////////////
// jobs

enum { J_PLATFORMS, J_ROMS, J_DOWNLOAD, J_UPLOAD, J_ASSETS, J_FETCH };

typedef struct Job {
	int kind;
	volatile int cancel;
	volatile int finished;
	volatile long long done, total;
	pthread_t thread;
	// what to do
	int platform_id;
	int rom_id;
	int asset_kind, asset_id;
	char file[RM_NAME_MAX];
	char name[RM_NAME_MAX];
	char tag[RM_TAG_MAX];
	char dir[RM_PATH_MAX];
	char path[RM_PATH_MAX]; // where it ends up
	char temp[RM_PATH_MAX];
	int multi; // a zip, unpacked into unpack, then moved to dir/game
	volatile int unpacking;
	char game[RM_NAME_MAX];
	char unpack[RM_PATH_MAX];
	char md5[33];
	long long file_size, file_mtime; // an upload's file as it was when queued
	long long expect;
	// what came of it
	int ok;
	int status;
	char error[256];
	RMPlatform* platforms;
	RMRom* roms;
	RMAsset* assets;
	int count;
} Job;

static Job* queue[JOBS_MAX];
static int queued = 0;
static Job* running = NULL;

static void jobFree(Job* j) {
	if (!j) return;
	free(j->platforms);
	free(j->roms);
	free(j->assets);
	free(j);
}

static void httpError(Job* j, const RNCall* call, const char* what) {
	if (call->status == 401 || call->status == 403) put(j->error, sizeof(j->error), "RomM refused the login");
	else if (call->status == 404) put(j->error, sizeof(j->error), "%s isn't on the server", what);
	else put(j->error, sizeof(j->error), "the server answered %d", call->status);
}

static int get(Job* j, RNCall* call, const char* target, long long max) {
	memset(call, 0, sizeof(*call));
	call->method = "GET";
	put(call->target, sizeof(call->target), "%s", target);
	call->max_body = max;
	call->cancel = &j->cancel;
	if (RN_run(&net, call) != 0) {
		put(j->error, sizeof(j->error), "%s", call->error);
		return -1;
	}
	return 0;
}

static void runPlatforms(Job* j) {
	RNCall call;
	if (get(j, &call, "/api/platforms", JSON_MAX) != 0) return;
	if (call.status != 200) {
		httpError(j, &call, "the platform list");
		RN_free(&call);
		return;
	}
	j->platforms = calloc(PLATFORMS_MAX, sizeof(RMPlatform));
	j->count = j->platforms ? RM_parsePlatforms(call.data, call.len, j->platforms, PLATFORMS_MAX) : -1;
	RN_free(&call);
	if (j->count < 0) put(j->error, sizeof(j->error), "the platform list isn't RomM's");
	else j->ok = 1;
}

static void runRoms(Job* j) {
	j->roms = calloc(ROMS_MAX, sizeof(RMRom));
	if (!j->roms) {
		put(j->error, sizeof(j->error), "out of memory");
		return;
	}
	int total = 0, offset = 0;
	for (int page = 0; page < 100; page++) {
		char target[256];
		put(target, sizeof(target),
			"/api/roms?platform_ids=%d&limit=500&offset=%d&order_by=name&order_dir=asc"
			"&with_char_index=false&with_filter_values=false&with_rom_id_index=false&with_files=true",
			j->platform_id, offset);
		RNCall call;
		if (get(j, &call, target, JSON_MAX) != 0) return;
		if (call.status != 200) {
			httpError(j, &call, "the rom list");
			RN_free(&call);
			return;
		}
		int got = RM_parseRoms(call.data, call.len, j->roms, &j->count, ROMS_MAX, &total);
		RN_free(&call);
		if (got < 0) {
			put(j->error, sizeof(j->error), "the rom list isn't RomM's");
			return;
		}
		offset += got;
		if (got == 0 || offset >= total || j->count >= ROMS_MAX) break;
	}
	j->ok = 1;
}

static void runDownload(Job* j) {
	char encoded[RM_NAME_MAX * 3];
	RM_urlEncode(j->file, encoded, sizeof(encoded));
	char target[RM_PATH_MAX];
	put(target, sizeof(target), "/api/roms/%d/content/%s", j->rom_id, encoded);
	RNCall call;
	memset(&call, 0, sizeof(call));
	call.method = "GET";
	put(call.target, sizeof(call.target), "%s", target);
	// a zip has its headers and RomM's .m3u on top of the files
	if (j->multi) call.max_body = j->expect > 0 ? j->expect + ZIP_OVERHEAD : RM_ZIP_TOTAL_MAX;
	else call.max_body = j->expect > 0 ? j->expect : ROM_MAX;
	call.out_path = j->temp;
	call.cancel = &j->cancel;
	call.done = &j->done;
	call.total = &j->total;
	j->total = j->expect;
	int rc = RN_run(&net, &call);
	if (rc != 0) {
		put(j->error, sizeof(j->error), "%s", call.error);
	} else if (call.status != 200) {
		httpError(j, &call, "the rom");
	} else if (j->multi) {
		RZJob z;
		memset(&z, 0, sizeof(z));
		z.zip = j->temp;
		z.dir = j->dir;
		z.game = j->game;
		z.temp = j->unpack;
		z.expect = j->expect;
		z.cancel = &j->cancel;
		z.done = &j->done;
		z.total = &j->total;
		j->unpacking = 1;
		if (RZ_unpack(&z) != 0) {
			put(j->error, sizeof(j->error), "%s", z.error);
		} else {
			put(j->path, sizeof(j->path), "%s/%s", j->dir, z.launch);
			j->done = j->total;
			j->ok = 1;
		}
	} else if (j->expect > 0 && j->done != j->expect) {
		put(j->error, sizeof(j->error), "the download was %lld bytes, not %lld", (long long)j->done, j->expect);
	} else if (rename(j->temp, j->path) != 0) {
		put(j->error, sizeof(j->error), "can't move the download into place");
	} else {
		j->ok = 1;
	}
	RN_free(&call);
	if (!j->ok || j->multi) unlink(j->temp);
}

static void runUpload(Job* j) {
	char boundary[48];
	unsigned char random[12];
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0 || read(fd, random, sizeof(random)) != (ssize_t)sizeof(random)) {
		for (size_t i = 0; i < sizeof(random); i++) random[i] = (unsigned char)(rand() & 255);
	}
	if (fd >= 0) close(fd);
	char hex[32];
	RM_hex(random, sizeof(random), hex, sizeof(hex));
	put(boundary, sizeof(boundary), "mosaicromm%s", hex);
	char before[RM_NAME_MAX + 256], after[96];
	if (RM_multipart(boundary, "saveFile", j->file, before, sizeof(before), after, sizeof(after)) != 0) {
		put(j->error, sizeof(j->error), "the save's name is too long");
		return;
	}
	char content_type[96];
	put(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
	RNCall call;
	memset(&call, 0, sizeof(call));
	call.method = "POST";
	put(call.target, sizeof(call.target),
		"/api/saves?rom_id=%d&slot=%s&content_hash=%s&autocleanup=true&autocleanup_limit=%d", j->rom_id, SAVE_SLOT,
		j->md5, keep_saves);
	call.content_type = content_type;
	call.body_file = j->path;
	call.body_before = before;
	call.body_after = after;
	call.max_body = JSON_MAX;
	call.cancel = &j->cancel;
	call.done = &j->done;
	if (RN_run(&net, &call) != 0) put(j->error, sizeof(j->error), "%s", call.error);
	else if (call.status != 200 && call.status != 201) httpError(j, &call, "the game");
	else j->ok = 1;
	RN_free(&call);
}

static void runAssets(Job* j) {
	j->assets = calloc(ASSETS_MAX, sizeof(RMAsset));
	if (!j->assets) {
		put(j->error, sizeof(j->error), "out of memory");
		return;
	}
	static const struct {
		const char* path;
		int kind;
	} lists[] = { { "/api/saves", RM_ASSET_SAVE }, { "/api/states", RM_ASSET_STATE } };
	for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
		char target[128];
		put(target, sizeof(target), "%s?rom_id=%d", lists[i].path, j->rom_id);
		RNCall call;
		if (get(j, &call, target, JSON_MAX) != 0) return;
		if (call.status != 200) {
			httpError(j, &call, "the game");
			RN_free(&call);
			return;
		}
		int got = RM_parseAssets(call.data, call.len, lists[i].kind, j->assets, &j->count, ASSETS_MAX);
		RN_free(&call);
		if (got < 0) {
			put(j->error, sizeof(j->error), "the server's list isn't RomM's");
			return;
		}
	}
	RM_sortAssets(j->assets, j->count);
	j->ok = 1;
}

static void runFetch(Job* j) {
	RNCall call;
	memset(&call, 0, sizeof(call));
	call.method = "GET";
	put(call.target, sizeof(call.target), "/api/%s/%d/content", j->asset_kind == RM_ASSET_SAVE ? "saves" : "states",
		j->asset_id);
	call.max_body = SAVE_MAX;
	call.out_path = j->temp;
	call.cancel = &j->cancel;
	call.done = &j->done;
	call.total = &j->total;
	if (RN_run(&net, &call) != 0) put(j->error, sizeof(j->error), "%s", call.error);
	else if (call.status != 200) httpError(j, &call, "that file");
	else if (j->expect >= 0 && j->done != j->expect) put(j->error, sizeof(j->error), "the file came back the wrong size");
	else j->ok = 1;
	RN_free(&call);
	if (!j->ok) unlink(j->temp);
}

static void* jobThread(void* arg) {
	Job* j = arg;
	switch (j->kind) {
	case J_PLATFORMS: runPlatforms(j); break;
	case J_ROMS: runRoms(j); break;
	case J_DOWNLOAD: runDownload(j); break;
	case J_UPLOAD: runUpload(j); break;
	case J_ASSETS: runAssets(j); break;
	case J_FETCH: runFetch(j); break;
	}
	__sync_synchronize();
	j->finished = 1;
	return NULL;
}

static Job* jobNew(int kind) {
	Job* j = calloc(1, sizeof(Job));
	if (j) {
		j->kind = kind;
		j->expect = -1;
	}
	return j;
}

// 1 when a job of kind (for that platform or rom, when above 0) is waiting or running
static int jobPending(int kind, int id) {
	for (int i = -1; i < queued; i++) {
		Job* j = i < 0 ? running : queue[i];
		if (!j || j->kind != kind) continue;
		if (id <= 0 || j->platform_id == id || j->rom_id == id) return 1;
	}
	return 0;
}

static int jobAdd(Job* j) {
	if (!j) return -1;
	if (queued >= JOBS_MAX) {
		jobFree(j);
		return -1;
	}
	queue[queued++] = j;
	return 0;
}

static void jobStartNext(void) {
	if (running || !queued) return;
	Job* j = queue[0];
	memmove(queue, queue + 1, (size_t)(queued - 1) * sizeof(Job*));
	queued--;
	if (pthread_create(&j->thread, NULL, jobThread, j) != 0) {
		put(j->error, sizeof(j->error), "can't start a thread");
		j->finished = 1;
		j->thread = 0;
	}
	running = j;
	if (MOSAIC_HOST_HAS(host, inhibit_sleep)) host->inhibit_sleep(ID, 1);
}

static void jobsCancel(void) {
	for (int i = 0; i < queued; i++) jobFree(queue[i]);
	queued = 0;
	if (running) {
		running->cancel = 1;
		if (running->thread) pthread_join(running->thread, NULL);
		if (running->kind == J_DOWNLOAD || running->kind == J_FETCH) unlink(running->temp);
		jobFree(running);
		running = NULL;
	}
}

///////////////////////////////////////
// the lists kept on the card

static RMPlatform platforms[PLATFORMS_MAX];
static int platform_count = -1; // -1 until read
static unsigned long platforms_asked = 0;

typedef struct RomList {
	int platform_id;
	RMRom* roms;
	int count;
	unsigned long asked;
	char folder[RM_NAME_MAX]; // the Roms folder its stubs go in
} RomList;

static RomList rom_lists[PLATFORMS_MAX];
static int rom_list_count = 0;

static void cleanText(const char* in, char* out, size_t size) {
	size_t n = 0;
	for (const unsigned char* p = (const unsigned char*)in; *p && n + 1 < size; p++) {
		out[n++] = (*p < 0x20 || *p == 0x7f) ? ' ' : (char)*p;
	}
	out[n] = '\0';
}

static void platformsSave(void) {
	size_t cap = (size_t)(platform_count > 0 ? platform_count : 1) * (RM_NAME_MAX + 128);
	char* text = malloc(cap);
	if (!text) return;
	size_t len = 0;
	for (int i = 0; i < platform_count; i++) {
		char name[RM_NAME_MAX];
		cleanText(platforms[i].name, name, sizeof(name));
		char slug[64];
		cleanText(platforms[i].slug, slug, sizeof(slug));
		int n = put(text + len, cap - len, "%d\t%s\t%d\t%s\n", platforms[i].id, slug, platforms[i].rom_count, name);
		if (n > 0 && (size_t)n < cap - len) len += (size_t)n;
	}
	char path[RM_PATH_MAX];
	put(path, sizeof(path), "%s/platforms.tsv", data_dir);
	writeFile(path, text, len);
	free(text);
}

static void platformsLoad(void) {
	char path[RM_PATH_MAX];
	put(path, sizeof(path), "%s/platforms.tsv", data_dir);
	char* text = readFile(path, 1024 * 1024, NULL);
	platform_count = 0;
	if (!text) return;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line && platform_count < PLATFORMS_MAX; line = strtok_r(NULL, "\n", &save)) {
		RMPlatform* p = &platforms[platform_count];
		memset(p, 0, sizeof(*p));
		char* f[4];
		int n = 0;
		for (char* s = line; n < 4; n++) {
			f[n] = s;
			char* tab = n < 3 ? strchr(s, '\t') : NULL;
			if (n < 3 && !tab) break;
			if (tab) {
				*tab = '\0';
				s = tab + 1;
			}
		}
		if (n < 4) continue;
		p->id = atoi(f[0]);
		put(p->slug, sizeof(p->slug), "%s", f[1]);
		p->rom_count = atoi(f[2]);
		put(p->name, sizeof(p->name), "%s", f[3]);
		RM_tagFor(p->slug, p->tag, sizeof(p->tag), NULL, 0);
		if (p->id > 0) platform_count++;
	}
	free(text);
}

static const RMPlatform* platformById(int id) {
	for (int i = 0; i < platform_count; i++) {
		if (platforms[i].id == id) return &platforms[i];
	}
	return NULL;
}

// The Roms folder for a tag: one on the card whose name ends in "(TAG)",
// else the name stock gives it.
static void romsFolder(const char* slug, const char* tag, char* out, size_t size) {
	char roms[RM_PATH_MAX];
	put(roms, sizeof(roms), "%s/Roms", sd);
	char want[RM_TAG_MAX + 4];
	put(want, sizeof(want), "(%s)", tag);
	size_t want_len = strlen(want);
	out[0] = '\0';
	DIR* d = opendir(roms);
	if (d) {
		struct dirent* e;
		while ((e = readdir(d))) {
			size_t len = strlen(e->d_name);
			if (!RM_safeName(e->d_name) || len < want_len || strcmp(e->d_name + len - want_len, want) != 0) continue;
			char full[RM_PATH_MAX];
			put(full, sizeof(full), "%s/%s", roms, e->d_name);
			if (!isDir(full)) continue;
			if (!out[0] || strcmp(e->d_name, out) < 0) put(out, size, "%s", e->d_name);
		}
		closedir(d);
	}
	if (!out[0]) RM_tagFor(slug, NULL, 0, out, size);
}

static void romListSave(const RomList* list) {
	size_t cap = (size_t)(list->count > 0 ? list->count : 1) * (3 * RM_NAME_MAX + 64);
	char* text = malloc(cap);
	if (!text) return;
	size_t len = 0;
	for (int i = 0; i < list->count; i++) {
		const RMRom* r = &list->roms[i];
		char name[RM_NAME_MAX];
		cleanText(r->name, name, sizeof(name));
		char launch[RM_NAME_MAX];
		cleanText(r->launch, launch, sizeof(launch));
		int n = put(text + len, cap - len, "%d\t%lld\t%d\t%s\t%s\t%s\n", r->id, r->size, r->multi, r->file, name, launch);
		if (n > 0 && (size_t)n < cap - len) len += (size_t)n;
	}
	char path[RM_PATH_MAX];
	put(path, sizeof(path), "%s/roms-%d.tsv", data_dir, list->platform_id);
	writeFile(path, text, len);
	free(text);
}

static RomList* romList(int platform_id) {
	for (int i = 0; i < rom_list_count; i++) {
		if (rom_lists[i].platform_id == platform_id) return &rom_lists[i];
	}
	if (rom_list_count >= PLATFORMS_MAX) return NULL;
	RomList* list = &rom_lists[rom_list_count++];
	memset(list, 0, sizeof(*list));
	list->platform_id = platform_id;
	list->count = -1;
	return list;
}

static void romListLoad(RomList* list) {
	if (list->count >= 0) return;
	list->count = 0;
	char path[RM_PATH_MAX];
	put(path, sizeof(path), "%s/roms-%d.tsv", data_dir, list->platform_id);
	char* text = readFile(path, 64 * 1024 * 1024, NULL);
	if (!text) return;
	int lines = 1;
	for (char* p = text; *p; p++) lines += *p == '\n';
	if (lines > ROMS_MAX) lines = ROMS_MAX;
	free(list->roms);
	list->roms = calloc((size_t)lines, sizeof(RMRom));
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line && list->roms && list->count < lines;
			line = strtok_r(NULL, "\n", &save)) {
		// id, size, multi, file, name and, since multi-file roms, the file to launch
		char* f[6] = { NULL };
		int n = 0;
		for (char* s = line; s && n < 6; n++) {
			f[n] = s;
			s = strchr(s, '\t');
			if (s) *s++ = '\0';
		}
		if (n < 5 || !RM_safeName(f[3])) continue;
		RMRom* r = &list->roms[list->count];
		memset(r, 0, sizeof(*r));
		r->id = atoi(f[0]);
		r->platform_id = list->platform_id;
		r->size = atoll(f[1]);
		r->multi = atoi(f[2]) != 0;
		put(r->file, sizeof(r->file), "%s", f[3]);
		put(r->name, sizeof(r->name), "%s", f[4]);
		if (n > 5) put(r->launch, sizeof(r->launch), "%s", f[5]);
		char why[64];
		if (r->multi && (!r->launch[0] || RM_zipNameCheck(r->launch, strlen(r->launch), why, sizeof(why)) != 0)) continue;
		if (r->id > 0) list->count++;
	}
	free(text);
}

// The virtual path of a platform's folder
static void platformPath(const RMPlatform* p, char* out, size_t size) {
	char name[RM_NAME_MAX];
	size_t n = 0;
	for (const char* s = p->name; *s && n + 1 < sizeof(name); s++) name[n++] = *s == '/' ? '-' : *s;
	name[n] = '\0';
	put(out, size, "%s/%s", root_path, name);
}

// A stub's path, or "" when it can't have one: a game of several files is
// its folder's file to launch (its .m3u, or its one disc)
static void stubPath(const RMPlatform* p, RomList* list, const RMRom* r, char* out, size_t size) {
	out[0] = '\0';
	if (!p->tag[0]) return;
	if (!list->folder[0]) romsFolder(p->slug, p->tag, list->folder, sizeof(list->folder));
	if (!list->folder[0]) return;
	if (r->multi) put(out, size, "%s/Roms/%s/%s/%s", sd, list->folder, r->file, r->launch);
	else put(out, size, "%s/Roms/%s/%s", sd, list->folder, r->file);
	if (strlen(out) >= RM_STOCK_PATH_MAX) out[0] = '\0'; // too long for NextUI to launch
}

// The path a game is known by, for its record and saves: a file in a
// folder with that folder's .m3u is the .m3u, as minarch names the
// game's saves and states after it (and NextUI its recents)
static void gameKey(const char* path, char* out, size_t size) {
	char m3u[RM_PATH_MAX];
	if (RM_m3uFor(path, m3u, sizeof(m3u)) == 0 && isFile(m3u)) put(out, size, "%s", m3u);
	else put(out, size, "%s", path);
}

typedef struct Found {
	const RMPlatform* platform;
	RomList* list;
	const RMRom* rom;
} Found;

// The rom from RomM a card path is, from the lists kept on the card
static int findRom(const char* path, Found* out) {
	memset(out, 0, sizeof(*out));
	if (!path || platform_count <= 0) return 0;
	char roms[RM_PATH_MAX];
	put(roms, sizeof(roms), "%s/Roms/", sd);
	if (strncmp(path, roms, strlen(roms)) != 0) return 0;
	const char* folder = path + strlen(roms);
	const char* slash = strchr(folder, '/');
	if (!slash) return 0;
	// a single file's name, or a game folder's for anything in it
	const char* file = slash + 1;
	const char* inner = strchr(file, '/');
	size_t file_len = inner ? (size_t)(inner - file) : strlen(file);
	for (int i = 0; i < platform_count; i++) {
		const RMPlatform* p = &platforms[i];
		if (!p->tag[0]) continue;
		char want[RM_TAG_MAX + 4];
		put(want, sizeof(want), "(%s)", p->tag);
		size_t folder_len = (size_t)(slash - folder), want_len = strlen(want);
		if (folder_len < want_len || strncmp(slash - want_len, want, want_len) != 0) continue;
		RomList* list = romList(p->id);
		if (!list) continue;
		romListLoad(list);
		for (int k = 0; k < list->count; k++) {
			const RMRom* r = &list->roms[k];
			if (r->multi != (inner != NULL) || strlen(r->file) != file_len || strncmp(r->file, file, file_len) != 0) continue;
			out->platform = p;
			out->list = list;
			out->rom = &list->roms[k];
			return 1;
		}
	}
	return 0;
}

///////////////////////////////////////
// what was synced and downloaded, kept in synced.txt and downloads.txt
// and in memory, so the services answer without the network or a read

static RMSynced synced[RM_SYNCED_MAX];
static int synced_count = 0;
static RMDownload downloads[RM_DOWNLOADS_MAX];
static int download_count = 0;

static int fileStat(const char* path, long long* size, long long* mtime) {
	struct stat st;
	if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
	*size = (long long)st.st_size;
	*mtime = (long long)st.st_mtime;
	return 0;
}

static void syncedLoad(void) {
	synced_count = 0;
	char file[RM_PATH_MAX];
	put(file, sizeof(file), "%s/synced.txt", data_dir);
	char* text = readFile(file, 4 * 1024 * 1024, NULL);
	char* save = NULL;
	for (char* line = text ? strtok_r(text, "\n", &save) : NULL; line && synced_count < RM_SYNCED_MAX;
			line = strtok_r(NULL, "\n", &save)) {
		if (RM_syncedParse(line, &synced[synced_count]) == 0) synced_count++;
	}
	free(text);
}

static void syncedSave(void) {
	char file[RM_PATH_MAX];
	put(file, sizeof(file), "%s/synced.txt", data_dir);
	size_t cap = (size_t)synced_count * (RM_PATH_MAX + 96) + 1;
	char* out = malloc(cap);
	if (!out) return;
	size_t used = 0;
	for (int i = 0; i < synced_count; i++) {
		int n = RM_syncedFormat(&synced[i], out + used, cap - used);
		if (n > 0) used += (size_t)n;
	}
	writeFile(file, out, used);
	free(out);
}

// Notes that path was just uploaded (down 0) or restored (down 1), as it
// was at size and mtime
static void syncedNote(const char* path, int down, long long size, long long mtime) {
	RMSynced e = { 0 };
	e.down = down;
	e.at = (long long)time(NULL);
	e.size = size;
	e.mtime = mtime;
	put(e.path, sizeof(e.path), "%s", path);
	synced_count = RM_syncedPut(synced, synced_count, RM_SYNCED_MAX, &e);
	syncedSave();
}

static void downloadsLoad(void) {
	download_count = 0;
	char file[RM_PATH_MAX];
	put(file, sizeof(file), "%s/downloads.txt", data_dir);
	char* text = readFile(file, 256 * 1024, NULL);
	char* save = NULL;
	for (char* line = text ? strtok_r(text, "\n", &save) : NULL; line && download_count < RM_DOWNLOADS_MAX;
			line = strtok_r(NULL, "\n", &save)) {
		if (RM_downloadParse(line, &downloads[download_count]) == 0) download_count++;
	}
	free(text);
}

// Logs a finished download, keeps the log, and tells other plugins
static void downloadNote(const char* path, const char* name) {
	RMDownload d = { 0 };
	d.at = (long long)time(NULL);
	put(d.path, sizeof(d.path), "%s", path);
	const char* slash = strrchr(path, '/');
	cleanText(name && name[0] ? name : slash ? slash + 1 : path, d.name, sizeof(d.name));
	for (char* p = d.name; *p; p++) {
		if (*p == '\t') *p = ' ';
	}
	download_count = RM_downloadPut(downloads, download_count, RM_DOWNLOADS_MAX, &d);
	char file[RM_PATH_MAX];
	put(file, sizeof(file), "%s/downloads.txt", data_dir);
	static char text[RM_DOWNLOADS_MAX * (RM_PATH_MAX + RM_NAME_MAX + 32)];
	size_t used = 0;
	for (int i = 0; i < download_count; i++) {
		int n = RM_downloadFormat(&downloads[i], text + used, sizeof(text) - used);
		if (n > 0) used += (size_t)n;
	}
	writeFile(file, text, used);
	char payload[1024];
	if (MOSAIC_HOST_HAS(host, publish) && RM_downloadPayload(&d, payload, sizeof(payload)) >= 0
			&& host->publish(ID ".download_done", payload) != 0) {
		say("couldn't publish romm.download_done");
	}
}

///////////////////////////////////////
// games from RomM and their saves

#define RECORDS_MAX 4096

static int recordFind(const char* path, RMRecord* out) {
	char file[RM_PATH_MAX];
	put(file, sizeof(file), "%s/games.txt", data_dir);
	char* text = readFile(file, 8 * 1024 * 1024, NULL);
	if (!text) return 0;
	int found = 0;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		RMRecord r;
		if (RM_recordParse(line, &r) == 0 && strcmp(r.path, path) == 0) {
			*out = r;
			found = 1;
		}
	}
	free(text);
	return found;
}

// Puts record in games.txt in place of any for the same path
static void recordPut(const RMRecord* record) {
	char file[RM_PATH_MAX];
	put(file, sizeof(file), "%s/games.txt", data_dir);
	size_t len = 0;
	char* text = readFile(file, 8 * 1024 * 1024, &len);
	size_t cap = len + RM_PATH_MAX + 256;
	char* out = malloc(cap);
	if (!out) {
		free(text);
		return;
	}
	size_t used = 0;
	int lines = 0;
	char* save = NULL;
	for (char* line = text ? strtok_r(text, "\n", &save) : NULL; line; line = strtok_r(NULL, "\n", &save)) {
		RMRecord r;
		if (RM_recordParse(line, &r) != 0 || strcmp(r.path, record->path) == 0 || lines >= RECORDS_MAX - 1) continue;
		int n = put(out + used, cap - used, "%s\n", line);
		if (n > 0 && (size_t)n < cap - used) used += (size_t)n;
		lines++;
	}
	int n = RM_recordFormat(record, out + used, cap - used);
	if (n > 0) used += (size_t)n;
	writeFile(file, out, used);
	free(out);
	free(text);
}

// Saves/<tag>/: the newest of the save files minarch may have written for
// rom_file. Returns its index in RM_saveNames, or -1 with none.
static int localSave(const char* tag, const char* rom_file, char* out, size_t size) {
	char names[3][RM_NAME_MAX];
	RM_saveNames(rom_file, names);
	int pick = -1;
	time_t newest = 0;
	for (int i = 0; i < 3; i++) {
		char path[RM_PATH_MAX];
		put(path, sizeof(path), "%s/Saves/%s/%s", sd, tag, names[i]);
		struct stat st;
		if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		if (pick < 0 || st.st_mtime > newest) {
			pick = i;
			newest = st.st_mtime;
			put(out, size, "%s", path);
		}
	}
	return pick;
}

static const char* baseName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static char pending_path[RM_PATH_MAX];

// After a game from RomM: uploads its save when it changed since the last sync
static void syncSave(int quiet) {
	char file[RM_PATH_MAX];
	put(file, sizeof(file), "%s/pending.txt", data_dir);
	char* text = readFile(file, RM_PATH_MAX, NULL);
	if (!text) return;
	put(pending_path, sizeof(pending_path), "%s", text);
	pending_path[strcspn(pending_path, "\r\n")] = '\0';
	free(text);
	RMRecord record;
	if (!recordFind(pending_path, &record)) {
		unlink(file);
		return;
	}
	char save[RM_PATH_MAX];
	if (localSave(record.tag, baseName(record.path), save, sizeof(save)) < 0) {
		sayf("no save for %s", baseName(record.path));
		unlink(file);
		return;
	}
	char md5[33];
	if (RN_md5File(save, SAVE_MAX, md5) != 0) {
		sayf("can't read the save %s", save);
		unlink(file);
		return;
	}
	if (!strcmp(md5, record.value)) {
		sayf("save for %s unchanged", baseName(record.path));
		// the same as the server's, even if it was written again
		const RMSynced* known = RM_syncedFind(synced, synced_count, save);
		long long size, mtime;
		if (known && fileStat(save, &size, &mtime) == 0 && (known->size != size || known->mtime != mtime)) {
			syncedNote(save, known->down, size, mtime);
		}
		unlink(file);
		return;
	}
	if (!sync_saves || jobPending(J_UPLOAD, record.rom_id)) return;
	if (!networkReady(quiet)) return; // pending stays for the next return or start
	Job* j = jobNew(J_UPLOAD);
	if (!j) return;
	j->rom_id = record.rom_id;
	put(j->path, sizeof(j->path), "%s", save);
	put(j->file, sizeof(j->file), "%s", baseName(save));
	put(j->tag, sizeof(j->tag), "%s", record.tag);
	put(j->name, sizeof(j->name), "%s", record.path);
	put(j->md5, sizeof(j->md5), "%s", md5);
	if (fileStat(save, &j->file_size, &j->file_mtime) != 0) j->file_size = -1;
	if (jobAdd(j) == 0) sayf("uploading %s for rom %d", j->file, j->rom_id);
}

///////////////////////////////////////
// the status pill

static unsigned long status_written = 0;
static long long status_done = -1;

static void statusWrite(const char* state, long long done, long long total, const char* label) {
	char text[256];
	int len = RM_statusText(state, done, total, label, text, sizeof(text));
	if (len < 0) return;
	char dir[RM_PATH_MAX];
	put(dir, sizeof(dir), "%s/launcher/status.d", host->userdata_path);
	makeDirs(dir);
	char tmp[RM_PATH_MAX];
	put(tmp, sizeof(tmp), "%s/.romm.tmp", dir);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) return;
	int ok = write(fd, text, (size_t)len) == len;
	close(fd);
	if (!ok || rename(tmp, status_path) != 0) unlink(tmp);
}

static void statusClear(void) {
	unlink(status_path);
	status_done = -1;
}

///////////////////////////////////////
// downloading a stub

static char launch_after[RM_PATH_MAX]; // the downloaded rom to launch
static int open_download_screen = 0;

static void startDownload(const Found* f, const char* path) {
	if (jobPending(J_DOWNLOAD, f->rom->id)) return;
	char dir[RM_PATH_MAX];
	put(dir, sizeof(dir), "%s/Roms/%s", sd, f->list->folder);
	if (!RM_safeName(f->list->folder)) return;
	makeDirs(dir);
	if (!insideCard(dir, "Roms")) {
		sayf("refusing to download into %s: it isn't a folder in Roms", dir);
		toast(tr("RomM: that system's folder isn't on the card"));
		return;
	}
	struct statvfs vfs;
	// a zip and what it unpacks to are on the card at once
	long long need = f->rom->size * (f->rom->multi ? 2 : 1) + 1024 * 1024;
	if (statvfs(dir, &vfs) == 0 && f->rom->size > 0 && (long long)vfs.f_bavail * (long long)vfs.f_frsize < need) {
		toast(tr("RomM: not enough space on the card"));
		say("not enough space for the download");
		return;
	}
	if (f->rom->multi) {
		char game[RM_PATH_MAX];
		put(game, sizeof(game), "%s/%s", dir, f->rom->file);
		struct stat st;
		if (lstat(game, &st) == 0) {
			// never unpacked over what's there
			sayf("not downloading into %s: it's already on the card", game);
			toast(tr("RomM: a folder of that name is already on the card"));
			return;
		}
	}
	Job* j = jobNew(J_DOWNLOAD);
	if (!j) return;
	j->rom_id = f->rom->id;
	j->expect = f->rom->size > 0 ? f->rom->size : -1;
	j->multi = f->rom->multi;
	put(j->file, sizeof(j->file), "%s", f->rom->file);
	put(j->game, sizeof(j->game), "%s", f->rom->file);
	put(j->name, sizeof(j->name), "%s", f->rom->name);
	put(j->tag, sizeof(j->tag), "%s", f->platform->tag);
	put(j->dir, sizeof(j->dir), "%s", dir);
	put(j->path, sizeof(j->path), "%s", path);
	put(j->temp, sizeof(j->temp), "%s/.romm-%d.part", dir, f->rom->id);
	put(j->unpack, sizeof(j->unpack), "%s/.romm-%d.unpack", dir, f->rom->id);
	// a download goes before lists waiting
	if (queued >= JOBS_MAX) {
		jobFree(j);
		return;
	}
	memmove(queue + 1, queue, (size_t)queued * sizeof(Job*));
	queue[0] = j;
	queued++;
	open_download_screen = 1;
	sayf("downloading %s (rom %d, %lld bytes%s) to %s", j->file, j->rom_id, j->expect, j->multi ? ", a zip" : "", path);
	statusWrite("running", 0, j->expect > 0 ? j->expect / 1024 : 0, j->name);
}

static Job* activeDownload(void) {
	if (running && running->kind == J_DOWNLOAD) return running;
	for (int i = 0; i < queued; i++) {
		if (queue[i]->kind == J_DOWNLOAD) return queue[i];
	}
	return NULL;
}

static void cancelDownload(void) {
	Job* j = activeDownload();
	if (!j) return;
	if (j == running) {
		j->cancel = 1;
	} else {
		for (int i = 0; i < queued; i++) {
			if (queue[i] != j) continue;
			memmove(queue + i, queue + i + 1, (size_t)(queued - i - 1) * sizeof(Job*));
			queued--;
			break;
		}
		jobFree(j);
		statusClear();
	}
	say("download cancelled");
}

///////////////////////////////////////
// drawing

static MosaicColor themeColor(int index, unsigned int fallback) {
	int rgb = host->theme_color(index);
	return mosaic_color(rgb < 0 ? fallback : (unsigned int)rgb, 255);
}

// the top of the screen below the status pill and header
static int topOf(const MosaicScreenView* v) {
	int top = 0;
	if (v->status.w > 0 && v->status.y + v->status.h > top) top = v->status.y + v->status.h;
	if (v->header.w > 0 && v->header.y + v->header.h > top) top = v->header.y + v->header.h;
	return top;
}

static void background(MosaicSurface* screen, const MosaicScreenView* v) {
	MosaicColor bg = themeColor(MOSAIC_COLOR_BACKGROUND, 0x000000);
	int top = topOf(v);
	MosaicRect below = { 0, top, v->area.w, v->area.h - top };
	host->fill_rect(screen, &below, bg);
	int left = v->area.w;
	if (v->header.w > 0 && v->header.x < left) left = v->header.x;
	if (v->status.w > 0 && v->status.x < left) left = v->status.x;
	MosaicRect band = { 0, 0, left, top };
	if (band.w > 0 && band.h > 0) host->fill_rect(screen, &band, bg);
}

static void titlePill(MosaicSurface* screen, const MosaicScreenView* v, const char* text) {
	int scale = host->screen_scale();
	int left = v->area.w;
	if (v->header.w > 0 && v->header.x < left) left = v->header.x;
	if (v->status.w > 0 && v->status.x < left) left = v->status.x;
	int pad = 10 * scale, h = host->text_height(MOSAIC_FONT_LARGE) + 8 * scale;
	int max = left - 2 * pad - 24 * scale;
	if (max <= 0) return;
	char cut[RM_NAME_MAX + 64];
	int w = host->text_truncate(MOSAIC_FONT_LARGE, text, max, cut, sizeof(cut));
	if (w < 0) return;
	MosaicRect pill = { pad, pad / 2, w + 24 * scale, h };
	host->fill_round_rect(screen, &pill, h / 2, themeColor(MOSAIC_COLOR_ACCENT, 0x9B2257));
	host->draw_text(screen, MOSAIC_FONT_LARGE, cut, pill.x + 12 * scale, pill.y + 4 * scale,
		themeColor(MOSAIC_COLOR_HINT, 0xFFFFFF), max);
}

static void centred(MosaicSurface* screen, int font, const char* text, int y, int width, MosaicColor color) {
	char cut[512];
	int w = host->text_truncate(font, text, width - 20, cut, sizeof(cut));
	if (w < 0) return;
	host->draw_text(screen, font, cut, (width - w) / 2, y, color, width);
}

static void hints(MosaicSurface* screen, const MosaicHint* left, int left_count, const MosaicHint* right, int right_count) {
	if (!MOSAIC_HOST_HAS(host, draw_hints)) return;
	if (left_count) host->draw_hints(screen, MOSAIC_HINTS_LEFT, left, left_count, NULL);
	if (right_count) host->draw_hints(screen, MOSAIC_HINTS_RIGHT, right, right_count, NULL);
}

///////////////////////////////////////
// the download screen

static char download_shown[64];

static int downloadInput(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) {
		cancelDownload();
		return MOSAIC_SCREEN_CLOSE;
	}
	return MOSAIC_SCREEN_STAY;
}

static int downloadAnimating(void* data) {
	(void)data;
	return 1;
}

static int downloadRender(MosaicSurface* screen, const MosaicScreenView* v, void* data) {
	(void)data;
	Job* j = activeDownload();
	background(screen, v);
	titlePill(screen, v, "RomM");
	int scale = host->screen_scale();
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	int line = host->text_height(MOSAIC_FONT_LARGE);
	int y = v->area.h / 2 - 2 * line;
	if (!j) {
		centred(screen, MOSAIC_FONT_LARGE, tr("Starting..."), y, v->area.w, ink);
		return 0;
	}
	char title[RM_NAME_MAX + 256];
	put(title, sizeof(title), j->unpacking ? tr("Unpacking %s") : tr("Downloading %s"), j->name);
	centred(screen, MOSAIC_FONT_LARGE, title, y, v->area.w, ink);
	long long done = j->done, total = j->total > 0 ? j->total : j->expect;
	int bar_w = v->area.w * 2 / 3, bar_h = 8 * scale;
	MosaicRect bar = { (v->area.w - bar_w) / 2, y + line + 12 * scale, bar_w, bar_h };
	host->fill_round_rect(screen, &bar, bar_h / 2, themeColor(MOSAIC_COLOR_ACCENT2, 0x333333));
	int percent = total > 0 ? (int)(done * 100 / total) : 0;
	if (percent > 100) percent = 100;
	MosaicRect fill = bar;
	fill.w = bar_w * percent / 100;
	if (fill.w >= bar_h) host->fill_round_rect(screen, &fill, bar_h / 2, themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF));
	char a[32], b[32], text[256];
	RM_formatSize(done, a, sizeof(a));
	RM_formatSize(total, b, sizeof(b));
	if (total > 0) put(text, sizeof(text), tr("%s of %s"), a, b);
	else put(text, sizeof(text), "%s", a);
	centred(screen, MOSAIC_FONT_SMALL, text, bar.y + bar_h + 12 * scale, v->area.w, ink);
	MosaicHint cancel = { MOSAIC_BUTTON_B, "CANCEL" };
	hints(screen, NULL, 0, &cancel, 1);
	char shown[64];
	put(shown, sizeof(shown), "%s%d%%", j->unpacking ? "unpacking " : "", percent / 25 * 25);
	if (strcmp(shown, download_shown) != 0) {
		put(download_shown, sizeof(download_shown), "%s", shown);
		sayf("download screen: %s, %s", j->file, shown);
	}
	return 0;
}

static int downloadEnter(void* data) {
	(void)data;
	download_shown[0] = '\0';
	return 0;
}

///////////////////////////////////////
// Flashback

static struct {
	int rom_id;
	char tag[RM_TAG_MAX];
	char path[RM_PATH_MAX]; // the rom
	char name[RM_NAME_MAX];
	RMAsset* assets;
	int count; // -1 while loading
	int selected, first;
	char error[256];
	int asked; // the asset the confirm is about
	char shown[256];
} fb;

// Where minarch keeps the Flashback game's states, read once as it opens:
// its <tag>-<core> folders in .userdata/shared, the core the system's pak
// runs, and the State format setting
#define STATE_FOLDERS_MAX 16
static struct {
	char folders[STATE_FOLDERS_MAX][RM_NAME_MAX];
	const char* names[STATE_FOLDERS_MAX];
	int count;
	char core[64];
	int format;
} states;

static void statesLoad(const char* tag) {
	memset(&states, 0, sizeof(states));
	char shared[RM_PATH_MAX];
	put(shared, sizeof(shared), "%s/.userdata/shared", sd);
	DIR* d = opendir(shared);
	char prefix[RM_TAG_MAX + 2];
	put(prefix, sizeof(prefix), "%s-", tag);
	struct dirent* e;
	while (d && (e = readdir(d)) && states.count < STATE_FOLDERS_MAX) {
		if (strncmp(e->d_name, prefix, strlen(prefix)) != 0 || !RM_safeName(e->d_name)) continue;
		char full[RM_PATH_MAX];
		put(full, sizeof(full), "%s/%s", shared, e->d_name);
		if (!isDir(full)) continue;
		put(states.folders[states.count], RM_NAME_MAX, "%s", e->d_name);
		states.names[states.count] = states.folders[states.count];
		states.count++;
	}
	if (d) closedir(d);
	// the pak stock launches the system with, as its getEmuPath looks
	const char* paks[] = { "%s/.system/%s/paks/Emus/%s.pak/launch.sh", "%s/Emus/%s/%s.pak/launch.sh" };
	for (size_t i = 0; i < sizeof(paks) / sizeof(paks[0]) && !states.core[0]; i++) {
		char path[RM_PATH_MAX];
		snprintf(path, sizeof(path), paks[i], sd, host->platform, tag);
		char* text = readFile(path, 64 * 1024, NULL);
		if (text && RM_emuCore(text, states.core, sizeof(states.core)) != 0) states.core[0] = '\0';
		free(text);
	}
	char path[RM_PATH_MAX];
	put(path, sizeof(path), "%s/minuisettings.txt", shared);
	char* text = readFile(path, 64 * 1024, NULL);
	states.format = text ? RM_stateFormat(text) : 0;
	free(text);
	sayf("states for %s: %d folders, core %s, state format %d", tag, states.count, states.core[0] ? states.core : "unknown",
		states.format);
}

// The state folder a state from the server goes in: the pak's core's, the
// only one there is, or the one for the state's emulator
static int stateDir(const char* tag, const char* emulator, char* out, size_t size) {
	char name[RM_NAME_MAX];
	if (RM_stateFolder(tag, states.names, states.count, states.core, emulator, name, sizeof(name)) != 0) return -1;
	put(out, size, "%s/.userdata/shared/%s", sd, name);
	return 0;
}

// Where a server file goes on the card for the Flashback game, or -1
static int assetTarget(const RMAsset* a, char* out, size_t size) {
	const char* rom_file = baseName(fb.path);
	if (a->kind == RM_ASSET_SAVE) {
		char names[3][RM_NAME_MAX], existing[RM_PATH_MAX];
		RM_saveNames(rom_file, names);
		int have = localSave(fb.tag, rom_file, existing, sizeof(existing));
		char name[RM_NAME_MAX];
		RM_saveTarget(rom_file, a->file, have >= 0 ? 1 << have : 0, name, sizeof(name));
		put(out, size, "%s/Saves/%s/%s", sd, fb.tag, name);
		return 0;
	}
	// any layout minarch writes, put back in the one this device's setting reads
	int slot = RM_stateSlot(a->file);
	char dir[RM_PATH_MAX], name[RM_NAME_MAX];
	if (slot < 0 || stateDir(fb.tag, a->emulator, dir, sizeof(dir)) != 0) return -1;
	if (RM_stateName(rom_file, states.format, slot, name, sizeof(name)) != 0) return -1;
	put(out, size, "%s/%s", dir, name);
	return 0;
}

static void assetLabel(const RMAsset* a, char* out, size_t size) {
	char when[64], bytes[64];
	RM_formatTime(a->updated, when, sizeof(when));
	RM_formatSize(a->size, bytes, sizeof(bytes));
	int slot = a->kind == RM_ASSET_STATE ? RM_stateSlot(a->file) : -1;
	char kind[128];
	if (a->kind == RM_ASSET_SAVE) put(kind, sizeof(kind), "%s", tr("Save"));
	else if (slot >= 0) put(kind, sizeof(kind), tr("State %d"), slot);
	else put(kind, sizeof(kind), "%s", tr("State"));
	char target[RM_PATH_MAX];
	int here = assetTarget(a, target, sizeof(target)) == 0;
	put(out, size, "%s  %s  %s%s%s", when, kind, bytes, here ? "" : "  ", here ? "" : tr("(can't restore)"));
}

static void flashbackLoad(void) {
	free(fb.assets);
	fb.assets = NULL;
	fb.count = -1;
	fb.error[0] = '\0';
	if (!networkReady(0)) {
		put(fb.error, sizeof(fb.error), "%s", tr("No network"));
		fb.count = 0;
		return;
	}
	Job* j = jobNew(J_ASSETS);
	if (!j) return;
	j->rom_id = fb.rom_id;
	if (jobAdd(j) != 0) {
		put(fb.error, sizeof(fb.error), "%s", tr("Busy, try again"));
		fb.count = 0;
	}
}

static int flashbackEnter(void* data) {
	(void)data;
	fb.selected = fb.first = 0;
	fb.shown[0] = '\0';
	flashbackLoad();
	return 0;
}

static void flashbackExit(void* data) {
	(void)data;
	free(fb.assets);
	fb.assets = NULL;
	fb.count = 0;
}

static void restoreConfirmed(void* data) {
	(void)data;
	if (fb.asked < 0 || fb.asked >= fb.count) return;
	const RMAsset* a = &fb.assets[fb.asked];
	char target[RM_PATH_MAX];
	if (assetTarget(a, target, sizeof(target)) != 0) return;
	char dir[RM_PATH_MAX];
	put(dir, sizeof(dir), "%s", target);
	*strrchr(dir, '/') = '\0';
	makeDirs(dir);
	if (!insideCard(dir, a->kind == RM_ASSET_SAVE ? "Saves" : ".userdata/shared")) {
		toast(tr("RomM: that folder isn't on the card"));
		return;
	}
	if (!networkReady(0)) return;
	Job* j = jobNew(J_FETCH);
	if (!j) return;
	j->asset_kind = a->kind;
	j->asset_id = a->id;
	j->rom_id = fb.rom_id;
	j->expect = a->size > 0 ? a->size : -1;
	put(j->path, sizeof(j->path), "%s", target);
	put(j->temp, sizeof(j->temp), "%s/.romm-restore-%d.part", dir, a->id);
	put(j->tag, sizeof(j->tag), "%s", fb.tag);
	put(j->name, sizeof(j->name), "%s", fb.path);
	if (jobAdd(j) == 0) {
		sayf("restoring %s %d to %s", a->kind == RM_ASSET_SAVE ? "save" : "state", a->id, target);
		toast(tr("RomM: restoring..."));
	}
}

static int flashbackInput(const MosaicInput* in, void* data) {
	(void)data;
	if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_B)) return MOSAIC_SCREEN_CLOSE;
	if (fb.count <= 0) {
		if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X)) {
			flashbackLoad();
			host->request_redraw();
		}
		return MOSAIC_SCREEN_STAY;
	}
	int before = fb.selected;
	if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_UP)) fb.selected = fb.selected > 0 ? fb.selected - 1 : fb.count - 1;
	else if (in->repeated & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_DOWN)) fb.selected = fb.selected + 1 < fb.count ? fb.selected + 1 : 0;
	else if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_X)) flashbackLoad();
	else if (in->pressed & MOSAIC_BUTTON_BIT(MOSAIC_BUTTON_A)) {
		const RMAsset* a = &fb.assets[fb.selected];
		char target[RM_PATH_MAX];
		if (assetTarget(a, target, sizeof(target)) != 0) {
			toast(tr("RomM: this state can't be restored on this device"));
			return MOSAIC_SCREEN_STAY;
		}
		if (!MOSAIC_HOST_HAS(host, confirm)) return MOSAIC_SCREEN_STAY;
		fb.asked = fb.selected;
		char label[RM_PATH_MAX];
		assetLabel(a, label, sizeof(label));
		char question[RM_PATH_MAX + 512];
		put(question, sizeof(question), a->kind == RM_ASSET_SAVE ? tr("Restore this save?\n%s\nThe one on the card is backed up first.")
			: tr("Restore this state?\n%s\nThe one on the card is backed up first."), label);
		host->confirm(question, tr("RESTORE"), restoreConfirmed, NULL);
		return MOSAIC_SCREEN_STAY;
	}
	if (fb.selected != before) host->request_redraw();
	return MOSAIC_SCREEN_STAY;
}

static int flashbackRender(MosaicSurface* screen, const MosaicScreenView* v, void* data) {
	(void)data;
	background(screen, v);
	char title[RM_NAME_MAX + 256];
	put(title, sizeof(title), tr("Flashback: %s"), fb.name);
	titlePill(screen, v, title);
	int scale = host->screen_scale();
	MosaicColor ink = themeColor(MOSAIC_COLOR_LIST_TEXT, 0xFFFFFF);
	MosaicColor ink_selected = themeColor(MOSAIC_COLOR_LIST_TEXT_SELECTED, 0x000000);
	MosaicColor pill = themeColor(MOSAIC_COLOR_MAIN, 0xFFFFFF);
	int line = host->text_height(MOSAIC_FONT_LARGE);
	int row_h = line + 8 * scale;
	int top = topOf(v);
	int title_bottom = 5 * scale + line + 8 * scale;
	if (title_bottom > top) top = title_bottom;
	top += 6 * scale;
	int bottom = v->area.h - 2 * row_h;
	char shown[256];
	if (fb.count < 0) {
		centred(screen, MOSAIC_FONT_LARGE, tr("Loading..."), v->area.h / 2 - line, v->area.w, ink);
		put(shown, sizeof(shown), "loading");
	} else if (fb.count == 0) {
		centred(screen, MOSAIC_FONT_LARGE, fb.error[0] ? fb.error : tr("No saves or states on the server"),
			v->area.h / 2 - line, v->area.w, ink);
		put(shown, sizeof(shown), "empty: %s", fb.error[0] ? fb.error : "none");
	} else {
		int rows = (bottom - top) / row_h;
		if (rows < 1) rows = 1;
		if (fb.selected < fb.first) fb.first = fb.selected;
		if (fb.selected >= fb.first + rows) fb.first = fb.selected - rows + 1;
		for (int i = fb.first; i < fb.count && i < fb.first + rows; i++) {
			char label[RM_PATH_MAX];
			assetLabel(&fb.assets[i], label, sizeof(label));
			int y = top + (i - fb.first) * row_h;
			char cut[RM_PATH_MAX];
			int w = host->text_truncate(MOSAIC_FONT_LARGE, label, v->area.w - 40 * scale, cut, sizeof(cut));
			if (w < 0) continue;
			if (i == fb.selected) {
				MosaicRect r = { 10 * scale, y, w + 20 * scale, row_h };
				host->fill_round_rect(screen, &r, row_h / 2, pill);
			}
			host->draw_text(screen, MOSAIC_FONT_LARGE, cut, 20 * scale, y + 4 * scale, i == fb.selected ? ink_selected : ink,
				v->area.w - 40 * scale);
		}
		char label[RM_PATH_MAX];
		assetLabel(&fb.assets[fb.selected], label, sizeof(label));
		put(shown, sizeof(shown), "%d of %d: %s", fb.selected, fb.count, label);
	}
	MosaicHint left[] = { { MOSAIC_BUTTON_B, "BACK" } };
	MosaicHint right[] = { { MOSAIC_BUTTON_X, "REFRESH" }, { MOSAIC_BUTTON_A, "RESTORE" } };
	hints(screen, left, 1, right, fb.count > 0 ? 2 : 1);
	if (strcmp(shown, fb.shown) != 0) {
		put(fb.shown, sizeof(fb.shown), "%s", shown);
		sayf("flashback %s", shown);
	}
	return 0;
}

// The entry at path, a game's file or folder, as a game from RomM, cached
// per path for the hints
static char asked_path[RM_PATH_MAX];
static int asked_answer = -1;
static RMRecord asked_record;

static int gameAt(const char* path, RMRecord* out) {
	if (!path) return 0;
	if (asked_answer >= 0 && !strcmp(path, asked_path)) {
		if (asked_answer) *out = asked_record;
		return asked_answer;
	}
	put(asked_path, sizeof(asked_path), "%s", path);
	asked_answer = 0;
	// following links, as the game list lists them; a game not on the card
	// yet, as the RomM folder lists it, is a rom
	struct stat st;
	int found = stat(path, &st) == 0;
	if (found && !S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) return 0;
	int type = found && S_ISDIR(st.st_mode) ? MOSAIC_ENTRY_DIR : MOSAIC_ENTRY_ROM;
	char key[RM_PATH_MAX];
	if (type == MOSAIC_ENTRY_DIR) {
		// a game's folder in Roms, which NextUI opens at its .m3u or .cue
		const char* name = strrchr(path, '/');
		if (!name) return 0;
		put(key, sizeof(key), "%s/%s.m3u", path, name + 1);
		if (!isFile(key)) put(key, sizeof(key), "%s/%s.cue", path, name + 1);
		if (!isFile(key)) return 0;
	} else {
		gameKey(path, key, sizeof(key));
	}
	RMRecord r;
	Found f;
	if (recordFind(key, &r)) {
		asked_answer = 1;
	} else if (findRom(key, &f)) {
		memset(&r, 0, sizeof(r));
		r.rom_id = f.rom->id;
		put(r.tag, sizeof(r.tag), "%s", f.platform->tag);
		put(r.path, sizeof(r.path), "%s", key);
		asked_answer = 1;
	}
	if (asked_answer) {
		asked_record = r;
		*out = r;
	}
	return asked_answer;
}

// path is the game's, from the button's selected entry or a plugin's
// run_action
static int canFlashback(const char* path, void* data) {
	(void)data;
	RMRecord r;
	return configured && gameAt(path, &r);
}

static void openFlashback(const char* path, void* data) {
	(void)data;
	RMRecord r;
	if (!configured || !gameAt(path, &r)) return;
	fb.rom_id = r.rom_id;
	put(fb.tag, sizeof(fb.tag), "%s", r.tag);
	put(fb.path, sizeof(fb.path), "%s", r.path);
	// the name the list shows when it's the selected entry
	const MosaicEntry* entry = host->selected_entry();
	const char* shown = entry && host->entry_path(entry) && !strcmp(host->entry_path(entry), path) ? host->entry_name(entry) : NULL;
	put(fb.name, sizeof(fb.name), "%s", shown ? shown : baseName(r.path));
	sayf("flashback for rom %d, %s", fb.rom_id, baseName(fb.path));
	statesLoad(fb.tag);
	if (host->open_screen(FLASHBACK_SCREEN) != 0) say("couldn't open the flashback screen");
}

///////////////////////////////////////
// job results, on the main thread

static void backupThenPlace(Job* j) {
	char backup_dir[RM_PATH_MAX];
	put(backup_dir, sizeof(backup_dir), "%s/backups/%s", data_dir, j->tag);
	char backup[RM_PATH_MAX] = "";
	if (isFile(j->path)) {
		makeDirs(backup_dir);
		time_t now = time(NULL);
		struct tm tm;
		char stamp[32] = "backup";
		if (localtime_r(&now, &tm)) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
		put(backup, sizeof(backup), "%s/%s.%s", backup_dir, baseName(j->path), stamp);
		// copy rather than move, so a failed rename below leaves the card as it was
		size_t len = 0;
		char* old = readFile(j->path, SAVE_MAX, &len);
		if (!old || writeFile(backup, old, len) != 0) {
			free(old);
			unlink(j->temp);
			sayf("can't back up %s, so it wasn't restored", j->path);
			toast(tr("RomM: couldn't back up the current file"));
			return;
		}
		free(old);
	}
	if (rename(j->temp, j->path) != 0) {
		unlink(j->temp);
		toast(tr("RomM: couldn't restore it"));
		sayf("can't move the restore into %s", j->path);
		return;
	}
	sayf("restored %s %d to %s, backup %s", j->asset_kind == RM_ASSET_SAVE ? "save" : "state", j->asset_id, j->path,
		backup[0] ? backup : "none (nothing there)");
	toast(tr("RomM: restored"));
	long long size, mtime;
	if (fileStat(j->path, &size, &mtime) == 0) syncedNote(j->path, 1, size, mtime);
	if (j->asset_kind == RM_ASSET_SAVE) {
		RMRecord r;
		if (recordFind(j->name, &r)) {
			char md5[33];
			if (RN_md5File(j->path, SAVE_MAX, md5) == 0) put(r.value, sizeof(r.value), "%s", md5);
			recordPut(&r);
		}
	}
	if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
}

static void finished(Job* j) {
	switch (j->kind) {
	case J_PLATFORMS:
		if (!j->ok) break;
		memcpy(platforms, j->platforms, (size_t)j->count * sizeof(RMPlatform));
		platform_count = j->count;
		platformsSave();
		{
			int mapped = 0;
			for (int i = 0; i < platform_count; i++) mapped += platforms[i].tag[0] != '\0';
			sayf("platforms: %d, %d with a system", platform_count, mapped);
		}
		if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
		break;
	case J_ROMS: {
		if (!j->ok) break;
		RomList* list = romList(j->platform_id);
		if (!list) break;
		free(list->roms);
		list->roms = j->roms;
		list->count = j->count;
		j->roms = NULL;
		romListSave(list);
		const RMPlatform* p = platformById(j->platform_id);
		sayf("roms for %s: %d", p ? p->slug : "?", list->count);
		if (MOSAIC_HOST_HAS(host, reload_lists)) host->reload_lists();
		break;
	}
	case J_DOWNLOAD:
		if (j->ok) {
			if (j->multi) sayf("downloaded and unpacked %s: %lld bytes", j->path, (long long)j->done);
			else sayf("downloaded %s: %lld bytes", j->path, (long long)j->done);
			RMRecord r = { 0 };
			r.rom_id = j->rom_id;
			put(r.tag, sizeof(r.tag), "%s", j->tag);
			// a save already on the card (eg. from before) isn't the server's
			put(r.value, sizeof(r.value), "-");
			gameKey(j->path, r.path, sizeof(r.path));
			recordPut(&r);
			statusWrite("done", j->expect > 0 ? j->expect / 1024 : 0, j->expect > 0 ? j->expect / 1024 : 0, j->name);
			downloadNote(j->path, j->name);
			put(launch_after, sizeof(launch_after), "%s", j->path);
		} else if (j->cancel) {
			statusClear();
		} else {
			statusWrite("error", 0, 0, j->name);
		}
		if (MOSAIC_HOST_HAS(host, open_screen) && !strcmp(host->current_screen(), DOWNLOAD_SCREEN)) {
			host->close_screen(DOWNLOAD_SCREEN);
		}
		break;
	case J_UPLOAD:
		if (j->ok) {
			sayf("uploaded %s for rom %d", j->file, j->rom_id);
			toast(tr("RomM: save uploaded"));
			RMRecord r;
			if (recordFind(j->name, &r)) {
				put(r.value, sizeof(r.value), "%s", j->md5);
				recordPut(&r);
			}
			if (j->file_size >= 0) syncedNote(j->path, 0, j->file_size, j->file_mtime);
			if (!strcmp(pending_path, j->name)) {
				char file[RM_PATH_MAX];
				put(file, sizeof(file), "%s/pending.txt", data_dir);
				unlink(file);
			}
		}
		break;
	case J_ASSETS:
		if (j->rom_id != fb.rom_id) break;
		free(fb.assets);
		fb.assets = NULL;
		fb.selected = fb.first = 0;
		if (j->ok) {
			fb.assets = j->assets;
			fb.count = j->count;
			j->assets = NULL;
			int saves = 0;
			for (int i = 0; i < fb.count; i++) saves += fb.assets[i].kind == RM_ASSET_SAVE;
			sayf("flashback for rom %d: %d saves, %d states", fb.rom_id, saves, fb.count - saves);
		} else {
			fb.count = 0;
			put(fb.error, sizeof(fb.error), "%s", j->error);
		}
		host->request_redraw();
		break;
	case J_FETCH:
		if (j->ok) backupThenPlace(j);
		break;
	}
	if (!j->ok && !j->cancel && j->error[0]) {
		static const char* what[] = { "listing platforms", "listing roms", "downloading", "uploading the save",
			"listing saves", "restoring" };
		sayf("%s failed: %s", what[j->kind], j->error);
		char message[512];
		put(message, sizeof(message), tr("RomM: %s"), j->error);
		toast(message);
	}
}

///////////////////////////////////////
// the folders

// 1 when a kept list was fetched in the last minute, such as by the run
// before a game launched
static int fresh(const char* name) {
	char path[RM_PATH_MAX];
	put(path, sizeof(path), "%s/%s", data_dir, name);
	struct stat st;
	time_t now = time(NULL);
	return stat(path, &st) == 0 && st.st_mtime <= now && now - st.st_mtime < REFRESH_MS / 1000;
}

static void refreshPlatforms(void) {
	if (jobPending(J_PLATFORMS, 0)) return;
	if (platforms_asked && now_ms - platforms_asked < REFRESH_MS) return;
	if (!platforms_asked && platform_count > 0 && fresh("platforms.tsv")) {
		platforms_asked = now_ms ? now_ms : 1;
		return;
	}
	if (!networkReady(platform_count > 0)) return;
	platforms_asked = now_ms ? now_ms : 1;
	jobAdd(jobNew(J_PLATFORMS));
}

static void refreshRoms(RomList* list) {
	if (jobPending(J_ROMS, list->platform_id)) return;
	if (list->asked && now_ms - list->asked < REFRESH_MS) return;
	char name[32];
	put(name, sizeof(name), "roms-%d.tsv", list->platform_id);
	if (!list->asked && list->count > 0 && fresh(name)) {
		list->asked = now_ms ? now_ms : 1;
		return;
	}
	if (!networkReady(list->count > 0)) return;
	list->asked = now_ms ? now_ms : 1;
	Job* j = jobNew(J_ROMS);
	if (!j) return;
	j->platform_id = list->platform_id;
	jobAdd(j);
}

static void rootEntries(MosaicList* root) {
	if (!configured) return;
	host->list_push(root, host->entry_new(root_path, MOSAIC_ENTRY_DIR, "RomM"));
}

static MosaicList* virtualDir(const char* path) {
	if (!configured || !path) return NULL;
	size_t root_len = strlen(root_path);
	if (strncmp(path, root_path, root_len) != 0 || (path[root_len] != '\0' && path[root_len] != '/')) return NULL;
	if (platform_count < 0) platformsLoad();
	MosaicList* list = host->list_new();
	if (path[root_len] == '\0') {
		refreshPlatforms();
		int shown = 0;
		for (int i = 0; i < platform_count; i++) {
			if (!platforms[i].tag[0] || platforms[i].rom_count <= 0) continue;
			char full[RM_PATH_MAX];
			platformPath(&platforms[i], full, sizeof(full));
			host->list_push(list, host->entry_new(full, MOSAIC_ENTRY_DIR, platforms[i].name));
			shown++;
		}
		sayf("root: %d platforms", shown);
		return list;
	}
	for (int i = 0; i < platform_count; i++) {
		char full[RM_PATH_MAX];
		platformPath(&platforms[i], full, sizeof(full));
		if (strcmp(full, path) != 0) continue;
		RomList* roms = romList(platforms[i].id);
		if (!roms) break;
		romListLoad(roms);
		refreshRoms(roms);
		int stubs = 0, local = 0;
		for (int k = 0; k < roms->count; k++) {
			char stub[RM_PATH_MAX];
			stubPath(&platforms[i], roms, &roms->roms[k], stub, sizeof(stub));
			if (!stub[0]) continue;
			if (isFile(stub)) local++;
			else stubs++;
			host->list_push(list, host->entry_new(stub, MOSAIC_ENTRY_ROM, NULL));
		}
		sayf("%s: %d roms, %d on the card, into %s", platforms[i].slug, stubs + local, local,
			roms->folder[0] ? roms->folder : "(none yet)");
		return list;
	}
	return list;
}

///////////////////////////////////////
// launching

static int preLaunch(const MosaicLaunch* launch) {
	if (!configured || launch->type != MOSAIC_ENTRY_ROM || !launch->path) return MOSAIC_LAUNCH_CONTINUE;
	if (platform_count < 0) platformsLoad();
	// a disc of a game with an .m3u (NextUI opens a folder at its first) is the game
	char key[RM_PATH_MAX];
	gameKey(launch->path, key, sizeof(key));
	RMRecord record;
	int known = recordFind(key, &record);
	Found f;
	int listed = findRom(launch->path, &f);
	if (isFile(launch->path)) {
		if (!known && listed) {
			memset(&record, 0, sizeof(record));
			record.rom_id = f.rom->id;
			put(record.tag, sizeof(record.tag), "%s", f.platform->tag);
			put(record.value, sizeof(record.value), "-");
			put(record.path, sizeof(record.path), "%s", key);
			recordPut(&record);
			known = 1;
		}
		if (known) {
			char file[RM_PATH_MAX];
			put(file, sizeof(file), "%s/pending.txt", data_dir);
			writeFile(file, key, strlen(key));
			sayf("launching %s (rom %d)", key, record.rom_id);
		}
		if (activeDownload()) cancelDownload();
		return MOSAIC_LAUNCH_CONTINUE;
	}
	if (!listed) return MOSAIC_LAUNCH_CONTINUE;
	if (networkReady(0)) startDownload(&f, launch->path);
	return MOSAIC_LAUNCH_CANCEL;
}

static void postReturn(void) {
	asked_answer = -1;
	if (configured) syncSave(0);
}

static unsigned long started_at = 0;
static int synced_at_start = 0;

static void tick(unsigned long now) {
	now_ms = now;
	if (!configured) return;
	if (!started_at) started_at = now ? now : 1;
	// a save left from a run that couldn't upload it, once the first frames are up
	if (!synced_at_start && now - started_at > 1500) {
		synced_at_start = 1;
		syncSave(1);
	}
	if (running && running->finished) {
		if (running->thread) pthread_join(running->thread, NULL);
		Job* j = running;
		running = NULL;
		if (MOSAIC_HOST_HAS(host, inhibit_sleep)) host->inhibit_sleep(ID, 0);
		finished(j);
		jobFree(j);
	}
	jobStartNext();
	Job* d = activeDownload();
	if (d && d == running && now - status_written >= 500 && d->done != status_done) {
		status_written = now;
		status_done = d->done;
		long long total = d->total > 0 ? d->total : d->expect;
		statusWrite("running", d->done / 1024, total > 0 ? total / 1024 : 0, d->name);
	}
	if (open_download_screen && MOSAIC_HOST_HAS(host, open_screen)) {
		open_download_screen = 0;
		if (d && host->open_screen(DOWNLOAD_SCREEN) != 0) say("couldn't open the download screen");
	}
	if (launch_after[0] && MOSAIC_HOST_HAS(host, open_path)) {
		char path[RM_PATH_MAX];
		put(path, sizeof(path), "%s", launch_after);
		launch_after[0] = '\0';
		if (!host->open_path(path, MOSAIC_ENTRY_ROM)) sayf("couldn't launch %s", path);
	}
}

///////////////////////////////////////
// services

// romm.synced: with no path, server=<1 when a server is set>; with
// path=<a file>, also whether it's the file the plugin last uploaded or
// restored there, unchanged since, and game=<1 when path is a game from
// RomM, which romm.flashback opens for>. Nothing about the server but
// whether it's set, and never the network.
static int provideSynced(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char path[RM_PATH_MAX];
	int len = host->event_value(args, "path", path, sizeof(path));
	if (len >= (int)sizeof(path)) return -1;
	if (len <= 0) return RM_syncedReply(configured, 0, NULL, -1, 0, 0, out, size) < 0 ? -1 : 0;
	long long file_size = -1, mtime = 0;
	if (fileStat(path, &file_size, &mtime) != 0) file_size = -1;
	RMRecord r;
	int game = configured && gameAt(path, &r);
	const RMSynced* entry = configured ? RM_syncedFind(synced, synced_count, path) : NULL;
	return RM_syncedReply(configured, 1, entry, file_size, mtime, game, out, size) < 0 ? -1 : 0;
}

static int stillThere(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

// romm.downloads: optionally count=<n> (5 by default), the games last
// downloaded that are still on the card, newest first
static int provideDownloads(const char* name, const char* args, char* out, size_t size) {
	(void)name;
	char text[16];
	int want = 5;
	if (host->event_value(args, "count", text, sizeof(text)) > 0) want = atoi(text);
	if (want < 0) want = 0;
	if (want > RM_DOWNLOADS_MAX) want = RM_DOWNLOADS_MAX;
	return RM_downloadsReply(downloads, configured ? download_count : 0, want, stillThere, out, size) < 0 ? -1 : 0;
}

///////////////////////////////////////
// starting and stopping

// partial downloads and unpacks a run stopped in the middle of (a launch or power off)
static void removeLeftoversIn(const char* top) {
	char roms[RM_PATH_MAX];
	put(roms, sizeof(roms), "%s/%s", sd, top);
	DIR* d = opendir(roms);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char dir[RM_PATH_MAX];
		put(dir, sizeof(dir), "%s/%s", roms, e->d_name);
		DIR* inner = isDir(dir) ? opendir(dir) : NULL;
		if (!inner) continue;
		struct dirent* f;
		while ((f = readdir(inner))) {
			if (strncmp(f->d_name, ".romm-", 6) != 0) continue;
			size_t len = strlen(f->d_name);
			char full[RM_PATH_MAX];
			put(full, sizeof(full), "%s/%s", dir, f->d_name);
			if (len > 5 && !strcmp(f->d_name + len - 5, ".part") && isFile(full)) unlink(full);
			// a zip's folder an unpack stopped in the middle of
			if (len > 7 && !strcmp(f->d_name + len - 7, ".unpack") && isDir(full) && RZ_removeTree(full) != 0) {
				sayf("couldn't remove all of %s", full);
			}
		}
		closedir(inner);
	}
	closedir(d);
}

static void removeLeftovers(void) {
	removeLeftoversIn("Roms");
	removeLeftoversIn("Saves");
	removeLeftoversIn(".userdata/shared");
}

static int init(const MosaicHost* h) {
	host = h;
	static const RMWords words = { tr };
	RM_setWords(&words);
	put(sd, sizeof(sd), "%s", host->sdcard_path);
	put(data_dir, sizeof(data_dir), "%s/launcher/romm", host->userdata_path);
	put(root_path, sizeof(root_path), "%s/RomM", host->sdcard_path);
	put(status_path, sizeof(status_path), "%s/launcher/status.d/romm.status", host->userdata_path);
	memset(&fb, 0, sizeof(fb));
	loadSettings();
	syncedLoad();
	downloadsLoad();
	if (MOSAIC_HOST_HAS(host, call)) {
		if (host->provide("romm.synced", provideSynced) != 0) say("couldn't provide romm.synced");
		if (host->provide("romm.downloads", provideDownloads) != 0) say("couldn't provide romm.downloads");
	}
	if (!configured) return 0;
	makeDirs(data_dir);
	removeLeftovers();
	statusClear();
	platformsLoad();
	if (MOSAIC_HOST_HAS(host, open_screen) && MOSAIC_HOST_HAS(host, draw_text)) {
		static const MosaicScreenHooks download_hooks = {
			.size = sizeof(MosaicScreenHooks),
			.enter = downloadEnter,
			.input = downloadInput,
			.render = downloadRender,
			.animating = downloadAnimating,
		};
		static const MosaicScreenHooks flashback_hooks = {
			.size = sizeof(MosaicScreenHooks),
			.enter = flashbackEnter,
			.exit = flashbackExit,
			.input = flashbackInput,
			.render = flashbackRender,
		};
		if (host->register_screen(DOWNLOAD_SCREEN, &download_hooks) != 0
				|| host->register_screen(FLASHBACK_SCREEN, &flashback_hooks) != 0) {
			say("couldn't register its screens");
		} else if (host->register_action(FLASHBACK_ACTION, "FLASHBACK", MOSAIC_ACTION_PATH, canFlashback, openFlashback, NULL) != 0) {
			say("couldn't register the flashback action");
		} else {
			static const struct {
				const char* name;
				int button;
			} buttons[] = {
				{ "Y", MOSAIC_BUTTON_Y },
				{ "L2", MOSAIC_BUTTON_L2 },
				{ "R2", MOSAIC_BUTTON_R2 },
				{ "SELECT", MOSAIC_BUTTON_SELECT },
			};
			const char* name = host->cfg_get("button", "Y");
			for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
				if (strcmp(name, buttons[i].name) != 0) continue;
				if (host->bind_button(MOSAIC_SCREEN_GAMELIST, buttons[i].button, MOSAIC_TRIGGER_PRESS, FLASHBACK_ACTION) != 0) {
					say("couldn't bind the flashback button");
				}
			}
		}
	} else {
		say("this launcher has no plugin screens, so there's no download screen or Flashback");
	}
	sayf("%d platforms kept from before", platform_count);
	return 0;
}

static void quit(void) {
	jobsCancel();
	for (int i = 0; i < rom_list_count; i++) free(rom_lists[i].roms);
	rom_list_count = 0;
	free(fb.assets);
	fb.assets = NULL;
	memset(&net, 0, sizeof(net));
}

static const MosaicPlugin plugin = {
	.size = sizeof(MosaicPlugin),
	.api_version = MOSAIC_API_VERSION,
	.id = ID,
	.init = init,
	.quit = quit,
	.root_entries = rootEntries,
	.virtual_dir = virtualDir,
	.pre_launch = preLaunch,
	.post_return = postReturn,
	.tick = tick,
};

const MosaicPlugin* mosaic_plugin_get(const MosaicHost* h) {
	(void)h;
	return &plugin;
}
