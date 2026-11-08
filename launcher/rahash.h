#ifndef RAHASH_H
#define RAHASH_H

// A game's rcheevos hash, worked out through rcheevos' own rc_hash the way
// minarch has it work one out when the game starts: the console from
// RA_consoleFor, CHD images through RAChd_reader, and a zip's first file
// unzipped first, as minarch does for cores that can't open zips (except
// arcade games, which fbneo opens zipped, and hash by name). Reads the
// file, so call it off the main thread.

#include <stddef.h>
#include <stdint.h>

#define RA_UNZIP_MAX (64 * 1024 * 1024) // what rc_hash hashes of a buffer at most

// Says whether rcheevos' guess is the hash minarch settled on, for a game
// with no console: 1 when the card has RetroAchievements data for it.
typedef int (*RAKnownHash)(const char* hash, void* ctx);

// Where rcheevos' error messages go, or NULL. Call it once before hashing.
void RAHash_setLog(void (*log)(const char* message));

// While *flag is set, every read of the file being hashed comes back empty,
// so a hash in progress ends early. Its answer is then wrong: a caller that
// sets the flag throws it away. NULL for none.
void RAHash_setCancel(volatile int* flag);

// Writes the hash, as 32 lower-case hex digits, and returns 1, or returns 0
// when rcheevos can't make one, or (with no console) known takes none of
// its guesses.
int RAHash_game(const char* path, int console, RAKnownHash known, void* ctx, char out[33]);

// The first file in a zip (not a folder), unzipped into *data (free it),
// with its name. Returns 1, or 0 for no such file, one over RA_UNZIP_MAX,
// or a damaged zip.
int RAHash_unzipFirst(const char* path, uint8_t** data, size_t* size, char* name, size_t name_size);

#endif
