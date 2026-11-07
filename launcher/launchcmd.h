#ifndef LAUNCHCMD_H
#define LAUNCHCMD_H

// A launch command a plugin puts in place of stock's: a program on the card
// and its arguments, written to /tmp/next quoted the way stock quotes its
// own commands.

#include <stddef.h>

#define LAUNCH_ARGS_MAX 8
#define LAUNCH_ARG_SIZE 256
#define LAUNCH_CMD_SIZE 4096

typedef struct LaunchCmd {
	char program[512]; // resolved, so no links or ".." are left in it
	char args[LAUNCH_ARGS_MAX][LAUNCH_ARG_SIZE];
	int count;
} LaunchCmd;

// Checks program is a regular file under sdcard, once links and ".." are
// resolved, and that there are at most LAUNCH_ARGS_MAX args, each shorter
// than LAUNCH_ARG_SIZE and without a newline. Fills cmd and returns 0, or
// returns -1 and leaves it alone.
int LaunchCmd_set(LaunchCmd* cmd, const char* sdcard, const char* program, const char* const* args, int count);

// Writes the command line: the program, its args, then target unless it's
// NULL, each in single quotes with any ' written as '\''. Returns 0, or -1
// when it doesn't fit in size.
int LaunchCmd_format(const LaunchCmd* cmd, const char* target, char* out, size_t size);

#endif
