// Bug report bundles' model: taking private details out of realistic
// logs and config files, the zip it writes, the settings and game list
// summary that go in, and which reports are kept.

#include <stdlib.h>
#include "check.h"
#include "reportmodel.h"

// text redacted, into a buffer the caller frees
static char* redacted(const Redactor* r, const char* text, int* replaced) {
	ReportBuf out = { 0 };
	int n = Redact(r, text, strlen(text), &out);
	if (replaced) *replaced = n;
	if (!out.data) return strdup("");
	return out.data;
}

#define CHECK_REDACT(r, text, want) do { \
	char* got_ = redacted((r), (text), NULL); \
	CHECK_STR(got_, (want)); \
	free(got_); \
} while (0)

static void testAddresses(void) {
	Redactor r;
	Redactor_init(&r, 0);
	CHECK_REDACT(&r, "[INFO] wifi: connected to 192.168.1.207 on wlan0", "[INFO] wifi: connected to <ip> on wlan0");
	CHECK_REDACT(&r, "romm at 10.0.0.1:8080, retrying", "romm at <ip>:8080, retrying");
	CHECK_REDACT(&r, "last_address=172.16.4.20\n", "last_address=<ip>\n");
	// what says nothing about the network stays
	CHECK_REDACT(&r, "syncthing API on 127.0.0.1:8384", "syncthing API on 127.0.0.1:8384");
	CHECK_REDACT(&r, "listening on 0.0.0.0, mask 255.255.255.0", "listening on 0.0.0.0, mask 255.255.255.0");
	// not addresses
	CHECK_REDACT(&r, "ffmpeg 6.1.2, rclone v1.75.2, build 1.2.3.4.5", "ffmpeg 6.1.2, rclone v1.75.2, build 1.2.3.4.5");
	CHECK_REDACT(&r, "300.1.1.1 and 1.2.3", "300.1.1.1 and 1.2.3");
	CHECK_REDACT(&r, "host-192.168.0.9 up", "host-<ip> up");

	CHECK_REDACT(&r, "inet6 fe80::1c2b:3cff:fe4d:5e6f%wlan0 scope link", "inet6 <ip> scope link");
	CHECK_REDACT(&r, "2001:db8:85a3:0:0:8a2e:370:7334 joined", "<ip> joined");
	CHECK_REDACT(&r, "server [2001:db8::1]:443", "server [<ip>]:443");
	CHECK_REDACT(&r, "mapped ::ffff:192.168.1.5 ok", "mapped <ip> ok");
	CHECK_REDACT(&r, "neighbour fe80::1: reachable", "neighbour <ip>: reachable");
	CHECK_REDACT(&r, "loopback ::1 and any ::", "loopback ::1 and any ::");
	// times, C++ names and plain words aren't addresses
	CHECK_REDACT(&r, "[12:34:56] started at 10:22:33.123", "[12:34:56] started at 10:22:33.123");
	CHECK_REDACT(&r, "std::vector<int> threw in a::b", "std::vector<int> threw in <ip>");
	CHECK_REDACT(&r, "cafe: dead beef", "cafe: dead beef");

	CHECK_REDACT(&r, "plugins: bt connected AA:BB:CC:DD:EE:FF", "plugins: bt connected <mac>");
	CHECK_REDACT(&r, "last=a4:c1:38:0b:22:9f\n", "last=<mac>\n");
	CHECK_REDACT(&r, "ether 00-1A-2B-3C-4D-5E", "ether <mac>");
	CHECK_REDACT(&r, "ether aa:bb:cc:dd:ee:ff:00 stays", "ether aa:bb:cc:dd:ee:ff:00 stays");

	CHECK_REDACT(&r, "<device id=\"ABCDEFG-HIJKLMN-OPQRSTU-VWXYZ23-4567ABC-DEFGHIJ-KLMNOPQ-RSTUVWX\" name=\"Deck\">",
		"<device id=\"<device-id>\" name=\"Deck\">");
	CHECK_REDACT(&r, "peer=ABCDEFG-HIJKLMN-OPQRSTU-VWXYZ23-4567ABC-DEFGHIJ-KLMNOPQ-RSTUVWX\n", "peer=<device-id>\n");
	Redactor_free(&r);
}

