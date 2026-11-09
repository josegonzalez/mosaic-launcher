#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "names_model.h"
#include "po_model.h"

static int lower(int c) {
	return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int alnum(int c) {
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

int Nm_key(const char* name, int strip_extension, char* out, size_t size) {
	if (!name || !out || size == 0) return -1;
	out[0] = '\0';
	char text[NM_KEY_MAX * 2];
	size_t len = strlen(name);
	if (len >= sizeof(text)) return -1;
	memcpy(text, name, len + 1);
	if (strip_extension) {
		char* dot = strrchr(text, '.');
		// an extension is short, with no spaces or brackets: "Dr. Mario (World)" has none
		size_t ext = dot ? strlen(dot + 1) : 0;
		if (dot && dot != text && ext >= 1 && ext <= 8 && !strpbrk(dot + 1, " ()[]{}")) {
			*dot = '\0';
			len = (size_t)(dot - text);
		}
	}

	// the groups go, and letters are lowercased
	char plain[sizeof(text)];
	size_t n = 0;
	int depth = 0;
	for (size_t i = 0; i < len; i++) {
		char c = text[i];
		if (c == '(' || c == '[' || c == '{') depth++;
		else if (c == ')' || c == ']' || c == '}') {
			if (depth) depth--;
		}
		else if (!depth) plain[n++] = (char)lower((unsigned char)c);
	}
	plain[n] = '\0';
	while (n && (plain[n - 1] == ' ' || plain[n - 1] == '\t')) plain[--n] = '\0';
	const char* start = plain;
	while (*start == ' ') start++;
	if (n >= 5 && !strcmp(plain + n - 5, ", the")) plain[n - 5] = '\0';
	if (!strncmp(start, "the ", 4)) start += 4;

	size_t k = 0;
	for (const char* p = start; *p; p++) {
		unsigned char c = (unsigned char)*p;
		const char* add = NULL;
		char one[2] = { (char)c, '\0' };
		if (c == '&') add = "and";
		else if (alnum(c) || c >= 0x80) add = one;
		if (!add) continue;
		size_t alen = strlen(add);
		if (k + alen >= size) return -1;
		memcpy(out + k, add, alen);
		k += alen;
	}
	out[k] = '\0';
	return k ? 0 : -1;
}

int Nm_tag(const char* folder, char* out, size_t size) {
	if (!folder || !out || size == 0) return -1;
	out[0] = '\0';
	const char* open = strrchr(folder, '(');
	if (!open) return -1;
	const char* close = strchr(open, ')');
	if (!close || close == open + 1) return -1;
	size_t len = (size_t)(close - open - 1);
	if (len >= size) return -1;
	for (size_t i = 0; i < len; i++) {
		char c = open[1 + i];
		if (c == '/' || c == '.' || c == '\\') return -1;
	}
	memcpy(out, open + 1, len);
	out[len] = '\0';
	return 0;
}

static int byKey(const void* a, const void* b) {
	const NmName* x = a;
	const NmName* y = b;
	int c = strcmp(x->key, y->key);
	if (c) return c;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

// The index of key in names' sorted items, or -1.
static int findKey(const NmNames* names, const char* key) {
	int lo = 0, hi = names->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(names->items[mid].key, key);
		if (!c) return mid;
		if (c < 0) lo = mid + 1;
		else hi = mid - 1;
	}
	return -1;
}

int Nm_namesRead(NmNames* names, const char* text) {
	if (!names || !text) return 0;
	int added = 0;
	for (const char* line = text; *line; ) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		const char* next = end ? end + 1 : line + len;
		if (len && line[len - 1] == '\r') len--;
		const char* tab = memchr(line, '\t', len);
		if (!len || line[0] == '#' || !tab) {
			line = next;
			continue;
		}
		char english[NM_KEY_MAX * 2], key[NM_KEY_MAX];
		size_t elen = (size_t)(tab - line);
		const char* value = tab + 1;
		size_t vlen = len - elen - 1;
		const char* vtab = memchr(value, '\t', vlen);
		if (vtab) vlen = (size_t)(vtab - value);
		while (vlen && (*value == ' ')) { value++; vlen--; }
		while (vlen && value[vlen - 1] == ' ') vlen--;
		if (elen >= sizeof(english) || !vlen || !Po_utf8(value, vlen)) {
			line = next;
			continue;
		}
		memcpy(english, line, elen);
		english[elen] = '\0';
		if (Nm_key(english, 0, key, sizeof(key)) < 0) {
			line = next;
			continue;
		}
		if (names->count >= names->cap) {
			int cap = names->cap ? names->cap * 2 : 64;
			NmName* items = realloc(names->items, (size_t)cap * sizeof(NmName));
			if (!items) return -1;
			names->items = items;
			names->cap = cap;
		}
		NmName* item = &names->items[names->count];
		item->key = strdup(key);
		item->name = malloc(vlen + 1);
		if (!item->key || !item->name) {
			free(item->key);
			free(item->name);
			return -1;
		}
		memcpy(item->name, value, vlen);
		item->name[vlen] = '\0';
		item->seq = names->seq++;
		names->count++;
		added++;
		line = next;
	}
	if (!added) return 0;

	// sorted by key, keeping the first of each: earlier lists, then earlier lines
	qsort(names->items, (size_t)names->count, sizeof(NmName), byKey);
	int kept = 0;
	for (int i = 0; i < names->count; i++) {
		if (kept && !strcmp(names->items[kept - 1].key, names->items[i].key)) {
			free(names->items[i].key);
			free(names->items[i].name);
			added--;
			continue;
		}
		names->items[kept++] = names->items[i];
	}
	names->count = kept;
	return added;
}

const char* Nm_namesFind(const NmNames* names, const char* key) {
	if (!names || !key || !names->count) return NULL;
	int i = findKey(names, key);
	return i < 0 ? NULL : names->items[i].name;
}

void Nm_namesFree(NmNames* names) {
	if (!names) return;
	for (int i = 0; i < names->count; i++) {
		free(names->items[i].key);
		free(names->items[i].name);
	}
	free(names->items);
	memset(names, 0, sizeof(*names));
}

// 1 when existing has a line for file
static int listed(const char* existing, const char* file) {
	size_t flen = strlen(file);
	for (const char* line = existing; line && *line; ) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		if (len > flen && line[flen] == '\t' && !strncmp(line, file, flen)) return 1;
		line = end ? end + 1 : NULL;
	}
	return 0;
}

