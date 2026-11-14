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
#include "words.h"
#include "miniz.h"
#include "romm_model.h"
#include "romm_net.h"
#include "romm_zip.h"

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
	CHECK(RM_parsePlatforms(json, len, p, 8) == 4);
	CHECK(p[0].id == 1 && p[0].rom_count == 2);
	CHECK_STR(p[0].slug, "gb");
	CHECK_STR(p[0].name, "Game Boy");
	CHECK_STR(p[0].tag, "GB");
	CHECK_STR(p[1].tag, "GBA");
	CHECK_STR(p[2].slug, "nes");
	CHECK_STR(p[2].tag, "FC");
	CHECK_STR(p[3].slug, "psx");
	CHECK_STR(p[3].tag, "PS");
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
	// a rom with an unsafe name is left out, and so is a zip without its files
	const char* evil = "{\"items\":[{\"id\":9,\"fs_name\":\"../../evil.gb\"},{\"id\":10,\"fs_name\":\".hidden\"},"
		"{\"id\":11,\"fs_name\":\"ok.gb\",\"has_multiple_files\":true},{\"id\":12,\"fs_name\":\"ok2.gb\"}],\"total\":4}";
	count = 0;
	CHECK(RM_parseRoms(evil, strlen(evil), roms, &count, 8, &total) == 4);
	CHECK(count == 1 && roms[0].id == 12 && !roms[0].multi);
	CHECK_STR(roms[0].name, "ok2.gb");
	// a zip whose files would unpack outside its folder, or that has nothing to
	// launch, is left out; one file in a folder is fetched as that file
	const char* zips = "{\"items\":["
		"{\"id\":20,\"fs_name\":\"Bad\",\"full_path\":\"roms/psx/Bad\",\"files\":["
		"{\"file_name\":\"a.cue\",\"full_path\":\"roms/psx/Bad/../a.cue\"},"
		"{\"file_name\":\"b.cue\",\"full_path\":\"roms/psx/Bad/b.cue\"}]},"
		"{\"id\":21,\"fs_name\":\"Lists\",\"full_path\":\"roms/psx/Lists\",\"files\":["
		"{\"file_name\":\"a.m3u\",\"full_path\":\"roms/psx/Lists/a.m3u\"},"
		"{\"file_name\":\"b.m3u\",\"full_path\":\"roms/psx/Lists/b.m3u\"}]},"
		"{\"id\":22,\"fs_name\":\"Nested\",\"full_path\":\"roms/psx/Nested\",\"files\":["
		"{\"file_name\":\"Nested (USA).chd\",\"full_path\":\"roms/psx/Nested/Nested (USA).chd\"}]}],\"total\":3}";
	count = 0;
	CHECK(RM_parseRoms(zips, strlen(zips), roms, &count, 8, &total) == 3);
	CHECK(count == 1 && roms[0].id == 22 && !roms[0].multi);
	CHECK_STR(roms[0].file, "Nested (USA).chd");

	// multi-file roms, recorded from RomM 5.4.1 with with_files=true
	json = readAll(FIXTURES "/roms-psx.json", &len);
	CHECK(json != NULL);
	count = 0;
	CHECK(RM_parseRoms(json, len, roms, &count, 8, &total) == 2);
	CHECK(count == 2 && total == 2);
	CHECK(roms[0].id == 4 && roms[0].multi && roms[0].size == 200084);
	CHECK_STR(roms[0].file, "Mosaic One Disc (USA)");
	CHECK_STR(roms[0].launch, "Mosaic One Disc (USA).cue");
	CHECK(roms[1].id == 3 && roms[1].multi && roms[1].size == 600186);
	CHECK_STR(roms[1].file, "Mosaic Two Disc (USA)");
	CHECK_STR(roms[1].launch, "Mosaic Two Disc (USA).m3u");
	free(json);

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
	// in the words they're given, then English again
	static const char* const words[] = { "unknown time", "hora desconocida", "KB", "kB", NULL };
	fake_words = words;
	RMWords set = { fakeTr, fakePlural };
	RM_setWords(&set);
	RM_formatTime(0, out, sizeof(out));
	CHECK_STR(out, "hora desconocida");
	RM_formatSize(32768, out, sizeof(out));
	CHECK_STR(out, "32.0 kB");
	RM_setWords(NULL);
	RM_formatSize(32768, out, sizeof(out));
	CHECK_STR(out, "32.0 KB");

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
	CHECK(symlink(file, link_path) == 0);
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

///////////////////////////////////////
// zips

static RMZipEntry entry(const char* name, unsigned long long size) {
	return (RMZipEntry){ name, strlen(name), 0, 0, 0100644UL << 16, size, size };
}

