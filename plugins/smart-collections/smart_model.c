// Smart collections model. See smart_model.h.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "smart_model.h"

// name, then up to SM_MAX_KEYWORDS keywords; NULL ends each row
static const char* const builtin[][SM_MAX_KEYWORDS + 2] = {
	{ "Advance Wars", "advance wars", NULL },
	{ "Adventure Island", "adventure island", NULL },
	{ "Batman", "batman", NULL },
	{ "Bomberman", "bomberman", NULL },
	{ "Breath of Fire", "breath of fire", NULL },
	{ "Bubble Bobble", "bubble bobble", NULL },
	{ "Castlevania", "castlevania", "akumajou dracula", NULL },
	{ "Chrono", "chrono trigger", "chrono cross", NULL },
	{ "Contra", "contra", "probotector", NULL },
	{ "Crash Bandicoot", "crash bandicoot", "crash team", "crash bash", NULL },
	{ "Donkey Kong", "donkey kong", NULL },
	{ "Double Dragon", "double dragon", NULL },
	{ "Dragon Ball", "dragon ball", NULL },
	{ "Dragon Quest", "dragon quest", "dragon warrior", NULL },
	{ "Earthworm Jim", "earthworm jim", NULL },
	{ "Ecco", "ecco", NULL },
	{ "F-Zero", "f zero", NULL },
	{ "Final Fantasy", "final fantasy", NULL },
	{ "Final Fight", "final fight", NULL },
	{ "Fire Emblem", "fire emblem", NULL },
	{ "Golden Axe", "golden axe", NULL },
	{ "Gradius", "gradius", NULL },
	{ "Gran Turismo", "gran turismo", NULL },
	{ "Gundam", "gundam", NULL },
	{ "Harvest Moon", "harvest moon", NULL },
	{ "King of Fighters", "king of fighters", NULL },
	{ "Kingdom Hearts", "kingdom hearts", NULL },
	{ "Kirby", "kirby", NULL },
	{ "Lemmings", "lemmings", NULL },
	{ "Mana", "of mana", "seiken densetsu", NULL },
	{ "Mario", "mario", NULL },
	{ "Mega Man", "mega man", "megaman", "rockman", NULL },
	{ "Metal Gear", "metal gear", NULL },
	{ "Metal Slug", "metal slug", NULL },
	{ "Metroid", "metroid", NULL },
	{ "Mortal Kombat", "mortal kombat", NULL },
	{ "Need for Speed", "need for speed", NULL },
	{ "Ninja Gaiden", "ninja gaiden", NULL },
	{ "Pac-Man", "pac man", "pacman", NULL },
	{ "Phantasy Star", "phantasy star", NULL },
	{ "Pokemon", "pokemon", NULL },
	{ "Puyo Puyo", "puyo puyo", NULL },
	{ "Resident Evil", "resident evil", "biohazard", NULL },
	{ "Ridge Racer", "ridge racer", NULL },
	{ "Samurai Shodown", "samurai shodown", "samurai spirits", NULL },
	{ "Shining", "shining force", "shining in the darkness", NULL },
	{ "Silent Hill", "silent hill", NULL },
	{ "Sonic", "sonic", NULL },
	{ "Spider-Man", "spider man", "spiderman", NULL },
	{ "Spyro", "spyro", NULL },
	{ "Star Fox", "star fox", "starfox", NULL },
	{ "Star Wars", "star wars", NULL },
	{ "Street Fighter", "street fighter", NULL },
	{ "Streets of Rage", "streets of rage", "bare knuckle", NULL },
	{ "Teenage Mutant Ninja Turtles", "ninja turtles", "tmnt", NULL },
	{ "Tekken", "tekken", NULL },
	{ "Tetris", "tetris", NULL },
	{ "Tony Hawk", "tony hawk", NULL },
	{ "Wario", "wario", "warioware", NULL },
	{ "Wonder Boy", "wonder boy", NULL },
	{ "Worms", "worms", NULL },
	{ "Yoshi", "yoshi", NULL },
	{ "Zelda", "zelda", NULL },
};

void SM_catalogInit(SMCatalog* catalog) {
	catalog->count = 0;
}

int SM_find(const SMCatalog* catalog, const char* name) {
	for (int i = 0; i < catalog->count; i++) {
		if (!strcasecmp(catalog->items[i].name, name)) return i;
	}
	return -1;
}

