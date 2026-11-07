#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "games_model.h"

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

void AG_cleanName(const char* file_name, char* out, size_t size) {
	if (!size) return;
	snprintf(out, size, "%s", file_name);

	char* dot;
	while ((dot = strrchr(out, '.')) != NULL) {
		size_t len = strlen(dot);
		if (len < 3 || len > 5) break;
		*dot = '\0';
	}

	char bare[AG_NAME_LEN];
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

///////////////////////////////////////
// map.txt

static int compareItems(const void* a, const void* b) {
	const AGMapItem* x = a;
	const AGMapItem* y = b;
	int c = strcmp(x->key, y->key);
	return c ? c : x->line - y->line;
}

int AG_mapParse(AGMap* map, char* text) {
	memset(map, 0, sizeof(*map));
	map->text = text;
	if (!text) return 0;

	int lines = 1;
	for (const char* p = text; *p; p++) {
		if (*p == '\n') lines++;
	}
	map->items = malloc(sizeof(AGMapItem) * (size_t)lines);
	if (!map->items) return -1;

	int line = 0;
	for (char* p = text; p; line++) {
		char* end = strchr(p, '\n');
		if (end) *end = '\0';
		size_t len = strlen(p);
		while (len > 0 && p[len - 1] == '\r') p[--len] = '\0';
		char* tab = strchr(p, '\t');
		if (tab && tab[1]) {
			*tab = '\0';
			map->items[map->count++] = (AGMapItem){ p, tab + 1, line };
		}
		p = end ? end + 1 : NULL;
	}

	// the later of two lines for a file name wins: keep only the last
	qsort(map->items, (size_t)map->count, sizeof(AGMapItem), compareItems);
	int kept = 0;
	for (int i = 0; i < map->count; i++) {
		if (i + 1 < map->count && !strcmp(map->items[i].key, map->items[i + 1].key)) continue;
		map->items[kept++] = map->items[i];
	}
	map->count = kept;
	return 0;
}

const char* AG_mapGet(const AGMap* map, const char* file_name) {
	if (!map || !map->count) return NULL;
	int lo = 0, hi = map->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(file_name, map->items[mid].key);
		if (!c) return map->items[mid].value;
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}

void AG_mapFree(AGMap* map) {
	free(map->items);
	free(map->text);
	memset(map, 0, sizeof(*map));
}

///////////////////////////////////////
// paths

int AG_hidden(const char* name) {
	return name[0] == '.' || endsWith(name, ".disabled") || !strcmp(name, "map.txt");
}

void AG_emuTag(const char* system, char* out, size_t size) {
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

// The length of the system folder's name in a path under roms, or 0.
static size_t systemLength(const char* roms, const char* path, const char** start) {
	size_t len = strlen(roms);
	if (!path || strncmp(path, roms, len) != 0 || path[len] != '/') return 0;
	const char* system = path + len + 1;
	const char* slash = strchr(system, '/');
	if (!slash || slash == system || !slash[1]) return 0;
	*start = system;
	return (size_t)(slash - system);
}

int AG_systemOf(const char* roms, const char* path, char* out, size_t size) {
	const char* system;
	size_t len = systemLength(roms, path, &system);
	if (!len) return 0;
	if (size) {
		if (len >= size) len = size - 1;
		memcpy(out, system, len);
		out[len] = '\0';
	}
	return 1;
}

int AG_folderArt(const char* path, char* out, size_t size) {
	const char* file = strrchr(path, '/');
	if (!file || file == path) return 0;
	const char* ext = strrchr(file, '.');
	if (!ext || (strcmp(ext, ".cue") && strcmp(ext, ".m3u"))) return 0;

	const char* folder = file - 1;
	while (folder > path && *folder != '/') folder--;
	if (*folder != '/' || folder == path) return 0;
	size_t name_len = (size_t)(file - folder - 1);
	if (name_len == 0 || (size_t)(ext - file - 1) != name_len || strncmp(folder + 1, file + 1, name_len)) return 0;

	int n = snprintf(out, size, "%.*s/.media/%.*s.png", (int)(folder - path), path, (int)name_len, folder + 1);
	return n >= 0 && (size_t)n < size;
}

int AG_rootCompare(const char* roms, const char* a_path, const char* b_path) {
	const char* ignored;
	int a = systemLength(roms, a_path, &ignored) > 0;
	int b = systemLength(roms, b_path, &ignored) > 0;
	return a - b;
}

///////////////////////////////////////
// index

void AG_indexInit(AGIndex* index) {
	memset(index, 0, sizeof(*index));
}

void AG_indexFree(AGIndex* index) {
	for (int i = 0; i < index->game_count; i++) {
		free(index->games[i].path);
		free(index->games[i].name);
	}
	for (int i = 0; i < index->folder_count; i++) free(index->folders[i].path);
	free(index->games);
	free(index->folders);
	AG_indexInit(index);
}

static int grow(void** items, int* cap, int count, size_t item_size) {
	if (count < *cap) return 0;
	int next = *cap ? *cap * 2 : 64;
	void* bigger = realloc(*items, item_size * (size_t)next);
	if (!bigger) return -1;
	*items = bigger;
	*cap = next;
	return 0;
}

int AG_indexAdd(AGIndex* index, const char* path, const char* name, int type) {
	if (grow((void**)&index->games, &index->game_cap, index->game_count, sizeof(AGGame)) != 0) return -1;
	AGGame* game = &index->games[index->game_count];
	game->path = strdup(path);
	game->name = strdup(name);
	game->type = type;
	if (!game->path || !game->name) {
		free(game->path);
		free(game->name);
		return -1;
	}
	index->game_count++;
	return 0;
}

int AG_indexAddFolder(AGIndex* index, const char* path, time_t mtime) {
	if (grow((void**)&index->folders, &index->folder_cap, index->folder_count, sizeof(AGFolder)) != 0) return -1;
	AGFolder* folder = &index->folders[index->folder_count];
	folder->path = strdup(path);
	if (!folder->path) return -1;
	folder->mtime = mtime;
	index->folder_count++;
	return 0;
}

static int compareGames(const void* a, const void* b) {
	const AGGame* x = a;
	const AGGame* y = b;
	int c = strcasecmp(x->name, y->name);
	return c ? c : strcmp(x->path, y->path);
}

void AG_indexSort(AGIndex* index) {
	if (index->game_count > 1) qsort(index->games, (size_t)index->game_count, sizeof(AGGame), compareGames);
}

///////////////////////////////////////
// collisions

typedef struct Named {
	const char* name;
	const char* system;
	int index;
} Named;

static int compareNamed(const void* a, const void* b) {
	const Named* x = a;
	const Named* y = b;
	int c = strcmp(x->name, y->name);
	return c ? c : strcmp(x->system, y->system);
}

int AG_collisions(const char* const* names, const char* const* systems, int count, int* tagged) {
	for (int i = 0; i < count; i++) tagged[i] = 0;
	if (count < 2) return 0;
	Named* sorted = malloc(sizeof(Named) * (size_t)count);
	if (!sorted) return -1;
	for (int i = 0; i < count; i++) sorted[i] = (Named){ names[i], systems[i], i };
	qsort(sorted, (size_t)count, sizeof(Named), compareNamed);

	int marked = 0;
	for (int start = 0; start < count; ) {
		int end = start + 1;
		while (end < count && !strcmp(sorted[end].name, sorted[start].name)) end++;
		// sorted by system within a name, so the ends differ when any do
		if (strcmp(sorted[start].system, sorted[end - 1].system) != 0) {
			for (int k = start; k < end; k++) tagged[sorted[k].index] = 1;
			marked += end - start;
		}
		start = end;
	}
	free(sorted);
	return marked;
}
