#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "sort_model.h"

// English kept in data, translated where it's shown, for make pot
#define TR_MARK(text) (text)

static const struct {
	const char* id;
	const char* label;
} modes[SM_MODES] = {
	{ "name", TR_MARK("Name") },
	{ "most played", TR_MARK("Most played") },
	{ "last played", TR_MARK("Last played") },
	{ "year", TR_MARK("Year") },
	{ "publisher", TR_MARK("Publisher") },
	{ "recently added", TR_MARK("Recently added") },
};

const char* SM_modeId(int mode) {
	return mode >= 0 && mode < SM_MODES ? modes[mode].id : modes[SM_NAME].id;
}

const char* SM_modeLabel(int mode) {
	return mode >= 0 && mode < SM_MODES ? modes[mode].label : modes[SM_NAME].label;
}

int SM_modeFind(const char* id) {
	for (int i = 0; id && i < SM_MODES; i++) {
		if (!strcmp(id, modes[i].id)) return i;
	}
	return -1;
}

int SM_modeNext(int mode) {
	return mode >= 0 && mode < SM_MODES - 1 ? mode + 1 : SM_NAME;
}

int SM_modeReadsPlay(int mode) {
	return mode == SM_MOST_PLAYED || mode == SM_LAST_PLAYED;
}

int SM_modeReadsIndex(int mode) {
	return mode == SM_YEAR || mode == SM_PUBLISHER;
}

// 1 when path is sdcard + "/" + name, or inside it with within.
static int under(const char* sdcard, const char* path, const char* name, int within) {
	size_t len = strlen(sdcard);
	if (strncmp(path, sdcard, len) != 0 || path[len] != '/') return 0;
	path += len + 1;
	len = strlen(name);
	if (strncmp(path, name, len) != 0) return 0;
	return !path[len] || (within && path[len] == '/');
}

int SM_sorts(const char* sdcard, const char* dir) {
	if (!sdcard || !dir || !strcmp(dir, sdcard)) return 0;
	return !under(sdcard, dir, "Roms", 0) && !under(sdcard, dir, "Recently Played", 0)
		&& !under(sdcard, dir, "Collections", 0) && !under(sdcard, dir, "Search", 1)
		&& !under(sdcard, dir, "Tools", 1);
}

int SM_inRoms(const char* sdcard, const char* path) {
	if (!sdcard || !path || !under(sdcard, path, "Roms", 1)) return 0;
	const char* system = path + strlen(sdcard) + strlen("/Roms");
	if (*system++ != '/') return 0;
	const char* slash = strchr(system, '/');
	return slash && slash > system && slash[1];
}

static int byNumber(long long a, long long b) {
	return a < b ? -1 : a > b;
}

int SM_compare(int mode, const SMKey* a, const SMKey* b, const char* name_a, const char* name_b) {
	if (mode <= SM_NAME || mode >= SM_MODES || !a || !b) return 0;
	if (a->kind != b->kind) return a->kind == SM_FOLDER ? -1 : 1;
	if (a->kind == SM_FOLDER) return 0;
	if (a->has != b->has) return a->has ? -1 : 1;
	if (a->has) {
		int order = 0;
		switch (mode) {
		case SM_YEAR: order = byNumber(a->value, b->value); break;
		case SM_PUBLISHER: order = strcasecmp(a->text ? a->text : "", b->text ? b->text : ""); break;
		default: order = byNumber(b->value, a->value); break;
		}
		if (order) return order < 0 ? -1 : 1;
	}
	int order = strcasecmp(name_a ? name_a : "", name_b ? name_b : "");
	return order < 0 ? -1 : order > 0;
}

int SM_yearOf(const char* text) {
	if (!text) return 0;
	while (*text == ' ') text++;
	int year = 0;
	for (int i = 0; i < 4; i++) {
		if (!isdigit((unsigned char)text[i])) return 0;
		year = year * 10 + (text[i] - '0');
	}
	return isdigit((unsigned char)text[4]) ? 0 : year;
}

// The start of the local day t is in.
static long long dayStart(long long t) {
	time_t when = (time_t)t;
	struct tm local;
	if (!localtime_r(&when, &local)) return t;
	local.tm_hour = local.tm_min = local.tm_sec = 0;
	local.tm_isdst = -1;
	return (long long)mktime(&local);
}

static int yearIn(long long t) {
	time_t when = (time_t)t;
	struct tm local;
	return localtime_r(&when, &local) ? local.tm_year + 1900 : 0;
}

