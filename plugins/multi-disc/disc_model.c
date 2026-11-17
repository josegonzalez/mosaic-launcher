#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "disc_model.h"

static const char* const WORDS[] = { "disc", "cd", "disk" };

// The marker at s, a "(", into out; its length, or 0.
static int markerAt(const char* s, MDMarker* out) {
	if (*s != '(') return 0;
	const char* p = s + 1;
	int word = -1;
	for (int i = 0; i < 3 && word < 0; i++) {
		size_t n = strlen(WORDS[i]);
		if (!strncasecmp(p, WORDS[i], n) && p[n] == ' ') word = i;
	}
	if (word < 0) return 0;
	p += strlen(WORDS[word]) + 1;
	int number = 0, digits = 0;
	while (isdigit((unsigned char)*p) && digits < 4) number = number * 10 + (*p++ - '0'), digits++;
	if (!digits || number < 1 || isdigit((unsigned char)*p)) return 0;
	int total = 0;
	if (!strncasecmp(p, " of ", 4)) {
		p += 4;
		digits = 0;
		while (isdigit((unsigned char)*p) && digits < 4) total = total * 10 + (*p++ - '0'), digits++;
		if (!digits || isdigit((unsigned char)*p) || total < number) return 0;
	}
	if (*p != ')') return 0;
	if (out) {
		out->number = number;
		out->total = total;
		out->start = 0;
		out->len = (int)(p + 1 - s);
		snprintf(out->word, sizeof(out->word), "%s", WORDS[word]);
	}
	return (int)(p + 1 - s);
}

int MD_marker(const char* name, MDMarker* out) {
	if (!name) return 0;
	int found = 0;
	MDMarker marker;
	memset(&marker, 0, sizeof(marker));
	for (const char* s = strchr(name, '('); s; s = strchr(s + 1, '(')) {
		MDMarker here;
		if (!markerAt(s, &here)) continue;
		if (++found > 1) return 0;
		marker = here;
		marker.start = (int)(s - name);
	}
	if (found != 1) return 0;
	if (out) *out = marker;
	return 1;
}

const char* MD_extension(const char* name) {
	const char* dot = name ? strrchr(name, '.') : NULL;
	if (!dot || dot == name) return NULL;
	size_t len = strlen(dot);
	return len > 2 && len <= 5 ? dot : NULL;
}

int MD_baseName(const char* name, const MDMarker* marker, char* out, size_t size) {
	if (!size) return 0;
	out[0] = '\0';
	size_t len = strlen(name);
	if (len >= MD_NAME_LEN) return 0;
	const char* ext = MD_extension(name);
	size_t end = ext ? (size_t)(ext - name) : len;
	size_t start = (size_t)marker->start, stop = start + (size_t)marker->len;
	if (stop > end) return 0;
	// the marker and one space beside it
	if (start > 0 && name[start - 1] == ' ') start--;
	else if (stop < end && name[stop] == ' ') stop++;
	char base[MD_NAME_LEN];
	snprintf(base, sizeof(base), "%.*s%.*s", (int)start, name, (int)(end - stop), name + stop);
	if (!base[0] || base[0] == '.' || base[0] == ' ' || base[strlen(base) - 1] == ' ') return 0;
	if (strlen(base) >= size) return 0;
	snprintf(out, size, "%s", base);
	return 1;
}

void MD_groupsInit(MDGroups* groups) {
	groups->items = NULL;
	groups->count = 0;
}

void MD_groupsFree(MDGroups* groups) {
	free(groups->items);
	MD_groupsInit(groups);
}

static int endsWithCase(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcasecmp(s + a - b, suffix);
}

static int isSheet(const char* name) {
	return endsWithCase(name, ".cue") || endsWithCase(name, ".ccd") || endsWithCase(name, ".gdi");
}

static int isSidecar(const char* name) {
	static const char* const exts[] = { ".sbi", ".sub", ".wav", ".mp3", ".ogg", ".flac", ".ape" };
	for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
		if (endsWithCase(name, exts[i])) return 1;
	}
	return 0;
}

// The name up to its last dot, its length.
static size_t stemLength(const char* name) {
	const char* dot = strrchr(name, '.');
	return dot && dot != name ? (size_t)(dot - name) : strlen(name);
}

// 1 when name starts with sheet's name without its extension, followed by
// a space, a dot or nothing.
static int ofSheet(const char* sheet, const char* name) {
	size_t n = stemLength(sheet);
	return !strncmp(name, sheet, n) && (name[n] == '\0' || name[n] == ' ' || name[n] == '.');
}

typedef struct Candidate {
	int index;
	int number;
	char key[MD_NAME_LEN * 2];
	char m3u[MD_NAME_LEN];
} Candidate;

