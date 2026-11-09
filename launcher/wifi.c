#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "wifi.h"

static int refresh_ms = 5000;
static int idle_ms = 30000;
#define QUIT_WAIT_MS 1000 // for a look still running at quit

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_cond_t gone = PTHREAD_COND_INITIALIZER;
static WifiSource source = NULL;
static int started = 0;       // the current worker is running
static unsigned epoch = 0;    // the current worker's; quitting moves it on
static int running = 0;       // workers alive, the current one and any left finishing a look
static int enabled = 0;       // the user's toggle, from the main thread
static unsigned turned = 0;   // moves on with each toggle, so a look from before it is dropped
static int paused = 0;        // asleep: no looks
static int refresh_now = 0;   // look on the worker's next turn
static long last_ask = 0;     // when a plugin last asked, in nowMs
static int have = 0;          // info holds a look since Wi-Fi came on
static WifiInfo info;         // the last look's answer

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

static void untilMs(struct timespec* until, long wait_ms) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long long ns = (long long)tv.tv_usec * 1000 + (long long)(wait_ms % 1000) * 1000000;
	until->tv_sec = tv.tv_sec + wait_ms / 1000 + (time_t)(ns / 1000000000);
	until->tv_nsec = (long)(ns % 1000000000);
}

///////////////////////////////////////
// answers

int Wifi_bars(int rssi) {
	if (rssi == -1 || rssi == 0) return 0;
	if (rssi >= -60) return 3;
	if (rssi >= -70) return 2;
	return 1;
}

static void copyName(char* out, size_t size, const char* in) {
	size_t n = 0;
	for (; in && in[n] && n + 1 < size; n++) {
		unsigned char c = (unsigned char)in[n];
		out[n] = c < 0x20 || c == 0x7f ? '?' : (char)c;
	}
	out[n] = '\0';
}

void Wifi_fill(WifiInfo* out, int valid, const char* ssid, const char* ip, int freq, int rssi, int link) {
	memset(out, 0, sizeof(*out));
	if (!valid) return;
	out->connected = 1;
	copyName(out->ssid, sizeof(out->ssid), ssid);
	copyName(out->ip, sizeof(out->ip), ip);
	out->rssi = rssi == -1 ? 0 : rssi;
	out->bars = Wifi_bars(rssi);
	out->freq = freq > 0 ? freq : 0;
	out->link = link > 0 ? link : 0;
}

void Wifi_parseFake(const char* text, int* on, WifiInfo* out, int* delay_ms) {
	int connected = 0, freq = -1, rssi = -1, link = -1, delay = 0, enabled_key = 0;
	char ssid[WIFI_SSID_SIZE] = "", ip[WIFI_IP_SIZE] = "";
	const char* p = text ? text : "";
	while (*p) {
		const char* end = strchr(p, '\n');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		char line[256];
		if (len >= sizeof(line)) len = sizeof(line) - 1;
		memcpy(line, p, len);
		line[len] = '\0';
		if (len && line[len - 1] == '\r') line[len - 1] = '\0';
		char* eq = strchr(line, '=');
		if (eq) {
			*eq = '\0';
			const char* value = eq + 1;
			if (!strcmp(line, "enabled")) enabled_key = atoi(value);
			else if (!strcmp(line, "connected")) connected = atoi(value);
			else if (!strcmp(line, "ssid")) snprintf(ssid, sizeof(ssid), "%s", value);
			else if (!strcmp(line, "ip")) snprintf(ip, sizeof(ip), "%s", value);
			else if (!strcmp(line, "rssi")) rssi = atoi(value);
			else if (!strcmp(line, "freq")) freq = atoi(value);
			else if (!strcmp(line, "link")) link = atoi(value);
			else if (!strcmp(line, "delay_ms")) delay = atoi(value);
		}
		if (!end) break;
		p = end + 1;
	}
	if (on) *on = enabled_key ? 1 : 0;
	if (out) Wifi_fill(out, connected, ssid, ip, freq, rssi, link);
	if (delay_ms) *delay_ms = delay > 0 ? delay : 0;
}

///////////////////////////////////////
// the worker

