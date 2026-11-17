// The tasks plugin's model and runner: finding tasks in a fixture Tasks
// folder, system tags, last lines, log names and the environment, then
// running real scripts with /bin/sh: arguments passed as they are, output
// in the log, exit codes, and stopping a whole process group.

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "check.h"
#include "../../plugins/tasks/tasks_model.h"
#include "../../plugins/tasks/tasks_run.h"

#define CARD TEST_SDCARD_PATH
#define TASKS CARD "/Tasks"
#define GB CARD "/Roms/Game Boy (GB)"

static void writeText(const char* path, const char* text) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	*slash = '\0';
	char cmd[1200];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
	(void)system(cmd);
	FILE* f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "can't write %s\n", path);
		exit(1);
	}
	fputs(text, f);
	fclose(f);
}

static char* readText(const char* path) {
	static char text[8192];
	text[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return text;
	size_t got = fread(text, 1, sizeof(text) - 1, f);
	text[got] = '\0';
	fclose(f);
	return text;
}

static void makeCard(void) {
	(void)system("rm -rf '" CARD "'");
	writeText(TASKS "/Free space.sh", "echo free\n");
	writeText(TASKS "/b task.sh", "echo b\n");
	writeText(TASKS "/A task.sh", "echo a\n");
	writeText(TASKS "/notes.txt", "not a task\n");
	writeText(TASKS "/.hidden.sh", "echo hidden\n");
	writeText(TASKS "/._Free space.sh", "macOS\n");
	writeText(TASKS "/Folder.sh/inside.sh", "a folder named like a task\n");
	writeText(TASKS "/Game/List saves.sh", "echo saves\n");
	writeText(TASKS "/Game/GB/Only GB.sh", "echo gb\n");
	writeText(TASKS "/Game/GB/List saves.sh", "echo gb saves\n");
	writeText(TASKS "/Game/PS/Only PS.sh", "echo ps\n");
	writeText(GB "/Tetris.gb", "x");
	writeText(GB "/Hacks/Tetris DX.gb", "x");
	writeText(CARD "/Roms/Sony PlayStation (PS)/Two Disc/Two Disc.m3u", "x");
	writeText(CARD "/Roms/Sony PlayStation (PS)/Just a folder/game.bin", "x");
	writeText(CARD "/Roms/No Tag/game.bin", "x");
}

static void testIsTask(void) {
	CHECK(TK_isTask("Free space.sh"));
	CHECK(TK_isTask("a.sh"));
	CHECK(!TK_isTask(".sh"));
	CHECK(!TK_isTask(".hidden.sh"));
	CHECK(!TK_isTask("._Free space.sh"));
	CHECK(!TK_isTask("notes.txt"));
	CHECK(!TK_isTask("script.SH"));
	CHECK(!TK_isTask(NULL));
}

static TKList global, game;

static void testScan(void) {
	TK_scan(TASKS, &global, &game);
	CHECK(global.count == 3);
	if (global.count == 3) {
		CHECK_STR(global.items[0].name, "A task");
		CHECK_STR(global.items[1].name, "b task");
		CHECK_STR(global.items[2].name, "Free space");
		CHECK_STR(global.items[2].path, TASKS "/Free space.sh");
		CHECK_STR(global.items[2].tag, "");
	}
	CHECK(game.count == 4);
	if (game.count == 4) {
		// by name, then the one for every system before the system's own
		CHECK_STR(game.items[0].name, "List saves");
		CHECK_STR(game.items[0].tag, "");
		CHECK_STR(game.items[1].name, "List saves");
		CHECK_STR(game.items[1].tag, "GB");
		CHECK_STR(game.items[1].path, TASKS "/Game/GB/List saves.sh");
		CHECK_STR(game.items[2].name, "Only GB");
		CHECK_STR(game.items[3].name, "Only PS");
		CHECK_STR(game.items[3].tag, "PS");
	}
	// rescanning starts again, and either list can be left out
	TK_scan(TASKS, NULL, &game);
	CHECK(game.count == 4);
	TK_scan(CARD "/missing", &global, &game);
	CHECK(global.count == 0 && game.count == 0);
	CHECK(TK_scanDir(CARD "/missing", "", &global) == 0);
}

static void testSystemTag(void) {
	char tag[TK_TAG_MAX];
	CHECK(TK_systemTag(CARD, GB "/Tetris.gb", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "GB");
	CHECK(TK_systemTag(CARD, GB "/Hacks/Tetris DX.gb", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "GB");
	CHECK(TK_systemTag(CARD, CARD "/Roms/Game Boy (Hacks) (GBH)/x.gb", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "GBH");
	CHECK(TK_systemTag(CARD, CARD "/Roms/No Tag/game.bin", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "");
	CHECK(TK_systemTag(CARD, GB, tag, sizeof(tag)) == 0);
	CHECK_STR(tag, "");
	CHECK(TK_systemTag(CARD, GB "/", tag, sizeof(tag)) == 0);
	CHECK(TK_systemTag(CARD, CARD "/Tasks/Free space.sh", tag, sizeof(tag)) == 0);
	CHECK(TK_systemTag(CARD, "/elsewhere/Roms/Game Boy (GB)/Tetris.gb", tag, sizeof(tag)) == 0);
	CHECK(TK_systemTag(CARD, NULL, tag, sizeof(tag)) == 0);
}

static void testIsGame(void) {
	CHECK(TK_isGame(CARD, GB "/Tetris.gb"));
	CHECK(TK_isGame(CARD, GB "/Hacks/Tetris DX.gb"));
	CHECK(TK_isGame(CARD, CARD "/Roms/Sony PlayStation (PS)/Two Disc"));
	CHECK(!TK_isGame(CARD, CARD "/Roms/Sony PlayStation (PS)/Just a folder"));
	CHECK(!TK_isGame(CARD, GB "/Hacks"));
	CHECK(!TK_isGame(CARD, GB));
	CHECK(!TK_isGame(CARD, GB "/Missing.gb"));
	CHECK(!TK_isGame(CARD, TASKS "/Free space.sh"));
}

static void testAppliesTo(void) {
	TKTask every = { "", "Every", "" };
	TKTask gb = { "", "GB only", "GB" };
	CHECK(TK_appliesTo(&every, "GB"));
	CHECK(TK_appliesTo(&every, ""));
	CHECK(TK_appliesTo(&gb, "GB"));
	CHECK(TK_appliesTo(&gb, "gb"));
	CHECK(!TK_appliesTo(&gb, "GBC"));
	CHECK(!TK_appliesTo(&gb, ""));
	CHECK(!TK_appliesTo(&gb, NULL));
	CHECK(!TK_appliesTo(NULL, "GB"));
}

static void testLastLine(void) {
	char line[16];
	const char* text = "first\nsecond line\n\n  \n";
	TK_lastLine(text, strlen(text), line, sizeof(line));
	CHECK_STR(line, "second line");
	text = "  trimmed\t \r\n";
	TK_lastLine(text, strlen(text), line, sizeof(line));
	CHECK_STR(line, "trimmed");
	text = "no newline";
	TK_lastLine(text, strlen(text), line, sizeof(line));
	CHECK_STR(line, "no newline");
	text = "\n \n\t\n";
	TK_lastLine(text, strlen(text), line, sizeof(line));
	CHECK_STR(line, "");
	TK_lastLine("", 0, line, sizeof(line));
	CHECK_STR(line, "");
	TK_lastLine(NULL, 0, line, sizeof(line));
	CHECK_STR(line, "");
	// cut to fit, never inside a character
	text = "a long line that won't fit\n";
	TK_lastLine(text, strlen(text), line, sizeof(line));
	CHECK_STR(line, "a long line tha");
	text = "aaaaaaaaaaaaaa\xc3\xa9\n"; // 14 a's then an é, which would need bytes 15 and 16
	TK_lastLine(text, strlen(text), line, sizeof(line));
	CHECK_STR(line, "aaaaaaaaaaaaaa");
	// only len bytes
	text = "kept\ncut off";
	TK_lastLine(text, 5, line, sizeof(line));
	CHECK_STR(line, "kept");
}

static void testLogName(void) {
	char name[TK_NAME_MAX];
	TK_logName(TASKS, TASKS "/Free space.sh", name, sizeof(name));
	CHECK_STR(name, "Free space");
	TK_logName(TASKS, TASKS "/Game/List saves.sh", name, sizeof(name));
	CHECK_STR(name, "Game-List saves");
	TK_logName(TASKS, TASKS "/Game/GB/List saves.sh", name, sizeof(name));
	CHECK_STR(name, "Game-GB-List saves");
	TK_logName(TASKS, CARD "/Elsewhere/x.sh", name, sizeof(name));
	CHECK_STR(name, "");
	TK_logName(TASKS, TASKS "-other/x.sh", name, sizeof(name));
	CHECK_STR(name, "");
	TK_logName(TASKS, TASKS "/", name, sizeof(name));
	CHECK_STR(name, "");
	char tiny[4];
	TK_logName(TASKS, TASKS "/Free space.sh", tiny, sizeof(tiny));
	CHECK_STR(tiny, "");
}

static void testEnv(void) {
	char* from[] = {
		(char*)"HOME=/root",
		(char*)"LD_PRELOAD=/lib/inject.so",
		(char*)"DYLD_INSERT_LIBRARIES=/lib/inject.dylib",
		(char*)"SDCARD_PATH=/old",
		(char*)"SDCARD_PATHS=kept",
		(char*)"LD_LIBRARY_PATH=/usr/lib",
		NULL,
	};
	const char* set[] = { "SDCARD_PATH=/mnt/SDCARD", "USERDATA_PATH=/mnt/SDCARD/.userdata/tg5040" };
	char* out[16];
	int n = TK_envBuild(from, set, 2, out, 16);
	CHECK(n == 5);
	if (n == 5) {
		CHECK_STR(out[0], "HOME=/root");
		CHECK_STR(out[1], "SDCARD_PATHS=kept");
		CHECK_STR(out[2], "LD_LIBRARY_PATH=/usr/lib");
		CHECK_STR(out[3], "SDCARD_PATH=/mnt/SDCARD");
		CHECK_STR(out[4], "USERDATA_PATH=/mnt/SDCARD/.userdata/tg5040");
	}
	CHECK(out[n] == NULL);
	// a small out still ends with NULL
	n = TK_envBuild(from, set, 2, out, 2);
	CHECK(n == 1 && out[1] == NULL);
	n = TK_envBuild(NULL, set, 2, out, 16);
	CHECK(n == 2 && out[2] == NULL);
}

///////////////////////////////////////
// running

#define LOG CARD "/logs/task.txt"

static char* env[] = { (char*)"PATH=/usr/bin:/bin", NULL };

// Polls run until it ends, up to seconds; 1 if it did.
static int waitFor(TKRun* run, int seconds) {
	for (int i = 0; i < seconds * 100; i++) {
		if (TK_poll(run)) return 1;
		usleep(10000);
	}
	return 0;
}

static void testRun(void) {
	(void)system("mkdir -p '" CARD "/logs'");
	char error[256];
	TKRun run;

	// the game's path as $1, untouched, quotes and all
	const char* game = GB "/It's a \"game\" $(x) `y`.gb";
	writeText(TASKS "/args.sh", "printf 'args %s\\n' \"$#\"\nprintf '[%s]\\n' \"$1\"\necho to stderr >&2\necho \"$PATH\"\necho last line\nexit 3\n");
	CHECK(TK_start(&run, TASKS "/args.sh", game, env, LOG, "== header\n", error, sizeof(error)) == 0);
	CHECK(run.pid > 0);
	CHECK(waitFor(&run, 5));
	CHECK(run.pid == 0);
	CHECK(run.how == TK_EXITED);
	CHECK(run.code == 3);
	char want[2048];
	snprintf(want, sizeof(want), "== header\nargs 1\n[%s]\nto stderr\n/usr/bin:/bin\nlast line\n== exited with 3 after ", game);
	CHECK(!strncmp(readText(LOG), want, strlen(want)));
	char tail[64];
	size_t len = TK_outputTail(&run, tail, sizeof(tail));
	snprintf(want, sizeof(want), "args 1\n[%s]\nto stderr\n/usr/bin:/bin\nlast line\n", game);
	// the tail of it, without the header or the line saying how it ended
	size_t want_len = strlen(want);
	CHECK(len == sizeof(tail) - 1);
	CHECK(len <= want_len && !strcmp(tail, want + want_len - len));
	char line[64];
	TK_lastLine(tail, len, line, sizeof(line));
	CHECK_STR(line, "last line");
	// once it ended, polling says so again
	CHECK(TK_poll(&run) == 1);

	// no game, no $1; a run starts its log afresh
	writeText(TASKS "/none.sh", "echo \"$#\"\n");
	CHECK(TK_start(&run, TASKS "/none.sh", NULL, env, LOG, NULL, error, sizeof(error)) == 0);
	CHECK(waitFor(&run, 5));
	CHECK(run.how == TK_EXITED && run.code == 0);
	CHECK(!strncmp(readText(LOG), "0\n== exited with 0 after ", 25));
	len = TK_outputTail(&run, tail, sizeof(tail));
	CHECK(len == 2);
	CHECK_STR(tail, "0\n");

	// no output
	writeText(TASKS "/quiet.sh", "exit 0\n");
	CHECK(TK_start(&run, TASKS "/quiet.sh", NULL, env, LOG, "== quiet\n", error, sizeof(error)) == 0);
	CHECK(waitFor(&run, 5));
	CHECK(TK_outputTail(&run, tail, sizeof(tail)) == 0);
	CHECK_STR(tail, "");

	// killed by a signal it sent itself
	writeText(TASKS "/self.sh", "kill -KILL $$\n");
	CHECK(TK_start(&run, TASKS "/self.sh", NULL, env, LOG, NULL, error, sizeof(error)) == 0);
	CHECK(waitFor(&run, 5));
	CHECK(run.how == TK_SIGNALED && run.code == SIGKILL);
	CHECK(strstr(readText(LOG), "== killed by signal 9") != NULL);

	// a log it can't open
	CHECK(TK_start(&run, TASKS "/none.sh", NULL, env, CARD "/missing/dir/log.txt", NULL, error, sizeof(error)) == -1);
	CHECK(strstr(error, "couldn't open its log") != NULL);
	CHECK(run.pid == 0);
}

// 1 while a process with pid is there
static int alive(pid_t pid) {
	return kill(pid, 0) == 0;
}

static pid_t readPid(const char* path) {
	for (int i = 0; i < 500; i++) {
		char* text = readText(path);
		if (text[0] && strchr(text, '\n')) return (pid_t)atoi(text);
		usleep(10000);
	}
	return 0;
}

static void testStop(void) {
	char error[256];
	TKRun run;
	// a script with a child in the background, both of them waiting
	writeText(TASKS "/long.sh", "sleep 30 &\necho $! > '" CARD "/logs/child.pid'\necho started\nwait\n");
	unlink(CARD "/logs/child.pid");
	CHECK(TK_start(&run, TASKS "/long.sh", NULL, env, LOG, NULL, error, sizeof(error)) == 0);
	pid_t child = readPid(CARD "/logs/child.pid");
	CHECK(child > 0);
	CHECK(getpgid(run.pid) == run.pid);
	CHECK(child > 0 && getpgid(child) == run.pid);
	CHECK(TK_poll(&run) == 0);
	TK_stop(&run);
	CHECK(run.stopping);
	CHECK(waitFor(&run, 5));
	CHECK(run.how == TK_STOPPED);
	for (int i = 0; i < 200 && child > 0 && alive(child); i++) usleep(10000);
	CHECK(child > 0 && !alive(child));
	CHECK(strstr(readText(LOG), "== stopped after") != NULL);

	// one that ignores SIGTERM gets SIGKILL after the grace
	writeText(TASKS "/stubborn.sh", "trap '' TERM\necho ready > '" CARD "/logs/child.pid'\nwhile :; do sleep 1; done\n");
	unlink(CARD "/logs/child.pid");
	CHECK(TK_start(&run, TASKS "/stubborn.sh", NULL, env, LOG, NULL, error, sizeof(error)) == 0);
	readPid(CARD "/logs/child.pid");
	long long asked = TK_nowMs();
	TK_stop(&run);
	CHECK(waitFor(&run, 10));
	long long took = TK_nowMs() - asked;
	CHECK(run.how == TK_STOPPED);
	CHECK(run.killed);
	CHECK(took >= TK_STOP_GRACE_MS - 50);

	// killed outright, as the launcher quits
	writeText(TASKS "/quit.sh", "sleep 30 &\necho $! > '" CARD "/logs/child.pid'\nwait\n");
	unlink(CARD "/logs/child.pid");
	CHECK(TK_start(&run, TASKS "/quit.sh", NULL, env, LOG, NULL, error, sizeof(error)) == 0);
	child = readPid(CARD "/logs/child.pid");
	TK_kill(&run);
	CHECK(run.pid == 0 && run.how == TK_STOPPED);
	for (int i = 0; i < 200 && child > 0 && alive(child); i++) usleep(10000);
	CHECK(child > 0 && !alive(child));
	// nothing to stop or kill now
	TK_stop(&run);
	TK_kill(&run);
	CHECK(run.pid == 0);
}

int main(void) {
	makeCard();
	testIsTask();
	testScan();
	testSystemTag();
	testIsGame();
	testAppliesTo();
	testLastLine();
	testLogName();
	testEnv();
	testRun();
	testStop();
	(void)system("rm -rf '" CARD "'");
	CHECK_DONE();
}
