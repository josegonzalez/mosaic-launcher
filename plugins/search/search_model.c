#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "search_model.h"

///////////////////////////////////////
// folding

// The plain letters for U+00C0 to U+00FF; "" is punctuation (× and ÷).
static const char* const latin1[64] = {
	"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
	"d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "ss",
	"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
	"d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y",
};

// The plain letters for runs of U+0100 to U+017F, Latin Extended-A.
static const struct {
	unsigned short last;
	const char* plain;
} extended[] = {
	{ 0x105, "a" }, { 0x10D, "c" }, { 0x111, "d" }, { 0x11B, "e" }, { 0x123, "g" }, { 0x127, "h" },
	{ 0x131, "i" }, { 0x133, "ij" }, { 0x135, "j" }, { 0x138, "k" }, { 0x142, "l" }, { 0x14B, "n" },
	{ 0x151, "o" }, { 0x153, "oe" }, { 0x159, "r" }, { 0x161, "s" }, { 0x167, "t" }, { 0x173, "u" },
	{ 0x175, "w" }, { 0x178, "y" }, { 0x17E, "z" }, { 0x17F, "s" },
};

typedef struct Folder {
	char* out;
	size_t size;
	size_t len;
	int space; // a space is due before the next character
} Folder;

static void put(Folder* f, const char* text, size_t n) {
	if (f->space && f->len > 0 && f->len + 1 < f->size) f->out[f->len++] = ' ';
	f->space = 0;
	for (size_t i = 0; i < n && f->len + 1 < f->size; i++) f->out[f->len++] = text[i];
}

// The length of the UTF-8 sequence starting with byte c, or 1 for a byte
// that can't start one.
static size_t sequenceLength(unsigned char c) {
	if (c >= 0xF0 && c < 0xF8) return 4;
	if (c >= 0xE0) return c < 0xF0 ? 3 : 1;
	if (c >= 0xC2) return 2;
	return 1;
}

