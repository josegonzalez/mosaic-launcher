#ifndef RAPROGRESS_H
#define RAPROGRESS_H

// Local RetroAchievements progress by a game's path on the card, read-only
// from what minarch keeps under .userdata/shared/.ra/: its rcheevos hash,
// worked out the way minarch does (rahash.c) on a worker thread of its own
// at low priority, one game at a time, only for games a plugin asks about,
// and kept in a cache file by path, size and modification time so it isn't
// worked out again; then the game's cached achievement set and unlocks,
// the unlock ledger and its muted list. Nothing goes over the network.
// Nothing runs, and no thread starts, until the first ask.

#include <stddef.h>
#include "ramodel.h"

#define RA_PROGRESS_UNKNOWN -1 // not worked out yet, or can't be
#define RA_PROGRESS_ENTRIES 8192 // games kept in memory; asks beyond it stay unknown
#define RA_PROGRESS_QUEUE 64     // asks waiting for the worker; asks beyond it are dropped and asked again later

// Where to look, and where to keep the hash cache: card is the card's root
// (cache paths are relative to it), roms its Roms folder, ra_dir minarch's
// .ra folder and cache_file the launcher's own file. log may be NULL.
void RAProgress_configure(const char* card, const char* roms, const char* ra_dir, const char* cache_file,
	void (*log)(const char* message));

// A game's progress: 1 with out filled, 0 when there's none on the card (no
// achievements cached for it, a console rcheevos can't hash, a file that
// isn't there), or RA_PROGRESS_UNKNOWN until the worker has been through it.
// The first ask about a path queues it.
int RAProgress_get(const char* path, RASummary* out);

// A game's rcheevos hash, with the same answers.
int RAProgress_hash(const char* path, char* out, size_t size);

// 1 once after the worker has finished games since the last call, so the
// list is drawn again with their answers.
int RAProgress_news(void);

// Stops the worker. A game it's hashing is finished first.
void RAProgress_quit(void);

#endif
