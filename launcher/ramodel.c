#include "ramodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

///////////////////////////////////////
// Consoles

typedef struct TagConsole {
	const char* tag;
	int console;
} TagConsole;

// minarch's ra_consoles.h: NextUI's tags and the consoles they hash as.
// Tags it maps to no console, and tags it doesn't list, both leave rcheevos
// to guess.
static const TagConsole tag_consoles[] = {
	{ "A2600", RC_CONSOLE_ATARI_2600 },
	{ "A5200", RC_CONSOLE_ATARI_5200 },
	{ "A7800", RC_CONSOLE_ATARI_7800 },
	{ "32X", RC_CONSOLE_SEGA_32X },
	{ "C128", RC_CONSOLE_COMMODORE_64 },
	{ "C64", RC_CONSOLE_COMMODORE_64 },
	{ "COLECO", RC_CONSOLE_COLECOVISION },
	{ "CPC", RC_CONSOLE_AMSTRAD_PC },
	{ "FC", RC_CONSOLE_NINTENDO },
	{ "FBN", RC_CONSOLE_ARCADE },
	{ "FDS", RC_CONSOLE_FAMICOM_DISK_SYSTEM },
	{ "GB", RC_CONSOLE_GAMEBOY },
	{ "GBA", RC_CONSOLE_GAMEBOY_ADVANCE },
	{ "GBC", RC_CONSOLE_GAMEBOY_COLOR },
	{ "GG", RC_CONSOLE_GAME_GEAR },
	{ "LYNX", RC_CONSOLE_ATARI_LYNX },
	{ "MD", RC_CONSOLE_MEGA_DRIVE },
	{ "MGBA", RC_CONSOLE_GAMEBOY_ADVANCE },
	{ "MSX", RC_CONSOLE_MSX },
	{ "NGP", RC_CONSOLE_NEOGEO_POCKET },
	{ "NGPC", RC_CONSOLE_NEOGEO_POCKET },
	{ "P8", RC_CONSOLE_PICO },
	{ "PCE", RC_CONSOLE_PC_ENGINE },
	{ "PKM", RC_CONSOLE_POKEMON_MINI },
	{ "PS", RC_CONSOLE_PLAYSTATION },
	{ "PSX", RC_CONSOLE_PLAYSTATION },
	{ "PUAE", RC_CONSOLE_AMIGA },
	{ "SEGACD", RC_CONSOLE_SEGA_CD },
	{ "SFC", RC_CONSOLE_SUPER_NINTENDO },
	{ "SG1000", RC_CONSOLE_SG1000 },
	{ "SGB", RC_CONSOLE_GAMEBOY },
	{ "SMS", RC_CONSOLE_MASTER_SYSTEM },
	{ "SUPA", RC_CONSOLE_SUPER_NINTENDO },
	{ "VB", RC_CONSOLE_VIRTUAL_BOY },
	{ "VIC", RC_CONSOLE_VIC20 },
};

static int isDiscImage(const char* path) {
	const char* ext = path ? strrchr(path, '.') : NULL;
	if (!ext) return 0;
	ext++;
	static const char* discs[] = { "chd", "cue", "ccd", "toc", "m3u" };
	for (size_t i = 0; i < sizeof(discs) / sizeof(discs[0]); i++)
		if (!strcasecmp(ext, discs[i])) return 1;
	return 0;
}

int RA_consoleFor(const char* tag, const char* rom_path) {
	if (!tag || !tag[0]) return RC_CONSOLE_UNKNOWN;
	int console = RC_CONSOLE_UNKNOWN;
	for (size_t i = 0; i < sizeof(tag_consoles) / sizeof(tag_consoles[0]); i++) {
		if (!strcmp(tag, tag_consoles[i].tag)) {
			console = tag_consoles[i].console;
			break;
		}
	}
	if (console == RC_CONSOLE_PC_ENGINE && isDiscImage(rom_path)) return RC_CONSOLE_PC_ENGINE_CD;
	if (console == RC_CONSOLE_MEGA_DRIVE && isDiscImage(rom_path)) return RC_CONSOLE_SEGA_CD;
	return console;
}