static void testWifi(void) {
	Redactor r;
	Redactor_init(&r, 0);
	CHECK_REDACT(&r,
		"ctrl_interface=/var/run/wpa_supplicant\nnetwork={\n\tssid=\"Home Net 5G\"\n\tpsk=\"hunter2hunter2\"\n\tkey_mgmt=WPA-PSK\n}\n",
		"ctrl_interface=/var/run/wpa_supplicant\nnetwork={\n\tssid=\"<wifi>\"\n\tpsk=\"<redacted>\"\n\tkey_mgmt=WPA-PSK\n}\n");
	CHECK_REDACT(&r, "ssid=Cafe Guest\nip=192.168.4.2\n", "ssid=<wifi>\nip=<ip>\n");
	CHECK_REDACT(&r, "[INFO] plugin system-info: values: Battery: 80%; Wi-Fi: Home Net 5G, 192.168.1.20, -48 dBm; IP: wlan0 192.168.1.20",
		"[INFO] plugin system-info: values: Battery: 80%; Wi-Fi: <wifi>, <ip>, -48 dBm; IP: wlan0 <ip>");
	CHECK_REDACT(&r, "values: Wi-Fi: not connected; IP: unavailable", "values: Wi-Fi: not connected; IP: unavailable");
	CHECK_REDACT(&r, "{\"ssid\": \"Pretty Fly\", \"rssi\": -50}", "{\"ssid\": \"<wifi>\", \"rssi\": -50}");
	// the network's name, wherever it shows
	CHECK(Redactor_add(&r, "Home Net 5G", 1) == 1);
	CHECK_REDACT(&r, "joined Home Net 5G ok", "joined <wifi> ok");
	CHECK_REDACT(&r, "joined Home Net 5Ghz ok", "joined Home Net 5Ghz ok");
	Redactor_free(&r);
}

static void testSecrets(void) {
	Redactor r;
	Redactor_init(&r, 0);
	// RomM: a server URL with a user and password, its token, a cookie
	CHECK_REDACT(&r, "url=http://admin:s3cr3t@192.168.1.5:8080/api\n", "url=http://<credentials>@<ip>:8080/api\n");
	CHECK_REDACT(&r, "[INFO] plugin romm: GET https://bob@romm.example.com/api/roms?token=abc123&page=2 200",
		"[INFO] plugin romm: GET https://<credentials>@romm.example.com/api/roms?token=<redacted>&page=2 200");
	CHECK_REDACT(&r, "Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.e30.sig\n", "Authorization: Bearer <redacted>\n");
	CHECK_REDACT(&r, "sent Bearer eyJhbGciOiJIUzI1NiJ9.e30.sig to the server", "sent Bearer <redacted> to the server");
	CHECK_REDACT(&r, "got Set-Cookie: session=abc; romm_session=xyz", "got Set-Cookie: <redacted>; romm_session=xyz");
	// a key at the start of a line, as in a config file, takes the rest of the line
	CHECK_REDACT(&r, "Set-Cookie: session=abc; romm_session=xyz\nnext", "Set-Cookie: <redacted>\nnext");
	CHECK_REDACT(&r, "{\"access_token\":\"ya29.a0AfH6SMB\",\"token_type\":\"bearer\",\"expires\":3600}",
		"{\"access_token\":\"<redacted>\",\"token_type\":\"<redacted>\",\"expires\":3600}");

	// rclone.conf
	CHECK_REDACT(&r,
		"[gdrive]\ntype = drive\nclient_secret = GOCSPX-abc_def\n"
		"token = {\"access_token\":\"ya29\",\"refresh_token\":\"1//0g\",\"expiry\":\"2026-11-17T10:00:00Z\"}\n"
		"pass = Xn8uOb2sdf\nuser = bob\nhost = nas.local\n",
		"[gdrive]\ntype = drive\nclient_secret = <redacted>\ntoken = <redacted>\npass = <redacted>\nuser = <redacted>\n"
		"host = nas.local\n");

	// Syncthing's config.xml and log
	CHECK_REDACT(&r,
		"<gui enabled=\"true\">\n\t<address>127.0.0.1:8384</address>\n\t<user>admin</user>\n"
		"\t<password>$2a$10$abcdefghijk</password>\n\t<apikey>AbCdEf123GhIj</apikey>\n</gui>\n",
		"<gui enabled=\"true\">\n\t<address>127.0.0.1:8384</address>\n\t<user><redacted></user>\n"
		"\t<password><redacted></password>\n\t<apikey><redacted></apikey>\n</gui>\n");
	CHECK_REDACT(&r, "INFO: request X-API-Key: AbCdEf123GhIj ok", "INFO: request X-API-Key: <redacted> ok");
	CHECK_REDACT(&r, "<address>tcp://192.168.1.9:22000</address>", "<address>tcp://<ip>:22000</address>");

	// a secret setting's value, wherever it shows
	CHECK(Redactor_add(&r, "hunter2hunter2", 0) == 1);
	CHECK(Redactor_add(&r, "hunter2hunter2", 0) == 0);
	CHECK(Redactor_add(&r, "abc", 0) == 0);
	CHECK(Redactor_add(&r, "", 0) == 0);
	CHECK(Redactor_add(&r, NULL, 0) == 0);
	int n = 0;
	char* got = redacted(&r, "login failed for hunter2hunter2 (hunter2hunter2x)", &n);
	CHECK_STR(got, "login failed for <redacted> (hunter2hunter2x)");
	CHECK(n == 1);
	free(got);

	// what isn't private stays
	CHECK_REDACT(&r, "[INFO] plugins: loaded favorites\nmonkey=1\nauthor=Bob\nkey_mgmt=WPA-PSK\nhotkey=L1\n",
		"[INFO] plugins: loaded favorites\nmonkey=1\nauthor=Bob\nkey_mgmt=WPA-PSK\nhotkey=L1\n");
	CHECK_REDACT(&r, "a == b, x=", "a == b, x=");
	CHECK_REDACT(&r, "", "");
	Redactor_free(&r);

	// past the cap, values are counted, not kept
	Redactor_init(&r, 0);
	char value[32];
	for (int i = 0; i < REPORT_LITERALS_MAX; i++) {
		snprintf(value, sizeof(value), "secret-%03d", i);
		CHECK(Redactor_add(&r, value, 0) == 1);
	}
	CHECK(Redactor_add(&r, "one-too-many", 0) == -1);
	CHECK(r.dropped == 1);
	Redactor_free(&r);
}

