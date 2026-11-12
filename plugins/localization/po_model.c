#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "po_model.h"

///////////////////////////////////////
// plural expressions

#define PLURAL_DEPTH 64

typedef struct Expr {
	const char* p;
	PoPlural* out;
	int depth;
} Expr;

static void skipSpace(Expr* e) {
	while (*e->p == ' ' || *e->p == '\t') e->p++;
}

// Consumes the operator op (one or two characters) when it's next.
static int accept(Expr* e, const char* op) {
	skipSpace(e);
	size_t len = strlen(op);
	if (strncmp(e->p, op, len) != 0) return 0;
	// "<" isn't the start of "<=", and so on
	if (len == 1 && (op[0] == '<' || op[0] == '>' || op[0] == '!') && e->p[1] == '=') return 0;
	e->p += len;
	return 1;
}

// A new node; a, b and c are -1 only for operands the op doesn't take, so
// an operand that failed to parse (also -1) is caught by the caller.
static int node(Expr* e, char op, int a, int b, int c, unsigned long value) {
	PoPlural* r = e->out;
	if (r->count >= PO_PLURAL_NODES) return -1;
	PoPluralNode* x = &r->nodes[r->count];
	x->op = op;
	x->a = a;
	x->b = b;
	x->c = c;
	x->value = value;
	return r->count++;
}

static int ternary(Expr* e);

static int primary(Expr* e) {
	skipSpace(e);
	char c = *e->p;
	if (c == 'n') {
		char next = e->p[1];
		if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9') || next == '_') return -1;
		e->p++;
		return node(e, 'n', 0, -1, -1, 0);
	}
	if (c >= '0' && c <= '9') {
		unsigned long v = 0;
		while (*e->p >= '0' && *e->p <= '9') {
			unsigned long d = (unsigned long)(*e->p - '0');
			if (v > (~0UL - d) / 10) return -1;
			v = v * 10 + d;
			e->p++;
		}
		return node(e, '#', 0, -1, -1, v);
	}
	if (c == '(') {
		e->p++;
		int inner = ternary(e);
		skipSpace(e);
		if (inner < 0 || *e->p != ')') return -1;
		e->p++;
		return inner;
	}
	return -1;
}

static int unary(Expr* e) {
	if (++e->depth > PLURAL_DEPTH) return -1;
	int r;
	if (accept(e, "!")) {
		int a = unary(e);
		r = a < 0 ? -1 : node(e, '!', a, -1, -1, 0);
	}
	else r = primary(e);
	e->depth--;
	return r;
}

// One level of left-associative binary operators: ops lists each operator's
// text and the node op it makes, and next parses the level below.
typedef struct BinOp {
	const char* text;
	char op;
} BinOp;

static int binary(Expr* e, const BinOp* ops, int count, int (*next)(Expr*)) {
	int left = next(e);
	for (;;) {
		if (left < 0) return -1;
		int matched = 0;
		for (int i = 0; i < count; i++) {
			if (!accept(e, ops[i].text)) continue;
			int right = next(e);
			left = right < 0 ? -1 : node(e, ops[i].op, left, right, -1, 0);
			matched = 1;
			break;
		}
		if (!matched) return left;
	}
}

static int mul(Expr* e) {
	static const BinOp ops[] = { { "*", '*' }, { "/", '/' }, { "%", '%' } };
	return binary(e, ops, 3, unary);
}
static int add(Expr* e) {
	static const BinOp ops[] = { { "+", '+' }, { "-", '-' } };
	return binary(e, ops, 2, mul);
}
static int rel(Expr* e) {
	static const BinOp ops[] = { { "<=", 'l' }, { ">=", 'g' }, { "<", '<' }, { ">", '>' } };
	return binary(e, ops, 4, add);
}
static int eq(Expr* e) {
	static const BinOp ops[] = { { "==", '=' }, { "!=", 'x' } };
	return binary(e, ops, 2, rel);
}
static int and(Expr* e) {
	static const BinOp ops[] = { { "&&", '&' } };
	return binary(e, ops, 1, eq);
}
static int or(Expr* e) {
	static const BinOp ops[] = { { "||", '|' } };
	return binary(e, ops, 1, and);
}

