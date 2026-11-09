// Starts, talks to and stops the plugin's helper program (helper/ builds
// it beside plugin.so). Delete this file, helper.c and helper/ to go
// without one, along with the calls marked "helper" in plugin.c.

#ifndef EXAMPLE_HELPER_H
#define EXAMPLE_HELPER_H

#include <stddef.h>
#include "mosaic_plugin.h"

// Starts program, from the folder plugin.so is in, with its output in
// .userdata/<platform>/logs/<id>.txt. 0 once it runs, -1 when it isn't
// there or can't start.
int helper_start(const MosaicHost* host, const char* id, const char* program);

// Sends line (ending in "\n") and reads the one line back into reply,
// without its "\n". 0, or -1 when there's no helper or it didn't answer in
// timeout_ms.
int helper_ask(const char* line, char* reply, size_t size, int timeout_ms);

// The helper's pid, or -1 when none runs.
int helper_pid(void);

// Closes its socket, which it quits on, and waits for it.
void helper_stop(void);

#endif
