#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "games_model.h"

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
	free(index->games);
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