static void testKeepNetwork(void) {
	Redactor r;
	Redactor_init(&r, 1);
	Redactor_add(&r, "Home Net 5G", 1);
	Redactor_add(&r, "hunter2hunter2", 0);
	CHECK_REDACT(&r, "joined Home Net 5G at 192.168.1.20 via AA:BB:CC:DD:EE:FF, fe80::1%wlan0",
		"joined Home Net 5G at 192.168.1.20 via AA:BB:CC:DD:EE:FF, fe80::1%wlan0");
	CHECK_REDACT(&r, "ssid=\"Home Net 5G\"\npsk=\"hunter2hunter2\"\n", "ssid=\"Home Net 5G\"\npsk=\"<redacted>\"\n");
	CHECK_REDACT(&r, "Wi-Fi: Home Net 5G, 192.168.1.20", "Wi-Fi: Home Net 5G, 192.168.1.20");
	// credentials and tokens go anyway
	CHECK_REDACT(&r, "url=http://admin:s3cr3t@192.168.1.5:8080/?api_key=zz9", "url=http://<credentials>@192.168.1.5:8080/?api_key=<redacted>");
	CHECK_REDACT(&r, "pw hunter2hunter2", "pw <redacted>");
	// a value that's a secret as well as a network's name is always taken out
	Redactor_add(&r, "SharedName", 1);
	Redactor_add(&r, "SharedName", 0);
	CHECK_REDACT(&r, "SharedName", "<redacted>");
	Redactor_free(&r);
}

///////////////////////////////////////
// zip

typedef struct Mem {
	unsigned char data[4096];
	size_t len;
	int fail_at; // a write past this many bytes fails, or 0
} Mem;

static int memWrite(void* ctx, const void* data, size_t len) {
	Mem* m = ctx;
	if ((m->fail_at && m->len + len > (size_t)m->fail_at) || m->len + len > sizeof(m->data)) return -1;
	memcpy(m->data + m->len, data, len);
	m->len += len;
	return 0;
}

static unsigned get16(const unsigned char* p) {
	return p[0] | (unsigned)p[1] << 8;
}

static uint32_t get32(const unsigned char* p) {
	return get16(p) | (uint32_t)get16(p + 2) << 16;
}

