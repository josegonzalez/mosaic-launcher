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
	if (!name[0] || strchr(name, ',') || SM_setHas(set, name) || set->count >= SM_MAX_COLLECTIONS) return 0;
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

///////////////////////////////////////
// rules

void SM_rulesInit(SMRules* rules) {
	rules->count = 0;
	rules->dropped = 0;
}

int SM_ruleFind(const SMRules* rules, const char* name) {
	for (int i = 0; i < rules->count; i++) {
		if (!strcasecmp(rules->items[i].name, name)) return i;
	}
	return -1;
}

int SM_rulesAdd(SMRules* rules, const SMCatalog* catalog, int kind, int number, const char* name, const char* query) {
	if (SM_ruleFind(rules, name) >= 0 || (catalog && SM_find(catalog, name) >= 0)) return SM_TAKEN;
	if (rules->count >= SM_MAX_RULES) {
		rules->dropped++;
		return SM_FULL;
	}
	SMRule* r = &rules->items[rules->count++];
	snprintf(r->name, sizeof(r->name), "%s", name);
	snprintf(r->query, sizeof(r->query), "%s", query);
	r->kind = kind;
	r->number = number;
	return SM_ADDED;
}

int SM_rulesAddBuiltin(SMRules* rules, const SMCatalog* catalog, int kind, int number) {
	char query[32];
	const char* name;
	switch (kind) {
	case SM_RULE_NEVER:
		name = "Never played";
		snprintf(query, sizeof(query), "unplayed");
		break;
	case SM_RULE_RECENT:
		name = "Recently played";
		snprintf(query, sizeof(query), "played:%d", number);
		break;
	case SM_RULE_LONG:
		name = "Long played";
		snprintf(query, sizeof(query), "hours:%d", number);
		break;
	case SM_RULE_ADDED:
		name = "Recently added";
		snprintf(query, sizeof(query), "added:%d", number);
		break;
	default:
		return SM_TAKEN;
	}
	return SM_rulesAdd(rules, catalog, kind, kind == SM_RULE_NEVER ? 0 : number, name, query);
}

// Copies the bytes from start to end, trimmed of spaces and tabs, into
// out. Returns their length, which may be size or more (nothing is copied
// then).
static size_t trimmed(const char* start, const char* end, char* out, size_t size) {
	while (start < end && (*start == ' ' || *start == '\t')) start++;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
	size_t len = (size_t)(end - start);
	out[0] = '\0';
	if (len < size) {
		memcpy(out, start, len);
		out[len] = '\0';
	}
	return len;
}

int SM_rulesParse(SMRules* rules, const SMCatalog* catalog, const char* text, SMRuleError error, void* data) {
	int added = 0, number = 0;
	if (!text) return 0;
	if (!strncmp(text, "\xEF\xBB\xBF", 3)) text += 3; // a byte order mark
	for (const char* line = text; *line; ) {
		const char* end = strchr(line, '\n');
		if (!end) end = line + strlen(line);
		const char* next = *end ? end + 1 : end;
		number++;
		char whole[SE_QUERY_LEN + SM_NAME_LEN + 8], name[SM_NAME_LEN], query[SE_QUERY_LEN];
		size_t len = trimmed(line, end, whole, sizeof(whole));
		if (len >= sizeof(whole)) {
			snprintf(whole, sizeof(whole), "%.*s", (int)(sizeof(whole) - 1), line);
		}
		const char* reason = NULL;
		const char* equals = memchr(line, '=', (size_t)(end - line));
		char bad[SE_QUERY_LEN + 32];
		if (!len || whole[0] == '#') {
			line = next;
			continue;
		}
		if (!equals) reason = "no = between the name and the query";
		else if (trimmed(line, equals, name, sizeof(name)) >= sizeof(name)) reason = "the name is longer than 63 bytes";
		else if (!name[0]) reason = "no name before the =";
		else if (name[0] == '.' || strpbrk(name, "/,")) reason = "a name can't start with . or hold / or ,";
		else if (trimmed(equals + 1, end, query, sizeof(query)) >= sizeof(query)) reason = "the query is longer than 127 bytes";
		else if (!query[0]) reason = "no query after the =";
		else {
			SEQuery parsed;
			SE_parse(query, &parsed);
			if (parsed.bad[0]) {
				snprintf(bad, sizeof(bad), "%s isn't a filter", parsed.bad);
				reason = bad;
			}
			else if (parsed.dropped) reason = "more than 8 filters";
			else {
				int rc = SM_rulesAdd(rules, catalog, SM_RULE_USER, 0, name, query);
				if (rc == SM_TAKEN) reason = "a collection already has the name";
				else if (rc == SM_FULL) reason = "there are already 128 rule collections";
				else added++;
			}
		}
		if (reason && error) error(number, reason, whole, data);
		line = next;
	}
	return added;
}

