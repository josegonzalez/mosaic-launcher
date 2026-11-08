#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "bluetooth.h"

static int refresh_ms = 5000;
static int refresh_timeout_ms = 3000;
static int connect_timeout_ms = 15000;
#define KILL_GRACE_MS 500 // between SIGTERM and SIGKILL
#define POLL_MS 20

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int started = 0;     // the worker is running
static int stopping = 0;    // read without the lock by Bluetooth_run
static int enabled = 0;     // the user's toggle, from the main thread
static int paused = 0;      // asleep: no refreshes
static int refresh_now = 0; // refresh on the worker's next turn
static char pending[BLUETOOTH_ADDRESS_SIZE]; // a queued connect
static int connecting = 0;  // a connect is running
static char address[BLUETOOTH_ADDRESS_SIZE]; // the last refresh's answer

static void (*log_fn)(const char* message) = NULL;

__attribute__((format(printf, 1, 2)))
static void say(const char* fmt, ...) {
	if (!log_fn) return;
	char message[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	log_fn(message);
}

static long nowMs(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static int isStopping(void) {
	return __atomic_load_n(&stopping, __ATOMIC_SEQ_CST);
}

///////////////////////////////////////
// addresses

int Bluetooth_validAddress(const char* s) {
	if (!s) return 0;
	for (int i = 0; i < 17; i++) {
		if (i % 3 == 2) {
			if (s[i] != ':') return 0;
		}
		else if (!isxdigit((unsigned char)s[i])) return 0;
	}
	return s[17] == '\0';
}

static void upper(char* out, const char* in) {
	for (int i = 0; i < 17; i++) out[i] = (char)toupper((unsigned char)in[i]);
	out[17] = '\0';
}

// The address right after word at p, if there is one, ending the token.
static int addressAfter(const char* p, const char* word, char* out) {
	size_t len = strlen(word);
	if (strncmp(p, word, len) != 0) return 0;
	p += len;
	char token[BLUETOOTH_ADDRESS_SIZE];
	for (int i = 0; i < 17; i++) {
		if (!p[i]) return 0;
		token[i] = p[i];
	}
	token[17] = '\0';
	if (p[17] && !isspace((unsigned char)p[17])) return 0;
	if (!Bluetooth_validAddress(token)) return 0;
	upper(out, token);
	return 1;
}

int Bluetooth_parseConnected(const char* output, char* out) {
	out[0] = '\0';
	if (!output) return 0;
	for (const char* p = output; *p; p++) {
		// only at the start of a word
		if (p != output && !isspace((unsigned char)p[-1])) continue;
		if (addressAfter(p, "ACL ", out) || addressAfter(p, "Device ", out)) return 1;
	}
	return 0;
}

///////////////////////////////////////
// running a program

// Whether pid has exited, leaving it a zombie so its pid and process group
// can't be reused until it's reaped.
static int hasExited(pid_t pid) {
	siginfo_t info;
	memset(&info, 0, sizeof(info));
	if (waitid(P_PID, pid, &info, WEXITED | WNOHANG | WNOWAIT) != 0) return errno != EINTR;
	return info.si_pid == pid;
}

static void signalGroup(pid_t pid, int sig) {
	kill(-pid, sig);
	kill(pid, sig); // in case it never got a group of its own
}

static int reap(pid_t pid) {
	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) return -1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void drain(int fd, char* out, size_t size, size_t* got, int* eof) {
	char buf[256];
	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n > 0) {
			if (out && *got + 1 < size) {
				size_t take = (size_t)n;
				if (take > size - 1 - *got) take = size - 1 - *got;
				memcpy(out + *got, buf, take);
				*got += take;
				out[*got] = '\0';
			}
			continue;
		}
		if (n == 0) *eof = 1;
		else if (errno == EINTR) continue;
		return;
	}
}

int Bluetooth_run(char* const argv[], int timeout_ms, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	if (!argv || !argv[0]) return -1;

	int fds[2];
	if (pipe(fds) != 0) return -1;
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);

	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (pid == 0) {
		// only async-signal-safe calls until the exec
		setpgid(0, 0);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		signal(SIGPIPE, SIG_DFL);
		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDERR_FILENO);
		}
		dup2(fds[1], STDOUT_FILENO);
		execvp(argv[0], argv);
		_exit(127);
	}
	setpgid(pid, pid); // either side may win; both ask for the same group
	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);

	size_t got = 0;
	int eof = 0;
	int result;
	long deadline = nowMs() + timeout_ms;
	for (;;) {
		if (hasExited(pid)) {
			drain(fds[0], out, size, &got, &eof);
			// anything it left behind in its group goes with it
			kill(-pid, SIGKILL);
			result = reap(pid);
			break;
		}
		if (isStopping() || nowMs() >= deadline) {
			signalGroup(pid, SIGTERM);
			long grace = nowMs() + KILL_GRACE_MS;
			while (!hasExited(pid) && nowMs() < grace) usleep(POLL_MS * 1000);
			signalGroup(pid, SIGKILL);
			reap(pid);
			result = -2;
			break;
		}
		if (eof) {
			usleep(POLL_MS * 1000);
			continue;
		}
		struct pollfd p = {.fd = fds[0], .events = POLLIN};
		if (poll(&p, 1, POLL_MS) > 0) drain(fds[0], out, size, &got, &eof);
	}
	close(fds[0]);
	return result;
}

///////////////////////////////////////
// the worker

