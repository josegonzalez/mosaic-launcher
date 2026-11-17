#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "indexmodel.h"
#include "infomodel.h"
#include "mosaic_plugin.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define NAME_MAX_LEN 256
#define INFO_NAMES_MAX 100000 // .info files listed in one .media folder

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

// Names stock leaves out of its lists.
static int hiddenName(const char* name) {
	return name[0] == '.' || endsWith(name, ".disabled") || !strcmp(name, "map.txt");
}

// A modification time in nanoseconds where the file system has them, or
// -1 for a path that isn't there.
static long long mtimeOf(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0) return -1;
#if defined(__APPLE__)
	return (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
	return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#else
	return (long long)st.st_mtime * 1000000000LL;
#endif
}

// What a folder's time is kept as: its time, or INDEX_UNSURE for one that
// changed so recently that another change in the same tick of the file
// system's clock (2 seconds on FAT) wouldn't show, so it's read again.
static long long stampOf(const char* path, long long started) {
	long long mtime = mtimeOf(path);
	return mtime >= 0 && mtime > started - INDEX_RACY_NS ? INDEX_UNSURE : mtime;
}

static int exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static int isDir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Up to max bytes of a file, NUL-terminated, with its length in *len.
static char* readFile(const char* path, size_t max, size_t* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	size_t cap = 4096, used = 0;
	char* text = malloc(cap + 1);
	while (text) {
		if (used == cap) {
			if (cap >= max) break;
			size_t next = cap * 2 > max ? max : cap * 2;
			char* bigger = realloc(text, next + 1);
			if (!bigger) {
				free(text);
				text = NULL;
				break;
			}
			text = bigger;
			cap = next;
		}
		size_t got = fread(text + used, 1, cap - used, f);
		if (!got) break;
		used += got;
	}
	fclose(f);
	if (!text) return NULL;
	text[used] = '\0';
	if (len) *len = used;
	return text;
}

///////////////////////////////////////
// names

void Index_cleanName(const char* file_name, char* out, size_t size) {
	if (!size) return;
	snprintf(out, size, "%s", file_name);

	char* dot;
	while ((dot = strrchr(out, '.')) != NULL) {
		size_t len = strlen(dot);
		if (len < 3 || len > 5) break;
		*dot = '\0';
	}

	char bare[NAME_MAX_LEN];
	snprintf(bare, sizeof(bare), "%s", out);
	for (;;) {
		char* open = strrchr(out, '(');
		if (!open) open = strrchr(out, '[');
		if (!open || open == out) break;
		*open = '\0';
	}
	if (!out[0]) snprintf(out, size, "%s", bare);

	size_t len = strlen(out);
	while (len > 1 && isspace((unsigned char)out[len - 1])) out[--len] = '\0';
}

void Index_emuTag(const char* system, char* out, size_t size) {
	if (!size) return;
	const char* open = strrchr(system, '(');
	if (!open) {
		snprintf(out, size, "%s", system);
		return;
	}
	const char* close = strchr(open + 1, ')');
	size_t len = close ? (size_t)(close - open - 1) : strlen(open + 1);
	if (len >= size) len = size - 1;
	memcpy(out, open + 1, len);
	out[len] = '\0';
}

///////////////////////////////////////
// map.txt: "file name<tab>alias" lines, the later of two for a name winning

typedef struct MapItem {
	const char* key;
	const char* value;
	int line;
} MapItem;

typedef struct Map {
	char* text;
	MapItem* items;
	int count;
} Map;

static int compareItems(const void* a, const void* b) {
	const MapItem* x = a;
	const MapItem* y = b;
	int c = strcmp(x->key, y->key);
	return c ? c : x->line - y->line;
}

static void mapRead(Map* map, const char* path) {
	memset(map, 0, sizeof(*map));
	char* text = readFile(path, 16 * 1024 * 1024, NULL);
	if (!text) return;
	map->text = text;
	int lines = 1;
	for (const char* p = text; *p; p++) lines += *p == '\n';
	map->items = malloc(sizeof(MapItem) * (size_t)lines);
	if (!map->items) return;
	int line = 0;
	for (char* p = text; p; line++) {
		char* end = strchr(p, '\n');
		if (end) *end = '\0';
		size_t len = strlen(p);
		while (len > 0 && p[len - 1] == '\r') p[--len] = '\0';
		char* tab = strchr(p, '\t');
		if (tab && tab[1]) {
			*tab = '\0';
			map->items[map->count++] = (MapItem){ p, tab + 1, line };
		}
		p = end ? end + 1 : NULL;
	}
	qsort(map->items, (size_t)map->count, sizeof(MapItem), compareItems);
	int kept = 0;
	for (int i = 0; i < map->count; i++) {
		if (i + 1 < map->count && !strcmp(map->items[i].key, map->items[i + 1].key)) continue;
		map->items[kept++] = map->items[i];
	}
	map->count = kept;
}

static const char* mapGet(const Map* map, const char* name) {
	int lo = 0, hi = map->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(name, map->items[mid].key);
		if (!c) return map->items[mid].value;
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}

static void mapFree(Map* map) {
	free(map->items);
	free(map->text);
	memset(map, 0, sizeof(*map));
}