static int ternary(Expr* e) {
	if (++e->depth > PLURAL_DEPTH) return -1;
	int cond = or(e);
	int r = cond;
	if (cond >= 0 && accept(e, "?")) {
		int yes = ternary(e);
		int no = yes >= 0 && accept(e, ":") ? ternary(e) : -1;
		r = no < 0 ? -1 : node(e, '?', cond, yes, no, 0);
	}
	e->depth--;
	return r;
}

int PoPlural_compile(const char* expr, PoPlural* out) {
	if (!expr || !out) return -1;
	memset(out, 0, sizeof(*out));
	Expr e = { expr, out, 0 };
	int root = ternary(&e);
	skipSpace(&e);
	if (root < 0 || (*e.p && *e.p != ';')) {
		out->count = 0;
		return -1;
	}
	out->root = root;
	return 0;
}

static int evalNode(const PoPlural* r, int i, unsigned long n, unsigned long* out) {
	const PoPluralNode* x = &r->nodes[i];
	unsigned long a, b;
	switch (x->op) {
		case 'n': *out = n; return 0;
		case '#': *out = x->value; return 0;
		case '!':
			if (evalNode(r, x->a, n, &a) < 0) return -1;
			*out = !a;
			return 0;
		case '?':
			if (evalNode(r, x->a, n, &a) < 0) return -1;
			return evalNode(r, a ? x->b : x->c, n, out);
		case '&':
			if (evalNode(r, x->a, n, &a) < 0) return -1;
			if (!a) { *out = 0; return 0; }
			if (evalNode(r, x->b, n, &b) < 0) return -1;
			*out = b != 0;
			return 0;
		case '|':
			if (evalNode(r, x->a, n, &a) < 0) return -1;
			if (a) { *out = 1; return 0; }
			if (evalNode(r, x->b, n, &b) < 0) return -1;
			*out = b != 0;
			return 0;
	}
	if (evalNode(r, x->a, n, &a) < 0 || evalNode(r, x->b, n, &b) < 0) return -1;
	switch (x->op) {
		case '*': *out = a * b; return 0;
		case '/': if (!b) return -1; *out = a / b; return 0;
		case '%': if (!b) return -1; *out = a % b; return 0;
		case '+': *out = a + b; return 0;
		case '-': *out = a - b; return 0;
		case '<': *out = a < b; return 0;
		case '>': *out = a > b; return 0;
		case 'l': *out = a <= b; return 0;
		case 'g': *out = a >= b; return 0;
		case '=': *out = a == b; return 0;
		case 'x': *out = a != b; return 0;
	}
	return -1;
}

int PoPlural_eval(const PoPlural* rule, unsigned long n, unsigned long* out) {
	if (!rule || !out || rule->count <= 0) return -1;
	return evalNode(rule, rule->root, n, out);
}

static int lower(int c) {
	return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

// The value of a header's "key:" line, up to its end, into out; 0, or -1
// when there's none.
static int headerField(const char* header, const char* key, char* out, size_t size) {
	size_t key_len = strlen(key);
	for (const char* line = header; line && *line; ) {
		const char* end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) : strlen(line);
		size_t i = 0;
		while (i < key_len && i < len && lower((unsigned char)line[i]) == lower((unsigned char)key[i])) i++;
		if (i == key_len && i < len && line[i] == ':') {
			const char* v = line + i + 1;
			size_t vlen = len - i - 1;
			while (vlen && (*v == ' ' || *v == '\t')) { v++; vlen--; }
			if (vlen >= size) return -1;
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			return 0;
		}
		line = end ? end + 1 : NULL;
	}
	return -1;
}

