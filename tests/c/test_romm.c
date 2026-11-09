// The romm plugin's model, on responses recorded from RomM 5.4.1 (the
// fixtures in fixtures/romm), and its HTTP client against a small server
// this test runs on 127.0.0.1. With ROMM_TEST_URL (and ROMM_TEST_USER,
// ROMM_TEST_PASSWORD, ROMM_TEST_PIN or ROMM_TEST_CA) set, it also asks a
// real RomM for its platforms, eg. one behind a TLS proxy.

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "check.h"
#include "romm_model.h"
#include "romm_net.h"

static char* readAll(const char* path, size_t* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* buf = malloc((size_t)n + 1);
	if (fread(buf, 1, (size_t)n, f) != (size_t)n) n = 0;
	buf[n] = '\0';
	fclose(f);
	*len = (size_t)n;
	return buf;
}

static void testUrls(void) {
	RMUrl u;
	char error[200];
	CHECK(RM_urlParse("http://192.168.1.20:8080", &u, error, sizeof(error)) == 0);
	CHECK(!u.https && u.port == 8080);
	CHECK_STR(u.host, "192.168.1.20");
	CHECK_STR(u.base, "");
	CHECK(RM_urlParse("https://romm.home/romm/", &u, error, sizeof(error)) == 0);
	CHECK(u.https && u.port == 443);
	CHECK_STR(u.host, "romm.home");
	CHECK_STR(u.base, "/romm");
	CHECK(RM_urlParse("HTTP://[fd00::1]:81/", &u, error, sizeof(error)) == 0);
	CHECK_STR(u.host, "fd00::1");
	CHECK(u.port == 81);
	CHECK(RM_urlParse("  http://host  ", &u, error, sizeof(error)) == 0);
	CHECK_STR(u.host, "host");
	CHECK(RM_urlParse("ftp://host", &u, error, sizeof(error)) != 0);
	CHECK(strstr(error, "http://") != NULL);
	CHECK(RM_urlParse("http://user:pw@host", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://host:0", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://host:65536", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://host:", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://ho_st", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://host/a b", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://host/a?b", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse("http://host/../x", &u, error, sizeof(error)) != 0);
	CHECK(RM_urlParse(NULL, &u, error, sizeof(error)) != 0);

	CHECK(RM_hostIsAddress("10.0.0.1"));
	CHECK(RM_hostIsAddress("fd00::1"));
	CHECK(!RM_hostIsAddress("romm.home"));
	CHECK(!RM_hostIsAddress("1.2.3"));
	CHECK(!RM_hostIsAddress("1.2.3.4a"));

	char host[300];
	RM_urlParse("http://romm.home", &u, NULL, 0);
	RM_hostHeader(&u, host, sizeof(host));
	CHECK_STR(host, "romm.home");
	RM_urlParse("https://[fd00::1]:8443", &u, NULL, 0);
	RM_hostHeader(&u, host, sizeof(host));
	CHECK_STR(host, "[fd00::1]:8443");

	char enc[256];
	RM_urlEncode("Mosaic Test One (World).gb", enc, sizeof(enc));
	CHECK_STR(enc, "Mosaic%20Test%20One%20%28World%29.gb");
	RM_urlEncode("Pok\xc3\xa9mon/&?", enc, sizeof(enc));
	CHECK_STR(enc, "Pok%C3%A9mon%2F%26%3F");
	RM_urlEncode("abcdef", enc, 4);
	CHECK_STR(enc, "abc");

	char target[RM_PATH_MAX];
	RM_urlParse("http://romm.home:8080/romm", &u, NULL, 0);
	CHECK(RM_redirect(&u, "/romm/api/x?y=1#frag", target, sizeof(target)) == 0);
	CHECK_STR(target, "/romm/api/x?y=1");
	CHECK(RM_redirect(&u, "http://ROMM.home:8080/romm/api/z", target, sizeof(target)) == 0);
	CHECK_STR(target, "/romm/api/z");
	CHECK(RM_redirect(&u, "http://other:8080/api", target, sizeof(target)) != 0);
	CHECK(RM_redirect(&u, "http://romm.home:9090/api", target, sizeof(target)) != 0);
	CHECK(RM_redirect(&u, "https://romm.home:8080/api", target, sizeof(target)) != 0);
	CHECK(RM_redirect(&u, "//other/api", target, sizeof(target)) != 0);
	CHECK(RM_redirect(&u, "api/relative", target, sizeof(target)) != 0);
	CHECK(RM_redirect(&u, "/a b", target, sizeof(target)) != 0);
	CHECK(RM_redirect(&u, "", target, sizeof(target)) != 0);
}

static void testRequests(void) {
	char out[2048];
	RM_base64((const unsigned char*)"admin:mosaic", 12, out, sizeof(out));
	CHECK_STR(out, "YWRtaW46bW9zYWlj");
	RM_base64((const unsigned char*)"a", 1, out, sizeof(out));
	CHECK_STR(out, "YQ==");
	RM_base64((const unsigned char*)"ab", 2, out, sizeof(out));
	CHECK_STR(out, "YWI=");

	RM_authHeader("admin", "mosaic", out, sizeof(out));
	CHECK_STR(out, "Authorization: Basic YWRtaW46bW9zYWlj\r\n");
	RM_authHeader("", "rmm_abc", out, sizeof(out));
	CHECK_STR(out, "Authorization: Bearer rmm_abc\r\n");
	RM_authHeader("admin", "", out, sizeof(out));
	CHECK_STR(out, "");
	RM_authHeader("admin", "pw\r\nX-Evil: 1", out, sizeof(out));
	CHECK_STR(out, "");
	RM_authHeader("ad\nmin", "pw", out, sizeof(out));
	CHECK_STR(out, "");

	RMUrl u;
	RM_urlParse("http://romm.home:8080/romm", &u, NULL, 0);
	int n = RM_requestHead("GET", &u, "/api/platforms", "Authorization: Bearer t\r\n", NULL, -1, out, sizeof(out));
	CHECK(n > 0);
	CHECK(strncmp(out, "GET /romm/api/platforms HTTP/1.1\r\nHost: romm.home:8080\r\n", 55) == 0);
	CHECK(strstr(out, "Authorization: Bearer t\r\n") != NULL);
	CHECK(strstr(out, "Connection: close\r\n") != NULL);
	CHECK(strstr(out, "Content-Length") == NULL);
	CHECK(n >= 4 && !strcmp(out + n - 4, "\r\n\r\n"));
	n = RM_requestHead("POST", &u, "/api/saves?rom_id=2", "", "Content-Type: x\r\n", 42, out, sizeof(out));
	CHECK(strstr(out, "Content-Type: x\r\nContent-Length: 42\r\n\r\n") != NULL);
	CHECK(RM_requestHead("GET", &u, "api", "", NULL, -1, out, sizeof(out)) < 0);
	CHECK(RM_requestHead("GET", &u, "/a b", "", NULL, -1, out, sizeof(out)) < 0);
	CHECK(RM_requestHead("GET", &u, "/a\r\nX: y", "", NULL, -1, out, sizeof(out)) < 0);
	CHECK(RM_requestHead("GET", &u, "/api", "", NULL, -1, out, 20) < 0);

	const char* head = "HTTP/1.1 200 OK\r\nServer: nginx\r\nContent-Length: 2775\r\nconnection: keep-alive\r\n\r\nbody";
	CHECK(RM_headEnd(head, strlen(head)) == (int)(strlen(head) - 4));
	CHECK(RM_headEnd(head, 20) == 0);
	RMHead h;
	CHECK(RM_headParse(head, strlen(head) - 4, &h) == 0);
	CHECK(h.status == 200 && h.length == 2775 && !h.chunked);
	const char* chunked = "HTTP/1.1 302 Found\r\nTransfer-Encoding: Chunked\r\nLOCATION:  /api/x \r\n\r\n";
	CHECK(RM_headParse(chunked, strlen(chunked), &h) == 0);
	CHECK(h.status == 302 && h.chunked && h.length == -1);
	CHECK_STR(h.location, "/api/x");
	CHECK(RM_headParse("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n", 56, &h) != 0);
	CHECK(RM_headParse("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", 39, &h) != 0);
	CHECK(RM_headParse("SSH-2.0-OpenSSH\r\n\r\n", 19, &h) != 0);
	char big[RM_HEAD_MAX + 10];
	memset(big, 'a', sizeof(big));
	CHECK(RM_headEnd(big, sizeof(big)) == -1);

	// chunked bodies, whole and a byte at a time
	const char* coded = "4;ext=1\r\nWiki\r\n5\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\nTrailer: x\r\n\r\nleft over";
	char buf[256];
	size_t len = 0;
	RMChunks c = { 0 };
	snprintf(buf, sizeof(buf), "%s", coded);
	CHECK(RM_chunksFeed(&c, buf, strlen(buf), &len) == 1);
	buf[len] = '\0';
	CHECK_STR(buf, "Wikipedia in\r\n\r\nchunks.");
	RMChunks d = { 0 };
	char body[256] = "";
	size_t body_len = 0;
	int state = 0;
	for (size_t i = 0; i < strlen(coded) && state == 0; i++) {
		char one = coded[i];
		size_t got = 0;
		state = RM_chunksFeed(&d, &one, 1, &got);
		if (got) body[body_len++] = one;
	}
	body[body_len] = '\0';
	CHECK(state == 1);
	CHECK_STR(body, "Wikipedia in\r\n\r\nchunks.");
	RMChunks e = { 0 };
	snprintf(buf, sizeof(buf), "zz\r\n");
	CHECK(RM_chunksFeed(&e, buf, strlen(buf), &len) == -1);
	RMChunks f = { 0 };
	snprintf(buf, sizeof(buf), "3\r\nabcX");
	CHECK(RM_chunksFeed(&f, buf, strlen(buf), &len) == -1);
	RMChunks g = { 0 };
	snprintf(buf, sizeof(buf), "fffffffffffffffff\r\n");
	CHECK(RM_chunksFeed(&g, buf, strlen(buf), &len) == -1);

	char before[512], after[64];
	CHECK(RM_multipart("BOUND", "saveFile", "Game \"1\".sav", before, sizeof(before), after, sizeof(after)) == 0);
	CHECK_STR(before, "--BOUND\r\nContent-Disposition: form-data; name=\"saveFile\"; filename=\"Game _1_.sav\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n");
	CHECK_STR(after, "\r\n--BOUND--\r\n");
	CHECK(RM_multipart("BOUND", "saveFile", "x", before, 10, after, sizeof(after)) != 0);
}

static void testJson(void) {
	RJDoc doc;
	const char* text = " {\"a\": [1, -2.5e3, true, false, null], \"s\": \"x\\u00e9\\ud83d\\ude00\\n\\\"\", \"o\": {\"k\": \"v\"}, "
		"\"a\": 7, \"bad\": \"\\ud800x\"} ";
	CHECK(RJ_parse(&doc, text, strlen(text), 1000) == 0);
	const RJNode* root = RJ_root(&doc);
	CHECK(root && root->type == RJ_OBJECT);
	CHECK(RJ_int(&doc, root, "a", 0) == 7); // the last of a repeated key
	CHECK_STR(RJ_string(&doc, root, "s", ""), "x\xc3\xa9\xf0\x9f\x98\x80\n\"");
	CHECK_STR(RJ_string(&doc, root, "bad", ""), "\xef\xbf\xbdx");
	const RJNode* o = RJ_get(&doc, root, "o");
	CHECK_STR(RJ_string(&doc, o, "k", ""), "v");
	CHECK_STR(RJ_string(&doc, o, "missing", "fb"), "fb");
	const RJNode* a = RJ_first(&doc, root);
	CHECK(a && a->type == RJ_ARRAY && a->count == 5);
	CHECK(RJ_at(&doc, a, 2)->type == RJ_TRUE);
	CHECK(RJ_at(&doc, a, 4)->type == RJ_NULL);
	CHECK(RJ_at(&doc, a, 5) == NULL);
	RJ_free(&doc);

	const char* bad[] = { "", "{", "[1,]", "{\"a\" 1}", "\"\x01\"", "tru", "[1] 2", "{\"a\":\"\\q\"}", "-", "[\"\\u12\"]" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(RJ_parse(&doc, bad[i], strlen(bad[i]), 1000) != 0);
	char deep[200];
	memset(deep, '[', 70);
	memset(deep + 70, ']', 70);
	CHECK(RJ_parse(&doc, deep, 140, 1000) != 0);
	CHECK(RJ_parse(&doc, deep + 10, 120, 1000) == 0); // 60 deep
	RJ_free(&doc);
	CHECK(RJ_parse(&doc, "[1,2,3,4]", 9, 4) != 0);
	CHECK(RJ_parse(&doc, "\"\\u0000\"", 8, 4) == 0);
	CHECK_STR(RJ_root(&doc)->text, "\xef\xbf\xbd");
	RJ_free(&doc);
}

static void testFixtures(void) {
	size_t len;
	char* json = readAll(FIXTURES "/platforms.json", &len);
	CHECK(json != NULL);
	RMPlatform p[8];
	CHECK(RM_parsePlatforms(json, len, p, 8) == 3);
	CHECK(p[0].id == 1 && p[0].rom_count == 2);
	CHECK_STR(p[0].slug, "gb");
	CHECK_STR(p[0].name, "Game Boy");
	CHECK_STR(p[0].tag, "GB");
	CHECK_STR(p[1].tag, "GBA");
	CHECK_STR(p[2].slug, "nes");
	CHECK_STR(p[2].tag, "FC");
	CHECK(RM_parsePlatforms(json, len, p, 1) == 1);
	CHECK(RM_parsePlatforms("{}", 2, p, 8) == -1);
	free(json);

	json = readAll(FIXTURES "/roms-gb.json", &len);
	CHECK(json != NULL);
	RMRom roms[8];
	int count = 0, total = 0;
	CHECK(RM_parseRoms(json, len, roms, &count, 8, &total) == 2);
	CHECK(count == 2 && total == 2);
	CHECK(roms[0].id == 2 && roms[0].platform_id == 1 && roms[0].size == 32768 && !roms[0].multi);
	CHECK_STR(roms[0].file, "Mosaic Test One (World).gb");
	CHECK_STR(roms[0].name, "Mosaic Test One");
	CHECK_STR(roms[1].file, "Mosaic Test Two (World).gb");
	count = 0;
	CHECK(RM_parseRoms(json, len, roms, &count, 1, &total) == 2); // the page had two, one kept
	CHECK(count == 1);
	free(json);
	// a rom with an unsafe name is left out
	const char* evil = "{\"items\":[{\"id\":9,\"fs_name\":\"../../evil.gb\"},{\"id\":10,\"fs_name\":\".hidden\"},"
		"{\"id\":11,\"fs_name\":\"ok.gb\",\"has_multiple_files\":true}],\"total\":3}";
	count = 0;
	CHECK(RM_parseRoms(evil, strlen(evil), roms, &count, 8, &total) == 3);
	CHECK(count == 1 && roms[0].id == 11 && roms[0].multi);
	CHECK_STR(roms[0].name, "ok.gb");

	setenv("TZ", "UTC", 1);
	tzset();
	json = readAll(FIXTURES "/saves-rom2.json", &len);
	RMAsset assets[8];
	count = 0;
	CHECK(RM_parseAssets(json, len, RM_ASSET_SAVE, assets, &count, 8) == 3);
	free(json);
	json = readAll(FIXTURES "/states-rom2.json", &len);
	CHECK(RM_parseAssets(json, len, RM_ASSET_STATE, assets, &count, 8) == 1);
	free(json);
	CHECK(count == 4);
	RM_sortAssets(assets, count);
	// newest first: the two timestamped uploads, then the save and state of 07:54:23
	CHECK(assets[0].id == 3 && assets[0].kind == RM_ASSET_SAVE);
	CHECK_STR(assets[0].slot, "mosaic");
	CHECK_STR(assets[0].hash, "0ed6e2fab5c9506a53c06ec3940424f1");
	CHECK(assets[1].id == 2);
	CHECK(assets[2].kind == RM_ASSET_SAVE && assets[2].id == 1 && assets[2].size == 27);
	CHECK(assets[3].kind == RM_ASSET_STATE && assets[3].id == 1);
	CHECK_STR(assets[3].file, "Mosaic Test One (World).st0");
	CHECK_STR(assets[2].emulator, "gambatte");
	char when[32];
	RM_formatTime(assets[2].updated, when, sizeof(when));
	CHECK_STR(when, "2026-11-09 07:54");
	CHECK(RM_stateSlot(assets[3].file) == 0);
}

static void testText(void) {
	CHECK(RM_parseTime("1970-01-02T00:00:00Z") == 86400);
	CHECK(RM_parseTime("2026-11-09T07:54:20.813303+00:00") == RM_parseTime("2026-11-09 07:54:20"));
	CHECK(RM_parseTime("2026-11-09T08:54:20+01:00") == RM_parseTime("2026-11-09T07:54:20Z"));
	CHECK(RM_parseTime("2026-11-09T06:24:20-01:30") == RM_parseTime("2026-11-09T07:54:20Z"));
	CHECK(RM_parseTime("2000-02-29T00:00:00Z") == 951782400);
	CHECK(RM_parseTime("2026-13-09T07:54:20Z") == 0);
	CHECK(RM_parseTime("2026-11-09T07:54") == 0);
	CHECK(RM_parseTime("2026-11-09T07:54:20junk") == 0);
	CHECK(RM_parseTime(NULL) == 0);
	char out[64];
	RM_formatTime(0, out, sizeof(out));
	CHECK_STR(out, "unknown time");
	RM_formatSize(27, out, sizeof(out));
	CHECK_STR(out, "27 B");
	RM_formatSize(32768, out, sizeof(out));
	CHECK_STR(out, "32.0 KB");
	RM_formatSize(3 * 1024 * 1024 / 2, out, sizeof(out));
	CHECK_STR(out, "1.5 MB");

	CHECK(RM_safeName("Mosaic Test One (World).gb"));
	CHECK(RM_safeName("Pok\xc3\xa9mon.gb"));
	CHECK(!RM_safeName(""));
	CHECK(!RM_safeName("."));
	CHECK(!RM_safeName(".."));
	CHECK(!RM_safeName(".hidden"));
	CHECK(!RM_safeName("a/b"));
	CHECK(!RM_safeName("a\\b"));
	CHECK(!RM_safeName("a\nb"));
	CHECK(!RM_safeName(NULL));
	char long_name[RM_NAME_MAX + 1];
	memset(long_name, 'a', RM_NAME_MAX);
	long_name[RM_NAME_MAX] = '\0';
	CHECK(!RM_safeName(long_name));

	char names[3][RM_NAME_MAX];
	CHECK(RM_saveNames("Tetris (World).gb", names) == 3);
	CHECK_STR(names[0], "Tetris (World).gb.sav");
	CHECK_STR(names[1], "Tetris (World).srm");
	CHECK_STR(names[2], "Tetris (World).sav");
	RM_saveNames("Game.a", names);
	CHECK_STR(names[1], "Game.a.srm"); // a one-letter extension stays, as minarch has it
	char target[RM_NAME_MAX];
	CHECK(RM_saveTarget("Tetris (World).gb", "Tetris (World) [2026-11-09_07-54-35].sav", 0, target, sizeof(target)) == 0);
	CHECK_STR(target, "Tetris (World).gb.sav");
	CHECK(RM_saveTarget("Tetris (World).gb", "Tetris (World).srm", 0, target, sizeof(target)) == 1);
	CHECK(RM_saveTarget("Tetris (World).gb", "Tetris (World).sav", 0, target, sizeof(target)) == 2);
	CHECK(RM_saveTarget("Tetris (World).gb", "whatever.srm", 1 << 2, target, sizeof(target)) == 2);
	CHECK_STR(target, "Tetris (World).sav");
	CHECK(RM_stateSlot("Game.gb.st3") == 3);
	CHECK(RM_stateSlot("Game [2026].ST9") == 9);
	CHECK(RM_stateSlot("Game.state") == -1);
	CHECK(RM_stateSlot("Game.st") == -1);

	unsigned char pin[32];
	CHECK(RM_parsePin("E6:29:54:2A:A1:AA:03:A1:A2:73:56:9E:FF:06:53:8D:E5:3F:6B:C2:2F:3E:CA:7C:A7:92:C6:8F:9D:73:B2:36", pin) == 0);
	CHECK(pin[0] == 0xE6 && pin[31] == 0x36);
	char hex[65];
	RM_hex(pin, 32, hex, sizeof(hex));
	CHECK_STR(hex, "e629542aa1aa03a1a273569eff06538de53f6bc22f3eca7ca792c68f9d73b236");
	CHECK(RM_parsePin(hex, pin) == 0);
	CHECK(RM_parsePin("e629", pin) != 0);
	CHECK(RM_parsePin("zz29542aa1aa03a1a273569eff06538de53f6bc22f3eca7ca792c68f9d73b236", pin) != 0);
	CHECK(RM_parsePin("e629542aa1aa03a1a273569eff06538de53f6bc22f3eca7ca792c68f9d73b2361", pin) != 0);

	char status[256];
	CHECK(RM_statusText("running", 5, 32, "Mosaic Test One \xc3\xa9 and a very long name", status, sizeof(status)) > 0);
	CHECK_STR(status, "state=running\ndone=5\ntotal=32\nlabel=Mosaic Test One ?? and a very lo\n");
	RM_statusText("done", 2000000000LL, -1, "", status, sizeof(status));
	CHECK_STR(status, "state=done\ndone=999999999\ntotal=0\nlabel=\n");

	RMRecord r;
	CHECK(RM_recordParse("2\tGB\t-\t/sd/Roms/Game Boy (GB)/A.gb\r", &r) == 0);
	CHECK(r.rom_id == 2);
	CHECK_STR(r.tag, "GB");
	CHECK_STR(r.value, "-");
	CHECK_STR(r.path, "/sd/Roms/Game Boy (GB)/A.gb");
	CHECK(RM_recordParse("x\tGB\t-\t/p", &r) != 0);
	CHECK(RM_recordParse("2\tGB\t-", &r) != 0);
	CHECK(RM_recordParse("2\t\t-\t/p", &r) != 0);
	char line[RM_PATH_MAX + 128];
	CHECK(RM_recordParse("7\tGB\t-\t/sd/Roms/Game Boy (GB)/A.gb", &r) == 0);
	snprintf(r.value, sizeof(r.value), "abc");
	CHECK(RM_recordFormat(&r, line, sizeof(line)) > 0);
	CHECK_STR(line, "7\tGB\tabc\t/sd/Roms/Game Boy (GB)/A.gb\n");
	snprintf(r.path, sizeof(r.path), "/a\tb");
	CHECK(RM_recordFormat(&r, line, sizeof(line)) < 0);

	char tag[RM_TAG_MAX], folder[RM_NAME_MAX];
	CHECK(RM_tagFor("nes", tag, sizeof(tag), folder, sizeof(folder)) == 0);
	CHECK_STR(tag, "FC");
	CHECK_STR(folder, "Nintendo Entertainment System (FC)");
	CHECK(RM_tagFor("ps5", tag, sizeof(tag), folder, sizeof(folder)) != 0);
	CHECK_STR(tag, "");
	CHECK(RM_tagCount() > 30);
	for (int i = 0; i < RM_tagCount(); i++) {
		CHECK(RM_tagFor(RM_tagSlug(i), tag, sizeof(tag), folder, sizeof(folder)) == 0);
		char want[RM_TAG_MAX + 4];
		snprintf(want, sizeof(want), "(%s)", tag);
		size_t fl = strlen(folder), wl = strlen(want);
		CHECK(fl > wl && !strcmp(folder + fl - wl, want));
	}
	CHECK(RM_tagSlug(RM_tagCount()) == NULL);
}

///////////////////////////////////////
// the client, against a server in a child process

static int listener = -1;
static int port = 0;
static pid_t server = 0;

static void readRequest(int fd, char* buf, size_t size, size_t* len) {
	*len = 0;
	long long need = -1;
	while (*len + 1 < size) {
		ssize_t n = recv(fd, buf + *len, size - *len - 1, 0);
		if (n <= 0) break;
		*len += (size_t)n;
		buf[*len] = '\0';
		char* end = strstr(buf, "\r\n\r\n");
		if (!end) continue;
		if (need < 0) {
			char* cl = strstr(buf, "Content-Length: ");
			need = (long long)(end + 4 - buf) + (cl && cl < end ? atoll(cl + 16) : 0);
		}
		if ((long long)*len >= need) break;
	}
}

static void sendText(int fd, const char* text) {
	size_t len = strlen(text);
	while (len) {
		ssize_t n = send(fd, text, len, 0);
		if (n <= 0) return;
		text += n;
		len -= (size_t)n;
	}
}

static void serve(void) {
	signal(SIGPIPE, SIG_IGN);
	for (;;) {
		int fd = accept(listener, NULL, NULL);
		if (fd < 0) continue;
		static char req[1 << 20];
		size_t len;
		readRequest(fd, req, sizeof(req), &len);
		char path[512] = "";
		sscanf(req, "%*s %511s", path);
		char out[4096];
		if (!strcmp(path, "/base/plain")) {
			sendText(fd, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
		} else if (!strcmp(path, "/base/auth")) {
			const char* auth = strstr(req, "Authorization: Basic YWRtaW46bW9zYWlj\r\n") ? "yes" : "no";
			snprintf(out, sizeof(out), "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(auth), auth);
			sendText(fd, out);
		} else if (!strcmp(path, "/base/chunked")) {
			sendText(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n");
			usleep(50000);
			sendText(fd, "2\r\nde\r\n0\r\n\r\n");
		} else if (!strcmp(path, "/base/to-close")) {
			sendText(fd, "HTTP/1.1 200 OK\r\n\r\nuntil the end");
		} else if (!strcmp(path, "/base/redirect")) {
			sendText(fd, "HTTP/1.1 302 Found\r\nLocation: /base/plain\r\nContent-Length: 0\r\n\r\n");
		} else if (!strcmp(path, "/base/away")) {
			sendText(fd, "HTTP/1.1 301 Moved\r\nLocation: http://example.invalid/base/plain\r\nContent-Length: 0\r\n\r\n");
		} else if (!strcmp(path, "/base/outside")) {
			sendText(fd, "HTTP/1.1 301 Moved\r\nLocation: /elsewhere\r\nContent-Length: 0\r\n\r\n");
		} else if (!strcmp(path, "/base/loop")) {
			sendText(fd, "HTTP/1.1 302 Found\r\nLocation: /base/loop\r\nContent-Length: 0\r\n\r\n");
		} else if (!strcmp(path, "/base/big")) {
			sendText(fd, "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n");
		} else if (!strcmp(path, "/base/short")) {
			sendText(fd, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc");
		} else if (!strcmp(path, "/base/slow")) {
			usleep(1500000);
		} else if (!strcmp(path, "/base/upload")) {
			char* body = strstr(req, "\r\n\r\n");
			body = body ? body + 4 : req;
			const char* ok = strstr(body, "filename=\"Game.sav\"") && strstr(body, "\r\n\r\nSAVEDATA\r\n--B--\r\n") ? "got" : "bad";
			snprintf(out, sizeof(out), "HTTP/1.1 201 Created\r\nContent-Length: 3\r\n\r\n%s", ok);
			sendText(fd, out);
		} else if (!strcmp(path, "/base/file")) {
			sendText(fd, "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nROMBYTES");
		} else {
			sendText(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
		}
		close(fd);
	}
}

static void startServer(void) {
	listener = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = 0;
	CHECK(bind(listener, (struct sockaddr*)&a, sizeof(a)) == 0);
	socklen_t alen = sizeof(a);
	getsockname(listener, (struct sockaddr*)&a, &alen);
	port = ntohs(a.sin_port);
	CHECK(listen(listener, 16) == 0);
	server = fork();
	if (server == 0) {
		serve();
		_exit(0);
	}
	close(listener);
}

static RNConfig config(void) {
	RNConfig c;
	memset(&c, 0, sizeof(c));
	char url[64];
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/base", port);
	RM_urlParse(url, &c.url, NULL, 0);
	RM_authHeader("admin", "mosaic", c.auth, sizeof(c.auth));
	c.connect_ms = 2000;
	c.io_ms = 2000;
	return c;
}

static int fetch(const RNConfig* c, const char* target, long long max, RNCall* call, volatile int* cancel) {
	memset(call, 0, sizeof(*call));
	call->method = "GET";
	snprintf(call->target, sizeof(call->target), "%s", target);
	call->max_body = max;
	call->cancel = cancel;
	return RN_run(c, call);
}

static void testClient(const char* dir) {
	startServer();
	RNConfig c = config();
	RNCall call;
	CHECK(fetch(&c, "/plain", 100, &call, NULL) == 0);
	CHECK(call.status == 200 && call.len == 5);
	CHECK_STR(call.data, "hello");
	RN_free(&call);
	CHECK(fetch(&c, "/auth", 100, &call, NULL) == 0);
	CHECK_STR(call.data, "yes");
	RN_free(&call);
	CHECK(fetch(&c, "/chunked", 100, &call, NULL) == 0);
	CHECK_STR(call.data, "abcde");
	RN_free(&call);
	CHECK(fetch(&c, "/to-close", 100, &call, NULL) == 0);
	CHECK_STR(call.data, "until the end");
	RN_free(&call);
	CHECK(fetch(&c, "/redirect", 100, &call, NULL) == 0);
	CHECK(call.status == 200);
	CHECK_STR(call.data, "hello");
	CHECK_STR(call.target, "/plain");
	RN_free(&call);
	CHECK(fetch(&c, "/away", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "another server") != NULL);
	CHECK(fetch(&c, "/outside", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "outside RomM") != NULL);
	CHECK(fetch(&c, "/loop", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "too many") != NULL);
	CHECK(fetch(&c, "/big", 10, &call, NULL) != 0);
	CHECK(strstr(call.error, "bigger") != NULL);
	CHECK(fetch(&c, "/short", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "cut short") != NULL);
	CHECK(fetch(&c, "/missing", 100, &call, NULL) == 0);
	CHECK(call.status == 404);
	RN_free(&call);

	RNConfig quick = c;
	quick.io_ms = 300;
	CHECK(fetch(&quick, "/slow", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "stopped answering") != NULL);
	volatile int cancel = 1;
	CHECK(fetch(&c, "/slow", 100, &call, &cancel) != 0);
	CHECK_STR(call.error, "cancelled");

	// to a file, never through a link
	char file[512], link_path[512], save[512];
	snprintf(file, sizeof(file), "%s/download.part", dir);
	volatile long long done = 0, total = 0;
	memset(&call, 0, sizeof(call));
	call.method = "GET";
	snprintf(call.target, sizeof(call.target), "/file");
	call.max_body = 100;
	call.out_path = file;
	call.done = &done;
	call.total = &total;
	CHECK(RN_run(&c, &call) == 0);
	CHECK(done == 8 && total == 8);
	RN_free(&call);
	size_t len;
	char* got = readAll(file, &len);
	CHECK(got && len == 8 && !memcmp(got, "ROMBYTES", 8));
	free(got);
	snprintf(link_path, sizeof(link_path), "%s/link.part", dir);
	symlink(file, link_path);
	call.out_path = link_path;
	CHECK(RN_run(&c, &call) != 0);

	// a multipart upload from a file
	snprintf(save, sizeof(save), "%s/Game.sav", dir);
	FILE* f = fopen(save, "wb");
	fputs("SAVEDATA", f);
	fclose(f);
	char before[512], after[64];
	RM_multipart("B", "saveFile", "Game.sav", before, sizeof(before), after, sizeof(after));
	memset(&call, 0, sizeof(call));
	call.method = "POST";
	snprintf(call.target, sizeof(call.target), "/upload");
	call.content_type = "multipart/form-data; boundary=B";
	call.body_file = save;
	call.body_before = before;
	call.body_after = after;
	call.max_body = 100;
	CHECK(RN_run(&c, &call) == 0);
	CHECK(call.status == 201);
	CHECK_STR(call.data ? call.data : "", "got");
	RN_free(&call);
	char md5[33];
	CHECK(RN_md5File(save, 100, md5) == 0);
	CHECK_STR(md5, "37edf47123a77ffbc4709691c273fb09");
	CHECK(RN_md5File(save, 4, md5) != 0);
	CHECK(RN_md5File(link_path, 100, md5) != 0);

	// https needs a pin or CA certificates, and with an address, a pin
	RNConfig tls = c;
	tls.url.https = 1;
	CHECK(fetch(&tls, "/plain", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "IP address") != NULL);
	snprintf(tls.url.host, sizeof(tls.url.host), "localhost");
	CHECK(fetch(&tls, "/plain", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "no CA certificates") != NULL);
	snprintf(tls.ca_file, sizeof(tls.ca_file), "%s", save);
	CHECK(fetch(&tls, "/plain", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "can't read any certificates") != NULL);
	// a pinned server that speaks plain HTTP fails the handshake, never falls back
	tls.have_pin = 1;
	CHECK(fetch(&tls, "/plain", 100, &call, NULL) != 0);

	c.url.port = 1; // nothing listens there
	CHECK(fetch(&c, "/plain", 100, &call, NULL) != 0);
	CHECK(strstr(call.error, "connect") != NULL);

	kill(server, SIGKILL);
	waitpid(server, NULL, 0);
	unlink(file);
	unlink(link_path);
	unlink(save);

	CHECK(RN_countAnchors(FIXTURES "/ca.pem") == 1);
	CHECK(RN_countAnchors(FIXTURES "/platforms.json") == -1);
	char ca[512];
	setenv("SSL_CERT_FILE", FIXTURES "/ca.pem", 1);
	CHECK(RN_systemCa("/nowhere", "desktop", ca, sizeof(ca)) == 0);
	CHECK_STR(ca, FIXTURES "/ca.pem");
	unsetenv("SSL_CERT_FILE");
}

// a real RomM, when one is named
static void testLive(void) {
	const char* url = getenv("ROMM_TEST_URL");
	if (!url || !url[0]) return;
	RNConfig c;
	memset(&c, 0, sizeof(c));
	char error[200];
	CHECK(RM_urlParse(url, &c.url, error, sizeof(error)) == 0);
	const char* secret = getenv("ROMM_TEST_PASSWORD");
	RM_authHeader(getenv("ROMM_TEST_USER"), secret ? secret : "", c.auth, sizeof(c.auth));
	const char* pin = getenv("ROMM_TEST_PIN");
	if (pin && pin[0]) {
		CHECK(RM_parsePin(pin, c.pin) == 0);
		c.have_pin = 1;
	}
	const char* ca = getenv("ROMM_TEST_CA");
	if (ca) snprintf(c.ca_file, sizeof(c.ca_file), "%s", ca);
	c.connect_ms = 5000;
	c.io_ms = 10000;
	RNCall call;
	int rc = fetch(&c, "/api/platforms", 1 << 24, &call, NULL);
	const char* want_error = getenv("ROMM_TEST_EXPECT_ERROR");
	if (want_error && want_error[0]) {
		CHECK(rc != 0);
		printf("live %s: refused as expected: %s\n", url, call.error);
		CHECK(strstr(call.error, want_error) != NULL);
		return;
	}
	CHECK(rc == 0);
	if (rc != 0) fprintf(stderr, "live %s: %s\n", url, call.error);
	CHECK(call.status == 200);
	RMPlatform p[64];
	int n = rc == 0 ? RM_parsePlatforms(call.data, call.len, p, 64) : -1;
	CHECK(n > 0);
	printf("live %s: %d platforms\n", url, n);
	RN_free(&call);
}

int main(void) {
	signal(SIGPIPE, SIG_IGN);
	char dir[] = "/tmp/romm-test-XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	testUrls();
	testRequests();
	testJson();
	testFixtures();
	testText();
	testClient(dir);
	testLive();
	rmdir(dir);
	CHECK_DONE();
}
