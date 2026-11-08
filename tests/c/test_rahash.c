// rahash.c and rachd.c: games hashed through rcheevos the way minarch has
// them hashed, against hashes worked out apart from rcheevos (MD5 of the
// bytes each rule takes, checked with Python's hashlib). Carts and zips are
// written here; the discs too, as bin and cue, and the same discs as CHD
// images are fixtures made from those with chdman:
//
//   build/tests/test_rahash --write-discs DIR
//   chdman createcd -i DIR/psx.cue -o tests/c/fixtures/ra/psx.chd
//   chdman createcd -i DIR/pce.cue -o tests/c/fixtures/ra/pce.chd
//   chdman createcd -i DIR/segacd.cue -o tests/c/fixtures/ra/segacd.chd

#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <zlib.h>
#include "check.h"
#include "rahash.h"
#include "ramodel.h"

#ifndef TEST_DIR
#error "build with -DTEST_DIR=\"...\""
#endif
#ifndef FIXTURES
#error "build with -DFIXTURES=\"...\""
#endif

#define ROMS TEST_DIR "/Roms"

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

static void parentDirs(const char* path) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	mkdirs(dir);
}

static void writeBytes(const char* path, const uint8_t* data, size_t size) {
	parentDirs(path);
	FILE* f = fopen(path, "wb");
	CHECK(f != NULL);
	if (!f) return;
	CHECK(fwrite(data, 1, size, f) == size);
	fclose(f);
}

// A cart: header bytes, then size bytes of pattern.
static void writeRom(const char* path, const uint8_t* header, size_t header_size, size_t size, int seed) {
	uint8_t* data = malloc(header_size + size);
	if (header_size) memcpy(data, header, header_size);
	for (size_t i = 0; i < size; i++) data[header_size + i] = pattern(i, seed);
	writeBytes(path, data, header_size + size);
	free(data);
}

static void hashOf(const char* path, const char* tag, const char* want) {
	char hash[33] = "x";
	int console = RA_consoleFor(tag, path);
	int ok = RAHash_game(path, console, NULL, NULL, hash);
	if (!want) {
		CHECK(ok == 0);
		CHECK_STR(hash, "");
		return;
	}
	CHECK(ok == 1);
	if (strcmp(hash, want)) fprintf(stderr, "%s (%s, console %i)\n", path, tag, console);
	CHECK_STR(hash, want);
}