int Po_pluralForms(const char* header, int* nplurals, PoPlural* rule, char* why, size_t why_size) {
	char line[512];
	if (why && why_size) why[0] = '\0';
#define WHY(text) do { if (why && why_size) snprintf(why, why_size, "%s", text); return -1; } while (0)
	if (!header || !nplurals || !rule) WHY("there's no header");
	if (headerField(header, "Plural-Forms", line, sizeof(line)) < 0) WHY("it has no Plural-Forms");
	int count = -1, have_rule = 0;
	for (const char* p = line; *p; ) {
		while (*p == ' ' || *p == '\t' || *p == ';') p++;
		if (!*p) break;
		if (!strncmp(p, "nplurals", 8)) {
			p += 8;
			while (*p == ' ' || *p == '\t') p++;
			if (*p++ != '=') WHY("its nplurals has no value");
			while (*p == ' ' || *p == '\t') p++;
			if (*p < '0' || *p > '9') WHY("its nplurals isn't a number");
			count = 0;
			while (*p >= '0' && *p <= '9') {
				if (count > PO_MAX_PLURALS) break;
				count = count * 10 + (*p++ - '0');
			}
			while (*p >= '0' && *p <= '9') p++;
			if (count < 1 || count > PO_MAX_PLURALS) WHY("its nplurals isn't from 1 to 10");
		}
		else if (!strncmp(p, "plural", 6) && (p[6] == ' ' || p[6] == '\t' || p[6] == '=')) {
			p += 6;
			while (*p == ' ' || *p == '\t') p++;
			if (*p++ != '=') WHY("its plural has no value");
			if (PoPlural_compile(p, rule) < 0) WHY("its plural expression isn't one gettext allows");
			have_rule = 1;
			while (*p && *p != ';') p++;
		}
		else WHY("it has something other than nplurals and plural");
	}
	if (count < 0) WHY("it has no nplurals");
	if (!have_rule) WHY("it has no plural expression");
#undef WHY
	*nplurals = count;
	return 0;
}

///////////////////////////////////////
// UTF-8