int RA_tagOf(const char* roms_path, const char* path, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!path) return 0;
	const char* name = path;
	size_t length = strlen(path);
	size_t roms = roms_path ? strlen(roms_path) : 0;
	if (roms && !strncmp(path, roms_path, roms) && path[roms] == '/') {
		name = path + roms + 1;
		const char* slash = strchr(name, '/');
		length = slash ? (size_t)(slash - name) : strlen(name);
	}
	// the last "(" in the name, up to the ")" after it
	const char* open = NULL;
	for (size_t i = 0; i < length; i++)
		if (name[i] == '(') open = name + i;
	if (open) {
		const char* close = memchr(open, ')', length - (size_t)(open - name));
		if (!close) return 0;
		name = open + 1;
		length = (size_t)(close - name);
	}
	if (!length || length >= size) return 0;
	memcpy(out, name, length);
	out[length] = '\0';
	return 1;
}

///////////////////////////////////////
// SHA-256, from FIPS 180-4

static const uint32_t k256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void shaBlock(RASha256* ctx, const uint8_t* p) {
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
	uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = h + (ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25)) + ((e & f) ^ (~e & g)) + k256[i] + w[i];
		uint32_t t2 = (ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

void RA_sha256Init(RASha256* ctx) {
	static const uint32_t start[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
		0x1f83d9ab, 0x5be0cd19 };
	memcpy(ctx->state, start, sizeof(start));
	ctx->length = 0;
	ctx->used = 0;
}

void RA_sha256Update(RASha256* ctx, const void* data, size_t size) {
	const uint8_t* p = data;
	ctx->length += size;
	while (size) {
		size_t take = 64 - ctx->used;
		if (take > size) take = size;
		memcpy(ctx->block + ctx->used, p, take);
		ctx->used += take;
		p += take;
		size -= take;
		if (ctx->used == 64) {
			shaBlock(ctx, ctx->block);
			ctx->used = 0;
		}
	}
}

void RA_sha256Final(RASha256* ctx, uint8_t out[32]) {
	uint64_t bits = ctx->length * 8;
	uint8_t pad = 0x80;
	RA_sha256Update(ctx, &pad, 1);
	pad = 0;
	while (ctx->used != 56) RA_sha256Update(ctx, &pad, 1);
	uint8_t tail[8];
	for (int i = 0; i < 8; i++) tail[i] = (uint8_t)(bits >> (56 - 8 * i));
	RA_sha256Update(ctx, tail, 8);
	for (int i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(ctx->state[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
		out[i * 4 + 3] = (uint8_t)ctx->state[i];
	}
}

void RA_sha256(const void* data, size_t size, uint8_t out[32]) {
	RASha256 ctx;
	RA_sha256Init(&ctx);
	RA_sha256Update(&ctx, data, size);
	RA_sha256Final(&ctx, out);
}

///////////////////////////////////////
// Framing

static uint32_t le32(const uint8_t* p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int RA_unframe(const uint8_t* data, size_t size, const uint8_t** body, size_t* body_size) {
	if (!data || size < 4 + 32) return 0;
	uint32_t length = le32(data);
	if (length > RA_FRAMED_MAX || size != 4 + (size_t)length + 32) return 0;
	uint8_t digest[32];
	RA_sha256(data + 4, length, digest);
	if (memcmp(digest, data + 4 + length, 32)) return 0;
	if (body) *body = data + 4;
	if (body_size) *body_size = length;
	return 1;
}

///////////////////////////////////////
// Lists

void RA_gameInit(RAGame* game) {
	memset(game, 0, sizeof(*game));
}

void RA_gameFree(RAGame* game) {
	free(game->set);
	free(game->unlocked);
	free(game->hardcore);
	free(game->muted);
	RA_gameInit(game);
}

static int pushId(uint32_t** list, int* count, uint32_t id) {
	if (*count >= RA_SET_MAX) return 0;
	if (!(*count & 63)) {
		uint32_t* grown = realloc(*list, (size_t)(*count + 64) * sizeof(uint32_t));
		if (!grown) return 0;
		*list = grown;
	}
	(*list)[(*count)++] = id;
	return 1;
}

static int pushAchievement(RAGame* game, uint32_t id, int points) {
	if (game->count >= RA_SET_MAX) return 0;
	if (!(game->count & 63)) {
		RAAchievement* grown = realloc(game->set, (size_t)(game->count + 64) * sizeof(RAAchievement));
		if (!grown) return 0;
		game->set = grown;
	}
	game->set[game->count++] = (RAAchievement){ id, points };
	return 1;
}

///////////////////////////////////////
// JSON: a strict walk over the parts that matter, skipping the rest

typedef struct Json {
	const char* p;
	const char* end;
	int depth;
	int bad;
} Json;

static void ws(Json* j) {
	while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int peek(Json* j) {
	ws(j);
	return j->p < j->end ? (unsigned char)*j->p : -1;
}

static int eat(Json* j, char c) {
	if (peek(j) != (unsigned char)c) return 0;
	j->p++;
	return 1;
}

// A string's raw text, between its quotes, escapes left as they are.
static int string(Json* j, const char** text, size_t* length) {
	if (!eat(j, '"')) return j->bad = 1, 0;
	const char* start = j->p;
	while (j->p < j->end && *j->p != '"') {
		if ((unsigned char)*j->p < 0x20) return j->bad = 1, 0;
		if (*j->p == '\\') {
			if (++j->p >= j->end) return j->bad = 1, 0;
		}
		j->p++;
	}
	if (j->p >= j->end) return j->bad = 1, 0;
	if (text) *text = start;
	if (length) *length = (size_t)(j->p - start);
	j->p++;
	return 1;
}

static int literal(Json* j, const char* word) {
	size_t n = strlen(word);
	if ((size_t)(j->end - j->p) < n || memcmp(j->p, word, n)) return j->bad = 1, 0;
	j->p += n;
	return 1;
}

// A number, as an integer when it's a whole one that fits.
static int number(Json* j, long long* out, int* whole) {
	const char* s = j->p;
	if (s < j->end && *s == '-') s++;
	const char* digits = s;
	while (s < j->end && *s >= '0' && *s <= '9') s++;
	if (s == digits) return j->bad = 1, 0;
	int is_whole = 1;
	if (s < j->end && *s == '.') {
		is_whole = 0;
		const char* f = ++s;
		while (s < j->end && *s >= '0' && *s <= '9') s++;
		if (s == f) return j->bad = 1, 0;
	}
	if (s < j->end && (*s == 'e' || *s == 'E')) {
		is_whole = 0;
		s++;
		if (s < j->end && (*s == '+' || *s == '-')) s++;
		const char* e = s;
		while (s < j->end && *s >= '0' && *s <= '9') s++;
		if (s == e) return j->bad = 1, 0;
	}
	long long value = 0;
	int negative = *j->p == '-';
	if (is_whole && s - digits > 12) is_whole = 0;
	if (is_whole)
		for (const char* d = digits; d < s && *d >= '0' && *d <= '9'; d++) value = value * 10 + (*d - '0');
	if (out) *out = negative ? -value : value;
	if (whole) *whole = is_whole;
	j->p = s;
	return 1;
}

static int skip(Json* j);

static int skipContainer(Json* j, char open, char close) {
	if (++j->depth > RA_JSON_DEPTH) return j->bad = 1, 0;
	j->p++; // open
	(void)open;
	if (eat(j, close)) {
		j->depth--;
		return 1;
	}
	do {
		if (close == '}') {
			if (!string(j, NULL, NULL) || !eat(j, ':')) return j->bad = 1, 0;
		}
		if (!skip(j)) return 0;
	} while (eat(j, ','));
	if (!eat(j, close)) return j->bad = 1, 0;
	j->depth--;
	return 1;
}

static int skip(Json* j) {
	switch (peek(j)) {
		case '{': return skipContainer(j, '{', '}');
		case '[': return skipContainer(j, '[', ']');
		case '"': return string(j, NULL, NULL);
		case 't': return literal(j, "true");
		case 'f': return literal(j, "false");
		case 'n': return literal(j, "null");
		default: return number(j, NULL, NULL);
	}
}

// Walks an object, calling on for each key with the cursor on its value;
// on reads the value or returns 0 to have it skipped.
typedef int (*KeyFn)(Json* j, const char* key, size_t length, void* ctx);

static int object(Json* j, KeyFn on, void* ctx) {
	if (peek(j) != '{') return j->bad = 1, 0;
	if (++j->depth > RA_JSON_DEPTH) return j->bad = 1, 0;
	j->p++;
	if (!eat(j, '}')) {
		do {
			const char* key;
			size_t length;
			if (!string(j, &key, &length) || !eat(j, ':')) return j->bad = 1, 0;
			ws(j);
			if (!on(j, key, length, ctx) && !j->bad && !skip(j)) return 0;
			if (j->bad) return 0;
		} while (eat(j, ','));
		if (!eat(j, '}')) return j->bad = 1, 0;
	}
	j->depth--;
	return 1;
}

typedef int (*ItemFn)(Json* j, void* ctx);

static int array(Json* j, ItemFn on, void* ctx) {
	if (peek(j) != '[') return j->bad = 1, 0;
	if (++j->depth > RA_JSON_DEPTH) return j->bad = 1, 0;
	j->p++;
	if (!eat(j, ']')) {
		do {
			ws(j);
			if (!on(j, ctx) || j->bad) return j->bad = 1, 0;
		} while (eat(j, ','));
		if (!eat(j, ']')) return j->bad = 1, 0;
	}
	j->depth--;
	return 1;
}

static int is(const char* key, size_t length, const char* want) {
	return strlen(want) == length && !memcmp(key, want, length);
}

// An id: a whole number from 1 to UINT32_MAX.
static int idValue(Json* j, uint32_t* out) {
	long long value;
	int whole;
	if (peek(j) != '-' && (peek(j) < '0' || peek(j) > '9')) return 0;
	if (!number(j, &value, &whole)) return 0;
	if (!whole || value <= 0 || value > 0xFFFFFFFFLL) return 1;
	*out = (uint32_t)value;
	return 1;
}

// "Success": false makes a response say nothing.
typedef struct Root {
	RAGame* game;
	int failed;
	int found;
} Root;

static int successKey(Json* j, const char* key, size_t length, Root* root) {
	if (!is(key, length, "Success")) return 0;
	if (peek(j) == 'f') {
		root->failed = 1;
		return literal(j, "false");
	}
	return 0;
}

typedef struct Cheevo {
	uint32_t id;
	long long flags;
	long long points;
} Cheevo;

static int cheevoKey(Json* j, const char* key, size_t length, void* ctx) {
	Cheevo* c = ctx;
	if (is(key, length, "ID")) return idValue(j, &c->id);
	if (is(key, length, "Flags") || is(key, length, "Points")) {
		int c0 = peek(j);
		if (c0 != '-' && (c0 < '0' || c0 > '9')) return 0;
		long long value;
		int whole;
		if (!number(j, &value, &whole)) return 0;
		if (!whole) value = -1;
		if (is(key, length, "Flags")) c->flags = value;
		else c->points = value;
		return 1;
	}
	return 0;
}

#define FLAG_CORE 3

static int cheevoItem(Json* j, void* ctx) {
	Root* root = ctx;
	if (peek(j) != '{') return skip(j);
	Cheevo c = { 0, 0, 0 };
	if (!object(j, cheevoKey, &c)) return 0;
	if (c.id && c.flags == FLAG_CORE) {
		int points = c.points > 0 && c.points < 100000 ? (int)c.points : 0;
		pushAchievement(root->game, c.id, points);
		root->found = 1;
	}
	return 1;
}

static int achievementsKey(Json* j, const char* key, size_t length, void* ctx) {
	if (!is(key, length, "Achievements") || peek(j) != '[') return 0;
	return array(j, cheevoItem, ctx);
}

static int setItem(Json* j, void* ctx) {
	if (peek(j) != '{') return skip(j);
	return object(j, achievementsKey, ctx);
}

static int patchKey(Json* j, const char* key, size_t length, void* ctx) {
	Root* root = ctx;
	if (is(key, length, "ID")) return idValue(j, &root->game->game_id);
	return achievementsKey(j, key, length, ctx);
}

static int setsRootKey(Json* j, const char* key, size_t length, void* ctx) {
	Root* root = ctx;
	if (successKey(j, key, length, root)) return 1;
	if (j->bad) return 0;
	if (is(key, length, "GameId")) return idValue(j, &root->game->game_id);
	if (is(key, length, "Sets") && peek(j) == '[') return array(j, setItem, ctx);
	if (is(key, length, "PatchData") && peek(j) == '{') return object(j, patchKey, ctx);
	return 0;
}

static int wholeDocument(Json* j) {
	ws(j);
	return !j->bad && j->p == j->end;
}

int RA_parseSets(const char* json, size_t size, RAGame* game) {
	if (!json || !game) return 0;
	Json j = { json, json + size, 0, 0 };
	Root root = { game, 0, 0 };
	int before = game->count;
	if (!object(&j, setsRootKey, &root) || !wholeDocument(&j) || root.failed || !root.found) {
		game->count = before;
		return 0;
	}
	return 1;
}

typedef struct Unlocks {
	RAGame* game;
	int hardcore;
} Unlocks;

static int unlockKey(Json* j, const char* key, size_t length, void* ctx) {
	if (!is(key, length, "ID")) return 0;
	return idValue(j, ctx);
}

static int unlockItem(Json* j, void* ctx) {
	Unlocks* u = ctx;
	uint32_t id = 0;
	if (peek(j) == '{') {
		if (!object(j, unlockKey, &id)) return 0;
	}
	else if (!idValue(j, &id)) return skip(j);
	if (!id) return 1;
	pushId(&u->game->unlocked, &u->game->unlocked_count, id);
	if (u->hardcore) pushId(&u->game->hardcore, &u->game->hardcore_count, id);
	return 1;
}

static int sessionRootKey(Json* j, const char* key, size_t length, void* ctx) {
	Root* root = ctx;
	if (successKey(j, key, length, root)) return 1;
	if (j->bad) return 0;
	int hardcore = is(key, length, "HardcoreUnlocks");
	if ((hardcore || is(key, length, "Unlocks")) && peek(j) == '[') {
		Unlocks u = { root->game, hardcore };
		return array(j, unlockItem, &u);
	}
	return 0;
}

int RA_parseSession(const char* json, size_t size, RAGame* game) {
	if (!json || !game) return 0;
	Json j = { json, json + size, 0, 0 };
	Root root = { game, 0, 0 };
	int unlocked = game->unlocked_count, hardcore = game->hardcore_count;
	if (!object(&j, sessionRootKey, &root) || !wholeDocument(&j) || root.failed) {
		game->unlocked_count = unlocked;
		game->hardcore_count = hardcore;
		return 0;
	}
	return 1;
}

///////////////////////////////////////
// Ledger and muted lists

// minarch's packed record: type, timestamp, game id, achievement id,
// hardcore, game hash (33), prev_hash (32), record_hash (32).
#define REC_TYPE 0
#define REC_ACHIEVEMENT 9
#define REC_HARDCORE 13
#define REC_HASH 14
#define REC_RECORD_HASH 79

int RA_parseLedger(const uint8_t* data, size_t size, const char* game_hash, RAGame* game) {
	if (!data || !game || !RA_isHash(game_hash)) return -1;
	if (size > RA_LEDGER_MAX || size % RA_LEDGER_RECORD_SIZE) return -1;
	int taken = 0;
	for (size_t at = 0; at < size; at += RA_LEDGER_RECORD_SIZE) {
		const uint8_t* rec = data + at;
		if (rec[REC_TYPE] != RA_LEDGER_UNLOCK) continue;
		uint8_t digest[32];
		RA_sha256(rec, REC_RECORD_HASH, digest);
		if (memcmp(digest, rec + REC_RECORD_HASH, 32)) continue;
		if (rec[REC_HASH + 32] != '\0' || strncasecmp((const char*)rec + REC_HASH, game_hash, 32)) continue;
		uint32_t id = le32(rec + REC_ACHIEVEMENT);
		if (!id) continue;
		pushId(&game->unlocked, &game->unlocked_count, id);
		if (rec[REC_HARDCORE]) pushId(&game->hardcore, &game->hardcore_count, id);
		taken++;
	}
	return taken;
}

int RA_parseMuted(const char* text, size_t size, RAGame* game) {
	if (!text || !game) return 0;
	int lines = 0, taken = 0;
	size_t at = 0;
	while (at < size && lines < RA_MUTED_MAX) {
		size_t end = at;
		while (end < size && text[end] != '\n') end++;
		lines++;
		// minarch reads each line with strtoul
		unsigned long long id = 0;
		size_t i = at;
		while (i < end && (text[i] == ' ' || text[i] == '\t')) i++;
		int digits = 0;
		while (i < end && text[i] >= '0' && text[i] <= '9' && digits < 11) {
			id = id * 10 + (unsigned)(text[i] - '0');
			i++;
			digits++;
		}
		if (id > 0 && id <= 0xFFFFFFFFULL && pushId(&game->muted, &game->muted_count, (uint32_t)id)) taken++;
		at = end + 1;
	}
	return taken;
}

static int compareIds(const void* a, const void* b) {
	uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
	return x < y ? -1 : x > y;
}

static int compareAchievements(const void* a, const void* b) {
	return compareIds(&((const RAAchievement*)a)->id, &((const RAAchievement*)b)->id);
}

static void uniqueIds(uint32_t* list, int* count) {
	if (*count < 2) return;
	qsort(list, (size_t)*count, sizeof(uint32_t), compareIds);
	int n = 1;
	for (int i = 1; i < *count; i++)
		if (list[i] != list[n - 1]) list[n++] = list[i];
	*count = n;
}

void RA_gameFinish(RAGame* game) {
	if (game->count > 1) {
		qsort(game->set, (size_t)game->count, sizeof(RAAchievement), compareAchievements);
		int n = 1;
		for (int i = 1; i < game->count; i++)
			if (game->set[i].id != game->set[n - 1].id) game->set[n++] = game->set[i];
		game->count = n;
	}
	uniqueIds(game->unlocked, &game->unlocked_count);
	uniqueIds(game->hardcore, &game->hardcore_count);
	uniqueIds(game->muted, &game->muted_count);
}

static int has(const uint32_t* list, int count, uint32_t id) {
	return list && count && bsearch(&id, list, (size_t)count, sizeof(uint32_t), compareIds) != NULL;
}

void RA_summarize(const RAGame* game, RASummary* out) {
	memset(out, 0, sizeof(*out));
	out->game_id = game->game_id;
	for (int i = 0; i < game->count; i++) {
		const RAAchievement* a = &game->set[i];
		out->total++;
		out->points += a->points;
		if (has(game->unlocked, game->unlocked_count, a->id)) {
			out->unlocked++;
			out->points_unlocked += a->points;
		}
		if (has(game->hardcore, game->hardcore_count, a->id)) out->unlocked_hardcore++;
		if (has(game->muted, game->muted_count, a->id)) out->muted++;
	}
}

///////////////////////////////////////
// Hash cache lines

int RA_isHash(const char* text) {
	if (!text) return 0;
	for (int i = 0; i < 32; i++) {
		char c = text[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
	}
	return text[32] == '\0';
}

static int field(const char** p, char* out, size_t size) {
	const char* tab = strchr(*p, '\t');
	if (!tab) return 0;
	size_t n = (size_t)(tab - *p);
	if (!n || n >= size) return 0;
	memcpy(out, *p, n);
	out[n] = '\0';
	*p = tab + 1;
	return 1;
}

static int wholeNumber(const char* text, long long* out) {
	if (!text[0]) return 0;
	long long value = 0;
	for (const char* c = text; *c; c++) {
		if (*c < '0' || *c > '9' || c - text > 17) return 0;
		value = value * 10 + (*c - '0');
	}
	*out = value;
	return 1;
}

int RA_parseCacheLine(const char* line, char hash[33], long long* size, long long* mtime, char* path,
	size_t path_size) {
	if (!line || !path || !path_size) return 0;
	const char* p = line;
	char a[40], b[24], c[24];
	if (!field(&p, a, sizeof(a)) || !field(&p, b, sizeof(b)) || !field(&p, c, sizeof(c))) return 0;
	if (strcmp(a, "-") && !RA_isHash(a)) return 0;
	if (!wholeNumber(b, size) || !wholeNumber(c, mtime)) return 0;
	size_t n = strcspn(p, "\r\n");
	if (!n || n >= path_size || memchr(p, '\t', n)) return 0;
	if (p[n] && strcmp(p + n, "\n") && strcmp(p + n, "\r\n")) return 0;
	memcpy(path, p, n);
	path[n] = '\0';
	memcpy(hash, a, strlen(a) + 1);
	for (char* h = hash; *h; h++)
		if (*h >= 'A' && *h <= 'F') *h = (char)(*h - 'A' + 'a');
	return 1;
}

int RA_formatCacheLine(const char* hash, long long size, long long mtime, const char* path, char* out,
	size_t out_size) {
	if (!hash || !path || !path[0] || !out || !out_size) return 0;
	if (strcmp(hash, "-") && !RA_isHash(hash)) return 0;
	if (strpbrk(path, "\t\r\n") || size < 0 || mtime < 0) return 0;
	int n = snprintf(out, out_size, "%s\t%lld\t%lld\t%s\n", hash, size, mtime, path);
	return n > 0 && (size_t)n < out_size;
}