static void testCarts(void) {
	// whole file
	writeRom(ROMS "/Game Boy (GB)/Plain.gb", NULL, 0, 32768, 1);
	hashOf(ROMS "/Game Boy (GB)/Plain.gb", "GB", "cbf8d240a8f9acf61c9b6385d47c707c");
	writeRom(ROMS "/Mega Drive (MD)/Plain.md", NULL, 0, 65536, 2);
	hashOf(ROMS "/Mega Drive (MD)/Plain.md", "MD", "ed0e047b2ab9f9cba98e809c20da0526");
	writeRom(ROMS "/Game Boy Advance (MGBA)/Plain.gba", NULL, 0, 4096, 3);
	hashOf(ROMS "/Game Boy Advance (MGBA)/Plain.gba", "MGBA", "00713548792eeac7c707d57a1c84074a");

	// NES: the iNES header is left out, and an FDS one
	uint8_t ines[16] = { 'N', 'E', 'S', 0x1a, 1, 1 };
	writeRom(ROMS "/Nintendo (FC)/Headered.nes", ines, sizeof(ines), 16384, 4);
	hashOf(ROMS "/Nintendo (FC)/Headered.nes", "FC", "35246526b227476bcf8dd832fcc3939b");
	writeRom(ROMS "/Nintendo (FC)/Bare.nes", NULL, 0, 16384, 4);
	hashOf(ROMS "/Nintendo (FC)/Bare.nes", "FC", "35246526b227476bcf8dd832fcc3939b");
	uint8_t fds[16] = { 'F', 'D', 'S', 0x1a, 1 };
	writeRom(ROMS "/Famicom Disk System (FDS)/Disk.fds", fds, sizeof(fds), 8192, 5);
	hashOf(ROMS "/Famicom Disk System (FDS)/Disk.fds", "FDS", "de8feb322b6d8ea3aaabb2c0ec805fdf");

	// SNES: a 512 byte copier header, found by size
	uint8_t copier[512] = { 0 };
	writeRom(ROMS "/Super Nintendo (SFC)/Headered.sfc", copier, sizeof(copier), 32768, 6);
	hashOf(ROMS "/Super Nintendo (SFC)/Headered.sfc", "SFC", "40cdf62b6b07f236a418e92a69f67a84");
	writeRom(ROMS "/Super Nintendo (SUPA)/Bare.sfc", NULL, 0, 32768, 6);
	hashOf(ROMS "/Super Nintendo (SUPA)/Bare.sfc", "SUPA", "40cdf62b6b07f236a418e92a69f67a84");

	// PC Engine: the same, by size & 512
	writeRom(ROMS "/PC Engine (PCE)/Headered.pce", copier, sizeof(copier), 8192, 7);
	hashOf(ROMS "/PC Engine (PCE)/Headered.pce", "PCE", "24161db703a3d85e893cf7a73e51a433");

	// Atari 7800: a 128 byte header saying ATARI7800
	uint8_t a78[128] = { 1, 'A', 'T', 'A', 'R', 'I', '7', '8', '0', '0' };
	writeRom(ROMS "/Atari 7800 (A7800)/Headered.a78", a78, sizeof(a78), 16384, 8);
	hashOf(ROMS "/Atari 7800 (A7800)/Headered.a78", "A7800", "e44189e32455a2d3f3cbf1f80ef746a1");

	// Lynx: a 64 byte LYNX header
	uint8_t lynx[64] = { 'L', 'Y', 'N', 'X', 0 };
	writeRom(ROMS "/Atari Lynx (LYNX)/Headered.lnx", lynx, sizeof(lynx), 8192, 9);
	hashOf(ROMS "/Atari Lynx (LYNX)/Headered.lnx", "LYNX", "db5ea97d998add8c8fb53bca74d6afa6");

	// arcade games hash by name, without the extension
	writeRom(ROMS "/Arcade (FBN)/sf2.zip", NULL, 0, 100, 10);
	hashOf(ROMS "/Arcade (FBN)/sf2.zip", "FBN", "a7946d9b269b67f7a6b1148d3f5d8155");

	// minarch maps these, but rcheevos can't hash them
	writeRom(ROMS "/Atari 5200 (A5200)/Game.a52", NULL, 0, 4096, 11);
	hashOf(ROMS "/Atari 5200 (A5200)/Game.a52", "A5200", NULL);
	writeRom(ROMS "/Pico-8 (P8)/Game.p8.png", NULL, 0, 4096, 11);
	hashOf(ROMS "/Pico-8 (P8)/Game.p8.png", "P8", NULL);

	// a file that isn't there
	hashOf(ROMS "/Game Boy (GB)/Missing.gb", "GB", NULL);
}

///////////////////////////////////////
// Zips, written with zlib

static void put16(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t* p, uint32_t v) {
	put16(p, v & 0xFFFF);
	put16(p + 2, v >> 16);
}

typedef struct ZipFile {
	const char* name;
	const uint8_t* data;
	uint32_t size;
	int deflate;
} ZipFile;