// The name an entry shows as: its alias, else its tidied name. 0 when an
// alias hides it.
static int shownName(const Map* map, const char* file_name, char* out, size_t size) {
	const char* alias = mapGet(map, file_name);
	if (alias) {
		if (alias[0] == '.') return 0;
		snprintf(out, size, "%s", alias);
		return 1;
	}
	Index_cleanName(file_name, out, size);
	return 1;
}

///////////////////////////////////////
// a system's strings, entries and stamps

static uint32_t addString(IndexSystem* sys, const char* s) {
	if (!s || !s[0]) return 0;
	size_t len = strlen(s) + 1;
	if (sys->strings_len + len > UINT32_MAX) return 0;
	if (sys->strings_len + len > sys->strings_cap) {
		size_t cap = sys->strings_cap ? sys->strings_cap * 2 : 4096;
		while (cap < sys->strings_len + len) cap *= 2;
		char* bigger = realloc(sys->strings, cap);
		if (!bigger) return 0;
		sys->strings = bigger;
		sys->strings_cap = cap;
	}
	if (!sys->strings_len) sys->strings[sys->strings_len++] = '\0'; // offset 0 is ""
	uint32_t at = (uint32_t)sys->strings_len;
	memcpy(sys->strings + sys->strings_len, s, len);
	sys->strings_len += len;
	return at;
}

const char* Index_str(const IndexSystem* sys, uint32_t offset) {
	return sys && sys->strings && offset ? sys->strings + offset : "";
}

static int addEntry(IndexSystem* sys, const IndexEntry* entry) {
	if (sys->count == sys->cap) {
		int cap = sys->cap ? sys->cap * 2 : 64;
		IndexEntry* bigger = realloc(sys->entries, sizeof(IndexEntry) * (size_t)cap);
		if (!bigger) return -1;
		sys->entries = bigger;
		sys->cap = cap;
	}
	sys->entries[sys->count++] = *entry;
	return 0;
}

static int addStamp(IndexSystem* sys, const char* path, long long mtime) {
	if (sys->stamp_count == sys->stamp_cap) {
		int cap = sys->stamp_cap ? sys->stamp_cap * 2 : 16;
		IndexStamp* bigger = realloc(sys->stamps, sizeof(IndexStamp) * (size_t)cap);
		if (!bigger) return -1;
		sys->stamps = bigger;
		sys->stamp_cap = cap;
	}
	uint32_t at = addString(sys, path);
	if (!at) return -1;
	sys->stamps[sys->stamp_count++] = (IndexStamp){ at, mtime };
	return 0;
}

static void systemFree(IndexSystem* sys) {
	free(sys->folder);
	free(sys->tag);
	free(sys->strings);
	free(sys->entries);
	free(sys->stamps);
	memset(sys, 0, sizeof(*sys));
}

static int systemCopy(IndexSystem* out, const IndexSystem* in) {
	memset(out, 0, sizeof(*out));
	out->folder = strdup(in->folder);
	out->tag = strdup(in->tag);
	out->emulator = in->emulator;
	out->strings = malloc(in->strings_len ? in->strings_len : 1);
	out->entries = malloc(sizeof(IndexEntry) * (size_t)(in->count ? in->count : 1));
	out->stamps = malloc(sizeof(IndexStamp) * (size_t)(in->stamp_count ? in->stamp_count : 1));
	if (!out->folder || !out->tag || !out->strings || !out->entries || !out->stamps) {
		systemFree(out);
		return -1;
	}
	if (in->strings_len) memcpy(out->strings, in->strings, in->strings_len);
	out->strings_len = out->strings_cap = in->strings_len;
	if (in->count) memcpy(out->entries, in->entries, sizeof(IndexEntry) * (size_t)in->count);
	out->count = out->cap = in->count;
	if (in->stamp_count) memcpy(out->stamps, in->stamps, sizeof(IndexStamp) * (size_t)in->stamp_count);
	out->stamp_count = out->stamp_cap = in->stamp_count;
	return 0;
}

// Gives back the room kept past what a system holds, once it's read, as
// it isn't added to again.
static void shrink(IndexSystem* sys) {
	char* strings = realloc(sys->strings, sys->strings_len ? sys->strings_len : 1);
	if (strings) {
		sys->strings = strings;
		sys->strings_cap = sys->strings_len;
	}
	IndexEntry* entries = realloc(sys->entries, sizeof(IndexEntry) * (size_t)(sys->count ? sys->count : 1));
	if (entries) {
		sys->entries = entries;
		sys->cap = sys->count;
	}
}

// Sorts a system's entries by path, bytewise: a merge sort, as qsort
// can't be handed the strings.
static void sortEntries(IndexSystem* sys) {
	int n = sys->count;
	if (n < 2) return;
	IndexEntry* tmp = malloc(sizeof(IndexEntry) * (size_t)n);
	if (!tmp) return;
	IndexEntry* a = sys->entries;
	for (int width = 1; width < n; width *= 2) {
		for (int lo = 0; lo < n; lo += 2 * width) {
			int mid = lo + width < n ? lo + width : n;
			int hi = lo + 2 * width < n ? lo + 2 * width : n;
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi) {
				if (strcmp(sys->strings + a[j].path, sys->strings + a[i].path) < 0) tmp[k++] = a[j++];
				else tmp[k++] = a[i++];
			}
			while (i < mid) tmp[k++] = a[i++];
			while (j < hi) tmp[k++] = a[j++];
		}
		memcpy(a, tmp, sizeof(IndexEntry) * (size_t)n);
	}
	free(tmp);
}