char* Nm_merge(const char* existing, const char* const* files, const int* dirs, int count, const NmNames* names, int* added) {
	if (added) *added = 0;
	if (!existing) existing = "";
	size_t len = strlen(existing);
	size_t cap = len + 2;
	char* out = malloc(cap);
	if (!out) return NULL;
	memcpy(out, existing, len + 1);
	int n = 0;
	for (int i = 0; i < count; i++) {
		const char* file = files[i];
		char key[NM_KEY_MAX];
		if (!file || !file[0] || strchr(file, '\t') || strchr(file, '\n')) continue;
		if (Nm_key(file, !(dirs && dirs[i]), key, sizeof(key)) < 0) continue;
		const char* name = Nm_namesFind(names, key);
		if (!name || listed(existing, file) || listed(out + len, file)) continue;
		size_t need = strlen(out) + strlen(file) + strlen(name) + 4;
		if (need > cap) {
			cap = need * 2;
			char* grown = realloc(out, cap);
			if (!grown) {
				free(out);
				return NULL;
			}
			out = grown;
		}
		size_t at = strlen(out);
		if (at && out[at - 1] != '\n') out[at++] = '\n';
		at += (size_t)snprintf(out + at, cap - at, "%s\t%s\n", file, name);
		n++;
	}
	if (added) *added = n;
	return out;
}
