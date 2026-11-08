// The Bluetooth reconnect plugin's model, and the plugin against a fake host.

#include <stdlib.h>
#include "check.h"
#include "../../plugins/bt-reconnect/bt_reconnect.c"

///////////////////////////////////////
// the model

static void testParseAddress(void) {
	char out[BR_ADDRESS_SIZE];
	CHECK(Br_parseAddress("AA:BB:CC:DD:EE:FF", out));
	CHECK_STR(out, "AA:BB:CC:DD:EE:FF");
	CHECK(Br_parseAddress("a0:1b:c2:3d:e4:5f", out));
	CHECK_STR(out, "A0:1B:C2:3D:E4:5F");
	CHECK(Br_parseAddress(" 00:11:22:33:44:55 ", out));
	CHECK_STR(out, "00:11:22:33:44:55");

	strcpy(out, "x");
	CHECK(!Br_parseAddress("", out));
	CHECK_STR(out, "");
	CHECK(!Br_parseAddress(NULL, out));
	CHECK(!Br_parseAddress("AA:BB:CC:DD:EE", out));
	CHECK(!Br_parseAddress("AA:BB:CC:DD:EE:F", out));
	CHECK(!Br_parseAddress("AA:BB:CC:DD:EE:FF0", out));
	CHECK(!Br_parseAddress("AA:BB:CC:DD:EE:FF:00", out));
	CHECK(!Br_parseAddress("AA-BB-CC-DD-EE-FF", out));
	CHECK(!Br_parseAddress("AA:BB:CC:DD:EE:GG", out));
	CHECK(!Br_parseAddress("AABBCCDDEEFF", out));
	CHECK(!Br_parseAddress("AA:BB:CC:DD:EE:FF x", out));
	CHECK_STR(out, "");
}

static void testIdle(void) {
	BrReconnect r;
	Br_init(&r);
	CHECK(Br_step(&r, 0, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, 1000000, 1, 0) == BR_NONE);
	CHECK(r.tries == 0);
}

static void testFirstLook(void) {
	BrReconnect r;
	Br_init(&r);
	Br_start(&r);
	// the wait starts at the first step, not at Br_start
	CHECK(Br_step(&r, 5000, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, 5000 + BR_FIRST_LOOK_MS - 1, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, 5000 + BR_FIRST_LOOK_MS, 1, 0) == BR_CONNECT);
	CHECK(r.tries == 1);

	// a device connected during the look may be from before the sleep, so
	// only the answer at its end counts
	Br_start(&r);
	CHECK(Br_step(&r, 0, 1, 1) == BR_NONE);
	CHECK(Br_step(&r, 1000, 1, 1) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS, 1, 1) == BR_CONNECTED);
	CHECK(r.state == BR_IDLE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + 1, 1, 0) == BR_NONE);

	// it went before the look ended
	Br_start(&r);
	CHECK(Br_step(&r, 0, 1, 1) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS, 1, 0) == BR_CONNECT);
}

static void testOff(void) {
	BrReconnect r;
	Br_init(&r);
	Br_start(&r);
	CHECK(Br_step(&r, 0, 0, 0) == BR_OFF);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS, 1, 0) == BR_NONE);
	CHECK(r.tries == 0);

	// turned off while a connect runs
	Br_start(&r);
	CHECK(Br_step(&r, 0, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS, 1, 0) == BR_CONNECT);
	Br_connectResult(&r, BR_FIRST_LOOK_MS, 0);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + 1000, 0, 0) == BR_OFF);
	CHECK(Br_step(&r, 1000000, 1, 0) == BR_NONE);
}

// Every BR_CONNECT up to the end, at 100ms frames, with nothing connecting.
static int schedule(BrReconnect* r, unsigned long* at, int max, unsigned long* ended, int* how) {
	int n = 0;
	for (unsigned long now = 0; now < 1000000; now += 100) {
		int e = Br_step(r, now, 1, 0);
		if (e == BR_CONNECT) {
			if (n < max) at[n] = now;
			n++;
			Br_connectResult(r, now, 0);
		}
		else if (e != BR_NONE) {
			*ended = now;
			*how = e;
			break;
		}
	}
	return n;
}