// With the lock held: whether a plugin has asked lately enough to keep looking.
static int asked(long now) {
	return now - last_ask < idle_ms;
}

static void* work(void* arg) {
	unsigned me = (unsigned)(uintptr_t)arg;
	long next = 0; // the first look is at once
	pthread_mutex_lock(&lock);
	while (me == epoch) {
		if (refresh_now) {
			refresh_now = 0;
			next = 0;
		}
		long now = nowMs();
		int looking = enabled && !paused && source && asked(now);
		if (looking && now >= next) {
			WifiSource look = source;
			unsigned toggle = turned;
			pthread_mutex_unlock(&lock);
			WifiInfo got;
			memset(&got, 0, sizeof(got));
			int rc = look(&got);
			pthread_mutex_lock(&lock);
			if (me != epoch) break;
			// Wi-Fi went off or on again meanwhile: this look is from before
			if (toggle == turned && enabled) {
				if (rc != 0) memset(&got, 0, sizeof(got));
				info = got;
				have = 1;
			}
			next = nowMs() + refresh_ms;
			continue;
		}
		// asleep, off or not asked: until something wakes it
		long wait_ms = looking ? next - now : 60000;
		if (wait_ms < 1) wait_ms = 1;
		struct timespec until;
		untilMs(&until, wait_ms);
		pthread_cond_timedwait(&wake, &lock, &until);
	}
	running--;
	pthread_cond_broadcast(&gone);
	pthread_mutex_unlock(&lock);
	return NULL;
}

// With the lock held.
static int ensureWorker(void) {
	if (started) return 0;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_t worker;
	unsigned me = ++epoch;
	int rc = pthread_create(&worker, &attr, work, (void*)(uintptr_t)me);
	pthread_attr_destroy(&attr);
	if (rc != 0) return -1;
	started = 1;
	running++;
	say("wifi: worker started\n");
	return 0;
}

void Wifi_setSource(WifiSource s) {
	pthread_mutex_lock(&lock);
	source = s;
	pthread_mutex_unlock(&lock);
}

void Wifi_setEnabled(int on) {
	pthread_mutex_lock(&lock);
	on = on ? 1 : 0;
	if (on != enabled) {
		enabled = on;
		turned++;
		have = 0;
		memset(&info, 0, sizeof(info));
		refresh_now = 1;
		if (started) pthread_cond_signal(&wake);
	}
	pthread_mutex_unlock(&lock);
}

int Wifi_connection(WifiInfo* out) {
	if (out) memset(out, 0, sizeof(*out));
	pthread_mutex_lock(&lock);
	int result;
	if (!enabled) result = 0;
	else if (!source || ensureWorker() != 0) result = -1;
	else {
		long now = nowMs();
		// after a quiet spell the worker has stopped looking, so look now
		if (!asked(now)) {
			refresh_now = 1;
			pthread_cond_signal(&wake);
		}
		last_ask = now;
		if (!have) result = -1;
		else {
			if (out) *out = info;
			result = info.connected ? 1 : 0;
		}
	}
	pthread_mutex_unlock(&lock);
	return result;
}

void Wifi_pause(int on) {
	pthread_mutex_lock(&lock);
	paused = on ? 1 : 0;
	if (!paused) refresh_now = 1;
	if (started) pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
}

void Wifi_quit(void) {
	pthread_mutex_lock(&lock);
	if (!started) {
		pthread_mutex_unlock(&lock);
		return;
	}
	epoch++;
	started = 0;
	pthread_cond_broadcast(&wake);
	struct timespec until;
	untilMs(&until, QUIT_WAIT_MS);
	while (running > 0) {
		if (pthread_cond_timedwait(&gone, &lock, &until) != 0) break;
	}
	int left = running;
	have = 0;
	memset(&info, 0, sizeof(info));
	refresh_now = 0;
	paused = 0;
	last_ask = 0;
	pthread_mutex_unlock(&lock);
	if (left) say("wifi: worker stopped, a look still running is left to finish\n");
	else say("wifi: worker stopped\n");
}

void Wifi_setLog(void (*log)(const char* message)) {
	log_fn = log;
}

void Wifi_setLimits(int refresh, int idle) {
	refresh_ms = refresh;
	idle_ms = idle;
}
