// The sync plugin's work off the launcher's thread: see sync_run.h.

#include "sync_run.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#define OUTPUT_MAX (8 * 1024 * 1024) // a listing's output
#define HTTP_MAX (1024 * 1024)
#define SYNCTHING_PID "/tmp/mosaic-sync-syncthing.pid"

extern char** environ;

static int put(char* out, size_t size, const char* format, ...) __attribute__((format(printf, 3, 4)));
static int put(char* out, size_t size, const char* format, ...) {
	va_list args;
	va_start(args, format);
	int n = vsnprintf(out, size, format, args);
	va_end(args);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

void Sr_log(const SrConfig* cfg, const char* format, ...) {
	FILE* f = fopen(cfg->log_path, "a");
	if (!f) return;
	time_t now = time(NULL);
	struct tm tm;
	char when[32];
	strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime_r(&now, &tm));
	fprintf(f, "%s ", when);
	va_list args;
	va_start(args, format);
	vfprintf(f, format, args);
	va_end(args);
	fputc('\n', f);
	fclose(f);
}

static int isFile(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int isProgram(const char* path) {
	return isFile(path) && access(path, X_OK) == 0;
}

static void makeDirs(const char* path) {
	char dir[SY_PATH_MAX];
	if (put(dir, sizeof(dir), "%s", path) != 0) return;
	for (char* p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	mkdir(dir, 0755);
}

static void parentDirs(const char* path) {
	char dir[SY_PATH_MAX];
	if (put(dir, sizeof(dir), "%s", path) != 0) return;
	char* slash = strrchr(dir, '/');
	if (!slash || slash == dir) return;
	*slash = '\0';
	makeDirs(dir);
}

void Sr_findTools(const char* dir, SrConfig* cfg) {
	struct {
		char* out;
		const char* name;
	} tools[] = {
		{ cfg->rclone, "rclone" },
		{ cfg->syncthing, "syncthing" },
	};
	for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
		char path[SY_PATH_MAX];
		tools[i].out[0] = '\0';
		if (put(path, sizeof(path), "%s/%s", dir, tools[i].name) == 0 && isProgram(path)) {
			snprintf(tools[i].out, SY_PATH_MAX, "%s", path);
		}
	}
}

void Sr_findCa(const char* sdcard, const char* platform, char* out, size_t size) {
	char own[SY_PATH_MAX];
	put(own, sizeof(own), "%s/.system/%s/etc/ssl/certs/ca-certificates.crt", sdcard, platform ? platform : "");
	const char* places[] = {
		getenv("SSL_CERT_FILE"),
		own,
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/ssl/cert.pem",
		"/etc/pki/tls/certs/ca-bundle.crt",
	};
	out[0] = '\0';
	for (size_t i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
		struct stat st;
		if (!places[i] || !places[i][0] || stat(places[i], &st) != 0 || !S_ISREG(st.st_mode) || !st.st_size) continue;
		snprintf(out, size, "%s", places[i]);
		return;
	}
}

///////////////////////////////////////
// running a program

// The programs' environment: only what they need, so nothing preloaded
// into the launcher is loaded into them. Strings in env_text.
typedef struct Env {
	char text[8][SY_PATH_MAX + 32];
	char* list[9];
} Env;

static void envFor(const SrConfig* cfg, Env* e) {
	int n = 0;
	put(e->text[n++], sizeof(e->text[0]), "HOME=%s", cfg->data_dir);
	put(e->text[n++], sizeof(e->text[0]), "PATH=/usr/bin:/bin");
	put(e->text[n++], sizeof(e->text[0]), "TZ=UTC");
	put(e->text[n++], sizeof(e->text[0]), "LC_ALL=C");
	put(e->text[n++], sizeof(e->text[0]), "STNOUPGRADE=1");
	if (cfg->ca_file[0]) put(e->text[n++], sizeof(e->text[0]), "SSL_CERT_FILE=%s", cfg->ca_file);
	if (cfg->rclone_pass[0]) put(e->text[n++], sizeof(e->text[0]), "RCLONE_CONFIG_PASS=%s", cfg->rclone_pass);
	for (int i = 0; i < n; i++) e->list[i] = e->text[i];
	e->list[n] = NULL;
}

static const char* baseName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void logCommand(const SrConfig* cfg, char* const argv[]) {
	char line[2048];
	size_t used = 0;
	for (int i = 0; argv[i] && used < sizeof(line) - 1; i++) {
		int n = snprintf(line + used, sizeof(line) - used, "%s%s", i ? " " : "", i ? argv[i] : baseName(argv[0]));
		if (n < 0) break;
		used += (size_t)n;
	}
	if (used >= sizeof(line)) used = sizeof(line) - 1;
	line[used] = '\0';
	Sr_log(cfg, "run: %s", line);
}

// Runs argv[0] with argv, its stderr (and stdout, unless out is given)
// into the log. With out, its stdout is kept there, to free. Returns its
// exit status, or -1 when it couldn't run or was stopped.
static int run(SrJob* job, char* const argv[], char** out, size_t* out_len) {
	const SrConfig* cfg = &job->cfg;
	logCommand(cfg, argv);
	int fds[2] = { -1, -1 };
	if (out) {
		*out = NULL;
		if (out_len) *out_len = 0;
		if (pipe(fds) != 0) return -1;
		fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	}
	int log = open(cfg->log_path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
	if (log < 0) log = open("/dev/null", O_WRONLY | O_CLOEXEC);
	Env env;
	envFor(cfg, &env);

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2(&actions, out ? fds[1] : log, 1);
	posix_spawn_file_actions_adddup2(&actions, log, 2);
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
	flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
	posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#else
	long max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
	for (int fd = 3; fd < max_fd; fd++) {
		if (fcntl(fd, F_GETFD) >= 0) posix_spawn_file_actions_addclose(&actions, fd);
	}
#endif
	posix_spawnattr_setflags(&attr, flags);
	sigset_t none, defaults;
	sigemptyset(&none);
	posix_spawnattr_setsigmask(&attr, &none);
	sigemptyset(&defaults);
	sigaddset(&defaults, SIGPIPE);
	sigaddset(&defaults, SIGTERM);
	sigaddset(&defaults, SIGINT);
	sigaddset(&defaults, SIGHUP);
	posix_spawnattr_setsigdefault(&attr, &defaults);
	pid_t pid;
	int rc = posix_spawn(&pid, argv[0], &actions, &attr, argv, env.list);
	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&actions);
	close(log);
	if (out) close(fds[1]);
	if (rc != 0) {
		Sr_log(cfg, "couldn't run %s: %s", argv[0], strerror(rc));
		if (out) close(fds[0]);
		return -1;
	}
	job->child = pid;

	char* buffer = NULL;
	size_t used = 0, cap = 0;
	int stopped = 0;
	if (out) {
		for (;;) {
			if (job->cancel && !stopped) {
				kill(pid, SIGTERM);
				stopped = 1;
			}
			struct pollfd p = { fds[0], POLLIN, 0 };
			int ready = poll(&p, 1, 100);
			if (ready < 0 && errno == EINTR) continue;
			if (ready <= 0) continue;
			if (used + 4096 + 1 > cap) {
				if (cap >= OUTPUT_MAX) break;
				size_t next = cap ? cap * 2 : 65536;
				char* grown = realloc(buffer, next);
				if (!grown) break;
				buffer = grown;
				cap = next;
			}
			ssize_t n = read(fds[0], buffer + used, cap - used - 1);
			if (n < 0 && errno == EINTR) continue;
			if (n <= 0) break;
			used += (size_t)n;
		}
		close(fds[0]);
	}
	int status = 0;
	for (;;) {
		pid_t w = waitpid(pid, &status, WNOHANG);
		if (w == pid) break;
		if (w < 0 && errno != EINTR) {
			status = -1;
			break;
		}
		if (job->cancel && !stopped) {
			kill(pid, SIGTERM);
			stopped = 1;
		}
		struct timespec pause = { 0, 20 * 1000000L };
		nanosleep(&pause, NULL);
	}
	job->child = 0;
	if (out) {
		if (buffer) buffer[used] = '\0';
		*out = buffer;
		if (out_len) *out_len = used;
	}
	if (stopped || job->cancel) return -1;
	if (status < 0 || !WIFEXITED(status)) return -1;
	int code = WEXITSTATUS(status);
	if (code) Sr_log(cfg, "%s exited with %d", baseName(argv[0]), code);
	return code;
}

///////////////////////////////////////
// the card

static int addFile(SyFile* files, int* count, const char* rel, const char* path) {
	struct stat st;
	if (*count >= SY_FILES_MAX || lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
	SyFile* f = &files[*count];
	if (put(f->rel, sizeof(f->rel), "%s", rel) != 0 || !Sy_relValid(f->rel)) return 0;
	f->size = (long long)st.st_size;
	f->mtime = (long long)st.st_mtime;
	(*count)++;
	return 1;
}

// files directly in each folder in top that dir_ok keeps, as <prefix>/<folder>/<file>
static void scanTop(const char* top, const char* prefix, int (*dir_ok)(const char*), SyFile* files, int* count) {
	DIR* d = opendir(top);
	struct dirent* e;
	while (d && (e = readdir(d))) {
		if (!dir_ok(e->d_name)) continue;
		char dir[SY_PATH_MAX];
		if (put(dir, sizeof(dir), "%s/%s", top, e->d_name) != 0) continue;
		DIR* inner = opendir(dir);
		struct dirent* f;
		while (inner && (f = readdir(inner))) {
			if (!Sy_syncable(f->d_name)) continue;
			char path[SY_PATH_MAX], rel[SY_PATH_MAX];
			if (put(path, sizeof(path), "%s/%s", dir, f->d_name) != 0) continue;
			if (put(rel, sizeof(rel), "%s/%s/%s", prefix, e->d_name, f->d_name) != 0) continue;
			addFile(files, count, rel, path);
		}
		if (inner) closedir(inner);
	}
	if (d) closedir(d);
}

int Sr_scanLocal(const SrConfig* cfg, SyFile** out) {
	SyFile* files = calloc(SY_FILES_MAX, sizeof(SyFile));
	*out = files;
	if (!files) return -1;
	int count = 0;
	char top[SY_PATH_MAX];
	if (cfg->saves && put(top, sizeof(top), "%s/Saves", cfg->sdcard) == 0) scanTop(top, "Saves", Sy_syncable, files, &count);
	if (cfg->states && put(top, sizeof(top), "%s/.userdata/shared", cfg->sdcard) == 0) {
		scanTop(top, "States", Sy_stateDir, files, &count);
	}
	Sy_sortFiles(files, count);
	return count;
}

///////////////////////////////////////
// the remote

// Adds the lines of a listing under prefix ("Saves" or "States") to files
static void addListing(char* text, const char* prefix, SyFile* files, int* count) {
	char* save = NULL;
	for (char* line = text ? strtok_r(text, "\n", &save) : NULL; line && *count < SY_FILES_MAX;
			line = strtok_r(NULL, "\n", &save)) {
		char full[SY_PATH_MAX + 64];
		SyFile f;
		if (snprintf(full, sizeof(full), "%s/%s", prefix, line) >= (int)sizeof(full)) continue;
		if (Sy_parseRclone(full, &f) == 0) files[(*count)++] = f;
	}
}

// The remote's saves and states, sorted, into a list to free. Returns the
// count, or -1.
static int listRemote(SrJob* job, SyFile** out) {
	const SrConfig* cfg = &job->cfg;
	SyFile* files = calloc(SY_FILES_MAX, sizeof(SyFile));
	*out = files;
	if (!files) return -1;
	int count = 0;
	const char* tops[] = { "Saves", "States" };
	const int on[] = { cfg->saves, cfg->states };
	for (int t = 0; t < 2; t++) {
		if (!on[t]) continue;
		char* text = NULL;
		char place[SY_PATH_MAX * 2];
		if (Sy_remoteJoin(cfg->remote, tops[t], place, sizeof(place)) != 0) return -1;
		char* argv[] = { (char*)cfg->rclone, "--config", (char*)cfg->rclone_config, "--retries", "1", "lsf", "-R", "--files-only",
			"--max-depth", "2", "--format", "pst", "--separator", "\t", place, NULL };
		int rc = run(job, argv, &text, NULL);
		// 3 is rclone's "directory not found": nothing there yet
		if (rc == 3) rc = 0;
		if (rc == 0) addListing(text, tops[t], files, &count);
		free(text);
		if (rc != 0) {
			put(job->error, sizeof(job->error), "couldn't list the remote's %s", tops[t]);
			return -1;
		}
	}
	Sy_sortFiles(files, count);
	return count;
}

static int rclone(SrJob* job, const char* command, const char* from, const char* to) {
	const SrConfig* cfg = &job->cfg;
	char* argv[] = { (char*)cfg->rclone, "--config", (char*)cfg->rclone_config, "--retries", "1", (char*)command, (char*)from,
		(char*)to, NULL };
	return run(job, argv, NULL, NULL) == 0 ? 0 : -1;
}

// Copies one file across: up from the card or down to it. keep makes the
// file it replaces <name>.conflict first.
static int transfer(SrJob* job, const char* rel, int up, int keep) {
	const SrConfig* cfg = &job->cfg;
	char local[SY_PATH_MAX], kept[SY_PATH_MAX + 16];
	if (Sy_localPath(cfg->sdcard, rel, local, sizeof(local)) != 0) return -1;
	if (!up) {
		parentDirs(local);
		if (keep) {
			if (put(kept, sizeof(kept), "%s" SY_CONFLICT, local) != 0 || rename(local, kept) != 0) return -1;
			Sr_log(cfg, "kept %s as %s", rel, kept);
		}
	}
	char place[SY_PATH_MAX * 2], conflict[SY_PATH_MAX * 2 + 16];
	if (Sy_remoteJoin(cfg->remote, rel, place, sizeof(place)) != 0) return -1;
	if (up && keep) {
		if (put(conflict, sizeof(conflict), "%s" SY_CONFLICT, place) != 0 || rclone(job, "moveto", place, conflict) != 0) return -1;
	}
	return rclone(job, "copyto", up ? local : place, up ? place : local);
}

// Removes a file from one side, as it was removed from the other, by
// moving it to <name>.deleted beside it, so it can be put back.
static int removeOne(SrJob* job, const char* rel, int here) {
	const SrConfig* cfg = &job->cfg;
	if (here) {
		char local[SY_PATH_MAX], kept[SY_PATH_MAX + 16];
		if (Sy_localPath(cfg->sdcard, rel, local, sizeof(local)) != 0 || put(kept, sizeof(kept), "%s" SY_DELETED, local) != 0) return -1;
		return rename(local, kept);
	}
	char place[SY_PATH_MAX * 2], kept[SY_PATH_MAX * 2 + 16];
	if (Sy_remoteJoin(cfg->remote, rel, place, sizeof(place)) != 0 || put(kept, sizeof(kept), "%s" SY_DELETED, place) != 0) return -1;
	return rclone(job, "moveto", place, kept);
}

///////////////////////////////////////
// a sync

static void basePath(const SrConfig* cfg, char* out, size_t size) {
	// one record for each remote, so a different remote starts afresh
	unsigned long h = 2166136261u;
	const char* parts[] = { "rclone", cfg->remote, "", "" };
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		for (const char* p = parts[i]; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
		h = (h ^ '\n') * 16777619u;
	}
	put(out, size, "%s/base-%08lx.txt", cfg->data_dir, h & 0xffffffffUL);
}

static int baseLoad(const SrConfig* cfg, SyBase** out) {
	SyBase* base = calloc(SY_FILES_MAX, sizeof(SyBase));
	*out = base;
	if (!base) return -1;
	char path[SY_PATH_MAX];
	basePath(cfg, path, sizeof(path));
	FILE* f = fopen(path, "r");
	int count = 0;
	char line[SY_PATH_MAX + 128];
	while (f && count < SY_FILES_MAX && fgets(line, sizeof(line), f)) {
		if (Sy_baseParse(line, &base[count]) == 0) count++;
	}
	if (f) fclose(f);
	Sy_sortBase(base, count);
	return count;
}

static void baseSave(const SrConfig* cfg, const SyBase* base, int count) {
	char path[SY_PATH_MAX], temp[SY_PATH_MAX + 8];
	basePath(cfg, path, sizeof(path));
	if (put(temp, sizeof(temp), "%s.tmp", path) != 0) return;
	FILE* f = fopen(temp, "w");
	if (!f) return;
	char line[SY_PATH_MAX + 128];
	for (int i = 0; i < count; i++) {
		if (Sy_baseFormat(&base[i], line, sizeof(line)) > 0) fputs(line, f);
	}
	if (fclose(f) == 0) rename(temp, path);
}

typedef struct Scope {
	const SrJob* job;
	char (*failed)[SY_PATH_MAX];
	int failed_count;
} Scope;

static int skipCompare(const void* key, const void* item) {
	return strcmp((const char*)key, (const char*)item);
}

static int wanted(const char* rel, void* data) {
	const Scope* s = data;
	const SrJob* j = s->job;
	int kind = Sy_kind(rel);
	if (kind < 0 || (kind == SY_SAVE && !j->cfg.saves) || (kind == SY_STATE && !j->cfg.states)) return 0;
	if (!j->whole && !Sy_ofGame(rel, j->tag, j->base)) return 0;
	return !(j->skip_count && bsearch(rel, j->skip, (size_t)j->skip_count, sizeof(j->skip[0]), skipCompare));
}

static int failedStep(const char* rel, void* data) {
	const Scope* s = data;
	return s->failed_count && bsearch(rel, s->failed, (size_t)s->failed_count, sizeof(s->failed[0]), skipCompare) != NULL;
}

static void runSync(SrJob* job) {
	SrConfig* cfg = &job->cfg;
	SyFile *local = NULL, *remote = NULL, *local_after = NULL, *remote_after = NULL;
	SyBase *base = NULL, *next = NULL;
	SyStep* steps = NULL;
	Scope scope = { job, NULL, 0 };
	int local_count = Sr_scanLocal(cfg, &local);
	if (local_count < 0) goto done;
	int remote_count = listRemote(job, &remote);
	if (remote_count < 0) goto done;
	// a list cut short would make the files past its end look removed
	if (local_count >= SY_FILES_MAX || remote_count >= SY_FILES_MAX) {
		put(job->error, sizeof(job->error), "more than %d files to sync", SY_FILES_MAX - 1);
		goto done;
	}
	int base_count = baseLoad(cfg, &base);
	steps = calloc(SY_FILES_MAX, sizeof(SyStep));
	scope.failed = calloc(SY_FILES_MAX, sizeof(scope.failed[0]));
	next = calloc(SY_FILES_MAX, sizeof(SyBase));
	if (base_count < 0 || !steps || !scope.failed || !next) goto done;
	// one side with nothing at all, such as a remote folder renamed or
	// gone, isn't taken as every file removed: it's filled again instead
	int plan_base = base_count;
	if (base_count && (!local_count || !remote_count)) {
		Sr_log(cfg, "nothing %s, so nothing is removed this time", local_count ? "on the remote" : "here");
		plan_base = 0;
	}
	int count = Sy_plan(local, local_count, remote, remote_count, base, plan_base, wanted, &scope, steps, SY_FILES_MAX);
	Sr_log(cfg, "%d files here, %d on the remote, %d in the record, %d to do (%s)", local_count, remote_count, base_count, count,
		job->whole ? "everything" : job->base);
	int moved = 0;
	for (int i = 0; i < count && !job->cancel; i++) {
		const SyStep* s = &steps[i];
		if (s->action == SY_SAME) continue;
		moved++;
		if (s->action == SY_DELETE_LOCAL || s->action == SY_DELETE_REMOTE) {
			int here = s->action == SY_DELETE_LOCAL;
			if (removeOne(job, s->rel, here) != 0) {
				Sr_log(cfg, "couldn't remove %s %s", s->rel, here ? "here" : "from the remote");
				snprintf(scope.failed[scope.failed_count++], SY_PATH_MAX, "%s", s->rel);
				job->failed++;
				continue;
			}
			Sr_log(cfg, "removed %s %s, as it was removed %s; kept as .deleted", s->rel, here ? "here" : "from the remote",
				here ? "from the remote" : "here");
			job->deleted++;
			continue;
		}
		int up = s->action == SY_UPLOAD || s->action == SY_UPLOAD_KEEP;
		int keep = s->action == SY_UPLOAD_KEEP || s->action == SY_DOWNLOAD_KEEP;
		if (transfer(job, s->rel, up, keep) != 0) {
			Sr_log(cfg, "couldn't %s %s", up ? "upload" : "download", s->rel);
			snprintf(scope.failed[scope.failed_count++], SY_PATH_MAX, "%s", s->rel);
			job->failed++;
			continue;
		}
		Sr_log(cfg, "%s %s%s", up ? "uploaded" : "downloaded", s->rel, keep ? ", the other kept as .conflict" : "");
		if (up) job->up++;
		else job->down++;
		if (keep) job->conflicts++;
	}
	if (job->cancel) goto done;
	// how both sides are now
	int local_after_count = local_count, remote_after_count = remote_count;
	const SyFile *l = local, *r = remote;
	if (moved) {
		local_after_count = Sr_scanLocal(cfg, &local_after);
		remote_after_count = listRemote(job, &remote_after);
		if (local_after_count < 0 || remote_after_count < 0) goto done;
		l = local_after;
		r = remote_after;
	}
	int next_count = Sy_baseNext(base, base_count, l, local_after_count, r, remote_after_count, wanted, failedStep, &scope, next,
		SY_FILES_MAX);
	baseSave(cfg, next, next_count);
	job->ok = job->failed == 0;
	if (job->failed) put(job->error, sizeof(job->error), "%d files didn't sync", job->failed);
done:
	if (!job->ok && !job->error[0]) put(job->error, sizeof(job->error), job->cancel ? "stopped" : "it didn't finish");
	free(local);
	free(remote);
	free(local_after);
	free(remote_after);
	free(base);
	free(next);
	free(steps);
	free(scope.failed);
}

///////////////////////////////////////
// Syncthing's REST API

// method target with body to Syncthing on 127.0.0.1. Returns the HTTP
// status, with the body in out (cut to fit), or -1.
static int http(const SrConfig* cfg, const char* method, const char* target, const char* body, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	struct timeval tv = { 10, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)cfg->gui_port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	size_t body_len = body ? strlen(body) : 0;
	char head[1024];
	if (put(head, sizeof(head),
			"%s %s HTTP/1.0\r\nHost: 127.0.0.1\r\nX-API-Key: %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
			method, target, cfg->api_key, body_len) != 0) {
		close(fd);
		return -1;
	}
	int flags = 0;
#ifdef MSG_NOSIGNAL
	flags = MSG_NOSIGNAL;
#endif
	if (send(fd, head, strlen(head), flags) < 0 || (body_len && send(fd, body, body_len, flags) < 0)) {
		close(fd);
		return -1;
	}
	char* reply = malloc(HTTP_MAX + 1);
	size_t used = 0;
	ssize_t n;
	while (reply && used < HTTP_MAX && (n = recv(fd, reply + used, HTTP_MAX - used, 0)) > 0) used += (size_t)n;
	close(fd);
	if (!reply) return -1;
	reply[used] = '\0';
	int status = -1;
	if (sscanf(reply, "HTTP/%*d.%*d %d", &status) != 1) status = -1;
	const char* start = strstr(reply, "\r\n\r\n");
	if (out && size && start) snprintf(out, size, "%s", start + 4);
	free(reply);
	return status;
}

static int waitForSyncthing(SrJob* job) {
	char reply[256];
	for (int i = 0; i < 120 && !job->cancel; i++) {
		if (http(&job->cfg, "GET", "/rest/system/ping", NULL, reply, sizeof(reply)) == 200) return 0;
		struct timespec pause = { 0, 500 * 1000000L };
		nanosleep(&pause, NULL);
	}
	return -1;
}

// The folder's .stignore: romm's files first, then for the states folder
// only the cores' folders, so the rest of .userdata/shared, such as the
// play history, never syncs
static int writeIgnores(const SrJob* job, const char* dir, const char* top) {
	const SrConfig* cfg = &job->cfg;
	char path[SY_PATH_MAX], temp[SY_PATH_MAX + 8];
	if (put(path, sizeof(path), "%s/.stignore", dir) != 0 || put(temp, sizeof(temp), "%s.tmp", path) != 0) return -1;
	makeDirs(dir);
	FILE* f = fopen(temp, "w");
	if (!f) return -1;
	fprintf(f, "// written by the sync plugin each time the launcher starts\n");
	size_t t = strlen(top);
	for (int i = 0; i < job->skip_count; i++) {
		char escaped[SY_PATH_MAX * 2];
		if (strncmp(job->skip[i], top, t) != 0 || job->skip[i][t] != '/') continue;
		if (Sy_ignoreEscape(job->skip[i] + t, escaped, sizeof(escaped)) == 0) fprintf(f, "%s\n", escaped);
	}
	fprintf(f, "/.*\n");
	if (!strcmp(top, "States")) {
		DIR* d = opendir(dir);
		struct dirent* e;
		while (d && (e = readdir(d))) {
			char sub[SY_PATH_MAX], escaped[SY_PATH_MAX];
			struct stat st;
			if (!Sy_stateDir(e->d_name) || put(sub, sizeof(sub), "%s/%s", dir, e->d_name) != 0 || stat(sub, &st) != 0
					|| !S_ISDIR(st.st_mode)) {
				continue;
			}
			if (Sy_ignoreEscape(e->d_name, escaped, sizeof(escaped)) == 0) fprintf(f, "!/%s\n", escaped);
		}
		if (d) closedir(d);
		fprintf(f, "*\n");
	}
	fprintf(f, "*" SY_CONFLICT "\n*" SY_DELETED "\n");
	if (fclose(f) != 0) return -1;
	(void)cfg;
	return rename(temp, path);
}

static void runSetup(SrJob* job) {
	SrConfig* cfg = &job->cfg;
	if (waitForSyncthing(job) != 0) {
		put(job->error, sizeof(job->error), "Syncthing didn't start");
		return;
	}
	static char reply[HTTP_MAX];
	if (http(cfg, "GET", "/rest/system/status", NULL, reply, sizeof(reply)) != 200
			|| Sy_jsonValue(reply, "myID", job->device_id, sizeof(job->device_id)) <= 0) {
		put(job->error, sizeof(job->error), "Syncthing didn't say its device ID");
		return;
	}
	Sr_log(cfg, "Syncthing is up as %s", job->device_id);
	int peer = Sy_deviceIdValid(cfg->peer);
	char body[SY_PATH_MAX * 3], target[256];
	if (peer) {
		put(target, sizeof(target), "/rest/config/devices/%s", cfg->peer);
		put(body, sizeof(body), "{\"deviceID\":\"%s\",\"name\":\"Mosaic sync\",\"addresses\":[\"dynamic\"]}", cfg->peer);
		if (http(cfg, "PUT", target, body, reply, sizeof(reply)) != 200) Sr_log(cfg, "Syncthing refused the device: %.200s", reply);
	}
	const struct {
		const char* id;
		const char* label;
		const char* top;
		const char* sub;
		int on;
	} folders[] = {
		{ SR_FOLDER_SAVES, "Mosaic saves", "Saves", "Saves", cfg->saves },
		{ SR_FOLDER_STATES, "Mosaic states", "States", ".userdata/shared", cfg->states },
	};
	int ok = 1;
	for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
		put(target, sizeof(target), "/rest/config/folders/%s", folders[i].id);
		if (!folders[i].on || !peer) {
			http(cfg, "DELETE", target, NULL, reply, sizeof(reply));
			continue;
		}
		char dir[SY_PATH_MAX], escaped[SY_PATH_MAX * 2];
		if (put(dir, sizeof(dir), "%s/%s", cfg->sdcard, folders[i].sub) != 0 || Sy_jsonEscape(dir, escaped, sizeof(escaped)) != 0) {
			ok = 0;
			continue;
		}
		// the ignores first, so nothing else in the folder is ever looked at
		if (writeIgnores(job, dir, folders[i].top) != 0) {
			Sr_log(cfg, "couldn't write %s/.stignore", dir);
			ok = 0;
			continue;
		}
		put(body, sizeof(body),
			"{\"id\":\"%s\",\"label\":\"%s\",\"path\":\"%s\",\"type\":\"sendreceive\",\"rescanIntervalS\":3600,"
			"\"fsWatcherEnabled\":false,\"devices\":[{\"deviceID\":\"%s\"},{\"deviceID\":\"%s\"}]}",
			folders[i].id, folders[i].label, escaped, job->device_id, cfg->peer);
		if (http(cfg, "PUT", target, body, reply, sizeof(reply)) != 200) {
			Sr_log(cfg, "Syncthing refused the folder %s: %.200s", folders[i].id, reply);
			ok = 0;
		}
	}
	job->ok = ok;
	if (!ok) put(job->error, sizeof(job->error), "Syncthing refused its folders");
}

static void runScan(SrJob* job) {
	SrConfig* cfg = &job->cfg;
	static char reply[HTTP_MAX];
	if (!Sy_deviceIdValid(cfg->peer)) {
		put(job->error, sizeof(job->error), "no other device set");
		return;
	}
	const char* ids[] = { SR_FOLDER_SAVES, SR_FOLDER_STATES };
	const int on[] = { cfg->saves, cfg->states };
	char target[256], value[64];
	for (int i = 0; i < 2; i++) {
		if (!on[i]) continue;
		put(target, sizeof(target), "/rest/db/scan?folder=%s", ids[i]);
		if (http(cfg, "POST", target, NULL, reply, sizeof(reply)) != 200) {
			put(job->error, sizeof(job->error), "Syncthing didn't look at the files");
			return;
		}
	}
	// until both folders are idle again, for at most a minute
	job->need = 0;
	for (int i = 0; i < 2; i++) {
		if (!on[i]) continue;
		put(target, sizeof(target), "/rest/db/status?folder=%s", ids[i]);
		for (int tries = 0; tries < 120 && !job->cancel; tries++) {
			if (http(cfg, "GET", target, NULL, reply, sizeof(reply)) == 200 && Sy_jsonValue(reply, "state", value, sizeof(value)) > 0
					&& !strcmp(value, "idle")) {
				break;
			}
			struct timespec pause = { 0, 500 * 1000000L };
			nanosleep(&pause, NULL);
		}
		if (Sy_jsonValue(reply, "needFiles", value, sizeof(value)) > 0) job->need += atoi(value);
	}
	if (job->cancel) return;
	job->connected = 0;
	if (http(cfg, "GET", "/rest/system/connections", NULL, reply, sizeof(reply)) == 200) {
		char quoted[96];
		put(quoted, sizeof(quoted), "\"%s\"", cfg->peer);
		const char* at = strstr(reply, quoted);
		job->connected = at && Sy_jsonValue(at, "connected", value, sizeof(value)) > 0 && !strcmp(value, "true");
	}
	job->completion = 100;
	for (int i = 0; i < 2; i++) {
		if (!on[i]) continue;
		put(target, sizeof(target), "/rest/db/completion?folder=%s&device=%s", ids[i], cfg->peer);
		if (http(cfg, "GET", target, NULL, reply, sizeof(reply)) == 200 && Sy_jsonValue(reply, "completion", value, sizeof(value)) > 0) {
			int c = atoi(value);
			if (c < job->completion) job->completion = c;
		}
	}
	Sr_log(cfg, "Syncthing: other device %s, it has %d%%, %d files to fetch here", job->connected ? "connected" : "not connected",
		job->completion, job->need);
	job->ok = 1;
}

void Sr_run(SrJob* job) {
	makeDirs(job->cfg.data_dir);
	if (job->kind == SR_JOB_SYNC) runSync(job);
	else if (job->kind == SR_JOB_ST_SETUP) runSetup(job);
	else if (job->kind == SR_JOB_ST_SCAN) runScan(job);
}

///////////////////////////////////////
// Syncthing itself

int Sr_newKey(char* out, size_t size) {
	static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	unsigned char bytes[32];
	if (size < sizeof(bytes) + 1) return -1;
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	ssize_t n = read(fd, bytes, sizeof(bytes));
	close(fd);
	if (n != (ssize_t)sizeof(bytes)) return -1;
	for (size_t i = 0; i < sizeof(bytes); i++) out[i] = letters[bytes[i] % (sizeof(letters) - 1)];
	out[sizeof(bytes)] = '\0';
	return 0;
}

pid_t Sr_startSyncthing(const SrConfig* cfg, const char* log_path) {
	if (!cfg->syncthing[0]) return -1;
	// one left from a launcher that didn't stop it
	FILE* f = fopen(SYNCTHING_PID, "r");
	long old = 0;
	if (f) {
		if (fscanf(f, "%ld", &old) != 1) old = 0;
		fclose(f);
	}
	if (old > 1 && kill((pid_t)old, 0) == 0) {
		char comm[256] = "";
#ifdef __linux__
		char proc[64];
		snprintf(proc, sizeof(proc), "/proc/%ld/cmdline", old);
		FILE* c = fopen(proc, "r");
		if (c) {
			size_t n = fread(comm, 1, sizeof(comm) - 1, c);
			comm[n] = '\0';
			fclose(c);
		}
#else
		snprintf(comm, sizeof(comm), "syncthing");
#endif
		if (strstr(comm, "syncthing")) {
			kill((pid_t)old, SIGTERM);
			for (int i = 0; i < 100 && kill((pid_t)old, 0) == 0; i++) {
				struct timespec pause = { 0, 50 * 1000000L };
				nanosleep(&pause, NULL);
			}
		}
	}
	char home[SY_PATH_MAX + 16], gui[64], key[96];
	if (put(home, sizeof(home), "--home=%s/syncthing", cfg->data_dir) != 0) return -1;
	put(gui, sizeof(gui), "--gui-address=http://127.0.0.1:%d", cfg->gui_port);
	put(key, sizeof(key), "--gui-apikey=%s", cfg->api_key);
	char* argv[] = { (char*)cfg->syncthing, "serve", home, gui, key, "--no-browser", "--no-restart", NULL };
	Env env;
	envFor(cfg, &env);
	char dir[SY_PATH_MAX + 16];
	put(dir, sizeof(dir), "%s/syncthing", cfg->data_dir);
	makeDirs(dir);
	int log = open(log_path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
	int null = open("/dev/null", O_RDONLY | O_CLOEXEC);
	long max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
	pid_t pid = fork();
	if (pid == 0) {
#ifdef __linux__
		// gone with the thread that started it, the plugin's worker, which
		// lives until the launcher quits
		prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		if (null >= 0) dup2(null, 0);
		if (log >= 0) {
			dup2(log, 1);
			dup2(log, 2);
		}
		for (int fd = 3; fd < max_fd; fd++) close(fd);
		execve(argv[0], argv, env.list);
		_exit(127);
	}
	if (log >= 0) close(log);
	if (null >= 0) close(null);
	if (pid < 0) return -1;
	f = fopen(SYNCTHING_PID, "w");
	if (f) {
		fprintf(f, "%ld\n", (long)pid);
		fclose(f);
	}
	Sr_log(cfg, "started Syncthing, pid %ld", (long)pid);
	return pid;
}

void Sr_stopSyncthing(pid_t pid) {
	if (pid <= 1) return;
	kill(pid, SIGTERM);
	unlink(SYNCTHING_PID);
}