const char* SM_section(int mode, const SMKey* key, long long now, char* out, size_t size) {
	if (out && size) out[0] = '\0';
	if (!key || !out || !size || mode <= SM_NAME || mode >= SM_MODES || key->kind == SM_FOLDER) return NULL;
	if (!key->has) {
		switch (mode) {
		case SM_YEAR: return TR_MARK("No year");
		case SM_PUBLISHER: return TR_MARK("No publisher");
		case SM_ADDED: return TR_MARK("No date");
		default: return TR_MARK("Not played");
		}
	}
	switch (mode) {
	case SM_MOST_PLAYED:
		if (key->value >= 100 * 3600LL) return TR_MARK("100h+");
		if (key->value >= 10 * 3600LL) return TR_MARK("10h+");
		if (key->value >= 3600LL) return TR_MARK("1h+");
		return TR_MARK("Under 1h");
	case SM_YEAR:
		snprintf(out, size, "%lld", key->value);
		return NULL;
	case SM_PUBLISHER:
		snprintf(out, size, "%s", key->text ? key->text : "");
		return NULL;
	default: {
		long long today = dayStart(now);
		if (key->value >= today) return TR_MARK("Today");
		if (key->value >= dayStart(today - 6 * 86400LL + 43200)) return TR_MARK("Last 7 days");
		if (key->value >= dayStart(today - 29 * 86400LL + 43200)) return TR_MARK("Last 30 days");
		snprintf(out, size, "%d", yearIn(key->value));
		return NULL;
	}
	}
}

// The last dot's extension, as stock cuts it: 2 to 4 characters after
// the dot. The length without it.
static size_t withoutExtension(const char* name, size_t len) {
	for (size_t i = len; i > 0; i--) {
		if (name[i - 1] != '.') continue;
		size_t ext = len - i;
		return ext >= 2 && ext <= 4 && i > 1 ? i - 1 : len;
	}
	return len;
}

// The length of a disc marker at s, "(Disc 1)" or "(cd 2 of 3)", or 0.
static size_t marker(const char* s) {
	static const char* words[] = { "disc", "disk", "cd" };
	if (*s != '(') return 0;
	for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++) {
		size_t n = strlen(words[w]);
		if (strncasecmp(s + 1, words[w], n) != 0 || s[1 + n] != ' ') continue;
		const char* p = s + 2 + n;
		if (!isdigit((unsigned char)*p)) return 0;
		while (isdigit((unsigned char)*p)) p++;
		if (!strncasecmp(p, " of ", 4) && isdigit((unsigned char)p[4])) {
			p += 4;
			while (isdigit((unsigned char)*p)) p++;
		}
		return *p == ')' ? (size_t)(p + 1 - s) : 0;
	}
	return 0;
}

int SM_discGame(const char* file, char* out, size_t size) {
	if (!file || !out || !size) return 0;
	out[0] = '\0';
	size_t file_len = withoutExtension(file, strlen(file));
	const char* found = NULL;
	size_t found_len = 0;
	for (size_t i = 0; i < file_len; i++) {
		size_t n = marker(file + i);
		if (!n || i + n > file_len) continue;
		if (found) return 0; // more than one
		found = file + i;
		found_len = n;
	}
	if (!found) return 0;
	size_t start = (size_t)(found - file), end = start + found_len;
	// with the space before it, or after it at the start
	if (start > 0 && file[start - 1] == ' ') start--;
	else if (end < file_len && file[end] == ' ') end++;
	size_t rest = file_len - end;
	if (start + rest == 0 || start + rest >= size) return 0;
	memcpy(out, file, start);
	memcpy(out + start, file + end, rest);
	out[start + rest] = '\0';
	return 1;
}

int SM_discOf(const char* row, const char* file) {
	if (!row || !file) return 0;
	char game[512];
	if (!SM_discGame(file, game, sizeof(game))) return 0;
	size_t row_len = withoutExtension(row, strlen(row));
	return strlen(game) == row_len && !strncasecmp(game, row, row_len);
}

///////////////////////////////////////
// the cache

struct SMItem {
	char* path;
	SMKey key;
	SMItem* next;
};