size_t SE_fold(const char* text, char* out, size_t size) {
	if (!size) return 0;
	Folder f = { out, size, 0, 0 };
	const unsigned char* p = (const unsigned char*)(text ? text : "");
	while (*p) {
		unsigned char c = *p;
		if (c < 0x80) {
			p++;
			if (isalnum(c)) {
				char lower = (char)tolower(c);
				put(&f, &lower, 1);
			}
			else if (c != '\'' && c != '`') f.space = 1;
			continue;
		}

		size_t n = sequenceLength(c);
		for (size_t i = 1; i < n; i++) {
			if ((p[i] & 0xC0) != 0x80) n = 1; // cut short: take the byte alone
		}
		if (n == 1) { // not UTF-8: keep it as it is
			put(&f, (const char*)p, 1);
			p++;
			continue;
		}

		unsigned cp = n == 2 ? ((c & 0x1Fu) << 6) | (p[1] & 0x3Fu)
			: n == 3 ? ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu)
			: ((c & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
		// curly apostrophes, and combining accents, as in a decomposed é
		// (e and U+0301) from a Mac, dropped
		if (cp == 0x2018 || cp == 0x2019 || (cp >= 0x300 && cp <= 0x36F)) {
			p += n;
			continue;
		}
		const char* plain = NULL; // kept as it is
		if (cp >= 0xC0 && cp <= 0xFF) plain = latin1[cp - 0xC0];
		else if (cp >= 0x100 && cp <= 0x17F) {
			for (size_t i = 0; i < sizeof(extended) / sizeof(extended[0]); i++) {
				if (cp <= extended[i].last) {
					plain = extended[i].plain;
					break;
				}
			}
		}
		// Latin-1's punctuation, and dashes, quotes and the like
		else if ((cp >= 0xA0 && cp < 0xC0) || (cp >= 0x2010 && cp <= 0x206F)) plain = "";

		if (!plain) put(&f, (const char*)p, n);
		else if (!plain[0]) f.space = 1;
		else put(&f, plain, strlen(plain));
		p += n;
	}
	out[f.len] = '\0';
	return f.len;
}

///////////////////////////////////////
// index

void SE_indexInit(SEIndex* index) {
	memset(index, 0, sizeof(*index));
}

void SE_indexFree(SEIndex* index) {
	for (int i = 0; i < index->game_count; i++) {
		free(index->games[i].path);
		free(index->games[i].name);
		free(index->games[i].folded);
		free(index->games[i].genre);
	}
	free(index->games);
	SE_indexInit(index);
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

int SE_year(const char* text) {
	for (const char* p = text ? text : ""; *p; p++) {
		if (!isdigit((unsigned char)*p)) continue;
		size_t n = strspn(p, "0123456789");
		if (n == 4) {
			int year = atoi(p);
			if (year >= 1900 && year <= 2099) return year;
		}
		p += n - 1;
	}
	return 0;
}

int SE_indexAddInfo(SEIndex* index, const char* path, const char* name, int type, const char* genre, const char* year) {
	if (grow((void**)&index->games, &index->game_cap, index->game_count, sizeof(SEGame)) != 0) return -1;
	char folded[SE_NAME_LEN];
	SE_fold(name, folded, sizeof(folded));
	SEGame* game = &index->games[index->game_count];
	memset(game, 0, sizeof(*game));
	game->path = strdup(path);
	game->name = strdup(name);
	game->folded = strdup(folded);
	game->type = type;
	game->year = SE_year(year);
	int genre_missing = 0;
	if (genre && genre[0]) {
		SE_fold(genre, folded, sizeof(folded));
		if (folded[0]) genre_missing = !(game->genre = strdup(folded));
	}
	if (!game->path || !game->name || !game->folded || genre_missing) {
		free(game->path);
		free(game->name);
		free(game->folded);
		free(game->genre);
		return -1;
	}
	index->game_count++;
	return 0;
}

int SE_indexAdd(SEIndex* index, const char* path, const char* name, int type) {
	return SE_indexAddInfo(index, path, name, type, NULL, NULL);
}

///////////////////////////////////////
// matching

// True when the word of length len at word starts a word in name: at the
// start or after a space.
static int startsWord(const char* name, const char* word, size_t len) {
	for (const char* p = name;; p++) {
		if (!strncmp(p, word, len)) return 1;
		p = strchr(p, ' ');
		if (!p) return 0;
	}
}

// True when the word of length len is anywhere in name.
static int contains(const char* name, const char* word, size_t len) {
	for (const char* p = name; (p = strchr(p, word[0])) != NULL; p++) {
		if (!strncmp(p, word, len)) return 1;
	}
	return 0;
}

int SE_rank(const char* name, const char* query) {
	if (!query[0]) return -1;
	if (!strcmp(name, query)) return SE_RANK_EXACT;
	size_t query_len = strlen(query);
	if (!strncmp(name, query, query_len)) return SE_RANK_PREFIX;

	int rank = SE_RANK_WORDS;
	for (const char* word = query; *word; ) {
		size_t len = strcspn(word, " ");
		if (len) {
			if (startsWord(name, word, len)) {}
			else if (contains(name, word, len)) rank = SE_RANK_INSIDE;
			else return -1;
		}
		word += len;
		while (*word == ' ') word++;
	}
	return rank;
}

static const SEIndex* sorting; // the index SE_search's hits point into

static int compareHits(const void* a, const void* b) {
	const SEHit* x = a;
	const SEHit* y = b;
	if (x->rank != y->rank) return x->rank - y->rank;
	const SEGame* p = &sorting->games[x->game];
	const SEGame* q = &sorting->games[y->game];
	int c = strcmp(p->folded, q->folded);
	return c ? c : strcmp(p->path, q->path);
}

int SE_filterNeedsTest(int kind) {
	return kind != SE_FILTER_GENRE && kind != SE_FILTER_YEAR;
}

// True when the folded text with its spaces dropped is compact.
static int compactEquals(const char* folded, const char* compact) {
	for (; *folded; folded++) {
		if (*folded == ' ') continue;
		if (*folded != *compact++) return 0;
	}
	return !*compact;
}

// genre:, year: and decade:, from the index.
static int infoPasses(const SEGame* game, const SEFilter* f) {
	if (f->kind == SE_FILTER_YEAR) return game->year && game->year >= f->from && game->year <= f->to;
	if (!game->genre) return 0;
	char folded[SE_QUERY_LEN * 2], compact[SE_QUERY_LEN * 2];
	SE_fold(f->arg, folded, sizeof(folded));
	SE_compact(f->arg, compact, sizeof(compact));
	if (compact[0] && compactEquals(game->genre, compact)) return 1;
	int rank = SE_rank(game->genre, folded);
	return rank >= 0 && rank <= SE_RANK_WORDS;
}

// How much a filter costs to ask: the index's, the sets', and system:
// first, then the play history's, then unplayed and added:, which can
// read the card.
static int cost(int kind) {
	if (kind == SE_FILTER_UNPLAYED || kind == SE_FILTER_ADDED) return 2;
	return kind == SE_FILTER_PLAYED || kind == SE_FILTER_HOURS;
}

// True when game passes every filter that costs level.
static int passes(const SEIndex* index, int game, const SEQuery* query, SEFilterFn test, void* data, int level) {
	for (int i = 0; i < query->filter_count; i++) {
		const SEFilter* f = &query->filters[i];
		if (cost(f->kind) != level) continue;
		const SEGame* g = &index->games[game];
		int ok = SE_filterNeedsTest(f->kind) ? test(g, i, f, data) != 0 : infoPasses(g, f);
		if (ok == (f->negate != 0)) return 0;
	}
	return 1;
}

int SE_timeMatches(const SEFilter* f, long long value, long long now) {
	if (value <= 0) return 0;
	if (f->kind == SE_FILTER_HOURS) return value > (long long)f->from * 3600;
	if (f->kind == SE_FILTER_PLAYED || f->kind == SE_FILTER_ADDED) return value >= now - (long long)f->from * 86400;
	return 0;
}

long long SE_playValue(const SEGame* game, int is_dir, int kind, long long (*ask)(int kind, const char* path, void* data),
		void* data) {
	long long value = ask(kind, game->path, data);
	if (value > 0 || !is_dir) return value;
	const char* name = strrchr(game->path, '/');
	char file[SE_NAME_LEN * 4];
	const char* extensions[] = { ".m3u", ".cue" };
	for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
		snprintf(file, sizeof(file), "%s%s%s", game->path, name ? name : "/", extensions[i]);
		long long other = ask(kind, file, data);
		if (other > 0) return other;
	}
	return value;
}

int SE_search(const SEIndex* index, const SEQuery* query, SEFilterFn test, void* data, SEHit* hits) {
	char folded[SE_QUERY_LEN * 2];
	SE_fold(query->words, folded, sizeof(folded));
	if (!folded[0] && !query->filter_count) return 0;
	for (int i = 0; i < query->filter_count && !test; i++) {
		if (SE_filterNeedsTest(query->filters[i].kind)) return 0;
	}

	int count = 0;
	for (int i = 0; i < index->game_count; i++) {
		int rank = folded[0] ? SE_rank(index->games[i].folded, folded) : SE_RANK_EXACT;
		if (rank < 0) continue;
		if (query->filter_count && (!passes(index, i, query, test, data, 0) || !passes(index, i, query, test, data, 1)
				|| !passes(index, i, query, test, data, 2))) continue;
		hits[count++] = (SEHit){ i, rank };
	}
	sorting = index;
	if (count > 1) qsort(hits, (size_t)count, sizeof(SEHit), compareHits);
	sorting = NULL;
	return count;
}

///////////////////////////////////////
// filters

static int startsWithNoCase(const char* s, const char* prefix) {
	return !strncasecmp(s, prefix, strlen(prefix));
}

#define DECADE 1000 // decade:, read as a year: range

// The filters with something after their colon.
static const struct {
	const char* prefix;
	int kind;
} prefixes[] = {
	{ "tag:", SE_FILTER_TAG },
	{ "system:", SE_FILTER_SYSTEM },
	{ "genre:", SE_FILTER_GENRE },
	{ "year:", SE_FILTER_YEAR },
	{ "decade:", DECADE },
	{ "played:", SE_FILTER_PLAYED },
	{ "hours:", SE_FILTER_HOURS },
	{ "added:", SE_FILTER_ADDED },
};

// A whole number from 1 to max, or 0.
static int number(const char* text, size_t len, int max) {
	if (!len || len > 6 || strspn(text, "0123456789") < len) return 0;
	int n = 0;
	for (size_t i = 0; i < len; i++) n = n * 10 + (text[i] - '0');
	return n <= max ? n : 0;
}

// Reads a filter's argument into from and to. Returns 1, or 0 when it
// isn't one the filter takes.
static int filterArg(int kind, const char* arg, int* from, int* to) {
	size_t len = strlen(arg);
	if (!len) return 0;
	switch (kind) {
	case SE_FILTER_PLAYED:
	case SE_FILTER_HOURS:
	case SE_FILTER_ADDED:
		return (*from = number(arg, len, SE_NUMBER_MAX)) > 0;
	case SE_FILTER_YEAR: {
		const char* dash = strchr(arg, '-');
		size_t first = dash ? (size_t)(dash - arg) : len;
		if (first != 4 || (dash && strlen(dash + 1) != 4)) return 0;
		*from = number(arg, 4, 2099);
		*to = dash ? number(dash + 1, 4, 2099) : *from;
		return *from >= 1900 && *to >= *from;
	}
	case DECADE: {
		if (arg[len - 1] == 's' || arg[len - 1] == 'S') len--;
		if ((len != 2 && len != 4) || strspn(arg, "0123456789") < len) return 0;
		int n = atoi(arg) % (len == 2 ? 100 : 10000);
		if (len == 2) n += n < 50 ? 2000 : 1900;
		if (n < 1900 || n > 2090 || n % 10 != 0) return 0;
		*from = n;
		*to = n + 9;
		return 1;
	}
	}
	return 1;
}

void SE_parse(const char* query, SEQuery* out) {
	memset(out, 0, sizeof(*out));
	size_t len = 0;
	for (const char* p = query ? query : ""; *p; ) {
		while (*p == ' ') p++;
		size_t n = strcspn(p, " ");
		if (!n) break;
		char term[SE_QUERY_LEN];
		snprintf(term, sizeof(term), "%.*s", (int)(n < sizeof(term) ? n : sizeof(term) - 1), p);
		p += n;

		int negate = term[0] == '-' && term[1];
		const char* body = term + negate;
		int kind = -1, from = 0, to = 0;
		const char* arg = "";
		if (!strcasecmp(body, "fav:")) kind = SE_FILTER_FAV;
		else if (!strcasecmp(body, "unplayed")) kind = SE_FILTER_UNPLAYED;
		else {
			for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]) && kind < 0; i++) {
				if (!startsWithNoCase(body, prefixes[i].prefix)) continue;
				arg = body + strlen(prefixes[i].prefix);
				if (filterArg(prefixes[i].kind, arg, &from, &to)) kind = prefixes[i].kind == DECADE ? SE_FILTER_YEAR : prefixes[i].kind;
				else if (!out->bad[0]) snprintf(out->bad, sizeof(out->bad), "%s", term);
				break;
			}
		}
		if (kind >= 0 && out->filter_count >= SE_FILTERS_MAX) out->dropped++;
		else if (kind >= 0) {
			SEFilter* f = &out->filters[out->filter_count++];
			f->kind = kind;
			f->negate = negate;
			snprintf(f->arg, sizeof(f->arg), "%s", arg);
			f->from = from;
			f->to = to;
			continue;
		}
		if (len + 1 + strlen(term) >= sizeof(out->words)) continue;
		len += (size_t)snprintf(out->words + len, sizeof(out->words) - len, "%s%s", len ? " " : "", term);
	}
}