static void testZipChecks(void) {
	char why[160];
	const char* good[] = { "a.bin", "Disc 1/Game (Disc 1).cue", "folder/", ".DS_Store", "a/b/c/d/e/f/g/h" };
	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) CHECK(RM_zipNameCheck(good[i], strlen(good[i]), why, sizeof(why)) == 0);
	const char* bad[] = { "", "/", "/etc/passwd", "C:x.bin", "../x", "a/../b", "a/./b", "a//b", "a\\..\\b", "a\\b",
		"\x01.bin", "a:b", "a?b", "trailing.", "trailing ", "a./b", ".romm-part", "x/.romm-3.unpack/y",
		"a/b/c/d/e/f/g/h/i", "..", "a//", " \xe1//" };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		int rc = RM_zipNameCheck(bad[i], strlen(bad[i]), why, sizeof(why));
		CHECK(rc == -1);
		if (rc == 0) fprintf(stderr, "  allowed \"%s\"\n", bad[i]);
	}
	CHECK(RM_zipNameCheck("a\0b", 3, why, sizeof(why)) == -1);
	CHECK(strstr(why, "NUL") != NULL);
	char long_name[RM_ZIP_NAME_MAX + 2];
	memset(long_name, 'a', sizeof(long_name));
	CHECK(RM_zipNameCheck(long_name, sizeof(long_name), why, sizeof(why)) == -1);
	CHECK(RM_zipNameCheck(long_name, RM_NAME_MAX, why, sizeof(why)) == -1); // one part too long

	RMZipEntry e = entry("a.bin", 100);
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == 0);
	e.external = 0120777UL << 16; // a link
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	CHECK(strstr(why, "link") != NULL);
	e.external = 0020644UL << 16; // a device
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	e.external = 0040755UL << 16; // a folder that's named as a file
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	e.external = 0x20; // MS-DOS's archive bit only
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == 0);
	e = entry("a.bin", 100);
	e.flags = 1; // encrypted
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	e = entry("a.bin", 100);
	e.method = 12; // bzip2
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	e = entry("a.bin", 100);
	e.comp = 99; // stored, but the sizes differ
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	e = entry("d/", 0);
	e.external = 0040755UL << 16;
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == 0);
	e.size = e.comp = 5; // a folder with data
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	e = entry("big.bin", (unsigned long long)RM_ZIP_FILE_MAX + 1);
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	// a bomb: deflated past what deflate can do, or something from nothing
	e = entry("bomb.bin", 1000ULL * 1024 * 1024);
	e.method = 8;
	e.comp = 1000;
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);
	CHECK(strstr(why, "zip bomb") != NULL);
	e.comp = 1000ULL * 1024 * 1024 / RM_ZIP_RATIO_MAX + 1;
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == 0);
	e.comp = 0;
	CHECK(RM_zipEntryCheck(&e, why, sizeof(why)) == -1);

	RMZipEntry set[4] = { entry("Game (Disc 1).cue", 90), entry("Game (Disc 1).bin", 100000), entry("Disc/a", 1),
		entry("disc/b", 1) };
	long long total = 0;
	CHECK(RM_zipCheck(set, 2, 100090, &total, why, sizeof(why)) == 0);
	CHECK(total == 100090);
	CHECK(RM_zipCheck(set, 2, 100090 - RM_ZIP_SLACK, &total, why, sizeof(why)) == 0);
	CHECK(RM_zipCheck(set, 2, 100090 - RM_ZIP_SLACK - 1, &total, why, sizeof(why)) == -1);
	CHECK(strstr(why, "more than the server said") != NULL);
	CHECK(RM_zipCheck(set, 2, -1, &total, why, sizeof(why)) == 0);
	CHECK(RM_zipCheck(set, 4, -1, &total, why, sizeof(why)) == -1); // two spellings of a folder
	set[3] = entry("Disc/b", 1);
	CHECK(RM_zipCheck(set, 4, -1, &total, why, sizeof(why)) == 0); // the same folder twice
	set[3] = entry("disc", 1);
	CHECK(RM_zipCheck(set, 4, -1, &total, why, sizeof(why)) == -1); // a file and a folder
	set[3] = entry("GAME (DISC 1).CUE", 1);
	CHECK(RM_zipCheck(set, 4, -1, &total, why, sizeof(why)) == -1); // a file twice
	CHECK(strstr(why, "same name") != NULL);
	set[3] = entry("Game (Disc 1).cue", 1);
	CHECK(RM_zipCheck(set, 4, -1, &total, why, sizeof(why)) == -1);
	set[3] = entry("../evil", 1);
	CHECK(RM_zipCheck(set, 4, -1, &total, why, sizeof(why)) == -1);
	CHECK(RM_zipCheck(set, RM_ZIP_ENTRIES_MAX + 1, -1, &total, why, sizeof(why)) == -1);
	RMZipEntry huge[5];
	for (int i = 0; i < 5; i++) {
		static char names[5][8];
		snprintf(names[i], sizeof(names[i]), "%d.bin", i);
		huge[i] = entry(names[i], (unsigned long long)RM_ZIP_FILE_MAX);
	}
	CHECK(RM_zipCheck(huge, 5, -1, &total, why, sizeof(why)) == -1); // over the total
}