static size_t hash(const char* s) {
	unsigned h = 2166136261u;
	while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

void SM_cacheInit(SMCache* cache) {
	memset(cache, 0, sizeof(*cache));
}

static void freeItem(SMItem* item) {
	free(item->path);
	free(item->key.text);
	free(item);
}

void SM_cacheClear(SMCache* cache) {
	for (size_t i = 0; i < cache->bucket_count; i++) {
		SMItem* item = cache->buckets[i];
		while (item) {
			SMItem* next = item->next;
			freeItem(item);
			item = next;
		}
	}
	free(cache->buckets);
	SM_cacheInit(cache);
}

const SMKey* SM_cacheGet(const SMCache* cache, const char* path) {
	if (!path || !cache->bucket_count) return NULL;
	for (SMItem* item = cache->buckets[hash(path) % cache->bucket_count]; item; item = item->next) {
		if (!strcmp(item->path, path)) return &item->key;
	}
	return NULL;
}

// Twice the buckets once there are twice as many items, so chains stay
// short. 0, or -1 when memory ran out, keeping the old ones.
static int grow(SMCache* cache) {
	size_t count = cache->bucket_count ? cache->bucket_count * 2 : 1024;
	SMItem** buckets = calloc(count, sizeof(SMItem*));
	if (!buckets) return -1;
	for (size_t i = 0; i < cache->bucket_count; i++) {
		SMItem* item = cache->buckets[i];
		while (item) {
			SMItem* next = item->next;
			size_t at = hash(item->path) % count;
			item->next = buckets[at];
			buckets[at] = item;
			item = next;
		}
	}
	free(cache->buckets);
	cache->buckets = buckets;
	cache->bucket_count = count;
	return 0;
}

const SMKey* SM_cachePut(SMCache* cache, const char* path, const SMKey* key) {
	if (!path || !key) return NULL;
	if ((!cache->bucket_count || cache->count >= cache->bucket_count * 2) && grow(cache) != 0 && !cache->bucket_count) return NULL;
	char* text = key->text ? strdup(key->text) : NULL;
	if (key->text && !text) return NULL;
	SMItem** at = &cache->buckets[hash(path) % cache->bucket_count];
	while (*at && strcmp((*at)->path, path)) at = &(*at)->next;
	if (*at) {
		free((*at)->key.text);
		(*at)->key = *key;
		(*at)->key.text = text;
		return &(*at)->key;
	}
	SMItem* item = malloc(sizeof(*item));
	char* copy = strdup(path);
	if (!item || !copy) {
		free(item);
		free(copy);
		free(text);
		return NULL;
	}
	item->path = copy;
	item->key = *key;
	item->key.text = text;
	item->next = NULL;
	*at = item;
	cache->count++;
	return &item->key;
}

///////////////////////////////////////
// the memo


void SM_memoInit(SMMemo* memo) {
	memset(memo, 0, sizeof(*memo));
}

void SM_memoFree(SMMemo* memo) {
	free(memo->entries);
	free(memo->keys);
	SM_memoInit(memo);
}

void SM_memoEmpty(SMMemo* memo) {
	if (memo->room) memset(memo->entries, 0, sizeof(void*) * memo->room);
	memo->count = 0;
}

static size_t slotOf(const void* entry, size_t room) {
	uint64_t h = (uint64_t)(uintptr_t)entry;
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	return (size_t)h & (room - 1);
}

const SMKey* SM_memoGet(const SMMemo* memo, const void* entry) {
	if (!memo->room || !entry) return NULL;
	for (size_t at = slotOf(entry, memo->room);; at = (at + 1) & (memo->room - 1)) {
		if (memo->entries[at] == entry) return memo->keys[at];
		if (!memo->entries[at]) return NULL;
	}
}

static void place(const void** entries, const SMKey** keys, size_t room, const void* entry, const SMKey* key) {
	size_t at = slotOf(entry, room);
	while (entries[at] && entries[at] != entry) at = (at + 1) & (room - 1);
	entries[at] = entry;
	keys[at] = key;
}

int SM_memoPut(SMMemo* memo, const void* entry, const SMKey* key) {
	if (!entry || !key) return -1;
	// at most half full, so a miss stops soon
	if ((memo->count + 1) * 2 > memo->room) {
		size_t room = memo->room ? memo->room * 2 : 1024;
		const void** entries = calloc(room, sizeof(void*));
		const SMKey** keys = calloc(room, sizeof(SMKey*));
		if (!entries || !keys) {
			free(entries);
			free(keys);
			return -1;
		}
		for (size_t i = 0; i < memo->room; i++) {
			if (memo->entries[i]) place(entries, keys, room, memo->entries[i], memo->keys[i]);
		}
		free(memo->entries);
		free(memo->keys);
		memo->entries = entries;
		memo->keys = keys;
		memo->room = room;
	}
	if (!SM_memoGet(memo, entry)) memo->count++;
	place(memo->entries, memo->keys, memo->room, entry, key);
	return 0;
}
