// Wi-Fi for plugins: filling an answer from stock's fields, the strength
// bars, the desktop stand-in's file, and the worker's looks, caching,
// toggling, sleep, quiet spells and shutdown, against a fake source.

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "wifi.h"
#include "check.h"

static long nowMs(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static void testBars(void) {
	CHECK(Wifi_bars(-1) == 0);
	CHECK(Wifi_bars(0) == 0);
	CHECK(Wifi_bars(-40) == 3);
	CHECK(Wifi_bars(-60) == 3);
	CHECK(Wifi_bars(-61) == 2);
	CHECK(Wifi_bars(-70) == 2);
	CHECK(Wifi_bars(-71) == 1);
	CHECK(Wifi_bars(-95) == 1);
}

static void testFill(void) {
	WifiInfo info;
	memset(&info, 0x55, sizeof(info));
	Wifi_fill(&info, 0, "Home", "10.0.0.2", 2412, -50, 72);
	CHECK(info.connected == 0);
	CHECK_STR(info.ssid, "");
	CHECK_STR(info.ip, "");
	CHECK(info.rssi == 0 && info.bars == 0 && info.freq == 0 && info.link == 0);

	Wifi_fill(&info, 1, "Home", "10.0.0.2", 5180, -65, 433);
	CHECK(info.connected == 1);
	CHECK_STR(info.ssid, "Home");
	CHECK_STR(info.ip, "10.0.0.2");
	CHECK(info.rssi == -65 && info.bars == 2 && info.freq == 5180 && info.link == 433);

	// stock's -1 for not known, and no address yet
	Wifi_fill(&info, 1, "Cafe", "", -1, -1, -1);
	CHECK(info.connected == 1);
	CHECK_STR(info.ip, "");
	CHECK(info.rssi == 0 && info.bars == 0 && info.freq == 0 && info.link == 0);

	// control characters, NULL names and names too long
	Wifi_fill(&info, 1, "a\tb\nc\x7f", NULL, 2412, -80, 1);
	CHECK_STR(info.ssid, "a?b?c?");
	CHECK_STR(info.ip, "");
	CHECK(info.bars == 1);
	char longName[200];
	memset(longName, 'x', sizeof(longName) - 1);
	longName[sizeof(longName) - 1] = '\0';
	Wifi_fill(&info, 1, longName, longName, 0, 0, 0);
	CHECK(strlen(info.ssid) == WIFI_SSID_SIZE - 1);
	CHECK(strlen(info.ip) == WIFI_IP_SIZE - 1);
}

static void testParseFake(void) {
	int on = 7, delay = 7;
	WifiInfo info;
	Wifi_parseFake("enabled=1\r\nconnected=1\nssid=Home Net\nip=192.168.1.50\nrssi=-58\nfreq=2437\nlink=65\ndelay_ms=300\nother=1\nnoequals\n",
		&on, &info, &delay);
	CHECK(on == 1);
	CHECK(delay == 300);
	CHECK(info.connected == 1);
	CHECK_STR(info.ssid, "Home Net");
	CHECK_STR(info.ip, "192.168.1.50");
	CHECK(info.rssi == -58 && info.bars == 3 && info.freq == 2437 && info.link == 65);

	Wifi_parseFake("enabled=1\nssid=Home\n", &on, &info, &delay);
	CHECK(on == 1 && delay == 0);
	CHECK(info.connected == 0);
	CHECK_STR(info.ssid, "");

	Wifi_parseFake(NULL, &on, &info, &delay);
	CHECK(on == 0 && delay == 0 && info.connected == 0);
	Wifi_parseFake("enabled=0", &on, NULL, NULL);
	CHECK(on == 0);
}

// the fake source: what it answers, how often it's asked, and how long it takes
static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;
static WifiInfo fake_info;
static int fake_looks = 0;
static int fake_delay_ms = 0;
static int fake_rc = 0;

static int fakeSource(WifiInfo* out) {
	pthread_mutex_lock(&fake_lock);
	WifiInfo answer = fake_info;
	int delay = fake_delay_ms;
	int rc = fake_rc;
	fake_looks++;
	pthread_mutex_unlock(&fake_lock);
	if (delay) usleep((useconds_t)delay * 1000);
	*out = answer;
	return rc;
}

static void setFake(int connected, const char* ssid, int rssi, int delay_ms) {
	pthread_mutex_lock(&fake_lock);
	Wifi_fill(&fake_info, connected, ssid, "10.0.0.9", 2412, rssi, 54);
	fake_delay_ms = delay_ms;
	pthread_mutex_unlock(&fake_lock);
}

static int looks(void) {
	pthread_mutex_lock(&fake_lock);
	int n = fake_looks;
	pthread_mutex_unlock(&fake_lock);
	return n;
}

// Waits until the source has been asked n times, for up to ms.
static int waitLooks(int n, int ms) {
	long deadline = nowMs() + ms;
	while (looks() < n && nowMs() < deadline) usleep(5000);
	return looks();
}

// Asks until the answer is want, for up to ms; returns the last answer.
static int askUntil(int want, WifiInfo* out, int ms) {
	long deadline = nowMs() + ms;
	int rc;
	while ((rc = Wifi_connection(out)) != want && nowMs() < deadline) usleep(5000);
	return rc;
}

static char logged[4096];
static void capture(const char* message) {
	size_t len = strlen(logged);
	snprintf(logged + len, sizeof(logged) - len, "%s", message);
}

static void testWorker(void) {
	WifiInfo info;
	Wifi_setLog(capture);
	Wifi_setLimits(100, 400);

	// no source: unavailable once on, and nothing started
	Wifi_setEnabled(1);
	CHECK(Wifi_connection(&info) == -1);
	CHECK(strstr(logged, "worker started") == NULL);

	// off: not connected, at once, without a worker
	Wifi_setSource(fakeSource);
	Wifi_setEnabled(0);
	CHECK(Wifi_connection(&info) == 0);
	CHECK(strstr(logged, "worker started") == NULL);
	CHECK(looks() == 0);

	// on: unknown until the first look, which doesn't hold up the asker
	setFake(1, "Home", -55, 150);
	Wifi_setEnabled(1);
	long before = nowMs();
	CHECK(Wifi_connection(&info) == -1);
	CHECK(nowMs() - before < 50);
	CHECK(strstr(logged, "worker started") != NULL);
	CHECK(askUntil(1, &info, 2000) == 1);
	CHECK_STR(info.ssid, "Home");
	CHECK_STR(info.ip, "10.0.0.9");
	CHECK(info.rssi == -55 && info.bars == 3 && info.freq == 2412 && info.link == 54);

	// a change shows on a later look
	setFake(1, "Work", -72, 0);
	long changed = nowMs();
	while (strcmp(info.ssid, "Work") != 0 && nowMs() - changed < 2000) {
		Wifi_connection(&info);
		usleep(5000);
	}
	CHECK_STR(info.ssid, "Work");
	CHECK(info.bars == 1);

	// dropping off the network
	setFake(0, "", 0, 0);
	CHECK(askUntil(0, &info, 2000) == 0);
	CHECK_STR(info.ssid, "");

	// a failed look reads as not connected
	setFake(1, "Home", -55, 0);
	CHECK(askUntil(1, &info, 2000) == 1);
	pthread_mutex_lock(&fake_lock);
	fake_rc = -1;
	pthread_mutex_unlock(&fake_lock);
	CHECK(askUntil(0, &info, 2000) == 0);
	pthread_mutex_lock(&fake_lock);
	fake_rc = 0;
	pthread_mutex_unlock(&fake_lock);
	CHECK(askUntil(1, &info, 2000) == 1);

	// off forgets the answer at once; on again is unknown until a new look
	Wifi_setEnabled(0);
	CHECK(Wifi_connection(&info) == 0);
	CHECK_STR(info.ssid, "");
	setFake(1, "Again", -50, 100);
	Wifi_setEnabled(1);
	CHECK(Wifi_connection(&info) == -1);
	CHECK(askUntil(1, &info, 2000) == 1);
	CHECK_STR(info.ssid, "Again");

	// a look that started before a toggle is dropped
	setFake(1, "Stale", -50, 200);
	usleep(150 * 1000); // the next look is under way
	Wifi_setEnabled(0);
	setFake(1, "Fresh", -50, 0);
	Wifi_setEnabled(1);
	CHECK(askUntil(1, &info, 2000) == 1);
	CHECK_STR(info.ssid, "Fresh");

	// asleep: no looks
	Wifi_pause(1);
	usleep(150 * 1000);
	int asleep = looks();
	usleep(300 * 1000);
	CHECK(looks() == asleep);
	CHECK(Wifi_connection(&info) == 1);
	Wifi_pause(0);
	CHECK(waitLooks(asleep + 1, 1000) > asleep);

	// a quiet spell: looks stop, and the next ask starts them again at once
	usleep(600 * 1000);
	int quiet = looks();
	usleep(300 * 1000);
	CHECK(looks() == quiet);
	setFake(1, "Woken", -50, 0);
	Wifi_connection(&info);
	CHECK(waitLooks(quiet + 1, 1000) > quiet);
	long woken = nowMs();
	while (Wifi_connection(&info) == 1 && strcmp(info.ssid, "Woken") != 0 && nowMs() - woken < 1000) usleep(5000);
	CHECK_STR(info.ssid, "Woken");

	// quit stops it, and asking again starts a new worker
	Wifi_quit();
	CHECK(strstr(logged, "wifi: worker stopped\n") != NULL);
	int stopped = looks();
	usleep(250 * 1000);
	CHECK(looks() == stopped);
	CHECK(Wifi_connection(&info) == -1);
	CHECK(askUntil(1, &info, 2000) == 1);

	// a look still running at quit is left to finish and dropped
	setFake(1, "Slow", -50, 1500);
	usleep(150 * 1000); // under way
	long quitting = nowMs();
	Wifi_quit();
	long waited = nowMs() - quitting;
	CHECK(waited >= 900 && waited < 1400);
	CHECK(strstr(logged, "left to finish") != NULL);
	setFake(1, "After", -50, 0);
	CHECK(Wifi_connection(&info) == -1);
	CHECK(askUntil(1, &info, 2000) == 1);
	CHECK_STR(info.ssid, "After");
	usleep(600 * 1000); // the old look comes back meanwhile and is dropped
	CHECK(Wifi_connection(&info) == 1);
	CHECK_STR(info.ssid, "After");
	Wifi_quit();
	Wifi_quit(); // twice is fine
}

int main(void) {
	testBars();
	testFill();
	testParseFake();
	testWorker();
	CHECK_DONE();
}
