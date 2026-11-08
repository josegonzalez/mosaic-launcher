#ifndef BT_MODEL_H
#define BT_MODEL_H

// What Bluetooth reconnect decides, apart from the host, so the host tests
// can check it on its own.

#include <stddef.h>

#define BR_ADDRESS_SIZE 18 // "XX:XX:XX:XX:XX:XX" and its terminator

#define BR_FIRST_LOOK_MS 8000    // the launcher looks at once, but each of its two programs may take 3 seconds
#define BR_CONNECT_WAIT_MS 20000 // the launcher stops a connect after 15 seconds, then looks again
#define BR_BACKOFF_MS 10000      // before the second try, doubled before each one after
#define BR_TRIES 3               // connects asked for before giving up

// An address in "XX:XX:XX:XX:XX:XX" form, hex in either case, written to
// out in upper case, as the launcher reports it. Returns 1, or writes ""
// and returns 0 for anything else, spaces around it aside.
int Br_parseAddress(const char* s, char out[BR_ADDRESS_SIZE]);

enum BrState {
	BR_IDLE,        // nothing to do
	BR_LOOKING,     // waiting for the launcher's first look
	BR_CONNECTING,  // a connect is queued; waiting for it to take
	BR_BACKING_OFF, // waiting before the next try
};

// What Br_step asks for, or says has just ended the reconnect.
enum BrEvent {
	BR_NONE,
	BR_CONNECT,   // ask the host to connect, then call Br_connectResult
	BR_CONNECTED, // a device is connected; nothing more to do
	BR_OFF,       // Bluetooth is off; nothing is tried while it is
	BR_GAVE_UP,   // every try is used up
};

typedef struct BrReconnect {
	int state;           // an enum BrState
	int armed;           // the current wait's deadline is set
	unsigned long until; // when the current wait ends
	int tries;           // connects asked for so far
} BrReconnect;

void Br_init(BrReconnect* r);

// Starts reconnecting, eg. at start or after a resume, from the first look
// again. The first Br_step sets the deadline, since the launcher looks
// only once a plugin asks.
void Br_start(BrReconnect* r);

// Stops, eg. before the device sleeps.
void Br_stop(BrReconnect* r);

// One frame: whether Bluetooth is on, and whether a device is connected
// now. Returns an enum BrEvent: BR_CONNECT at most once per try, and one
// of the ending events once, after which it's idle until Br_start.
// Bluetooth being off ends it at once. A device connected during the
// first look counts only at its end, since after a resume the launcher's
// answer is from before the sleep until it looks again.
int Br_step(BrReconnect* r, unsigned long now, int enabled, int connected);

// What the host said to the connect BR_CONNECT asked for: 0 for queued,
// anything else for refused, which uses up the try as a failed one would.
void Br_connectResult(BrReconnect* r, unsigned long now, int result);

#endif
