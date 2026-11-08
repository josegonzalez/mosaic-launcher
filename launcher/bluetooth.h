#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stddef.h>

// Bluetooth for plugins, behind bt_enabled, bt_connected_address and
// bt_connect in mosaic_plugin.h. Nothing here touches stock's BT_* calls,
// which block for seconds and build shell strings from the address.
//
// One worker thread, started by the first Bluetooth_connectedAddress or
// Bluetooth_connect while Bluetooth is on, refreshes the connected device's address every few
// seconds with `hcitool con` (or `bluetoothctl devices Connected` where
// hcitool isn't there) and runs a plugin's connect as `bluetoothctl connect
// <address>`. Each program runs through Bluetooth_run: fork and execvp with
// an argv array, never a shell, and a time limit. Bluetooth_quit stops the
// worker and any program it's running before it returns. Until something
// asks, there's no thread and nothing runs.

#define BLUETOOTH_ADDRESS_SIZE 18 // "XX:XX:XX:XX:XX:XX" and its terminator

// Bluetooth_connect's refusals.
#define BLUETOOTH_BAD_ADDRESS -1
#define BLUETOOTH_OFF -2
#define BLUETOOTH_BUSY -3   // a connect is already queued or running
#define BLUETOOTH_NO_WORKER -4

// 1 when address is six pairs of hex digits joined by colons, either case.
int Bluetooth_validAddress(const char* address);

// The first connected device in the output of `hcitool con` ("ACL
// <address>") or `bluetoothctl devices Connected` ("Device <address>"),
// written to out (BLUETOOTH_ADDRESS_SIZE bytes) in upper case. Returns 1, or
// 0 with out set to "" when there's none.
int Bluetooth_parseConnected(const char* output, char* out);

// Runs argv[0] from PATH with argv, stdin and stderr on /dev/null and
// stdout into out (cut to size, always terminated, NULL to drop it), in a
// process group of its own. Returns the exit status, -1 when it couldn't
// start or was killed by a signal, or -2 when it ran past timeout_ms or the
// worker was stopping: then its group gets SIGTERM, SIGKILL 500ms later if
// it's still there, and it's reaped before this returns. A program that
// exits on its own also has its group swept with SIGKILL before it's
// reaped, so nothing it started outlives it. A program that can't be found
// exits 127.
int Bluetooth_run(char* const argv[], int timeout_ms, char* out, size_t size);

// The user's Bluetooth toggle, from the main thread (every frame, and
// before each plugin call), so the worker never reads stock's settings.
void Bluetooth_setEnabled(int on);

// The device connected now, as of the last refresh, written to out in
// upper case. Returns 1, or 0 with out set to "" when none is, while
// Bluetooth is off, before the first refresh, or when size is under
// BLUETOOTH_ADDRESS_SIZE. Starts the worker while Bluetooth is on.
int Bluetooth_connectedAddress(char* out, size_t size);

// Queues `bluetoothctl connect <address>` on the worker and returns 0, or
// one of the refusals above. Starts the worker.
int Bluetooth_connect(const char* address);

// 1 while a connect is queued or running.
int Bluetooth_connecting(void);

// Holds off refreshes while the device sleeps; 0 refreshes at once.
void Bluetooth_pause(int paused);

// Stops the worker, and anything it's running, and waits for it. Safe to
// call when it never started, and more than once; it also runs at exit.
void Bluetooth_quit(void);

// Where the worker's messages go, eg. LOG_info. NULL drops them.
void Bluetooth_setLog(void (*log)(const char* message));

// The refresh interval and both time limits, in milliseconds, for tests.
void Bluetooth_setLimits(int refresh_ms, int refresh_timeout_ms, int connect_timeout_ms);

#endif