///////////////////////////////////////
// reading the card

typedef struct Names {
	char** items;
	int count, cap;
} Names;

static int compareNames(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

static void namesFree(Names* names) {
	for (int i = 0; i < names->count; i++) free(names->items[i]);
	free(names->items);
	memset(names, 0, sizeof(*names));
}

static int namesAdd(Names* names, const char* name, size_t len) {
	if (names->count == names->cap) {
		int cap = names->cap ? names->cap * 2 : 32;
		char** bigger = realloc(names->items, sizeof(char*) * (size_t)cap);
		if (!bigger) return -1;
		names->items = bigger;
		names->cap = cap;
	}
	char* copy = malloc(len + 1);
	if (!copy) return -1;
	memcpy(copy, name, len);
	copy[len] = '\0';
	names->items[names->count++] = copy;
	return 0;
}

static int namesHas(const Names* names, const char* name) {
	return names->count && bsearch(&name, names->items, (size_t)names->count, sizeof(char*), compareNames) != NULL;
}

// The .info files in a folder's .media, by name without .info.
static void infoNames(const char* media, Names* out) {
	memset(out, 0, sizeof(*out));
	DIR* dh = opendir(media);
	if (!dh) return;
	struct dirent* dp;
	while ((dp = readdir(dh)) != NULL && out->count < INFO_NAMES_MAX) {
		size_t len = strlen(dp->d_name);
		if (len <= 5 || dp->d_name[0] == '.' || strcmp(dp->d_name + len - 5, ".info") != 0) continue;
		if (namesAdd(out, dp->d_name, len - 5) != 0) break;
	}
	closedir(dh);
	if (out->count > 1) qsort(out->items, (size_t)out->count, sizeof(char*), compareNames);
}

typedef struct Scan {
	const IndexCard* card;
	IndexSystem* sys;
	volatile int* cancel;
	long long started; // when the build began, in nanoseconds
	int failed;
} Scan;

static int cancelled(const Scan* scan) {
	return scan->cancel && *scan->cancel;
}

// What a dirent is: 1 a folder, 0 anything else, -1 gone.
static int direntIsDir(const struct dirent* dp, const char* path) {
#ifdef DT_DIR
	if (dp->d_type == DT_DIR) return 1;
	if (dp->d_type == DT_REG) return 0;
#endif
	struct stat st;
	if (stat(path, &st) != 0) return -1;
	return S_ISDIR(st.st_mode) ? 1 : 0;
}

// The year, publisher, genre and players of a .info file listed in its folder's
// .media.
static void readInfo(Scan* scan, const Names* infos, const char* path, const char* file_name, IndexEntry* entry) {
	if (!infos->count) return;
	char base[NAME_MAX_LEN * 2];
	if (snprintf(base, sizeof(base), "%s", file_name) >= (int)sizeof(base)) return;
	char* dot = strrchr(base, '.');
	if (dot && dot != base) *dot = '\0';
	if (!namesHas(infos, base)) return;
	char info_path[PATH_MAX];
	Info_path(path, info_path, sizeof(info_path));
	size_t len = 0;
	char* data = readFile(info_path, INFO_FILE_MAX + 1, &len);
	if (!data) return;
	const char* why = NULL;
	InfoFile* info = len <= INFO_FILE_MAX ? Info_parse(data, len, &why) : NULL;
	free(data);
	if (!info) return;
	entry->year = addString(scan->sys, Info_get(info, "year"));
	entry->publisher = addString(scan->sys, Info_get(info, "publisher"));
	entry->genre = addString(scan->sys, Info_get(info, "genre"));
	entry->players = addString(scan->sys, Info_get(info, "players"));
	free(info);
}

static void scanFolder(Scan* scan, const char* dir, int depth) {
	DIR* dh = opendir(dir);
	if (!dh) return;
	char extra[PATH_MAX];
	if (addStamp(scan->sys, dir, stampOf(dir, scan->started)) != 0) scan->failed = 1;
	Map map;
	memset(&map, 0, sizeof(map));
	if (snprintf(extra, sizeof(extra), "%s/map.txt", dir) < (int)sizeof(extra) && exists(extra)) {
		if (addStamp(scan->sys, extra, stampOf(extra, scan->started)) != 0) scan->failed = 1;
		mapRead(&map, extra);
	}
	Names infos;
	memset(&infos, 0, sizeof(infos));
	if (snprintf(extra, sizeof(extra), "%s/.media", dir) < (int)sizeof(extra) && isDir(extra)) {
		if (addStamp(scan->sys, extra, stampOf(extra, scan->started)) != 0) scan->failed = 1;
		infoNames(extra, &infos);
	}

	struct dirent* dp;
	while (!scan->failed && !cancelled(scan) && (dp = readdir(dh)) != NULL) {
		if (hiddenName(dp->d_name)) continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name) >= (int)sizeof(path)) continue;
		int is_dir = direntIsDir(dp, path);
		if (is_dir < 0) continue;
		char name[NAME_MAX_LEN];
		if (!shownName(&map, dp->d_name, name, sizeof(name))) continue;

		IndexEntry entry;
		memset(&entry, 0, sizeof(entry));
		entry.depth = (int8_t)depth;
		entry.kind = INDEX_KIND_GAME;
		entry.type = MOSAIC_ENTRY_ROM;
		char launch[PATH_MAX] = "";
		if (is_dir) {
			entry.type = MOSAIC_ENTRY_DIR;
			if (endsWith(dp->d_name, ".pak")) entry.type = MOSAIC_ENTRY_PAK;
			else if ((snprintf(launch, sizeof(launch), "%s/%s.cue", path, dp->d_name) < (int)sizeof(launch) && exists(launch))
					|| (snprintf(launch, sizeof(launch), "%s/%s.m3u", path, dp->d_name) < (int)sizeof(launch) && exists(launch))) {
				// a game folder, which launches its .cue or .m3u
			}
			else {
				launch[0] = '\0';
				entry.kind = INDEX_KIND_FOLDER;
			}
		}
		entry.path = addString(scan->sys, path);
		entry.launch = launch[0] ? addString(scan->sys, launch) : entry.path;
		entry.name = addString(scan->sys, name);
		if (!entry.path || !entry.name || (launch[0] && !entry.launch)) {
			scan->failed = 1;
			break;
		}
		readInfo(scan, &infos, path, dp->d_name, &entry);
		if (addEntry(scan->sys, &entry) != 0) {
			scan->failed = 1;
			break;
		}
		if (entry.kind == INDEX_KIND_FOLDER && depth < INDEX_DEPTH_MAX) scanFolder(scan, path, depth + 1);
	}
	closedir(dh);
	namesFree(&infos);
	mapFree(&map);
}

