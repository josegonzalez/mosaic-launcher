#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "playmodel.h"

void PlayTable_init(PlayTable* table) {
	memset(table, 0, sizeof(*table));
}

void PlayTable_free(PlayTable* table) {
	for (int i = 0; i < table->count; i++) {
		free(table->games[i].key);
		free(table->games[i].name);
	}
	free(table->games);
	free(table->sessions);
	free(table->list);
	PlayTable_init(table);
}

// The list points into games, which adding and sorting move.
static void dropList(PlayTable* table) {
	free(table->list);
	table->list = NULL;
	table->list_count = 0;
	table->listed = 0;
}

static int add(PlayTable* table, const char* key, const char* name, int orphan, int alias, long long seconds,
	long long first, long long last, int count) {
	if (!key || !key[0]) return 0;
	dropList(table);
	if (table->count == table->cap) {
		int cap = table->cap ? table->cap * 2 : 64;
		PlayGame* games = realloc(table->games, cap * sizeof(PlayGame));
		if (!games) return -1;
		table->games = games;
		table->cap = cap;
	}
	int has_name = name && name[0];
	char* copy = strdup(key);
	char* named = has_name ? strdup(name) : NULL;
	if (!copy || (has_name && !named)) {
		free(copy);
		free(named);
		return -1;
	}
	PlayGame* game = &table->games[table->count];
	game->key = copy;
	game->name = named;
	game->seconds = seconds > 0 ? seconds : 0;
	game->first = first > 0 ? first : 0;
	game->last = last > 0 ? last : 0;
	game->count = count > 0 ? count : 0;
	game->orphan = orphan ? 1 : 0;
	game->alias = alias ? 1 : 0;
	game->order = table->count++;
	table->sorted = 0;
	return 0;
}

int PlayTable_addGame(PlayTable* table, const char* key, int orphan, long long seconds, long long last, int count) {
	return add(table, key, NULL, orphan, 0, seconds, 0, last, count);
}

int PlayTable_addRom(PlayTable* table, const char* path, const char* name, int orphan, long long seconds,
	long long first, long long last, int count) {
	int has_path = path && path[0];
	if (add(table, path, name, 0, 0, seconds, first, last, count)) return -1;
	if (orphan && add(table, name, name, 1, has_path, seconds, first, last, count)) return -1;
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
	dropList(table);
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

// Most played first, then the latest played, then in read order.
static int compareListed(const void* a, const void* b) {
	const PlayGame* x = *(const PlayGame* const*)a;
	const PlayGame* y = *(const PlayGame* const*)b;
	if (x->seconds != y->seconds) return x->seconds > y->seconds ? -1 : 1;
	if (x->last != y->last) return x->last > y->last ? -1 : 1;
	return x->order - y->order;
}

int PlayTable_list(PlayTable* table) {
	if (!table || !table->sorted) return 0;
	if (table->listed) return table->list_count;
	if (table->count) {
		table->list = malloc(table->count * sizeof(*table->list));
		if (!table->list) return -1;
	}
	int n = 0;
	for (int i = 0; i < table->count; i++) {
		const PlayGame* game = &table->games[i];
		if (!game->count || game->alias) continue;
		// sorted by key then read order, so a path's first row is the one found
		const PlayGame* before = i > 0 ? &table->games[i - 1] : NULL;
		if (!game->orphan && before && !before->orphan && !strcmp(before->key, game->key)) continue;
		table->list[n++] = game;
	}
	if (n > 1) qsort(table->list, n, sizeof(*table->list), compareListed);
	table->list_count = n;
	table->listed = 1;
	return n;
}

const PlayGame* PlayTable_listed(const PlayTable* table, int index) {
	if (!table || !table->listed || index < 0 || index >= table->list_count) return NULL;
	return table->list[index];
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

int Play_fullPath(const char* roms_path, const char* rel_path, char* out, size_t size) {
	if (!out || !size) return 0;
	out[0] = '\0';
	if (!roms_path || !roms_path[0] || !rel_path || !rel_path[0]) return 0;
	size_t n = strlen(roms_path);
	while (n > 1 && roms_path[n - 1] == '/') n--;
	int len = snprintf(out, size, "%.*s/%s", (int)n, roms_path, rel_path);
	if (len < 0 || (size_t)len >= size) {
		out[0] = '\0';
		return 0;
	}
	return 1;
}

void Play_dayBounds(long long now, long long* start, long long* end) {
	Play_dayBoundsAgo(now, 0, start, end);
}

void Play_dayBoundsAgo(long long now, int days_ago, long long* start, long long* end) {
	time_t t = (time_t)now;
	struct tm day;
	localtime_r(&t, &day);
	day.tm_hour = 0;
	day.tm_min = 0;
	day.tm_sec = 0;
	day.tm_mday -= days_ago;
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

void PlayDays_init(PlayDays* days) {
	memset(days, 0, sizeof(*days));
}

void PlayDays_free(PlayDays* days) {
	free(days->sessions);
	PlayDays_init(days);
}

int PlayDays_add(PlayDays* days, long long start, long long seconds) {
	if (days->count == days->cap) {
		int cap = days->cap ? days->cap * 2 : 256;
		PlaySession* sessions = realloc(days->sessions, cap * sizeof(PlaySession));
		if (!sessions) return -1;
		days->sessions = sessions;
		days->cap = cap;
	}
	PlaySession* session = &days->sessions[days->count++];
	session->start = start;
	session->seconds = seconds > 0 ? seconds : 0;
	days->sorted = 0;
	return 0;
}

static int compareStarts(const void* a, const void* b) {
	const PlaySession* x = a;
	const PlaySession* y = b;
	return x->start < y->start ? -1 : x->start > y->start;
}

void PlayDays_finish(PlayDays* days, int capped) {
	if (days->count > 1) qsort(days->sessions, days->count, sizeof(PlaySession), compareStarts);
	days->longest = 0;
	for (int i = 0; i < days->count; i++) {
		if (days->sessions[i].seconds > days->longest) days->longest = days->sessions[i].seconds;
	}
	days->capped = capped ? 1 : 0;
	days->cut = days->capped && days->count ? days->sessions[0].start : 0;
	days->sorted = 1;
}

long long PlayDays_seconds(const PlayDays* days, long long from, long long to) {
	if (!days || !days->sorted) return -1;
	if (days->capped && from <= days->cut) return -1;
	// the first session that can still run into the window
	long long earliest = from - days->longest;
	int lo = 0, hi = days->count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (days->sessions[mid].start < earliest) lo = mid + 1;
		else hi = mid;
	}
	int end = lo;
	while (end < days->count && days->sessions[end].start < to) end++;
	return Play_secondsBetween(days->sessions + lo, end - lo, from, to);
}
