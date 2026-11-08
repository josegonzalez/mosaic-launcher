// raprogress.c: the worker, on a fixture card with minarch's .ra files: a
// game's progress once its hash is worked out, the hash cache file kept by
// path, size and modification time, and what's left unknown or empty.

#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "check.h"
#include "raprogress.h"

#ifndef TEST_DIR
#error "build with -DTEST_DIR=\"...\""
#endif

#define CARD TEST_DIR "/card"
#define ROMS CARD "/Roms"
#define RA CARD "/.userdata/shared/.ra"
#define CACHE CARD "/.userdata/shared/launcher/ra-hashes.txt"
#define GB_HASH "cbf8d240a8f9acf61c9b6385d47c707c" // test_rahash's Plain.gb

static int hashed = 0, from_log = 0;
static char last_log[1024];

static void onLog(const char* message) {
	snprintf(last_log, sizeof(last_log), "%s", message);
	if (strstr(message, ", hashed as console")) hashed++;
	if (strstr(message, "ignoring")) from_log++;
}

static uint8_t pattern(size_t i, int seed) {
	return (uint8_t)((i * 131 + (size_t)seed * 17 + (i >> 8)) ^ 0x5A);
}

static void mkdirs(const char* path) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char* p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0755);
		*p = '/';
	}
	mkdir(dir, 0755);
}

static void writeBytes(const char* path, const void* data, size_t size) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	*strrchr(dir, '/') = '\0';
	mkdirs(dir);
	FILE* f = fopen(path, "wb");
	CHECK(f != NULL);
	if (!f) return;
	fwrite(data, 1, size, f);
	fclose(f);
}

static void writeText(const char* path, const char* text) {
	writeBytes(path, text, strlen(text));
}

static void writeRom(const char* path, size_t size, int seed) {
	uint8_t* data = malloc(size);
	for (size_t i = 0; i < size; i++) data[i] = pattern(i, seed);
	writeBytes(path, data, size);
	free(data);
}

static void writeFramed(const char* path, const char* json) {
	size_t n = strlen(json);
	uint8_t* out = malloc(4 + n + 32);
	out[0] = (uint8_t)n;
	out[1] = (uint8_t)(n >> 8);
	out[2] = (uint8_t)(n >> 16);
	out[3] = (uint8_t)(n >> 24);
	memcpy(out + 4, json, n);
	RA_sha256(json, n, out + 4 + n);
	writeBytes(path, out, 4 + n + 32);
	free(out);
}

static void writeLedger(void) {
	uint8_t rec[RA_LEDGER_RECORD_SIZE];
	memset(rec, 0, sizeof(rec));
	rec[0] = RA_LEDGER_UNLOCK;
	rec[9] = 3; // achievement 3, made offline in softcore
	memcpy(rec + 14, GB_HASH, 32);
	RA_sha256(rec, 79, rec + 79);
	writeBytes(RA "/offline/ledger.bin", rec, sizeof(rec));
}

