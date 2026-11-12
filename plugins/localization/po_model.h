#ifndef LOCALIZATION_PO_MODEL_H
#define LOCALIZATION_PO_MODEL_H

#include <stddef.h>

// The SDL-free half of localization: a reader for gettext .po files and
// the Plural-Forms expressions in their headers. No .mo, no libintl.
//
// A file is read whole or not at all, as msgfmt reads one: a syntax error,
// a bad escape, a duplicate entry, text that isn't UTF-8 or a charset other
// than UTF-8 refuses it, saying which line. Fuzzy and obsolete (#~) entries
// are skipped, as are untranslated ones. The header (msgid "") is read for
// its Plural-Forms even when it's fuzzy, as a .pot's is.

#define PO_MAX_PLURALS 10   // msgstr[0] to msgstr[9]
#define PO_MAX_STRING 65536 // a msgid's or msgstr's longest, in bytes
#define PO_PLURAL_NODES 128 // the most a plural expression can hold

///////////////////////////////////////
// plural expressions

// A compiled plural expression: the subset of C gettext uses (n, unsigned
// decimal numbers, parentheses, !, * / %, + -, < > <= >=, == !=, &&, ||
// and ?:). Nothing else is ever evaluated.
typedef struct PoPluralNode {
	char op;           // 'n', '#' for a number, '!', '?', or a binary operator ('<' '>' 'l' for <=, 'g' for >=, '=' '!' for !=, '&', '|')
	int a, b, c;       // operands, as node indexes
	unsigned long value; // a number's
} PoPluralNode;

typedef struct PoPlural {
	PoPluralNode nodes[PO_PLURAL_NODES];
	int count;
	int root;
} PoPlural;

// Compiles expr, which ends at its NUL or at a ';'. Returns 0, or -1 for one
// that isn't in the subset or is too big.
int PoPlural_compile(const char* expr, PoPlural* out);

// Evaluates rule for n into out. Returns 0, or -1 for a division by zero.
int PoPlural_eval(const PoPlural* rule, unsigned long n, unsigned long* out);

// Reads "nplurals=N; plural=EXPR;" from a header's Plural-Forms line.
// Returns 0, or -1 when there's no such line or it's bad (nplurals outside
// 1 to PO_MAX_PLURALS, or an expression that doesn't compile), saying why
// in why.
int Po_pluralForms(const char* header, int* nplurals, PoPlural* rule, char* why, size_t why_size);

///////////////////////////////////////
// catalogs

typedef struct PoEntry {
	char* context; // msgctxt, or NULL for none
	char* id;      // msgid
	char* plural;  // msgid_plural, or NULL for a string with no plural
	char* str[PO_MAX_PLURALS];
	int strs;      // how many msgstr there are; 1 for a string with no plural
} PoEntry;

typedef struct PoCatalog {
	PoEntry* entries;
	int count;
	int* slots; // indexes into entries + 1, 0 for empty
	int slot_count;
	int nplurals;
	PoPlural rule;
	int english_rule; // no usable Plural-Forms, so plurals use n != 1
	char note[160];   // why the header's Plural-Forms wasn't used, or ""
} PoCatalog;

// Reads the .po in text (len bytes; a NUL inside is an error). Returns a
// catalog to free with Po_free, or NULL with the line and reason in err.
PoCatalog* Po_parse(const char* text, size_t len, char* err, size_t err_size);
void Po_free(PoCatalog* catalog);

// The msgctxt a pack keeps a string under, for the strings slot's
// translate: the launcher's own (plugin_id NULL) are in their context, or
// none, and a plugin's in its own context when it gives one, else under
// its id.
const char* Po_context(const char* plugin_id, const char* context);

// The translation of msgid in context (NULL for none), msgstr or msgstr[0],
// or NULL when there's none.
const char* Po_find(const PoCatalog* catalog, const char* context, const char* msgid);

// The form of msgid's plural the catalog's rule picks for n, or NULL when
// there's none. A rule that fails (a division by zero, or an index past
// nplurals) picks as English does, n != 1.
const char* Po_findPlural(const PoCatalog* catalog, const char* context, const char* msgid, unsigned long n);

// 1 when text is valid UTF-8, by the launcher's rules (no overlong forms,
// surrogates or code points past U+10FFFF).
int Po_utf8(const char* text, size_t len);

#endif
