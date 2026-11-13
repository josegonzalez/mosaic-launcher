#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "search_model.h"

static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

void SE_cleanName(const char* file_name, char* out, size_t size) {
	if (!size) return;
	snprintf(out, size, "%s", file_name);

	char* dot;
	while ((dot = strrchr(out, '.')) != NULL) {
		size_t len = strlen(dot);
		if (len < 3 || len > 5) break;
		*dot = '\0';
	}

	char bare[SE_NAME_LEN];
	snprintf(bare, sizeof(bare), "%s", out);
	for (;;) {
		char* open = strrchr(out, '(');
		char* square = strrchr(out, '[');
		if (!open || (square && square > open)) open = square;
		if (!open || open == out) break;
		*open = '\0';
	}
	if (!out[0]) snprintf(out, size, "%s", bare);

	size_t len = strlen(out);
	while (len > 1 && isspace((unsigned char)out[len - 1])) out[--len] = '\0';
}

int SE_hidden(const char* name) {
	return name[0] == '.' || endsWith(name, ".disabled") || !strcmp(name, "map.txt");
}

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
// map.txt

static int compareItems(const void* a, const void* b) {
	const SEMapItem* x = a;
	const SEMapItem* y = b;
	int c = strcmp(x->key, y->key);
	return c ? c : x->line - y->line;
}

int SE_mapParse(SEMap* map, char* text) {
	memset(map, 0, sizeof(*map));
	map->text = text;
	if (!text) return 0;

	int lines = 1;
	for (const char* p = text; *p; p++) {
		if (*p == '\n') lines++;
	}
	map->items = malloc(sizeof(SEMapItem) * (size_t)lines);
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
			map->items[map->count++] = (SEMapItem){ p, tab + 1, line };
		}
		p = end ? end + 1 : NULL;
	}

	// keep only the last line for a file name
	qsort(map->items, (size_t)map->count, sizeof(SEMapItem), compareItems);
	int kept = 0;
	for (int i = 0; i < map->count; i++) {
		if (i + 1 < map->count && !strcmp(map->items[i].key, map->items[i + 1].key)) continue;
		map->items[kept++] = map->items[i];
	}
	map->count = kept;
	return 0;
}

const char* SE_mapGet(const SEMap* map, const char* file_name) {
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

void SE_mapFree(SEMap* map) {
	free(map->items);
	free(map->text);
	memset(map, 0, sizeof(*map));
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
	}
	for (int i = 0; i < index->folder_count; i++) free(index->folders[i].path);
	free(index->games);
	free(index->folders);
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

int SE_indexAdd(SEIndex* index, const char* path, const char* name, int type) {
	if (grow((void**)&index->games, &index->game_cap, index->game_count, sizeof(SEGame)) != 0) return -1;
	char folded[SE_NAME_LEN];
	SE_fold(name, folded, sizeof(folded));
	SEGame* game = &index->games[index->game_count];
	game->path = strdup(path);
	game->name = strdup(name);
	game->folded = strdup(folded);
	game->type = type;
	if (!game->path || !game->name || !game->folded) {
		free(game->path);
		free(game->name);
		free(game->folded);
		return -1;
	}
	index->game_count++;
	return 0;
}

int SE_indexAddFolder(SEIndex* index, const char* path, time_t mtime) {
	if (grow((void**)&index->folders, &index->folder_cap, index->folder_count, sizeof(SEFolder)) != 0) return -1;
	SEFolder* folder = &index->folders[index->folder_count];
	folder->path = strdup(path);
	if (!folder->path) return -1;
	folder->mtime = mtime;
	index->folder_count++;
	return 0;
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

// True when game passes every filter of kind (or every kind but it, with
// others set).
static int passes(const SEIndex* index, int game, const SEQuery* query, SEFilterFn test, void* data, int kind,
		int others) {
	for (int i = 0; i < query->filter_count; i++) {
		const SEFilter* f = &query->filters[i];
		if ((f->kind == kind) == (others != 0)) continue;
		if ((test(&index->games[game], i, f, data) != 0) == (f->negate != 0)) return 0;
	}
	return 1;
}

int SE_search(const SEIndex* index, const SEQuery* query, SEFilterFn test, void* data, SEHit* hits) {
	char folded[SE_QUERY_LEN * 2];
	SE_fold(query->words, folded, sizeof(folded));
	if (!folded[0] && !query->filter_count) return 0;
	if (query->filter_count && !test) return 0;

	int count = 0;
	for (int i = 0; i < index->game_count; i++) {
		int rank = folded[0] ? SE_rank(index->games[i].folded, folded) : SE_RANK_EXACT;
		if (rank < 0) continue;
		if (query->filter_count && (!passes(index, i, query, test, data, SE_FILTER_UNPLAYED, 1)
				|| !passes(index, i, query, test, data, SE_FILTER_UNPLAYED, 0))) continue;
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
		int kind = -1;
		const char* arg = "";
		if (!strcasecmp(body, "fav:")) kind = SE_FILTER_FAV;
		else if (!strcasecmp(body, "unplayed")) kind = SE_FILTER_UNPLAYED;
		else if (startsWithNoCase(body, "tag:") && body[4]) {
			kind = SE_FILTER_TAG;
			arg = body + 4;
		}
		else if (startsWithNoCase(body, "system:") && body[7]) {
			kind = SE_FILTER_SYSTEM;
			arg = body + 7;
		}
		if (kind >= 0 && out->filter_count < SE_FILTERS_MAX) {
			SEFilter* f = &out->filters[out->filter_count++];
			f->kind = kind;
			f->negate = negate;
			snprintf(f->arg, sizeof(f->arg), "%s", arg);
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
