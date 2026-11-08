#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "playmodel.h"

void PlayTable_init(PlayTable* table) {
	memset(table, 0, sizeof(*table));
}

void PlayTable_free(PlayTable* table) {
	for (int i = 0; i < table->count; i++) free(table->games[i].key);
	free(table->games);
	free(table->sessions);
	PlayTable_init(table);
}

int PlayTable_addGame(PlayTable* table, const char* key, int orphan, long long seconds, long long last, int count) {
	if (!key || !key[0]) return 0;
	if (table->count == table->cap) {
		int cap = table->cap ? table->cap * 2 : 64;
		PlayGame* games = realloc(table->games, cap * sizeof(PlayGame));
		if (!games) return -1;
		table->games = games;
		table->cap = cap;
	}
	char* copy = strdup(key);
	if (!copy) return -1;
	PlayGame* game = &table->games[table->count];
	game->key = copy;
	game->seconds = seconds > 0 ? seconds : 0;
	game->last = last > 0 ? last : 0;
	game->count = count > 0 ? count : 0;
	game->orphan = orphan ? 1 : 0;
	game->order = table->count++;
	table->sorted = 0;
	return 0;
}

int PlayTable_addSession(PlayTable* table, long long start, long long seconds) {
	if (table->session_count == table->session_cap) {
		int cap = table->session_cap ? table->session_cap * 2 : 16;
		PlaySession* sessions = realloc(table->sessions, cap * sizeof(PlaySession));
		if (!sessions) return -1;
		table->sessions = sessions;
		table->session_cap = cap;
	}
	PlaySession* session = &table->sessions[table->session_count++];
	session->start = start;
	session->seconds = seconds > 0 ? seconds : 0;
	return 0;
}

// Paths before orphans, then by key, then in read order.
static int compareGames(const void* a, const void* b) {
	const PlayGame* x = a;
	const PlayGame* y = b;
	if (x->orphan != y->orphan) return x->orphan - y->orphan;
	int by_key = strcmp(x->key, y->key);
	if (by_key) return by_key;
	return x->order - y->order;
}

void PlayTable_finish(PlayTable* table) {
	if (table->count > 1) qsort(table->games, table->count, sizeof(PlayGame), compareGames);
	table->sorted = 1;
}

// The first game with this kind and key.
static const PlayGame* findKey(const PlayTable* table, int orphan, const char* key) {
	int lo = 0, hi = table->count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		const PlayGame* game = &table->games[mid];
		int cmp = game->orphan != orphan ? game->orphan - orphan : strcmp(game->key, key);
		if (cmp < 0) lo = mid + 1;
		else hi = mid;
	}
	if (lo < table->count) {
		const PlayGame* game = &table->games[lo];
		if (game->orphan == orphan && !strcmp(game->key, key)) return game;
	}
	return NULL;
}

const PlayGame* PlayTable_find(const PlayTable* table, const char* rel_path) {
	if (!table || !table->sorted || !rel_path || !rel_path[0]) return NULL;
	const PlayGame* game = findKey(table, 0, rel_path);
	if (game) return game;

	const char* file = strrchr(rel_path, '/');
	file = file ? file + 1 : rel_path;
	if (!file[0]) return NULL;
	char name[512];
	size_t len = strlen(file);
	if (len >= sizeof(name)) return NULL;
	memcpy(name, file, len + 1);
	char* dot = strrchr(name, '.');
	if (dot && dot != name) {
		*dot = '\0';
		game = findKey(table, 1, name);
		if (game) return game;
	}
	return findKey(table, 1, file);
}

int Play_relPath(const char* roms_path, const char* path, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!roms_path || !path) return 0;
	size_t n = strlen(roms_path);
	while (n > 1 && roms_path[n - 1] == '/') n--;
	if (strncmp(path, roms_path, n) || path[n] != '/') return 0;
	const char* rel = path + n;
	while (*rel == '/') rel++;
	size_t len = strlen(rel);
	if (!len || len >= size) return 0;
	memcpy(out, rel, len + 1);
	return 1;
}

void Play_dayBounds(long long now, long long* start, long long* end) {
	time_t t = (time_t)now;
	struct tm day;
	localtime_r(&t, &day);
	day.tm_hour = 0;
	day.tm_min = 0;
	day.tm_sec = 0;
	day.tm_isdst = -1;
	struct tm next = day;
	next.tm_mday++;
	*start = (long long)mktime(&day);
	*end = (long long)mktime(&next);
}

long long Play_secondsBetween(const PlaySession* sessions, int count, long long from, long long to) {
	long long total = 0;
	for (int i = 0; i < count; i++) {
		long long a = sessions[i].start;
		long long b = a + sessions[i].seconds;
		if (a < from) a = from;
		if (b > to) b = to;
		if (b > a) total += b - a;
	}
	return total;
}
