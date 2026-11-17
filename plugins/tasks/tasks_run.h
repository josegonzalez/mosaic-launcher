#ifndef TASKS_RUN_H
#define TASKS_RUN_H

// Running one task: /bin/sh with the script's path, and the game's as $1
// for a game task, as arguments, never through a shell command line. It
// starts in a process group of its own, with stdin from /dev/null and its
// stdout and stderr in its log file, which a run starts afresh, and none of
// the launcher's other open files. Stopping it stops the whole group.

#include <stddef.h>
#include <sys/types.h>

#define TK_STOP_GRACE_MS 2000 // from SIGTERM to SIGKILL

// How a run ended, for TKRun's how.
enum TKHow {
	TK_RUNNING,
	TK_EXITED,   // exited on its own, with code
	TK_SIGNALED, // killed by a signal it wasn't sent by TK_stop, in code
	TK_STOPPED,  // stopped with TK_stop
};

typedef struct TKRun {
	pid_t pid;               // the script's, which is its group's id; 0 when none runs
	int how;                 // enum TKHow
	int code;                // the exit code, or the signal for TK_SIGNALED
	int stopping;            // TK_stop sent SIGTERM
	int killed;              // SIGKILL sent
	long long stop_at;       // when TK_stop did, in TK_nowMs's time
	long long started;       // when it started, in TK_nowMs's time
	long output_start;       // where its output starts in the log, after the header
	long output_end;         // and where it ends, once it has, before the line saying how
	char log_path[1024];
} TKRun;

// Milliseconds on a clock that only goes forward.
long long TK_nowMs(void);

// Starts script with game as $1 (NULL for none) and env, writing header
// and then its output into log_path. Returns 0, or -1 when it couldn't,
// with a reason in error.
int TK_start(TKRun* run, const char* script, const char* game, char* const env[], const char* log_path,
	const char* header, char* error, size_t error_size);

// Checks on a running task without waiting. Once TK_stop's grace has
// passed it sends the group SIGKILL. Returns 1 once it has ended, filling
// how and code, and appending a last line saying how to its log; 0 while
// it runs.
int TK_poll(TKRun* run);

// Asks the task's whole group to stop with SIGTERM; TK_poll follows with
// SIGKILL after TK_STOP_GRACE_MS.
void TK_stop(TKRun* run);

// Kills the task's whole group and waits for the script, as the launcher
// quits.
void TK_kill(TKRun* run);

// The tail of the task's output in its log (up to size - 1 bytes, after
// the header), into out with its length, or 0 when there is none.
size_t TK_outputTail(const TKRun* run, char* out, size_t size);

#endif
