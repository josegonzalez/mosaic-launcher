#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosaic_plugin.h"
#include "presets_model.h"

///////////////////////////////////////
// the folder

void FP_folderInit(FPFolder* folder, const char* dir) {
	memset(folder, 0, sizeof(*folder));
	snprintf(folder->dir, sizeof(folder->dir), "%s", dir ? dir : "");
	SE_indexInit(&folder->games);
}

void FP_folderFree(FPFolder* folder) {
	for (int i = 0; i < folder->games.game_count; i++) free(folder->genres[i]);
	free(folder->genres);
	free(folder->players);
	SE_indexFree(&folder->games);
	FP_folderInit(folder, NULL);
}

int FP_players(const char* value) {
	int most = 0;
	for (const char* p = value ? value : ""; *p; ) {
		if (!isdigit((unsigned char)*p)) {
			p++;
			continue;
		}
		long n = 0;
		while (isdigit((unsigned char)*p)) {
			if (n < 1000) n = n * 10 + (*p - '0');
			p++;
		}
		if (n > most && n < 1000) most = (int)n;
	}
	return most;
}

int FP_folderAdd(FPFolder* folder, const char* path, const char* name, int type, const char* genre, const char* players,
		const char* year) {
	int n = folder->games.game_count;
	if (n >= folder->cap) {
		int next = folder->cap ? folder->cap * 2 : 64;
		char** genres = realloc(folder->genres, sizeof(char*) * (size_t)next);
		if (!genres) return -1;
		folder->genres = genres;
		int* counts = realloc(folder->players, sizeof(int) * (size_t)next);
		if (!counts) return -1;
		folder->players = counts;
		folder->cap = next;
	}
	char* copy = strdup(genre ? genre : "");
	if (!copy) return -1;
	if (SE_indexAddInfo(&folder->games, path, name, type, genre, year) != 0) {
		free(copy);
		return -1;
	}
	folder->genres[n] = copy;
	folder->players[n] = FP_players(players);
	return 0;
}

///////////////////////////////////////
// the presets

static FPPreset* add(FPPreset* out, int* count, int max, int kind, int label) {
	if (*count >= max) return NULL;
	FPPreset* p = &out[(*count)++];
	memset(p, 0, sizeof(*p));
	p->kind = kind;
	p->label = label;
	return p;
}

typedef struct Genre {
	char folded[SE_NAME_LEN];
	const char* name;
} Genre;

static int compareGenres(const void* a, const void* b) {
	return strcmp(((const Genre*)a)->folded, ((const Genre*)b)->folded);
}

int FP_presets(const FPFolder* folder, const char* const user[FP_USER_MAX], FPPreset* out, int max) {
	int count = 0;
	FPPreset* p;
	if ((p = add(out, &count, max, FP_QUERY, FP_LABEL_UNPLAYED))) {
		snprintf(p->key, sizeof(p->key), "unplayed");
		snprintf(p->query, sizeof(p->query), "unplayed");
	}
	if ((p = add(out, &count, max, FP_QUERY, FP_LABEL_FAVORITES))) {
		snprintf(p->key, sizeof(p->key), "favorites");
		snprintf(p->query, sizeof(p->query), "fav:");
	}
	if ((p = add(out, &count, max, FP_SAVE, FP_LABEL_SAVE))) snprintf(p->key, sizeof(p->key), "save");

	// each genre once, by its folded name
	int games = folder->games.game_count;
	Genre* genres = games ? malloc(sizeof(Genre) * (size_t)games) : NULL;
	int genre_count = 0;
	for (int i = 0; genres && i < games; i++) {
		char folded[SE_NAME_LEN];
		SE_fold(folder->genres[i], folded, sizeof(folded));
		if (!folded[0]) continue;
		int seen = 0;
		for (int k = 0; k < genre_count && !seen; k++) seen = !strcmp(genres[k].folded, folded);
		if (seen) continue;
		snprintf(genres[genre_count].folded, sizeof(genres[0].folded), "%s", folded);
		genres[genre_count++].name = folder->genres[i];
	}
	if (genre_count > 1) qsort(genres, (size_t)genre_count, sizeof(Genre), compareGenres);
	for (int k = 0; k < genre_count; k++) {
		if (!(p = add(out, &count, max, FP_GENRE, FP_LABEL_GENRE))) break;
		snprintf(p->key, sizeof(p->key), "genre:%s", genres[k].folded);
		snprintf(p->genre, sizeof(p->genre), "%s", genres[k].name);
	}
	free(genres);

	// at least N players, for each N a game reaches, smallest first
	int most = 0;
	for (int i = 0; i < games; i++) {
		if (folder->players[i] > most) most = folder->players[i];
	}
	for (int n = 2; n <= most; n++) {
		int reached = 0;
		for (int i = 0; i < games && !reached; i++) reached = folder->players[i] == n;
		if (!reached) continue;
		if (!(p = add(out, &count, max, FP_PLAYERS, FP_LABEL_PLAYERS))) break;
		snprintf(p->key, sizeof(p->key), "players:%d", n);
		p->players = n;
	}

	for (int i = 0; user && i < FP_USER_MAX; i++) {
		char query[SE_QUERY_LEN];
		snprintf(query, sizeof(query), "%s", user[i] ? user[i] : "");
		SE_trim(query);
		if (!query[0]) continue;
		if (!(p = add(out, &count, max, FP_QUERY, FP_LABEL_QUERY))) break;
		snprintf(p->key, sizeof(p->key), "user%d", i + 1);
		snprintf(p->query, sizeof(p->query), "%s", query);
	}
	return count;
}