static void testDiscs(void) {
	const char* two[] = { "Game (USA) (Disc 2).bin", "Game (USA) (Disc 1).cue", "Game (USA).m3u", "Game (USA) (Disc 1).bin",
		"Game (USA) (Disc 2).cue" };
	int order[8];
	CHECK(RM_discs(two, 5, order) == 2);
	CHECK(order[0] == 1 && order[1] == 4);
	char launch[RM_NAME_MAX];
	CHECK(RM_launchFile("Game (USA)", two, 5, launch, sizeof(launch)) == 2);
	CHECK_STR(launch, "Game (USA).m3u");
	char m3u[256];
	CHECK(RM_m3uText(two, order, 2, m3u, sizeof(m3u)) > 0);
	CHECK_STR(m3u, "Game (USA) (Disc 1).cue\nGame (USA) (Disc 2).cue\n");
	CHECK(RM_m3uText(two, order, 2, m3u, 10) == -1);

	const char* one[] = { "Game.bin", "Game.cue" };
	CHECK(RM_launchFile("Game", one, 2, launch, sizeof(launch)) == 1);
	CHECK_STR(launch, "Game.cue");
	// numbered first, in number order (10 after 2), then by name; a CHD
	// stands on its own beside a sheet
	const char* chds[] = { "x (Disc 10).chd", "extra.chd", "x (Disc 2).chd", "x (CD 1).chd", "t.gdi", "t.bin" };
	CHECK(RM_discs(chds, 6, order) == 5);
	CHECK(order[0] == 3 && order[1] == 2 && order[2] == 0 && order[3] == 1 && order[4] == 4);
	// with no sheet, tracks are discs
	const char* bins[] = { "b.bin", "a.bin" };
	CHECK(RM_discs(bins, 2, order) == 2);
	CHECK(order[0] == 1);
	const char* none[] = { "a.m3u", "Game.m3u" };
	CHECK(RM_launchFile("Game", none, 2, launch, sizeof(launch)) == 0);
	CHECK_STR(launch, "");

	CHECK(RM_discNumber("Game (Disc 2).cue") == 2);
	CHECK(RM_discNumber("Game (disk 1 of 2).adf") == 1);
	CHECK(RM_discNumber("Game (Disc 2A).cue") == 2);
	CHECK(RM_discNumber("Game (Disc B).cue") == 2);
	CHECK(RM_discNumber("Game (Disque 3).cue") == 3);
	CHECK(RM_discNumber("Game (CDi).cue") == 0);
	CHECK(RM_discNumber("Game (Disc).cue") == 0);
	CHECK(RM_discNumber("Game (Disc 777777777777777777).cue") > 0); // found by fuzzing: no overflow
	CHECK(RM_discNumber("Disc 2 (Disc 1)/a.cue") == 0); // only the file's own name counts

	CHECK(RM_isGameM3u("Game (USA)", "game (usa).M3U"));
	CHECK(!RM_isGameM3u("Game (USA)", "sub/Game (USA).m3u"));
	CHECK(!RM_isGameM3u("Game (USA)", "Game.m3u"));

	char path[RM_PATH_MAX];
	CHECK(RM_m3uFor("/sd/Roms/PS/Game/Game (Disc 1).cue", path, sizeof(path)) == 0);
	CHECK_STR(path, "/sd/Roms/PS/Game/Game.m3u");
	CHECK(RM_m3uFor("/sd/Roms/PS/Game/Game.m3u", path, sizeof(path)) == 0);
	CHECK_STR(path, "/sd/Roms/PS/Game/Game.m3u");
	CHECK(RM_m3uFor("Game.cue", path, sizeof(path)) == -1);
}