static void writeZip(const char* path, const ZipFile* files, int count) {
	size_t cap = 1 << 20, used = 0;
	uint8_t* out = calloc(1, cap);
	uint8_t central[4096];
	size_t central_used = 0;
	for (int i = 0; i < count; i++) {
		const ZipFile* z = &files[i];
		uint32_t crc = (uint32_t)crc32(0, z->data, z->size);
		uint8_t* packed = malloc(z->size + 1024);
		uint32_t packed_size = z->size;
		if (z->deflate) {
			z_stream s;
			memset(&s, 0, sizeof(s));
			deflateInit2(&s, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
			s.next_in = (Bytef*)z->data;
			s.avail_in = z->size;
			s.next_out = packed;
			s.avail_out = z->size + 1024;
			CHECK(deflate(&s, Z_FINISH) == Z_STREAM_END);
			packed_size = (uint32_t)s.total_out;
			deflateEnd(&s);
		}
		else if (z->size) memcpy(packed, z->data, z->size);
		uint32_t name_len = (uint32_t)strlen(z->name);
		uint8_t* local = out + used;
		put32(local, 0x04034b50);
		put16(local + 4, 20);
		put16(local + 8, z->deflate ? 8 : 0);
		put32(local + 14, crc);
		put32(local + 18, packed_size);
		put32(local + 22, z->size);
		put16(local + 26, name_len);
		memcpy(local + 30, z->name, name_len);
		uint8_t* c = central + central_used;
		memset(c, 0, 46);
		put32(c, 0x02014b50);
		put16(c + 4, 20);
		put16(c + 6, 20);
		put16(c + 10, z->deflate ? 8 : 0);
		put32(c + 16, crc);
		put32(c + 20, packed_size);
		put32(c + 24, z->size);
		put16(c + 28, name_len);
		put32(c + 42, (uint32_t)used);
		memcpy(c + 46, z->name, name_len);
		central_used += 46 + name_len;
		used += 30 + name_len;
		memcpy(out + used, packed, packed_size);
		used += packed_size;
		free(packed);
	}
	uint32_t central_at = (uint32_t)used;
	memcpy(out + used, central, central_used);
	used += central_used;
	uint8_t* end = out + used;
	memset(end, 0, 22);
	put32(end, 0x06054b50);
	put16(end + 8, (uint32_t)count);
	put16(end + 10, (uint32_t)count);
	put32(end + 12, (uint32_t)central_used);
	put32(end + 16, central_at);
	used += 22;
	writeBytes(path, out, used);
	free(out);
}

static void testZips(void) {
	uint8_t* rom = malloc(32768);
	for (size_t i = 0; i < 32768; i++) rom[i] = pattern(i, 1);
	uint8_t other[16] = "not the game";
	// the first file decides; folders are passed over
	ZipFile deflated[] = {
		{ "folder/", (const uint8_t*)"", 0, 0 },
		{ "folder/Plain.gb", rom, 32768, 1 },
		{ "readme.txt", other, sizeof(other), 1 },
	};
	writeZip(ROMS "/Game Boy (GB)/Zipped.zip", deflated, 3);
	// the same game as Plain.gb above
	hashOf(ROMS "/Game Boy (GB)/Zipped.zip", "GB", "cbf8d240a8f9acf61c9b6385d47c707c");

	ZipFile stored[] = { { "Plain.gb", rom, 32768, 0 } };
	writeZip(ROMS "/Game Boy (GB)/Stored.zip", stored, 1);
	hashOf(ROMS "/Game Boy (GB)/Stored.zip", "GB", "cbf8d240a8f9acf61c9b6385d47c707c");

	// the header rules apply to what's inside
	uint8_t* nes = malloc(16 + 16384);
	uint8_t ines[16] = { 'N', 'E', 'S', 0x1a, 1, 1 };
	memcpy(nes, ines, 16);
	for (size_t i = 0; i < 16384; i++) nes[16 + i] = pattern(i, 4);
	ZipFile headered[] = { { "Headered.nes", nes, 16 + 16384, 1 } };
	writeZip(ROMS "/Nintendo (FC)/Zipped.zip", headered, 1);
	hashOf(ROMS "/Nintendo (FC)/Zipped.zip", "FC", "35246526b227476bcf8dd832fcc3939b");

	uint8_t* data = NULL;
	size_t size = 0;
	char name[64];
	CHECK(RAHash_unzipFirst(ROMS "/Game Boy (GB)/Zipped.zip", &data, &size, name, sizeof(name)) == 1);
	CHECK(size == 32768 && data && !memcmp(data, rom, size));
	CHECK_STR(name, "Plain.gb");
	free(data);

	// damaged: cut short
	FILE* f = fopen(ROMS "/Game Boy (GB)/Zipped.zip", "rb");
	uint8_t buf[4096];
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	writeBytes(ROMS "/Game Boy (GB)/Cut.zip", buf, n / 2);
	hashOf(ROMS "/Game Boy (GB)/Cut.zip", "GB", NULL);
	CHECK(RAHash_unzipFirst(ROMS "/Game Boy (GB)/Cut.zip", &data, &size, name, sizeof(name)) == 0);
	CHECK(data == NULL);
	// only folders
	ZipFile empty[] = { { "folder/", (const uint8_t*)"", 0, 0 } };
	writeZip(ROMS "/Game Boy (GB)/Empty.zip", empty, 1);
	hashOf(ROMS "/Game Boy (GB)/Empty.zip", "GB", NULL);

	free(rom);
	free(nes);
}

///////////////////////////////////////
// Discs

typedef struct Disc {
	uint8_t* data;
	size_t size;
} Disc;

static int bcd(int v) {
	return (v / 10) << 4 | (v % 10);
}

// One raw 2352 byte sector holding data (2048 bytes) at lba, in mode 1 or
// mode 2 form 1. The error correction bytes are left zero.
static void rawSector(uint8_t* out, int lba, int mode, const uint8_t* data) {
	memset(out, 0, 2352);
	out[0] = 0x00;
	memset(out + 1, 0xFF, 10);
	out[11] = 0x00;
	int msf = lba + 150;
	out[12] = (uint8_t)bcd(msf / 75 / 60);
	out[13] = (uint8_t)bcd(msf / 75 % 60);
	out[14] = (uint8_t)bcd(msf % 75);
	out[15] = (uint8_t)mode;
	memcpy(out + (mode == 2 ? 24 : 16), data, 2048);
}

// A directory record, written at p. Returns its length.
static int dirRecord(uint8_t* p, const char* name, int name_len, uint32_t lba, uint32_t size, int dir) {
	int length = 33 + name_len + (name_len % 2 == 0);
	memset(p, 0, (size_t)length);
	p[0] = (uint8_t)length;
	put32(p + 2, lba);
	for (int i = 0; i < 4; i++) p[6 + i] = (uint8_t)(lba >> (24 - 8 * i));
	put32(p + 10, size);
	for (int i = 0; i < 4; i++) p[14 + i] = (uint8_t)(size >> (24 - 8 * i));
	p[25] = dir ? 2 : 0;
	p[28] = 1;
	p[31] = 1;
	p[32] = (uint8_t)name_len;
	memcpy(p + 33, name, (size_t)name_len);
	return length;
}

#define ISO_SECTORS 24

// An ISO 9660 image of 2048 byte sectors: SYSTEM.CNF booting SLUS_123.45,
// a PS-X EXE of 4096 bytes after its 2048 byte header.
static void psxImage(uint8_t* iso) {
	memset(iso, 0, ISO_SECTORS * 2048);
	uint8_t* pvd = iso + 16 * 2048;
	pvd[0] = 1;
	memcpy(pvd + 1, "CD001", 5);
	pvd[6] = 1;
	put16(pvd + 128, 2048);
	dirRecord(pvd + 156, "\0", 1, 18, 2048, 1);
	uint8_t* end = iso + 17 * 2048;
	end[0] = 255;
	memcpy(end + 1, "CD001", 5);
	end[6] = 1;
	const char* cnf = "BOOT = cdrom:\\SLUS_123.45;1\r\nTCB = 4\r\nEVENT = 10\r\n";
	uint8_t* root = iso + 18 * 2048;
	int at = 0;
	at += dirRecord(root + at, "\0", 1, 18, 2048, 1);
	at += dirRecord(root + at, "\1", 1, 18, 2048, 1);
	at += dirRecord(root + at, "SLUS_123.45;1", 13, 20, 2048 + 4096, 0);
	at += dirRecord(root + at, "SYSTEM.CNF;1", 12, 19, (uint32_t)strlen(cnf), 0);
	memcpy(iso + 19 * 2048, cnf, strlen(cnf));
	uint8_t* exe = iso + 20 * 2048;
	memcpy(exe, "PS-X EXE", 8);
	put32(exe + 28, 4096);
	for (size_t i = 16; i < 2048 + 4096; i++)
		if (i != 28 && i != 29 && i != 30 && i != 31) exe[i] = pattern(i, 20);
}

static void writeText(const char* path, const char* text) {
	writeBytes(path, (const uint8_t*)text, strlen(text));
}

static void writeDiscs(const char* dir) {
	mkdirs(dir);
	char path[512];
	// PlayStation: one MODE2/2352 track
	uint8_t* iso = malloc(ISO_SECTORS * 2048);
	psxImage(iso);
	uint8_t* bin = malloc(ISO_SECTORS * 2352);
	for (int s = 0; s < ISO_SECTORS; s++) rawSector(bin + s * 2352, s, 2, iso + s * 2048);
	snprintf(path, sizeof(path), "%s/psx.bin", dir);
	writeBytes(path, bin, ISO_SECTORS * 2352);
	snprintf(path, sizeof(path), "%s/psx.cue", dir);
	writeText(path, "FILE \"psx.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
	free(bin);
	free(iso);

	// PC Engine CD: an audio track, then the data track, whose second
	// sector names 2 program sectors from sector 2
	enum { AUDIO = 6, DATA = 8 };
	bin = malloc((AUDIO + DATA) * 2352);
	for (int i = 0; i < AUDIO * 2352; i++) bin[i] = pattern((size_t)i, 30);
	uint8_t sector[2048];
	for (int s = 0; s < DATA; s++) {
		memset(sector, 0, sizeof(sector));
		if (s == 1) {
			sector[2] = 2;
			sector[3] = 2;
			memcpy(sector + 32, "PC Engine CD-ROM SYSTEM", 23);
			memcpy(sector + 106, "MOSAIC TEST TITLE     ", 22);
		}
		else if (s >= 2)
			for (int i = 0; i < 2048; i++) sector[i] = pattern((size_t)(s * 2048 + i), 31);
		rawSector(bin + (AUDIO + s) * 2352, AUDIO + s, 1, sector);
	}
	snprintf(path, sizeof(path), "%s/pce.bin", dir);
	writeBytes(path, bin, (AUDIO + DATA) * 2352);
	snprintf(path, sizeof(path), "%s/pce.cue", dir);
	writeText(path, "FILE \"pce.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
		"  TRACK 02 MODE1/2352\n    INDEX 01 00:00:06\n");
	free(bin);

	// Sega CD: one MODE1/2048 track starting SEGADISCSYSTEM
	enum { SEGA = 32 };
	uint8_t* cooked = malloc(SEGA * 2048);
	for (int i = 0; i < SEGA * 2048; i++) cooked[i] = pattern((size_t)i, 40);
	memcpy(cooked, "SEGADISCSYSTEM  ", 16);
	snprintf(path, sizeof(path), "%s/segacd.iso", dir);
	writeBytes(path, cooked, SEGA * 2048);
	snprintf(path, sizeof(path), "%s/segacd.cue", dir);
	writeText(path, "FILE \"segacd.iso\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
	free(cooked);
}

static void copyFile(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	CHECK(in != NULL);
	if (!in) return;
	parentDirs(to);
	FILE* out = fopen(to, "wb");
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
}

#define PSX_HASH "6e30bc4408f787a36657a839583762c9"
#define PCE_HASH "62c68c7171fa4d4a67c8143ae9f2d232"
#define SEGA_HASH "3dad8336ea0ed17614f1970c7d177256"

static void testDiscs(void) {
	writeDiscs(ROMS "/Sony PlayStation (PS)");
	hashOf(ROMS "/Sony PlayStation (PS)/psx.cue", "PS", PSX_HASH);
	// the same disc as a CHD
	copyFile(FIXTURES "/psx.chd", ROMS "/Sony PlayStation (PS)/psx.chd");
	hashOf(ROMS "/Sony PlayStation (PS)/psx.chd", "PS", PSX_HASH);
	// and through a playlist, which rcheevos follows to its first disc
	writeText(ROMS "/Sony PlayStation (PS)/psx.m3u", "# a comment\n\npsx.chd\n");
	hashOf(ROMS "/Sony PlayStation (PS)/psx.m3u", "PS", PSX_HASH);

	// PC Engine CDs go on the PCE tag; the data track is the second
	writeDiscs(ROMS "/PC Engine (PCE)");
	hashOf(ROMS "/PC Engine (PCE)/pce.cue", "PCE", PCE_HASH);
	copyFile(FIXTURES "/pce.chd", ROMS "/PC Engine (PCE)/pce.chd");
	hashOf(ROMS "/PC Engine (PCE)/pce.chd", "PCE", PCE_HASH);

	// Sega CDs on the MD tag, and on SEGACD
	writeDiscs(ROMS "/Mega Drive (MD)");
	hashOf(ROMS "/Mega Drive (MD)/segacd.cue", "MD", SEGA_HASH);
	copyFile(FIXTURES "/segacd.chd", ROMS "/Mega Drive (MD)/segacd.chd");
	hashOf(ROMS "/Mega Drive (MD)/segacd.chd", "MD", SEGA_HASH);
	writeDiscs(ROMS "/Sega CD (SEGACD)");
	hashOf(ROMS "/Sega CD (SEGACD)/segacd.cue", "SEGACD", SEGA_HASH);
	// minarch doesn't move an .iso on MD to Sega CD, so it hashes whole
	CHECK(RA_consoleFor("MD", ROMS "/Mega Drive (MD)/segacd.iso") == RC_CONSOLE_MEGA_DRIVE);

	// not a PlayStation disc
	hashOf(ROMS "/Sony PlayStation (PS)/segacd.cue", "PS", NULL);
}

///////////////////////////////////////
// Systems minarch leaves rcheevos to guess

static int knowsWant(const char* hash, void* ctx) {
	int* asked = ctx;
	(*asked)++;
	return !strcmp(hash, "cbf8d240a8f9acf61c9b6385d47c707c");
}

static int knowsNone(const char* hash, void* ctx) {
	(void)hash;
	int* asked = ctx;
	(*asked)++;
	return 0;
}

static void testGuesses(void) {
	writeRom(ROMS "/Doom (PRBOOM)/Plain.gb", NULL, 0, 32768, 1);
	CHECK(RA_consoleFor("PRBOOM", "x.gb") == RC_CONSOLE_UNKNOWN);
	char hash[33];
	int asked = 0;
	CHECK(RAHash_game(ROMS "/Doom (PRBOOM)/Plain.gb", RC_CONSOLE_UNKNOWN, knowsWant, &asked, hash) == 1);
	CHECK_STR(hash, "cbf8d240a8f9acf61c9b6385d47c707c");
	CHECK(asked >= 1);
	asked = 0;
	CHECK(RAHash_game(ROMS "/Doom (PRBOOM)/Plain.gb", RC_CONSOLE_UNKNOWN, knowsNone, &asked, hash) == 0);
	CHECK(asked >= 1);
	CHECK_STR(hash, "");
	CHECK(RAHash_game(ROMS "/Doom (PRBOOM)/Plain.gb", RC_CONSOLE_UNKNOWN, NULL, NULL, hash) == 0);
}

static void testCancel(void) {
	volatile int flag = 1;
	RAHash_setCancel(&flag);
	char hash[33];
	CHECK(RAHash_game(ROMS "/Game Boy (GB)/Plain.gb", RC_CONSOLE_GAMEBOY, NULL, NULL, hash) == 0);
	flag = 0;
	CHECK(RAHash_game(ROMS "/Game Boy (GB)/Plain.gb", RC_CONSOLE_GAMEBOY, NULL, NULL, hash) == 1);
	RAHash_setCancel(NULL);
}

int main(int argc, char** argv) {
	if (argc == 3 && !strcmp(argv[1], "--write-discs")) {
		writeDiscs(argv[2]);
		return 0;
	}
	testCarts();
	testZips();
	testDiscs();
	testGuesses();
	testCancel();
	CHECK_DONE();
}
