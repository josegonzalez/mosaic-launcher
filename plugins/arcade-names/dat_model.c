#include "dat_model.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// in into out, cut to fit in size bytes.
static void copyStr(char* out, size_t size, const char* in) {
	size_t len = strlen(in);
	if (len >= size) len = size - 1;
	memcpy(out, in, len);
	out[len] = '\0';
}

///////////////////////////////////////
// the table

void AN_init(ANTable* t) {
	memset(t, 0, sizeof(*t));
}

void AN_free(ANTable* t) {
	free(t->strings);
	free(t->sets);
	AN_init(t);
}

const char* AN_str(const ANTable* t, uint32_t offset) {
	return t && t->strings && offset && offset < t->strings_len ? t->strings + offset : "";
}

size_t AN_bytes(const ANTable* t) {
	return sizeof(*t) + t->strings_cap + sizeof(ANSet) * (size_t)t->cap;
}

// Returns its offset, or 0 for "" and when there's no room.
static uint32_t addString(ANTable* t, const char* s) {
	if (!s || !s[0]) return 0;
	size_t len = strlen(s) + 1;
	if (t->strings_len + len > AN_STRINGS_MAX) {
		t->full = 1;
		return 0;
	}
	if (t->strings_len + len > t->strings_cap) {
		size_t cap = t->strings_cap ? t->strings_cap * 2 : 65536;
		while (cap < t->strings_len + len) cap *= 2;
		char* bigger = realloc(t->strings, cap);
		if (!bigger) {
			t->full = 1;
			return 0;
		}
		t->strings = bigger;
		t->strings_cap = cap;
	}
	if (!t->strings_len) t->strings[t->strings_len++] = '\0'; // offset 0 is ""
	uint32_t at = (uint32_t)t->strings_len;
	memcpy(t->strings + at, s, len);
	t->strings_len += len;
	return at;
}