static void testZip(void) {
	CHECK(Zip_crc32(0, "hello", 5) == 0x3610a686u);
	CHECK(Zip_crc32(0, "", 0) == 0);
	CHECK(Zip_crc32(Zip_crc32(0, "hel", 3), "lo", 2) == 0x3610a686u);

	Mem m = { { 0 }, 0, 0 };
	ZipWriter z;
	Zip_init(&z, memWrite, &m);
	struct tm when = { 0 };
	when.tm_year = 2026 - 1900;
	when.tm_mon = 10;
	when.tm_mday = 17;
	when.tm_hour = 15;
	when.tm_min = 30;
	when.tm_sec = 12;
	when.tm_isdst = -1;
	time_t t = mktime(&when);
	CHECK(Zip_add(&z, "mosaic-x/README.txt", "hello", 5, t) == 0);
	CHECK(Zip_add(&z, "mosaic-x/logs/empty.txt", "", 0, t) == 0);
	CHECK(Zip_finish(&z) == 0);
	Zip_free(&z);

	// the local header of the first
	CHECK(get32(m.data) == 0x04034b50u);
	CHECK(get16(m.data + 8) == 0);
	CHECK(get16(m.data + 10) == ((15 << 11) | (30 << 5) | 6));
	CHECK(get16(m.data + 12) == (((2026 - 1980) << 9) | (11 << 5) | 17));
	CHECK(get32(m.data + 14) == 0x3610a686u);
	CHECK(get32(m.data + 18) == 5 && get32(m.data + 22) == 5);
	CHECK(get16(m.data + 26) == strlen("mosaic-x/README.txt"));
	CHECK(!memcmp(m.data + 30, "mosaic-x/README.txt", 19));
	CHECK(!memcmp(m.data + 49, "hello", 5));
	// the end record and the central directory it points to
	const unsigned char* end = m.data + m.len - 22;
	CHECK(get32(end) == 0x06054b50u);
	CHECK(get16(end + 8) == 2 && get16(end + 10) == 2);
	uint32_t cd = get32(end + 16), cd_size = get32(end + 12);
	CHECK(cd + cd_size == m.len - 22);
	CHECK(get32(m.data + cd) == 0x02014b50u);
	CHECK(get32(m.data + cd + 42) == 0);
	const unsigned char* second = m.data + cd + 46 + 19;
	CHECK(get32(second) == 0x02014b50u);
	CHECK(get32(second + 42) == 30 + 19 + 5);
	CHECK(get16(second + 28) == strlen("mosaic-x/logs/empty.txt"));

	// a failed write fails the rest
	Mem small = { { 0 }, 0, 40 };
	Zip_init(&z, memWrite, &small);
	CHECK(Zip_add(&z, "a.txt", "hello", 5, t) == 0);
	CHECK(Zip_add(&z, "b.txt", "hello", 5, t) == -1);
	CHECK(Zip_finish(&z) == -1);
	Zip_free(&z);

	// before 1980, as on a device whose clock was never set
	Mem old = { { 0 }, 0, 0 };
	Zip_init(&z, memWrite, &old);
	CHECK(Zip_add(&z, "a.txt", "x", 1, 0) == 0);
	CHECK(get16(old.data + 12) == ((1 << 5) | 1));
	CHECK(Zip_add(&z, "", "x", 1, 0) == -1);
	Zip_free(&z);
}

///////////////////////////////////////
// what goes in

static void testSettings(void) {
	PluginManifest m;
	memset(&m, 0, sizeof(m));
	CHECK(Manifest_parse("id=romm\nname=RomM\napi=1\n"
		"setting.url=text|Server|||protected\n"
		"setting.password=text|Password or API token|||protected,secret\n"
		"setting.limit=choice|Limit|off|off,15|protected\n"
		"setting.button=choice|Button|none|none,Y\n"
		"setting.note=text|Note|\n", &m) == 0);
	char* keys[] = { "button", "url", "password", "note", "last_sync", "limit" };
	char* values[] = { "Y", "http://192.168.1.5:8080", "hunter2hunter2", "my notes", "2026-11-17", "on" };
	ReportBuf out = { 0 };
	Report_settings(&m, keys, values, 6, &out);
	CHECK_STR(out.data, "# romm\nbutton=Y\nnote=my notes\n"
		"# left out, protected or secret in plugin.txt: url, password, limit\n"
		"# left out: 1 key plugin.txt doesn't list\n");
	ReportBuf_free(&out);

	Redactor r;
	Redactor_init(&r, 0);
	// the text ones; a protected choice's value is one of its options
	CHECK(Report_secrets(&m, keys, values, 6, &r) == 2);
	CHECK_REDACT(&r, "connecting to http://192.168.1.5:8080 with hunter2hunter2 on",
		"connecting to <redacted> with <redacted> on");
	Redactor_free(&r);

	// nothing set
	Report_settings(&m, NULL, NULL, 0, &out);
	CHECK_STR(out.data, "# romm\n");
	ReportBuf_free(&out);
	Manifest_free(&m);
}