static void testRetries(void) {
	BrReconnect r;
	Br_init(&r);
	Br_start(&r);
	unsigned long at[8];
	unsigned long ended = 0;
	int how = BR_NONE;
	CHECK(schedule(&r, at, 8, &ended, &how) == BR_TRIES);
	CHECK(at[0] == BR_FIRST_LOOK_MS);
	CHECK(at[1] == at[0] + BR_CONNECT_WAIT_MS + BR_BACKOFF_MS);
	CHECK(at[2] == at[1] + BR_CONNECT_WAIT_MS + 2 * BR_BACKOFF_MS);
	CHECK(how == BR_GAVE_UP);
	CHECK(ended == at[2] + BR_CONNECT_WAIT_MS);
	CHECK(r.state == BR_IDLE);
	CHECK(Br_step(&r, ended + 1000000, 1, 0) == BR_NONE);
}

static void testConnects(void) {
	BrReconnect r;
	Br_init(&r);
	Br_start(&r);
	CHECK(Br_step(&r, 0, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS, 1, 0) == BR_CONNECT);
	Br_connectResult(&r, BR_FIRST_LOOK_MS, 0);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + 3000, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + 4000, 1, 1) == BR_CONNECTED);
	CHECK(Br_step(&r, 1000000, 1, 0) == BR_NONE);

	// during the back-off
	Br_start(&r);
	CHECK(Br_step(&r, 0, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS, 1, 0) == BR_CONNECT);
	Br_connectResult(&r, BR_FIRST_LOOK_MS, 0);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + BR_CONNECT_WAIT_MS, 1, 0) == BR_NONE);
	CHECK(r.state == BR_BACKING_OFF);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + BR_CONNECT_WAIT_MS + 100, 1, 1) == BR_CONNECTED);
}

static void testRefused(void) {
	BrReconnect r;
	Br_init(&r);
	Br_start(&r);
	CHECK(Br_step(&r, 0, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS, 1, 0) == BR_CONNECT);
	// a refusal uses the try straight away, then backs off as usual
	Br_connectResult(&r, BR_FIRST_LOOK_MS, -1);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + 100, 1, 0) == BR_NONE);
	CHECK(r.state == BR_BACKING_OFF);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + 100 + BR_BACKOFF_MS - 1, 1, 0) == BR_NONE);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS + 100 + BR_BACKOFF_MS, 1, 0) == BR_CONNECT);
	CHECK(r.tries == 2);

	// refused every time: still only BR_TRIES
	Br_start(&r);
	int connects = 0, gave_up = 0;
	for (unsigned long now = 0; now < 1000000; now += 100) {
		int e = Br_step(&r, now, 1, 0);
		if (e == BR_CONNECT) {
			connects++;
			Br_connectResult(&r, now, -1);
		}
		if (e == BR_GAVE_UP) gave_up++;
	}
	CHECK(connects == BR_TRIES);
	CHECK(gave_up == 1);
}

static void testStop(void) {
	BrReconnect r;
	Br_init(&r);
	Br_start(&r);
	CHECK(Br_step(&r, 0, 1, 0) == BR_NONE);
	Br_stop(&r);
	CHECK(Br_step(&r, BR_FIRST_LOOK_MS * 10, 1, 0) == BR_NONE);
	// a result after the stop changes nothing
	Br_connectResult(&r, 0, -1);
	CHECK(r.state == BR_IDLE);
}

///////////////////////////////////////
// the plugin against a fake host

static int supported_now = 1;
static int enabled_now = 1;
static char connected_now[BR_ADDRESS_SIZE];
static int connect_result = 0;
static int connects = 0;
static char connected_to[64];
static int looks = 0;
static int sets = 0;
static int logs = 0;
static char settings[8][2][64];