static void lower(char* s) {
	for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

// A set as a DAT describes it. Returns 1 when it was added.
static int addSet(ANTable* t, const char* name, const char* parent, const char* desc, uint32_t flags) {
	if (t->finished || !name || !name[0] || strlen(name) >= AN_NAME_LEN) return 0;
	if (t->count >= AN_SETS_MAX) {
		t->full = 1;
		return 0;
	}
	if (t->count == t->cap) {
		int cap = t->cap ? t->cap * 2 : 1024;
		ANSet* bigger = realloc(t->sets, sizeof(ANSet) * (size_t)cap);
		if (!bigger) {
			t->full = 1;
			return 0;
		}
		t->sets = bigger;
		t->cap = cap;
	}
	char key[AN_NAME_LEN], parent_key[AN_NAME_LEN] = "";
	copyStr(key, sizeof(key), name);
	lower(key);
	if (parent && parent[0] && strlen(parent) < AN_NAME_LEN) {
		copyStr(parent_key, sizeof(parent_key), parent);
		lower(parent_key);
		if (!strcmp(parent_key, key)) parent_key[0] = '\0';
	}
	ANSet set = { addString(t, key), addString(t, desc), 0, flags };
	if (parent_key[0]) set.parent = (int32_t)addString(t, parent_key);
	if (!set.name) return 0;
	t->sets[t->count++] = set;
	return 1;
}

///////////////////////////////////////
// reading, from a file or memory, a buffer at a time

typedef struct Reader {
	FILE* f;
	const unsigned char* data;
	size_t len, at;
	long long total;
	int eof;
	unsigned char buf[65536];
} Reader;

static int next(Reader* r) {
	if (r->at < r->len) return r->data[r->at++];
	if (r->eof || !r->f || r->total >= AN_FILE_MAX) {
		r->eof = 1;
		return -1;
	}
	r->len = fread(r->buf, 1, sizeof(r->buf), r->f);
	r->data = r->buf;
	r->at = 0;
	r->total += (long long)r->len;
	if (!r->len) {
		r->eof = 1;
		return -1;
	}
	return r->data[r->at++];
}

// Puts back the character next just returned.
static void back(Reader* r) {
	if (r->at > 0) r->at--;
}

static Reader* readerFile(FILE* f) {
	Reader* r = calloc(1, sizeof(Reader));
	if (r) r->f = f;
	return r;
}

static Reader* readerText(const char* text, size_t len) {
	Reader* r = calloc(1, sizeof(Reader));
	if (!r) return NULL;
	r->data = (const unsigned char*)text;
	r->len = len;
	r->eof = 1;
	return r;
}

///////////////////////////////////////
// text

// Appends c to a buffer of size, keeping it terminated, while it fits.
static void put(char* out, size_t* len, size_t size, int c) {
	if (*len + 1 < size) {
		out[(*len)++] = (char)c;
		out[*len] = '\0';
	}
}

// Cuts s to fit in size bytes, and drops a UTF-8 character left cut short
// at its end, as by an earlier cut.
static void cutUtf8(char* s, size_t size) {
	size_t len = strlen(s);
	if (len >= size) s[len = size - 1] = '\0';
	size_t start = len;
	while (start > 0 && ((unsigned char)s[start - 1] & 0xC0) == 0x80) start--;
	if (!start) return;
	unsigned char lead = (unsigned char)s[start - 1];
	size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
	if (len - (start - 1) < want) s[start - 1] = '\0';
}

static int utf8(unsigned long cp, char* out) {
	if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

// XML text as it reads: the predefined entities and character references
// decoded, anything else left as it is, white space runs made one space
// and trimmed.
static void decode(const char* in, char* out, size_t size) {
	size_t len = 0;
	out[0] = '\0';
	int space = 0;
	for (const char* p = in; *p; p++) {
		char bytes[4];
		int n = 1;
		bytes[0] = *p;
		if (*p == '&') {
			static const struct {
				const char* name;
				char c;
			} named[] = { { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' } };
			int matched = 0;
			for (size_t k = 0; k < sizeof(named) / sizeof(named[0]) && !matched; k++) {
				size_t l = strlen(named[k].name);
				if (!strncmp(p + 1, named[k].name, l)) {
					bytes[0] = named[k].c;
					p += l;
					matched = 1;
				}
			}
			if (!matched && p[1] == '#') {
				const char* q = p + 2;
				int hex = *q == 'x' || *q == 'X';
				if (hex) q++;
				unsigned long cp = 0;
				int digits = 0;
				while (digits < 8 && (hex ? isxdigit((unsigned char)*q) : isdigit((unsigned char)*q))) {
					int d = isdigit((unsigned char)*q) ? *q - '0' : tolower((unsigned char)*q) - 'a' + 10;
					cp = cp * (hex ? 16 : 10) + (unsigned long)d;
					q++;
					digits++;
				}
				int m;
				if (digits && *q == ';' && (m = utf8(cp, bytes)) > 0) {
					n = m;
					p = q;
				}
			}
		}
		if (n == 1 && (bytes[0] == ' ' || bytes[0] == '\t' || bytes[0] == '\r' || bytes[0] == '\n')) {
			space = 1;
			continue;
		}
		if (space && len) put(out, &len, size, ' ');
		space = 0;
		for (int k = 0; k < n; k++) put(out, &len, size, bytes[k]);
	}
	cutUtf8(out, size);
}

///////////////////////////////////////
// Logiqx XML

// Skips to the end of "<!--", whose "<!--" was read.
static void skipComment(Reader* r) {
	int dashes = 0, c;
	while ((c = next(r)) >= 0) {
		if (c == '>' && dashes >= 2) return;
		dashes = c == '-' ? dashes + 1 : 0;
	}
}

// Skips to the end of "<?", whose "<?" was read.
static void skipPi(Reader* r) {
	int prev = 0, c;
	while ((c = next(r)) >= 0) {
		if (c == '>' && prev == '?') return;
		prev = c;
	}
}

// Skips a declaration such as <!DOCTYPE ...>, whose "<!" was read, with
// its internal subset: what it declares, entities included, is ignored.
static void skipDecl(Reader* r) {
	int quote = 0, depth = 0, c;
	while ((c = next(r)) >= 0) {
		if (quote) {
			if (c == quote) quote = 0;
			continue;
		}
		if (c == '"' || c == '\'') quote = c;
		else if (c == '[') depth++;
		else if (c == ']') depth--;
		else if (c == '>' && depth <= 0) return;
		else if (c == '<' && depth > 0) {
			// a declaration of the subset's, or a comment in it
			int a = next(r);
			if (a == '!') {
				int b = next(r);
				if (b == '-') {
					next(r);
					skipComment(r);
					continue;
				}
				back(r);
			}
			else if (a == '?') {
				skipPi(r);
				continue;
			}
			else if (a >= 0) back(r);
			int q = 0;
			while ((c = next(r)) >= 0) {
				if (q) {
					if (c == q) q = 0;
				}
				else if (c == '"' || c == '\'') q = c;
				else if (c == '>') break;
			}
		}
	}
}

// An attribute's value from a tag's text, decoded. Returns 1 when it's there.
static int attr(const char* tag, const char* name, char* out, size_t size) {
	size_t name_len = strlen(name);
	const char* p = tag;
	while (*p && !isspace((unsigned char)*p) && *p != '/') p++; // the element's name
	while (*p) {
		while (isspace((unsigned char)*p)) p++;
		const char* key = p;
		while (*p && *p != '=' && !isspace((unsigned char)*p) && *p != '/') p++;
		size_t key_len = (size_t)(p - key);
		while (isspace((unsigned char)*p)) p++;
		if (*p != '=') {
			if (!*p) break;
			p++;
			continue;
		}
		p++;
		while (isspace((unsigned char)*p)) p++;
		char quote = *p;
		if (quote != '"' && quote != '\'') break;
		const char* value = ++p;
		while (*p && *p != quote) p++;
		if (!*p) break;
		if (key_len == name_len && !strncmp(key, name, name_len)) {
			size_t value_len = (size_t)(p - value);
			char* raw = malloc(value_len + 1);
			if (!raw) return 0;
			memcpy(raw, value, value_len);
			raw[value_len] = '\0';
			decode(raw, out, size);
			free(raw);
			return 1;
		}
		p++;
	}
	out[0] = '\0';
	return 0;
}

// 1 when a tag's text names this element.
static int named(const char* tag, const char* name) {
	size_t n = strlen(name);
	return !strncmp(tag, name, n) && (!tag[n] || isspace((unsigned char)tag[n]) || tag[n] == '/');
}

static int yes(const char* tag, const char* name) {
	char value[16];
	return attr(tag, name, value, sizeof(value)) && !strcmp(value, "yes");
}

typedef struct Pending {
	int open;
	char name[AN_NAME_LEN * 2];
	char parent[AN_NAME_LEN * 2];
	char desc[AN_DESC_LEN * 4]; // as it reads, before decoding
	size_t desc_len;
	int in_desc;
	uint32_t flags;
} Pending;

static int commit(ANTable* t, Pending* set) {
	if (!set->open) return 0;
	set->open = 0;
	set->in_desc = 0;
	char desc[AN_DESC_LEN];
	decode(set->desc, desc, sizeof(desc));
	return addSet(t, set->name, set->parent, desc, set->flags);
}

static void startTag(ANTable* t, Pending* set, const char* tag, int closed, int* added) {
	if (named(tag, "game") || named(tag, "machine")) {
		*added += commit(t, set);
		memset(set, 0, sizeof(*set));
		set->open = 1;
		attr(tag, "name", set->name, sizeof(set->name));
		attr(tag, "cloneof", set->parent, sizeof(set->parent));
		if (yes(tag, "isbios")) set->flags |= AN_BIOS;
		char runnable[8];
		if (yes(tag, "isdevice") || (attr(tag, "runnable", runnable, sizeof(runnable)) && !strcmp(runnable, "no"))) set->flags |= AN_DEVICE;
		if (closed) *added += commit(t, set);
	}
	else if (!set->open) return;
	else if (named(tag, "description") && !closed) {
		set->in_desc = 1;
		set->desc_len = 0;
		set->desc[0] = '\0';
	}
	else if (named(tag, "driver")) {
		char status[32];
		if (attr(tag, "status", status, sizeof(status)) && !strcmp(status, "preliminary")) set->flags |= AN_NOT_WORKING;
	}
}

static void endTag(ANTable* t, Pending* set, const char* tag, int* added) {
	if (named(tag, "game") || named(tag, "machine")) *added += commit(t, set);
	else if (named(tag, "description")) set->in_desc = 0;
}

static int readXml(ANTable* t, Reader* r) {
	Pending* set = calloc(1, sizeof(Pending));
	char* tag = malloc(AN_TAG_MAX);
	if (!set || !tag) {
		free(set);
		free(tag);
		return 0;
	}
	int added = 0, c;
	while ((c = next(r)) >= 0) {
		if (c != '<') {
			if (set->in_desc) put(set->desc, &set->desc_len, sizeof(set->desc), c);
			continue;
		}
		c = next(r);
		if (c == '?') {
			skipPi(r);
			continue;
		}
		if (c == '!') {
			int a = next(r);
			if (a == '-') {
				next(r);
				skipComment(r);
			}
			else if (a == '[') {
				// <![CDATA[ ... ]]>, as text
				char word[7] = "";
				int k = 0;
				while (k < 6 && (c = next(r)) >= 0) word[k++] = (char)c;
				word[k] = '\0';
				if (strcmp(word, "CDATA[") != 0) {
					skipDecl(r);
					continue;
				}
				int brackets = 0; // ']' read and not yet known to be text
				while ((c = next(r)) >= 0) {
					if (c == ']') {
						if (brackets < 2) brackets++;
						else if (set->in_desc) put(set->desc, &set->desc_len, sizeof(set->desc), ']');
						continue;
					}
					if (c == '>' && brackets == 2) break;
					if (set->in_desc) {
						for (; brackets > 0; brackets--) put(set->desc, &set->desc_len, sizeof(set->desc), ']');
						// stands for itself in text that's decoded later
						if (c == '&') {
							const char* amp = "&amp;";
							for (const char* q = amp; *q; q++) put(set->desc, &set->desc_len, sizeof(set->desc), *q);
						}
						else put(set->desc, &set->desc_len, sizeof(set->desc), c);
					}
					brackets = 0;
				}
			}
			else {
				if (a >= 0) back(r);
				skipDecl(r);
			}
			continue;
		}
		if (c < 0) break;
		// a tag, up to its '>' outside quotes
		size_t len = 0;
		int quote = 0, too_long = 0;
		back(r);
		while ((c = next(r)) >= 0) {
			if (quote) {
				if (c == quote) quote = 0;
			}
			else if (c == '"' || c == '\'') quote = c;
			else if (c == '>') break;
			if (len + 1 < AN_TAG_MAX) tag[len++] = (char)c;
			else too_long = 1;
		}
		tag[len] = '\0';
		if (too_long || !len) continue;
		int closed = len > 0 && tag[len - 1] == '/';
		if (closed) tag[--len] = '\0';
		if (tag[0] == '/') endTag(t, set, tag + 1, &added);
		else startTag(t, set, tag, closed, &added);
	}
	added += commit(t, set);
	free(set);
	free(tag);
	return added;
}

///////////////////////////////////////
// ClrMamePro text

enum { TOKEN_END, TOKEN_OPEN, TOKEN_CLOSE, TOKEN_WORD };

// The next token, a word or a quoted string cut to fit size, in out.
static int token(Reader* r, char* out, size_t size) {
	int c;
	do c = next(r);
	while (c >= 0 && isspace(c));
	out[0] = '\0';
	if (c < 0) return TOKEN_END;
	if (c == '(') return TOKEN_OPEN;
	if (c == ')') return TOKEN_CLOSE;
	size_t len = 0;
	if (c == '"') {
		while ((c = next(r)) >= 0 && c != '"' && c != '\n') put(out, &len, size, c);
		return TOKEN_WORD;
	}
	put(out, &len, size, c);
	while ((c = next(r)) >= 0 && !isspace(c) && c != '(' && c != ')' && c != '"') put(out, &len, size, c);
	if (c >= 0 && !isspace(c)) back(r);
	return TOKEN_WORD;
}

static int readCmp(ANTable* t, Reader* r) {
	char word[AN_DESC_LEN * 2], key[64] = "", block[64] = "", sub[64] = "";
	Pending set;
	memset(&set, 0, sizeof(set));
	int depth = 0, added = 0, kind;
	while ((kind = token(r, word, sizeof(word))) != TOKEN_END) {
		if (depth == 0) {
			if (kind == TOKEN_WORD) copyStr(block, sizeof(block), word);
			else if (kind == TOKEN_OPEN) {
				depth = 1;
				key[0] = '\0';
				memset(&set, 0, sizeof(set));
				set.open = !strcmp(block, "game") || !strcmp(block, "machine") || !strcmp(block, "resource");
				if (!strcmp(block, "resource")) set.flags |= AN_BIOS;
				block[0] = '\0';
			}
			continue;
		}
		if (depth == 1) {
			if (kind == TOKEN_CLOSE) {
				if (set.open) {
					// already decoded: nothing in this format is escaped
					char desc[AN_DESC_LEN];
					size_t len = strlen(set.desc);
					if (len >= sizeof(desc)) len = sizeof(desc) - 1;
					memcpy(desc, set.desc, len);
					desc[len] = '\0';
					cutUtf8(desc, sizeof(desc));
					set.open = 0;
					added += addSet(t, set.name, set.parent, desc, set.flags);
				}
				depth = 0;
				continue;
			}
			if (kind == TOKEN_OPEN) {
				copyStr(sub, sizeof(sub), key);
				key[0] = '\0';
				depth = 2;
				continue;
			}
			if (!key[0]) {
				copyStr(key, sizeof(key), word);
				continue;
			}
			if (set.open) {
				if (!strcmp(key, "name")) copyStr(set.name, sizeof(set.name), word);
				else if (!strcmp(key, "cloneof")) copyStr(set.parent, sizeof(set.parent), word);
				else if (!strcmp(key, "description")) copyStr(set.desc, sizeof(set.desc), word);
				else if (!strcmp(key, "isbios") && !strcmp(word, "yes")) set.flags |= AN_BIOS;
				else if (!strcmp(key, "isdevice") && !strcmp(word, "yes")) set.flags |= AN_DEVICE;
				else if (!strcmp(key, "runnable") && !strcmp(word, "no")) set.flags |= AN_DEVICE;
			}
			key[0] = '\0';
			continue;
		}
		// depth 2 and deeper: only driver ( status ... ) matters
		if (kind == TOKEN_OPEN) depth++;
		else if (kind == TOKEN_CLOSE) {
			if (--depth == 1) key[0] = '\0';
		}
		else if (depth == 2 && !strcmp(sub, "driver")) {
			if (!key[0]) copyStr(key, sizeof(key), word);
			else {
				if (!strcmp(key, "status") && !strcmp(word, "preliminary")) set.flags |= AN_NOT_WORKING;
				key[0] = '\0';
			}
		}
	}
	return added;
}

///////////////////////////////////////

static int readDat(ANTable* t, Reader* r) {
	int c;
	do c = next(r);
	while (c >= 0 && (isspace(c) || c == 0xEF || c == 0xBB || c == 0xBF)); // and a UTF-8 byte order mark
	if (c < 0) return 0;
	back(r);
	return c == '<' ? readXml(t, r) : readCmp(t, r);
}

int AN_readDat(ANTable* t, const char* path) {
	FILE* f = path ? fopen(path, "rb") : NULL;
	if (!f) return -1;
	Reader* r = readerFile(f);
	int added = r ? readDat(t, r) : 0;
	free(r);
	fclose(f);
	return added;
}

int AN_readDatText(ANTable* t, const char* text, size_t len) {
	if (!text) return -1;
	Reader* r = readerText(text, len);
	int added = r ? readDat(t, r) : 0;
	free(r);
	return added;
}

///////////////////////////////////////
// finishing: sorted, one of each name, parents by index

static const ANTable* sorting;

static int compareOrder(const void* a, const void* b) {
	int ia = *(const int*)a, ib = *(const int*)b;
	int c = strcmp(sorting->strings + sorting->sets[ia].name, sorting->strings + sorting->sets[ib].name);
	return c ? c : (ia > ib) - (ia < ib); // the first read first
}

int AN_finish(ANTable* t) {
	if (t->finished) return 0;
	int* order = t->count ? malloc(sizeof(int) * (size_t)t->count) : NULL;
	ANSet* sets = t->count ? malloc(sizeof(ANSet) * (size_t)t->count) : NULL;
	if (t->count && (!order || !sets)) {
		free(order);
		free(sets);
		AN_free(t);
		return -1;
	}
	for (int i = 0; i < t->count; i++) order[i] = i;
	sorting = t;
	if (t->count > 1) qsort(order, (size_t)t->count, sizeof(int), compareOrder);
	sorting = NULL;

	// the kept sets, with their strings in a new table
	ANTable out;
	AN_init(&out);
	int kept = 0;
	for (int k = 0; k < t->count; k++) {
		const ANSet* in = &t->sets[order[k]];
		const char* name = t->strings + in->name;
		if (kept && !strcmp(out.strings + sets[kept - 1].name, name)) continue;
		ANSet set = { addString(&out, name), addString(&out, AN_str(t, in->desc)), -1, in->flags };
		if (!set.name) break;
		sets[kept++] = set;
	}
	// parents, by their names in the old strings
	for (int k = 0, at = 0; k < t->count && at < kept; k++) {
		const ANSet* in = &t->sets[order[k]];
		if (strcmp(t->strings + in->name, out.strings + sets[at].name) != 0) continue;
		if (in->parent > 0) {
			const char* parent = AN_str(t, (uint32_t)in->parent);
			int lo = 0, hi = kept - 1;
			while (lo <= hi) {
				int mid = lo + (hi - lo) / 2;
				int c = strcmp(parent, out.strings + sets[mid].name);
				if (!c) {
					sets[at].parent = mid;
					break;
				}
				if (c < 0) hi = mid - 1;
				else lo = mid + 1;
			}
		}
		at++;
	}
	free(order);
	int full = t->full || out.full;
	free(t->strings);
	free(t->sets);
	t->strings = out.strings;
	t->strings_len = t->strings_cap = out.strings_len;
	ANSet* fitted = kept ? realloc(sets, sizeof(ANSet) * (size_t)kept) : NULL;
	if (!kept) free(sets);
	t->sets = fitted ? fitted : (kept ? sets : NULL);
	t->count = t->cap = kept;
	char* strings = t->strings_len ? realloc(t->strings, t->strings_len) : NULL;
	if (strings) t->strings = strings;
	t->finished = 1;
	t->full = full;
	return 0;
}

int AN_find(const ANTable* t, const char* name) {
	if (!t || !t->finished || !name) return -1;
	char key[AN_NAME_LEN];
	if (strlen(name) >= sizeof(key)) return -1;
	copyStr(key, sizeof(key), name);
	lower(key);
	int lo = 0, hi = t->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(key, t->strings + t->sets[mid].name);
		if (!c) return mid;
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return -1;
}

///////////////////////////////////////
// catver.ini

// 1 for a category to hide with the categories setting.
static int hiddenCategory(const char* category) {
	char low[AN_LINE_MAX];
	copyStr(low, sizeof(low), category);
	lower(low);
	return strstr(low, "mahjong") || strstr(low, "casino") || strstr(low, "quiz");
}

static int readCatver(ANTable* t, Reader* r) {
	if (!t->finished) return 0;
	char line[AN_LINE_MAX];
	size_t len = 0;
	int too_long = 0, in_category = 0, marked = 0, c;
	do {
		c = next(r);
		if (c >= 0 && c != '\n') {
			if (len + 1 < sizeof(line)) line[len++] = (char)c;
			else too_long = 1;
			continue;
		}
		line[len] = '\0';
		if (len && line[len - 1] == '\r') line[--len] = '\0';
		if (!too_long && len) {
			char* s = line;
			while (isspace((unsigned char)*s)) s++;
			if (*s == '[') {
				char* end = strchr(s, ']');
				if (end) *end = '\0';
				in_category = !strcasecmp(s + 1, "Category");
			}
			else if (in_category && *s != ';') {
				char* eq = strchr(s, '=');
				if (eq) {
					*eq = '\0';
					char* name_end = eq;
					while (name_end > s && isspace((unsigned char)name_end[-1])) *--name_end = '\0';
					int i = AN_find(t, s);
					if (i >= 0 && hiddenCategory(eq + 1) && !(t->sets[i].flags & AN_CATEGORY)) {
						t->sets[i].flags |= AN_CATEGORY;
						marked++;
					}
				}
			}
		}
		len = 0;
		too_long = 0;
	} while (c >= 0);
	return marked;
}

int AN_readCatver(ANTable* t, const char* path) {
	FILE* f = path ? fopen(path, "rb") : NULL;
	if (!f) return -1;
	Reader* r = readerFile(f);
	int marked = r ? readCatver(t, r) : 0;
	free(r);
	fclose(f);
	return marked;
}

int AN_readCatverText(ANTable* t, const char* text, size_t len) {
	if (!text) return -1;
	Reader* r = readerText(text, len);
	int marked = r ? readCatver(t, r) : 0;
	free(r);
	return marked;
}

///////////////////////////////////////
// the cache

#define CACHE_MAGIC "MOSAICAN"
#define CACHE_SIGNATURE_MAX 65536

typedef struct CacheHeader {
	char magic[8];
	uint32_t version;
	uint32_t signature_len;
	uint32_t count;
	uint32_t strings_len;
} CacheHeader;

int AN_save(const ANTable* t, const char* path, const char* signature) {
	if (!t->finished || !path || !signature || strlen(signature) > CACHE_SIGNATURE_MAX) return -1;
	char tmp[1100];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return -1;
	FILE* f = fopen(tmp, "wb");
	if (!f) return -1;
	CacheHeader h;
	memset(&h, 0, sizeof(h));
	memcpy(h.magic, CACHE_MAGIC, 8);
	h.version = AN_CACHE_VERSION;
	h.signature_len = (uint32_t)strlen(signature);
	h.count = (uint32_t)t->count;
	h.strings_len = (uint32_t)t->strings_len;
	int ok = fwrite(&h, sizeof(h), 1, f) == 1
		&& fwrite(signature, 1, h.signature_len, f) == h.signature_len
		&& (!t->count || fwrite(t->sets, sizeof(ANSet), (size_t)t->count, f) == (size_t)t->count)
		&& (!t->strings_len || fwrite(t->strings, 1, t->strings_len, f) == t->strings_len);
	if (fclose(f) != 0) ok = 0;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
		return -1;
	}
	return 0;
}

int AN_load(ANTable* t, const char* path, const char* signature) {
	AN_init(t);
	FILE* f = path && signature ? fopen(path, "rb") : NULL;
	if (!f) return -1;
	CacheHeader h;
	size_t sig_len = strlen(signature);
	char* sig = NULL;
	int ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, CACHE_MAGIC, 8) && h.version == AN_CACHE_VERSION
		&& h.signature_len == sig_len && sig_len <= CACHE_SIGNATURE_MAX && h.count <= AN_SETS_MAX
		&& h.strings_len <= AN_STRINGS_MAX && (h.count == 0 || h.strings_len > 1);
	if (ok) {
		sig = malloc(sig_len + 1);
		ok = sig && fread(sig, 1, sig_len, f) == sig_len && !memcmp(sig, signature, sig_len);
	}
	free(sig);
	if (ok && h.count) {
		t->sets = malloc(sizeof(ANSet) * h.count);
		ok = t->sets && fread(t->sets, sizeof(ANSet), h.count, f) == h.count;
	}
	if (ok && h.strings_len) {
		t->strings = malloc(h.strings_len);
		ok = t->strings && fread(t->strings, 1, h.strings_len, f) == h.strings_len;
	}
	// nothing after it
	if (ok) ok = fgetc(f) == EOF;
	fclose(f);
	t->count = t->cap = (int)h.count;
	t->strings_len = t->strings_cap = h.strings_len;
	// every string ends inside, every index is one, and the names are in order
	if (ok && h.strings_len) ok = t->strings[0] == '\0' && t->strings[h.strings_len - 1] == '\0';
	for (uint32_t i = 0; ok && i < h.count; i++) {
		const ANSet* s = &t->sets[i];
		ok = s->name > 0 && s->name < h.strings_len && s->desc < h.strings_len && s->parent >= -1 && s->parent < (int32_t)h.count
			&& s->parent != (int32_t)i && strlen(t->strings + s->name) < AN_NAME_LEN
			&& (i == 0 || strcmp(t->strings + t->sets[i - 1].name, t->strings + s->name) < 0);
	}
	if (!ok) {
		AN_free(t);
		return -1;
	}
	t->finished = 1;
	return 0;
}

///////////////////////////////////////
// names

void AN_stockName(const char* file_name, char* out, size_t size) {
	if (!size) return;
	char work[512];
	copyStr(work, sizeof(work), file_name ? file_name : "");
	char* tmp;
	while ((tmp = strrchr(work, '.')) != NULL) {
		size_t len = strlen(tmp);
		if (len > 2 && len <= 5) *tmp = '\0';
		else break;
	}
	char bare[512];
	copyStr(bare, sizeof(bare), work);
	while ((tmp = strrchr(work, '(')) != NULL || (tmp = strrchr(work, '[')) != NULL) {
		if (tmp == work) break;
		*tmp = '\0';
	}
	if (!work[0]) copyStr(work, sizeof(work), bare);
	size_t len = strlen(work);
	while (len > 1 && isspace((unsigned char)work[len - 1])) work[--len] = '\0';
	copyStr(out, size, work);
}

int AN_setOf(const char* file_name, char* out, size_t size) {
	if (!size) return 0;
	out[0] = '\0';
	if (!file_name || !file_name[0] || file_name[0] == '.') return 0;
	const char* dot = strrchr(file_name, '.');
	size_t len = dot ? (size_t)(dot - file_name) : strlen(file_name);
	if (!len || len >= size || len >= AN_NAME_LEN) return 0;
	memcpy(out, file_name, len);
	out[len] = '\0';
	lower(out);
	return 1;
}

// Where a description's parentheses or brackets start, after its first
// character, or NULL.
static const char* parens(const char* desc) {
	for (const char* p = desc + (desc[0] ? 1 : 0); *p; p++) {
		if (*p == '(' || *p == '[') return p;
	}
	return NULL;
}

static void trimEnd(char* s) {
	size_t len = strlen(s);
	while (len && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

void AN_cleanTitle(const char* desc, char* out, size_t size) {
	if (!size) return;
	const char* cut = parens(desc);
	size_t len = cut ? (size_t)(cut - desc) : strlen(desc);
	if (len >= size) len = size - 1;
	memcpy(out, desc, len);
	out[len] = '\0';
	trimEnd(out);
	if (!out[0]) copyStr(out, size, desc);
	cutUtf8(out, size);
}

// The first parentheses' contents, from desc, into out. 0 when there are none.
static int firstParens(const char* desc, char* out, size_t size) {
	out[0] = '\0';
	const char* open = strchr(desc + (desc[0] ? 1 : 0), '(');
	const char* close = open ? strchr(open, ')') : NULL;
	if (!close || close == open + 1) return 0;
	size_t len = (size_t)(close - open - 1);
	if (len >= size) len = size - 1;
	memcpy(out, open + 1, len);
	out[len] = '\0';
	return 1;
}

// A word that's a date or another number of 4 digits or more, such as
// 910522, 1998 or 2001-04-12.
static int dateWord(const char* word, size_t len) {
	size_t digits = 0;
	for (size_t i = 0; i < len; i++) {
		if (isdigit((unsigned char)word[i])) digits++;
		else if (word[i] != '-' && word[i] != '/' && word[i] != '.') return 0;
	}
	return digits >= 4;
}

// s without its date words, its spaces made single.
static void dropDates(const char* s, char* out, size_t size) {
	size_t len = 0;
	out[0] = '\0';
	const char* p = s;
	while (*p) {
		while (*p == ' ') p++;
		const char* word = p;
		while (*p && *p != ' ') p++;
		size_t n = (size_t)(p - word);
		int comma = n && word[n - 1] == ',';
		if (!n) continue;
		if (dateWord(word, n - (size_t)comma)) {
			// "US 920101, rev A": the comma stays with the word before
			if (comma && len) put(out, &len, size, ',');
			continue;
		}
		if (len) put(out, &len, size, ' ');
		for (size_t i = 0; i < n; i++) put(out, &len, size, word[i]);
	}
	cutUtf8(out, size);
}

void AN_region(const char* desc, char* out, size_t size) {
	char inside[AN_DESC_LEN];
	if (!size) return;
	out[0] = '\0';
	if (!firstParens(desc, inside, sizeof(inside))) return;
	char* comma = strchr(inside, ',');
	if (comma) *comma = '\0';
	dropDates(inside, out, size);
	cutUtf8(out, size);
}

int AN_hasTag(const char* list, const char* tag) {
	if (!list || !tag || !tag[0]) return 0;
	size_t tag_len = strlen(tag);
	const char* p = list;
	while (*p) {
		while (*p == ' ' || *p == ',') p++;
		const char* word = p;
		while (*p && *p != ',') p++;
		const char* end = p;
		while (end > word && end[-1] == ' ') end--;
		if ((size_t)(end - word) == tag_len && !strncasecmp(word, tag, tag_len)) return 1;
	}
	return 0;
}

///////////////////////////////////////
// a folder

typedef struct Row {
	char* file;
	int set;
	int hidden;
	char* name;
	int settled;
} Row;

static int compareRows(const void* a, const void* b) {
	return strcmp(((const Row*)a)->file, ((const Row*)b)->file);
}

static int compareInts(const void* a, const void* b) {
	int x = *(const int*)a, y = *(const int*)b;
	return (x > y) - (x < y);
}

// A name at a level of detail: 0 the clean title, 1 with its region, 2
// with its first parentheses without dates, 3 the full description.
static void nameAt(const char* desc, int level, char* out, size_t size) {
	char title[AN_DESC_LEN], extra[AN_DESC_LEN] = "";
	AN_cleanTitle(desc, title, sizeof(title));
	if (level == 1) AN_region(desc, extra, sizeof(extra));
	else if (level == 2) {
		char inside[AN_DESC_LEN];
		if (firstParens(desc, inside, sizeof(inside))) dropDates(inside, extra, sizeof(extra));
	}
	if (level >= 3 || (level > 0 && !extra[0])) copyStr(out, size, desc);
	else if (level == 0) copyStr(out, size, title);
	else if (snprintf(out, size, "%s (%s)", title, extra) >= (int)size) out[size - 1] = '\0';
	cutUtf8(out, size);
}

static const Row* naming_rows;

static int compareTitles(const void* a, const void* b) {
	const Row* x = &naming_rows[*(const int*)a];
	const Row* y = &naming_rows[*(const int*)b];
	return strcmp(x->name, y->name);
}

// Gives each shown row of a run sharing a clean title a name of its own,
// as little more detailed as makes it one.
static int separate(const ANTable* t, Row* rows, const int* run, int n) {
	for (int level = 1; level <= 3; level++) {
		char (*names)[AN_DESC_LEN] = malloc(sizeof(*names) * (size_t)n);
		if (!names) return -1;
		for (int k = 0; k < n; k++) {
			if (!rows[run[k]].settled) nameAt(AN_str(t, t->sets[rows[run[k]].set].desc), level, names[k], AN_DESC_LEN);
		}
		for (int k = 0; k < n; k++) {
			Row* row = &rows[run[k]];
			if (row->settled) continue;
			int shared = 0;
			for (int j = 0; j < n && !shared; j++) {
				const Row* other = &rows[run[j]];
				shared = j != k && !other->settled && other->set != row->set && !strcmp(names[j], names[k]);
			}
			if (shared && level < 3) continue;
			char* name = strdup(names[k]);
			if (!name) {
				free(names);
				return -1;
			}
			free(row->name);
			row->name = name;
			row->settled = 1;
		}
		free(names);
	}
	return 0;
}

int AN_folder(const ANTable* t, const ANOptions* options, const char* const* files, int count, ANFolder* out) {
	memset(out, 0, sizeof(*out));
	if (!t || !t->finished || !t->count || !files || count <= 0) return 0;
	Row* rows = calloc((size_t)count, sizeof(Row));
	int* present = malloc(sizeof(int) * (size_t)count);
	if (!rows || !present) {
		free(rows);
		free(present);
		return -1;
	}
	int n = 0;
	for (int i = 0; i < count; i++) {
		char set[AN_NAME_LEN];
		if (!files[i] || !AN_setOf(files[i], set, sizeof(set))) continue;
		int s = AN_find(t, set);
		if (s < 0) continue;
		rows[n].file = strdup(files[i]);
		if (!rows[n].file) break;
		rows[n].set = s;
		present[n] = s;
		n++;
	}
	qsort(present, (size_t)n, sizeof(int), compareInts);

	// what hides a set on its own, without looking at the folder
	uint32_t hide = AN_BIOS | AN_DEVICE;
	if (options && options->not_working) hide |= AN_NOT_WORKING;
	if (options && options->categories) hide |= AN_CATEGORY;
	int failed = 0;
	for (int i = 0; i < n; i++) {
		const ANSet* s = &t->sets[rows[i].set];
		rows[i].hidden = (s->flags & hide) != 0;
		if (!rows[i].hidden && options && options->clones && s->parent >= 0 && !(t->sets[s->parent].flags & hide)) {
			rows[i].hidden = bsearch(&s->parent, present, (size_t)n, sizeof(int), compareInts) != NULL;
		}
		if (rows[i].hidden) {
			rows[i].settled = 1;
			continue;
		}
		const char* desc = AN_str(t, s->desc);
		if (!desc[0]) {
			rows[i].settled = 1;
			continue;
		}
		char name[AN_DESC_LEN];
		nameAt(desc, options && options->full ? 3 : 0, name, sizeof(name));
		rows[i].name = strdup(name);
		if (!rows[i].name) failed = 1;
		rows[i].settled = options && options->full;
	}

	// clean titles two shown sets would share
	int* order = malloc(sizeof(int) * (size_t)(n ? n : 1));
	int shown = 0;
	if (!order) failed = 1;
	for (int i = 0; !failed && i < n; i++) {
		if (!rows[i].settled && rows[i].name) order[shown++] = i;
	}
	naming_rows = rows;
	if (!failed && shown > 1) qsort(order, (size_t)shown, sizeof(int), compareTitles);
	for (int i = 0; !failed && i < shown;) {
		int j = i + 1;
		while (j < shown && !strcmp(rows[order[j]].name, rows[order[i]].name)) j++;
		int distinct = 0;
		for (int k = i + 1; k < j && !distinct; k++) distinct = rows[order[k]].set != rows[order[i]].set;
		if (distinct && separate(t, rows, order + i, j - i) != 0) failed = 1;
		i = j;
	}
	naming_rows = NULL;
	free(order);
	free(present);

	qsort(rows, (size_t)n, sizeof(Row), compareRows);
	out->files = malloc(sizeof(char*) * (size_t)(n ? n : 1));
	out->names = malloc(sizeof(char*) * (size_t)(n ? n : 1));
	out->hidden = malloc((size_t)(n ? n : 1));
	if (!out->files || !out->names || !out->hidden) failed = 1;
	for (int i = 0; i < n; i++) {
		if (failed) {
			free(rows[i].file);
			free(rows[i].name);
			continue;
		}
		out->files[i] = rows[i].file;
		out->names[i] = rows[i].hidden ? NULL : rows[i].name;
		if (rows[i].hidden) free(rows[i].name);
		out->hidden[i] = (unsigned char)rows[i].hidden;
	}
	free(rows);
	if (failed) {
		free(out->files);
		free(out->names);
		free(out->hidden);
		memset(out, 0, sizeof(*out));
		return -1;
	}
	out->count = n;
	return 0;
}

int AN_folderFind(const ANFolder* f, const char* file_name) {
	if (!f || !file_name) return -1;
	int lo = 0, hi = f->count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int c = strcmp(file_name, f->files[mid]);
		if (!c) return mid;
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return -1;
}

void AN_folderFree(ANFolder* f) {
	for (int i = 0; i < f->count; i++) {
		free(f->files[i]);
		free(f->names[i]);
	}
	free(f->files);
	free(f->names);
	free(f->hidden);
	memset(f, 0, sizeof(*f));
}
