// A plugin's replacement launch command runs only a file on the card, and
// is quoted the way the launcher quotes its own commands.

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "launchcmd.h"

#define CARD TEST_DIR "/launchcmd-card"

static void writeFile(const char* path) {
	FILE* f = fopen(path, "w");
	if (f) fclose(f);
}

int main(void) {
	system("rm -rf '" CARD "' '" TEST_DIR "/launchcmd-outside'");
	mkdir(CARD, 0755);
	mkdir(CARD "/Emus", 0755);
	mkdir(CARD "/Emus/Other.pak", 0755);
	mkdir(TEST_DIR "/launchcmd-outside", 0755);
	writeFile(CARD "/Emus/Other.pak/launch.sh");
	writeFile(TEST_DIR "/launchcmd-outside/evil.sh");
	symlink(TEST_DIR "/launchcmd-outside/evil.sh", CARD "/Emus/link.sh");

	LaunchCmd cmd;
	char out[LAUNCH_CMD_SIZE];

	// a program on the card, with the game added last
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak/launch.sh", NULL, 0) == 0);
	CHECK(cmd.count == 0);
	CHECK(LaunchCmd_format(&cmd, CARD "/Roms/GB/Game.gb", out, sizeof(out)) == 0);
	char want[1024];
	snprintf(want, sizeof(want), "'%s' '%s'", cmd.program, CARD "/Roms/GB/Game.gb");
	CHECK_STR(out, want);

	// args go between the program and the game, and quotes are escaped as
	// escapeSingleQuotes does; a pak gets nothing added
	const char* args[] = { "core's.so", "--host" };
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/../Emus/Other.pak/launch.sh", args, 2) == 0);
	CHECK(strstr(cmd.program, "..") == NULL);
	CHECK(LaunchCmd_format(&cmd, "/Roms/It's.gb", out, sizeof(out)) == 0);
	snprintf(want, sizeof(want), "'%s' 'core'\\''s.so' '--host' '/Roms/It'\\''s.gb'", cmd.program);
	CHECK_STR(out, want);
	CHECK(LaunchCmd_format(&cmd, NULL, out, sizeof(out)) == 0);
	snprintf(want, sizeof(want), "'%s' 'core'\\''s.so' '--host'", cmd.program);
	CHECK_STR(out, want);

	// refused: off the card, through a link or ".." leading off it, a
	// folder, a missing file, and args that don't fit
	LaunchCmd before = cmd;
	CHECK(LaunchCmd_set(&cmd, CARD, TEST_DIR "/launchcmd-outside/evil.sh", NULL, 0) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/link.sh", NULL, 0) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/../launchcmd-outside/evil.sh", NULL, 0) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak", NULL, 0) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Missing.sh", NULL, 0) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, NULL, NULL, 0) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, "", NULL, 0) == -1);
	const char* newline[] = { "a\nrm -rf /" };
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak/launch.sh", newline, 1) == -1);
	char big[LAUNCH_ARG_SIZE + 1];
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	const char* too_big[] = { big };
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak/launch.sh", too_big, 1) == -1);
	const char* many[LAUNCH_ARGS_MAX + 1] = { "a", "b", "c", "d", "e", "f", "g", "h", "i" };
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak/launch.sh", many, LAUNCH_ARGS_MAX + 1) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak/launch.sh", NULL, 1) == -1);
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak/launch.sh", many, -1) == -1);
	CHECK(memcmp(&before, &cmd, sizeof(cmd)) == 0); // refusals leave it alone
	CHECK(LaunchCmd_set(&cmd, CARD, CARD "/Emus/Other.pak/launch.sh", many, LAUNCH_ARGS_MAX) == 0);

	// a command that doesn't fit is refused, never cut short
	char small[16];
	CHECK(LaunchCmd_format(&cmd, "/Roms/Game.gb", small, sizeof(small)) == -1);
	CHECK_STR(small, "");

	system("rm -rf '" CARD "' '" TEST_DIR "/launchcmd-outside'");
	CHECK_DONE();
}