static void refresh(void) {
	pthread_mutex_lock(&lock);
	int on = enabled;
	pthread_mutex_unlock(&lock);

	char found[BLUETOOTH_ADDRESS_SIZE] = "";
	if (on) {
		char output[2048];
		char* hcitool[] = {"hcitool", "con", NULL};
		int status = Bluetooth_run(hcitool, refresh_timeout_ms, output, sizeof(output));
		if (status == 0) Bluetooth_parseConnected(output, found);
		else if (status == 127) {
			char* ctl[] = {"bluetoothctl", "devices", "Connected", NULL};
			if (Bluetooth_run(ctl, refresh_timeout_ms, output, sizeof(output)) == 0) {
				Bluetooth_parseConnected(output, found);
			}
		}
	}

	pthread_mutex_lock(&lock);
	snprintf(address, sizeof(address), "%s", found);
	pthread_mutex_unlock(&lock);
}

static void runConnect(char* target) {
	say("bluetooth: connecting %s\n", target);
	char* argv[] = {"bluetoothctl", "connect", target, NULL};
	int status = Bluetooth_run(argv, connect_timeout_ms, NULL, 0);
	if (status == -2) say("bluetooth: connecting %s stopped after %ims\n", target, connect_timeout_ms);
	else say("bluetooth: bluetoothctl connect %s exited %i\n", target, status);
}

static void* work(void* arg) {
	(void)arg;
	long next = 0; // the first refresh is at once
	pthread_mutex_lock(&lock);
	while (!stopping) {
		if (pending[0]) {
			char target[BLUETOOTH_ADDRESS_SIZE];
			snprintf(target, sizeof(target), "%s", pending);
			pending[0] = '\0';
			connecting = 1;
			pthread_mutex_unlock(&lock);
			runConnect(target);
			pthread_mutex_lock(&lock);
			connecting = 0;
			next = 0; // see whether it took
			continue;
		}
		if (refresh_now) {
			refresh_now = 0;
			next = 0;
		}
		if (!paused && nowMs() >= next) {
			pthread_mutex_unlock(&lock);
			refresh();
			pthread_mutex_lock(&lock);
			next = nowMs() + refresh_ms;
			continue;
		}
		long wait_ms = paused ? 60000 : next - nowMs();
		if (wait_ms < 1) wait_ms = 1;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		long long ns = (long long)tv.tv_usec * 1000 + (long long)(wait_ms % 1000) * 1000000;
		struct timespec until = {
			.tv_sec = tv.tv_sec + wait_ms / 1000 + (time_t)(ns / 1000000000),
			.tv_nsec = (long)(ns % 1000000000),
		};
		pthread_cond_timedwait(&wake, &lock, &until);
	}
	pthread_mutex_unlock(&lock);
	return NULL;
}

// With the lock held.
static int ensureWorker(void) {
	static int registered = 0;
	if (started) return 0;
	if (!registered) {
		atexit(Bluetooth_quit);
		registered = 1;
	}
	__atomic_store_n(&stopping, 0, __ATOMIC_SEQ_CST);
	if (pthread_create(&worker, NULL, work, NULL) != 0) return -1;
	started = 1;
	say("bluetooth: worker started\n");
	return 0;
}

void Bluetooth_setEnabled(int on) {
	pthread_mutex_lock(&lock);
	on = on ? 1 : 0;
	if (on != enabled) {
		enabled = on;
		if (!on) address[0] = '\0';
		refresh_now = 1;
		if (started) pthread_cond_signal(&wake);
	}
	pthread_mutex_unlock(&lock);
}

int Bluetooth_connectedAddress(char* out, size_t size) {
	if (out && size) out[0] = '\0';
	pthread_mutex_lock(&lock);
	// while it's off there's nothing to refresh, so the worker waits too
	if (enabled) ensureWorker();
	int found = 0;
	if (enabled && address[0] && out && size >= BLUETOOTH_ADDRESS_SIZE) {
		snprintf(out, size, "%s", address);
		found = 1;
	}
	pthread_mutex_unlock(&lock);
	return found;
}

int Bluetooth_connect(const char* target) {
	if (!Bluetooth_validAddress(target)) return BLUETOOTH_BAD_ADDRESS;
	pthread_mutex_lock(&lock);
	int result = 0;
	if (!enabled) result = BLUETOOTH_OFF;
	else if (pending[0] || connecting) result = BLUETOOTH_BUSY;
	else if (ensureWorker() != 0) result = BLUETOOTH_NO_WORKER;
	else {
		upper(pending, target);
		pthread_cond_signal(&wake);
	}
	pthread_mutex_unlock(&lock);
	return result;
}

int Bluetooth_connecting(void) {
	pthread_mutex_lock(&lock);
	int busy = pending[0] || connecting;
	pthread_mutex_unlock(&lock);
	return busy;
}

void Bluetooth_pause(int on) {
	pthread_mutex_lock(&lock);
	paused = on ? 1 : 0;
	if (!paused) refresh_now = 1;
	if (started) pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
}

void Bluetooth_quit(void) {
	pthread_mutex_lock(&lock);
	if (!started) {
		pthread_mutex_unlock(&lock);
		return;
	}
	__atomic_store_n(&stopping, 1, __ATOMIC_SEQ_CST);
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);

	pthread_join(worker, NULL);

	pthread_mutex_lock(&lock);
	started = 0;
	pending[0] = '\0';
	connecting = 0;
	address[0] = '\0';
	paused = 0;
	pthread_mutex_unlock(&lock);
	say("bluetooth: worker stopped\n");
}

void Bluetooth_setLog(void (*log)(const char* message)) {
	log_fn = log;
}

void Bluetooth_setLimits(int refresh, int refresh_timeout, int connect_timeout) {
	refresh_ms = refresh;
	refresh_timeout_ms = refresh_timeout;
	connect_timeout_ms = connect_timeout;
}