static void fakeLog(const char* id, const char* m) {
	CHECK(strcmp(id, ID) == 0);
	(void)m;
	logs++;
}
static const char* fakeCfgGet(const char* id, const char* key, const char* fallback) {
	CHECK(strcmp(id, ID) == 0);
	for (int i = 0; i < 8 && settings[i][0][0]; i++) {
		if (!strcmp(settings[i][0], key)) return settings[i][1];
	}
	return fallback;
}
static void setting(const char* key, const char* value) {
	int i = 0;
	while (i < 7 && settings[i][0][0] && strcmp(settings[i][0], key)) i++;
	snprintf(settings[i][0], sizeof(settings[i][0]), "%s", key);
	snprintf(settings[i][1], sizeof(settings[i][1]), "%s", value);
}
static void fakeCfgSet(const char* id, const char* key, const char* value) {
	CHECK(strcmp(id, ID) == 0);
	sets++;
	setting(key, value);
}
static int fakeSupported(void) { return supported_now; }
static int fakeEnabled(void) { return supported_now && enabled_now; }
static int fakeConnectedAddress(char* out, size_t size) {
	looks++;
	out[0] = '\0';
	if (!supported_now || !enabled_now || !connected_now[0] || size < BR_ADDRESS_SIZE) return 0;
	snprintf(out, size, "%s", connected_now);
	return 1;
}
static int fakeConnect(const char* address) {
	connects++;
	snprintf(connected_to, sizeof(connected_to), "%s", address);
	return connect_result;
}

static MosaicHost fake = {
	.size = sizeof(MosaicHost),
	.api_version = MOSAIC_API_VERSION,
	.log = fakeLog,
	.cfg_get = fakeCfgGet,
	.cfg_set = fakeCfgSet,
	.bt_supported = fakeSupported,
	.bt_enabled = fakeEnabled,
	.bt_connected_address = fakeConnectedAddress,
	.bt_connect = fakeConnect,
};

static void reset(void) {
	memset(settings, 0, sizeof(settings));
	supported_now = 1;
	enabled_now = 1;
	connected_now[0] = '\0';
	connect_result = 0;
}

static void start(const MosaicHost* h) {
	connects = 0;
	connected_to[0] = '\0';
	looks = 0;
	sets = 0;
	logs = 0;
	const MosaicPlugin* p = mosaic_plugin_get(h);
	CHECK(p->init(h) == 0);
}

static void ticks(unsigned long* now, unsigned long until) {
	for (; *now <= until; *now += 100) tick(*now);
}

static void testRemember(void) {
	reset();
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 10000);
	CHECK(sets == 0);
	CHECK(connects == 0);
	strcpy(connected_now, "AA:BB:CC:DD:EE:FF");
	ticks(&now, 20000);
	// saved once, not every frame
	CHECK(sets == 1);
	CHECK_STR(fakeCfgGet(ID, "last", ""), "AA:BB:CC:DD:EE:FF");
	strcpy(connected_now, "11:22:33:44:55:66");
	ticks(&now, 30000);
	CHECK(sets == 2);
	CHECK_STR(fakeCfgGet(ID, "last", ""), "11:22:33:44:55:66");
	// gone again: the last one stays
	connected_now[0] = '\0';
	ticks(&now, 40000);
	CHECK(sets == 2);
	CHECK_STR(fakeCfgGet(ID, "last", ""), "11:22:33:44:55:66");
	CHECK(connects == 0);
}

static void testReconnectAtStart(void) {
	reset();
	// typed in by hand, in lower case
	setting("last", "aa:bb:cc:dd:ee:ff");
	start(&fake);
	unsigned long now = 0;
	ticks(&now, BR_FIRST_LOOK_MS - 100);
	CHECK(connects == 0);
	ticks(&now, BR_FIRST_LOOK_MS);
	CHECK(connects == 1);
	CHECK_STR(connected_to, "AA:BB:CC:DD:EE:FF");
	// it took
	ticks(&now, BR_FIRST_LOOK_MS + 5000);
	strcpy(connected_now, "AA:BB:CC:DD:EE:FF");
	ticks(&now, 300000);
	CHECK(connects == 1);
	// the same device in upper case isn't saved again
	CHECK(sets == 0);
	// and it doesn't come back once it goes
	connected_now[0] = '\0';
	ticks(&now, 600000);
	CHECK(connects == 1);
}

static void testAlreadyConnected(void) {
	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	strcpy(connected_now, "11:22:33:44:55:66");
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 300000);
	CHECK(connects == 0);
	// another device is the one to reconnect now
	CHECK_STR(fakeCfgGet(ID, "last", ""), "11:22:33:44:55:66");
}

