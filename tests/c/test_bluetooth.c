// Bluetooth for plugins: the address check, the bounded runner, and the
// worker's refresh, connect, refusals and shutdown, against a fake hcitool
// and bluetoothctl first on PATH.

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "bluetooth.h"
#include "check.h"

#ifndef TEST_DIR
#error "build with -DTEST_DIR=\"...\""
#endif

static char logged[8192];

static void capture(const char* message) {
	size_t len = strlen(logged);
	snprintf(logged + len, sizeof(logged) - len, "%s", message);
}

static long nowMs(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static void pathTo(char* out, size_t size, const char* name) {
	snprintf(out, size, "%s/%s", TEST_DIR, name);
}

static void writeFile(const char* name, const char* text) {
	char path[512];
	pathTo(path, sizeof(path), name);
	FILE* f = fopen(path, "w");
	if (!f) {
		perror(path);
		exit(1);
	}
	fputs(text, f);
	fclose(f);
}

static void writeScript(const char* name, const char* body) {
	writeFile(name, body);
	char path[512];
	pathTo(path, sizeof(path), name);
	chmod(path, 0755);
}

static void removeFile(const char* name) {
	char path[512];
	pathTo(path, sizeof(path), name);
	unlink(path);
}

static char* readFile(const char* name) {
	static char text[4096];
	char path[512];
	pathTo(path, sizeof(path), name);
	text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return text;
	size_t got = fread(text, 1, sizeof(text) - 1, f);
	text[got] = '\0';
	fclose(f);
	return text;
}

static int countLines(const char* name) {
	int lines = 0;
	for (const char* p = readFile(name); *p; p++) lines += *p == '\n';
	return lines;
}

// The pid a fake wrote, waiting up to 2s for it.
static pid_t readPid(const char* name) {
	for (long until = nowMs() + 2000; nowMs() < until; usleep(10000)) {
		int pid = atoi(readFile(name));
		if (pid > 0) return pid;
	}
	return 0;
}

// Whether pid is gone, giving whoever reaps an orphan up to 2s.
static int gone(pid_t pid) {
	if (pid <= 0) return 0;
	for (long until = nowMs() + 2000; nowMs() < until; usleep(10000)) {
		if (kill(pid, 0) != 0 && errno == ESRCH) return 1;
	}
	return 0;
}

static int connected(char* out) {
	return Bluetooth_connectedAddress(out, BLUETOOTH_ADDRESS_SIZE);
}

// Waits up to ms for the refreshed address to be want ("" for none).
static int addressBecomes(const char* want, int ms) {
	char got[BLUETOOTH_ADDRESS_SIZE];
	for (long until = nowMs() + ms; nowMs() < until; usleep(10000)) {
		connected(got);
		if (!strcmp(got, want)) return 1;
	}
	return 0;
}

static int connectEnds(int ms) {
	for (long until = nowMs() + ms; nowMs() < until; usleep(10000)) {
		if (!Bluetooth_connecting()) return 1;
	}
	return 0;
}

static void testAddresses(void) {
	CHECK(Bluetooth_validAddress("AA:BB:CC:DD:EE:FF"));
	CHECK(Bluetooth_validAddress("00:1a:7d:da:71:0f"));
	CHECK(!Bluetooth_validAddress(NULL));
	CHECK(!Bluetooth_validAddress(""));
	CHECK(!Bluetooth_validAddress("AA:BB:CC:DD:EE"));
	CHECK(!Bluetooth_validAddress("AA:BB:CC:DD:EE:F"));
	CHECK(!Bluetooth_validAddress("AA:BB:CC:DD:EE:FF:00"));
	CHECK(!Bluetooth_validAddress("AA:BB:CC:DD:EE:FF "));
	CHECK(!Bluetooth_validAddress(" AA:BB:CC:DD:EE:FF"));
	CHECK(!Bluetooth_validAddress("AA-BB-CC-DD-EE-FF"));
	CHECK(!Bluetooth_validAddress("GG:BB:CC:DD:EE:FF"));
	CHECK(!Bluetooth_validAddress("AA:BB:CC:DD:EE:FF;reboot"));
	CHECK(!Bluetooth_validAddress("$(reboot):CC:DD:EE:FF"));
	CHECK(!Bluetooth_validAddress("AA:BB:CC:DD:EE:F\n"));
}

static void testParse(void) {
	char out[BLUETOOTH_ADDRESS_SIZE];
	CHECK(Bluetooth_parseConnected("Connections:\n\t< ACL 11:22:33:aa:bb:cc handle 1 state 1 lm MASTER AUTH ENCRYPT\n", out));
	CHECK_STR(out, "11:22:33:AA:BB:CC");
	CHECK(Bluetooth_parseConnected("Device AA:BB:CC:DD:EE:FF 8BitDo Pro 2\nDevice 11:22:33:44:55:66 Buds\n", out));
	CHECK_STR(out, "AA:BB:CC:DD:EE:FF");
	CHECK(Bluetooth_parseConnected("Device 11:22:33:44:55:66", out));
	CHECK_STR(out, "11:22:33:44:55:66");
	CHECK(!Bluetooth_parseConnected("Connections:\n", out));
	CHECK_STR(out, "");
	CHECK(!Bluetooth_parseConnected("", out));
	CHECK(!Bluetooth_parseConnected(NULL, out));
	CHECK(!Bluetooth_parseConnected("Device AA:BB:CC:DD:EE:FFX Pad\n", out));
	CHECK(!Bluetooth_parseConnected("NoDevice AA:BB:CC:DD:EE:FF Pad\n", out));
	CHECK(!Bluetooth_parseConnected("Device AA:BB:CC:DD:EE Pad\n", out));
	CHECK(!Bluetooth_parseConnected("Too many arguments: 2 > 1\n", out));
	CHECK_STR(out, "");
}

static void testRun(void) {
	char out[64];
	char script[512];

	writeScript("say", "#!/bin/sh\necho \"$#:$1\"\nexit 3\n");
	pathTo(script, sizeof(script), "say");
	char* say[] = {script, "two words; reboot", NULL};
	CHECK(Bluetooth_run(say, 2000, out, sizeof(out)) == 3);
	CHECK_STR(out, "1:two words; reboot\n");

	// cut to size, and no output wanted
	char small[8];
	CHECK(Bluetooth_run(say, 2000, small, sizeof(small)) == 3);
	CHECK_STR(small, "1:two w");
	CHECK(Bluetooth_run(say, 2000, NULL, 0) == 3);

	char* missing[] = {"mosaic-no-such-program", NULL};
	CHECK(Bluetooth_run(missing, 2000, out, sizeof(out)) == 127);

	// past the limit: SIGTERM takes it and its child, and both are reaped
	writeScript("hang", "#!/bin/sh\necho $$ > \"$FAKE_DIR/hang.pid\"\n/bin/sleep 30 &\necho $! > \"$FAKE_DIR/hang-sleep.pid\"\nwait\n");
	removeFile("hang.pid");
	removeFile("hang-sleep.pid");
	pathTo(script, sizeof(script), "hang");
	char* hang[] = {script, NULL};
	long start = nowMs();
	CHECK(Bluetooth_run(hang, 300, out, sizeof(out)) == -2);
	long took = nowMs() - start;
	CHECK(took >= 300 && took < 1500);
	CHECK(gone(readPid("hang.pid")));
	CHECK(gone(readPid("hang-sleep.pid")));

	// one that ignores SIGTERM gets SIGKILL after the grace
	writeScript("stubborn", "#!/bin/sh\ntrap '' TERM\necho $$ > \"$FAKE_DIR/stubborn.pid\"\n/bin/sleep 30 &\necho $! > \"$FAKE_DIR/stubborn-sleep.pid\"\nwait\n");
	removeFile("stubborn.pid");
	removeFile("stubborn-sleep.pid");
	pathTo(script, sizeof(script), "stubborn");
	char* stubborn[] = {script, NULL};
	start = nowMs();
	CHECK(Bluetooth_run(stubborn, 300, out, sizeof(out)) == -2);
	took = nowMs() - start;
	CHECK(took >= 750 && took < 2500);
	CHECK(gone(readPid("stubborn.pid")));
	CHECK(gone(readPid("stubborn-sleep.pid")));

	// exiting on its own still takes what it left running
	writeScript("leave", "#!/bin/sh\n/bin/sleep 30 &\necho $! > \"$FAKE_DIR/leave-sleep.pid\"\necho left\n");
	removeFile("leave-sleep.pid");
	pathTo(script, sizeof(script), "leave");
	char* leave[] = {script, NULL};
	start = nowMs();
	CHECK(Bluetooth_run(leave, 5000, out, sizeof(out)) == 0);
	CHECK(nowMs() - start < 2000);
	CHECK_STR(out, "left\n");
	CHECK(gone(readPid("leave-sleep.pid")));
}

static const char* BLUETOOTHCTL =
	"#!/bin/sh\n"
	"printf '%s|' \"$@\" >> \"$FAKE_DIR/calls\"\n"
	"echo >> \"$FAKE_DIR/calls\"\n"
	"case \"$1\" in\n"
	"connect)\n"
	"\techo $$ > \"$FAKE_DIR/ctl.pid\"\n"
	"\t/bin/sleep \"$FAKE_SLEEP\" &\n"
	"\techo $! > \"$FAKE_DIR/ctl-sleep.pid\"\n"
	"\twait\n"
	"\t;;\n"
	"devices) /bin/cat \"$FAKE_DIR/devices.out\" ;;\n"
	"esac\n";

static const char* HCITOOL =
	"#!/bin/sh\n"
	"echo $$ >> \"$FAKE_DIR/hcitool.calls\"\n"
	"/bin/cat \"$FAKE_DIR/hcitool.out\"\n";

static void testRefresh(void) {
	char out[BLUETOOTH_ADDRESS_SIZE];
	writeScript("hcitool", HCITOOL);
	writeScript("bluetoothctl", BLUETOOTHCTL);
	writeFile("hcitool.out", "Connections:\n\t> ACL 00:1a:7d:da:71:0f handle 11 state 1 lm MASTER\n");
	writeFile("devices.out", "Device 11:22:33:44:55:66 Buds\n");
	removeFile("hcitool.calls");
	removeFile("calls");
	logged[0] = '\0';

	// off: nothing, and no worker
	Bluetooth_setEnabled(0);
	CHECK(connected(out) == 0);
	CHECK_STR(out, "");
	CHECK(Bluetooth_connectedAddress(NULL, 0) == 0);
	usleep(300000);
	CHECK(strstr(logged, "worker started") == NULL);
	CHECK(countLines("hcitool.calls") == 0);

	// on: refreshed off the caller's thread
	Bluetooth_setEnabled(1);
	CHECK(addressBecomes("00:1A:7D:DA:71:0F", 2000));
	CHECK(strstr(logged, "worker started") != NULL);
	char tiny[8] = "x";
	CHECK(Bluetooth_connectedAddress(tiny, sizeof(tiny)) == 0);
	CHECK_STR(tiny, "");

	// disconnected
	writeFile("hcitool.out", "Connections:\n");
	CHECK(addressBecomes("", 2000));

	// turning it off clears it at once and stops the refreshes
	writeFile("hcitool.out", "Connections:\n\t> ACL 00:1a:7d:da:71:0f handle 11 state 1 lm MASTER\n");
	CHECK(addressBecomes("00:1A:7D:DA:71:0F", 2000));
	Bluetooth_setEnabled(0);
	CHECK(connected(out) == 0);
	usleep(150000);
	int calls = countLines("hcitool.calls");
	usleep(400000);
	CHECK(countLines("hcitool.calls") == calls);
	Bluetooth_setEnabled(1);
	CHECK(addressBecomes("00:1A:7D:DA:71:0F", 2000));

	// asleep: no refreshes until it resumes
	Bluetooth_pause(1);
	usleep(150000);
	calls = countLines("hcitool.calls");
	usleep(400000);
	CHECK(countLines("hcitool.calls") == calls);
	Bluetooth_pause(0);
	usleep(300000);
	CHECK(countLines("hcitool.calls") > calls);

	// without hcitool, bluetoothctl's own list
	removeFile("hcitool");
	CHECK(addressBecomes("11:22:33:44:55:66", 2000));
	CHECK(strstr(readFile("calls"), "devices|Connected|\n") != NULL);
	writeScript("hcitool", HCITOOL);
}

static void testConnect(void) {
	char out[BLUETOOTH_ADDRESS_SIZE];
	writeFile("hcitool.out", "Connections:\n");
	removeFile("calls");
	removeFile("ctl.pid");
	removeFile("ctl-sleep.pid");
	logged[0] = '\0';

	// refusals
	Bluetooth_setEnabled(1);
	CHECK(Bluetooth_connect(NULL) == BLUETOOTH_BAD_ADDRESS);
	CHECK(Bluetooth_connect("AA:BB:CC:DD:EE:FF; reboot") == BLUETOOTH_BAD_ADDRESS);
	CHECK(Bluetooth_connect("AA:BB:CC:DD:EE") == BLUETOOTH_BAD_ADDRESS);
	Bluetooth_setEnabled(0);
	CHECK(Bluetooth_connect("AA:BB:CC:DD:EE:FF") == BLUETOOTH_OFF);
	usleep(200000);
	CHECK(strstr(readFile("calls"), "connect") == NULL);
	CHECK(!Bluetooth_connecting());

	// a connect that finishes, then the address it brought
	Bluetooth_setEnabled(1);
	setenv("FAKE_SLEEP", "0", 1);
	CHECK(Bluetooth_connect("aa:bb:cc:dd:ee:ff") == 0);
	CHECK(connectEnds(2000));
	CHECK(strstr(readFile("calls"), "connect|AA:BB:CC:DD:EE:FF|\n") != NULL);
	CHECK(strstr(logged, "bluetoothctl connect AA:BB:CC:DD:EE:FF exited 0") != NULL);
	writeFile("hcitool.out", "Connections:\n\t> ACL AA:BB:CC:DD:EE:FF handle 11 state 1 lm MASTER\n");
	CHECK(addressBecomes("AA:BB:CC:DD:EE:FF", 2000));
	writeFile("hcitool.out", "Connections:\n");
	CHECK(addressBecomes("", 2000));

	// one at a time, and stopped at the limit with its child
	removeFile("ctl.pid");
	removeFile("ctl-sleep.pid");
	setenv("FAKE_SLEEP", "30", 1);
	long start = nowMs();
	CHECK(Bluetooth_connect("11:22:33:44:55:66") == 0);
	CHECK(Bluetooth_connecting());
	CHECK(Bluetooth_connect("11:22:33:44:55:66") == BLUETOOTH_BUSY);
	CHECK(Bluetooth_connect("AA:BB:CC:DD:EE:FF") == BLUETOOTH_BUSY);
	pid_t ctl = readPid("ctl.pid");
	pid_t child = readPid("ctl-sleep.pid");
	// the address still answers from the caller's side
	long asked = nowMs();
	connected(out);
	CHECK(nowMs() - asked < 50);
	CHECK(connectEnds(3000));
	long took = nowMs() - start;
	CHECK(took >= 300 && took < 2000);
	CHECK(gone(ctl));
	CHECK(gone(child));
	CHECK(strstr(logged, "connecting 11:22:33:44:55:66 stopped after 300ms") != NULL);
	CHECK(countLines("calls") >= 2);
	CHECK(Bluetooth_connect("11:22:33:44:55:66") == 0);
	CHECK(connectEnds(3000));
}

static void testQuit(void) {
	removeFile("ctl.pid");
	removeFile("ctl-sleep.pid");
	logged[0] = '\0';

	// quitting mid-connect stops it before returning
	Bluetooth_setLimits(100, 1000, 10000);
	setenv("FAKE_SLEEP", "30", 1);
	Bluetooth_setEnabled(1);
	CHECK(Bluetooth_connect("AA:BB:CC:DD:EE:FF") == 0);
	pid_t ctl = readPid("ctl.pid");
	pid_t child = readPid("ctl-sleep.pid");
	CHECK(ctl > 0 && child > 0);
	long start = nowMs();
	Bluetooth_quit();
	CHECK(nowMs() - start < 1500);
	CHECK(kill(ctl, 0) != 0 && errno == ESRCH);
	CHECK(gone(child));
	CHECK(!Bluetooth_connecting());
	CHECK(strstr(logged, "worker stopped") != NULL);

	// again does nothing
	Bluetooth_quit();

	// and it starts again on the next ask
	setenv("FAKE_SLEEP", "0", 1);
	writeFile("hcitool.out", "Connections:\n\t> ACL 00:1a:7d:da:71:0f handle 11 state 1 lm MASTER\n");
	CHECK(addressBecomes("00:1A:7D:DA:71:0F", 2000));
	Bluetooth_quit();

	// quitting while it's between refreshes is quick too
	Bluetooth_setLimits(60000, 1000, 10000);
	CHECK(addressBecomes("00:1A:7D:DA:71:0F", 2000));
	start = nowMs();
	Bluetooth_quit();
	CHECK(nowMs() - start < 500);
}

int main(void) {
	mkdir(TEST_DIR, 0755);
	setenv("FAKE_DIR", TEST_DIR, 1);
	// the fakes only, so nothing real runs
	setenv("PATH", TEST_DIR, 1);
	Bluetooth_setLog(capture);
	Bluetooth_setLimits(100, 1000, 300);

	testAddresses();
	testParse();
	testRun();
	testRefresh();
	testConnect();
	testQuit();
	CHECK_DONE();
}