void SM_catalogAddBuiltin(SMCatalog* catalog) {
	for (size_t i = 0; i < sizeof(builtin) / sizeof(builtin[0]); i++) {
		if (catalog->count >= SM_MAX_FRANCHISES || SM_find(catalog, builtin[i][0]) >= 0) continue;
		SMFranchise* f = &catalog->items[catalog->count++];
		snprintf(f->name, sizeof(f->name), "%s", builtin[i][0]);
		f->keyword_count = 0;
		for (int k = 1; builtin[i][k] && f->keyword_count < SM_MAX_KEYWORDS; k++) {
			snprintf(f->keywords[f->keyword_count++], SM_NAME_LEN, "%s", builtin[i][k]);
		}
	}
}

// Copies the next comma-separated item, trimmed, and returns what follows it.
static const char* nextItem(const char* list, char* out, size_t size) {
	while (*list == ' ' || *list == '\t') list++;
	const char* end = strchr(list, ',');
	size_t len = end ? (size_t)(end - list) : strlen(list);
	while (len > 0 && (list[len - 1] == ' ' || list[len - 1] == '\t')) len--;
	if (len >= size) len = size - 1;
	memcpy(out, list, len);
	out[len] = '\0';
	return end ? end + 1 : NULL;
}

int SM_catalogAddList(SMCatalog* catalog, const char* list) {
	int added = 0;
	char name[SM_NAME_LEN];
	while (list && catalog->count < SM_MAX_FRANCHISES) {
		list = nextItem(list, name, sizeof(name));
		if (!name[0] || strchr(name, '/') || SM_find(catalog, name) >= 0) continue;
		SMFranchise* f = &catalog->items[catalog->count];
		snprintf(f->name, sizeof(f->name), "%s", name);
		SM_normalize(name, f->keywords[0], SM_NAME_LEN);
		if (!f->keywords[0][0]) continue;
		f->keyword_count = 1;
		catalog->count++;
		added++;
	}
	return added;
}

void SM_cleanName(const char* file_name, char* out, size_t size) {
	char name[256];
	snprintf(name, sizeof(name), "%s", file_name);

	// extensions, eg. .p8.png
	char* tmp;
	while ((tmp = strrchr(name, '.')) != NULL) {
		size_t len = strlen(tmp);
		if (len > 2 && len <= 5) *tmp = '\0';
		else break;
	}

	// tags such as (USA) and [!]
	char kept[256];
	snprintf(kept, sizeof(kept), "%s", name);
	while ((tmp = strrchr(name, '(')) != NULL || (tmp = strrchr(name, '[')) != NULL) {
		if (tmp == name) break;
		*tmp = '\0';
	}
	if (!name[0]) snprintf(name, sizeof(name), "%s", kept);

	size_t len = strlen(name);
	while (len > 1 && isspace((unsigned char)name[len - 1])) name[--len] = '\0';
	snprintf(out, size, "%s", name);
}

void SM_normalize(const char* in, char* out, size_t size) {
	size_t n = 0;
	int space = 1; // no leading space
	for (const unsigned char* p = (const unsigned char*)in; *p && n + 1 < size; p++) {
		unsigned char c = *p;
		if (c == 0xC3 && (p[1] == 0xA9 || p[1] == 0x89)) { // é, É
			c = 'e';
			p++;
		}
		if (c >= 0x80 || isalnum(c)) {
			out[n++] = (char)(c < 0x80 ? tolower(c) : c);
			space = 0;
		}
		else if (!space) {
			out[n++] = ' ';
			space = 1;
		}
	}
	if (n > 0 && out[n - 1] == ' ') n--;
	out[n] = '\0';
}

int SM_hasKeyword(const char* title, const char* keyword) {
	size_t len = strlen(keyword);
	if (!len) return 0;
	for (const char* p = title; (p = strstr(p, keyword)) != NULL; p++) {
		if (p != title && p[-1] != ' ') continue;
		char after = p[len];
		if (after == '\0' || after == ' ' || isdigit((unsigned char)after)) return 1;
	}
	return 0;
}

int SM_inFranchise(const SMFranchise* franchise, const char* title) {
	for (int k = 0; k < franchise->keyword_count; k++) {
		if (SM_hasKeyword(title, franchise->keywords[k])) return 1;
	}
	return 0;
}

int SM_mapLookup(const char* map, const char* file_name, char* out, size_t size) {
	size_t len = strlen(file_name);
	for (const char* line = map; line && *line; ) {
		const char* end = strchr(line, '\n');
		if (!strncmp(line, file_name, len) && line[len] == '\t') {
			const char* alias = line + len + 1;
			size_t n = end ? (size_t)(end - alias) : strlen(alias);
			while (n > 0 && (alias[n - 1] == '\r' || alias[n - 1] == '\n')) n--;
			if (n >= size) n = size - 1;
			memcpy(out, alias, n);
			out[n] = '\0';
			return 1;
		}
		line = end ? end + 1 : NULL;
	}
	return 0;
}