static void testGivesUp(void) {
	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 1000000);
	CHECK(connects == BR_TRIES);

	// refused by the host: still only BR_TRIES
	connect_result = -1;
	start(&fake);
	now = 0;
	ticks(&now, 1000000);
	CHECK(connects == BR_TRIES);
}

static void testOffNeverTries(void) {
	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	enabled_now = 0;
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 300000);
	afterResume();
	ticks(&now, 600000);
	CHECK(connects == 0);

	// turned off between tries
	enabled_now = 1;
	start(&fake);
	now = 0;
	ticks(&now, BR_FIRST_LOOK_MS + 1000);
	CHECK(connects == 1);
	enabled_now = 0;
	ticks(&now, 100000);
	enabled_now = 1;
	ticks(&now, 1000000);
	CHECK(connects == 1);
}

static void testResume(void) {
	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	strcpy(connected_now, "AA:BB:CC:DD:EE:FF");
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 60000);
	CHECK(connects == 0);

	beforeSleep();
	afterResume();
	// still the answer from before the sleep for a moment
	ticks(&now, now + 2000);
	connected_now[0] = '\0';
	ticks(&now, now + BR_FIRST_LOOK_MS);
	CHECK(connects == 1);

	// asleep in the middle of it: no more tries
	beforeSleep();
	ticks(&now, now + 1000000);
	CHECK(connects == 1);
}

static void testSettings(void) {
	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	setting("on_start", "off");
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 300000);
	CHECK(connects == 0);
	afterResume();
	ticks(&now, 600000);
	CHECK(connects == BR_TRIES);

	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	setting("on_resume", "off");
	start(&fake);
	now = 0;
	ticks(&now, 300000);
	CHECK(connects == BR_TRIES);
	afterResume();
	ticks(&now, 600000);
	CHECK(connects == BR_TRIES);

	// both off: nothing at all, not even remembering
	reset();
	setting("on_start", "off");
	setting("on_resume", "off");
	strcpy(connected_now, "AA:BB:CC:DD:EE:FF");
	start(&fake);
	now = 0;
	ticks(&now, 300000);
	afterResume();
	ticks(&now, 600000);
	CHECK(connects == 0);
	CHECK(looks == 0);
	CHECK(sets == 0);
	CHECK(logs == 1);
}

static void testForgotten(void) {
	// cleared on the Plugins page
	reset();
	setting("last", "");
	start(&fake);
	unsigned long now = 0;
	ticks(&now, 300000);
	CHECK(connects == 0);
	CHECK(logs == 0);

	// not an address
	reset();
	setting("last", "my headphones");
	start(&fake);
	CHECK(logs == 1);
	now = 0;
	ticks(&now, 300000);
	afterResume();
	ticks(&now, 600000);
	CHECK(connects == 0);
	strcpy(connected_now, "AA:BB:CC:DD:EE:FF");
	ticks(&now, 700000);
	CHECK_STR(fakeCfgGet(ID, "last", ""), "AA:BB:CC:DD:EE:FF");
}

static void testUnsupported(void) {
	// desktop: no Bluetooth
	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	supported_now = 0;
	start(&fake);
	CHECK(logs == 1);
	unsigned long now = 0;
	ticks(&now, 300000);
	beforeSleep();
	afterResume();
	ticks(&now, 600000);
	CHECK(connects == 0);
	CHECK(looks == 0);
	CHECK(sets == 0);
	CHECK(logs == 1);

	// an older launcher, without the Bluetooth calls
	reset();
	setting("last", "AA:BB:CC:DD:EE:FF");
	MosaicHost old = fake;
	old.size = offsetof(MosaicHost, bt_supported);
	start(&old);
	CHECK(logs == 1);
	now = 0;
	ticks(&now, 300000);
	afterResume();
	ticks(&now, 600000);
	CHECK(connects == 0);
	CHECK(looks == 0);
}

int main(void) {
	testParseAddress();
	testIdle();
	testFirstLook();
	testOff();
	testRetries();
	testConnects();
	testRefused();
	testStop();
	testRemember();
	testReconnectAtStart();
	testAlreadyConnected();
	testGivesUp();
	testOffNeverTries();
	testResume();
	testSettings();
	testForgotten();
	testUnsupported();
	CHECK_DONE();
}