static long long millis(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Asks until there's an answer, as a plugin asking every frame does.
static int waitFor(const char* path, RASummary* out) {
	long long until = millis() + 10000;
	int rc;
	while ((rc = RAProgress_get(path, out)) == RA_PROGRESS_UNKNOWN && millis() < until) usleep(5000);
	return rc;
}

static void setUp(void) {
	writeRom(ROMS "/Game Boy (GB)/Plain.gb", 32768, 1);
	writeRom(ROMS "/Game Boy (GB)/Other.gb", 4096, 2);
	writeRom(ROMS "/Atari 5200 (A5200)/Game.a52", 4096, 3);
	// a folder with its own m3u, played from its first disc
	writeRom(ROMS "/Game Boy (GB)/Multi/Plain copy.gb", 32768, 1);
	writeText(ROMS "/Game Boy (GB)/Multi/Multi.m3u", "\nPlain copy.gb\nnot this.gb\n");

	writeFramed(RA "/offline/cache/achievementsets_" GB_HASH ".bin",
		"{\"Success\":true,\"GameId\":42,\"Sets\":[{\"Achievements\":["
		"{\"ID\":1,\"Flags\":3,\"Points\":5},{\"ID\":2,\"Flags\":3,\"Points\":10},"
		"{\"ID\":3,\"Flags\":3,\"Points\":25},{\"ID\":4,\"Flags\":3,\"Points\":50},{\"ID\":9,\"Flags\":5,\"Points\":1}]}]}");
	writeFramed(RA "/offline/cache/startsession_" GB_HASH ".bin",
		"{\"Success\":true,\"HardcoreUnlocks\":[{\"ID\":1,\"When\":1}],\"Unlocks\":[{\"ID\":2,\"When\":2}]}");
	writeLedger();
	writeText(RA "/muted/" GB_HASH ".txt", "4\n");
}

static void testBeforeConfigure(void) {
	RASummary s;
	CHECK(RAProgress_get(ROMS "/Game Boy (GB)/Plain.gb", &s) == RA_PROGRESS_UNKNOWN);
	CHECK(RAProgress_news() == 0);
}

static void testProgress(void) {
	RAProgress_configure(CARD, ROMS, RA, CACHE, onLog);
	RASummary s;
	memset(&s, 0, sizeof(s));
	// unknown at first: the worker has it now
	CHECK(RAProgress_get(ROMS "/Game Boy (GB)/Plain.gb", &s) == RA_PROGRESS_UNKNOWN);
	CHECK(waitFor(ROMS "/Game Boy (GB)/Plain.gb", &s) == 1);
	CHECK(RAProgress_news() == 1);
	CHECK(RAProgress_news() == 0);
	CHECK(s.total == 4);
	CHECK(s.unlocked == 3); // 1 and 2 from the session, 3 from the ledger
	CHECK(s.unlocked_hardcore == 1);
	CHECK(s.points == 90 && s.points_unlocked == 40);
	CHECK(s.muted == 1);
	CHECK(s.game_id == 42);
	char hash[40];
	CHECK(RAProgress_hash(ROMS "/Game Boy (GB)/Plain.gb", hash, sizeof(hash)) == 1);
	CHECK_STR(hash, GB_HASH);

	// hashed, but nothing on the card for it
	CHECK(waitFor(ROMS "/Game Boy (GB)/Other.gb", &s) == 0);
	CHECK(RAProgress_hash(ROMS "/Game Boy (GB)/Other.gb", hash, sizeof(hash)) == 1);
	// rcheevos can't hash it
	CHECK(waitFor(ROMS "/Atari 5200 (A5200)/Game.a52", &s) == 0);
	CHECK(RAProgress_hash(ROMS "/Atari 5200 (A5200)/Game.a52", hash, sizeof(hash)) == 0);
	// not there
	CHECK(waitFor(ROMS "/Game Boy (GB)/Missing.gb", &s) == 0);
	// the folder, as its first disc
	CHECK(waitFor(ROMS "/Game Boy (GB)/Multi", &s) == 1);
	CHECK(s.total == 4);
	CHECK(waitFor(ROMS "/Game Boy (GB)/Multi/Multi.m3u", &s) == 1);
	CHECK(hashed == 4); // Plain, Other, Game.a52, Plain copy: once each
	RAProgress_quit();

	// each line of the cache, relative to the card
	FILE* f = fopen(CACHE, "r");
	CHECK(f != NULL);
	char text[2048] = "";
	if (f) {
		size_t n = fread(text, 1, sizeof(text) - 1, f);
		text[n] = '\0';
		fclose(f);
	}
	CHECK(strstr(text, GB_HASH "\t32768\t") != NULL);
	CHECK(strstr(text, "\tRoms/Game Boy (GB)/Plain.gb\n") != NULL);
	CHECK(strstr(text, "-\t4096\t") != NULL);
	CHECK(strstr(text, "\tRoms/Game Boy (GB)/Multi/Plain copy.gb\n") != NULL);
	CHECK(strstr(text, "Missing") == NULL);
}

static void testCached(void) {
	// a restart reads the hashes back instead
	hashed = 0;
	RAProgress_configure(CARD, ROMS, RA, CACHE, onLog);
	RASummary s;
	CHECK(waitFor(ROMS "/Game Boy (GB)/Plain.gb", &s) == 1 && s.unlocked == 3);
	CHECK(waitFor(ROMS "/Atari 5200 (A5200)/Game.a52", &s) == 0);
	CHECK(hashed == 0);
	RAProgress_quit();

	// a changed file is hashed again
	writeRom(ROMS "/Game Boy (GB)/Plain.gb", 32768, 5);
	struct timeval times[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
	CHECK(utimes(ROMS "/Game Boy (GB)/Plain.gb", times) == 0);
	RAProgress_configure(CARD, ROMS, RA, CACHE, onLog);
	CHECK(waitFor(ROMS "/Game Boy (GB)/Plain.gb", &s) == 0);
	CHECK(hashed == 1);
	RAProgress_quit();
	writeRom(ROMS "/Game Boy (GB)/Plain.gb", 32768, 1);
}

static void testDamaged(void) {
	// a cache file whose checksum is wrong says nothing
	FILE* f = fopen(RA "/offline/cache/achievementsets_" GB_HASH ".bin", "r+b");
	CHECK(f != NULL);
	if (!f) return;
	fseek(f, 10, SEEK_SET);
	fputc('#', f);
	fclose(f);
	from_log = 0;
	RAProgress_configure(CARD, ROMS, RA, CACHE, onLog);
	RASummary s;
	CHECK(waitFor(ROMS "/Game Boy (GB)/Plain.gb", &s) == 0);
	CHECK(from_log >= 1);
	RAProgress_quit();

	// a damaged hash cache line is passed over, and the game hashed again
	writeText(CACHE, "garbage\nnot\ta\tline\n");
	hashed = 0;
	RAProgress_configure(CARD, ROMS, RA, CACHE, onLog);
	CHECK(waitFor(ROMS "/Game Boy (GB)/Other.gb", &s) == 0);
	CHECK(hashed == 1);
	RAProgress_quit();
}

static void testUnconfigured(void) {
	// no .ra folder named: nothing runs
	RAProgress_configure(CARD, ROMS, "", CACHE, onLog);
	RASummary s;
	CHECK(RAProgress_get(ROMS "/Game Boy (GB)/Plain.gb", &s) == RA_PROGRESS_UNKNOWN);
	usleep(20000);
	CHECK(RAProgress_get(ROMS "/Game Boy (GB)/Plain.gb", &s) == RA_PROGRESS_UNKNOWN);
	CHECK(RAProgress_get(NULL, &s) == RA_PROGRESS_UNKNOWN);
	RAProgress_quit();
}

int main(void) {
	char cmd[600];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", TEST_DIR);
	CHECK(system(cmd) == 0);
	setUp();
	testBeforeConfigure();
	testProgress();
	testCached();
	testDamaged();
	testUnconfigured();
	CHECK_DONE();
}