int FP_find(const FPPreset* presets, int count, const char* key) {
	for (int i = 0; key && i < count; i++) {
		if (!strcmp(presets[i].key, key)) return i;
	}
	return -1;
}

///////////////////////////////////////
// matching

static int passes(const SEGame* game, int filter, const SEFilter* f, void* data) {
	(void)filter;
	const FPTests* tests = data;
	switch (f->kind) {
	case SE_FILTER_FAV:
	case SE_FILTER_TAG:
		return tests->in_set ? tests->in_set(f->kind, f->arg, game, tests->data) != 0 : 0;
	case SE_FILTER_UNPLAYED:
		return tests->played ? !tests->played(game, tests->data) : 0;
	case SE_FILTER_SYSTEM:
		return tests->roms && SE_systemMatches(tests->roms, game->path, f->arg);
	case SE_FILTER_PLAYED:
	case SE_FILTER_HOURS:
	case SE_FILTER_ADDED:
		if (!tests->play_value) return 0;
		return SE_timeMatches(f, SE_playValue(game, game->type == MOSAIC_ENTRY_DIR, f->kind, tests->play_value, tests->data),
			tests->now ? tests->now : (long long)time(NULL));
	}
	return 0;
}

int FP_match(const FPFolder* folder, const FPPreset* preset, const FPTests* tests, unsigned char* matched) {
	int games = folder->games.game_count;
	if (!games) return 0;
	memset(matched, 0, (size_t)games);
	int kept = 0;
	if (preset->kind == FP_QUERY) {
		SEQuery query;
		SE_parse(preset->query, &query);
		SEHit* hits = malloc(sizeof(SEHit) * (size_t)games);
		if (!hits) return -1;
		int count = SE_search(&folder->games, &query, passes, (void*)tests, hits);
		for (int i = 0; i < count; i++) matched[hits[i].game] = 1;
		free(hits);
		return count;
	}
	char want[SE_NAME_LEN], folded[SE_NAME_LEN];
	if (preset->kind == FP_GENRE) SE_fold(preset->genre, want, sizeof(want));
	for (int i = 0; i < games; i++) {
		const SEGame* game = &folder->games.games[i];
		int keep = 0;
		if (preset->kind == FP_SAVE) keep = tests->has_save && tests->has_save(game, tests->data);
		else if (preset->kind == FP_GENRE) {
			SE_fold(folder->genres[i], folded, sizeof(folded));
			keep = want[0] && !strcmp(folded, want);
		}
		else if (preset->kind == FP_PLAYERS) keep = preset->players > 0 && folder->players[i] >= preset->players;
		matched[i] = (unsigned char)(keep != 0);
		kept += keep != 0;
	}
	return kept;
}

int FP_step(int count, int current, int dir, int (*keeps)(int preset, void* data), void* data) {
	if (count < 1) return -1;
	dir = dir < 0 ? -1 : 1;
	if (current < -1 || current >= count) current = -1;
	// the positions are none and each preset, in a ring of count + 1
	int at = current;
	for (int i = 0; i < count + 1; i++) {
		at += dir;
		if (at >= count) at = -1;
		else if (at < -1) at = count - 1;
		if (at == -1 || (keeps && keeps(at, data))) return at;
	}
	return -1;
}
