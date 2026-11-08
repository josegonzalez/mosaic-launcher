#include <ctype.h>
#include <string.h>
#include "bt_model.h"

///////////////////////////////////////
// Addresses

int Br_parseAddress(const char* s, char out[BR_ADDRESS_SIZE]) {
	out[0] = '\0';
	if (!s) return 0;
	while (isspace((unsigned char)*s)) s++;
	char address[BR_ADDRESS_SIZE];
	for (int i = 0; i < 17; i++) {
		unsigned char c = (unsigned char)s[i];
		if (i % 3 == 2) {
			if (c != ':') return 0;
		}
		else if (!isxdigit(c)) return 0;
		address[i] = (char)toupper(c);
	}
	address[17] = '\0';
	for (const char* rest = s + 17; *rest; rest++) {
		if (!isspace((unsigned char)*rest)) return 0;
	}
	memcpy(out, address, sizeof(address));
	return 1;
}

///////////////////////////////////////
// Reconnecting

void Br_init(BrReconnect* r) {
	*r = (BrReconnect){0};
	r->state = BR_IDLE;
}

void Br_start(BrReconnect* r) {
	Br_init(r);
	r->state = BR_LOOKING;
}

void Br_stop(BrReconnect* r) {
	Br_init(r);
}

static void waitFor(BrReconnect* r, int state, unsigned long until) {
	r->state = state;
	r->until = until;
	r->armed = 1;
}

int Br_step(BrReconnect* r, unsigned long now, int enabled, int connected) {
	if (r->state == BR_IDLE) return BR_NONE;
	if (!r->armed) waitFor(r, r->state, now + BR_FIRST_LOOK_MS);

	if (!enabled) {
		Br_init(r);
		return BR_OFF;
	}
	// Until the first look is over, the address may be the one from before
	// the device slept, so only its answer at the end counts.
	int looking = r->state == BR_LOOKING && now < r->until;
	// whichever device it is, there's nothing to do
	if (connected && !looking) {
		Br_init(r);
		return BR_CONNECTED;
	}
	if (now < r->until) return BR_NONE;

	if (r->state == BR_CONNECTING) {
		// it didn't take
		if (r->tries >= BR_TRIES) {
			Br_init(r);
			return BR_GAVE_UP;
		}
		waitFor(r, BR_BACKING_OFF, now + ((unsigned long)BR_BACKOFF_MS << (r->tries - 1)));
		return BR_NONE;
	}

	// the first look found nothing, or the back-off is over
	r->tries++;
	waitFor(r, BR_CONNECTING, now + BR_CONNECT_WAIT_MS);
	return BR_CONNECT;
}

void Br_connectResult(BrReconnect* r, unsigned long now, int result) {
	if (r->state != BR_CONNECTING || result == 0) return;
	// refused: the next step treats it as a connect that didn't take
	r->until = now;
}