int SM_decades(const SEIndex* games, int* out, int max) {
	int count = 0;
	for (int i = 0; i < games->game_count; i++) {
		int year = games->games[i].year;
		if (!year) continue;
		int decade = year - year % 10, at = 0;
		while (at < count && out[at] < decade) at++;
		if ((at < count && out[at] == decade) || count >= max) continue;
		memmove(&out[at + 1], &out[at], sizeof(int) * (size_t)(count - at));
		out[at] = decade;
		count++;
	}
	return count;
}

int SM_genreName(const char* genre, char* out, size_t size) {
	size_t n = 0;
	int separator = 0;
	const char* p = genre ? genre : "";
	while (*p == ' ' || *p == '\t') p++;
	for (; *p; p++) {
		if (strchr("/,;|", *p)) {
			separator = 1;
			while (n > 0 && out[n - 1] == ' ') n--;
			continue;
		}
		if (separator && (*p == ' ' || *p == '\t')) continue;
		if (separator && n > 0) {
			if (n + 3 >= size) return -1;
			memcpy(out + n, " - ", 3);
			n += 3;
		}
		separator = 0;
		if (n + 1 >= size) return -1;
		out[n++] = (char)((unsigned char)*p < 0x20 ? ' ' : *p);
	}
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
	out[n] = '\0';
	return n ? 0 : -1;
}

int SM_genresAdd(SMRules* genres, const char* genre) {
	char name[SM_NAME_LEN], compact[SE_QUERY_LEN], query[SE_QUERY_LEN];
	SE_compact(genre, compact, sizeof(compact));
	size_t len = strlen(compact);
	if (!len || len + 6 >= sizeof(query)) return -3;
	memcpy(query, "genre:", 6);
	memcpy(query + 6, compact, len + 1);
	for (int i = 0; i < genres->count; i++) {
		if (!strcmp(genres->items[i].query, query)) return 1;
	}
	if (SM_genreName(genre, name, sizeof(name)) != 0) return -3;
	return SM_rulesAdd(genres, NULL, SM_RULE_GENRE, 0, name, query);
}

static int byRuleName(const void* a, const void* b) {
	const SMRule* x = a;
	const SMRule* y = b;
	int c = strcasecmp(x->name, y->name);
	return c ? c : strcmp(x->name, y->name);
}

void SM_rulesSort(SMRules* rules) {
	if (rules->count > 1) qsort(rules->items, (size_t)rules->count, sizeof(SMRule), byRuleName);
}

void SM_ruleHitsInit(SMRuleHits* hits) {
	memset(hits, 0, sizeof(*hits));
}

void SM_ruleHitsFree(SMRuleHits* hits) {
	for (int i = 0; i < SM_MAX_RULES; i++) free(hits->members[i]);
	SM_ruleHitsInit(hits);
}

static const SEIndex* sorting_games; // qsort has no context argument

static int byFolded(const void* a, const void* b) {
	const SEGame* x = &sorting_games->games[*(const int*)a];
	const SEGame* y = &sorting_games->games[*(const int*)b];
	int c = strcmp(x->folded, y->folded);
	return c ? c : strcmp(x->path, y->path);
}

int SM_rulesMatch(const SMRules* rules, const SEIndex* games, SEFilterFn test, void* data, SMRuleHits* hits) {
	SM_ruleHitsFree(hits);
	if (!games->game_count) return 0;
	SEHit* found = malloc(sizeof(SEHit) * (size_t)games->game_count);
	if (!found) return -1;
	int rc = 0;
	for (int r = 0; r < rules->count; r++) {
		SEQuery query;
		SE_parse(rules->items[r].query, &query);
		int count = SE_search(games, &query, test, data, found);
		if (!count) continue;
		int* members = malloc(sizeof(int) * (size_t)count);
		if (!members) {
			rc = -1;
			continue;
		}
		for (int i = 0; i < count; i++) members[i] = found[i].game;
		if (query.words[0]) { // else they're by name already, all ranked the same
			sorting_games = games;
			qsort(members, (size_t)count, sizeof(int), byFolded);
			sorting_games = NULL;
		}
		hits->members[r] = members;
		hits->count[r] = count;
	}
	free(found);
	return rc;
}

unsigned long SM_hitsHash(const SEIndex* games, const int* members, int count) {
	unsigned long h = 2166136261u;
	for (int i = 0; i < count; i++) {
		for (const unsigned char* p = (const unsigned char*)games->games[members[i]].path; *p; p++) h = (h ^ *p) * 16777619u;
		h = (h ^ '\n') * 16777619u;
	}
	return h ^ (unsigned long)count;
}
