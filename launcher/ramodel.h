#ifndef RAMODEL_H
#define RAMODEL_H

// RetroAchievements progress as minarch keeps it on the card, without SDL,
// threads or the card: which rcheevos console a game's folder tag hashes as
// (minarch's table, with its switch to Sega CD and PC Engine CD for disc
// images), the framed cache files under .userdata/shared/.ra/offline/cache
// ([u32 length][JSON][SHA-256 of the JSON]), the JSON in them, the unlock
// ledger (offline/ledger.bin), the muted lists (muted/<hash>.txt), and the
// launcher's own path-to-hash cache lines. Every parser takes bytes already
// read, refuses anything malformed or over its cap, and never trusts a
// length it hasn't checked against what's there.

#include <stddef.h>
#include <stdint.h>
#include <rc_consoles.h>

#define RA_FRAMED_MAX (8 * 1024 * 1024)  // minarch's own cap on a cache file's JSON
#define RA_LEDGER_MAX (8 * 1024 * 1024)  // more than 75000 records
#define RA_LEDGER_RECORD_SIZE 111        // minarch's packed RA_LedgerRecord
#define RA_MUTED_MAX 1024                // lines read from a muted list
#define RA_SET_MAX 4096                  // achievements kept per game
#define RA_JSON_DEPTH 32

// Ledger record types, as minarch writes them.
enum RALedgerType {
	RA_LEDGER_SESSION_START = 1,
	RA_LEDGER_UNLOCK = 2,
	RA_LEDGER_SESSION_END = 3,
	RA_LEDGER_SYNC_ACK = 4,
};


///////////////////////////////////////
// Consoles

// The rcheevos console a game hashes as, the way minarch picks it: the
// folder's tag (the last "(TAG)" in the Roms folder's name) through
// minarch's table, then Mega Drive and PC Engine games with a disc image
// extension (chd, cue, ccd, toc, m3u) moved to Sega CD and PC Engine CD.
// RC_CONSOLE_UNKNOWN for a tag minarch doesn't map, where it lets rcheevos
// guess from the file.
int RA_consoleFor(const char* tag, const char* rom_path);

// The tag minarch's getEmuName gives a path: for one inside roms_path, the
// first folder under it; then the text in its last "(...)", if any.
// Returns 1, or 0 when there's nothing (writing "").
int RA_tagOf(const char* roms_path, const char* path, char* out, size_t size);

///////////////////////////////////////
// SHA-256

typedef struct RASha256 {
	uint32_t state[8];
	uint64_t length;
	uint8_t block[64];
	size_t used;
} RASha256;

void RA_sha256Init(RASha256* ctx);
void RA_sha256Update(RASha256* ctx, const void* data, size_t size);
void RA_sha256Final(RASha256* ctx, uint8_t out[32]);
void RA_sha256(const void* data, size_t size, uint8_t out[32]);

///////////////////////////////////////
// Cache files

// The JSON inside a framed cache file: data is the whole file. Returns 1
// with *body and *body_size pointing into data, or 0 when the length field
// is over RA_FRAMED_MAX, the file isn't exactly 4 + length + 32 bytes, or
// the SHA-256 doesn't match.
int RA_unframe(const uint8_t* data, size_t size, const uint8_t** body, size_t* body_size);

// One achievement of a game's set.
typedef struct RAAchievement {
	uint32_t id;
	int points;
} RAAchievement;

// A game's progress, put together from its cache files, the ledger and its
// muted list.
typedef struct RAGame {
	uint32_t game_id;
	RAAchievement* set;   // core achievements, sorted by id, no repeats
	int count;
	uint32_t* unlocked;   // ids unlocked in either mode, sorted, no repeats
	int unlocked_count;
	uint32_t* hardcore;   // ids unlocked in hardcore, sorted, no repeats
	int hardcore_count;
	uint32_t* muted;      // muted ids, sorted, no repeats
	int muted_count;
} RAGame;

void RA_gameInit(RAGame* game);
void RA_gameFree(RAGame* game);

// The core achievements (Flags 3) of every set in an achievementsets
// response, or of PatchData in an older patch response. Returns 1, or 0
// for JSON that's malformed, says "Success": false, or lists none.
int RA_parseSets(const char* json, size_t size, RAGame* game);

// The unlocks a startsession response lists: Unlocks, and HardcoreUnlocks
// into both lists. Returns 1, or 0 for malformed JSON or "Success": false.
int RA_parseSession(const char* json, size_t size, RAGame* game);

// Unlock records in a ledger file for game_hash (32 hex digits), added to
// the game's lists; records whose own SHA-256 doesn't match are skipped,
// as minarch skips them. Returns how many unlocks it took, or -1 when the
// file is over RA_LEDGER_MAX or not a whole number of records.
int RA_parseLedger(const uint8_t* data, size_t size, const char* game_hash, RAGame* game);

// The ids in a muted list, one per line. Returns how many.
int RA_parseMuted(const char* text, size_t size, RAGame* game);

// Sorts and dedupes the lists, so the counts below are right.
void RA_gameFinish(RAGame* game);

typedef struct RASummary {
	int total, unlocked, unlocked_hardcore;
	int points, points_unlocked;
	int muted;
	uint32_t game_id;
} RASummary;

// Counts over the set: unlocks, hardcore unlocks and muted ids that aren't
// in it are left out.
void RA_summarize(const RAGame* game, RASummary* out);

///////////////////////////////////////
// The launcher's hash cache

// One line of it: "<hash>\t<size>\t<mtime>\t<path>", hash 32 lower-case
// hex digits or "-" for a file that has none, and path relative to the
// card. Returns 1, or 0 for anything else.
int RA_parseCacheLine(const char* line, char hash[33], long long* size, long long* mtime, char* path,
	size_t path_size);
// Writes one, with its newline. Returns 1, or 0 when it doesn't fit or the
// path has a tab or newline in it.
int RA_formatCacheLine(const char* hash, long long size, long long mtime, const char* path, char* out,
	size_t out_size);

// 1 for 32 hex digits.
int RA_isHash(const char* text);

#endif