static void testStates(void) {
	CHECK(RM_stateSlot("Game.gb.st0") == 0);
	CHECK(RM_stateSlot("Game.state") == 0);
	CHECK(RM_stateSlot("Game.state3") == 3);
	CHECK(RM_stateSlot("Game.state.4") == 4);
	CHECK(RM_stateSlot("Game.STATE.AUTO") == 9);
	CHECK(RM_stateSlot("Game.state10") == -1);
	CHECK(RM_stateSlot("Game.st10") == -1);
	CHECK(RM_stateSlot(".state") == -1);
	CHECK(RM_stateSlot("Game.sav") == -1);
	CHECK(RM_stateSlot("Game.state.png") == -1);

	char name[RM_NAME_MAX];
	CHECK(RM_stateName("Game (USA).m3u", 0, 3, name, sizeof(name)) == 0);
	CHECK_STR(name, "Game (USA).m3u.st3");
	CHECK(RM_stateName("Game (USA).m3u", 1, 3, name, sizeof(name)) == 0);
	CHECK_STR(name, "Game (USA).state.3");
	CHECK(RM_stateName("Game (USA).m3u", 2, 9, name, sizeof(name)) == 0);
	CHECK_STR(name, "Game (USA).state.auto");
	CHECK(RM_stateName("Game.gb", 3, 0, name, sizeof(name)) == 0);
	CHECK_STR(name, "Game.state");
	CHECK(RM_stateName("Game.gb", 4, 2, name, sizeof(name)) == 0);
	CHECK_STR(name, "Game.state2");
	CHECK(RM_stateName("Game.gb", 3, 9, name, sizeof(name)) == 0);
	CHECK_STR(name, "Game.state.auto");
	CHECK(RM_stateName("Game.gb", 5, 0, name, sizeof(name)) == -1);
	CHECK(RM_stateName("Game.gb", 0, 10, name, sizeof(name)) == -1);

	CHECK(RM_stateFormat("saveFormat=1\nstateFormat=3\n") == 3);
	CHECK(RM_stateFormat("stateFormat=7\n") == 0);
	CHECK(RM_stateFormat("xstateFormat=3\n") == 0);
	CHECK(RM_stateFormat("") == 0);

	char core[64];
	CHECK(RM_emuCore("#!/bin/sh\n\nEMU_EXE=pcsx_rearmed\n\n###\n", core, sizeof(core)) == 0);
	CHECK_STR(core, "pcsx_rearmed");
	CHECK(RM_emuCore("  EMU_EXE=\"gambatte\"\n", core, sizeof(core)) == 0);
	CHECK_STR(core, "gambatte");
	CHECK(RM_emuCore("EMU_EXE=../x\n", core, sizeof(core)) == -1);
	CHECK(RM_emuCore("EMU_EXE=\"a b\"\n", core, sizeof(core)) == -1);
	CHECK(RM_emuCore("echo hi\n", core, sizeof(core)) == -1);

	const char* folders[] = { "PS-pcsx_rearmed", "PS-swanstation" };
	char folder[RM_NAME_MAX];
	CHECK(RM_stateFolder("PS", folders, 2, "swanstation", "", folder, sizeof(folder)) == 0);
	CHECK_STR(folder, "PS-swanstation");
	CHECK(RM_stateFolder("PS", folders, 0, "pcsx_rearmed", "", folder, sizeof(folder)) == 0); // made when restored
	CHECK_STR(folder, "PS-pcsx_rearmed");
	CHECK(RM_stateFolder("PS", folders, 1, "", "", folder, sizeof(folder)) == 0);
	CHECK_STR(folder, "PS-pcsx_rearmed");
	CHECK(RM_stateFolder("PS", folders, 2, "", "", folder, sizeof(folder)) == -1);
	CHECK(RM_stateFolder("PS", folders, 2, "", "swanstation", folder, sizeof(folder)) == 0);
	CHECK_STR(folder, "PS-swanstation");
	CHECK(RM_stateFolder("PS", folders, 2, "", "mednafen", folder, sizeof(folder)) == -1);
}

// A zip made by hand, so it can hold what a real zip tool won't write
typedef struct TestEntry {
	const char* name;
	size_t name_len; // 0 for strlen
	const char* data;
	size_t len;
	int deflate; // as one stored deflate block
	unsigned long external;
	unsigned long long claim; // the size it says it has, when not 0
	int bad_crc;
} TestEntry;

static void put16(FILE* f, unsigned v) {
	fputc(v & 255, f);
	fputc(v >> 8 & 255, f);
}

static void put32(FILE* f, unsigned long v) {
	put16(f, v & 0xffff);
	put16(f, v >> 16 & 0xffff);
}

static void makeZip(const char* path, const TestEntry* entries, int count) {
	FILE* f = fopen(path, "wb");
	unsigned long* offsets = calloc((size_t)count, sizeof(unsigned long));
	for (int pass = 0; pass < 2; pass++) {
		long cd_start = ftell(f);
		for (int i = 0; i < count; i++) {
			const TestEntry* e = &entries[i];
			size_t name_len = e->name_len ? e->name_len : strlen(e->name);
			unsigned long crc = (unsigned long)mz_crc32(MZ_CRC32_INIT, (const unsigned char*)e->data, e->len) ^ (e->bad_crc ? 1 : 0);
			unsigned long comp = (unsigned long)(e->deflate ? e->len + 5 : e->len);
			unsigned long size = (unsigned long)(e->claim ? e->claim : e->len);
			if (!e->deflate && e->claim) comp = size;
			if (pass == 0) {
				offsets[i] = (unsigned long)ftell(f);
				put32(f, 0x04034b50);
				put16(f, 20);
				put16(f, 0);
			} else {
				put32(f, 0x02014b50);
				put16(f, 3 << 8 | 20);
				put16(f, 20);
				put16(f, 0);
			}
			put16(f, e->deflate ? 8 : 0);
			put16(f, 0);
			put16(f, 0x21);
			put32(f, crc);
			put32(f, comp);
			put32(f, size);
			put16(f, (unsigned)name_len);
			put16(f, 0);
			if (pass == 1) {
				put16(f, 0);
				put16(f, 0);
				put16(f, 0);
				put32(f, e->external ? e->external : 0100644UL << 16);
				put32(f, offsets[i]);
			}
			fwrite(e->name, 1, name_len, f);
			if (pass == 0) {
				if (e->deflate) {
					fputc(1, f);
					put16(f, (unsigned)e->len);
					put16(f, (unsigned)~e->len & 0xffff);
				}
				fwrite(e->data, 1, e->len, f);
			}
		}
		if (pass == 1) {
			long cd_end = ftell(f);
			put32(f, 0x06054b50);
			put16(f, 0);
			put16(f, 0);
			put16(f, (unsigned)count);
			put16(f, (unsigned)count);
			put32(f, (unsigned long)(cd_end - cd_start));
			put32(f, (unsigned long)cd_start);
			put16(f, 0);
		}
	}
	free(offsets);
	fclose(f);
}

