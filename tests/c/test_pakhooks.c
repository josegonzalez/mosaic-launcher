// Paks' pre-launch.sh scripts: what they see, and when they cancel.

#include <stdlib.h>
#include <sys/stat.h>
#include "check.h"
#include "launcher.h"

// launch.c would pull in the whole launcher; the hooks only need this.
char* escapeSingleQuotes(char* str) { return str; }

static void writeScript(const char* path, const char* body) {
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "mkdir -p \"$(dirname '%s')\"", path);
	CHECK(system(cmd) == 0);
	FILE* f = fopen(path, "w");
	CHECK(f != NULL);
	if (!f) return;
	fprintf(f, "#!/bin/sh\n%s\n", body);
	fclose(f);
}

static void readText(const char* path, char* out, size_t size) {
	out[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return;
	size_t got = fread(out, 1, size - 1, f);
	out[got] = '\0';
	fclose(f);
}

#define SEEN SDCARD_PATH "/seen.txt"

int main(void) {
	CHECK(system("rm -rf '" SDCARD_PATH "' && mkdir -p '" SDCARD_PATH "'") == 0);

	// no hooks: nothing to run, launches go ahead
	PakHooks_init();
	CHECK(PakHooks_preLaunch(ROMS_PATH "/GB/Tetris.gb", ENTRY_ROM) == 1);

	writeScript(TOOLS_PATH "/A Recorder.pak/pre-launch.sh",
		"echo \"$HOOK_PHASE|$HOOK_TYPE|$HOOK_ROM_PATH|$HOOK_PAK_PATH\" >> '" SEEN "'");
	writeScript(TOOLS_PATH "/B Broken.pak/pre-launch.sh", "exit 3");
	writeScript(TOOLS_PATH "/.hidden.pak/pre-launch.sh", "echo hidden >> '" SEEN "'");
	writeScript(TOOLS_PATH "/Not A Pak/pre-launch.sh", "echo not-a-pak >> '" SEEN "'");
	PakHooks_init();

	// every pak's hook runs with the launch described; a failure that isn't
	// a cancel doesn't stop the launch
	CHECK(PakHooks_preLaunch(ROMS_PATH "/GB/Tetris.gb", ENTRY_ROM) == 1);
	CHECK(PakHooks_preLaunch(TOOLS_PATH "/Clock.pak", ENTRY_PAK) == 1);
	char seen[1024];
	readText(SEEN, seen, sizeof(seen));
	CHECK_STR(seen,
		"pre|rom|" ROMS_PATH "/GB/Tetris.gb|\n"
		"pre|pak||" TOOLS_PATH "/Clock.pak\n");

	// the environment is cleaned up afterwards
	CHECK(getenv("HOOK_TYPE") == NULL);

	// exiting 1 cancels, and later hooks don't run
	writeScript(TOOLS_PATH "/0 Gate.pak/pre-launch.sh", "exit 1");
	PakHooks_init();
	remove(SEEN);
	CHECK(PakHooks_preLaunch(ROMS_PATH "/GB/Tetris.gb", ENTRY_ROM) == 0);
	readText(SEEN, seen, sizeof(seen));
	CHECK_STR(seen, "");

	CHECK_DONE();
}