void SE_compact(const char* text, char* out, size_t size) {
	if (!size) return;
	char folded[SE_NAME_LEN * 2];
	SE_fold(text, folded, sizeof(folded));
	size_t n = 0;
	for (const char* p = folded; *p && n + 1 < size; p++) {
		if (*p != ' ') out[n++] = *p;
	}
	out[n] = '\0';
}

int SE_tagMatches(const char* set_name, const char* arg) {
	if (!set_name || strncmp(set_name, "tags:", 5) != 0 || !arg) return 0;
	char a[SE_NAME_LEN], b[SE_NAME_LEN];
	SE_compact(set_name + 5, a, sizeof(a));
	SE_compact(arg, b, sizeof(b));
	return a[0] && !strcmp(a, b);
}

int SE_systemMatches(const char* roms, const char* path, const char* arg) {
	size_t n = strlen(roms);
	if (!arg || !arg[0] || strncmp(path, roms, n) != 0 || path[n] != '/') return 0;
	const char* start = path + n + 1;
	const char* slash = strchr(start, '/');
	if (!slash || slash == start) return 0;
	char folder[SE_NAME_LEN];
	snprintf(folder, sizeof(folder), "%.*s", (int)(slash - start), start);

	char* open = strrchr(folder, '(');
	if (open) {
		char tag[SE_NAME_LEN];
		snprintf(tag, sizeof(tag), "%s", open + 1);
		char* close = strchr(tag, ')');
		if (close) *close = '\0';
		if (tag[0] && !strcasecmp(tag, arg)) return 1;
		if (open > folder) *open = '\0';
	}
	char name[SE_NAME_LEN], want[SE_NAME_LEN];
	SE_compact(folder, name, sizeof(name));
	SE_compact(arg, want, sizeof(want));
	return want[0] && !strncmp(name, want, strlen(want));
}