///////////////////////////////////////
// index

void SM_indexInit(SMIndex* index) {
	memset(index, 0, sizeof(*index));
}

void SM_indexFree(SMIndex* index) {
	for (int i = 0; i < index->game_count; i++) {
		free(index->games[i].path);
		free(index->games[i].name);
	}
	free(index->games);
	for (int i = 0; i < SM_MAX_FRANCHISES; i++) free(index->members[i]);
	SM_indexInit(index);
}

static int grow(void** items, int* cap, int count, size_t item_size) {
	if (count < *cap) return 0;
	int next = *cap ? *cap * 2 : 16;
	void* bigger = realloc(*items, (size_t)next * item_size);
	if (!bigger) return -1;
	*items = bigger;
	*cap = next;
	return 0;
}

int SM_indexAdd(SMIndex* index, const SMCatalog* catalog, const char* path, const char* name, int type) {
	char title[256];
	SM_normalize(name, title, sizeof(title));

	int joined = 0;
	int game = index->game_count;
	for (int i = 0; i < catalog->count; i++) {
		if (!SM_inFranchise(&catalog->items[i], title)) continue;
		if (!joined) {
			if (grow((void**)&index->games, &index->game_cap, index->game_count, sizeof(SMGame))) return 0;
			SMGame* g = &index->games[index->game_count++];
			g->path = strdup(path);
			g->name = strdup(name);
			g->type = type;
		}
		if (grow((void**)&index->members[i], &index->member_cap[i], index->member_count[i], sizeof(int))) continue;
		index->members[i][index->member_count[i]++] = game;
		joined++;
	}
	return joined;
}

static const SMGame* sorting; // qsort has no context argument

static int byName(const void* a, const void* b) {
	const SMGame* x = &sorting[*(const int*)a];
	const SMGame* y = &sorting[*(const int*)b];
	int c = strcasecmp(x->name, y->name);
	return c ? c : strcmp(x->path, y->path);
}

void SM_indexSort(SMIndex* index, const SMCatalog* catalog) {
	sorting = index->games;
	for (int i = 0; i < catalog->count; i++) {
		if (index->member_count[i] > 1) qsort(index->members[i], (size_t)index->member_count[i], sizeof(int), byName);
	}
	sorting = NULL;
}

///////////////////////////////////////
// dismissed

void SM_setParse(SMNameSet* set, const char* list) {
	set->count = 0;
	char name[SM_NAME_LEN];
	while (list) {
		list = nextItem(list, name, sizeof(name));
		if (name[0]) SM_setAdd(set, name);
	}
}

void SM_setFormat(const SMNameSet* set, char* out, size_t size) {
	size_t n = 0;
	out[0] = '\0';
	for (int i = 0; i < set->count; i++) {
		int wrote = snprintf(out + n, size - n, "%s%s", i ? "," : "", set->names[i]);
		if (wrote < 0 || (size_t)wrote >= size - n) break;
		n += (size_t)wrote;
	}
	out[n] = '\0';
}

int SM_setHas(const SMNameSet* set, const char* name) {
	for (int i = 0; i < set->count; i++) {
		if (!strcasecmp(set->names[i], name)) return 1;
	}
	return 0;
}

int SM_setAdd(SMNameSet* set, const char* name) {
	if (!name[0] || strchr(name, ',') || SM_setHas(set, name) || set->count >= SM_MAX_FRANCHISES) return 0;
	snprintf(set->names[set->count++], SM_NAME_LEN, "%s", name);
	return 1;
}

int SM_setRemove(SMNameSet* set, const char* name) {
	for (int i = 0; i < set->count; i++) {
		if (strcasecmp(set->names[i], name)) continue;
		memmove(&set->names[i], &set->names[i + 1], (size_t)(set->count - i - 1) * SM_NAME_LEN);
		set->count--;
		return 1;
	}
	return 0;
}

int SM_visible(const SMIndex* index, const SMCatalog* catalog, int min, const SMNameSet* dismissed, int* out) {
	if (min < 1) min = 1;
	int n = 0;
	for (int i = 0; i < catalog->count; i++) {
		if (index->member_count[i] < min) continue;
		if (dismissed && SM_setHas(dismissed, catalog->items[i].name)) continue;
		// insertion by name, so the folder lists them in order
		int at = n;
		while (at > 0 && strcasecmp(catalog->items[out[at - 1]].name, catalog->items[i].name) > 0) {
			out[at] = out[at - 1];
			at--;
		}
		out[at] = i;
		n++;
	}
	return n;
}