int Po_utf8(const char* text, size_t len) {
	const unsigned char* s = (const unsigned char*)text;
	const unsigned char* end = s + len;
	while (s < end) {
		unsigned c = *s;
		int more;
		unsigned cp;
		if (c < 0x80) {
			s++;
			continue;
		}
		else if (c >= 0xC2 && c <= 0xDF) { more = 1; cp = c & 0x1F; }
		else if (c >= 0xE0 && c <= 0xEF) { more = 2; cp = c & 0x0F; }
		else if (c >= 0xF0 && c <= 0xF4) { more = 3; cp = c & 0x07; }
		else return 0;
		s++;
		if (end - s < more) return 0;
		for (int i = 0; i < more; i++, s++) {
			if ((*s & 0xC0) != 0x80) return 0;
			cp = (cp << 6) | (*s & 0x3F);
		}
		if (more == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
		if (more == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
	}
	return 1;
}

///////////////////////////////////////
// the table

static unsigned long hashKey(const char* context, const char* id) {
	unsigned long h = 2166136261u;
	if (context) {
		for (const unsigned char* p = (const unsigned char*)context; *p; p++) h = (h ^ *p) * 16777619u;
		h = (h ^ 0x04) * 16777619u;
	}
	else h = (h ^ 0x01) * 16777619u;
	for (const unsigned char* p = (const unsigned char*)id; *p; p++) h = (h ^ *p) * 16777619u;
	return h;
}

static int sameContext(const char* a, const char* b) {
	if (!a || !b) return a == b;
	return !strcmp(a, b);
}

// The slot holding context and id, or the empty one where it would go.
static int slotFor(const PoCatalog* c, const char* context, const char* id) {
	unsigned long i = hashKey(context, id) % (unsigned long)c->slot_count;
	for (;;) {
		int at = c->slots[i];
		if (!at) return (int)i;
		const PoEntry* e = &c->entries[at - 1];
		if (sameContext(e->context, context) && !strcmp(e->id, id)) return (int)i;
		i = (i + 1) % (unsigned long)c->slot_count;
	}
}

static int grow(PoCatalog* c) {
	int slots = c->slot_count ? c->slot_count * 2 : 64;
	int* fresh = calloc((size_t)slots, sizeof(int));
	if (!fresh) return -1;
	int* old = c->slots;
	int old_count = c->slot_count;
	c->slots = fresh;
	c->slot_count = slots;
	for (int i = 0; i < old_count; i++) {
		if (!old[i]) continue;
		const PoEntry* e = &c->entries[old[i] - 1];
		c->slots[slotFor(c, e->context, e->id)] = old[i];
	}
	free(old);
	return 0;
}

static const PoEntry* findEntry(const PoCatalog* c, const char* context, const char* id) {
	if (!c || !id || !c->slot_count) return NULL;
	int at = c->slots[slotFor(c, context, id)];
	return at ? &c->entries[at - 1] : NULL;
}

static void freeEntry(PoEntry* e) {
	free(e->context);
	free(e->id);
	free(e->plural);
	for (int i = 0; i < PO_MAX_PLURALS; i++) free(e->str[i]);
	memset(e, 0, sizeof(*e));
}

void Po_free(PoCatalog* c) {
	if (!c) return;
	for (int i = 0; i < c->count; i++) freeEntry(&c->entries[i]);
	free(c->entries);
	free(c->slots);
	free(c);
}

///////////////////////////////////////
// reading a file

typedef struct Buf {
	char* data;
	size_t len, cap;
	int set; // its keyword was seen
} Buf;

static int bufAdd(Buf* b, char c) {
	if (b->len >= PO_MAX_STRING) return -1;
	if (b->len + 1 >= b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 64;
		char* data = realloc(b->data, cap);
		if (!data) return -1;
		b->data = data;
		b->cap = cap;
	}
	b->data[b->len++] = c;
	b->data[b->len] = '\0';
	return 0;
}

// Hands over b's text (an empty string for none), leaving b empty.
static char* bufTake(Buf* b) {
	char* s = b->data ? b->data : strdup("");
	memset(b, 0, sizeof(*b));
	return s;
}

static void bufFree(Buf* b) {
	free(b->data);
	memset(b, 0, sizeof(*b));
}

typedef struct Pending {
	Buf context, id, plural;
	Buf str[PO_MAX_PLURALS];
	int strs;
	int fuzzy;
	int line;
} Pending;

typedef struct Reader {
	PoCatalog* c;
	Pending e;
	Buf* current; // the string continuation lines add to
	int line;
	int fuzzy_next;
	int header_seen;
	int header_line;
	char* header;
	char* err;
	size_t err_size;
} Reader;

static int fail(Reader* r, int line, const char* format, ...) {
	if (r->err && r->err_size) {
		char why[200];
		va_list args;
		va_start(args, format);
		vsnprintf(why, sizeof(why), format, args);
		va_end(args);
		snprintf(r->err, r->err_size, "line %i: %s", line, why);
	}
	return -1;
}

static int started(const Pending* e) {
	return e->context.set || e->id.set;
}

static void pendingFree(Pending* e) {
	bufFree(&e->context);
	bufFree(&e->id);
	bufFree(&e->plural);
	for (int i = 0; i < PO_MAX_PLURALS; i++) bufFree(&e->str[i]);
	memset(e, 0, sizeof(*e));
}

// Files the entry read so far, if there is one.
static int finish(Reader* r) {
	Pending* e = &r->e;
	r->current = NULL;
	if (!started(e)) return 0;
	int line = e->line;
	if (!e->id.set) return fail(r, line, "msgctxt with no msgid");
	if (!e->strs) return fail(r, line, "msgid with no msgstr");
	if (e->plural.set && e->strs < 1) return fail(r, line, "msgid_plural with no msgstr[0]");

	// the header
	if (!e->context.set && !e->id.len) {
		if (r->header_seen) return fail(r, line, "a second header");
		r->header_seen = 1;
		r->header_line = line;
		r->header = bufTake(&e->str[0]);
		pendingFree(e);
		return 0;
	}
	int keep = !e->fuzzy && e->id.len;
	int translated = 0;
	for (int i = 0; i < e->strs; i++) {
		if (e->str[i].len) translated = 1;
	}
	if (!keep) {
		pendingFree(e);
		return 0;
	}

	int ok = Po_utf8(e->id.data ? e->id.data : "", e->id.len) && Po_utf8(e->context.data ? e->context.data : "", e->context.len)
		&& Po_utf8(e->plural.data ? e->plural.data : "", e->plural.len);
	for (int i = 0; ok && i < e->strs; i++) ok = Po_utf8(e->str[i].data ? e->str[i].data : "", e->str[i].len);
	if (!ok) return fail(r, line, "an escape makes text that isn't UTF-8");

	PoCatalog* c = r->c;
	const char* context = e->context.set ? (e->context.data ? e->context.data : "") : NULL;
	const char* id = e->id.data;
	if (findEntry(c, context, id)) return fail(r, line, "\"%s\" is in the file twice", id);
	if (!translated) {
		pendingFree(e);
		return 0;
	}
	if ((c->count + 1) * 2 > c->slot_count && grow(c) < 0) return fail(r, line, "out of memory");
	if (!(c->count & (c->count - 1)) || !c->entries) {
		size_t cap = c->count ? (size_t)c->count * 2 : 16;
		PoEntry* entries = realloc(c->entries, cap * sizeof(PoEntry));
		if (!entries) return fail(r, line, "out of memory");
		c->entries = entries;
	}
	PoEntry* out = &c->entries[c->count];
	memset(out, 0, sizeof(*out));
	out->context = e->context.set ? bufTake(&e->context) : NULL;
	out->id = bufTake(&e->id);
	out->plural = e->plural.set ? bufTake(&e->plural) : NULL;
	out->strs = e->strs;
	for (int i = 0; i < e->strs; i++) {
		// an empty form is untranslated: the English shows for it
		if (e->str[i].len) out->str[i] = bufTake(&e->str[i]);
	}
	if (!out->id || (e->context.set && !out->context)) {
		freeEntry(out);
		return fail(r, line, "out of memory");
	}
	c->count++;
	c->slots[slotFor(c, out->context, out->id)] = c->count;
	pendingFree(e);
	return 0;
}

static int hexDigit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Reads the quoted string at p (its opening quote) to the end of the line
// into b.
static int readString(Reader* r, const char* p, const char* end, Buf* b) {
	if (p >= end || *p != '"') return fail(r, r->line, "expected a quoted string");
	p++;
	for (;;) {
		if (p >= end) return fail(r, r->line, "a string with no closing quote");
		char c = *p++;
		if (c == '"') break;
		if (c != '\\') {
			if (bufAdd(b, c) < 0) return fail(r, r->line, "a string over %i bytes", PO_MAX_STRING);
			continue;
		}
		if (p >= end) return fail(r, r->line, "a string with no closing quote");
		c = *p++;
		int v;
		switch (c) {
			case 'n': v = '\n'; break;
			case 't': v = '\t'; break;
			case 'r': v = '\r'; break;
			case 'a': v = '\a'; break;
			case 'b': v = '\b'; break;
			case 'f': v = '\f'; break;
			case 'v': v = '\v'; break;
			case '\\': case '"': case '\'': case '?': v = c; break;
			case 'x': {
				int d = p < end ? hexDigit(*p) : -1;
				if (d < 0) return fail(r, r->line, "\\x with no hex digits");
				v = 0;
				for (int i = 0; i < 2 && p < end && (d = hexDigit(*p)) >= 0; i++, p++) v = v * 16 + d;
				break;
			}
			default:
				if (c < '0' || c > '7') return fail(r, r->line, "an unknown escape \\%c", c);
				v = c - '0';
				for (int i = 0; i < 2 && p < end && *p >= '0' && *p <= '7'; i++, p++) v = v * 8 + (*p - '0');
				if (v > 0xFF) return fail(r, r->line, "an octal escape past \\377");
		}
		if (!v) return fail(r, r->line, "an escape for a NUL");
		if (bufAdd(b, (char)v) < 0) return fail(r, r->line, "a string over %i bytes", PO_MAX_STRING);
	}
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p < end) return fail(r, r->line, "text after a string's closing quote");
	b->set = 1;
	return 0;
}

static int keyword(Reader* r, const char* p, const char* end) {
	const char* word = p;
	while (p < end && ((*p >= 'a' && *p <= 'z') || *p == '_')) p++;
	size_t len = (size_t)(p - word);
	int index = -1;
	if (p < end && *p == '[') {
		p++;
		if (p >= end || *p < '0' || *p > '9') return fail(r, r->line, "a msgstr[] with no index");
		index = 0;
		while (p < end && *p >= '0' && *p <= '9') {
			index = index * 10 + (*p++ - '0');
			if (index > 1000) break;
		}
		if (p >= end || *p != ']') return fail(r, r->line, "a msgstr[] with no closing bracket");
		p++;
	}
	if (p >= end || (*p != ' ' && *p != '\t' && *p != '"')) return fail(r, r->line, "expected a keyword and a string");
	while (p < end && (*p == ' ' || *p == '\t')) p++;

	Pending* e = &r->e;
#define IS(name) (len == strlen(name) && !strncmp(word, name, len))
	if (index >= 0 && !IS("msgstr")) return fail(r, r->line, "an index on something other than msgstr");
	Buf* into;
	if (IS("msgctxt") || (IS("msgid") && !e->context.set) || (IS("msgid") && e->strs)) {
		if (e->strs && finish(r) < 0) return -1;
		if (started(e)) return fail(r, r->line, IS("msgctxt") ? "msgctxt in the middle of an entry" : "msgid with no msgstr before it");
		e->line = r->line;
		e->fuzzy = r->fuzzy_next;
		r->fuzzy_next = 0;
		into = IS("msgctxt") ? &e->context : &e->id;
	}
	else if (IS("msgid")) {
		if (e->id.set) return fail(r, r->line, "a second msgid");
		into = &e->id;
	}
	else if (IS("msgid_plural")) {
		if (!e->id.set || e->plural.set || e->strs) return fail(r, r->line, "msgid_plural not right after a msgid");
		into = &e->plural;
	}
	else if (IS("msgstr")) {
		if (!e->id.set) return fail(r, r->line, "msgstr with no msgid");
		if (e->plural.set && index < 0) return fail(r, r->line, "msgstr with no index after a msgid_plural");
		if (!e->plural.set && index >= 0) return fail(r, r->line, "msgstr[%i] with no msgid_plural", index);
		if (index < 0) {
			if (e->strs) return fail(r, r->line, "a second msgstr");
			index = 0;
		}
		else if (index != e->strs) return fail(r, r->line, "msgstr[%i] out of order", index);
		if (index >= PO_MAX_PLURALS) return fail(r, r->line, "more than %i plural forms", PO_MAX_PLURALS);
		into = &e->str[index];
		e->strs = index + 1;
	}
	else return fail(r, r->line, "an unknown keyword \"%.*s\"", (int)(len > 20 ? 20 : len), word);
#undef IS
	r->current = into;
	return readString(r, p, end, into);
}

// The header's charset is UTF-8, or one UTF-8 covers, or a template's
// placeholder.
static int utf8Charset(const char* header, char* name, size_t size) {
	char type[256];
	name[0] = '\0';
	if (headerField(header, "Content-Type", type, sizeof(type)) < 0) return 1;
	const char* cs = NULL;
	for (const char* p = type; *p; p++) {
		if (!strncmp(p, "charset=", 8)) {
			cs = p + 8;
			break;
		}
	}
	if (!cs) return 1;
	size_t len = 0;
	while (cs[len] && cs[len] != ';' && cs[len] != ' ' && len + 1 < size) {
		name[len] = (char)lower((unsigned char)cs[len]);
		len++;
	}
	name[len] = '\0';
	static const char* const ok[] = { "utf-8", "utf8", "charset", "ascii", "us-ascii" };
	for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
		if (!strcmp(name, ok[i])) return 1;
	}
	return 0;
}

