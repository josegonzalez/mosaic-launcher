// Running one task: see tasks_run.h.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // posix_spawn_file_actions_addclosefrom_np
#endif
#include "tasks_run.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

long long TK_nowMs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void setError(char* error, size_t size, const char* what, int err) {
	if (error && size) snprintf(error, size, "%s: %s", what, strerror(err));
}

int TK_start(TKRun* run, const char* script, const char* game, char* const env[], const char* log_path,
		const char* header, char* error, size_t error_size) {
	if (error && error_size) error[0] = '\0';
	memset(run, 0, sizeof(*run));
	if (!script || !log_path) {
		setError(error, error_size, "no script", EINVAL);
		return -1;
	}
	snprintf(run->log_path, sizeof(run->log_path), "%s", log_path);
	int log = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (log < 0) {
		setError(error, error_size, "couldn't open its log", errno);
		return -1;
	}
	if (header && header[0]) {
		size_t len = strlen(header);
		if (write(log, header, len) != (ssize_t)len) {
			setError(error, error_size, "couldn't write its log", errno);
			close(log);
			return -1;
		}
	}
	run->output_start = (long)lseek(log, 0, SEEK_END);

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2(&actions, log, 1);
	posix_spawn_file_actions_adddup2(&actions, log, 2);
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	// a group of its own, so stopping it stops what it starts too
	short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP;
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
	flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
	posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#else
	long max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
	for (int fd = 3; fd < max_fd; fd++) {
		if (fd != log && fcntl(fd, F_GETFD) >= 0) posix_spawn_file_actions_addclose(&actions, fd);
	}
#endif
	posix_spawnattr_setflags(&attr, flags);
	posix_spawnattr_setpgroup(&attr, 0);
	sigset_t none, defaults;
	sigemptyset(&none);
	posix_spawnattr_setsigmask(&attr, &none);
	sigemptyset(&defaults);
	sigaddset(&defaults, SIGPIPE);
	sigaddset(&defaults, SIGTERM);
	sigaddset(&defaults, SIGINT);
	sigaddset(&defaults, SIGHUP);
	sigaddset(&defaults, SIGCHLD);
	posix_spawnattr_setsigdefault(&attr, &defaults);

	char* argv[4];
	argv[0] = (char*)"/bin/sh";
	argv[1] = (char*)script;
	argv[2] = game ? (char*)game : NULL;
	argv[3] = NULL;
	pid_t pid;
	int rc = posix_spawn(&pid, argv[0], &actions, &attr, argv, env);
	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&actions);
	close(log);
	if (rc != 0) {
		setError(error, error_size, "couldn't start /bin/sh", rc);
		return -1;
	}
	run->pid = pid;
	run->how = TK_RUNNING;
	run->started = TK_nowMs();
	return 0;
}

static void signalGroup(TKRun* run, int sig) {
	if (run->pid <= 1) return;
	// the group may be gone with only its leader left to reap
	if (kill(-run->pid, sig) != 0 && errno == ESRCH) kill(run->pid, sig);
}

static void finish(TKRun* run, int status) {
	if (run->stopping) {
		run->how = TK_STOPPED;
		run->code = 0;
		// what's left of the group, if it ignored SIGTERM
		if (!run->killed) kill(-run->pid, SIGKILL);
	} else if (WIFEXITED(status)) {
		run->how = TK_EXITED;
		run->code = WEXITSTATUS(status);
	} else {
		run->how = TK_SIGNALED;
		run->code = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
	}
	run->pid = 0;

	struct stat st;
	run->output_end = stat(run->log_path, &st) == 0 ? (long)st.st_size : run->output_start;
	FILE* f = fopen(run->log_path, "a");
	if (!f) return;
	long long seconds = (TK_nowMs() - run->started) / 1000;
	if (run->how == TK_EXITED) fprintf(f, "== exited with %d after %lld s\n", run->code, seconds);
	else if (run->how == TK_STOPPED) fprintf(f, "== stopped after %lld s\n", seconds);
	else fprintf(f, "== killed by signal %d after %lld s\n", run->code, seconds);
	fclose(f);
}

int TK_poll(TKRun* run) {
	if (run->pid <= 0) return run->how != TK_RUNNING;
	if (run->stopping && !run->killed && TK_nowMs() - run->stop_at >= TK_STOP_GRACE_MS) {
		signalGroup(run, SIGKILL);
		run->killed = 1;
	}
	int status = 0;
	pid_t w = waitpid(run->pid, &status, WNOHANG);
	if (w == 0 || (w < 0 && errno == EINTR)) return 0;
	if (w < 0) status = 0; // reaped elsewhere: say it exited
	finish(run, status);
	return 1;
}

void TK_stop(TKRun* run) {
	if (run->pid <= 0 || run->stopping) return;
	run->stopping = 1;
	run->stop_at = TK_nowMs();
	signalGroup(run, SIGTERM);
}

void TK_kill(TKRun* run) {
	if (run->pid <= 0) return;
	if (!run->stopping) {
		run->stopping = 1;
		run->stop_at = TK_nowMs();
	}
	signalGroup(run, SIGKILL);
	run->killed = 1;
	int status = 0;
	while (waitpid(run->pid, &status, 0) < 0 && errno == EINTR) {}
	finish(run, status);
}

size_t TK_outputTail(const TKRun* run, char* out, size_t size) {
	if (!out || size < 2) return 0;
	out[0] = '\0';
	long end = run->output_end;
	if (end <= run->output_start) return 0;
	long start = end - (long)(size - 1);
	if (start < run->output_start) start = run->output_start;
	int fd = open(run->log_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return 0;
	size_t got = 0, want = (size_t)(end - start);
	if (lseek(fd, start, SEEK_SET) == start) {
		while (got < want) {
			ssize_t n = read(fd, out + got, want - got);
			if (n < 0 && errno == EINTR) continue;
			if (n <= 0) break;
			got += (size_t)n;
		}
	}
	close(fd);
	out[got] = '\0';
	return got;
}
