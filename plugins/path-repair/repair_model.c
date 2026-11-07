// Path repair, without the host. See repair_model.h.

#include <stdio.h>
#include <string.h>
#include "repair_model.h"

#define KEY_LEN 1024

// Latin letters with accents, as the plain letters they fold to. Covers
// Latin-1 and Latin Extended-A; a range maps every code point in it.
static const struct {
	unsigned lo, hi;
	const char* plain;
} folds[] = {
	{0xC0, 0xC5, "a"}, {0xC6, 0xC6, "ae"}, {0xC7, 0xC7, "c"}, {0xC8, 0xCB, "e"},
	{0xCC, 0xCF, "i"}, {0xD0, 0xD0, "d"}, {0xD1, 0xD1, "n"}, {0xD2, 0xD6, "o"},
	{0xD7, 0xD7, ""}, {0xD8, 0xD8, "o"}, {0xD9, 0xDC, "u"}, {0xDD, 0xDD, "y"},
	{0xDE, 0xDE, "th"}, {0xDF, 0xDF, "ss"}, {0xE0, 0xE5, "a"}, {0xE6, 0xE6, "ae"},
	{0xE7, 0xE7, "c"}, {0xE8, 0xEB, "e"}, {0xEC, 0xEF, "i"}, {0xF0, 0xF0, "d"},
	{0xF1, 0xF1, "n"}, {0xF2, 0xF6, "o"}, {0xF7, 0xF7, ""}, {0xF8, 0xF8, "o"},
	{0xF9, 0xFC, "u"}, {0xFD, 0xFD, "y"}, {0xFE, 0xFE, "th"}, {0xFF, 0xFF, "y"},
	{0x100, 0x105, "a"}, {0x106, 0x10D, "c"}, {0x10E, 0x111, "d"}, {0x112, 0x11B, "e"},
	{0x11C, 0x123, "g"}, {0x124, 0x127, "h"}, {0x128, 0x131, "i"}, {0x132, 0x133, "ij"},
	{0x134, 0x135, "j"}, {0x136, 0x138, "k"}, {0x139, 0x142, "l"}, {0x143, 0x14B, "n"},
	{0x14C, 0x151, "o"}, {0x152, 0x153, "oe"}, {0x154, 0x159, "r"}, {0x15A, 0x161, "s"},
	{0x162, 0x167, "t"}, {0x168, 0x173, "u"}, {0x174, 0x175, "w"}, {0x176, 0x178, "y"},
	{0x179, 0x17E, "z"}, {0x17F, 0x17F, "s"},
};

// What decode returns for a byte that doesn't start a valid character.
#define NOT_UTF8 0x110000

// Decodes the UTF-8 character at s, setting *len to its bytes. A byte that
// doesn't start a valid character is NOT_UTF8, one byte long.
static unsigned decode(const unsigned char* s, int* len) {
	unsigned c = s[0];
	*len = 1;
	if (c < 0x80) return c;
	if (c < 0xC0 || c >= 0xF8) return NOT_UTF8;
	int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
	unsigned cp = c & (0x3F >> (n - 1));
	for (int i = 1; i < n; i++) {
		if ((s[i] & 0xC0) != 0x80) return NOT_UTF8;
		cp = (cp << 6) | (s[i] & 0x3F);
	}
	*len = n;
	return cp;
}

// Appends text to out at *at, or marks it full.
static void append(char* out, size_t size, size_t* at, const char* text, size_t len) {
	if (*at + len >= size) {
		*at = size; // full
		return;
	}
	memcpy(out + *at, text, len);
	*at += len;
}

static int isAlnum(unsigned c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Folds name's first len bytes into out at *at.
static void fold(const char* name, size_t len, char* out, size_t size, size_t* at) {
	const unsigned char* s = (const unsigned char*)name;
	size_t i = 0;
	while (i < len && *at < size) {
		int n;
		unsigned cp = decode(s + i, &n);
		if (i + (size_t)n > len) n = (int)(len - i);
		if (cp < 0x80) {
			if (isAlnum(cp)) {
				char c = (char)(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp);
				append(out, size, at, &c, 1);
			}
		}
		else if (cp >= 0x300 && cp <= 0x36F) {
			// a combining accent, as in names written decomposed
		}
		else {
			const char* plain = NULL;
			for (size_t k = 0; k < sizeof(folds) / sizeof(folds[0]); k++) {
				if (cp >= folds[k].lo && cp <= folds[k].hi) {
					plain = folds[k].plain;
					break;
				}
			}
			if (plain) append(out, size, at, plain, strlen(plain));
			// other scripts, and bytes that aren't UTF-8, as they are
			else if (cp >= 0xA0) append(out, size, at, name + i, (size_t)n);
		}
		i += (size_t)n;
	}
}

// Where name's extension starts (its dot), or NULL: a dot after the first
// character followed by one to five letters or digits.
static const char* extension(const char* name) {
	const char* dot = strrchr(name, '.');
	if (!dot || dot == name) return NULL;
	size_t len = strlen(dot + 1);
	if (len < 1 || len > 5) return NULL;
	for (const char* c = dot + 1; *c; c++) {
		if (!isAlnum((unsigned char)*c)) return NULL;
	}
	return dot;
}

void Repair_key(const char* name, char* out, size_t size) {
	if (!out || !size) return;
	out[0] = '\0';
	if (!name) return;
	const char* ext = extension(name);
	size_t at = 0;
	fold(name, ext ? (size_t)(ext - name) : strlen(name), out, size, &at);
	if (ext) {
		append(out, size, &at, ".", 1);
		fold(ext + 1, strlen(ext + 1), out, size, &at);
	}
	if (at >= size) at = 0; // too long to match safely
	out[at] = '\0';
}

int Repair_pick(const char* missing, const char* const* names, int count) {
	char want[KEY_LEN];
	Repair_key(missing, want, sizeof(want));
	// a name of only punctuation, or only an extension, matches too much
	if (!want[0] || want[0] == '.') return -1;
	int found = -1;
	for (int i = 0; i < count; i++) {
		if (!names[i] || !strcmp(names[i], missing)) continue;
		char key[KEY_LEN];
		Repair_key(names[i], key, sizeof(key));
		if (strcmp(key, want) != 0) continue;
		if (found >= 0) return -1;
		found = i;
	}
	return found;
}

size_t Repair_fixedLength(const char* rel) {
	if (!rel || rel[0] != '/') return 0;
	const char* second = strchr(rel + 1, '/');
	if (!second || second == rel + 1) return 0;
	const char* below = strchr(second + 1, '/');
	if (!below || below == second + 1 || !below[1]) return 0;
	return (size_t)(below - rel);
}