PoCatalog* Po_parse(const char* text, size_t len, char* err, size_t err_size) {
	if (err && err_size) err[0] = '\0';
	Reader r;
	memset(&r, 0, sizeof(r));
	r.err = err;
	r.err_size = err_size;
	if (!text) {
		fail(&r, 0, "no text");
		return NULL;
	}
	if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) {
		text += 3;
		len -= 3;
	}
	if (memchr(text, '\0', len)) {
		fail(&r, 0, "a NUL byte in the file");
		return NULL;
	}
	if (!Po_utf8(text, len)) {
		// find the line, for the message
		int line = 1;
		size_t start = 0;
		for (size_t i = 0; i <= len; i++) {
			if (i == len || text[i] == '\n') {
				if (!Po_utf8(text + start, i - start)) break;
				line++;
				start = i + 1;
			}
		}
		fail(&r, line, "text that isn't UTF-8");
		return NULL;
	}
	r.c = calloc(1, sizeof(PoCatalog));
	if (!r.c) {
		fail(&r, 0, "out of memory");
		return NULL;
	}
	int bad = 0;
	const char* end = text + len;
	for (const char* p = text; p < end && !bad; ) {
		const char* eol = memchr(p, '\n', (size_t)(end - p));
		const char* line_end = eol ? eol : end;
		const char* next = eol ? eol + 1 : end;
		r.line++;
		if (line_end > p && line_end[-1] == '\r') line_end--;
		while (p < line_end && (*p == ' ' || *p == '\t')) p++;
		if (p == line_end) r.current = NULL;
		else if (*p == '#') {
			r.current = NULL;
			if (p + 1 < line_end && p[1] == ',') {
				// flags, comma-separated, for the entry that follows
				for (const char* f = p + 2; f < line_end; ) {
					while (f < line_end && (*f == ' ' || *f == '\t' || *f == ',')) f++;
					const char* w = f;
					while (f < line_end && *f != ',' && *f != ' ' && *f != '\t') f++;
					if (f - w == 5 && !strncmp(w, "fuzzy", 5)) {
						if (started(&r.e) && !r.e.strs) r.e.fuzzy = 1;
						else r.fuzzy_next = 1;
					}
				}
			}
		}
		else if (*p == '"') {
			if (!r.current) bad = fail(&r, r.line, "a string with no keyword before it");
			else bad = readString(&r, p, line_end, r.current) < 0;
		}
		else bad = keyword(&r, p, line_end) < 0;
		p = next;
	}
	if (!bad) bad = finish(&r) < 0;
	if (!bad && r.header) {
		char name[64];
		if (!Po_utf8(r.header, strlen(r.header))) bad = fail(&r, r.header_line, "a header that isn't UTF-8");
		else if (!utf8Charset(r.header, name, sizeof(name))) bad = fail(&r, r.header_line, "the charset is %s, not UTF-8", name);
	}
	if (bad) {
		pendingFree(&r.e);
		free(r.header);
		Po_free(r.c);
		return NULL;
	}
	PoCatalog* c = r.c;
	char why[120];
	if (Po_pluralForms(r.header, &c->nplurals, &c->rule, why, sizeof(why)) < 0) {
		c->english_rule = 1;
		c->nplurals = 2;
		snprintf(c->note, sizeof(c->note), "%s, so plurals use English's rule", why);
	}
	free(r.header);
	return c;
}

///////////////////////////////////////
// lookups

const char* Po_find(const PoCatalog* catalog, const char* context, const char* msgid) {
	const PoEntry* e = findEntry(catalog, context, msgid);
	return e ? e->str[0] : NULL;
}

const char* Po_findPlural(const PoCatalog* catalog, const char* context, const char* msgid, unsigned long n) {
	const PoEntry* e = findEntry(catalog, context, msgid);
	if (!e) return NULL;
	if (!e->plural) return e->str[0];
	unsigned long index = n != 1;
	if (!catalog->english_rule) {
		unsigned long picked;
		if (PoPlural_eval(&catalog->rule, n, &picked) == 0 && picked < (unsigned long)catalog->nplurals) index = picked;
	}
	return index < (unsigned long)e->strs ? e->str[index] : NULL;
}