static int hasEmulator(const IndexCard* card, const char* tag) {
	char path[PATH_MAX];
	if (!card || !card->sdcard || !card->platform) return 0;
	if (snprintf(path, sizeof(path), "%s/.system/%s/paks/Emus/%s.pak/launch.sh", card->sdcard, card->platform, tag) < (int)sizeof(path)
			&& exists(path)) return 1;
	return snprintf(path, sizeof(path), "%s/Emus/%s/%s.pak/launch.sh", card->sdcard, card->platform, tag) < (int)sizeof(path)
		&& exists(path);
}

static int readSystem(IndexSystem* sys, const IndexCard* card, const char* roms, const char* folder, const char* name,
		volatile int* cancel, long long started) {
	memset(sys, 0, sizeof(*sys));
	char tag[NAME_MAX_LEN];
	Index_emuTag(folder, tag, sizeof(tag));
	sys->folder = strdup(folder);
	sys->tag = strdup(tag);
	if (!sys->folder || !sys->tag) {
		systemFree(sys);
		return -1;
	}
	sys->emulator = hasEmulator(card, tag);
	char path[PATH_MAX];
	if (snprintf(path, sizeof(path), "%s/%s", roms, folder) >= (int)sizeof(path)) {
		systemFree(sys);
		return -1;
	}
	IndexEntry entry;
	memset(&entry, 0, sizeof(entry));
	entry.kind = INDEX_KIND_SYSTEM;
	entry.type = MOSAIC_ENTRY_DIR;
	entry.depth = -1;
	entry.path = entry.launch = addString(sys, path);
	entry.name = addString(sys, name);
	if (!entry.path || !entry.name || addEntry(sys, &entry) != 0) {
		systemFree(sys);
		return -1;
	}
	Scan scan = { card, sys, cancel, started, 0 };
	scanFolder(&scan, path, 0);
	if (scan.failed || cancelled(&scan)) {
		systemFree(sys);
		return -1;
	}
	sortEntries(sys);
	shrink(sys);
	return 0;
}

// 1 when everything a system was read from has the time it had.
static int systemCurrent(const IndexSystem* sys) {
	if (!sys->stamp_count) return 0;
	for (int i = 0; i < sys->stamp_count; i++) {
		if (mtimeOf(Index_str(sys, sys->stamps[i].path)) != sys->stamps[i].mtime) return 0;
	}
	return 1;
}

static const IndexSystem* findSystem(const CardIndex* index, const char* folder, size_t len) {
	if (!index) return NULL;
	int lo = 0, hi = index->system_count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		const char* name = index->systems[mid].folder;
		int c = strncmp(folder, name, len);
		if (!c && name[len]) c = -1;
		if (!c) return &index->systems[mid];
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}

static int compareSystems(const void* a, const void* b) {
	return strcmp(((const IndexSystem*)a)->folder, ((const IndexSystem*)b)->folder);
}

static void finish(CardIndex* index) {
	if (index->system_count > 1) qsort(index->systems, (size_t)index->system_count, sizeof(IndexSystem), compareSystems);
	index->total = 0;
	for (int i = 0; i < index->system_count; i++) {
		index->systems[i].first = index->total;
		index->total += index->systems[i].count;
	}
}

