// ramodel.c: consoles by tag, SHA-256, minarch's framed cache files, the
// JSON in them, its unlock ledger and muted lists, and the hash cache lines.

#include <stdlib.h>
#include "check.h"
#include "ramodel.h"

static void hex(const uint8_t* digest, char out[65]) {
	for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
}

static void testSha256(void) {
	uint8_t digest[32];
	char text[65];
	RA_sha256("", 0, digest);
	hex(digest, text);
	CHECK_STR(text, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	RA_sha256("abc", 3, digest);
	hex(digest, text);
	CHECK_STR(text, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	RA_sha256(two, strlen(two), digest);
	hex(digest, text);
	CHECK_STR(text, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	// fed in pieces, a million a's
	RASha256 ctx;
	RA_sha256Init(&ctx);
	char a[1000];
	memset(a, 'a', sizeof(a));
	for (int i = 0; i < 1000; i++) RA_sha256Update(&ctx, a, sizeof(a));
	RA_sha256Final(&ctx, digest);
	hex(digest, text);
	CHECK_STR(text, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// [u32 length][body][sha256 of body]
static uint8_t* frame(const char* body, size_t* size) {
	size_t n = strlen(body);
	uint8_t* out = malloc(4 + n + 32);
	out[0] = (uint8_t)n;
	out[1] = (uint8_t)(n >> 8);
	out[2] = (uint8_t)(n >> 16);
	out[3] = (uint8_t)(n >> 24);
	memcpy(out + 4, body, n);
	RA_sha256(body, n, out + 4 + n);
	*size = 4 + n + 32;
	return out;
}

static void testUnframe(void) {
	size_t size;
	uint8_t* data = frame("{\"Success\":true}", &size);
	const uint8_t* body;
	size_t body_size;
	CHECK(RA_unframe(data, size, &body, &body_size) == 1);
	CHECK(body_size == 16 && !memcmp(body, "{\"Success\":true}", 16));
	// one byte too many or too few
	CHECK(RA_unframe(data, size - 1, &body, &body_size) == 0);
	uint8_t* longer = malloc(size + 1);
	memcpy(longer, data, size);
	longer[size] = 0;
	CHECK(RA_unframe(longer, size + 1, &body, &body_size) == 0);
	free(longer);
	// a changed byte
	data[6] ^= 1;
	CHECK(RA_unframe(data, size, &body, &body_size) == 0);
	data[6] ^= 1;
	// a length past the cap
	data[3] = 0x7F;
	CHECK(RA_unframe(data, size, &body, &body_size) == 0);
	CHECK(RA_unframe(data, 10, &body, &body_size) == 0);
	CHECK(RA_unframe(NULL, 0, &body, &body_size) == 0);
	free(data);
}

static const char* SETS =
	"{\"Success\":true,\"GameId\":1234,\"Title\":\"Game \\\"One\\\"\",\"ImageIconUrl\":null,"
	"\"Sets\":[{\"AchievementSetId\":1,\"Type\":\"core\",\"Achievements\":["
	"{\"ID\":10,\"Title\":\"A\",\"Flags\":3,\"Points\":5,\"MemAddr\":\"0xH0=1\",\"Rarity\":12.5},"
	"{\"ID\":11,\"Flags\":3,\"Points\":10},"
	"{\"ID\":12,\"Flags\":5,\"Points\":25},"  // unofficial
	"{\"ID\":13,\"Flags\":3,\"Points\":1.5}," // not whole points
	"{\"ID\":-1,\"Flags\":3,\"Points\":5}],\"Leaderboards\":[{\"ID\":1}]},"
	"{\"AchievementSetId\":2,\"Type\":\"bonus\",\"Achievements\":[{\"ID\":20,\"Flags\":3,\"Points\":50},"
	"{\"ID\":11,\"Flags\":3,\"Points\":10}]}]}";

static void testSets(void) {
	RAGame game;
	RA_gameInit(&game);
	CHECK(RA_parseSets(SETS, strlen(SETS), &game) == 1);
	RA_gameFinish(&game);
	CHECK(game.game_id == 1234);
	CHECK(game.count == 4); // 10, 11, 13, 20: 11 once
	CHECK(game.set[0].id == 10 && game.set[0].points == 5);
	CHECK(game.set[2].id == 13 && game.set[2].points == 0);
	CHECK(game.set[3].id == 20 && game.set[3].points == 50);
	RA_gameFree(&game);

	// an older patch response
	const char* patch = "{\"Success\":true,\"PatchData\":{\"ID\":77,\"Title\":\"Old\",\"Achievements\":[{\"ID\":1,\"Flags\":3,\"Points\":2}]}}";
	RA_gameInit(&game);
	CHECK(RA_parseSets(patch, strlen(patch), &game) == 1);
	CHECK(game.game_id == 77 && game.count == 1);
	RA_gameFree(&game);

	// what says nothing
	const char* bad[] = {
		"{\"Success\":false,\"Error\":\"Unknown game\",\"Sets\":[{\"Achievements\":[{\"ID\":1,\"Flags\":3}]}]}",
		"{\"Success\":true,\"Sets\":[]}",
		"{\"Success\":true,\"Sets\":[{\"Achievements\":[{\"ID\":1,\"Flags\":5}]}]}",
		"{\"Success\":true,\"Sets\":[{\"Achievements\":[{\"ID\":1,\"Flags\":3}]}]", // cut short
		"{\"Success\":true,\"Sets\":[{\"Achievements\":[{\"ID\":1,\"Flags\":3,}]}]}", // trailing comma
		"{\"Success\":true} trailing",
		"[1,2,3]",
		"",
		"{\"Sets\":\"x\",\"Achievements\":[{\"ID\":1,\"Flags\":3}]}", // not where a set's achievements are
		"{\"a\":\"\x01\"}",
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		RA_gameInit(&game);
		CHECK(RA_parseSets(bad[i], strlen(bad[i]), &game) == 0);
		CHECK(game.count == 0);
		RA_gameFree(&game);
	}
	// nested too deep
	char deep[200];
	int n = 0;
	n += snprintf(deep + n, sizeof(deep) - (size_t)n, "{\"x\":");
	for (int i = 0; i < 40; i++) deep[n++] = '[';
	for (int i = 0; i < 40; i++) deep[n++] = ']';
	deep[n++] = '}';
	deep[n] = '\0';
	RA_gameInit(&game);
	CHECK(RA_parseSets(deep, strlen(deep), &game) == 0);
	RA_gameFree(&game);
	// a length short of the text is what counts
	RA_gameInit(&game);
	CHECK(RA_parseSets(SETS, strlen(SETS) - 1, &game) == 0);
	RA_gameFree(&game);
}

static void testSession(void) {
	RAGame game;
	RA_gameInit(&game);
	const char* json = "{\"Success\":true,\"HardcoreUnlocks\":[{\"ID\":10,\"When\":1700000000}],"
		"\"Unlocks\":[{\"ID\":11,\"When\":1700000001},{\"ID\":10,\"When\":1}],\"ServerNow\":1700000002}";
	CHECK(RA_parseSession(json, strlen(json), &game) == 1);
	RA_gameFinish(&game);
	CHECK(game.unlocked_count == 2 && game.unlocked[0] == 10 && game.unlocked[1] == 11);
	CHECK(game.hardcore_count == 1 && game.hardcore[0] == 10);
	RA_gameFree(&game);

	RA_gameInit(&game);
	const char* failed = "{\"Success\":false,\"Unlocks\":[{\"ID\":11}]}";
	CHECK(RA_parseSession(failed, strlen(failed), &game) == 0);
	CHECK(game.unlocked_count == 0);
	const char* broken = "{\"Success\":true,\"Unlocks\":[{\"ID\":11}";
	CHECK(RA_parseSession(broken, strlen(broken), &game) == 0);
	CHECK(game.unlocked_count == 0);
	// none yet is fine
	const char* none = "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}";
	CHECK(RA_parseSession(none, strlen(none), &game) == 1);
	RA_gameFree(&game);
}

#define HASH_A "0123456789abcdef0123456789abcdef"
#define HASH_B "fedcba9876543210fedcba9876543210"

// minarch's packed record, with its own SHA-256
static void record(uint8_t* rec, int type, uint32_t achievement, int hardcore, const char* hash) {
	memset(rec, 0, RA_LEDGER_RECORD_SIZE);
	rec[0] = (uint8_t)type;
	rec[1] = 0x10; // timestamp
	rec[5] = 7;    // game id
	rec[9] = (uint8_t)achievement;
	rec[10] = (uint8_t)(achievement >> 8);
	rec[13] = (uint8_t)hardcore;
	memcpy(rec + 14, hash, 32);
	RA_sha256(rec, 79, rec + 79);
}

static void testLedger(void) {
	uint8_t ledger[RA_LEDGER_RECORD_SIZE * 7];
	uint8_t* r = ledger;
	record(r, RA_LEDGER_SESSION_START, 0, 0, HASH_A);
	record(r += RA_LEDGER_RECORD_SIZE, RA_LEDGER_UNLOCK, 11, 0, HASH_A);
	record(r += RA_LEDGER_RECORD_SIZE, RA_LEDGER_UNLOCK, 300, 1, HASH_A);
	record(r += RA_LEDGER_RECORD_SIZE, RA_LEDGER_UNLOCK, 12, 0, HASH_B); // another game
	record(r += RA_LEDGER_RECORD_SIZE, RA_LEDGER_UNLOCK, 13, 0, HASH_A);
	r[50] ^= 1; // its checksum no longer matches
	record(r += RA_LEDGER_RECORD_SIZE, RA_LEDGER_SYNC_ACK, 11, 0, HASH_A);
	record(r += RA_LEDGER_RECORD_SIZE, RA_LEDGER_SESSION_END, 0, 0, HASH_A);

	RAGame game;
	RA_gameInit(&game);
	CHECK(RA_parseLedger(ledger, sizeof(ledger), HASH_A, &game) == 2);
	RA_gameFinish(&game);
	CHECK(game.unlocked_count == 2 && game.unlocked[0] == 11 && game.unlocked[1] == 300);
	CHECK(game.hardcore_count == 1 && game.hardcore[0] == 300);
	// not whole records, or no hash to look for
	CHECK(RA_parseLedger(ledger, sizeof(ledger) - 1, HASH_A, &game) == -1);
	CHECK(RA_parseLedger(ledger, sizeof(ledger), "short", &game) == -1);
	CHECK(RA_parseLedger(ledger, 0, HASH_A, &game) == 0);
	RA_gameFree(&game);
}

static void testMuted(void) {
	RAGame game;
	RA_gameInit(&game);
	const char* text = "11\n\n0\nnot a number\n  20\r\n99999999999\n12";
	CHECK(RA_parseMuted(text, strlen(text), &game) == 3);
	RA_gameFinish(&game);
	CHECK(game.muted_count == 3 && game.muted[0] == 11 && game.muted[1] == 12 && game.muted[2] == 20);
	RA_gameFree(&game);
}

static void testSummary(void) {
	RAGame game;
	RA_gameInit(&game);
	CHECK(RA_parseSets(SETS, strlen(SETS), &game) == 1);
	const char* json = "{\"Success\":true,\"HardcoreUnlocks\":[{\"ID\":20}],\"Unlocks\":[{\"ID\":10},{\"ID\":99}]}";
	CHECK(RA_parseSession(json, strlen(json), &game) == 1);
	const char* muted = "13\n12\n";
	RA_parseMuted(muted, strlen(muted), &game);
	RA_gameFinish(&game);
	RASummary s;
	RA_summarize(&game, &s);
	CHECK(s.total == 4);
	CHECK(s.unlocked == 2); // 10, 20; 99 isn't in the set
	CHECK(s.unlocked_hardcore == 1);
	CHECK(s.points == 65);
	CHECK(s.points_unlocked == 55);
	CHECK(s.muted == 1); // 12 is unofficial
	CHECK(s.game_id == 1234);
	RA_gameFree(&game);
}

static void testConsoles(void) {
	CHECK(RA_consoleFor("GB", "/Roms/Game Boy (GB)/a.gb") == RC_CONSOLE_GAMEBOY);
	CHECK(RA_consoleFor("SGB", "a.gb") == RC_CONSOLE_GAMEBOY);
	CHECK(RA_consoleFor("FBN", "a.zip") == RC_CONSOLE_ARCADE);
	CHECK(RA_consoleFor("PS", "a.chd") == RC_CONSOLE_PLAYSTATION);
	CHECK(RA_consoleFor("MD", "a.md") == RC_CONSOLE_MEGA_DRIVE);
	CHECK(RA_consoleFor("MD", "a.CHD") == RC_CONSOLE_SEGA_CD);
	CHECK(RA_consoleFor("MD", "a.m3u") == RC_CONSOLE_SEGA_CD);
	CHECK(RA_consoleFor("MD", "a.iso") == RC_CONSOLE_MEGA_DRIVE);
	CHECK(RA_consoleFor("PCE", "a.pce") == RC_CONSOLE_PC_ENGINE);
	CHECK(RA_consoleFor("PCE", "a.cue") == RC_CONSOLE_PC_ENGINE_CD);
	CHECK(RA_consoleFor("PCE", "a.toc") == RC_CONSOLE_PC_ENGINE_CD);
	CHECK(RA_consoleFor("PRBOOM", "a.wad") == RC_CONSOLE_UNKNOWN);
	CHECK(RA_consoleFor("NOPE", "a.gb") == RC_CONSOLE_UNKNOWN);
	CHECK(RA_consoleFor("", "a.gb") == RC_CONSOLE_UNKNOWN);
	CHECK(RA_consoleFor(NULL, "a.gb") == RC_CONSOLE_UNKNOWN);
	CHECK(RA_consoleFor("gb", "a.gb") == RC_CONSOLE_UNKNOWN); // tags are matched exactly

	char tag[32];
	CHECK(RA_tagOf("/sd/Roms", "/sd/Roms/Game Boy (GB)/Tetris (World).gb", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "GB");
	CHECK(RA_tagOf("/sd/Roms", "/sd/Roms/Sony (PS)/Game (Disc 1)/Game (Disc 1).cue", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "PS");
	CHECK(RA_tagOf("/sd/Roms", "/sd/Roms/Odd (x) (MD)/a.md", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "MD");
	CHECK(RA_tagOf("/sd/Roms", "/sd/Roms/Plain/a.md", tag, sizeof(tag)) == 1);
	CHECK_STR(tag, "Plain");
	CHECK(RA_tagOf("/sd/Roms", "/sd/Roms/Open (MD/a.md", tag, sizeof(tag)) == 0);
	CHECK(RA_tagOf("/sd/Roms", NULL, tag, sizeof(tag)) == 0);
}

static void testCacheLines(void) {
	char line[256], hash[33], path[128];
	long long size, mtime;
	CHECK(RA_formatCacheLine(HASH_A, 1024, 1700000000, "Roms/Game Boy (GB)/a.gb", line, sizeof(line)) == 1);
	CHECK_STR(line, HASH_A "\t1024\t1700000000\tRoms/Game Boy (GB)/a.gb\n");
	CHECK(RA_parseCacheLine(line, hash, &size, &mtime, path, sizeof(path)) == 1);
	CHECK_STR(hash, HASH_A);
	CHECK(size == 1024 && mtime == 1700000000);
	CHECK_STR(path, "Roms/Game Boy (GB)/a.gb");
	CHECK(RA_parseCacheLine("-\t5\t6\tRoms/x.a52", hash, &size, &mtime, path, sizeof(path)) == 1);
	CHECK_STR(hash, "-");
	CHECK(RA_parseCacheLine("0123456789ABCDEF0123456789ABCDEF\t5\t6\tx\r\n", hash, &size, &mtime, path, sizeof(path)) == 1);
	CHECK_STR(hash, HASH_A);
	CHECK_STR(path, "x");
	const char* bad[] = {
		"", "# comment", HASH_A "\t5\t6\t", HASH_A "\t-5\t6\tx", HASH_A "\t5\tsix\tx", "abc\t5\t6\tx",
		HASH_A "\t5\t6\tx\ty", HASH_A "\t5\t6", HASH_A "5\t6\tx",
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(RA_parseCacheLine(bad[i], hash, &size, &mtime, path, sizeof(path)) == 0);
	CHECK(RA_formatCacheLine(HASH_A, 1, 1, "tab\there", line, sizeof(line)) == 0);
	CHECK(RA_formatCacheLine("nope", 1, 1, "x", line, sizeof(line)) == 0);
	CHECK(RA_formatCacheLine(HASH_A, 1, 1, "x", line, 10) == 0);
	CHECK(RA_isHash(HASH_A) == 1);
	CHECK(RA_isHash(HASH_A "0") == 0);
	CHECK(RA_isHash("0123456789abcdef0123456789abcdeg") == 0);
}

int main(void) {
	testSha256();
	testUnframe();
	testSets();
	testSession();
	testLedger();
	testMuted();
	testSummary();
	testConsoles();
	testCacheLines();
	CHECK_DONE();
}