static int byKey(const void* a, const void* b) {
	const Candidate* x = a;
	const Candidate* y = b;
	int c = strcmp(x->key, y->key);
	return c ? c : x->number - y->number;
}

static int byM3u(const void* a, const void* b) {
	return strcmp(((const MDGroup*)a)->m3u, ((const MDGroup*)b)->m3u);
}

// 1 when the name can be a disc.
static int candidate(const char* const* names, int count, int i) {
	const char* name = names[i];
	if (!name || name[0] == '.' || endsWithCase(name, ".m3u") || isSidecar(name)) return 0;
	if (isSheet(name)) return 1;
	for (int k = 0; k < count; k++) {
		if (k != i && names[k] && names[k][0] != '.' && isSheet(names[k]) && ofSheet(names[k], name)) return 0;
	}
	return 1;
}

int MD_group(const char* const* names, int count, MDGroups* out) {
	MD_groupsInit(out);
	if (count <= 0) return 0;
	Candidate* found = malloc(sizeof(Candidate) * (size_t)count);
	if (!found) return -1;
	int n = 0;
	for (int i = 0; i < count; i++) {
		MDMarker marker;
		if (!candidate(names, count, i) || !MD_marker(names[i], &marker)) continue;
		Candidate* c = &found[n];
		char base[MD_NAME_LEN];
		if (!MD_baseName(names[i], &marker, base, sizeof(base)) || strlen(base) + 4 >= sizeof(c->m3u)) continue;
		const char* ext = MD_extension(names[i]);
		char lower[8] = "";
		for (int k = 0; ext && ext[k] && k < 7; k++) lower[k] = (char)tolower((unsigned char)ext[k]), lower[k + 1] = '\0';
		snprintf(c->key, sizeof(c->key), "%s\t%s\t%s\t%i", base, lower, marker.word, marker.total);
		size_t base_len = strlen(base); // fits, from the check above
		memcpy(c->m3u, base, base_len);
		memcpy(c->m3u + base_len, ".m3u", 5);
		c->index = i;
		c->number = marker.number;
		n++;
	}
	qsort(found, (size_t)n, sizeof(Candidate), byKey);

	MDGroup* groups = malloc(sizeof(MDGroup) * (size_t)(n ? n : 1));
	if (!groups) {
		free(found);
		return -1;
	}
	int made = 0;
	for (int i = 0; i < n;) {
		int j = i;
		while (j < n && !strcmp(found[j].key, found[i].key)) j++;
		int ok = j - i >= 2 && j - i <= MD_DISCS_MAX;
		for (int k = i + 1; ok && k < j; k++) ok = found[k].number != found[k - 1].number;
		if (ok) {
			MDGroup* g = &groups[made++];
			snprintf(g->m3u, sizeof(g->m3u), "%s", found[i].m3u);
			g->count = j - i;
			for (int k = i; k < j; k++) g->discs[k - i] = found[k].index;
		}
		i = j;
	}
	free(found);

	// two that would share an .m3u name stay as they are
	int kept = 0;
	for (int i = 0; i < made; i++) {
		int clash = 0;
		for (int k = 0; k < made && !clash; k++) clash = k != i && !strcasecmp(groups[k].m3u, groups[i].m3u);
		if (!clash) groups[kept++] = groups[i];
	}
	if (!kept) {
		free(groups);
		return 0;
	}
	qsort(groups, (size_t)kept, sizeof(MDGroup), byM3u);
	out->items = groups;
	out->count = kept;
	return kept;
}

int MD_findGroup(const MDGroups* groups, const char* m3u) {
	for (int i = 0; m3u && i < groups->count; i++) {
		if (!strcmp(groups->items[i].m3u, m3u)) return i;
	}
	return -1;
}

int MD_findDisc(const MDGroups* groups, const char* const* names, const char* disc) {
	for (int i = 0; disc && i < groups->count; i++) {
		for (int k = 0; k < groups->items[i].count; k++) {
			if (!strcmp(names[groups->items[i].discs[k]], disc)) return i;
		}
	}
	return -1;
}

int MD_m3uText(const MDGroup* group, const char* const* names, char* out, size_t size) {
	size_t used = 0;
	if (size) out[0] = '\0';
	for (int i = 0; i < group->count; i++) {
		int n = snprintf(out + used, size - used, "%s\n", names[group->discs[i]]);
		if (n < 0 || (size_t)n >= size - used) {
			if (size) out[0] = '\0';
			return -1;
		}
		used += (size_t)n;
	}
	return (int)used;
}

int MD_stockFolder(const char* dir_name, const char* const* names, int count) {
	if (!dir_name || !dir_name[0]) return 0;
	char m3u[MD_NAME_LEN + 8], cue[MD_NAME_LEN + 8];
	snprintf(m3u, sizeof(m3u), "%s.m3u", dir_name);
	snprintf(cue, sizeof(cue), "%s.cue", dir_name);
	for (int i = 0; i < count; i++) {
		if (names[i] && (!strcmp(names[i], m3u) || !strcmp(names[i], cue))) return 1;
	}
	return 0;
}