void Index_init(CardIndex* index) {
	memset(index, 0, sizeof(*index));
	index->roms_mtime = index->map_mtime = -1;
}

void Index_free(CardIndex* index) {
	for (int i = 0; i < index->system_count; i++) systemFree(&index->systems[i]);
	free(index->systems);
	free(index->roms);
	Index_init(index);
}

// The same entries, field for field.
static int sameSystem(const IndexSystem* a, const IndexSystem* b) {
	if (strcmp(a->folder, b->folder) != 0 || a->emulator != b->emulator || a->count != b->count) return 0;
	for (int i = 0; i < a->count; i++) {
		const IndexEntry* x = &a->entries[i];
		const IndexEntry* y = &b->entries[i];
		if (x->kind != y->kind || x->type != y->type || x->depth != y->depth) return 0;
		const uint32_t xs[] = { x->path, x->launch, x->name, x->year, x->publisher, x->genre, x->players };
		const uint32_t ys[] = { y->path, y->launch, y->name, y->year, y->publisher, y->genre, y->players };
		for (size_t k = 0; k < sizeof(xs) / sizeof(xs[0]); k++) {
			if (strcmp(Index_str(a, xs[k]), Index_str(b, ys[k])) != 0) return 0;
		}
	}
	return 1;
}

static int sameSystems(const CardIndex* a, const CardIndex* b) {
	if (a->system_count != b->system_count || a->total != b->total) return 0;
	for (int i = 0; i < a->system_count; i++) {
		if (!sameSystem(&a->systems[i], &b->systems[i])) return 0;
	}
	return 1;
}

