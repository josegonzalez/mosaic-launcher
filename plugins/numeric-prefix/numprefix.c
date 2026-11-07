#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "numprefix.h"

#define MAX_DIGITS 9

int NumPrefix_parse(const char* name, long* number, const char** rest) {
	if (!name) return 0;
	const char* p = name;
	int bracketed = *p == '[';
	if (bracketed) p++;

	long value = 0;
	int digits = 0;
	while (*p >= '0' && *p <= '9') {
		if (++digits > MAX_DIGITS) return 0;
		value = value * 10 + (*p - '0');
		p++;
	}
	if (!digits) return 0;

	if (bracketed) {
		if (*p != ']') return 0;
	}
	else if (*p != ')' && *p != '.') return 0;
	p++;

	if (*p != ' ') return 0;
	while (*p == ' ') p++;
	if (!*p) return 0;

	if (number) *number = value;
	if (rest) *rest = p;
	return 1;
}

int NumPrefix_strip(const char* name, char* out, size_t size) {
	const char* rest;
	int had = NumPrefix_parse(name, NULL, &rest);
	snprintf(out, size, "%s", had ? rest : name ? name : "");
	return had;
}

int NumPrefix_compare(const NumKey* a, const NumKey* b) {
	if (a->group != b->group) return a->group < b->group ? -1 : 1;
	if (a->group != NUMPREFIX_ORDERED) return 0;
	if (a->numbered != b->numbered) return a->numbered ? -1 : 1;
	if (!a->numbered || a->number == b->number) return 0;
	return a->number < b->number ? -1 : 1;
}

// True when path is base + "/" + something.
static int under(const char* base, const char* path) {
	size_t len = strlen(base);
	return !strncmp(path, base, len) && path[len] == '/' && path[len + 1];
}

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static int isNamed(const char* sdcard, const char* path, const char* name) {
	size_t len = strlen(sdcard);
	return !strncmp(path, sdcard, len) && path[len] == '/' && !strcmp(path + len + 1, name);
}

// A collection's .txt in the collections folder.
static int isCollection(const char* sdcard, const char* path) {
	char collections[512];
	snprintf(collections, sizeof(collections), "%s/Collections", sdcard);
	return under(collections, path) && endsWith(path, ".txt");
}

int NumPrefix_rootGroup(const char* sdcard, const char* path) {
	if (!sdcard || !path) return NUMPREFIX_AFTER;
	if (isNamed(sdcard, path, "Recently Played")) return NUMPREFIX_BEFORE;
	char roms[512];
	snprintf(roms, sizeof(roms), "%s/Roms", sdcard);
	if (under(roms, path) || isCollection(sdcard, path)) return NUMPREFIX_ORDERED;
	return NUMPREFIX_AFTER;
}

int NumPrefix_replacedRoot(const char* sdcard, const char* path) {
	if (!sdcard || !path) return 0;
	char roms[512];
	snprintf(roms, sizeof(roms), "%s/Roms", sdcard);
	if (!strcmp(path, roms)) return 1;
	return under(roms, path) && strchr(path + strlen(roms) + 1, '/') != NULL;
}

int NumPrefix_keepsOrder(const char* sdcard, const char* dir) {
	if (!sdcard || !dir) return 0;
	return isNamed(sdcard, dir, "Recently Played") || isCollection(sdcard, dir);
}

///////////////////////////////////////

struct NumCacheItem {
	char* path;
	long number;
	NumCacheItem* next;
};

static unsigned hash(const char* s) {
	unsigned h = 2166136261u;
	while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
	return h % NUMCACHE_BUCKETS;
}

void NumCache_init(NumCache* cache) {
	memset(cache, 0, sizeof(*cache));
}

void NumCache_clear(NumCache* cache) {
	for (int i = 0; i < NUMCACHE_BUCKETS; i++) {
		NumCacheItem* item = cache->buckets[i];
		while (item) {
			NumCacheItem* next = item->next;
			free(item->path);
			free(item);
			item = next;
		}
	}
	NumCache_init(cache);
}

static NumCacheItem** find(NumCache* cache, const char* path) {
	NumCacheItem** at = &cache->buckets[hash(path)];
	while (*at && strcmp((*at)->path, path)) at = &(*at)->next;
	return at;
}

int NumCache_set(NumCache* cache, const char* path, long number) {
	if (!path) return -1;
	NumCacheItem** at = find(cache, path);
	if (*at) {
		(*at)->number = number;
		return 0;
	}
	NumCacheItem* item = malloc(sizeof(*item));
	if (!item) return -1;
	item->path = strdup(path);
	if (!item->path) {
		free(item);
		return -1;
	}
	item->number = number;
	item->next = NULL;
	*at = item;
	cache->count++;
	return 0;
}

void NumCache_remove(NumCache* cache, const char* path) {
	if (!path) return;
	NumCacheItem** at = find(cache, path);
	if (!*at) return;
	NumCacheItem* item = *at;
	*at = item->next;
	free(item->path);
	free(item);
	cache->count--;
}

int NumCache_get(const NumCache* cache, const char* path, long* number) {
	if (!path) return 0;
	NumCacheItem* const* at = find((NumCache*)cache, path);
	if (!*at) return 0;
	if (number) *number = (*at)->number;
	return 1;
}