///////////////////////////////////////
// paths

void SE_pathsInit(SEPaths* paths) {
	memset(paths, 0, sizeof(*paths));
}

void SE_pathsFree(SEPaths* paths) {
	for (int i = 0; i < paths->count; i++) free(paths->items[i]);
	free(paths->items);
	SE_pathsInit(paths);
}

int SE_pathsAdd(SEPaths* paths, const char* path) {
	if (!path || grow((void**)&paths->items, &paths->cap, paths->count, sizeof(char*)) != 0) return -1;
	char* copy = strdup(path);
	if (!copy) return -1;
	paths->items[paths->count++] = copy;
	paths->sorted = 0;
	return 0;
}

static int comparePaths(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

int SE_pathsHas(SEPaths* paths, const char* path) {
	if (!path || !paths->count) return 0;
	if (!paths->sorted) {
		qsort(paths->items, (size_t)paths->count, sizeof(char*), comparePaths);
		paths->sorted = 1;
	}
	return bsearch(&path, paths->items, (size_t)paths->count, sizeof(char*), comparePaths) != NULL;
}

///////////////////////////////////////
// order

static int compareOrder(const void* a, const void* b) {
	const SEOrderItem* x = a;
	const SEOrderItem* y = b;
	return strcmp(x->path, y->path);
}

int SE_orderBuild(SEOrder* order, const SEIndex* index, const SEHit* hits, int count) {
	memset(order, 0, sizeof(*order));
	if (count < 1) return 0;
	order->items = malloc(sizeof(SEOrderItem) * (size_t)count);
	if (!order->items) return -1;
	for (int i = 0; i < count; i++) order->items[i] = (SEOrderItem){ index->games[hits[i].game].path, i };
	order->count = count;
	qsort(order->items, (size_t)count, sizeof(SEOrderItem), compareOrder);
	return 0;
}

int SE_orderOf(const SEOrder* order, const char* path) {
	if (!path || !order->count) return -1;
	SEOrderItem key = { path, 0 };
	const SEOrderItem* found = bsearch(&key, order->items, (size_t)order->count, sizeof(SEOrderItem), compareOrder);
	return found ? found->position : -1;
}

void SE_orderFree(SEOrder* order) {
	free(order->items);
	memset(order, 0, sizeof(*order));
}

///////////////////////////////////////
// recents

void SE_trim(char* query) {
	size_t start = 0, len = strlen(query);
	while (start < len && isspace((unsigned char)query[start])) start++;
	while (len > start && isspace((unsigned char)query[len - 1])) len--;
	memmove(query, query + start, len - start);
	query[len - start] = '\0';
}

void SE_recentsAdd(SERecents* recents, const char* query, int max) {
	if (max > SE_RECENTS_MAX) max = SE_RECENTS_MAX;
	if (max < 1) return;
	char text[SE_QUERY_LEN];
	snprintf(text, sizeof(text), "%s", query);
	SE_trim(text);
	if (!text[0]) return;

	char folded[SE_QUERY_LEN * 2], other[SE_QUERY_LEN * 2];
	SE_fold(text, folded, sizeof(folded));
	int kept = 0;
	char items[SE_RECENTS_MAX][SE_QUERY_LEN];
	snprintf(items[kept++], SE_QUERY_LEN, "%s", text);
	for (int i = 0; i < recents->count && kept < max; i++) {
		SE_fold(recents->items[i], other, sizeof(other));
		if (!strcmp(folded, other)) continue;
		snprintf(items[kept++], SE_QUERY_LEN, "%s", recents->items[i]);
	}
	memcpy(recents->items, items, sizeof(items));
	recents->count = kept;
}