static void testGames(void) {
	ReportBuf out = { 0 };
	Report_games(NULL, &out);
	CHECK_STR(out.data, "The card index hasn't been read yet, so there's no game list summary.\n");
	ReportBuf_free(&out);

	IndexEntry gb[] = {
		{ .kind = INDEX_KIND_SYSTEM, .depth = -1 },
		{ .kind = INDEX_KIND_GAME },
		{ .kind = INDEX_KIND_FOLDER },
		{ .kind = INDEX_KIND_GAME, .depth = 1 },
		{ .kind = INDEX_KIND_GAME, .added = 1 },
	};
	IndexEntry ps[] = {
		{ .kind = INDEX_KIND_SYSTEM, .depth = -1 },
		{ .kind = INDEX_KIND_GAME },
	};
	IndexSystem systems[3];
	memset(systems, 0, sizeof(systems));
	systems[0].folder = "Game Boy (GB)";
	systems[0].emulator = 1;
	systems[0].entries = gb;
	systems[0].count = 5;
	systems[1].folder = "Sony PlayStation (PS)";
	systems[1].emulator = 1;
	systems[1].entries = ps;
	systems[1].count = 2;
	systems[2].folder = "Empty (XX)";
	systems[2].entries = gb;
	systems[2].count = 1;
	CardIndex index;
	memset(&index, 0, sizeof(index));
	index.systems = systems;
	index.system_count = 3;
	Report_games(&index, &out);
	CHECK_STR(out.data, "Game Boy (GB): 2 games\nSony PlayStation (PS): 1 game\nEmpty (XX): 0 games (no emulator)\n"
		"total: 3 games in 3 systems\n");
	ReportBuf_free(&out);
}

static void testLogsAndNames(void) {
	// read from partway into a line: from the next one
	const char* log = "rst line\nsecond line\nthird\n";
	CHECK(Report_lineStart(log, strlen(log)) == strlen("rst line\n"));
	// one long line is kept from where it was cut
	CHECK(Report_lineStart("abcdefgh", 8) == 0);
	CHECK(Report_lineStart("", 0) == 0);

	struct tm when = { 0 };
	when.tm_year = 2026 - 1900;
	when.tm_mon = 10;
	when.tm_mday = 7;
	when.tm_hour = 5;
	when.tm_min = 3;
	when.tm_sec = 9;
	char name[64];
	Report_name(&when, name, sizeof(name));
	CHECK_STR(name, "mosaic-2026-11-07-050309");

	CHECK(Report_isReport("mosaic-2026-11-07-050309.zip"));
	CHECK(Report_isReport("mosaic-2026-11-07-050309-2.zip"));
	CHECK(!Report_isReport("mosaic-.zip"));
	CHECK(!Report_isReport("notes.zip"));
	CHECK(!Report_isReport("mosaic-2026.txt"));
	CHECK(!Report_isReport(".mosaic-2026.zip"));

	char* names[] = {
		"mosaic-2026-11-07-050309.zip", "notes.txt", "mosaic-2026-11-01-000000.zip", "mosaic-2026-11-07-050309-2.zip",
		"other.zip", "mosaic-2026-11-09-120000.zip",
	};
	int drop[6];
	CHECK(Report_prune(names, 6, 2, drop) == 2);
	CHECK(drop[2] == 1 && drop[0] == 1);
	CHECK(drop[1] == 0 && drop[3] == 0 && drop[4] == 0 && drop[5] == 0);
	CHECK(Report_prune(names, 6, 10, drop) == 0);
	CHECK(Report_prune(names, 6, 0, drop) == 4);
	CHECK(drop[1] == 0 && drop[4] == 0);
}

static void testBuf(void) {
	ReportBuf b = { 0 };
	char big[600];
	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	CHECK(ReportBuf_printf(&b, "%s-%i", big, 7) == 0);
	CHECK(b.len == sizeof(big) + 1);
	CHECK(b.data[b.len - 1] == '7' && b.data[b.len] == '\0');
	CHECK(ReportBuf_add(&b, "ab", 2) == 0);
	CHECK(b.len == sizeof(big) + 3);
	ReportBuf_free(&b);
	CHECK(b.data == NULL && b.len == 0);
}

int main(void) {
	testAddresses();
	testWifi();
	testSecrets();
	testKeepNetwork();
	testZip();
	testSettings();
	testGames();
	testLogsAndNames();
	testBuf();
	CHECK_DONE();
}
