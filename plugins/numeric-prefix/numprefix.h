#ifndef NUMPREFIX_H
#define NUMPREFIX_H

#include <stddef.h>

// Numbers at the start of a name that set its place in a list, eg.
// "1) Super Mario Land" or "02. Tetris". The forms are:
//
//     1) Name     1. Name     [1] Name
//
// with any number of digits (leading zeros are fine, up to 9 digits), then
// at least one space, then the rest of the name, which can't be empty.
// "1942", "3-D Worldrunner" and "007 - Nightfire" have none, which is why
// "1 - Name" isn't a form. The launcher drops anything after the last dot
// that looks like an extension, so with "1. " the rest needs more than
// three letters: "1. Kirby.gb" shows as "Kirby", but "1. Qix.gb" as "1".

// Returns 1 and sets *number and *rest (the name after the prefix and its
// spaces) when name starts with a prefix, or 0. Either pointer can be NULL.
int NumPrefix_parse(const char* name, long* number, const char** rest);

// Writes name without its prefix to out and returns 1, or writes name as it
// is and returns 0 when it has none.
int NumPrefix_strip(const char* name, char* out, size_t size);

// Where an entry sits in a list, for ordering. Groups go in order, and in
// the NUMPREFIX_ORDERED group numbered entries come first, by number, then
// the rest. Entries this doesn't separate compare 0, so they keep the
// launcher's order.
typedef struct NumKey {
	int group;
	int numbered;
	long number;
} NumKey;

int NumPrefix_compare(const NumKey* a, const NumKey* b);

// The group of an entry on the root, so numbers only move the consoles
// (and collections, when the root lists them in place of consoles):
// Recently Played stays first, and Collections, plugin folders and Tools
// stay after them.
enum {
	NUMPREFIX_BEFORE,
	NUMPREFIX_ORDERED,
	NUMPREFIX_AFTER,
};
int NumPrefix_rootGroup(const char* sdcard, const char* path);

// True for lists that keep their own order: Recently Played and a
// collection.
int NumPrefix_keepsOrder(const char* sdcard, const char* dir);

// The numbers of the entries seen with a prefix, by path. display_name
// takes the number off the name, so the sort looks it up here.
typedef struct NumCacheItem NumCacheItem;
#define NUMCACHE_BUCKETS 1024
typedef struct NumCache {
	NumCacheItem* buckets[NUMCACHE_BUCKETS];
	int count;
} NumCache;

void NumCache_init(NumCache* cache);
void NumCache_clear(NumCache* cache);
// Returns 0, or -1 when memory ran out.
int NumCache_set(NumCache* cache, const char* path, long number);
void NumCache_remove(NumCache* cache, const char* path);
// Returns 1 and sets *number when path is there, or 0.
int NumCache_get(const NumCache* cache, const char* path, long* number);

#endif