// the names in a folder, sorted and joined by '|', for checking what's left
static void listing(const char* dir, char* out, size_t size) {
	char cmd[RM_PATH_MAX * 2];
	snprintf(cmd, sizeof(cmd), "cd '%s' && find . | LC_ALL=C sort | tr '\\n' '|'", dir);
	FILE* p = popen(cmd, "r");
	size_t n = p ? fread(out, 1, size - 1, p) : 0;
	out[n] = '\0';
	if (p) pclose(p);
}

static int unpack(const char* dir, const char* zip, const char* game, long long expect, int cancel, RZJob* j) {
	static char temp[RM_PATH_MAX];
	snprintf(temp, sizeof(temp), "%s/.romm-3.unpack", dir);
	static volatile int stop;
	static volatile long long done, total;
	stop = cancel;
	memset(j, 0, sizeof(*j));
	j->zip = zip;
	j->dir = dir;
	j->game = game;
	j->temp = temp;
	j->expect = expect;
	j->cancel = &stop;
	j->done = &done;
	j->total = &total;
	return RZ_unpack(j);
}

static void testUnpack(const char* top) {
	char dir[512], zip[RM_PATH_MAX], path[RM_PATH_MAX], was[2048], now[2048];
	snprintf(dir, sizeof(dir), "%s/PS", top);
	mkdir(dir, 0755);
	snprintf(zip, sizeof(zip), "%s/game.zip", top);
	RZJob j;
	const char* cue1 = "FILE \"G (Disc 1).bin\" BINARY\n";
	const char* cue2 = "FILE \"G (Disc 2).bin\" BINARY\n";

	// two discs, RomM's own .m3u replaced by the plugin's
	TestEntry two[] = {
		{ "G (Disc 1).bin", 0, "disc one", 8, 0, 0, 0, 0 },
		{ "G (Disc 1).cue", 0, cue1, strlen(cue1), 1, 0, 0, 0 },
		{ "G (Disc 2).bin", 0, "disc two", 8, 0, 0, 0, 0 },
		{ "G (Disc 2).cue", 0, cue2, strlen(cue2), 0, 0, 0, 0 },
		{ "G.m3u", 0, "nonsense\n", 9, 0, 0, 0, 0 },
	};
	makeZip(zip, two, 5);
	CHECK(unpack(dir, zip, "G", 16 + (long long)strlen(cue1) + strlen(cue2), 0, &j) == 0);
	if (j.error[0]) fprintf(stderr, "  unpack: %s\n", j.error);
	CHECK_STR(j.launch, "G/G.m3u");
	CHECK(j.files == 5 && j.discs == 2);
	listing(dir, now, sizeof(now));
	CHECK_STR(now, ".|./G|./G/G (Disc 1).bin|./G/G (Disc 1).cue|./G/G (Disc 2).bin|./G/G (Disc 2).cue|./G/G.m3u|");
	snprintf(path, sizeof(path), "%s/G/G.m3u", dir);
	size_t len;
	char* text = readAll(path, &len);
	CHECK(text && !strcmp(text, "G (Disc 1).cue\nG (Disc 2).cue\n"));
	free(text);
	snprintf(path, sizeof(path), "%s/G/G (Disc 1).cue", dir);
	text = readAll(path, &len);
	CHECK(text && !strcmp(text, cue1));
	free(text);
	// it's there now, so it isn't unpacked over
	listing(dir, was, sizeof(was));
	CHECK(unpack(dir, zip, "G", -1, 0, &j) == -1);
	CHECK(strstr(j.error, "already on the card") != NULL);
	listing(dir, now, sizeof(now));
	CHECK_STR(now, was);
	snprintf(path, sizeof(path), "%s/G", dir);
	CHECK(RZ_removeTree(path) == 0);

	// a zip of one file lands as that file
	TestEntry single[] = { { "Solo.chd", 0, "chd", 3, 0, 0, 0, 0 }, { "Solo.m3u", 0, "Solo.chd", 8, 0, 0, 0, 0 } };
	makeZip(zip, single, 2);
	CHECK(unpack(dir, zip, "Solo", 11, 0, &j) == 0);
	CHECK_STR(j.launch, "Solo.chd");
	listing(dir, now, sizeof(now));
	CHECK_STR(now, ".|./Solo.chd|");
	snprintf(path, sizeof(path), "%s/Solo.chd", dir);
	unlink(path);

	// one disc in a folder: no .m3u, its cue launches; subfolders are kept
	TestEntry one[] = { { "One.cue", 0, "cue", 3, 0, 0, 0, 0 }, { "tracks/", 0, "", 0, 0, 0040755UL << 16, 0, 0 },
		{ "tracks/One.bin", 0, "bin", 3, 1, 0, 0, 0 } };
	makeZip(zip, one, 3);
	CHECK(unpack(dir, zip, "One", -1, 0, &j) == 0);
	CHECK_STR(j.launch, "One/One.cue");
	listing(dir, now, sizeof(now));
	CHECK_STR(now, ".|./One|./One/One.cue|./One/tracks|./One/tracks/One.bin|");
	snprintf(path, sizeof(path), "%s/One", dir);
	RZ_removeTree(path);

	// zips that are refused, leaving nothing behind; RomM said 64 KiB
	listing(dir, was, sizeof(was));
	CHECK_STR(was, ".|");
	static char big[2 * RM_ZIP_SLACK];
	struct {
		const char* what;
		TestEntry entries[3];
		int count;
		const char* why;
	} evil[] = {
		{ "path traversal", { { "ok.cue", 0, "x", 1, 0, 0, 0, 0 }, { "../../evil.cue", 0, "x", 1, 0, 0, 0, 0 } }, 2, ". or .." },
		{ "absolute path", { { "ok.cue", 0, "x", 1, 0, 0, 0, 0 }, { "/tmp/evil.cue", 0, "x", 1, 0, 0, 0, 0 } }, 2, "absolute" },
		{ "backslash", { { "ok.cue", 0, "x", 1, 0, 0, 0, 0 }, { "..\\evil.cue", 0, "x", 1, 0, 0, 0, 0 } }, 2, "backslash" },
		{ "NUL", { { "ok.cue", 0, "x", 1, 0, 0, 0, 0 }, { "a.cue\0../x", 10, "x", 1, 0, 0, 0, 0 } }, 2, "NUL" },
		{ "symlink entry",
			{ { "ok.cue", 0, "x", 1, 0, 0, 0, 0 }, { "link.cue", 0, "/etc/passwd", 11, 0, 0120777UL << 16, 0, 0 } }, 2, "link" },
		{ "bomb ratio", { { "ok.cue", 0, "x", 1, 0, 0, 0, 0 }, { "bomb.bin", 0, "zzzz", 4, 1, 0, 1ULL << 30, 0 } }, 2,
			"zip bomb" },
		{ "duplicate names", { { "a.cue", 0, "x", 1, 0, 0, 0, 0 }, { "A.CUE", 0, "y", 1, 0, 0, 0, 0 } }, 2, "same name" },
		{ "duplicate folder", { { "d/a.cue", 0, "x", 1, 0, 0, 0, 0 }, { "D/b.cue", 0, "y", 1, 0, 0, 0, 0 } }, 2, "same name" },
		{ "more than RomM said", { { "a.cue", 0, "x", 1, 0, 0, 0, 0 }, { "b.bin", 0, big, sizeof(big), 0, 0, 0, 0 } }, 2,
			"more than the server said" },
		// these get as far as writing: the first file is in before the second fails
		{ "bad CRC", { { "a.cue", 0, "x", 1, 0, 0, 0, 0 }, { "b.bin", 0, "data", 4, 0, 0, 0, 1 } }, 2, "can't unpack" },
		{ "inflates past its size", { { "a.cue", 0, "x", 1, 0, 0, 0, 0 }, { "b.bin", 0, "data", 4, 1, 0, 2, 0 } }, 2,
			"can't unpack" },
	};
	for (size_t i = 0; i < sizeof(evil) / sizeof(evil[0]); i++) {
		makeZip(zip, evil[i].entries, evil[i].count);
		int rc = unpack(dir, zip, "Evil", 1, 0, &j);
		CHECK(rc == -1);
		if (rc != -1 || !strstr(j.error, evil[i].why)) fprintf(stderr, "  %s: %s\n", evil[i].what, j.error);
		CHECK(strstr(j.error, evil[i].why) != NULL);
		listing(dir, now, sizeof(now));
		CHECK_STR(now, was);
		listing(top, now, sizeof(now));
		CHECK(strstr(now, "evil") == NULL);
	}

	// cancelled before the first file: nothing is left
	makeZip(zip, two, 5);
	CHECK(unpack(dir, zip, "G", -1, 1, &j) == -1);
	CHECK_STR(j.error, "cancelled");
	listing(dir, now, sizeof(now));
	CHECK_STR(now, was);

	// not a zip at all
	FILE* f = fopen(zip, "wb");
	fputs("<html>not a zip</html>", f);
	fclose(f);
	CHECK(unpack(dir, zip, "G", -1, 0, &j) == -1);
	CHECK(strstr(j.error, "isn't a zip") != NULL);
	// a link where the zip should be is never followed
	snprintf(path, sizeof(path), "%s/link.zip", top);
	CHECK(symlink(zip, path) == 0);
	makeZip(zip, two, 5);
	CHECK(unpack(dir, path, "G", -1, 0, &j) == -1);
	unlink(path);
	listing(dir, now, sizeof(now));
	CHECK_STR(now, was);

	// RZ_removeTree takes a link away, not what it points at
	snprintf(path, sizeof(path), "%s/tree/a/b", top);
	char cmd[RM_PATH_MAX * 4 + 64];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s' && touch '%s/f' && ln -s '%s' '%s/../out'", path, path, dir, path);
	CHECK(system(cmd) == 0);
	snprintf(path, sizeof(path), "%s/tree", top);
	CHECK(RZ_removeTree(path) == 0);
	CHECK(access(path, F_OK) != 0 && access(dir, F_OK) == 0);
	unlink(zip);
	rmdir(dir);
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

static int keepAllBut(const char* path) {
	return strcmp(path, "/gone.gb") != 0;
}

// what's synced and downloaded: lines, lists kept newest first, replies
static void testSynced(void) {
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (World).gb.st3") == 3);
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (World).state.auto") == 9);
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (World).state") == 0);
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (World).state.4") == 4);
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (World).state7") == 7);
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (World).gb.sav") == -1);
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (USA).gb.st3") == -1);
	CHECK(RM_stateOf("Tetris (World).gb", "Tetris (World).gb.st10") == -1);
	RMSynced e;
	CHECK(RM_syncedParse("down\t1700000000\t20\t1699999999\t/sd/.userdata/shared/GB-gambatte/A.gb.st1\r", &e) == 0);
	CHECK(e.down == 1 && e.at == 1700000000 && e.size == 20 && e.mtime == 1699999999);
	CHECK_STR(e.path, "/sd/.userdata/shared/GB-gambatte/A.gb.st1");
	CHECK(RM_syncedParse("up\t1\t2\t3\t/p", &e) == 0 && e.down == 0);
	CHECK(RM_syncedParse("sideways\t1\t2\t3\t/p", &e) != 0);
	CHECK(RM_syncedParse("up\t1\t2\t/p", &e) != 0);
	CHECK(RM_syncedParse("up\t1\t-2\t3\t/p", &e) != 0);
	CHECK(RM_syncedParse("up\t1\t2\t3\t", &e) != 0);
	char line[RM_PATH_MAX + 128];
	CHECK(RM_syncedParse("up\t1\t2\t3\t/p", &e) == 0);
	CHECK(RM_syncedFormat(&e, line, sizeof(line)) > 0);
	CHECK_STR(line, "up\t1\t2\t3\t/p\n");
	snprintf(e.path, sizeof(e.path), "/a\nb");
	CHECK(RM_syncedFormat(&e, line, sizeof(line)) < 0);

	RMSynced list[3];
	int count = 0;
	const char* paths[] = { "/a", "/b", "/c", "/d" };
	for (int i = 0; i < 4; i++) {
		memset(&e, 0, sizeof(e));
		e.at = i;
		snprintf(e.path, sizeof(e.path), "%s", paths[i]);
		count = RM_syncedPut(list, count, 3, &e);
	}
	CHECK(count == 3);
	CHECK_STR(list[0].path, "/d");
	CHECK_STR(list[2].path, "/b"); // /a, the oldest, went
	memset(&e, 0, sizeof(e));
	e.at = 9;
	e.down = 1;
	snprintf(e.path, sizeof(e.path), "/b");
	count = RM_syncedPut(list, count, 3, &e);
	CHECK(count == 3);
	CHECK_STR(list[0].path, "/b");
	CHECK_STR(list[1].path, "/d");
	CHECK_STR(list[2].path, "/c");
	CHECK(RM_syncedFind(list, count, "/b") == &list[0]);
	CHECK(RM_syncedFind(list, count, "/a") == NULL);

	char reply[256];
	CHECK(RM_syncedReply(1, 0, NULL, -1, 0, 0, reply, sizeof(reply)) > 0);
	CHECK_STR(reply, "server=1\n");
	list[0].size = 20;
	list[0].mtime = 30;
	CHECK(RM_syncedReply(1, 1, &list[0], 20, 30, 1, reply, sizeof(reply)) > 0);
	CHECK_STR(reply, "server=1\nsynced=1\nhow=download\nat=9\ngame=1\n");
	// changed since, gone, or never synced
	CHECK(RM_syncedReply(1, 1, &list[0], 21, 30, 1, reply, sizeof(reply)) > 0);
	CHECK_STR(reply, "server=1\nsynced=0\ngame=1\n");
	CHECK(RM_syncedReply(1, 1, &list[0], 20, 31, 0, reply, sizeof(reply)) > 0);
	CHECK_STR(reply, "server=1\nsynced=0\ngame=0\n");
	CHECK(RM_syncedReply(0, 1, &list[0], -1, 0, 0, reply, sizeof(reply)) > 0);
	CHECK_STR(reply, "server=0\nsynced=0\ngame=0\n");
	CHECK(RM_syncedReply(0, 1, NULL, 20, 30, 0, reply, sizeof(reply)) > 0);
	CHECK_STR(reply, "server=0\nsynced=0\ngame=0\n");
	CHECK(RM_syncedReply(1, 1, &list[0], 20, 30, 1, reply, 10) < 0);

	RMDownload d;
	CHECK(RM_downloadParse("1700000000\t/sd/Roms/Game Boy (GB)/A.gb\tA Game\n", &d) == 0);
	CHECK(d.at == 1700000000);
	CHECK_STR(d.path, "/sd/Roms/Game Boy (GB)/A.gb");
	CHECK_STR(d.name, "A Game");
	CHECK(RM_downloadParse("1\t/p\t", &d) != 0);
	CHECK(RM_downloadParse("1\t\tname", &d) != 0);
	CHECK(RM_downloadParse("x\t/p\tname", &d) != 0);
	CHECK(RM_downloadParse("1700000000\t/sd/Roms/Game Boy (GB)/A.gb\tA Game", &d) == 0);
	CHECK(RM_downloadFormat(&d, line, sizeof(line)) > 0);
	CHECK_STR(line, "1700000000\t/sd/Roms/Game Boy (GB)/A.gb\tA Game\n");

	RMDownload log[2];
	count = 0;
	const char* games[] = { "/gone.gb", "/one.gb", "/two.gb" };
	for (int i = 0; i < 3; i++) {
		memset(&d, 0, sizeof(d));
		d.at = 100 + i;
		snprintf(d.path, sizeof(d.path), "%s", games[i]);
		snprintf(d.name, sizeof(d.name), "Game %d", i);
		count = RM_downloadPut(log, count, 2, &d);
	}
	CHECK(count == 2);
	CHECK_STR(log[0].path, "/two.gb");
	CHECK_STR(log[1].path, "/one.gb");
	char big[4096];
	CHECK(RM_downloadsReply(log, count, 5, NULL, big, sizeof(big)) > 0);
	CHECK_STR(big, "count=2\npath.0=/two.gb\nname.0=Game 2\nat.0=102\npath.1=/one.gb\nname.1=Game 1\nat.1=101\n");
	CHECK(RM_downloadsReply(log, count, 1, NULL, big, sizeof(big)) > 0);
	CHECK_STR(big, "count=1\npath.0=/two.gb\nname.0=Game 2\nat.0=102\n");
	// the ones gone from the card are left out
	snprintf(log[0].path, sizeof(log[0].path), "/gone.gb");
	CHECK(RM_downloadsReply(log, count, 5, keepAllBut, big, sizeof(big)) > 0);
	CHECK_STR(big, "count=1\npath.0=/one.gb\nname.0=Game 1\nat.0=101\n");
	CHECK(RM_downloadsReply(log, 0, 5, NULL, big, sizeof(big)) > 0);
	CHECK_STR(big, "count=0\n");
	// as many as fit
	CHECK(RM_downloadsReply(log, count, 5, NULL, big, 80) > 0);
	CHECK_STR(big, "count=1\npath.0=/gone.gb\nname.0=Game 2\nat.0=102\n");

	char payload[1100];
	CHECK(RM_downloadPayload(&log[1], payload, sizeof(payload)) > 0);
	CHECK_STR(payload, "path=/one.gb\nname=Game 1");
	memset(&d, 0, sizeof(d));
	memset(d.path, 'p', 1000);
	snprintf(d.name, sizeof(d.name), "%s", "A long name that doesn't fit");
	CHECK(RM_downloadPayload(&d, payload, sizeof(payload)) == 1005);
	CHECK(strstr(payload, "name=") == NULL);
	memset(d.path, 'p', 1020);
	CHECK(RM_downloadPayload(&d, payload, sizeof(payload)) < 0);
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
	testZipChecks();
	testDiscs();
	testStates();
	testSynced();
	testUnpack(dir);
	testClient(dir);
	testLive();
	rmdir(dir);
	CHECK_DONE();
}