// The name with its extension cut as minarch cuts it.
static void cut(const char* name, char* out, size_t size) {
	const char* ext = MD_extension(name);
	snprintf(out, size, "%.*s", ext ? (int)(ext - name) : (int)strlen(name), name);
}

static int add(MDCopy* out, int max, int n, int place, const char* from_name, const char* to_name, const char* suffix) {
	if (n >= max) return n;
	char from[sizeof(out->from)], to[sizeof(out->to)];
	if (snprintf(from, sizeof(from), "%s%s", from_name, suffix) >= (int)sizeof(from)) return n;
	if (snprintf(to, sizeof(to), "%s%s", to_name, suffix) >= (int)sizeof(to)) return n;
	if (!strcmp(from, to)) return n;
	for (int i = 0; i < n; i++) {
		if (out[i].place == place && !strcmp(out[i].from, from)) return n;
	}
	out[n].place = place;
	snprintf(out[n].from, sizeof(out[n].from), "%s", from);
	snprintf(out[n].to, sizeof(out[n].to), "%s", to);
	return n + 1;
}

int MD_saveCopies(const char* disc, const char* m3u, MDCopy* out, int max) {
	if (!disc || !m3u || max <= 0) return 0;
	if (max > MD_COPIES_MAX) max = MD_COPIES_MAX;
	char disc_cut[MD_NAME_LEN], m3u_cut[MD_NAME_LEN];
	cut(disc, disc_cut, sizeof(disc_cut));
	cut(m3u, m3u_cut, sizeof(m3u_cut));
	char suffix[24];
	int n = 0;
	n = add(out, max, n, MD_SAVES, disc, m3u, ".sav");
	n = add(out, max, n, MD_SAVES, disc, m3u, ".rtc");
	n = add(out, max, n, MD_SAVES, disc_cut, m3u_cut, ".sav");
	n = add(out, max, n, MD_SAVES, disc_cut, m3u_cut, ".srm");
	for (int slot = 0; slot <= 9; slot++) {
		snprintf(suffix, sizeof(suffix), ".st%i", slot);
		n = add(out, max, n, MD_STATES, disc, m3u, suffix);
	}
	n = add(out, max, n, MD_STATES, disc_cut, m3u_cut, ".state");
	n = add(out, max, n, MD_STATES, disc_cut, m3u_cut, ".state.auto");
	for (int slot = 0; slot <= 8; slot++) {
		if (slot) {
			snprintf(suffix, sizeof(suffix), ".state%i", slot);
			n = add(out, max, n, MD_STATES, disc_cut, m3u_cut, suffix);
		}
		snprintf(suffix, sizeof(suffix), ".state.%i", slot);
		n = add(out, max, n, MD_STATES, disc_cut, m3u_cut, suffix);
	}
	n = add(out, max, n, MD_SLOTS, disc, m3u, ".txt");
	for (int slot = 0; slot <= 9; slot++) {
		snprintf(suffix, sizeof(suffix), ".%i.bmp", slot);
		n = add(out, max, n, MD_SLOTS, disc, m3u, suffix);
		snprintf(suffix, sizeof(suffix), ".%i.txt", slot);
		n = add(out, max, n, MD_SLOTS, disc, m3u, suffix);
	}
	return n;
}

int MD_companion(const char* disc, const char* other) {
	if (!disc || !other || !strcmp(disc, other) || other[0] == '.' || endsWithCase(other, ".m3u")) return 0;
	if (isSheet(disc) && !isSheet(other) && ofSheet(disc, other)) return 1;
	size_t n = stemLength(disc);
	return isSidecar(other) && stemLength(other) == n && !strncmp(disc, other, n);
}

int MD_m3uMoved(const char* text, const char* const* moved, int count, char* out, size_t size) {
	if (!size) return -1;
	out[0] = '\0';
	size_t used = 0;
	int lines = 0;
	for (const char* p = text ? text : ""; *p;) {
		size_t len = strcspn(p, "\r\n");
		if (len) {
			int named = 0;
			for (int i = 0; i < count && !named; i++) named = strlen(moved[i]) == len && !strncmp(moved[i], p, len);
			if (!named || used + len + 1 >= size) {
				out[0] = '\0';
				return -1;
			}
			memcpy(out + used, p, len);
			used += len;
			out[used++] = '\n';
			out[used] = '\0';
			lines++;
		}
		p += len;
		while (*p == '\r' || *p == '\n') p++;
	}
	if (!lines) {
		out[0] = '\0';
		return -1;
	}
	return (int)used;
}