static long long nowNs(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int Index_build(CardIndex* index, const IndexCard* card, const CardIndex* prev, volatile int* cancel, IndexBuildStats* stats) {
	IndexBuildStats unused;
	if (!stats) stats = &unused;
	memset(stats, 0, sizeof(*stats));
	Index_init(index);
	char roms[PATH_MAX];
	if (!card || !card->sdcard || snprintf(roms, sizeof(roms), "%s/Roms", card->sdcard) >= (int)sizeof(roms)) return -1;
	index->roms = strdup(roms);
	if (!index->roms) return -1;
	if (prev && prev->roms && strcmp(prev->roms, roms) != 0) prev = NULL;

	long long started = nowNs();
	char map_path[PATH_MAX];
	if (snprintf(map_path, sizeof(map_path), "%s/map.txt", roms) >= (int)sizeof(map_path)) {
		Index_free(index);
		return -1;
	}
	index->roms_mtime = mtimeOf(roms);
	index->map_mtime = stampOf(map_path, started);
	// a system's name comes from Roms/map.txt, so a change there reads every system again
	int reuse = prev && prev->map_mtime == mtimeOf(map_path);
	Map map;
	mapRead(&map, map_path);

	DIR* dh = opendir(roms);
	int cap = 0, failed = 0;
	struct dirent* dp;
	while (dh && !failed && !(cancel && *cancel) && (dp = readdir(dh)) != NULL) {
		if (hiddenName(dp->d_name)) continue;
		char path[PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", roms, dp->d_name) >= (int)sizeof(path)) continue;
		if (direntIsDir(dp, path) != 1) continue;
		char name[NAME_MAX_LEN];
		if (!shownName(&map, dp->d_name, name, sizeof(name))) continue;
		if (index->system_count == cap) {
			int next = cap ? cap * 2 : 32;
			IndexSystem* bigger = realloc(index->systems, sizeof(IndexSystem) * (size_t)next);
			if (!bigger) {
				failed = 1;
				break;
			}
			index->systems = bigger;
			cap = next;
		}
		IndexSystem* sys = &index->systems[index->system_count];
		const IndexSystem* was = reuse ? findSystem(prev, dp->d_name, strlen(dp->d_name)) : NULL;
		if (was && systemCurrent(was)) {
			if (systemCopy(sys, was) != 0) {
				failed = 1;
				break;
			}
			sys->emulator = hasEmulator(card, sys->tag);
			stats->systems_kept++;
		}
		else {
			if (readSystem(sys, card, roms, dp->d_name, name, cancel, started) != 0) {
				failed = 1;
				break;
			}
			stats->systems_read++;
		}
		index->system_count++;
	}
	if (dh) closedir(dh);
	mapFree(&map);
	if (failed || (cancel && *cancel)) {
		Index_free(index);
		return -1;
	}
	finish(index);
	// what was read again may be just as it was, such as a folder touched
	stats->changed = !prev || !sameSystems(index, prev);
	stats->restamped = stats->systems_read > 0 || !prev || prev->roms_mtime != index->roms_mtime
		|| prev->map_mtime != index->map_mtime;
	return 0;
}

///////////////////////////////////////
// asking

static const IndexSystem* systemNamed(const CardIndex* index, const char* system) {
	return system ? findSystem(index, system, strlen(system)) : NULL;
}

int Index_count(const CardIndex* index, const char* system) {
	if (!index) return 0;
	if (!system) return index->total;
	const IndexSystem* sys = systemNamed(index, system);
	return sys ? sys->count : 0;
}

const IndexEntry* Index_at(const CardIndex* index, const char* system, int i, const IndexSystem** out) {
	if (!index || i < 0) return NULL;
	const IndexSystem* sys = NULL;
	if (system) {
		sys = systemNamed(index, system);
		if (!sys || i >= sys->count) return NULL;
	}
	else {
		if (i >= index->total) return NULL;
		int lo = 0, hi = index->system_count - 1;
		while (lo < hi) { // the last system starting at or before i
			int mid = lo + (hi - lo + 1) / 2;
			if (index->systems[mid].first <= i) lo = mid;
			else hi = mid - 1;
		}
		sys = &index->systems[lo];
		i -= sys->first;
		if (i >= sys->count) return NULL;
	}
	if (out) *out = sys;
	return &sys->entries[i];
}

int Index_find(const CardIndex* index, const char* path, const IndexEntry** entry, const IndexSystem** out) {
	if (!index || !index->roms || !path) return -1;
	size_t len = strlen(index->roms);
	if (strncmp(path, index->roms, len) != 0 || path[len] != '/') return -1;
	const char* folder = path + len + 1;
	const char* slash = strchr(folder, '/');
	size_t folder_len = slash ? (size_t)(slash - folder) : strlen(folder);
	const IndexSystem* sys = findSystem(index, folder, folder_len);
	if (!sys) return -1;
	int lo = 0, hi = sys->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(path, Index_str(sys, sys->entries[mid].path));
		if (!c) {
			if (entry) *entry = &sys->entries[mid];
			if (out) *out = sys;
			return sys->first + mid;
		}
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return -1;
}

int Index_withRows(CardIndex* out, const CardIndex* index, const IndexRow* rows, int count) {
	Index_init(out);
	if (!index) return -1;
	out->roms = index->roms ? strdup(index->roms) : NULL;
	out->roms_mtime = index->roms_mtime;
	out->map_mtime = index->map_mtime;
	if (index->roms && !out->roms) return -1;
	if (index->system_count) {
		out->systems = calloc((size_t)index->system_count, sizeof(IndexSystem));
		if (!out->systems) {
			Index_free(out);
			return -1;
		}
	}
	for (int i = 0; i < index->system_count; i++) {
		if (systemCopy(&out->systems[i], &index->systems[i]) != 0) {
			Index_free(out);
			return -1;
		}
		out->systems[i].first = index->systems[i].first;
		out->system_count++;
	}
	out->total = index->total;

	int added = 0;
	size_t roms_len = out->roms ? strlen(out->roms) : 0;
	for (int r = 0; r < count; r++) {
		const char* path = rows[r].path;
		if (!path || !out->roms || strncmp(path, out->roms, roms_len) != 0 || path[roms_len] != '/') continue;
		const char* folder = path + roms_len + 1;
		const char* slash = strchr(folder, '/');
		if (!slash || !slash[1]) continue;
		IndexSystem* sys = (IndexSystem*)findSystem(out, folder, (size_t)(slash - folder));
		if (!sys) continue;
		// already there: found in the copy as it was, rows added since included
		int there = 0;
		for (int k = 0; k < sys->count && !there; k++) there = !strcmp(Index_str(sys, sys->entries[k].path), path);
		if (there) continue;
		int depth = 0;
		for (const char* p = slash + 1; *p; p++) depth += *p == '/';
		if (depth > INDEX_DEPTH_MAX) continue;

		const IndexEntry* from = NULL;
		const IndexSystem* from_sys = NULL;
		if (rows[r].from && rows[r].from[0]) Index_find(index, rows[r].from, &from, &from_sys);
		char name[NAME_MAX_LEN];
		const char* file_name = strrchr(path, '/') + 1;
		if (from) snprintf(name, sizeof(name), "%s", Index_str(from_sys, from->name));
		else Index_cleanName(file_name, name, sizeof(name));

		IndexEntry entry;
		memset(&entry, 0, sizeof(entry));
		entry.kind = INDEX_KIND_GAME;
		entry.type = (uint8_t)rows[r].type;
		entry.depth = (int8_t)depth;
		entry.added = 1;
		entry.path = addString(sys, path);
		entry.launch = entry.path;
		entry.name = addString(sys, name);
		if (from) {
			entry.year = addString(sys, Index_str(from_sys, from->year));
			entry.publisher = addString(sys, Index_str(from_sys, from->publisher));
			entry.genre = addString(sys, Index_str(from_sys, from->genre));
			entry.players = addString(sys, Index_str(from_sys, from->players));
		}
		if (!entry.path || (name[0] && !entry.name) || addEntry(sys, &entry) != 0) {
			Index_free(out);
			return -1;
		}
		added++;
	}
	if (added) {
		for (int i = 0; i < out->system_count; i++) sortEntries(&out->systems[i]);
		finish(out);
	}
	return added;
}

int Index_rename(CardIndex* index, const IndexName* names, int count) {
	if (!index || (count && !names)) return -1;
	int renamed = 0, failed = 0;
	unsigned char* touched = index->system_count ? calloc((size_t)index->system_count, 1) : NULL;
	if (index->system_count && !touched) return -1;
	for (int n = 0; n < count && !failed; n++) {
		const IndexSystem* found = NULL;
		if (!names[n].name || !Index_at(index, NULL, names[n].index, &found)) continue;
		IndexSystem* sys = (IndexSystem*)found;
		uint32_t at = addString(sys, names[n].name);
		if (names[n].name[0] && !at) {
			failed = 1;
			break;
		}
		sys->entries[names[n].index - sys->first].name = at;
		touched[sys - index->systems] = 1;
		renamed++;
	}
	for (int i = 0; i < index->system_count; i++) {
		if (touched[i]) shrink(&index->systems[i]);
	}
	free(touched);
	return failed ? -1 : renamed;
}

size_t Index_bytes(const CardIndex* index) {
	size_t bytes = sizeof(*index) + sizeof(IndexSystem) * (size_t)index->system_count;
	for (int i = 0; i < index->system_count; i++) {
		const IndexSystem* sys = &index->systems[i];
		bytes += sys->strings_cap + sizeof(IndexEntry) * (size_t)sys->cap + sizeof(IndexStamp) * (size_t)sys->stamp_cap;
		bytes += strlen(sys->folder) + strlen(sys->tag) + 2;
	}
	return bytes;
}

///////////////////////////////////////
// the file on the card

static void putEscaped(FILE* f, const char* s) {
	for (; *s; s++) {
		switch (*s) {
		case '\\': fputs("\\\\", f); break;
		case '\t': fputs("\\t", f); break;
		case '\n': fputs("\\n", f); break;
		case '\r': fputs("\\r", f); break;
		default: fputc(*s, f); break;
		}
	}
}

int Index_save(const CardIndex* index, const char* path) {
	if (!index || !index->roms || !path) return -1;
	char temp[PATH_MAX];
	if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return -1;
	FILE* f = fopen(temp, "w");
	if (!f) return -1;
	fprintf(f, "mosaic-card-index\t%d\nroms\t", INDEX_FILE_VERSION);
	putEscaped(f, index->roms);
	fprintf(f, "\t%lld\t%lld\n", index->roms_mtime, index->map_mtime);
	for (int i = 0; i < index->system_count; i++) {
		const IndexSystem* sys = &index->systems[i];
		fputs("system\t", f);
		putEscaped(f, sys->folder);
		fprintf(f, "\t%d\t%d\t%d\n", sys->emulator, sys->count, sys->stamp_count);
		for (int k = 0; k < sys->stamp_count; k++) {
			fprintf(f, "stamp\t%lld\t", sys->stamps[k].mtime);
			putEscaped(f, Index_str(sys, sys->stamps[k].path));
			fputc('\n', f);
		}
		for (int k = 0; k < sys->count; k++) {
			const IndexEntry* e = &sys->entries[k];
			fprintf(f, "entry\t%d\t%d\t%d\t", e->kind, e->type, e->depth);
			const uint32_t fields[] = { e->path, e->launch == e->path ? 0 : e->launch, e->name, e->year, e->publisher, e->genre,
				e->players };
			for (size_t n = 0; n < sizeof(fields) / sizeof(fields[0]); n++) {
				if (n) fputc('\t', f);
				putEscaped(f, Index_str(sys, fields[n]));
			}
			fputc('\n', f);
		}
	}
	fprintf(f, "end\t%d\n", index->system_count);
	int failed = ferror(f) != 0;
	if (fclose(f) != 0) failed = 1;
	if (failed || rename(temp, path) != 0) {
		unlink(temp);
		return -1;
	}
	return 0;
}

// Splits a line at tabs, in place, undoing putEscaped's escapes. Returns
// how many fields there were, or -1 for a bad escape or more than max.
static int fields(char* line, char** out, int max) {
	int n = 0;
	char* w = line;
	out[n++] = w;
	for (char* r = line; *r; r++) {
		if (*r == '\t') {
			*w++ = '\0';
			if (n == max) return -1;
			out[n++] = w;
		}
		else if (*r == '\\') {
			r++;
			switch (*r) {
			case '\\': *w++ = '\\'; break;
			case 't': *w++ = '\t'; break;
			case 'n': *w++ = '\n'; break;
			case 'r': *w++ = '\r'; break;
			default: return -1;
			}
		}
		else *w++ = *r;
	}
	*w = '\0';
	return n;
}

// A whole number field, into *out. 0, or -1 for anything else.
static int number(const char* s, long long lo, long long hi, long long* out) {
	if (!s || !*s) return -1;
	char* end;
	errno = 0;
	long long v = strtoll(s, &end, 10);
	if (*end || errno || v < lo || v > hi) return -1;
	*out = v;
	return 0;
}

static char* nextLine(char** at) {
	char* line = *at;
	if (!line || !*line) return NULL;
	char* end = strchr(line, '\n');
	if (!end) return NULL; // every line ends in one
	*end = '\0';
	*at = end + 1;
	return line;
}

static int loadSystem(CardIndex* index, IndexSystem* sys, char** at, char** f, int n) {
	long long emulator, count, stamps;
	if (n != 5 || strcmp(f[0], "system") != 0 || !f[1][0] || strchr(f[1], '/') || number(f[2], 0, 1, &emulator) != 0
			|| number(f[3], 1, 10000000, &count) != 0 || number(f[4], 0, 10000000, &stamps) != 0) return -1;
	char tag[NAME_MAX_LEN];
	Index_emuTag(f[1], tag, sizeof(tag));
	sys->folder = strdup(f[1]);
	sys->tag = strdup(tag);
	sys->emulator = (int)emulator;
	if (!sys->folder || !sys->tag) return -1;
	char prefix[PATH_MAX];
	if (snprintf(prefix, sizeof(prefix), "%s/%s", index->roms, sys->folder) >= (int)sizeof(prefix)) return -1;
	size_t prefix_len = strlen(prefix);

	for (long long i = 0; i < stamps; i++) {
		char* line = nextLine(at);
		char* g[3];
		long long mtime;
		if (!line || fields(line, g, 3) != 3 || strcmp(g[0], "stamp") != 0 || number(g[1], INDEX_UNSURE, LLONG_MAX, &mtime) != 0
				|| strncmp(g[2], prefix, prefix_len) != 0 || addStamp(sys, g[2], mtime) != 0) return -1;
	}
	for (long long i = 0; i < count; i++) {
		char* line = nextLine(at);
		char* g[11];
		long long kind, type, depth;
		if (!line || fields(line, g, 11) != 11 || strcmp(g[0], "entry") != 0 || number(g[1], 0, 2, &kind) != 0
				|| number(g[2], MOSAIC_ENTRY_DIR, MOSAIC_ENTRY_ROM, &type) != 0 || number(g[3], -1, INDEX_DEPTH_MAX, &depth) != 0
				|| strncmp(g[4], prefix, prefix_len) != 0 || !g[6][0]) return -1;
		// the system folder first, by itself, and every other entry inside it
		int first = i == 0;
		if (first != (kind == INDEX_KIND_SYSTEM) || first != (depth == -1) || first != (g[4][prefix_len] == '\0')
				|| (!first && g[4][prefix_len] != '/')) return -1;
		IndexEntry e;
		memset(&e, 0, sizeof(e));
		e.kind = (uint8_t)kind;
		e.type = (uint8_t)type;
		e.depth = (int8_t)depth;
		e.path = addString(sys, g[4]);
		e.launch = g[5][0] ? addString(sys, g[5]) : e.path;
		e.name = addString(sys, g[6]);
		e.year = addString(sys, g[7]);
		e.publisher = addString(sys, g[8]);
		e.genre = addString(sys, g[9]);
		e.players = addString(sys, g[10]);
		if (!e.path || !e.name || (g[5][0] && !e.launch) || (g[7][0] && !e.year) || (g[8][0] && !e.publisher)
				|| (g[9][0] && !e.genre) || (g[10][0] && !e.players)) return -1;
		// in order, as Index_find looks them up
		if (sys->count && strcmp(Index_str(sys, sys->entries[sys->count - 1].path), g[4]) >= 0) return -1;
		if (addEntry(sys, &e) != 0) return -1;
	}
	shrink(sys);
	return 0;
}

int Index_load(CardIndex* index, const char* path, const char* roms) {
	Index_init(index);
	if (!path || !roms) return -1;
	char* text = readFile(path, 256 * 1024 * 1024, NULL);
	if (!text) return -1;
	char* at = text;
	char* f[10];
	char* line = nextLine(&at);
	long long version, mtime;
	int ok = line && fields(line, f, 10) == 2 && !strcmp(f[0], "mosaic-card-index") && number(f[1], 0, 1000, &version) == 0
		&& version == INDEX_FILE_VERSION;
	if (ok) {
		line = nextLine(&at);
		int n = line ? fields(line, f, 10) : -1;
		ok = n == 4 && !strcmp(f[0], "roms") && !strcmp(f[1], roms) && number(f[2], INDEX_UNSURE, LLONG_MAX, &mtime) == 0;
		if (ok) index->roms_mtime = mtime;
		ok = ok && number(f[3], INDEX_UNSURE, LLONG_MAX, &mtime) == 0;
		if (ok) index->map_mtime = mtime;
		ok = ok && (index->roms = strdup(roms)) != NULL;
	}
	int cap = 0;
	while (ok) {
		line = nextLine(&at);
		int n = line ? fields(line, f, 10) : -1;
		if (n < 1) {
			ok = 0;
			break;
		}
		if (!strcmp(f[0], "end")) {
			long long systems;
			ok = n == 2 && number(f[1], 0, 1000000, &systems) == 0 && systems == index->system_count && !*at;
			break;
		}
		if (index->system_count == cap) {
			int next = cap ? cap * 2 : 32;
			IndexSystem* bigger = realloc(index->systems, sizeof(IndexSystem) * (size_t)next);
			if (!bigger) {
				ok = 0;
				break;
			}
			index->systems = bigger;
			cap = next;
		}
		IndexSystem* sys = &index->systems[index->system_count];
		memset(sys, 0, sizeof(*sys));
		index->system_count++;
		ok = loadSystem(index, sys, &at, f, n) == 0;
		// systems in order too
		if (ok && index->system_count > 1 && strcmp(index->systems[index->system_count - 2].folder, sys->folder) >= 0) ok = 0;
	}
	free(text);
	if (!ok) {
		Index_free(index);
		return -1;
	}
	finish(index);
	return 0;
}
