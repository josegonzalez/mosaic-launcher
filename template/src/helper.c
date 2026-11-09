// The plugin's side of its helper program. The helper is found by its
// path beside plugin.so, through dladdr, and started with fork and execve:
// never through a shell or PATH. It gets one end of a socket pair as its
// stdin, its output goes to the plugin's log file, and no other file of
// the launcher's stays open in it. Libraries preloaded into the launcher
// (LD_PRELOAD, DYLD_INSERT_LIBRARIES) aren't passed on. It quits when its
// socket closes, so it's stopped by closing that and waiting for its pid.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // dladdr
#endif

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "helper.h"

extern char** environ;

static pid_t helper = -1;
static int helper_fd = -1;

// path is the folder plugin.so is in, then "/" and program
static int besidePlugin(const char* program, char* path, size_t size) {
	Dl_info info;
	if (!dladdr((const void*)&besidePlugin, &info) || !info.dli_fname) return -1;
	const char* slash = strrchr(info.dli_fname, '/');
	if (!slash) return -1;
	int n = snprintf(path, size, "%.*s/%s", (int)(slash - info.dli_fname), info.dli_fname, program);
	return n > 0 && (size_t)n < size ? 0 : -1;
}

// the launcher's environment without preloaded libraries; NULL without
// memory
static char** helperEnv(void) {
	int count = 0;
	while (environ[count]) count++;
	char** env = malloc((size_t)(count + 1) * sizeof(char*));
	if (!env) return NULL;
	int n = 0;
	for (int i = 0; i < count; i++) {
		if (!strncmp(environ[i], "LD_PRELOAD=", 11) || !strncmp(environ[i], "DYLD_INSERT_LIBRARIES=", 22)) continue;
		env[n++] = environ[i];
	}
	env[n] = NULL;
	return env;
}

int helper_start(const MosaicHost* host, const char* id, const char* program) {
	if (helper > 0) return 0;
	char path[1024], log_path[1024];
	struct stat sb;
	if (besidePlugin(program, path, sizeof(path)) != 0 || stat(path, &sb) != 0 || !S_ISREG(sb.st_mode) || access(path, X_OK) != 0) {
		return -1;
	}
	snprintf(log_path, sizeof(log_path), "%s/logs", host->userdata_path);
	mkdir(log_path, 0755);
	snprintf(log_path, sizeof(log_path), "%s/logs/%s.txt", host->userdata_path, id);

	int fds[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return -1;
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt(fds[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
	char** env = helperEnv();
	int out = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (out < 0) out = open("/dev/null", O_WRONLY | O_CLOEXEC);
	long max_fd = sysconf(_SC_OPEN_MAX);
	if (max_fd < 0 || max_fd > 4096) max_fd = 4096;
	char* const argv[] = { path, NULL };

	// only calls that are safe after a fork, until execve
	pid_t pid = env ? fork() : -1;
	if (pid == 0) {
		dup2(fds[1], 0);
		if (out >= 0) {
			dup2(out, 1);
			dup2(out, 2);
		}
		for (int fd = 3; fd < max_fd; fd++) close(fd);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		execve(path, argv, env);
		_exit(127);
	}
	free(env);
	if (out >= 0) close(out);
	close(fds[1]);
	if (pid < 0) {
		close(fds[0]);
		return -1;
	}
	helper = pid;
	helper_fd = fds[0];
	return 0;
}

int helper_ask(const char* line, char* reply, size_t size, int timeout_ms) {
	if (size) reply[0] = '\0';
	if (helper_fd < 0 || !size) return -1;
	struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
	setsockopt(helper_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(helper_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef MSG_NOSIGNAL
	int flags = MSG_NOSIGNAL;
#else
	int flags = 0;
#endif
	size_t len = strlen(line), sent = 0;
	while (sent < len) {
		ssize_t n = send(helper_fd, line + sent, len - sent, flags);
		if (n <= 0) return -1;
		sent += (size_t)n;
	}
	// one byte at a time, so nothing past the line is read
	size_t got = 0;
	while (got + 1 < size) {
		char c;
		if (read(helper_fd, &c, 1) != 1) return -1;
		if (c == '\n') break;
		reply[got++] = c;
	}
	reply[got] = '\0';
	return 0;
}

int helper_pid(void) {
	return helper > 0 ? (int)helper : -1;
}

void helper_stop(void) {
	if (helper_fd >= 0) close(helper_fd);
	helper_fd = -1;
	if (helper <= 0) return;
	// it quits once its socket closes; give it a moment, then make sure
	for (int i = 0; i < 50; i++) {
		if (waitpid(helper, NULL, WNOHANG) != 0) {
			helper = -1;
			return;
		}
		struct timespec pause = { 0, 10000000L };
		nanosleep(&pause, NULL);
	}
	kill(helper, SIGKILL);
	waitpid(helper, NULL, 0);
	helper = -1;
}
